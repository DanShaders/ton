#pragma once

#include <array>
#include <atomic>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <thread>
#include <tuple>
#include <variant>
#include <vector>

#include "adnl/adnl-node-id.hpp"
#include "adnl/utils.hpp"
#include "td/actor/PromiseFuture.h"
#include "td/utils/Heap.h"
#include "td/utils/MpscPollableQueue.h"
#include "td/utils/buffer.h"
#include "td/utils/port/IPAddress.h"
#include "td/utils/port/UdpSocketFd.h"

#include <sys/socket.h>

#include "Ed25519.h"
#include "quic-common.h"
#include "quic-connection-rate-limiters.h"
#include "quic-server.h"  // For QuicServer::Callback, QuicServer::Options, QuicServer::Stats, StreamOptions

struct io_uring;
struct io_uring_cqe;

namespace ton::quic {

struct QuicConnectionOptions;
struct ServerIdentities;
struct ServerInitialInfo;
struct VersionCid;
struct QuicConnectionPImpl;

// Bridge for the worker → owning actor. Worker thread invokes these from the
// I/O loop; the implementation must enqueue them onto the actor's MPSC queue
// and never block. Methods may be called from the worker thread.
class QuicWorkerEventSink {
 public:
  virtual ~QuicWorkerEventSink() = default;
  virtual void on_connected(QuicConnectionId cid, td::SecureString local_public_key, td::SecureString peer_public_key,
                            bool is_outbound) = 0;
  virtual void on_stream(QuicConnectionId cid, QuicStreamID sid, td::BufferSlice data, bool is_end) = 0;
  virtual void on_closed(QuicConnectionId cid) = 0;
  virtual void on_stream_closed(QuicConnectionId cid, QuicStreamID sid) = 0;
};

// QuicWorker runs the QUIC data plane on a dedicated I/O thread:
//   recvmmsg → ngtcp2 decrypt → emit events to sink → ngtcp2 encrypt → sendmmsg.
// The owning QuicServer actor only does control-plane forwarding into the
// MPSC command queue exposed by post(), and receives callbacks via the sink.
class QuicWorker {
 public:
  using Options = QuicServer::Options;
  using Stats = QuicServer::Stats;

  QuicWorker(td::UdpSocketFd fd, td::BufferSlice alpn, std::unique_ptr<QuicWorkerEventSink> sink, Options options);
  ~QuicWorker();

  QuicWorker(const QuicWorker&) = delete;
  QuicWorker& operator=(const QuicWorker&) = delete;

  // Spawn the dedicated I/O thread. Must be called exactly once.
  void start(std::string thread_name);

  // Stop the I/O thread, wait for join, and tear down all connections.
  // After stop() returns the worker is unusable.
  void stop();

  // Enqueue a closure to be run on the worker thread. Thread-safe; may be called
  // from any thread. The closure must not throw.
  void post(UniqueFn fn);

  // --- Methods below run on the worker thread (call only via post()) ---

  void wt_connect(td::IPAddress remote_address, td::Ed25519::PrivateKey client_key, std::string alpn, std::string sni,
                  td::Promise<QuicConnectionId> promise);
  void wt_send_stream(QuicConnectionId cid, std::variant<QuicStreamID, StreamOptions> stream, td::BufferSlice data,
                      bool is_end, td::Promise<QuicStreamID> promise);
  void wt_open_stream(QuicConnectionId cid, StreamOptions options, td::Promise<QuicStreamID> promise);
  void wt_shutdown_stream(QuicConnectionId cid, QuicStreamID sid);
  void wt_on_connection_closed(QuicConnectionId cid);
  void wt_log_stats(std::string reason);
  void wt_add_identity(adnl::AdnlNodeIdShort local_id, td::Ed25519::PrivateKey key);
  void wt_collect_stats(td::Promise<Stats> promise);

 private:
  friend QuicConnectionPImpl;
  class PImplCallback;

  constexpr static size_t DEFAULT_MTU = 1350;
  constexpr static size_t kMaxBurst = 16;
  constexpr static size_t kIngressBatch = 16;
  constexpr static size_t kEgressBatch = 16;
  constexpr static size_t kMaxDatagram = 64 * 1024;

  // io_uring fast-path resources
  constexpr static size_t kIoUringDepth = 1024;
  constexpr static size_t kNumRecvSlots = 64;
  constexpr static size_t kNumSendSlots = 256;

  struct ConnectionState : td::HeapNode {
    QuicConnectionPImpl& impl() {
      CHECK(impl_);
      return *impl_;
    }
    std::unique_ptr<QuicConnectionPImpl> impl_;
    td::IPAddress remote_address;
    QuicConnectionId cid;
    std::optional<QuicConnectionId> bootstrap_routed_cid;
    std::set<QuicConnectionId> routed_cids;
    bool is_outbound;
    bool in_active_queue = false;
    friend td::StringBuilder& operator<<(td::StringBuilder& sb, const ConnectionState& state) {
      sb << "Connection{" << (state.is_outbound ? "to" : "from") << " " << state.remote_address;
      sb << " cid=" << state.cid;
      sb << "}";
      return sb;
    }
  };
  struct BootstrapRouteKey {
    td::IPAddress remote_address;
    QuicConnectionId routed_cid;
    friend bool operator<(const BootstrapRouteKey& a, const BootstrapRouteKey& b) {
      return std::tie(a.remote_address, a.routed_cid) < std::tie(b.remote_address, b.routed_cid);
    }
  };

