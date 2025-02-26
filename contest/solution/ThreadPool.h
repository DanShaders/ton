
/*
 * MIT License
 *
 * Copyright (c) 2021 Josef Barnes
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <atomic>
#include <condition_variable>
#include <future>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace threadpool {

/** A class for creating a threadpool with a job queue.
 * Each job is callable object (eg. lambda, function pointer, function object).
 */
class ThreadPool {
 private:
  std::vector<std::jthread> m_threads;       /**< Pool of threads */
  std::queue<std::function<void()>> m_tasks; /**< Task queue */
  std::mutex m_mtx;                          /**< Mutex for locking the task queue */
  std::condition_variable_any m_cv;          /**< Condition variable for threads to wait for a task */
  std::atomic<size_t> m_nActive;             /**< Number of threads currently processing a task */

 public:
  /** Constructor for a ThreadPool. Creates a pool of threads, which will wait
    * for jobs to be added to the queue. The jobs will begin processing in the
    * order that they are added to the queue (ie. first in, first out).
    *
    * @param count            The number of thread to spawn.
    */
  ThreadPool(size_t count) {
    auto fn = [this](std::stop_token stok) {
      while (true) {
        std::unique_lock<std::mutex> lk(m_mtx);

        /* Wait for either a task to be added, or a stop to be requested */
        m_cv.wait(lk, stok, [this]() { return !m_tasks.empty(); });
        if (stok.stop_requested() && m_tasks.empty()) {
          return;
        }

        std::function<void()> fn = m_tasks.front();
        m_tasks.pop();
        lk.unlock();

        m_nActive++;
        fn();
        m_nActive--;
      }
    };

    for (size_t i = 0; i < count; ++i) {
      m_threads.emplace_back(std::jthread(fn));
    }
  }

  /** @name Deleted special member functions
    * A threadpool object is neither copyable or moveable, so the following
    * group of special member functions are deleted:
    *    - Copy Constructor
    *    - Copy Assigment Operator
    *    - Move Constructor
    *    - Move Assigment Operator
    */
  /**@{*/
  ThreadPool(const ThreadPool &) = delete;
  ThreadPool(ThreadPool &&) = delete;
  ThreadPool &operator=(const ThreadPool &) = delete;
  ThreadPool &operator=(ThreadPool &&) = delete;
  /**@}*/

  /** Destructor for a ThreadPool. It manually destroys the threads so that
    * they can finish any tasks in the queue before its destroyed.
    */
  ~ThreadPool() {
    m_threads.clear();
  }

  /** @returns the total number of threads in the pool.
    */
  size_t threadCount() const noexcept {
    return m_threads.size();
  }

  /** @returns the number of threads that are currently running a task.
    */
  size_t activeCount() const noexcept {
    return m_nActive;
  }

  /** @returns the number of tasks waiting in the queue.
    */
  size_t queuedCount() {
    std::scoped_lock lk(m_mtx);
    return m_tasks.size();
  }

  /** Reset the task queue to an empty state.
    */
  void clearQueue() {
    std::scoped_lock lk(m_mtx);
    m_tasks = std::queue<std::function<void()>>();
  }

  /** Add a task to the queue.
    *
    * A task is represented by a callable object, along with its arguments. A
    * call to addJob will place the task on the queue and return a future,
    * which can be use to either get the result of the task, or simply to check
    * whether the task has finished.
    *
    * @tparam F    Callable object (function, lambda etc.)
    * @tparam Args Arguments accepted by `F`
    * @param fn    The task to be run
    * @param args  The arguments to `fn`
    * @returns A future of the type returned by `fn(args...)`
    */
  template <typename F, typename... Args>
  std::future<std::invoke_result_t<F, Args...>> addJob(F &&fn, Args &&...args) {
    /* ret_type is the type reutrned by fn(arg...) */
    using ret_type = std::invoke_result_t<F, Args...>;
    std::future<ret_type> result;

    /* Limit the lifetime of the scoped lock */
    {
      std::scoped_lock lk(m_mtx);

      /*
          * We really want to directly capture the promise by the lambda below,
          * but promises cant be copied, so cannot be contained in a
          * std::function. We can get around this by wrapping the promise in a
          * shared pointer, which is copyable.
          */
      auto p = std::make_shared<std::promise<ret_type>>();
      result = p->get_future();

      m_tasks.emplace([p = std::move(p), fn = std::forward<F>(fn), ... args = std::forward<Args>(args)]() {
        /*
             * If the function returns void, we can't set a value in the
             * promise. But we can still use it to signal that the task was
             * complete.
             */
        if constexpr (std::is_same<ret_type, void>::value) {
          std::invoke(fn, args...);
          p->set_value();
        } else {
          p->set_value(std::invoke(fn, args...));
        }
      });
    }

    /* Wake up a single thread to run the task */
    m_cv.notify_one();

    return result;
  }
};

}  // namespace threadpool

#endif  // THREADPOOL_H
