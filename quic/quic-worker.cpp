#include "quic-worker.h"

#include <atomic>
#include <chrono>
#include <cstring>
#include <liburing.h>
#include <netinet/in.h>
#include <netinet/udp.h>
#include <poll.h>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <unistd.h>

#include "td/actor/actor.h"
#include "td/utils/Timer.h"
#include "td/utils/logging.h"

#include "quic-pimpl.h"

// User-data tagging for io_uring SQEs. Top bits identify the SQE type; low
// bits hold the slot index (where applicable).
namespace {
constexpr uint64_t kTagRecv = 1ULL << 60;
constexpr uint64_t kTagSend = 2ULL << 60;
constexpr uint64_t kTagPoll = 3ULL << 60;
constexpr uint64_t kTagMask = (1ULL << 60) - 1;
inline uint64_t make_tag(uint64_t kind, size_t idx) {
  return kind | (static_cast<uint64_t>(idx) & kTagMask);
}
inline uint64_t tag_kind(uint64_t v) {
  return v & ~kTagMask;
}
inline size_t tag_idx(uint64_t v) {
  return static_cast<size_t>(v & kTagMask);
}
}  // namespace

namespace ton::quic {

namespace {

constexpr ngtcp2_duration RETRY_TOKEN_TIMEOUT = 10 * NGTCP2_SECONDS;

ngtcp2_tstamp retry_token_now() {
  return static_cast<ngtcp2_tstamp>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch())
          .count());
}

}  // namespace

class QuicWorker::PImplCallback final : public QuicConnectionPImpl::Callback {
 public:
  explicit PImplCallback(QuicWorker& worker, bool is_outbound)
      : worker_(worker), sink_(*worker.sink_), is_outbound_(is_outbound) {
  }

  void set_connection_id(QuicConnectionId cid) override {
    cid_ = cid;
  }
  void on_local_cid_issued(QuicConnectionId cid) override {
    worker_.on_local_cid_issued(cid_, cid);
  }
  void on_local_cid_retired(QuicConnectionId cid) override {
    worker_.on_local_cid_retired(cid_, cid);
  }

  void on_handshake_completed(HandshakeCompletedEvent event) override {
    sink_.on_connected(cid_, std::move(event.local_public_key), std::move(event.peer_public_key), is_outbound_);
  }

  td::Status on_stream_data(StreamDataEvent event) override {
    // Wire format: 4-byte little-endian length header at the start of every
    // logical message. With the header known up-front, the receive buffer is
    // allocated exactly once at the exact size — no doubling, no copy-on-grow,
    // no page-fault tax beyond the unavoidable first-touch of those pages.
    auto it = inbound_streams_.find(event.sid);
    if (it == inbound_streams_.end()) {
      auto [ins_it, _] = inbound_streams_.emplace(event.sid, ExactBuffer{});
      it = ins_it;
    }
    auto& eb = it->second;
    td::Slice data = event.data;

    // Finish the header if we don't have it yet.
    while (eb.header_filled < 4 && !data.empty()) {
      eb.header_buf[eb.header_filled++] = static_cast<uint8_t>(data[0]);
      data.remove_prefix(1);
    }
    if (eb.header_filled == 4 && !eb.allocated) {
      uint32_t total = static_cast<uint32_t>(eb.header_buf[0]) |
                       (static_cast<uint32_t>(eb.header_buf[1]) << 8) |
                       (static_cast<uint32_t>(eb.header_buf[2]) << 16) |
                       (static_cast<uint32_t>(eb.header_buf[3]) << 24);
      eb.storage = td::BufferSlice(total);
      eb.total = total;
      eb.allocated = true;
    }
    if (eb.allocated && !data.empty()) {
      // Defensive — should never overflow given the header was right.
      size_t to_copy = std::min<size_t>(data.size(), eb.total - eb.written);
      std::memcpy(eb.storage.as_slice().begin() + eb.written, data.data(), to_copy);
      eb.written += to_copy;
    }
    if (event.fin) {
      // The user expects a BufferSlice sized to exactly the payload.
      // If the peer terminated the stream early (FIN before the announced length
      // landed), surface what we got, truncated to written.
      td::BufferSlice out = std::move(eb.storage);
      if (eb.written != eb.total) {
        out.truncate(eb.written);
      }
      sink_.on_stream(cid_, event.sid, std::move(out), true);
      inbound_streams_.erase(it);
    }
    return td::Status::OK();
  }
  void on_stream_closed(QuicStreamID sid) override {
    inbound_streams_.erase(sid);
    sink_.on_stream_closed(cid_, sid);
  }

 private:
  // Per-stream state for length-prefixed reception. The 4-byte header arrives
  // as part of the first fragment (almost always all four bytes at once); we
  // allocate `storage` once at exact final size and copy each fragment into it.
  struct ExactBuffer {
    uint8_t header_buf[4]{};
    uint8_t header_filled = 0;
    bool allocated = false;
    uint32_t total = 0;
    size_t written = 0;
    td::BufferSlice storage;
  };

  QuicWorker& worker_;
  QuicWorkerEventSink& sink_;
  QuicConnectionId cid_;
  bool is_outbound_;
  std::unordered_map<QuicStreamID, ExactBuffer> inbound_streams_;
};

QuicWorker::QuicWorker(td::UdpSocketFd fd, td::BufferSlice alpn, std::unique_ptr<QuicWorkerEventSink> sink,
                       Options options)
    : fd_(std::move(fd))
    , alpn_(std::move(alpn))
    , identities_(td::make_ref<ServerIdentities>())
    , options_(options)
    , conn_rate_limiters_(options.new_connection_rate_limit_capacity, options.new_connection_rate_limit_period)
    , global_conn_rate_limiter_(options.global_new_connection_rate_limit_capacity,
                                options.global_new_connection_rate_limit_period)
    , gso_enabled_(options.enable_gso && td::UdpSocketFd::is_gso_supported())
    , sink_(std::move(sink)) {
  td::Random::secure_bytes(td::MutableSlice(retry_secret_.data(), retry_secret_.size()));
  if (options.enable_gro) {
    auto gro_status = fd_.enable_gro();
    if (gro_status.is_ok()) {
      gro_enabled_ = true;
    } else {
      LOG(DEBUG) << "UDP_GRO not enabled: " << gro_status;
    }
  }
  if (!options.enable_mmsg) {
    fd_.disable_mmsg();
  } else {
    fd_.enable_mmsg();
  }
  LOG(INFO) << "UDP allowed: GRO=" << (gro_enabled_ ? "on" : "off") << " GSO=" << (gso_enabled_ ? "on" : "off")
            << " MMSG=" << (fd_.is_mmsg_enabled() ? "on" : "off") << " CC=" << options_.cc_algo
            << " Retry=" << (options_.stateless_retry ? "on" : "off");

  // Ingress/egress buffers live in the io_uring slot pools; allocated in run_loop().
  cmd_queue_.init();
}

