/*
 * Two-node QUIC throughput microbench.
 *
 * Strips everything except the QUIC path: two nodes on 127.0.0.1, both joined
 * to a tiny private overlay, sender fires N messages of S bytes back-to-back
 * via Overlays::send_message_via(dst, src, overlay_id, data, quic_sender). The
 * receiver's Overlays::Callback::receive_message records arrival times. Goal:
 * isolate QUIC's single-connection per-message latency from broadcast/rebroadcast/
 * decoder overhead.
 *
 * Usage: bench-quic-throughput [--size BYTES] [--count N] [--port BASE]
 */

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
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

using ton::adnl::AdnlNodeIdShort;
using ton::overlay::OverlayIdFull;
using ton::overlay::OverlayIdShort;

struct Arrival {
  double t_enter_ms = 0;
  double t_arrive_ms = 0;
  size_t bytes = 0;
};

struct Shared {
  std::atomic<size_t> received{0};
  std::vector<Arrival> arrivals;
  double t0 = 0;  // wall-clock anchor (bench start)
};

class RecvCallback final : public ton::overlay::Overlays::Callback {
 public:
  explicit RecvCallback(Shared *s) : s_(s) {
  }
  void receive_message(AdnlNodeIdShort, OverlayIdShort, td::BufferSlice data) override {
    if (data.size() < 16) return;
    td::uint64 seqno;
    td::uint64 send_us;
    std::memcpy(&seqno, data.data(), 8);
    std::memcpy(&send_us, data.data() + 8, 8);
    if (seqno >= s_->arrivals.size()) return;
    auto &a = s_->arrivals[seqno];
    a.t_enter_ms = static_cast<double>(send_us) / 1000.0 - s_->t0 * 1000.0;
    a.t_arrive_ms = td::Time::now() * 1000.0 - s_->t0 * 1000.0;
    a.bytes = data.size();
    s_->received.fetch_add(1, std::memory_order_relaxed);
  }
  void check_broadcast(ton::PublicKeyHash, OverlayIdShort, td::BufferSlice, td::Promise<td::Unit> p) override {
    p.set_value(td::Unit());
  }

 private:
  Shared *s_;
};

struct Node {
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

class Driver final : public td::actor::Actor {
 public:
  Driver(Node *sender, Node *receiver, OverlayIdShort overlay, size_t msg_size, size_t count, Shared *shared,
         std::string db_root_base)
      : sender_(sender)
      , receiver_(receiver)
      , overlay_(overlay)
      , msg_size_(msg_size)
      , count_(count)
      , shared_(shared)
      , db_root_base_(std::move(db_root_base)) {
  }
  void start_up() override {
    run().start_immediate().detach("driver");
  }

 private:
  td::actor::Task<> run() {
    co_await warmup_quic();
    co_await measure();
    std::fflush(stderr);
    td::rmrf(db_root_base_).ignore();
    _exit(0);
  }

  // Force the QUIC connection to handshake before timing anything.
  td::actor::Task<> warmup_quic() {
    auto t0 = td::Time::now();
    for (int attempt = 1; attempt <= 30; ++attempt) {
      auto [task, promise] = td::actor::StartedTask<td::BufferSlice>::make_bridge();
      td::actor::send_closure(sender_->quic, &ton::quic::QuicSender::send_query, sender_->adnl_short,
                              receiver_->adnl_short, std::string("warm"), std::move(promise),
                              td::Timestamp::in(0.3), td::BufferSlice("warm-up"));
      auto r = co_await std::move(task).wrap();
      if (r.is_ok()) {
        std::fprintf(stderr, "warmup: ok on attempt %d (%.1f ms)\n", attempt, (td::Time::now() - t0) * 1000.0);
        co_return {};
      }
      co_await td::actor::coro_sleep(td::Timestamp::in(0.05));
    }
    std::fprintf(stderr, "warmup: gave up\n");
    co_return {};
  }

