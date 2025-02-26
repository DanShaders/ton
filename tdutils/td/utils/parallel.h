/*
   [tbd] licensing terms
 */
#pragma once

#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <chrono>

#include <functional>
#include <string>
#include <utility>

#include "td/utils/logging.h"
#include "td/utils/port/detail/ThreadIdGuard.h"

namespace td {

namespace parallel {

enum{ timeout_scale = 1*3999 };

static std::chrono::duration<int, std::milli> send_timeout(1500 * timeout_scale);
static std::chrono::duration<int, std::milli> recv_timeout(1500 * timeout_scale);

struct ChannelTraitsDefault {
    constexpr static std::size_t capacity{1};
    constexpr static bool multiple_senders{true};
    constexpr static bool multiple_receivers{true};
};

template<class T, class Traits = ChannelTraitsDefault>
class Channel {

 public:
  void send(T&& v, const char *tids) {

    std::unique_lock lk(mx);
    cv_send.wait_for(lk, send_timeout, [this]() { return size < capa; });

    CHECK(size < capa);

    if (capa == 1) {
      queue[0] = std::move(v);
    } else {
      queue[tail] = std::move(v);
      tail++;
      // [tbd] if (capa == 2^k) ... else
      if (capa <= tail) { tail = 0; }
    }
    size++;

    if (Traits::multiple_senders) {
      if (size < capa) {
        cv_send.notify_one();
      }
    }

    lk.unlock();
    cv_recv.notify_one();
  }

  void recv(T& v, const char *tids) {

    std::unique_lock lk(mx);
    cv_recv.wait_for(lk, recv_timeout, [this]() { return 0 < size; });

    CHECK(0 < size);

#if 00*9
    if (capa == 1) {
      v = std::move(queue[0]);
    } else {
#endif
      v = std::move(queue[head]);
      head++;
      // [tbd] if (capa == 2^k) ... else
      if (capa <= head) { head = 0; }
      size--;
#if 00*9
    }
#endif

    if (Traits::multiple_receivers) {
      if (0 < size) {
        cv_recv.notify_one();
      }
    }

    lk.unlock();
    cv_send.notify_one();
  }

  Channel(const char* name_) : name(name_) {}

 private:
  enum{ capa = Traits::capacity };

  const char* name;

  std::mutex mx;
  std::condition_variable cv_send;
  std::condition_variable cv_recv;

  // [fyi] accessed from wait predicate
  std::atomic<int> size{0};
  int head{0};
  int tail{0};
  T queue[capa];
};


class LoadGenerator {
 public:
  // [fyi] runs on caller thread
  using ReceiverComp6n = std::function<void ()>;
  // [fyi] runs on a worker thread
  using WorkerComp6n = std::function<ReceiverComp6n (int)>;

  virtual ~LoadGenerator() = default;

  // [fyi] runs on load generator thread
  virtual WorkerComp6n next() = 0;

  int running() const { return running_; }
  int pending() const { return pending_; }
  int canceled() const { return canceled_; }

 protected:
  void start() { running_++; }
  void stop() { running_ = 0; }

  void pending_more() { pending_++; }
  void pending_less() { pending_--; }

  void cancel() { canceled_++; stop(); }

 private:
  std::atomic<int> running_{0};
  std::atomic<int> pending_{0};
  std::atomic<int> canceled_{0};

  friend class Parallel;
};

class Parallel;

namespace impl{

extern std::unique_ptr<Parallel> instance_;
std::unique_ptr<Parallel>& instance();

} // namespace impl

class Parallel {
public:
  enum{ worker_count = 8 };

  // [fyi] not less than worker_count to prevent worker stalls
  enum{ workload_results_min = worker_count*2 };

  enum{ load_generator_channel_capacity = 1 };
  enum{ workload_channel_capacity = workload_results_min*4 };
  enum{ receiver_channel_capacity = workload_results_min*4 };

  struct Sequentially {};
  struct Concurrently {};

  static void run(LoadGenerator &load_gene) {
    impl::instance()->run_concurrently(load_gene);
  }

  static void run(LoadGenerator &load_gene, Sequentially) {
    impl::instance()->run_sequentially(load_gene);
  }

  static std::unique_ptr<Parallel> make_instance() {
    return std::unique_ptr<Parallel>{new Parallel()};
  }

private:
  Parallel() {
    auto load_generator = [this]() -> void {
      auto tig_ = td::detail::ThreadIdGuard();

      std::string tids("lg[");
      tids.append(std::to_string(get_thread_id())).append("]");

      LoadGenerator *load_gene;
      for (;;) {
        lg_chan.recv(load_gene, tids.c_str());

        if (!load_gene) { break; }

        load_gene->start();

        for (;;) {
          auto comp6n = load_gene->next();
          if (!comp6n) { break; }

          load_gene->pending_more();

          // [fyi] send comp6n to worker
          wl_chan.send(std::move(comp6n), tids.c_str());
        }

        load_gene->stop();

        rr_chan.send(nullptr, tids.c_str());
      }
    };

    auto worker = [this](int /* worker_index */) -> void {
      auto tig_ = td::detail::ThreadIdGuard();
      auto tid = get_thread_id();

      std::string tids("wl[");
      tids.append(std::to_string(tid)).append("]");

      for (;;) {
        WorkerComp6n comp6n;
        wl_chan.recv(comp6n, tids.c_str());

        if (!comp6n) { break; }

        // [fyi] on worker: send rcvc to caller thread
        auto rcvc = comp6n(tid);

        rr_chan.send(std::move(rcvc), tids.c_str());
      }
    };

    generator_thread = std::thread(std::function(load_generator));

    for (int k = 0; k < worker_count; k++) {
      worker_thread[k] = std::thread([worker, k](){ return worker(k); });
    }
  }

 public:
  ~Parallel() {
    shutdown();
  }

 private:
  void run_(LoadGenerator &load_gene);

  void run_sequentially(LoadGenerator &load_gene);
  void run_concurrently(LoadGenerator &load_gene);

  void shutdown();

  std::thread generator_thread;
  std::thread worker_thread [worker_count];

  using WorkerComp6n = LoadGenerator::WorkerComp6n;
  using ReceiverComp6n = LoadGenerator::ReceiverComp6n;

  // [fyi] load generator channel
  Channel<LoadGenerator*> lg_chan{Channel<LoadGenerator*>("lg_chan")};

  // [fyi] workload channel

  struct workload_channel_traits : ChannelTraitsDefault {
    constexpr static std::size_t capacity{workload_channel_capacity};
    constexpr static bool multiple_senders{false};
  };

  using wl_chan_type = Channel<WorkerComp6n, workload_channel_traits>;
  wl_chan_type wl_chan{wl_chan_type("wl_chan")};

  // [fyi] receive result channel

  struct receiver_channel_traits : ChannelTraitsDefault {
    constexpr static std::size_t capacity{receiver_channel_capacity};
    constexpr static bool multiple_receivers{false};
  };

  using rr_chan_type = Channel<ReceiverComp6n, receiver_channel_traits>;
  rr_chan_type rr_chan{rr_chan_type("rr_chan")};
};

void setup();
void cleanup();

} // namespace parallel

} // namespace td
