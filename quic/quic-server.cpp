#include "quic-server.h"

#include <utility>

#include "td/actor/actor.h"
#include "td/utils/logging.h"

#include "quic-worker.h"

namespace ton::quic {

// Bridge from the worker thread to the QuicServer actor. Every event is packed
// into a closure and enqueued on the actor's events_ queue; the queue's
// eventfd is subscribed to the scheduler poll so the actor wakes up.
class QuicServer::SinkImpl final : public QuicWorkerEventSink {
 public:
  SinkImpl(std::shared_ptr<td::MpscPollableQueue<UniqueFn>> queue, Callback* callback)
      : queue_(std::move(queue)), callback_(callback) {
  }

  void on_connected(QuicConnectionId cid, td::SecureString local_public_key, td::SecureString peer_public_key,
                    bool is_outbound) override {
    auto cb = callback_;
    queue_->writer_put([cb, cid, local = std::move(local_public_key), peer = std::move(peer_public_key),
                        is_outbound]() mutable {
      auto status = cb->on_connected(cid, std::move(local), std::move(peer), is_outbound);
      if (status.is_error()) {
        LOG(WARNING) << "on_connected failed for " << cid << ": " << status;
      }
    });
    queue_->writer_flush();
  }

  void on_stream(QuicConnectionId cid, QuicStreamID sid, td::BufferSlice data, bool is_end) override {
    auto cb = callback_;
    queue_->writer_put([cb, cid, sid, data = std::move(data), is_end]() mutable {
      auto status = cb->on_stream(cid, sid, std::move(data), is_end);
      if (status.is_error()) {
        LOG(DEBUG) << "on_stream failed cid=" << cid << " sid=" << sid << ": " << status;
      }
    });
    queue_->writer_flush();
  }

  void on_closed(QuicConnectionId cid) override {
    auto cb = callback_;
    queue_->writer_put([cb, cid] { cb->on_closed(cid); });
    queue_->writer_flush();
  }

  void on_stream_closed(QuicConnectionId cid, QuicStreamID sid) override {
    auto cb = callback_;
    queue_->writer_put([cb, cid, sid] { cb->on_stream_closed(cid, sid); });
    queue_->writer_flush();
  }

