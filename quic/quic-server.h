#pragma once

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <variant>

#include "adnl/adnl-node-id.hpp"
#include "adnl/utils.hpp"
#include "td/actor/ActorOwn.h"
#include "td/actor/core/Actor.h"
#include "td/utils/MpscPollableQueue.h"
#include "td/utils/buffer.h"
#include "td/utils/port/IPAddress.h"
#include "td/utils/port/UdpSocketFd.h"

#include "Ed25519.h"
#include "quic-common.h"
#include "quic-connection-rate-limiters.h"

namespace ton::quic {

class QuicWorker;
class QuicWorkerEventSink;

struct StreamOptions {
  std::optional<td::uint64> max_size;
  td::Timestamp timeout = td::Timestamp::never();
  double timeout_seconds = 0.0;
  td::uint64 query_size = 0;
  td::uint32 query_magic = 0;
};

struct StreamShutdownList {
  struct Entry {
    QuicConnectionId cid;
    QuicStreamID sid;
  };
  td::vector<Entry> entries;
};

// QuicServer is now a thin actor that owns a QuicWorker running on its own
// I/O thread. The actor side handles control-plane forwarding plus user
// callback dispatch; the worker handles UDP I/O + ngtcp2 crypto.
class QuicServer : public td::actor::Actor, public td::ObserverBase {
 public:
  struct Options {
    bool enable_gso = true;
    bool enable_gro = true;
    bool enable_mmsg = true;
    CongestionControlAlgo cc_algo = CongestionControlAlgo::Bbr;
    std::optional<size_t> flood_control = DEFAULT_FLOOD_CONTROL;
    std::optional<size_t> max_streams_bidi = std::nullopt;
    td::uint32 new_connection_rate_limit_capacity = 10;
    double new_connection_rate_limit_period = 0.2;
    td::uint32 global_new_connection_rate_limit_capacity = 100000;
    double global_new_connection_rate_limit_period = 0.00001;
    bool stateless_retry = true;
  };
  class Callback {
   public:
    virtual td::Status on_connected(QuicConnectionId cid, td::SecureString local_public_key,
                                    td::SecureString peer_public_key, bool is_outbound) = 0;
    virtual td::Status on_stream(QuicConnectionId cid, QuicStreamID sid, td::BufferSlice data, bool is_end) = 0;
    virtual void on_closed(QuicConnectionId cid) = 0;
    virtual void on_stream_closed(QuicConnectionId cid, QuicStreamID sid) = 0;
    virtual void set_stream_options(QuicConnectionId cid, QuicStreamID sid, StreamOptions options) {
    }
    virtual void loop(td::Timestamp now, StreamShutdownList &streams_to_shutdown) {
    }
    virtual td::Timestamp next_alarm() const {
      return td::Timestamp::never();
    }
    virtual void set_peer_mtu_callback(std::function<td::uint64(adnl::AdnlNodeIdShort, adnl::AdnlNodeIdShort)> f) = 0;
    virtual ~Callback() = default;
  };

  // Async / fire-and-forget control plane forwards to the worker.
  // NOTE: every send_stream call MUST be the complete message (is_end=true).
  // The worker writes a 4-byte length prefix on the wire so receivers can
  // pre-size the receive buffer exactly; incremental partial sends are
  // unsupported. open_stream is for explicit two-step open + send.
  void open_stream(QuicConnectionId cid, StreamOptions options, td::Promise<QuicStreamID> promise);

  void send_stream(QuicConnectionId cid, std::variant<QuicStreamID, StreamOptions> stream, td::BufferSlice data,
                   bool is_end, td::Promise<QuicStreamID> promise);

  void connect(std::string host, int port, td::Ed25519::PrivateKey client_key, std::string alpn, std::string sni,
               td::Promise<QuicConnectionId> promise);

  void shutdown_stream(QuicConnectionId cid, QuicStreamID sid);
  void on_connection_closed(QuicConnectionId cid);
  void log_stats(std::string reason = "stats");

  void set_default_mtu(adnl::AdnlNodeIdShort local_id, td::uint64 mtu);
  void set_peer_mtu(adnl::AdnlNodeIdShort local_id, adnl::AdnlNodeIdShort peer_id, td::uint64 mtu);

  void add_identity(adnl::AdnlNodeIdShort local_id, td::Ed25519::PrivateKey key);

  constexpr static size_t DEFAULT_FLOOD_CONTROL = 1000;

  QuicServer(td::UdpSocketFd fd, td::uint64 default_mtu, td::BufferSlice alpn, std::unique_ptr<Callback> callback,
             Options options);
  ~QuicServer() override;

  static td::Result<td::actor::ActorOwn<QuicServer>> create(int port, std::unique_ptr<Callback> callback,
                                                            td::uint64 default_mtu, td::Slice alpn = "ton",
                                                            td::Slice bind_host = "0.0.0.0");
  static td::Result<td::actor::ActorOwn<QuicServer>> create(int port, std::unique_ptr<Callback> callback,
                                                            td::uint64 default_mtu, td::Slice alpn, td::Slice bind_host,
                                                            Options options);

  struct Stats {
    struct Entry {
      size_t total_conns = 1;
      QuicConnectionStats impl_stats = {};

      Entry operator+(const Entry &other) const {
        Entry res = {.total_conns = total_conns + other.total_conns, .impl_stats = impl_stats + other.impl_stats};
        auto tc = total_conns + other.total_conns;
        if (tc > 0)
          res.impl_stats.mean_rtt = (static_cast<double>(total_conns) * impl_stats.mean_rtt +
                                     static_cast<double>(other.total_conns) * other.impl_stats.mean_rtt) /
                                    static_cast<double>(tc);
        return res;
      }

      Entry operator-(const Entry &other) const {
        Entry res = {.total_conns = total_conns - other.total_conns, .impl_stats = impl_stats - other.impl_stats};
        res.impl_stats.mean_rtt = impl_stats.mean_rtt;
        return res;
      }
    };

    Entry summary = {.total_conns = 0};
    std::unordered_map<QuicConnectionId, Entry> per_conn = {};
  };

  void collect_stats(td::Promise<Stats> P);

 protected:
  void start_up() override;
  void tear_down() override;
  void hangup() override;
  void hangup_shared() override;
  void alarm() override;
  void loop() override;

  void notify() override;

 private:
  class SinkImpl;

  td::uint64 mtu_for(adnl::AdnlNodeIdShort local_id, adnl::AdnlNodeIdShort peer_id) const;
  void drain_callback_events();

  // Socket info needed to construct the worker (transferred at start_up time).
  td::UdpSocketFd pending_fd_;
  td::BufferSlice alpn_;
  Options options_;
  td::uint64 default_mtu_ = 0;
  std::string socket_name_;

  std::unique_ptr<Callback> callback_;
  std::unique_ptr<QuicWorker> worker_;

  // Worker → actor event queue. Worker pushes UniqueFn; actor drains and invokes
  // each on the actor thread.
  std::shared_ptr<td::MpscPollableQueue<UniqueFn>> events_;
  bool events_subscribed_ = false;

  td::actor::ActorId<QuicServer> self_id_;

  // MTU state lives here so the user Callback's get_peer_mtu_ closure (invoked
  // on the actor thread from inside on_stream) can read it without a race.
  std::map<adnl::AdnlNodeIdShort, td::uint64> default_mtu_by_local_id_;
  std::map<std::pair<adnl::AdnlNodeIdShort, adnl::AdnlNodeIdShort>, td::uint64> peers_mtu_;
};

}  // namespace ton::quic