  td::actor::Task<> measure() {
    shared_->arrivals.assign(count_, Arrival{});
    shared_->t0 = td::Time::now();

    std::vector<char> filler(msg_size_);
    for (size_t i = 16; i < msg_size_; ++i) {
      filler[i] = static_cast<char>(td::Random::fast_uint32() & 0xff);
    }

    std::fprintf(stderr, "measure: pumping %zu messages of %zu bytes\n", count_, msg_size_);
    double pump_start = td::Time::now();
    for (size_t i = 0; i < count_; ++i) {
      td::BufferSlice buf(msg_size_);
      td::uint64 seqno = i;
      td::uint64 send_us = static_cast<td::uint64>(td::Time::now() * 1e6);
      std::memcpy(buf.as_slice().begin(), &seqno, 8);
      std::memcpy(buf.as_slice().begin() + 8, &send_us, 8);
      std::memcpy(buf.as_slice().begin() + 16, filler.data() + 16, msg_size_ - 16);
      td::actor::send_closure(sender_->overlays, &ton::overlay::Overlays::send_message_via, receiver_->adnl_short,
                              sender_->adnl_short, overlay_, std::move(buf),
                              td::actor::ActorId<ton::adnl::AdnlSenderInterface>{sender_->quic.get()});
    }
    double pump_done = td::Time::now();
    std::fprintf(stderr, "measure: enqueued %zu sends in %.2f ms\n", count_, (pump_done - pump_start) * 1000.0);

    double deadline = pump_done + 60.0;
    while (shared_->received.load() < count_) {
      if (td::Time::now() > deadline) {
        std::fprintf(stderr, "timeout: %zu/%zu received\n", shared_->received.load(), count_);
        break;
      }
      co_await td::actor::coro_sleep(td::Timestamp::in(0.005));
    }
    double done = td::Time::now();

    std::vector<double> e2e_ms;
    e2e_ms.reserve(count_);
    double last_arrive = 0;
    for (auto &a : shared_->arrivals) {
      if (a.bytes == 0) continue;
      e2e_ms.push_back(a.t_arrive_ms - a.t_enter_ms);
      last_arrive = std::max(last_arrive, a.t_arrive_ms);
    }
    std::sort(e2e_ms.begin(), e2e_ms.end());
    auto pct = [&](double q) {
      if (e2e_ms.empty()) return 0.0;
      double idx = q * static_cast<double>(e2e_ms.size() - 1);
      auto lo = static_cast<size_t>(idx);
      auto hi = std::min(e2e_ms.size() - 1, lo + 1);
      double f = idx - static_cast<double>(lo);
      return e2e_ms[lo] * (1 - f) + e2e_ms[hi] * f;
    };
    double total_bytes = static_cast<double>(count_ * msg_size_);
    double total_s = done - pump_start;
    std::fprintf(stderr,
                 "\n# QUIC throughput (2 nodes, loopback, single connection via overlay::send_message_via)\n"
                 "# msg_size=%zu B  count=%zu  delivered=%zu\n"
                 "# pump_enqueue=%.2f ms  total_wall=%.2f ms (last arrive %.2f ms after enqueue start)\n"
                 "# throughput  %.2f MB/s  (%.2f Gbps)\n"
                 "# per-msg e2e latency: min=%.2f  p50=%.2f  p90=%.2f  p99=%.2f  max=%.2f ms\n",
                 msg_size_, count_, e2e_ms.size(),
                 (pump_done - pump_start) * 1000.0, total_s * 1000.0, last_arrive,
                 total_bytes / total_s / 1e6, total_bytes / total_s / 1e9 * 8,
                 e2e_ms.empty() ? 0 : e2e_ms.front(), pct(0.5), pct(0.9), pct(0.99),
                 e2e_ms.empty() ? 0 : e2e_ms.back());

    std::fprintf(stderr, "\n# inter-arrival timing (ms from pump start):\n");
    std::fprintf(stderr, "# %-6s %-10s %-10s %-10s\n", "seq", "enter", "arrive", "e2e");
    size_t head = std::min<size_t>(count_, 16);
    for (size_t i = 0; i < head; ++i) {
      auto &a = shared_->arrivals[i];
      std::fprintf(stderr, "  %-6zu %-10.2f %-10.2f %-10.2f\n", i, a.t_enter_ms, a.t_arrive_ms,
                   a.t_arrive_ms - a.t_enter_ms);
    }
    if (count_ > 16) {
      std::fprintf(stderr, "  ...\n");
      for (size_t i = count_ - 4; i < count_; ++i) {
        auto &a = shared_->arrivals[i];
        std::fprintf(stderr, "  %-6zu %-10.2f %-10.2f %-10.2f\n", i, a.t_enter_ms, a.t_arrive_ms,
                     a.t_arrive_ms - a.t_enter_ms);
      }
    }
    co_return {};
  }

  Node *sender_;
  Node *receiver_;
  OverlayIdShort overlay_;
  size_t msg_size_;
  size_t count_;
  Shared *shared_;
  std::string db_root_base_;
};

class WarmupCallback final : public ton::adnl::Adnl::Callback {
 public:
  void receive_message(AdnlNodeIdShort, AdnlNodeIdShort, td::BufferSlice) override {
  }
  void receive_query(AdnlNodeIdShort, AdnlNodeIdShort, td::BufferSlice, td::Promise<td::BufferSlice> p) override {
    p.set_value(td::BufferSlice("ok"));
  }
};

}  // namespace