QuicWorker::~QuicWorker() {
  if (started_.load()) {
    stop();
  }
  cmd_queue_.destroy();
}

void QuicWorker::start(std::string thread_name) {
  CHECK(!started_.exchange(true));
  thread_ = std::thread([this, name = std::move(thread_name)]() {
#ifdef __linux__
    pthread_setname_np(pthread_self(), name.substr(0, 15).c_str());
#endif
    run_loop();
  });
}

void QuicWorker::stop() {
  if (!started_.load()) {
    return;
  }
  stop_requested_.store(true);
  // Kick the worker thread.
  post([] {});
  if (thread_.joinable()) {
    thread_.join();
  }
  started_.store(false);
}

void QuicWorker::post(UniqueFn fn) {
  cmd_queue_.writer_put(std::move(fn));
  cmd_queue_.writer_flush();
}

void QuicWorker::on_connection_updated(ConnectionState& state) {
  if (!state.in_active_queue) {
    state.in_active_queue = true;
    active_connections_.push_back(state.cid);
  }

  double key = state.impl().get_expiry_timestamp().at();
  if (state.in_heap()) {
    timeout_heap_.fix(key, &state);
  } else {
    timeout_heap_.insert(key, &state);
  }
}

void QuicWorker::bind_cid(const QuicConnectionId& primary_cid, const QuicConnectionId& cid) {
  auto connection = find_connection(primary_cid);
  LOG_CHECK(connection) << "Can't bind CID for unknown primary cid " << primary_cid;
  LOG_CHECK(!cid_to_primary_cid_.contains(cid)) << "CID collision while binding " << cid << " to " << primary_cid;

  auto [_, routed_inserted] = connection->routed_cids.insert(cid);
  LOG_CHECK(routed_inserted) << "Duplicate routed CID " << cid << " for primary " << primary_cid;

  auto [__, mapping_inserted] = cid_to_primary_cid_.emplace(cid, primary_cid);
  LOG_CHECK(mapping_inserted) << "Failed to insert CID mapping " << cid << " -> " << primary_cid;
}

void QuicWorker::unbind_cid(const QuicConnectionId& primary_cid, const QuicConnectionId& cid) {
  auto connection = find_connection(primary_cid);
  LOG_CHECK(connection) << "Can't unbind CID for unknown primary cid " << primary_cid;

  auto it = cid_to_primary_cid_.find(cid);
  LOG_CHECK(it != cid_to_primary_cid_.end()) << "Missing CID mapping for " << cid;
  LOG_CHECK(it->second == primary_cid) << "CID " << cid << " is mapped to " << it->second << ", not " << primary_cid;

  auto erased = connection->routed_cids.erase(cid);
  LOG_CHECK(erased == 1) << "Missing routed CID " << cid << " for primary " << primary_cid;
  cid_to_primary_cid_.erase(it);
}

void QuicWorker::unbind_all_cids(ConnectionState& state) {
  if (state.bootstrap_routed_cid.has_value()) {
    auto bootstrap_it = bootstrap_routes_.find(
        BootstrapRouteKey{.remote_address = state.remote_address, .routed_cid = *state.bootstrap_routed_cid});
    LOG_CHECK(bootstrap_it != bootstrap_routes_.end())
        << "Missing bootstrap route " << *state.bootstrap_routed_cid << " from " << state.remote_address;
    LOG_CHECK(bootstrap_it->second == state.cid)
        << "Bootstrap route " << *state.bootstrap_routed_cid << " from " << state.remote_address << " is mapped to "
        << bootstrap_it->second << ", not " << state.cid;
    bootstrap_routes_.erase(bootstrap_it);
  }
  for (const auto& cid : state.routed_cids) {
    auto it = cid_to_primary_cid_.find(cid);
    LOG_CHECK(it != cid_to_primary_cid_.end()) << "Missing CID mapping for " << cid;
    LOG_CHECK(it->second == state.cid) << "CID " << cid << " is mapped to " << it->second << ", not " << state.cid;
    cid_to_primary_cid_.erase(it);
  }
  state.routed_cids.clear();
}

td::Result<std::shared_ptr<QuicWorker::ConnectionState>> QuicWorker::install_connection(
    std::unique_ptr<QuicConnectionPImpl> p_impl, const td::IPAddress& remote_address, bool is_outbound,
    std::optional<QuicConnectionId> bootstrap_routed_cid) {
  TRY_RESULT(initial_cid_state, p_impl->take_initial_cid_state());

  auto state = std::make_shared<ConnectionState>(ConnectionState{
      .impl_ = std::move(p_impl),
      .remote_address = remote_address,
      .cid = initial_cid_state.primary_scid,
      .bootstrap_routed_cid = {},
      .routed_cids = {},
      .is_outbound = is_outbound,
  });
  LOG(INFO) << "creating " << *state;

  auto [_, inserted] = connections_.emplace(state->cid, state);
  LOG_CHECK(inserted) << "Duplicate primary CID " << state->cid;

  if (bootstrap_routed_cid.has_value()) {
    auto [__, bootstrap_inserted] = bootstrap_routes_.emplace(
        BootstrapRouteKey{.remote_address = remote_address, .routed_cid = *bootstrap_routed_cid}, state->cid);
    LOG_CHECK(bootstrap_inserted) << "Duplicate bootstrap route " << *bootstrap_routed_cid << " from "
                                  << remote_address;
    state->bootstrap_routed_cid = bootstrap_routed_cid;
  }

  std::set<QuicConnectionId> startup_cids;
  startup_cids.insert(state->cid);
  for (const auto& local_cid : initial_cid_state.scids) {
    startup_cids.insert(local_cid);
  }

  for (const auto& cid : startup_cids) {
    bind_cid(state->cid, cid);
  }

  return state;
}

