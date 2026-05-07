/*
 * Copyright (c) 2026, TON CORE TECHNOLOGIES CO. L.L.C
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */
#pragma once

#include <concepts>
#include <deque>
#include <memory>
#include <optional>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

#include "td/actor/BusRuntime.h"
#include "td/actor/coro_task.h"
#include "td/actor/coro_utils.h"
#include "td/utils/Status.h"
#include "td/utils/Time.h"
#include "td/utils/logging.h"
#include "td/utils/tests.h"
#include "td/utils/type_traits.h"

namespace td::actor {

using ::td::In;
using ::td::IndexIn;

// =============================================================================
// Type-list machinery for partitioning a tuple of bus event types into the
// requests (those with `ReturnType`) and plain events.
// =============================================================================

namespace detail {

template <typename T>
concept HasReturnType = requires { typename T::ReturnType; };

template <bool Keep, typename T>
using KeepIf = std::conditional_t<Keep, std::tuple<T>, std::tuple<>>;

template <typename Tuple>
struct PartitionRequestsHelper;

template <typename... Ts>
struct PartitionRequestsHelper<std::tuple<Ts...>> {
  using requests = decltype(std::tuple_cat(std::declval<KeepIf<HasReturnType<Ts>, Ts>>()...));
  using events = decltype(std::tuple_cat(std::declval<KeepIf<!HasReturnType<Ts>, Ts>>()...));
};

template <typename B, typename Tuple>
struct FilterPublishTargetsHelper;

template <typename B, typename... Ts>
struct FilterPublishTargetsHelper<B, std::tuple<Ts...>> {
  using type = decltype(std::tuple_cat(std::declval<KeepIf<ValidPublishTargetFor<Ts, B>, Ts>>()...));
};

}  // namespace detail

// Tuple of types from `Tuple` that are valid publish targets for bus `B`.
template <typename B, typename Tuple>
using FilterPublishTargets = typename detail::FilterPublishTargetsHelper<B, Tuple>::type;

// Subset of `Tuple` that are requests (have `::ReturnType`).
template <typename Tuple>
using RequestsOf = typename detail::PartitionRequestsHelper<Tuple>::requests;

// Subset of `Tuple` that are plain events.
template <typename Tuple>
using EventsOf = typename detail::PartitionRequestsHelper<Tuple>::events;

// =============================================================================
// MockBus<B, EventList, RequestList>
//
// Pretends to be the parent bus `B` while logging every published event into
// `events_` and serving each call from a queue installed with `returns<R>(...)`.
//
// EventList is the full tuple of types the mock observes (events + requests).
// RequestList is a subset whose entries have a `ReturnType` alias.
// =============================================================================

template <typename T>
struct MockResult {
  td::Result<typename T::ReturnType> value;
  std::optional<td::Timestamp> respond_at;
};

template <typename B, typename EventList, typename RequestList>
class MockBus : public B {
 public:
  using Parent = B;
  using Events = TypeList<>;
  using Logs = EventList;
  using Calls = RequestList;

 private:
  template <typename>
  struct EventsStorage;
  template <typename... Ts>
  struct EventsStorage<std::tuple<Ts...>> {
    using type = std::vector<std::variant<std::shared_ptr<const Ts>...>>;
  };

  template <typename>
  struct ResultsStorage;
  template <typename... Ts>
  struct ResultsStorage<std::tuple<Ts...>> {
    using type = std::tuple<std::deque<MockResult<Ts>>...>;
  };

 public:
  mutable typename EventsStorage<EventList>::type events_;
  mutable typename ResultsStorage<RequestList>::type results_;

  template <In<EventList> E>
  void log(std::shared_ptr<const E> event) const {
    events_.emplace_back(std::move(event));
  }

  template <typename R>
    requires(In<R, EventList> && In<R, RequestList>)
  Task<typename R::ReturnType> call(std::shared_ptr<const R> request) const {
    log<R>(std::move(request));
    auto& results = std::get<IndexIn<R, RequestList>>(results_);
    if (results.empty()) {
      LOG(ERROR) << "MockBus: unexpected call (no queued result for request type)";
      td::TestContext::get()->register_test_failure();
      co_return td::Status::Error("MockBus: unexpected call");
    }
    auto result = std::move(results.front());
    results.pop_front();
    if (result.respond_at) {
      co_await coro_sleep(*result.respond_at);
    }
    co_return std::move(result.value);
  }
};

// =============================================================================
// MockActor<B, EventList, RequestList>
//
// Auto-generates `handle()` and `process()` overloads delegating into the
// MockBus. A test only needs to declare which event types to capture; the
// handler set is derived from the type lists.
// =============================================================================

template <typename B, typename EventList, typename RequestList>
class MockActor : public SpawnsWith<B>, public ConnectsTo<B> {
 private:
  using BH = BusHandle<B>;

