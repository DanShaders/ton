/*
 * Single-broadcast timing bench for the two-step FEC path over actual QUIC.
 *
 * Each node binds a real UDP socket on 127.0.0.1; QUIC uses adnl_port+1000.
 * Phase 1 (warmup, coroutine): every node sends a query to every other node
 * over QuicSender and awaits all responses — that forces the full N*(N-1)
 * QUIC handshake matrix before we measure anything.
 * Phase 2 (measure, coroutine): node 0 fires one broadcast of `--size` bytes
 * and we await per-receiver delivery promises, printing arrival times
 * relative to send_broadcast_fec_ex.
 */

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "adnl/adnl-peer-table.h"
#include "adnl/adnl.h"
#include "auto/tl/ton_api.h"
#include "keyring/keyring.h"
#include "overlay/overlays.h"
#include "quic/quic-sender.h"
#include "td/actor/PromiseFuture.h"
#include "td/actor/actor.h"
#include "td/actor/coro_task.h"
#include "td/actor/coro_utils.h"
#include "td/utils/OptionParser.h"
#include "td/utils/Random.h"
#include "td/utils/Time.h"
#include "td/utils/logging.h"
#include "td/utils/port/IPAddress.h"
#include "td/utils/port/path.h"

namespace {

using ton::PublicKeyHash;
using ton::adnl::AdnlNodeIdShort;
using ton::overlay::OverlayIdFull;
using ton::overlay::OverlayIdShort;

struct PerReceiver {
  double t_first_recv_ms = -1;
  size_t bytes = 0;
  // Resolves on the first receive_broadcast call for this receiver.
  std::shared_ptr<td::actor::StartedTask<td::Unit>> task;
  // Promise side captured at construction so the callback can hand it off.
  std::shared_ptr<td::actor::StartedTask<td::Unit>::ExternalPromise> promise;
};

class RecvCallback final : public ton::overlay::Overlays::Callback {
 public:
  RecvCallback(PerReceiver *slot, double *broadcast_start, int idx)
      : slot_(slot), broadcast_start_(broadcast_start), idx_(idx) {
  }

  void receive_broadcast(PublicKeyHash, OverlayIdShort, td::BufferSlice data) override {
    if (slot_->t_first_recv_ms >= 0) return;
    slot_->t_first_recv_ms = (td::Time::now() - *broadcast_start_) * 1000.0;
    slot_->bytes = data.size();
    if (slot_->promise && *slot_->promise) {
      slot_->promise->set_value(td::Unit{});
    }
  }

  void check_broadcast(PublicKeyHash, OverlayIdShort, td::BufferSlice, td::Promise<td::Unit> p) override {
    p.set_value(td::Unit());
  }

 private:
  PerReceiver *slot_;
  double *broadcast_start_;
  int idx_;
};

class WarmupAdnlCallback final : public ton::adnl::Adnl::Callback {
 public:
  void receive_message(AdnlNodeIdShort, AdnlNodeIdShort, td::BufferSlice) override {
  }
  void receive_query(AdnlNodeIdShort, AdnlNodeIdShort, td::BufferSlice data, td::Promise<td::BufferSlice> p) override {
    p.set_value(td::BufferSlice("ok"));
  }
};

struct Node {
  int idx = 0;
  td::uint16 adnl_port = 0;
  td::IPAddress addr;
  ton::adnl::AdnlAddressList addr_list;
  ton::PrivateKey pk;
  ton::adnl::AdnlNodeIdFull adnl_full;
  ton::adnl::AdnlNodeIdShort adnl_short;
  std::string db_root;

  td::actor::ActorOwn<ton::keyring::Keyring> keyring;
  td::actor::ActorOwn<ton::adnl::AdnlNetworkManager> network_manager;
  td::actor::ActorOwn<ton::adnl::Adnl> adnl;
  td::actor::ActorOwn<ton::quic::QuicSender> quic;
  td::actor::ActorOwn<ton::overlay::Overlays> overlays;
};

// Talks to `perf record --control fifo:ctl,ack`. enable_perf() turns sampling on
// and waits for ack; disable_perf() turns it off and waits for ack. Blocking I/O,
// but only called briefly at warmup/broadcast boundaries.
class PerfCtl {
 public:
  PerfCtl(std::string ctl, std::string ack) : ctl_path_(std::move(ctl)), ack_path_(std::move(ack)) {
  }