td::Status QuicWorker::ensure_flood_allowed(const std::string& flood_addr) {
  if (!options_.flood_control.has_value()) {
    return td::Status::OK();
  }
  if (auto it = flood_map_.find(flood_addr); it != flood_map_.end() && it->second >= *options_.flood_control) {
    return td::Status::Error("flood control overflow");
  }
  TRY_STATUS(conn_rate_limiters_.take_new_connection(flood_addr));
  if (!global_conn_rate_limiter_.take()) {
    return td::Status::Error("global new connection rate limit exceeded");
  }
  return td::Status::OK();
}

void QuicWorker::flood_on_inbound_connection_created(const std::string& flood_addr) {
  if (!options_.flood_control.has_value()) {
    return;
  }
  flood_map_[flood_addr]++;
}

void QuicWorker::flood_on_inbound_connection_closed(const std::string& flood_addr) {
  if (!options_.flood_control.has_value()) {
    return;
  }
  auto it = flood_map_.find(flood_addr);
  if (it == flood_map_.end()) {
    return;
  }
  if (--it->second == 0) {
    flood_map_.erase(it);
  }
}

QuicConnectionOptions QuicWorker::build_connection_options() const {
  QuicConnectionOptions conn_options;
  conn_options.cc_algo = options_.cc_algo;
  if (options_.max_streams_bidi.has_value()) {
    conn_options.max_streams_bidi = *options_.max_streams_bidi;
  }
  return conn_options;
}

void QuicWorker::on_local_cid_issued(const QuicConnectionId& primary_cid, const QuicConnectionId& cid) {
  bind_cid(primary_cid, cid);
}

void QuicWorker::on_local_cid_retired(const QuicConnectionId& primary_cid, const QuicConnectionId& cid) {
  unbind_cid(primary_cid, cid);
}

td::Result<std::optional<ServerInitialInfo>> QuicWorker::prepare_server_initial_info(
    const VersionCid& initial_packet, const td::IPAddress& remote_address) {
  ServerInitialInfo initial_info{
      .packet = initial_packet,
      .original_dcid = initial_packet.dcid,
      .retry_scid = std::nullopt,
  };

  if (!options_.stateless_retry) {
    return std::optional<ServerInitialInfo>(std::move(initial_info));
  }

  if (initial_packet.token.empty()) {
    TRY_STATUS(send_retry(initial_packet, remote_address));
    return std::optional<ServerInitialInfo>{};
  }

  auto original_dcid = verify_retry_token(initial_packet, remote_address);
  if (original_dcid.is_error()) {
    LOG(DEBUG) << "invalid Retry token from " << remote_address << ": " << original_dcid.error();
    TRY_STATUS(send_invalid_token_connection_close(initial_packet, remote_address));
    return std::optional<ServerInitialInfo>{};
  }

  initial_info.original_dcid = original_dcid.move_as_ok();
  initial_info.retry_scid = initial_packet.dcid;
  return std::optional<ServerInitialInfo>(std::move(initial_info));
}

td::Result<QuicConnectionId> QuicWorker::verify_retry_token(const VersionCid& packet,
                                                            const td::IPAddress& remote_address) const {
  CHECK(!packet.token.empty());

  auto packet_dcid = QuicConnectionIdAccess::to_ngtcp2(packet.dcid);
  ngtcp2_cid original_dcid{};
  int rv = ngtcp2_crypto_verify_retry_token2(
      &original_dcid, reinterpret_cast<const uint8_t*>(packet.token.data()), packet.token.size(), retry_secret_.data(),
      retry_secret_.size(), packet.version, reinterpret_cast<const ngtcp2_sockaddr*>(remote_address.get_sockaddr()),
      static_cast<ngtcp2_socklen>(remote_address.get_sockaddr_len()), &packet_dcid, RETRY_TOKEN_TIMEOUT,
      retry_token_now());
  switch (rv) {
    case 0:
      return QuicConnectionIdAccess::from_ngtcp2(original_dcid);
    case NGTCP2_CRYPTO_ERR_VERIFY_TOKEN:
      return td::Status::Error("retry token verification failed");
    case NGTCP2_CRYPTO_ERR_UNREADABLE_TOKEN:
      return td::Status::Error("retry token is unreadable");
    default:
      return td::Status::Error(PSTRING() << "retry token validation failed: " << rv);
  }
}

td::Status QuicWorker::send_stateless_datagram(td::Slice packet_kind, const td::IPAddress& remote_address,
                                               td::Slice data) {
  td::UdpSocketFd::OutboundMessage message{.to = &remote_address, .data = data, .gso_size = 0};
  bool is_sent = false;
  auto status = fd_.send_message(message, is_sent);
  egress_stats_.syscalls++;
  if (is_sent) {
    egress_stats_.packets++;
    egress_stats_.bytes += data.size();
  }
  if (status.is_error()) {
    return status;
  }
  if (!is_sent) {
    LOG(DEBUG) << "dropping stateless " << packet_kind << " to " << remote_address << ": send_message blocked";
    return td::Status::OK();
  }
  return td::Status::OK();
}

