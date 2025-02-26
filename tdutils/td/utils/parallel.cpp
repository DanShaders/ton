/*
   [tbd] licensing terms
 */

#include "td/utils/parallel.h"

namespace td {

namespace parallel {

namespace impl{

std::unique_ptr<Parallel> instance_;
std::unique_ptr<Parallel>& instance() { return instance_; }

} // namespace impl

void Parallel::shutdown() {

    lg_chan.send(nullptr, "zzzz");

    for (int k = 0; k < worker_count; k++) {
      wl_chan.send(nullptr, "zzzz");
    }

    generator_thread.join();

    for (int k = 0; k < worker_count; k++) {
      worker_thread[k].join();
    }
}

void Parallel::run_(LoadGenerator &load_gene) {
  run_concurrently(load_gene);
}

void Parallel::run_sequentially(LoadGenerator &load_gene) {
  auto tid = get_thread_id();
  for (;;) {
    auto comp6n = load_gene.next();
    if (!comp6n) { break; }

    // [fyi] send comp6n to worker
    // [fyi] on worker: send rcvc to caller thread
    auto rcvc = comp6n(tid);

    // [fyi] on caller thread
    rcvc();

    if (load_gene.canceled()) { break; }
  }
}

void Parallel::run_concurrently(LoadGenerator &load_gene) {
  CHECK(!load_gene.running());

  lg_chan.send(&load_gene, "lgen");

  auto lg_running = !0;
  for (;;) {
    ReceiverComp6n rcvc;
    rr_chan.recv(rcvc, "rres");

    if (!rcvc) {
      CHECK(lg_running);
      lg_running = 0;
      if (load_gene.pending()) { continue; }
      break;
    }

    load_gene.pending_less();
    rcvc();

    if (load_gene.pending()) { continue; }
    if (!lg_running) { break; }
  }

  CHECK(!load_gene.running());
  CHECK(!load_gene.pending());
}

void setup() {
  CHECK(!impl::instance_);
  impl::instance_ = std::move(Parallel::make_instance());
}
void cleanup() {
  CHECK(impl::instance_);
  impl::instance_.reset();
}

} // namespace parallel

} // namespace td