  bool enabled() const {
    return !ctl_path_.empty();
  }

  void command(const char *cmd) {
    if (!enabled()) return;
    FILE *c = std::fopen(ctl_path_.c_str(), "w");  // blocks until perf opens reader
    if (!c) {
      std::fprintf(stderr, "perf: failed to open ctl fifo %s\n", ctl_path_.c_str());
      return;
    }
    std::fprintf(c, "%s\n", cmd);
    std::fflush(c);
    std::fclose(c);
    if (!ack_path_.empty()) {
      FILE *a = std::fopen(ack_path_.c_str(), "r");
      if (a) {
        char buf[16];
        if (std::fgets(buf, sizeof(buf), a)) {
          // expect "ack\n"
        }
        std::fclose(a);
      }
    }
  }

 private:
  std::string ctl_path_;
  std::string ack_path_;
};

// One snapshot of per-connection QUIC state captured during the broadcast
// window. We index by [(local_id, peer_id) -> sample at time t], so we can
// dump a per-conn timeline of cwnd / bytes_sent / loss over the lifetime of
// the one measured broadcast.
struct ConnSample {
  double t_ms = 0;             // ms since broadcast start
  int64_t bytes_tx = 0;
  int64_t bytes_rx = 0;
  int64_t bytes_lost = 0;
  int64_t bytes_unacked = 0;
  double smoothed_rtt_ms = 0;  // ngtcp2 smoothed_rtt converted to ms
};
struct NodeSampleSeries {
  // Keyed by "local_hex_prefix>remote_hex_prefix" so outbound and inbound for
  // the same peer pair both show up as distinct rows.
  std::map<std::string, std::vector<ConnSample>> by_peer;
};

// Driver actor: owns the coroutine that runs warmup + measurement.
class Driver final : public td::actor::Actor {
 public:
  Driver(std::vector<Node> *nodes, std::vector<PerReceiver> *per_receiver, double *broadcast_start,
         OverlayIdShort overlay, size_t payload_size, std::string db_root_base, PerfCtl perf,
         double sample_period_ms)
      : nodes_(nodes)
      , per_receiver_(per_receiver)
      , broadcast_start_(broadcast_start)
      , overlay_(overlay)
      , payload_size_(payload_size)
      , db_root_base_(std::move(db_root_base))
      , perf_(std::move(perf))
      , sample_period_ms_(sample_period_ms) {
  }

  void start_up() override {
    run().start_immediate().detach("driver");
  }

 private:
  td::actor::Task<> run() {
    co_await warmup_quic();
    if (perf_.enabled()) {
      std::fprintf(stderr, "perf: enabling sampling\n");
      perf_.command("enable");
    }
    co_await measure_one_broadcast();
    if (perf_.enabled()) {
      std::fprintf(stderr, "perf: disabling sampling\n");
      perf_.command("disable");
    }
    co_await dump_quic_stats();
    std::fflush(stderr);
    td::rmrf(db_root_base_).ignore();
    _exit(0);
  }