td::Status QuicWorker::send_retry(const VersionCid& packet, const td::IPAddress& remote_address) {
  auto client_scid = QuicConnectionIdAccess::to_ngtcp2(packet.scid);
  auto original_dcid = QuicConnectionIdAccess::to_ngtcp2(packet.dcid);
  auto retry_scid = QuicConnectionIdAccess::to_ngtcp2(QuicConnectionId::random());

  std::array<uint8_t, NGTCP2_CRYPTO_MAX_RETRY_TOKENLEN2> token;
  auto tokenlen = ngtcp2_crypto_generate_retry_token2(
      token.data(), retry_secret_.data(), retry_secret_.size(), packet.version,
      reinterpret_cast<const ngtcp2_sockaddr*>(remote_address.get_sockaddr()),
      static_cast<ngtcp2_socklen>(remote_address.get_sockaddr_len()), &retry_scid, &original_dcid, retry_token_now());
  if (tokenlen < 0) {
    return td::Status::Error("failed to generate retry token");
  }

  std::array<uint8_t, NGTCP2_MAX_UDP_PAYLOAD_SIZE> datagram;
  auto datagram_size = ngtcp2_crypto_write_retry(datagram.data(), datagram.size(), packet.version, &client_scid,
                                                 &retry_scid, &original_dcid, token.data(), tokenlen);
  if (datagram_size < 0) {
    return td::Status::Error("failed to write retry packet");
  }

  LOG(DEBUG) << "sending Retry to " << remote_address << " for original dcid=" << packet.dcid;
  return send_stateless_datagram(
      "Retry", remote_address,
      td::Slice(reinterpret_cast<const char*>(datagram.data()), static_cast<size_t>(datagram_size)));
}

td::Status QuicWorker::send_invalid_token_connection_close(const VersionCid& packet,
                                                           const td::IPAddress& remote_address) {
  auto client_scid = QuicConnectionIdAccess::to_ngtcp2(packet.scid);
  auto original_dcid = QuicConnectionIdAccess::to_ngtcp2(packet.dcid);

  std::array<uint8_t, NGTCP2_MAX_UDP_PAYLOAD_SIZE> datagram;
  auto datagram_size = ngtcp2_crypto_write_connection_close(
      datagram.data(), datagram.size(), packet.version, &client_scid, &original_dcid, NGTCP2_INVALID_TOKEN, nullptr, 0);
  if (datagram_size < 0) {
    return td::Status::Error("failed to write stateless connection close");
  }

  LOG(DEBUG) << "sending invalid-token connection close to " << remote_address;
  return send_stateless_datagram(
      "invalid-token connection close", remote_address,
      td::Slice(reinterpret_cast<const char*>(datagram.data()), static_cast<size_t>(datagram_size)));
}

std::shared_ptr<QuicWorker::ConnectionState> QuicWorker::find_connection(const QuicConnectionId& cid) {
  if (auto it = connections_.find(cid); it != connections_.end()) {
    return it->second;
  }
  return nullptr;
}

bool QuicWorker::handle_expiry(ConnectionState& state) {
  if (!state.impl().is_expired()) {
    on_connection_updated(state);
    return false;
  }
  auto R = state.impl().handle_expiry();
  if (R.is_error()) {
    LOG(INFO) << "expiry error: " << R.error();
    return true;
  }
  switch (R.ok()) {
    case QuicConnectionPImpl::ExpiryAction::None:
      LOG(DEBUG) << "expiry None for " << state.remote_address;
      return false;
    case QuicConnectionPImpl::ExpiryAction::ScheduleWrite:
      LOG(DEBUG) << "expiry ScheduleWrite for " << state.remote_address;
      on_connection_updated(state);
      return false;
    case QuicConnectionPImpl::ExpiryAction::IdleClose:
      LOG(INFO) << "expiry IdleClose for " << state.remote_address;
      return true;
    case QuicConnectionPImpl::ExpiryAction::Close:
      LOG(INFO) << "expiry Close for " << state.remote_address;
      on_connection_updated(state);
      return true;
  }
  return true;
}

void QuicWorker::handle_timeouts() {
  double now = td::Timestamp::now().at();
  while (!timeout_heap_.empty() && timeout_heap_.top_key() <= now) {
    auto* state = static_cast<ConnectionState*>(timeout_heap_.pop());
    if (handle_expiry(*state)) {
      to_erase_connections_.push_back(state->cid);
    }
  }
  // User-level stream timeouts are now driven from the QuicServer actor side.
  {
    td::PerfWarningTimer w("cleanup_conn_rate_limiters", 0.1);
    conn_rate_limiters_.cleanup();
  }
}

void QuicWorker::erase_pending_connections() {
  for (auto cid : to_erase_connections_) {
    wt_on_connection_closed(cid);
  }
  to_erase_connections_.clear();
}

void QuicWorker::wt_log_stats(std::string reason) {
  LOG(INFO) << "quic stats (" << reason << "): udp ingress{syscalls=" << ingress_stats_.syscalls
            << " packets=" << ingress_stats_.packets << " bytes=" << ingress_stats_.bytes
            << "} egress{syscalls=" << egress_stats_.syscalls << " packets=" << egress_stats_.packets
            << " bytes=" << egress_stats_.bytes << "}";
  if (connections_.empty()) {
    return;
  }
  for (auto& [cid, state] : connections_) {
    log_conn_stats(*state, reason.c_str());
  }
}

void QuicWorker::wt_add_identity(adnl::AdnlNodeIdShort local_id, td::Ed25519::PrivateKey key) {
  CHECK(identities_.not_null());
  auto sni = compute_sni_name(local_id);
  if (identities_->by_sni.contains(sni)) {
    return;
  }

  auto next = td::make_ref<ServerIdentities>();
  auto& next_w = next.unique_write();
  for (const auto& [existing_sni, entry] : identities_->by_sni) {
    next_w.by_sni.emplace(existing_sni, entry.clone());
  }
  next_w.by_sni.emplace(sni, ServerIdentities::Entry{.local_id = local_id, .key = std::move(key)});
  next_w.default_sni = identities_->default_sni.has_value() ? identities_->default_sni : std::optional{sni};
  identities_ = std::move(next);
  LOG(INFO) << "QuicWorker: registered identity " << local_id << " (now hosting " << identities_->by_sni.size()
            << " identities)";
}

void QuicWorker::log_conn_stats(ConnectionState& state, const char* reason) {
  constexpr double kNsToMs = 1e-6;
  auto info = state.impl().get_conn_info();
  double loss_pct =
      info.pkt_sent ? (100.0 * static_cast<double>(info.pkt_lost) / static_cast<double>(info.pkt_sent)) : 0.0;
  LOG(INFO) << "quic stats (" << reason << ") for " << state.remote_address << " cid=" << state.cid
            << " rtt_ms{smoothed=" << static_cast<double>(info.smoothed_rtt) * kNsToMs
            << " min=" << static_cast<double>(info.min_rtt) * kNsToMs
            << " latest=" << static_cast<double>(info.latest_rtt) * kNsToMs
            << " var=" << static_cast<double>(info.rttvar) * kNsToMs << "}"
            << " cwnd=" << info.cwnd << " inflight=" << info.bytes_in_flight << " sent=" << info.pkt_sent << "/"
            << info.bytes_sent << " recv=" << info.pkt_recv << "/" << info.bytes_recv << " lost=" << info.pkt_lost
            << "/" << info.bytes_lost << " loss=" << loss_pct << "%";
}

