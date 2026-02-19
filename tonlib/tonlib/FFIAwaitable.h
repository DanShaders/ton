#pragma once

#include "td/actor/coro_task.h"

#include "FFIEventLoop.h"

namespace tonlib {

template <typename T>
class FFIAwaitable {
  struct Data {
    FFIEventLoop &loop;
    std::atomic<uintptr_t> continuation{};
    td::Result<T> result;
  };

 public:
  static FFIAwaitable create_resolved(FFIEventLoop &loop, td::Result<T> value) {
    FFIAwaitable awaitable{loop};
    awaitable.data_->continuation = Continuation::resolved_tag;
    awaitable.data_->result = std::move(value);
    return awaitable;
  }

  static FFIAwaitable create_bridge(FFIEventLoop &loop, td::actor::Task<T> task) {
    FFIAwaitable awaitable{loop};
    auto callback = [data = awaitable.data_](td::Result<T> result) -> td::Unit {
      data->result = std::move(result);
      auto maybe_continuation = data->continuation.exchange(Continuation::resolved_tag);
      if (maybe_continuation != 0 && maybe_continuation != Continuation::resolved_tag) {
        data->loop.put({maybe_continuation});
      }
      return {};
    };
    loop.run_in_context([&] { awaitable.task_ = std::move(task).transform(callback).start_without_scope(); });
    return awaitable;
  }

  FFIAwaitable(const FFIAwaitable &) = delete;
  FFIAwaitable(FFIAwaitable &&) = default;
  FFIAwaitable &operator=(const FFIAwaitable &) = delete;
  FFIAwaitable &operator=(FFIAwaitable &&) = default;

  ~FFIAwaitable() {
    if (data_) {
      auto maybe_continuation = data_->continuation.exchange(Continuation::resolved_tag);
      if (maybe_continuation != 0 && maybe_continuation != Continuation::resolved_tag) {
        data_->loop.put({maybe_continuation});
      }
      data_->loop.run_in_context([&] { task_.reset(); });
    }
  }

  bool await_ready() {
    return data_->continuation == Continuation::resolved_tag;
  }

  void await_suspend(Continuation continuation) {
    uintptr_t expected = 0;
    if (!data_->continuation.compare_exchange_strong(expected, continuation.value)) {
      CHECK(expected == Continuation::resolved_tag);
      data_->loop.put(continuation);
    }
  }

  td::Result<T> &result() & {
    CHECK(data_->continuation == Continuation::resolved_tag);
    return data_->result;
  }

 private:
  FFIAwaitable(FFIEventLoop &loop) : data_(std::make_shared<Data>(loop)) {
  }

  std::shared_ptr<Data> data_;
  td::actor::StartedTask<td::Unit> task_;
};

}  // namespace tonlib