  // Per-node QUIC stats: per-path bytes_tx/rx, mean_rtt, etc. Helps tell whether
  // QUIC's the bottleneck (high unacked / low cwnd / high rtt) vs the actor system.
  td::actor::Task<> dump_quic_stats() {
    auto n = nodes_->size();
    std::fprintf(stderr, "\n# ---- per-node QuicSender stats ----\n");
    for (size_t i = 0; i < n; ++i) {
      auto stats = co_await td::actor::ask((*nodes_)[i].quic.get(), &ton::quic::QuicSender::collect_stats);
      std::fprintf(stderr, "node %zu (%s): conns=%zu  tx=%lld B  rx=%lld B  unacked=%lld B  lost=%lld B  open_sids=%lld  total_sids=%lld  mean_rtt=%.2f ms\n",
                   i, (*nodes_)[i].adnl_short.bits256_value().to_hex().substr(0, 8).c_str(),
                   stats.summary.server_stats.total_conns,
                   (long long)stats.summary.server_stats.impl_stats.bytes_tx,
                   (long long)stats.summary.server_stats.impl_stats.bytes_rx,
                   (long long)stats.summary.server_stats.impl_stats.bytes_unacked,
                   (long long)stats.summary.server_stats.impl_stats.bytes_lost,
                   (long long)stats.summary.server_stats.impl_stats.open_sids,
                   (long long)stats.summary.server_stats.impl_stats.total_sids,
                   // mean_rtt is ngtcp2 smoothed_rtt in nanoseconds.
                   stats.summary.server_stats.impl_stats.mean_rtt / 1e6);
    }
    co_return {};
  }

  // Send a query from every node to every other node via QuicSender; await all.
  // Retries failed pairs until they succeed because QuicSender::add_id is fire-and-forget:
  // its add_local_id_coro suspends on keyring->export_private_key before populating
  // local_keys_, so queries scheduled immediately after add_id can race and fail
  // with "no local key for source ADNL ID". After this method returns, every (i, j)
  // QUIC connection has been established.
  td::actor::Task<> warmup_quic() {
    auto t0 = td::Time::now();
    auto n = nodes_->size();
    std::vector<std::pair<size_t, size_t>> remaining;
    for (size_t i = 0; i < n; ++i) {
      for (size_t j = 0; j < n; ++j) {
        if (i != j) remaining.emplace_back(i, j);
      }
    }
    int attempt = 0;
    while (!remaining.empty()) {
      ++attempt;
      std::vector<td::actor::StartedTask<td::BufferSlice>> tasks;
      tasks.reserve(remaining.size());
      for (auto [i, j] : remaining) {
        auto [task, promise] = td::actor::StartedTask<td::BufferSlice>::make_bridge();
        td::actor::send_closure((*nodes_)[i].quic, &ton::quic::QuicSender::send_query, (*nodes_)[i].adnl_short,
                                (*nodes_)[j].adnl_short, std::string("warm"), std::move(promise),
                                td::Timestamp::in(15.0), td::BufferSlice("warm-up"));
        tasks.push_back(std::move(task));
      }
      std::vector<std::pair<size_t, size_t>> next_remaining;
      size_t ok = 0;
      for (size_t k = 0; k < tasks.size(); ++k) {
        auto r = co_await std::move(tasks[k]).wrap();
        if (r.is_ok()) {
          ++ok;
        } else {
          next_remaining.push_back(remaining[k]);
        }
      }
      std::fprintf(stderr, "warmup attempt %d: %zu ok / %zu remaining\n", attempt, ok, next_remaining.size());
      if (next_remaining.empty()) break;
      if (attempt >= 20) {
        std::fprintf(stderr, "warmup: giving up with %zu pairs unconnected\n", next_remaining.size());
        break;
      }
      // Brief backoff for the keyring fetch / server bind to complete.
      co_await td::actor::coro_sleep(td::Timestamp::in(0.1));
      remaining = std::move(next_remaining);
    }
    std::fprintf(stderr, "warmup: done in %.2f ms (%d attempts)\n", (td::Time::now() - t0) * 1000.0, attempt);
    co_return {};
  }