td::Result<std::shared_ptr<QuicWorker::ConnectionState>> QuicWorker::get_or_create_connection(
    const UdpMessageBuffer& msg_in) {
  TRY_RESULT(vc, VersionCid::from_datagram(td::Slice(msg_in.storage)));

  if (auto it = cid_to_primary_cid_.find(vc.dcid); it != cid_to_primary_cid_.end()) {
    auto connection = find_connection(it->second);
    LOG_CHECK(connection) << "Found stale CID mapping " << vc.dcid << " -> " << it->second;
    return connection;
  }

  auto bootstrap_key = BootstrapRouteKey{.remote_address = msg_in.address, .routed_cid = vc.dcid};
  if (auto it = bootstrap_routes_.find(bootstrap_key); it != bootstrap_routes_.end()) {
    auto connection = find_connection(it->second);
    LOG_CHECK(connection) << "Found stale bootstrap route " << vc.dcid << " from " << msg_in.address << " -> "
                          << it->second;
    return connection;
  }

  TRY_RESULT(initial_packet, VersionCid::from_initial_datagram(td::Slice(msg_in.storage)));

  auto flood_addr = msg_in.address.get_ip_host();
  TRY_STATUS(ensure_flood_allowed(flood_addr));

  TRY_RESULT(initial_info, prepare_server_initial_info(initial_packet, msg_in.address));
  if (!initial_info.has_value()) {
    return std::shared_ptr<ConnectionState>{};
  }

  TRY_RESULT(local_address, fd_.get_local_address());

  auto conn_options = build_connection_options();
  auto pimpl_callback = std::make_unique<PImplCallback>(*this, false);
  TRY_RESULT(p_impl, QuicConnectionPImpl::create_server(local_address, msg_in.address, identities_, alpn_.as_slice(),
                                                        *initial_info, std::move(pimpl_callback), conn_options));
  TRY_RESULT(state, install_connection(std::move(p_impl), msg_in.address, false, initial_packet.dcid));

  flood_on_inbound_connection_created(flood_addr);

  return state;
}

void QuicWorker::wt_connect(td::IPAddress remote_address, td::Ed25519::PrivateKey client_key, std::string alpn,
                            std::string sni, td::Promise<QuicConnectionId> promise) {
  auto local_address_r = fd_.get_local_address();
  if (local_address_r.is_error()) {
    promise.set_error(local_address_r.move_as_error());
    return;
  }
  auto local_address = local_address_r.move_as_ok();

  auto conn_options = build_connection_options();
  auto pimpl_callback = std::make_unique<PImplCallback>(*this, true);
  auto p_impl_r = QuicConnectionPImpl::create_client(local_address, remote_address, std::move(client_key),
                                                     td::Slice(alpn), td::Slice(sni), std::move(pimpl_callback),
                                                     conn_options);
  if (p_impl_r.is_error()) {
    promise.set_error(p_impl_r.move_as_error());
    return;
  }
  auto state_r = install_connection(p_impl_r.move_as_ok(), remote_address, true, std::nullopt);
  if (state_r.is_error()) {
    promise.set_error(state_r.move_as_error());
    return;
  }
  auto state = state_r.move_as_ok();
  on_connection_updated(*state);
  promise.set_value(QuicConnectionId(state->cid));
}


void QuicWorker::wt_shutdown_stream(QuicConnectionId cid, QuicStreamID sid) {
  auto state = find_connection(cid);
  if (!state) {
    return;
  }
  state->impl().shutdown_stream(sid);
  on_connection_updated(*state);
}

void QuicWorker::wt_collect_stats(td::Promise<Stats> P) {
  Stats stats;
  for (auto& [id, conn] : connections_) {
    Stats::Entry entry{.total_conns = 1, .impl_stats = conn->impl_->get_stats()};
    stats.summary = stats.summary + entry;
    stats.per_conn[id] = entry;
  }
  P.set_value(std::move(stats));
}

void QuicWorker::wt_on_connection_closed(QuicConnectionId cid) {
  auto it = connections_.find(cid);
  if (it == connections_.end()) {
    LOG(WARNING) << "Can't find connection for closing " << cid;
    return;
  }
  auto state = it->second;
  LOG(INFO) << "Close connection: " << *state;
  unbind_all_cids(*state);
  if (state->in_heap()) {
    timeout_heap_.erase(state.get());
  }
  if (!state->is_outbound) {
    flood_on_inbound_connection_closed(state->remote_address.get_ip_host());
  }
  connections_.erase(it);
  sink_->on_closed(cid);
}

void QuicWorker::wt_send_stream(QuicConnectionId cid, std::variant<QuicStreamID, StreamOptions> stream,
                                td::BufferSlice data, bool is_end, td::Promise<QuicStreamID> promise) {
  auto state = find_connection(cid);
  if (!state) {
    promise.set_error(td::Status::Error("Connection not found"));
    return;
  }

  QuicStreamID sid;
  if (auto* existing = std::get_if<QuicStreamID>(&stream)) {
    sid = *existing;
  } else {
    auto sid_r = state->impl().open_stream();
    if (sid_r.is_error()) {
      promise.set_error(sid_r.move_as_error());
      return;
    }
    sid = sid_r.move_as_ok();
    auto& options = std::get<StreamOptions>(stream);
    // The user-side set_stream_options notification runs on the actor thread;
    // we just apply ngtcp2 credit here.
    if (options.max_size.has_value()) {
      state->impl().set_stream_receive_credit_from_max_size(sid, *options.max_size);
    }
  }

  // Wire format: every logical message is prefixed by a 4-byte little-endian
  // length header. The receiver uses this to pre-allocate the exact buffer up
  // front, avoiding growable-buffer reallocations and page-faulting fresh
  // pages on every doubling. This means every send_stream call MUST be the
  // complete message (is_end=true); incremental send_stream_data is gone.
  uint8_t hdr[4];
  uint32_t len = static_cast<uint32_t>(data.size());
  hdr[0] = static_cast<uint8_t>(len);
  hdr[1] = static_cast<uint8_t>(len >> 8);
  hdr[2] = static_cast<uint8_t>(len >> 16);
  hdr[3] = static_cast<uint8_t>(len >> 24);

  auto status = state->impl().buffer_stream(sid, td::Slice(hdr, 4), std::move(data), is_end);
  if (status.is_error()) {
    promise.set_error(std::move(status));
    return;
  }
  on_connection_updated(*state);
  promise.set_value(QuicStreamID(sid));
}

