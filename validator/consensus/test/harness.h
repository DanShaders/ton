/*
 * Copyright (c) 2026, TON CORE TECHNOLOGIES CO. L.L.C
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */
#pragma once

#include <memory>
#include <utility>
#include <variant>
#include <vector>

#include "td/actor/BusRuntime.h"
#include "td/actor/BusUtils.h"
#include "td/actor/Mocks.h"
#include "td/actor/TestScheduler.h"
#include "td/utils/buffer.h"
#include "td/utils/logging.h"
#include "td/utils/tests.h"
#include "td/utils/type_traits.h"
#include "validator/consensus/bus.h"
#include "validator/consensus/simplex/bus.h"
#include "validator/consensus/test/equality.h"
#include "validator/consensus/test/fixtures.h"

namespace ton::validator::consensus::test {

// Convenience: a MockedBus over consensus::simplex::Bus that lists `Es...` as
// captured events. Test files do `using FooBus = TestBus<EventA, RequestB, ...>;`.
template <typename... Es>
using TestBus = td::actor::MockedBus<consensus::simplex::Bus, Es...>;

// SimplexTest<ActorUnderTest, BusType>
//
// Owns a TestScheduler, a runtime, and a bus handle. The bus is built from the
// options() hook, validators and keys are installed via test_fixtures, and the
// MockedActorFor<BusType> is registered as the listener that captures events.
//
// Subclasses override:
//   options()       — returns TestBusOptions for this test
//   configure(Bus&) — installs Db / Keyring / extra config before run
//   run_test()      — the scenario coroutine
//
// Helpers exposed to scenarios:
//   send_event<E>(e)        publish a fire-and-forget event on the bus
//   send_request<R>(r)      publish a request and obtain its Task<ReturnType>
//   returns<R>(value)       enqueue a scripted response for the next call to R
//   expect_events(e1, e2…)  strict ordered match against the captured log + clear
//   count_events<E>()       autogen-style lax accessor
//   events_of<E>()          autogen-style lax accessor
template <typename ActorUnderTest, typename BusType>
class SimplexTest : public td::Test {
 protected:
  using Bus = BusType;
  using Actor = td::actor::MockedActorFor<BusType>;

  td::actor::TestScheduler ts_;
  td::actor::BusHandle<BusType> bh_;
  std::vector<PrivateKey> keys_;

  virtual TestBusOptions options() const {
    return TestBusOptions{};
  }

  virtual void configure(Bus&) {
  }

  virtual td::actor::Task<> run_test() = 0;

  template <td::actor::detail::ValidEventFor<consensus::simplex::Bus> E>
  void send_event(E event) {
    bh_.publish(std::make_shared<E>(std::move(event)));
  }

  template <td::actor::detail::ValidRequestFor<consensus::simplex::Bus> R>
  td::actor::Task<typename R::ReturnType> send_request(R request) {
    return bh_.publish(std::make_shared<R>(std::move(request)));
  }

  template <td::In<typename Bus::Calls> R>
  void returns(typename R::ReturnType value) {
    std::get<td::IndexIn<R, typename Bus::Calls>>(bh_->results_).push_back({std::move(value), std::nullopt});
  }

  template <td::In<typename Bus::Logs>... E>
  void expect_events(const E&... e) {
    auto& es = bh_->events_;
    EXPECT_EQ(es.size(), sizeof...(E));
    if (es.size() == sizeof...(E)) {
      auto it = es.begin();
      (
          [&]() {
            if (!(std::holds_alternative<std::shared_ptr<const E>>(*it) &&
                  td::actor::events_equal(*std::get<std::shared_ptr<const E>>(*it), e))) {
              LOG(ERROR) << "Expectation failed: mismatched event ("
                         << std::visit([](const auto& v) { return v->contents_to_string(); }, *it)
                         << " != " << e.contents_to_string() << ")";
              td::TestContext::get()->register_test_failure();
            }
            ++it;
          }(),
          ...);
    }
    es.clear();
  }

  // Autogen-style lax accessors for tests that don't care about strict ordering.
  template <typename E>
  size_t count_events() const {
    return td::actor::count_events<E>(bh_->events_);
  }

  template <typename E>
  auto events_of() const {
    return td::actor::events_of<E>(bh_->events_);
  }

  void clear_events() {
    bh_->events_.clear();
  }

  void run() override {
    ts_.run([this]() -> td::actor::Task<> {
      td::actor::Runtime runtime;
      ActorUnderTest::register_in(runtime);
      runtime.register_actor<Actor>("Mock");

      auto bus = std::make_shared<BusType>();
      install_validators(*bus, keys_, options());
      bus->populate_collator_schedule();
      configure(*bus);

      bh_ = runtime.start(std::move(bus));
      co_await run_test();
      send_event<consensus::StopRequested>({});
      co_return {};
    });
  }
};

}  // namespace ton::validator::consensus::test