  // Fire-and-forget coroutine: every `sample_period_ms_` ms snapshot
  // per-connection QUIC stats from every node into samples_. Stops as soon
  // as `sampling_done_` flips to true (set by measure_one_broadcast right
  // after the last per-receiver promise resolves).
  td::actor::Task<> sample_quic_during_broadcast() {
    if (sample_period_ms_ <= 0) {
      co_return {};
    }
    while (!sampling_done_.load(std::memory_order_acquire)) {
      double t_ms = (td::Time::now() - *broadcast_start_) * 1000.0;
      auto n = nodes_->size();
      for (size_t i = 0; i < n; ++i) {
        auto stats = co_await td::actor::ask((*nodes_)[i].quic.get(), &ton::quic::QuicSender::collect_stats);
        for (const auto &[path_dir, entry] : stats.per_path) {
          const auto &[path, is_outbound] = path_dir;
          const auto &impl = entry.server_stats.impl_stats;
          std::string peer_key = path.first.bits256_value().to_hex().substr(0, 8) + (is_outbound ? ">" : "<") +
                                 path.second.bits256_value().to_hex().substr(0, 8);
          samples_[i].by_peer[peer_key].push_back(ConnSample{
              .t_ms = t_ms,
              .bytes_tx = impl.bytes_tx,
              .bytes_rx = impl.bytes_rx,
              .bytes_lost = impl.bytes_lost,
              .bytes_unacked = impl.bytes_unacked,
              .smoothed_rtt_ms = impl.mean_rtt / 1e6,
          });
        }
      }
      co_await td::actor::coro_sleep(td::Timestamp::in(sample_period_ms_ / 1000.0));
    }
    co_return {};
  }

  // Print one row per (node, peer) showing how bytes_tx and bytes_lost grew
  // across the snapshots, plus the per-window MB/s. The first sample is t≈0.
  void dump_samples() {
    std::fprintf(stderr, "\n# ---- node short-id map (first 8 hex chars) ----\n");
    for (size_t i = 0; i < nodes_->size(); ++i) {
      std::fprintf(stderr, "  node %zu = %s\n", i,
                   (*nodes_)[i].adnl_short.bits256_value().to_hex().substr(0, 8).c_str());
    }
    std::fprintf(stderr, "\n# ---- QUIC per-connection timeline (one row per snapshot per pair) ----\n");
    std::fprintf(stderr, "# %-4s %-18s %-8s %-12s %-12s %-10s %-10s %-10s\n", "node", "src>dst", "t_ms",
                 "tx_total_B", "rx_total_B", "lost_B", "unacked_B", "rtt_ms");
    for (size_t i = 0; i < samples_.size(); ++i) {
      for (auto &[peer, series] : samples_[i].by_peer) {
        for (auto &s : series) {
          std::fprintf(stderr, "  %-4zu %-18s %-8.1f %-12lld %-12lld %-10lld %-10lld %-10.2f\n", i, peer.c_str(), s.t_ms,
                       (long long)s.bytes_tx, (long long)s.bytes_rx, (long long)s.bytes_lost,
                       (long long)s.bytes_unacked, s.smoothed_rtt_ms);
        }
      }
    }
    // Compact rate summary: for each (node, peer) pair show the max single-window MB/s.
    std::fprintf(stderr, "\n# ---- max single-window tx-rate per (node, peer) ----\n");
    std::fprintf(stderr, "# %-4s %-18s  %-12s  %-10s\n", "node", "src>dst", "max_MB_per_s", "samples");
    for (size_t i = 0; i < samples_.size(); ++i) {
      for (auto &[peer, series] : samples_[i].by_peer) {
        double max_rate = 0;
        for (size_t k = 1; k < series.size(); ++k) {
          double dt = (series[k].t_ms - series[k - 1].t_ms) / 1000.0;
          if (dt <= 0) continue;
          double db = static_cast<double>(series[k].bytes_tx - series[k - 1].bytes_tx);
          double rate_MBs = db / dt / (1024.0 * 1024.0);
          max_rate = std::max(max_rate, rate_MBs);
        }
        if (series.size() >= 2) {
          std::fprintf(stderr, "  %-4zu %-18s  %-12.1f  %zu\n", i, peer.c_str(), max_rate, series.size());
        }
      }
    }
  }