void QuicWorker::wt_open_stream(QuicConnectionId cid, StreamOptions options, td::Promise<QuicStreamID> promise) {
  auto state = find_connection(cid);
  if (!state) {
    promise.set_error(td::Status::Error("Connection not found"));
    return;
  }
  auto sid_r = state->impl().open_stream();
  if (sid_r.is_error()) {
    promise.set_error(sid_r.move_as_error());
    return;
  }
  auto sid = sid_r.move_as_ok();
  if (options.max_size.has_value()) {
    state->impl().set_stream_receive_credit_from_max_size(sid, *options.max_size);
  }
  on_connection_updated(*state);
  promise.set_value(QuicStreamID(sid));
}

void QuicWorker::drain_commands() {
  while (true) {
    int n = cmd_queue_.reader_wait_nonblock();
    if (n == 0) {
      return;
    }
    for (int i = 0; i < n; i++) {
      auto fn = cmd_queue_.reader_get_unsafe();
      fn();
    }
    cmd_queue_.reader_flush();
  }
}

// --- io_uring fast path ---

void QuicWorker::uring_setup_recv_buf_ring() {
  // Per-buffer layout: io_uring_recvmsg_out + sockaddr + cmsg + payload.
  recv_buf_stride_ = sizeof(io_uring_recvmsg_out) + kRecvBufNamelen + kRecvBufCmsglen + kRecvBufPayloadCap;
  recv_buf_storage_.assign(kNumRecvBufs * recv_buf_stride_, 0);

  int err = 0;
  recv_buf_ring_ = io_uring_setup_buf_ring(ring_, static_cast<unsigned int>(kNumRecvBufs), kRecvBufGroupId, 0, &err);
  if (recv_buf_ring_ == nullptr) {
    LOG(FATAL) << "io_uring_setup_buf_ring failed: " << strerror(-err);
  }
  int mask = io_uring_buf_ring_mask(kNumRecvBufs);
  for (size_t i = 0; i < kNumRecvBufs; ++i) {
    io_uring_buf_ring_add(recv_buf_ring_, recv_buf_storage_.data() + i * recv_buf_stride_,
                          static_cast<unsigned int>(recv_buf_stride_), static_cast<unsigned short>(i), mask,
                          static_cast<int>(i));
  }
  io_uring_buf_ring_advance(recv_buf_ring_, static_cast<int>(kNumRecvBufs));

  // The msghdr template carries only name/control length expectations — the
  // kernel uses the buffer ring for actual storage.
  std::memset(&recv_msg_template_, 0, sizeof(recv_msg_template_));
  recv_msg_template_.msg_namelen = kRecvBufNamelen;
  recv_msg_template_.msg_controllen = kRecvBufCmsglen;
}

void QuicWorker::uring_submit_multishot_recv() {
  auto* sqe = io_uring_get_sqe(ring_);
  CHECK(sqe != nullptr);
  io_uring_prep_recvmsg_multishot(sqe, udp_fd_, &recv_msg_template_, 0);
  sqe->flags |= IOSQE_BUFFER_SELECT;
  sqe->buf_group = kRecvBufGroupId;
  io_uring_sqe_set_data64(sqe, kTagRecv);
  ++pending_sqes_;
  multishot_recv_armed_ = true;
}

void QuicWorker::uring_return_buffer(unsigned short buf_id) {
  int mask = io_uring_buf_ring_mask(kNumRecvBufs);
  io_uring_buf_ring_add(recv_buf_ring_, recv_buf_storage_.data() + buf_id * recv_buf_stride_,
                        static_cast<unsigned int>(recv_buf_stride_), buf_id, mask, 0);
  io_uring_buf_ring_advance(recv_buf_ring_, 1);
}

void QuicWorker::uring_submit_send(size_t slot_idx) {
  auto& s = send_slots_[slot_idx];
  auto* sqe = io_uring_get_sqe(ring_);
  CHECK(sqe != nullptr);
  io_uring_prep_sendmsg(sqe, udp_fd_, &s.hdr, 0);
  io_uring_sqe_set_data64(sqe, make_tag(kTagSend, slot_idx));
  ++pending_sqes_;
}

void QuicWorker::uring_submit_poll_cmd() {
  auto* sqe = io_uring_get_sqe(ring_);
  CHECK(sqe != nullptr);
  io_uring_prep_poll_multishot(sqe, cmd_fd_, POLLIN);
  io_uring_sqe_set_data64(sqe, kTagPoll);
  ++pending_sqes_;
  cmd_poll_armed_ = true;
}