  void run_loop();
  void drain_commands();

  // io_uring helpers (defined in quic-worker.cpp)
  void uring_submit_recv(size_t slot_idx);
  void uring_submit_send(size_t slot_idx);
  void uring_submit_poll_cmd();
  void uring_handle_recv_cqe(int res, size_t slot_idx);
  void uring_handle_send_cqe(int res, size_t slot_idx);
  void uring_handle_cmd_cqe(int res, unsigned flags);
  void uring_flush_egress();
  void on_connection_updated(ConnectionState& state);
  void bind_cid(const QuicConnectionId& primary_cid, const QuicConnectionId& cid);
  void unbind_cid(const QuicConnectionId& primary_cid, const QuicConnectionId& cid);
  void unbind_all_cids(ConnectionState& state);
  td::Result<std::shared_ptr<ConnectionState>> install_connection(std::unique_ptr<QuicConnectionPImpl> p_impl,
                                                                  const td::IPAddress& remote_address, bool is_outbound,
                                                                  std::optional<QuicConnectionId> bootstrap_routed_cid);
  void on_local_cid_issued(const QuicConnectionId& primary_cid, const QuicConnectionId& cid);
  void on_local_cid_retired(const QuicConnectionId& primary_cid, const QuicConnectionId& cid);
  td::Result<std::optional<ServerInitialInfo>> prepare_server_initial_info(const VersionCid& initial_packet,
                                                                           const td::IPAddress& remote_address);
  td::Result<QuicConnectionId> verify_retry_token(const VersionCid& packet, const td::IPAddress& remote_address) const;
  td::Status send_stateless_datagram(td::Slice packet_kind, const td::IPAddress& remote_address, td::Slice data);
  td::Status send_retry(const VersionCid& packet, const td::IPAddress& remote_address);
  td::Status send_invalid_token_connection_close(const VersionCid& packet, const td::IPAddress& remote_address);

  void drain_ingress();
  void flush_egress();
  bool flush_pending();
  bool produce_next_egress(size_t batch_index);
  void handle_timeouts();
  void erase_pending_connections();

  std::shared_ptr<ConnectionState> find_connection(const QuicConnectionId& cid);
  td::Result<std::shared_ptr<ConnectionState>> get_or_create_connection(const UdpMessageBuffer& msg_in);
  td::Status ensure_flood_allowed(const std::string& flood_addr);
  void flood_on_inbound_connection_created(const std::string& flood_addr);
  void flood_on_inbound_connection_closed(const std::string& flood_addr);
  QuicConnectionOptions build_connection_options() const;
  bool handle_expiry(ConnectionState& state);
  void log_conn_stats(ConnectionState& state, const char* reason);

  td::UdpSocketFd fd_;
  td::BufferSlice alpn_;
  td::Ref<ServerIdentities> identities_;
  std::array<td::uint8, 32> retry_secret_{};
  Options options_;
  QuicConnectionRateLimiters conn_rate_limiters_;
  adnl::RateLimiter global_conn_rate_limiter_;
  bool gso_enabled_{true};
  bool gro_enabled_{false};
  std::unordered_map<std::string, size_t> flood_map_;

  std::unique_ptr<QuicWorkerEventSink> sink_;

  std::map<QuicConnectionId, QuicConnectionId> cid_to_primary_cid_;
  std::map<BootstrapRouteKey, QuicConnectionId> bootstrap_routes_;
  std::map<QuicConnectionId, std::shared_ptr<ConnectionState>> connections_;
  std::deque<QuicConnectionId> active_connections_;
  std::vector<QuicConnectionId> to_erase_connections_;
  td::KHeap<double> timeout_heap_;

  // io_uring recv slot pool. Each slot owns the buffers the kernel will fill
  // for a single recvmsg SQE; the slot is resubmitted after we drain the CQE.
  struct RecvSlot {
    alignas(8) char payload[kMaxDatagram];
    alignas(8) char control[64];
    sockaddr_storage src;
    iovec iov;
    msghdr hdr;
  };

  // io_uring send slot pool. produce_egress writes directly into the slot's
  // payload buffer, which then stays alive until the send CQE arrives.
  struct SendSlot {
    alignas(8) char payload[DEFAULT_MTU * kMaxBurst];
    alignas(8) char control[64];
    sockaddr_storage dst;
    iovec iov;
    msghdr hdr;
  };

  std::vector<RecvSlot> recv_slots_;  // size kNumRecvSlots
  std::vector<SendSlot> send_slots_;  // size kNumSendSlots
  std::vector<size_t> free_send_slots_;
  io_uring* ring_ = nullptr;
  int udp_fd_ = -1;
  int cmd_fd_ = -1;
  size_t pending_sqes_ = 0;
  bool cmd_poll_armed_ = false;

  // UDP-level stats
  struct UdpStats {
    td::uint64 syscalls = 0;
    td::uint64 packets = 0;
    td::uint64 bytes = 0;
  };
  UdpStats ingress_stats_;
  UdpStats egress_stats_;

  // Thread + command queue
  std::thread thread_;
  std::atomic<bool> stop_requested_{false};
  std::atomic<bool> started_{false};
  td::MpscPollableQueue<UniqueFn> cmd_queue_;
};

}  // namespace ton::quic