 public:
  template <typename BB, typename EE>
  void handle(BusHandle<BB>, std::shared_ptr<const EE>) = delete;

  template <typename BB, typename EE>
  Task<typename EE::ReturnType> process(BusHandle<BB>, std::shared_ptr<const EE>) = delete;

  template <std::same_as<B> = B, In<EventList> E>
  void handle(BH bh, std::shared_ptr<const E> event) {
    bh->template log<E>(std::move(event));
  }

  template <std::same_as<B> = B, In<RequestList> R>
  Task<typename R::ReturnType> process(BH bh, std::shared_ptr<R> request) {
    return bh->template call<R>(std::move(request));
  }

 private:
  using Self = MockActor<B, EventList, RequestList>;

  template <typename>
  struct StaticAsserts;
  template <typename... Ts>
  struct StaticAsserts<std::tuple<Ts...>> {
    static constexpr bool can_handle()
      requires(detail::CanActorHandleEvent<Self, B, Ts> && ...)
    {
      return true;
    }
    static constexpr bool can_process()
      requires(detail::CanActorProcessEvent<Self, B, Ts> && ...)
    {
      return true;
    }
  };
  static_assert(StaticAsserts<EventList>::can_handle());
  static_assert(StaticAsserts<RequestList>::can_process());
};

// =============================================================================
// Convenience aliases for the common case where you give a single tuple of
// "things to mock" and let the helper partition it.
//
//     using FooBus = MockedBus<RealFooBus, EventA, RequestB, EventC>;
//     using FooMock = MockedActorFor<FooBus>;
//
// MockedActorFor<B> derives the per-actor partition automatically.
// =============================================================================

template <typename B, typename... Es>
using MockedBus = MockBus<B, std::tuple<Es...>, RequestsOf<std::tuple<Es...>>>;

// `B` must be a (possibly-derived) MockBus that exposes `Parent`, `Logs`, `Calls`.
template <typename B>
using MockedActorFor = MockActor<B, EventsOf<FilterPublishTargets<typename B::Parent, typename B::Logs>>,
                                 RequestsOf<FilterPublishTargets<typename B::Parent, typename B::Logs>>>;

// =============================================================================
// MockAsync<R, Args...> — a single-call mock for one specific RPC.
//
// Designed for tests that need to interleave assertions with an in-flight call:
//
//     auto pending = mock.expect();        // arms the next call
//     trigger_action_under_test();          // causes the actor to call mock(...)
//     auto pc = co_await std::move(pending);
//     EXPECT_EQ(pc.args, ...);
//     // ... do other work, observe state ...
//     pc.respond.set_value(make_response());
//
// Or, for the simpler case, just queue results:
//
//     mock.returns(make_response());
//     trigger_action_under_test();
// =============================================================================

template <typename R, typename... Args>
class MockAsync {
 public:
  using CallArgs = std::tuple<std::decay_t<Args>...>;

  struct PendingCall {
    CallArgs args;
    td::Promise<R> respond;
  };

  void returns(R result) {
    results_.push_back(std::move(result));
  }

  StartedTask<PendingCall> expect() {
    CHECK(!pending_expect_);
    auto [task, promise] = StartedTask<PendingCall>::make_bridge();
    pending_expect_ = std::move(promise);
    return std::move(task);
  }

  Task<R> call(Args... args) {
    ++call_count_;
    if (pending_expect_) {
      auto [result_task, result_promise] = StartedTask<R>::make_bridge();
      pending_expect_.set_value(PendingCall{CallArgs(std::move(args)...), std::move(result_promise)});
      co_return co_await std::move(result_task);
    }
    CHECK(!results_.empty());
    auto result = std::move(results_.front());
    results_.pop_front();
    co_return std::move(result);
  }

  int call_count() const {
    return call_count_;
  }

 private:
  std::deque<R> results_;
  td::Promise<PendingCall> pending_expect_;
  int call_count_ = 0;
};

// =============================================================================
// Event query helpers — usable on the variant vector exposed by MockBus::events_.
// =============================================================================

template <typename E, typename... Es>
size_t count_events(const std::vector<std::variant<std::shared_ptr<const Es>...>>& events) {
  size_t count = 0;
  for (const auto& e : events) {
    if (std::holds_alternative<std::shared_ptr<const E>>(e)) {
      ++count;
    }
  }
  return count;
}

template <typename E, typename... Es>
std::vector<std::shared_ptr<const E>> events_of(const std::vector<std::variant<std::shared_ptr<const Es>...>>& events) {
  std::vector<std::shared_ptr<const E>> result;
  for (const auto& e : events) {
    if (auto* p = std::get_if<std::shared_ptr<const E>>(&e)) {
      result.push_back(*p);
    }
  }
  return result;
}

}  // namespace td::actor