void QuicWorker::uring_handle_recv_cqe(int res, unsigned flags) {
  if (res <= 0) {
    if (res < 0 && res != -EAGAIN && res != -EINTR && res != -ENOBUFS) {
      LOG(DEBUG) << "recvmsg cqe error: " << strerror(-res);
    }
    // If the multishot terminated, we need to resubmit it after this loop.
    if (!(flags & IORING_CQE_F_MORE)) {
      multishot_recv_armed_ = false;
    }
    return;
  }
  if (!(flags & IORING_CQE_F_BUFFER)) {
    LOG(WARNING) << "multishot recvmsg cqe missing buffer flag";
    if (!(flags & IORING_CQE_F_MORE)) {
      multishot_recv_armed_ = false;
    }
    return;
  }
  unsigned short buf_id = static_cast<unsigned short>(flags >> IORING_CQE_BUFFER_SHIFT);
  void* buf = recv_buf_storage_.data() + buf_id * recv_buf_stride_;
  bool last = !(flags & IORING_CQE_F_MORE);

  io_uring_recvmsg_out* o = io_uring_recvmsg_validate(buf, res, &recv_msg_template_);
  if (o == nullptr) {
    LOG(WARNING) << "recvmsg_validate failed";
    uring_return_buffer(buf_id);
    if (last) {
      multishot_recv_armed_ = false;
    }
    return;
  }
  // Truncated by either name or control overflow — we sized the buffer to fit,
  // but be defensive about kernel-truncation flags.
  if (o->flags & (MSG_TRUNC | MSG_CTRUNC)) {
    LOG(DEBUG) << "recvmsg truncated, flags=" << o->flags;
  }

  ingress_stats_.syscalls++;  // counts CQEs now, not actual syscalls
  ingress_stats_.bytes += io_uring_recvmsg_payload_length(o, res, &recv_msg_template_);

  // Source address.
  td::IPAddress src;
  void* name = io_uring_recvmsg_name(o);
  src.init_sockaddr(reinterpret_cast<sockaddr*>(name), o->namelen).ignore();

  // GRO segment size from cmsg.
  size_t gso_size = 0;
  for (cmsghdr* cm = io_uring_recvmsg_cmsg_firsthdr(o, &recv_msg_template_); cm != nullptr;
       cm = io_uring_recvmsg_cmsg_nexthdr(o, &recv_msg_template_, cm)) {
    if (cm->cmsg_level == SOL_UDP && cm->cmsg_type == UDP_GRO) {
      uint16_t v = 0;
      std::memcpy(&v, CMSG_DATA(cm), sizeof(v));
      gso_size = v;
    }
  }

  unsigned int payload_len = io_uring_recvmsg_payload_length(o, res, &recv_msg_template_);
  char* payload = static_cast<char*>(io_uring_recvmsg_payload(o, &recv_msg_template_));

  auto handle_one = [&](td::MutableSlice slice) {
    ingress_stats_.packets++;
    UdpMessageBuffer m;
    m.storage = slice;
    m.address = src;
    auto R = get_or_create_connection(m);
    if (R.is_error()) {
      LOG(DEBUG) << "drop inbound from " << src << ": " << R.error();
      return;
    }
    auto state = R.move_as_ok();
    if (!state) {
      return;
    }
    auto st = state->impl().handle_ingress(m);
    if (st.is_error()) {
      LOG(WARNING) << "handle_ingress error for " << *state << ": " << st;
      wt_on_connection_closed(state->cid);
      return;
    }
    on_connection_updated(*state);
  };

  if (gso_size > 0 && payload_len > gso_size) {
    size_t offset = 0;
    while (offset < payload_len) {
      size_t len = std::min<size_t>(gso_size, payload_len - offset);
      handle_one(td::MutableSlice(payload + offset, len));
      offset += len;
    }
  } else {
    handle_one(td::MutableSlice(payload, payload_len));
  }

  uring_return_buffer(buf_id);
  if (last) {
    multishot_recv_armed_ = false;
  }
}

void QuicWorker::uring_handle_send_cqe(int res, size_t slot_idx) {
  if (res < 0) {
    LOG(DEBUG) << "sendmsg cqe error: " << strerror(-res);
  } else {
    auto& s = send_slots_[slot_idx];
    egress_stats_.syscalls++;
    egress_stats_.bytes += static_cast<td::uint64>(res);
    size_t gso_size = 0;
    if (s.hdr.msg_controllen > 0) {
      for (cmsghdr* cm = CMSG_FIRSTHDR(&s.hdr); cm != nullptr; cm = CMSG_NXTHDR(&s.hdr, cm)) {
        if (cm->cmsg_level == SOL_UDP && cm->cmsg_type == UDP_SEGMENT) {
          uint16_t v = 0;
          std::memcpy(&v, CMSG_DATA(cm), sizeof(v));
          gso_size = v;
        }
      }
    }
    if (gso_size > 0 && static_cast<size_t>(res) > gso_size) {
      egress_stats_.packets += (static_cast<size_t>(res) + gso_size - 1) / gso_size;
    } else {
      egress_stats_.packets++;
    }
  }
  free_send_slots_.push_back(slot_idx);
}

void QuicWorker::uring_handle_cmd_cqe(int res, unsigned flags) {
  if (res < 0) {
    LOG(WARNING) << "cmd-poll cqe error: " << strerror(-res);
    cmd_poll_armed_ = false;
    return;
  }
  if (!(flags & IORING_CQE_F_MORE)) {
    cmd_poll_armed_ = false;
  }
  // Drain the eventfd so it can fire again, then drain commands.
  cmd_queue_.reader_get_event_fd().acquire();
  drain_commands();
}

void QuicWorker::uring_flush_egress() {
  td::PerfWarningTimer w("flush_egress_uring", 0.1);
  const size_t max_packets = gso_enabled_ ? kMaxBurst : 1;

  while (!active_connections_.empty() && !free_send_slots_.empty()) {
    auto cid = active_connections_.front();
    active_connections_.pop_front();

    auto conn = find_connection(cid);
    if (!conn) {
      continue;
    }
    conn->in_active_queue = false;

    size_t slot_idx = free_send_slots_.back();
    auto& slot = send_slots_[slot_idx];

    UdpMessageBuffer ub;
    ub.storage = td::MutableSlice(slot.payload, sizeof(slot.payload));
    auto status = conn->impl().produce_egress(ub, gso_enabled_, max_packets);
    if (status.is_error()) {
      LOG(WARNING) << "produce_egress failed for " << conn->remote_address << ": " << status;
      continue;
    }
    if (ub.storage.empty()) {
      continue;
    }
    on_connection_updated(*conn);

    // produce_egress filled ub.storage in-place starting at slot.payload.
    free_send_slots_.pop_back();

    // Marshal sockaddr.
    size_t addrlen = ub.address.get_sockaddr_len();
    std::memcpy(&slot.dst, ub.address.get_sockaddr(), addrlen);
    slot.iov.iov_base = slot.payload;
    slot.iov.iov_len = ub.storage.size();
    std::memset(&slot.hdr, 0, sizeof(slot.hdr));
    slot.hdr.msg_name = &slot.dst;
    slot.hdr.msg_namelen = static_cast<socklen_t>(addrlen);
    slot.hdr.msg_iov = &slot.iov;
    slot.hdr.msg_iovlen = 1;
    if (gso_enabled_ && ub.gso_size > 0 && ub.storage.size() > ub.gso_size) {
      auto* cm = reinterpret_cast<cmsghdr*>(slot.control);
      cm->cmsg_level = SOL_UDP;
      cm->cmsg_type = UDP_SEGMENT;
      cm->cmsg_len = CMSG_LEN(sizeof(uint16_t));
      uint16_t v = static_cast<uint16_t>(ub.gso_size);
      std::memcpy(CMSG_DATA(cm), &v, sizeof(v));
      slot.hdr.msg_control = slot.control;
      slot.hdr.msg_controllen = cm->cmsg_len;
    } else {
      slot.hdr.msg_control = nullptr;
      slot.hdr.msg_controllen = 0;
    }

    uring_submit_send(slot_idx);
  }
}