  td::actor::Task<> measure_one_broadcast() {
    td::BufferSlice payload(payload_size_);
    for (size_t i = 0; i < payload_size_; ++i) {
      payload.as_slice().begin()[i] = static_cast<char>(td::Random::fast_uint32() & 0xff);
    }

    auto n = nodes_->size();
    // Install fresh promises on receivers so the callback can resolve them.
    std::vector<td::actor::StartedTask<td::Unit>> wait_tasks;
    wait_tasks.reserve(n);
    for (size_t i = 0; i < n; ++i) {
      auto [task, promise] = td::actor::StartedTask<td::Unit>::make_bridge();
      (*per_receiver_)[i].promise =
          std::make_shared<td::actor::StartedTask<td::Unit>::ExternalPromise>(std::move(promise));
      wait_tasks.push_back(std::move(task));
    }

    std::fprintf(stderr, "broadcasting %zu B from node 0...\n", payload_size_);
    *broadcast_start_ = td::Time::now();
    samples_.assign(n, NodeSampleSeries{});
    sampling_done_.store(false, std::memory_order_release);
    auto sampler_task = sample_quic_during_broadcast().start_immediate();
    td::actor::send_closure((*nodes_)[0].overlays, &ton::overlay::Overlays::send_broadcast_fec_ex,
                            (*nodes_)[0].adnl_short, overlay_, (*nodes_)[0].adnl_short.pubkey_hash(),
                            td::uint32{0}, std::move(payload));

    // Skip sender's own self-delivery (idx 0); await each remote receiver.
    int target = static_cast<int>(n) - 1;
    int got = 0;
    for (size_t i = 1; i < n; ++i) {
      auto r = co_await std::move(wait_tasks[i]).wrap();
      if (r.is_ok()) {
        ++got;
      }
    }
    double finish_time = td::Time::now();
    sampling_done_.store(true, std::memory_order_release);
    co_await std::move(sampler_task).wrap();

    std::fprintf(stderr,
                 "\n# broadcast timing — %zu nodes, payload=%zu B (%.2f MB), QUIC on 127.0.0.1:%u..\n",
                 n, payload_size_, payload_size_ / 1024.0 / 1024.0,
                 static_cast<unsigned>((*nodes_)[0].adnl_port + 1000));
    std::fprintf(stderr, "# all times in ms, relative to send_broadcast_fec_ex on node 0\n");
    std::fprintf(stderr, "# %-4s %-12s %s\n", "node", "first_recv", "bytes");
    std::vector<double> times;
    for (size_t i = 0; i < n; ++i) {
      const auto &r = (*per_receiver_)[i];
      if (i == 0) {
        std::fprintf(stderr, "  %-4zu %-12s  %s\n", i, "(sender)", "-");
        continue;
      }
      if (r.t_first_recv_ms < 0) {
        std::fprintf(stderr, "  %-4zu %-12s  %s\n", i, "MISSING", "-");
      } else {
        std::fprintf(stderr, "  %-4zu %-12.2f  %zu\n", i, r.t_first_recv_ms, r.bytes);
        times.push_back(r.t_first_recv_ms);
      }
    }
    std::sort(times.begin(), times.end());
    if (!times.empty()) {
      double total = finish_time - *broadcast_start_;
      auto pct = [&](double q) {
        double f_idx = q * static_cast<double>(times.size() - 1);
        auto lo = static_cast<size_t>(f_idx);
        auto hi = std::min(times.size() - 1, lo + 1);
        double f = f_idx - static_cast<double>(lo);
        return times[lo] * (1 - f) + times[hi] * f;
      };
      std::fprintf(stderr,
                   "\n# summary: delivered=%d/%d  first=%.2f ms  p50=%.2f  p95=%.2f  last=%.2f  total_wall=%.2f ms\n",
                   got, target, times.front(), pct(0.5), pct(0.95), times.back(), total * 1000.0);
    } else {
      std::fprintf(stderr, "\n# summary: delivered=0/%d (all timed out)\n", target);
    }
    dump_samples();
    co_return {};
  }

  std::vector<Node> *nodes_;
  std::vector<PerReceiver> *per_receiver_;
  double *broadcast_start_;
  OverlayIdShort overlay_;
  size_t payload_size_;
  std::string db_root_base_;
  PerfCtl perf_;
  double sample_period_ms_;
  std::atomic<bool> sampling_done_{false};
  std::vector<NodeSampleSeries> samples_;
};

}  // namespace