 private:
  std::shared_ptr<td::MpscPollableQueue<UniqueFn>> queue_;
  Callback* callback_;
};

td::Result<td::actor::ActorOwn<QuicServer>> QuicServer::create(int port, std::unique_ptr<Callback> callback,
                                                               td::uint64 default_mtu, td::Slice alpn,
                                                               td::Slice bind_host) {
  return create(port, std::move(callback), default_mtu, alpn, bind_host, Options{});
}

td::Result<td::actor::ActorOwn<QuicServer>> QuicServer::create(int port, std::unique_ptr<Callback> callback,
                                                               td::uint64 default_mtu, td::Slice alpn,
                                                               td::Slice bind_host, Options options) {
  CHECK(callback);
  td::IPAddress local_addr;
  TRY_STATUS(local_addr.init_host_port(bind_host.str(), port));

  TRY_RESULT(fd, td::UdpSocketFd::open(local_addr));

  // Default kernel UDP buffers (~208 KB on Linux) overflow trivially under
  // burst load — at 1 Gbps that's ~1.7 ms of buffering, less than one actor
  // hop. Bump to whatever the kernel allows (capped by net.core.{rmem,wmem}_max,
  // typically 4 MB). Loss inside the kernel UDP queue then shows up as
  // ngtcp2-reported "lost" bytes, even on loopback.
  auto rcv = fd.maximize_rcv_buffer();
  if (rcv.is_error()) {
    LOG(WARNING) << "QuicServer: maximize_rcv_buffer failed: " << rcv.error();
  } else {
    LOG(INFO) << "QuicServer: udp rcv buffer set to " << rcv.ok() << " bytes";
  }
  auto snd = fd.maximize_snd_buffer();
  if (snd.is_error()) {
    LOG(WARNING) << "QuicServer: maximize_snd_buffer failed: " << snd.error();
  } else {
    LOG(INFO) << "QuicServer: udp snd buffer set to " << snd.ok() << " bytes";
  }

  auto name = PSTRING() << "QUIC:" << local_addr;
  return td::actor::create_actor<QuicServer>(td::actor::ActorOptions().with_name(name), std::move(fd), default_mtu,
                                             td::BufferSlice(alpn), std::move(callback), options);
}

QuicServer::QuicServer(td::UdpSocketFd fd, td::uint64 default_mtu, td::BufferSlice alpn,
                       std::unique_ptr<Callback> callback, Options options)
    : pending_fd_(std::move(fd))
    , alpn_(std::move(alpn))
    , options_(options)
    , default_mtu_(default_mtu)
    , callback_(std::move(callback)) {
  events_ = std::make_shared<td::MpscPollableQueue<UniqueFn>>();
  events_->init();
}

QuicServer::~QuicServer() {
  if (worker_) {
    worker_->stop();
    worker_.reset();
  }
  if (events_) {
    events_->destroy();
  }
}

// Wraps a Promise<T> such that calling set_value/set_error on the wrapper from
// any thread routes the resolution onto the actor thread (where it's safe to
// touch actor-framework internals like coroutine schedulers).
template <class T>
static td::Promise<T> wrap_promise_for_actor(td::Promise<T> p,
                                              std::shared_ptr<td::MpscPollableQueue<ton::quic::UniqueFn>> q) {
  return td::PromiseCreator::lambda(
      [p = std::move(p), q = std::move(q)](td::Result<T> r) mutable {
        q->writer_put(ton::quic::UniqueFn{
            [p = std::move(p), r = std::move(r)]() mutable { p.set_result(std::move(r)); }});
        q->writer_flush();
      });
}

td::uint64 QuicServer::mtu_for(adnl::AdnlNodeIdShort local_id, adnl::AdnlNodeIdShort peer_id) const {
  td::uint64 mtu = default_mtu_;
  if (auto it = default_mtu_by_local_id_.find(local_id); it != default_mtu_by_local_id_.end()) {
    mtu = std::max(mtu, it->second);
  }
  if (auto it = peers_mtu_.find({local_id, peer_id}); it != peers_mtu_.end()) {
    mtu = std::max(mtu, it->second);
  }
  return mtu;
}

void QuicServer::start_up() {
  LOG(INFO) << "starting up";
  self_id_ = actor_id(this);

  // Wire the user callback's MTU lookup to our actor-thread state.
  callback_->set_peer_mtu_callback([this](adnl::AdnlNodeIdShort local_id, adnl::AdnlNodeIdShort peer_id) {
    return mtu_for(local_id, peer_id);
  });

  auto sink = std::make_unique<SinkImpl>(events_, callback_.get());
  worker_ = std::make_unique<QuicWorker>(std::move(pending_fd_), std::move(alpn_), std::move(sink), options_);
  worker_->start("quic-io");

  // Try to subscribe the events_ queue's eventfd to the scheduler poll for
  // zero-latency wakeups. start_up may be dispatched on a cpu worker (no poll
  // context) — in that case fall back to a tight alarm.
  auto* sched_ctx = td::actor::SchedulerContext::get_ptr();
  if (sched_ctx != nullptr && sched_ctx->has_poll()) {
    sched_ctx->get_poll().subscribe(events_->reader_get_event_fd().get_poll_info().extract_pollable_fd(this),
                                    td::PollFlags::Read());
    events_subscribed_ = true;
  } else {
    alarm_timestamp() = td::Timestamp::in(0.001);
  }
  LOG(INFO) << "startup completed (events_subscribed=" << events_subscribed_ << ")";
}

void QuicServer::tear_down() {
  if (events_subscribed_) {
    auto* sched_ctx = td::actor::SchedulerContext::get_ptr();
    if (sched_ctx != nullptr && sched_ctx->has_poll()) {
      sched_ctx->get_poll().unsubscribe(events_->reader_get_event_fd().get_poll_info().get_pollable_fd_ref());
    }
    events_subscribed_ = false;
  }
  if (worker_) {
    worker_->stop();
    worker_.reset();
  }
  LOG(INFO) << "tear down";
}

void QuicServer::hangup() {
  stop();
}

void QuicServer::hangup_shared() {
  LOG(ERROR) << "unexpected hangup_shared signal";
}

void QuicServer::notify() {
  td::actor::send_signals(self_id_, td::actor::ActorSignals::wakeup());
}

void QuicServer::drain_callback_events() {
  while (true) {
    int n = events_->reader_wait_nonblock();
    if (n == 0) {
      return;
    }
    for (int i = 0; i < n; i++) {
      auto fn = events_->reader_get_unsafe();
      fn();
    }
    events_->reader_flush();
  }
}

void QuicServer::loop() {
  // reader_wait_nonblock consumes pending eventfd events internally.
  drain_callback_events();
  // User stream timeouts run on alarm() only, not on every wakeup.
  if (callback_) {
    td::Timestamp next = callback_->next_alarm();
    if (next) {
      alarm_timestamp().relax(next);
    }
  }
}

void QuicServer::alarm() {
  drain_callback_events();
  if (callback_) {
    StreamShutdownList shutdown;
    callback_->loop(td::Timestamp::now(), shutdown);
    for (auto& e : shutdown.entries) {
      shutdown_stream(e.cid, e.sid);
    }
    td::Timestamp next = callback_->next_alarm();
    if (next) {
      alarm_timestamp().relax(next);
    }
  }
  if (!events_subscribed_) {
    // Re-arm short alarm so we keep polling events_.
    alarm_timestamp().relax(td::Timestamp::in(0.001));
  }
}

// --- forwarders ---

void QuicServer::open_stream(QuicConnectionId cid, StreamOptions options, td::Promise<QuicStreamID> promise) {
  if (!worker_) {
    promise.set_error(td::Status::Error("server stopped"));
    return;
  }
  // Wrap the outer promise so its resolution bounces back to the actor thread.
  // Also inject set_stream_options notification on the actor thread.
  auto cb_ptr = callback_.get();
  td::Promise<QuicStreamID> outer{td::PromiseCreator::lambda(
      [promise = std::move(promise), cb_ptr, cid, options](td::Result<QuicStreamID> r) mutable {
        if (r.is_error()) {
          promise.set_error(r.move_as_error());
          return;
        }
        QuicStreamID sid = r.move_as_ok();
        cb_ptr->set_stream_options(cid, sid, options);
        promise.set_value(QuicStreamID(sid));
      })};
  auto worker_promise = wrap_promise_for_actor(std::move(outer), events_);
  worker_->post([w = worker_.get(), cid, options, worker_promise = std::move(worker_promise)]() mutable {
    w->wt_open_stream(cid, options, std::move(worker_promise));
  });
}

void QuicServer::send_stream(QuicConnectionId cid, std::variant<QuicStreamID, StreamOptions> stream,
                             td::BufferSlice data, bool is_end, td::Promise<QuicStreamID> promise) {
  if (!worker_) {
    promise.set_error(td::Status::Error("server stopped"));
    return;
  }
  // If a new stream is being opened we also need to notify the user callback on
  // the actor side once we know the sid.
  bool opens_stream = std::holds_alternative<StreamOptions>(stream);
  td::Promise<QuicStreamID> outer = std::move(promise);
  if (opens_stream) {
    StreamOptions options = std::get<StreamOptions>(stream);
    auto cb_ptr = callback_.get();
    outer = td::Promise<QuicStreamID>(td::PromiseCreator::lambda(
        [promise = std::move(outer), cb_ptr, cid, options](td::Result<QuicStreamID> r) mutable {
          if (r.is_error()) {
            promise.set_error(r.move_as_error());
            return;
          }
          QuicStreamID sid = r.move_as_ok();
          cb_ptr->set_stream_options(cid, sid, options);
          promise.set_value(QuicStreamID(sid));
        }));
  }
  auto worker_promise = wrap_promise_for_actor(std::move(outer), events_);
  worker_->post([w = worker_.get(), cid, stream = std::move(stream), data = std::move(data), is_end,
                 worker_promise = std::move(worker_promise)]() mutable {
    w->wt_send_stream(cid, std::move(stream), std::move(data), is_end, std::move(worker_promise));
  });
}

void QuicServer::connect(std::string host, int port, td::Ed25519::PrivateKey client_key, std::string alpn,
                         std::string sni, td::Promise<QuicConnectionId> promise) {
  if (!worker_) {
    promise.set_error(td::Status::Error("server stopped"));
    return;
  }
  td::IPAddress remote_address;
  auto status = remote_address.init_host_port(host, port);
  if (status.is_error()) {
    promise.set_error(std::move(status));
    return;
  }
  auto worker_promise = wrap_promise_for_actor(std::move(promise), events_);
  worker_->post([w = worker_.get(), remote_address, client_key = std::move(client_key), alpn, sni,
                 worker_promise = std::move(worker_promise)]() mutable {
    w->wt_connect(remote_address, std::move(client_key), std::move(alpn), std::move(sni),
                  std::move(worker_promise));
  });
}

void QuicServer::shutdown_stream(QuicConnectionId cid, QuicStreamID sid) {
  if (!worker_) {
    return;
  }
  worker_->post([w = worker_.get(), cid, sid] { w->wt_shutdown_stream(cid, sid); });
}

void QuicServer::on_connection_closed(QuicConnectionId cid) {
  if (!worker_) {
    return;
  }
  worker_->post([w = worker_.get(), cid] { w->wt_on_connection_closed(cid); });
}

void QuicServer::log_stats(std::string reason) {
  if (!worker_) {
    return;
  }
  worker_->post([w = worker_.get(), reason = std::move(reason)]() mutable { w->wt_log_stats(std::move(reason)); });
}

void QuicServer::set_default_mtu(adnl::AdnlNodeIdShort local_id, td::uint64 mtu) {
  if (mtu == 0) {
    default_mtu_by_local_id_.erase(local_id);
  } else {
    default_mtu_by_local_id_[local_id] = mtu;
  }
}

void QuicServer::set_peer_mtu(adnl::AdnlNodeIdShort local_id, adnl::AdnlNodeIdShort peer_id, td::uint64 mtu) {
  if (mtu == 0) {
    peers_mtu_.erase({local_id, peer_id});
  } else {
    peers_mtu_[{local_id, peer_id}] = mtu;
  }
}

void QuicServer::add_identity(adnl::AdnlNodeIdShort local_id, td::Ed25519::PrivateKey key) {
  if (!worker_) {
    return;
  }
  // Move the key into a shared_ptr because PrivateKey is non-copyable but lambdas need to capture it.
  auto key_holder = std::make_shared<td::Ed25519::PrivateKey>(key.as_octet_string());
  worker_->post([w = worker_.get(), local_id, key_holder] {
    w->wt_add_identity(local_id, td::Ed25519::PrivateKey(key_holder->as_octet_string()));
  });
}

void QuicServer::collect_stats(td::Promise<Stats> P) {
  if (!worker_) {
    P.set_error(td::Status::Error("server stopped"));
    return;
  }
  auto worker_promise = wrap_promise_for_actor(std::move(P), events_);
  worker_->post([w = worker_.get(), worker_promise = std::move(worker_promise)]() mutable {
    w->wt_collect_stats(std::move(worker_promise));
  });
}

}  // namespace ton::quic