void QuicWorker::run_loop() {
  io_uring ring{};
  io_uring_params params{};
  // SETUP_COOP_TASKRUN avoids inter-processor interrupts for completions when
  // we're the only issuer thread (which we are).
  // DEFER_TASKRUN delays completion processing until we explicitly call
  // io_uring_submit_and_wait / io_uring_wait_cqe* — saves task_work runs from
  // happening during random syscalls. Requires SINGLE_ISSUER (one thread does
  // all submits/waits), which we do.
  params.flags = IORING_SETUP_COOP_TASKRUN | IORING_SETUP_SINGLE_ISSUER | IORING_SETUP_DEFER_TASKRUN;
  int rv = io_uring_queue_init_params(kIoUringDepth, &ring, &params);
  if (rv < 0) {
    LOG(FATAL) << "io_uring_queue_init_params failed: " << strerror(-rv);
  }
  ring_ = &ring;

  udp_fd_ = fd_.get_native_fd().fd();
  cmd_fd_ = cmd_queue_.reader_get_event_fd().get_poll_info().native_fd().fd();

  send_slots_.resize(kNumSendSlots);
  free_send_slots_.clear();
  free_send_slots_.reserve(kNumSendSlots);
  for (size_t i = 0; i < kNumSendSlots; ++i) {
    free_send_slots_.push_back(kNumSendSlots - 1 - i);
  }

  uring_setup_recv_buf_ring();
  uring_submit_multishot_recv();
  uring_submit_poll_cmd();
  drain_commands();

  while (!stop_requested_.load(std::memory_order_relaxed)) {
    // Compute wait deadline from earliest QUIC timer.
    __kernel_timespec ts{};
    __kernel_timespec* ts_ptr = nullptr;
    bool can_block = active_connections_.empty();
    if (can_block && !timeout_heap_.empty()) {
      double now = td::Timestamp::now().at();
      double next = timeout_heap_.top_key();
      double diff_s = next - now;
      if (diff_s <= 0) {
        ts.tv_sec = 0;
        ts.tv_nsec = 0;
      } else if (diff_s > 60.0) {
        ts.tv_sec = 60;
        ts.tv_nsec = 0;
      } else {
        ts.tv_sec = static_cast<__kernel_time64_t>(diff_s);
        ts.tv_nsec = static_cast<long long>((diff_s - static_cast<double>(ts.tv_sec)) * 1e9);
      }
      ts_ptr = &ts;
    } else if (!can_block) {
      // Have egress to do — don't block.
      ts.tv_sec = 0;
      ts.tv_nsec = 0;
      ts_ptr = &ts;
    }

    // Combine submit + wait into one syscall when possible. This pushes any
    // pending recv/poll/send SQEs and blocks for at least one CQE (or returns
    // immediately if some already arrived).
    if (ts_ptr && ts.tv_sec == 0 && ts.tv_nsec == 0) {
      if (pending_sqes_ > 0) {
        io_uring_submit(ring_);
        pending_sqes_ = 0;
      }
    } else {
      io_uring_cqe* unused_cqe = nullptr;
      int wret = io_uring_submit_and_wait_timeout(ring_, &unused_cqe, 1, ts_ptr, nullptr);
      pending_sqes_ = 0;
      if (wret < 0 && wret != -ETIME && wret != -EINTR) {
        LOG(WARNING) << "io_uring_submit_and_wait_timeout failed: " << strerror(-wret);
      }
    }

    // Drain all available CQEs in one pass.
    unsigned head = 0;
    unsigned consumed = 0;
    io_uring_cqe* cqe = nullptr;
    io_uring_for_each_cqe(ring_, head, cqe) {
      uint64_t tag = io_uring_cqe_get_data64(cqe);
      uint64_t kind = tag_kind(tag);
      size_t idx = tag_idx(tag);
      int res = cqe->res;
      unsigned flags = cqe->flags;
      if (kind == kTagRecv) {
        uring_handle_recv_cqe(res, flags);
      } else if (kind == kTagSend) {
        uring_handle_send_cqe(res, idx);
      } else if (kind == kTagPoll) {
        uring_handle_cmd_cqe(res, flags);
      }
      ++consumed;
    }
    if (consumed > 0) {
      io_uring_cq_advance(ring_, consumed);
    }

    if (!multishot_recv_armed_) {
      uring_submit_multishot_recv();
    }
    if (!cmd_poll_armed_) {
      uring_submit_poll_cmd();
    }

    handle_timeouts();
    uring_flush_egress();
    erase_pending_connections();
  }

  // Drain any remaining commands one last time.
  drain_commands();

  if (recv_buf_ring_ != nullptr) {
    io_uring_free_buf_ring(ring_, recv_buf_ring_, static_cast<unsigned int>(kNumRecvBufs), kRecvBufGroupId);
    recv_buf_ring_ = nullptr;
  }
  // queue_exit cancels any in-flight SQEs (recv/poll) and unregisters resources.
  ring_ = nullptr;
  io_uring_queue_exit(&ring);
}

}  // namespace ton::quic