int main(int argc, char *argv[]) {
  size_t msg_size = 1200000;
  size_t count = 16;
  td::uint16 base_port = 45000;
  int verbosity = 0;

  td::OptionParser p;
  p.add_option('\0', "size", "message bytes (default 1.2 MB)",
               [&](td::Slice s) { msg_size = std::stoul(s.str()); });
  p.add_option('\0', "count", "message count (default 16)", [&](td::Slice s) { count = std::stoul(s.str()); });
  p.add_option('\0', "port", "base UDP port (default 45000)",
               [&](td::Slice s) { base_port = static_cast<td::uint16>(std::stoul(s.str())); });
  p.add_option('v', "verbosity", "log verbosity offset (default 0)", [&](td::Slice s) { verbosity = std::stoi(s.str()); });
  auto S = p.run(argc, argv);
  if (S.is_error()) {
    std::fprintf(stderr, "%s\n", S.error().message().c_str());
    return 1;
  }
  if (msg_size < 32) {
    std::fprintf(stderr, "msg_size too small\n");
    return 1;
  }

  SET_VERBOSITY_LEVEL(verbosity_INFO + verbosity);

  std::string db_root_base = "tmp-dir-bench-quic-throughput";
  td::rmrf(db_root_base).ignore();
  td::mkdir(db_root_base).ensure();

  Shared shared;
  Node nodes[2];
  OverlayIdShort overlay_short;

  td::actor::Scheduler scheduler({4});
  scheduler.run_in_context([&] {
    for (size_t i = 0; i < 2; ++i) {
      auto &nd = nodes[i];
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
          ton::quic::QuicServer::Options{.new_connection_rate_limit_capacity = 1000});
      td::actor::send_closure(nd.quic, &ton::quic::QuicSender::add_id, nd.adnl_short);

      nd.overlays = ton::overlay::Overlays::create(nd.db_root, nd.keyring.get(), nd.adnl.get(),
                                                   td::actor::ActorId<ton::dht::Dht>{});

      // Stub Adnl callback for the warmup query.
      td::actor::send_closure(nd.adnl, &ton::adnl::Adnl::subscribe, nd.adnl_short, std::string("warm"),
                              std::make_unique<WarmupCallback>());
    }

    td::actor::send_closure(nodes[0].adnl, &ton::adnl::Adnl::add_peer, nodes[0].adnl_short, nodes[1].adnl_full,
                            nodes[1].addr_list);
    td::actor::send_closure(nodes[1].adnl, &ton::adnl::Adnl::add_peer, nodes[1].adnl_short, nodes[0].adnl_full,
                            nodes[0].addr_list);

    auto overlay_full_bytes =
        ton::create_serialize_tl_object<ton::ton_api::pub_overlay>(td::BufferSlice("bench-quic-throughput"));
    overlay_short = OverlayIdFull{overlay_full_bytes.clone()}.compute_short_id();
    std::vector<AdnlNodeIdShort> peers{nodes[0].adnl_short, nodes[1].adnl_short};

    for (size_t i = 0; i < 2; ++i) {
      ton::overlay::OverlayOptions opts;
      opts.allow_old_broadcasts_ = false;
      opts.max_peers_ = 2;
      opts.max_neighbours_ = 1;
      // We don't actually use broadcasts here, but setting this is the only
      // way to make OverlayImpl call PeersMtuGuard on the QuicSender — without
      // it the receiver caps inbound stream size at 1 KB and our messages get
      // closed with "stream size limit exceeded".
      opts.twostep_broadcast_sender_ = nodes[i].quic.get();
      ton::overlay::OverlayPrivacyRules rules(
          static_cast<td::uint32>(msg_size + 1024),
          ton::overlay::CertificateFlags::AllowFec | ton::overlay::CertificateFlags::Trusted, {});
      td::actor::send_closure(nodes[i].overlays, &ton::overlay::Overlays::create_private_overlay_ex,
                              nodes[i].adnl_short, OverlayIdFull{overlay_full_bytes.clone()}, peers,
                              std::make_unique<RecvCallback>(&shared), rules, std::string(""), opts);
    }
  });

  scheduler.run_in_context([&] {
    td::actor::create_actor<Driver>("driver", &nodes[0], &nodes[1], overlay_short, msg_size, count, &shared, db_root_base)
        .release();
  });
  scheduler.run();
  return 0;
}