int main(int argc, char *argv[]) {
  size_t n_nodes = 8;
  size_t payload_size = 4 * 1024 * 1024;
  td::uint16 base_port = 22000;
  int verbosity = 0;
  std::string perf_ctl;
  std::string perf_ack;
  double sample_period_ms = 0;

  td::OptionParser p;
  p.add_option('n', "nodes", "number of overlay nodes (default 8)",
               [&](td::Slice s) { n_nodes = std::stoul(s.str()); });
  p.add_option('s', "size", "broadcast payload bytes (default 4 MiB)",
               [&](td::Slice s) { payload_size = std::stoul(s.str()); });
  p.add_option('p', "port", "base ADNL UDP port on 127.0.0.1 (default 22000)",
               [&](td::Slice s) { base_port = static_cast<td::uint16>(std::stoul(s.str())); });
  p.add_option('v', "verbosity", "log verbosity offset from INFO (default 0)",
               [&](td::Slice s) { verbosity = std::stoi(s.str()); });
  p.add_option('\0', "perf-ctl", "perf control fifo path (writes enable/disable)",
               [&](td::Slice s) { perf_ctl = s.str(); });
  p.add_option('\0', "perf-ack", "perf ack fifo path (reads ack lines)",
               [&](td::Slice s) { perf_ack = s.str(); });
  p.add_option('\0', "sample-period-ms",
               "if >0, sample per-conn QUIC stats every N ms during the broadcast and dump a timeline at end",
               [&](td::Slice s) { sample_period_ms = std::stod(s.str()); });
  auto S = p.run(argc, argv);
  if (S.is_error()) {
    std::fprintf(stderr, "%s\n", S.error().message().c_str());
    return 1;
  }
  if (n_nodes < 6) {
    std::fprintf(stderr, "need at least 6 nodes for FEC to engage\n");
    return 1;
  }
  if (payload_size < 16) {
    std::fprintf(stderr, "payload_size must be >= 16\n");
    return 1;
  }

  SET_VERBOSITY_LEVEL(verbosity_INFO + verbosity);

  std::vector<PerReceiver> per_receiver(n_nodes);
  double broadcast_start_time = 0;

  std::string db_root_base = "tmp-dir-bench-broadcast-twostep";
  td::rmrf(db_root_base).ignore();
  td::mkdir(db_root_base).ensure();

  std::vector<Node> nodes(n_nodes);
  OverlayIdShort overlay_short;

  td::actor::Scheduler scheduler({16});

  scheduler.run_in_context([&] {
    for (size_t i = 0; i < n_nodes; ++i) {
      auto &nd = nodes[i];
      nd.idx = static_cast<int>(i);
      // ADNL on base+2i; QUIC on adnl_port+1000.
      nd.adnl_port = static_cast<td::uint16>(base_port + 2 * i);
      nd.addr.init_ipv4_port("127.0.0.1", nd.adnl_port).ensure();
      nd.db_root = db_root_base + "/node-" + std::to_string(i);
      td::mkdir(nd.db_root).ensure();

      nd.pk = ton::PrivateKey{ton::privkeys::Ed25519::random()};
      auto pub = nd.pk.compute_public_key();
      nd.adnl_full = ton::adnl::AdnlNodeIdFull{pub};
      nd.adnl_short = ton::adnl::AdnlNodeIdShort{pub.compute_short_id()};

      nd.addr_list.add_udp_adnl_address(nd.addr).ensure();
      nd.addr_list.set_version(static_cast<td::int32>(td::Clocks::system()));
      nd.addr_list.set_reinit_date(ton::adnl::Adnl::adnl_start_time());

      nd.keyring = ton::keyring::Keyring::create(nd.db_root);
      td::actor::send_closure(nd.keyring, &ton::keyring::Keyring::add_key, nd.pk, true, [](td::Result<>) {});

      nd.network_manager = ton::adnl::AdnlNetworkManager::create(nd.adnl_port);
      nd.adnl = ton::adnl::Adnl::create(nd.db_root, nd.keyring.get());
      td::actor::send_closure(nd.adnl, &ton::adnl::Adnl::register_network_manager, nd.network_manager.get());

      ton::adnl::AdnlCategoryMask cat_mask;
      cat_mask[0] = true;
      td::actor::send_closure(nd.network_manager, &ton::adnl::AdnlNetworkManager::add_self_addr, nd.addr,
                              std::move(cat_mask), static_cast<td::uint32>(0));
      td::actor::send_closure(nd.adnl, &ton::adnl::Adnl::add_id, nd.adnl_full, nd.addr_list, static_cast<td::uint8>(0));

      nd.quic = td::actor::create_actor<ton::quic::QuicSender>(
          "quic", td::actor::actor_dynamic_cast<ton::adnl::AdnlPeerTable>(nd.adnl.get()), nd.keyring.get(),
          ton::quic::QuicServer::Options{
              .new_connection_rate_limit_capacity = 1000,
          });
      td::actor::send_closure(nd.quic, &ton::quic::QuicSender::add_id, nd.adnl_short);

      nd.overlays = ton::overlay::Overlays::create(nd.db_root, nd.keyring.get(), nd.adnl.get(),
                                                   td::actor::ActorId<ton::dht::Dht>{});

      // Subscribe a stub handler so warmup queries with prefix "warm" get answered.
      td::actor::send_closure(nd.adnl, &ton::adnl::Adnl::subscribe, nd.adnl_short, std::string("warm"),
                              std::make_unique<WarmupAdnlCallback>());
    }

    // Mesh: introduce every node to every other node's full id + address list.
    for (size_t i = 0; i < n_nodes; ++i) {
      for (size_t j = 0; j < n_nodes; ++j) {
        if (i == j) continue;
        td::actor::send_closure(nodes[i].adnl, &ton::adnl::Adnl::add_peer, nodes[i].adnl_short, nodes[j].adnl_full,
                                nodes[j].addr_list);
      }
    }

    auto overlay_full_bytes =
        ton::create_serialize_tl_object<ton::ton_api::pub_overlay>(td::BufferSlice("bench-twostep"));
    overlay_short = OverlayIdFull{overlay_full_bytes.clone()}.compute_short_id();

    std::vector<AdnlNodeIdShort> peers;
    peers.reserve(n_nodes);
    for (auto &nd : nodes) {
      peers.push_back(nd.adnl_short);
    }

    for (size_t i = 0; i < n_nodes; ++i) {
      auto &nd = nodes[i];
      ton::overlay::OverlayOptions opts;
      opts.send_twostep_broadcast_ = true;
      opts.twostep_broadcast_sender_ = nd.quic.get();
      opts.allow_old_broadcasts_ = false;
      opts.nodes_to_send_ = static_cast<td::uint32>(n_nodes - 1);
      opts.max_peers_ = static_cast<td::uint32>(n_nodes);
      opts.max_neighbours_ = static_cast<td::uint32>(n_nodes - 1);
      opts.propagate_broadcast_to_ = static_cast<td::uint32>(n_nodes - 1);

      ton::overlay::OverlayPrivacyRules rules(
          static_cast<td::uint32>(payload_size + 1024),
          ton::overlay::CertificateFlags::AllowFec | ton::overlay::CertificateFlags::Trusted, {});

      td::actor::send_closure(nd.overlays, &ton::overlay::Overlays::create_private_overlay_ex, nd.adnl_short,
                              OverlayIdFull{overlay_full_bytes.clone()}, peers,
                              std::make_unique<RecvCallback>(&per_receiver[i], &broadcast_start_time, nd.idx), rules,
                              std::string(""), opts);
    }
  });

  // Hand off to the coroutine driver.
  scheduler.run_in_context([&] {
    td::actor::create_actor<Driver>("driver", &nodes, &per_receiver, &broadcast_start_time, overlay_short, payload_size,
                                    db_root_base, PerfCtl{perf_ctl, perf_ack}, sample_period_ms)
        .release();
  });
  scheduler.run();  // driver _exit(0)s when done
  return 0;
}
