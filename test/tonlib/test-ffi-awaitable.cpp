#include <chrono>
#include <latch>
#include <thread>

#include "td/utils/Status.h"
#include "td/utils/tests.h"
#include "tonlib/FFIAwaitable.h"

namespace tonlib {
namespace {

struct Tag {};

static constexpr Tag tags[2];
static constexpr const void* continuation_0 = &tags[0];
static constexpr const void* continuation_1 = &tags[1];

template <typename T>
struct ControllableBridge {
  FFIAwaitable<T> awaitable;
  td::Promise<T> promise;
};

template <typename T>
ControllableBridge<T> make_bridge(FFIEventLoop& loop) {
  auto [started_task, promise] = td::actor::StartedTask<T>::make_bridge();
  auto task = [](td::actor::StartedTask<T> task) -> td::actor::Task<T> {
    co_return co_await std::move(task);
  }(std::move(started_task));
  return {FFIAwaitable<T>::create_bridge(loop, std::move(task)), std::move(promise)};
}

// === create_resolved tests ===

TEST(FFIAwaitable, CreateResolvedWithValue) {
  FFIEventLoop loop(1);
  auto aw = FFIAwaitable<int>::create_resolved(loop, 42);

  EXPECT(aw.await_ready());
  EXPECT(aw.result().is_ok());
  EXPECT_EQ(aw.result().ok(), 42);
}

TEST(FFIAwaitable, CreateResolvedWithError) {
  FFIEventLoop loop(1);
  auto aw = FFIAwaitable<int>::create_resolved(loop, td::Status::Error(123, "test error"));

  EXPECT(aw.await_ready());
  EXPECT(aw.result().is_error());
  EXPECT_EQ(aw.result().error().code(), 123);
}

TEST(FFIAwaitable, AwaitSuspendOnResolved) {
  FFIEventLoop loop(1);
  auto aw = FFIAwaitable<int>::create_resolved(loop, 42);

  aw.await_suspend({continuation_0});

  auto result = loop.wait(0);
  EXPECT(result.has_value());
  EXPECT_EQ(result->ptr(), continuation_0);
}

// === create_bridge: normal resolve ===

TEST(FFIAwaitable, BridgeResolveAfterSuspend) {
  FFIEventLoop loop(1);
  auto [aw, promise] = make_bridge<int>(loop);
  EXPECT(!aw.await_ready());

  aw.await_suspend({continuation_0});
  promise.set_value(42);

  auto result = loop.wait(-1);
  EXPECT(result.has_value());
  EXPECT_EQ(result->ptr(), continuation_0);

  EXPECT(aw.await_ready());
  EXPECT(aw.result().is_ok());
  EXPECT_EQ(aw.result().ok(), 42);
}

TEST(FFIAwaitable, BridgeResolveWithError) {
  FFIEventLoop loop(1);
  auto [aw, promise] = make_bridge<int>(loop);
  aw.await_suspend({continuation_0});

  promise.set_error(td::Status::Error(456, "some error"));

  auto result = loop.wait(-1);
  EXPECT(result.has_value());
  EXPECT_EQ(result->ptr(), continuation_0);

  EXPECT(aw.await_ready());
  EXPECT(aw.result().is_error());
  EXPECT_EQ(aw.result().error().code(), 456);
}

TEST(FFIAwaitable, BridgeResolveBeforeSuspend) {
  FFIEventLoop loop(1);
  auto [aw, promise] = make_bridge<int>(loop);
  EXPECT(!aw.await_ready());

  promise.set_value(42);

  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  // Let's hope that at this point scheduler resolved the awaitable.

  aw.await_suspend({continuation_0});

  auto result = loop.wait(-1);
  EXPECT(result.has_value());
  EXPECT_EQ(result->ptr(), continuation_0);

  EXPECT(aw.await_ready());
  EXPECT(aw.result().is_ok());
  EXPECT_EQ(aw.result().ok(), 42);
}

// === destroy without resolve ===

TEST(FFIAwaitable, DestroyResolvedWithoutSuspend) {
  FFIEventLoop loop(1);
  auto aw = FFIAwaitable<int>::create_resolved(loop, 42);
  EXPECT(aw.await_ready());
}

TEST(FFIAwaitable, DestroyResolvedAfterSuspend) {
  FFIEventLoop loop(1);
  {
    auto aw = FFIAwaitable<int>::create_resolved(loop, 42);
    aw.await_suspend({continuation_1});
  }

  auto result = loop.wait(0);
  EXPECT(result.has_value());
  EXPECT_EQ(result->ptr(), continuation_1);
}

TEST(FFIAwaitable, DestroyUnresolvedWithoutSuspend) {
  FFIEventLoop loop(1);
  {
    auto [aw, promise] = make_bridge<int>(loop);
  }

  auto result = loop.wait(0);
  EXPECT(!result.has_value());
}

TEST(FFIAwaitable, DestroyUnresolvedAfterSuspend) {
  FFIEventLoop loop(1);
  {
    auto [aw, promise] = make_bridge<int>(loop);
    aw.await_suspend({continuation_0});
  }

  auto result = loop.wait(0);
  EXPECT(result.has_value());
  EXPECT_EQ(result->ptr(), continuation_0);
}

// === Cancellation ===

TEST(FFIAwaitable, CancelPropagation) {
  FFIEventLoop loop(1);

  std::latch latch{1};
  auto [started_task, promise] = td::actor::StartedTask<int>::make_bridge();

  auto task = [](std::latch& latch, td::actor::StartedTask<int> task) -> td::actor::Task<int> {
    auto cancel_cb = [&latch](td::Result<td::Unit> r) {
      if (r.is_ok()) {
        latch.count_down();
      }
    };
    td::actor::current_scope_lease().publish_cancel_promise(td::PromiseCreator::lambda(cancel_cb));
    co_return co_await std::move(task);
  }(latch, std::move(started_task));

  {
    auto aw = FFIAwaitable<int>::create_bridge(loop, std::move(task));

    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    EXPECT(!latch.try_wait());
  }

  latch.wait();
}

// === Concurrency ===

TEST(FFIAwaitable, ConcurrentResolveAndDestroy) {
  FFIEventLoop loop(1);
  for (size_t i = 0; i < 10; ++i) {
    auto bridge = std::make_unique<ControllableBridge<int>>(make_bridge<int>(loop));
    bridge->awaitable.await_suspend({continuation_0});

    std::thread resolver([promise = std::move(bridge->promise)]() mutable {
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
      promise.set_value(999);
    });

    std::thread destroyer([&]() {
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
      bridge.reset();
    });

    auto result = *loop.wait(-1);

    resolver.join();
    destroyer.join();

    EXPECT_EQ(result.ptr(), continuation_0);
  }
}

}  // namespace
}  // namespace tonlib
