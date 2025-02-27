#include "fabric.h"
#include <future>
#include <vector>
#include <queue>
#include <memory>
#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <stdexcept>
#include <iostream>
#include <sstream>
#include <stop_token>
#include <tbb/concurrent_vector.h>

namespace solution {

using namespace ton;
using namespace ton::validator;

using td::Ref;
using namespace std::literals::string_literals;

namespace logger {
// ANSI escape codes for coloring text
#define RESET "\033[0m"
#define RED "\033[31m"
#define GREEN "\033[32m"
#define YELLOW "\033[33m"
#define GREY "\033[90m"
#define CYAN "\033[36m"
#define MAGENTA "\033[35m"
#define BLUE "\033[34m"

struct LogData {
  const std::string name;
  const std::chrono::high_resolution_clock::time_point creation_time;
  std::vector<std::pair<std::string, std::chrono::high_resolution_clock::time_point>> breakpoints{};

  LogData() : name(""), creation_time(std::chrono::high_resolution_clock::now()) {
  }

  LogData(const std::string& name) : name(name), creation_time(std::chrono::high_resolution_clock::now()) {
  }

  void add_breakpoint(const std::string& bp_name, std::chrono::high_resolution_clock::time_point time) {
    if (!breakpoints.empty() && breakpoints.back().second > time) {
      throw std::runtime_error("Breakpoint time must be after the last one.");
    }
    breakpoints.emplace_back(bp_name, time);
  }

  std::vector<std::pair<std::string, int64_t>> get_durations() const {
    std::vector<std::pair<std::string, int64_t>> durations;
    std::chrono::high_resolution_clock::time_point prev_time = creation_time;

    for (const auto& [breakpoint_name, timestamp] : breakpoints) {
      auto duration = std::chrono::duration_cast<std::chrono::microseconds>(timestamp - prev_time).count();
      durations.emplace_back(breakpoint_name, duration);
      prev_time = timestamp;
    }

    return durations;
  }
};

class LogManager {
 private:
  LogManager() {
  }
  LogManager(const LogManager&) = delete;
  LogManager& operator=(const LogManager&) = delete;
  tbb::concurrent_vector<LogData> logs_;

  static bool caseInsensitiveCompare(const std::string& a, const std::string& b) {
    auto itA = a.begin();
    auto itB = b.begin();

    while (itA != a.end() && itB != b.end()) {
      char chA = std::tolower(*itA);
      char chB = std::tolower(*itB);

      if (chA != chB)
        return chA < chB;

      ++itA;
      ++itB;
    }

    return a.size() < b.size();  // Handle case when one string is a prefix of another
  }

  static bool naturalCompare(const std::string& a, const std::string& b) {
    std::stringstream streamA(a), streamB(b);
    std::string segmentA, segmentB;

    while (std::getline(streamA, segmentA, ' ') && std::getline(streamB, segmentB, ' ')) {
      // Compare numeric segments first
      int numA = 0, numB = 0;

      if (std::all_of(segmentA.begin(), segmentA.end(), ::isdigit)) {
        numA = std::stoi(segmentA);
      }
      if (std::all_of(segmentB.begin(), segmentB.end(), ::isdigit)) {
        numB = std::stoi(segmentB);
      }

      if (numA != numB)
        return numA < numB;

      // If numeric segments are equal, fall back to lexicographical comparison
      if (segmentA != segmentB)
        return segmentA < segmentB;
    }

    return a < b;  // In case one is a prefix of the other
  }

 public:
  static void add_log(const LogData& log) {
    getInstance().logs_.emplace_back(log);
  }

  static void log_all() {
    std::cout << GREY << "=========== Duration Log ===========" << RESET << "\n";
    auto logs = getInstance().logs_;
    std::map<std::string, std::map<std::string, int64_t>> aggregated_logs;
    for (const auto& log : logs) {
      for (const auto& [bp_name, duration] : log.get_durations()) {
        aggregated_logs[log.name][bp_name] += duration;
      }
    }

    std::vector<std::pair<std::string, std::map<std::string, int64_t>>> sorted_logs(aggregated_logs.begin(),
                                                                                    aggregated_logs.end());
    std::sort(sorted_logs.begin(), sorted_logs.end(),
              [](const auto& a, const auto& b) { return naturalCompare(a.first, b.first); });

    // Print the sorted results
    for (const auto& [log_name, breakpoints] : sorted_logs) {
      int total_calls_count =
          std::count_if(logs.begin(), logs.end(), [log_name](const auto& item) { return item.name == log_name; });
      int64_t total_log_duration = std::accumulate(breakpoints.begin(), breakpoints.end(), int64_t(0),
                                                   [](int64_t sum, const auto& entry) { return sum + entry.second; });

      std::cout << GREY << "" << RESET << CYAN << log_name << GREY << " | Calls: " << RESET << GREEN
                << total_calls_count << GREY << " | Total: " << RESET << YELLOW << (total_log_duration / 1000)
                << "ms\n";

      std::vector<std::pair<std::string, int64_t>> sorted_breakpoints(breakpoints.begin(), breakpoints.end());
      std::sort(sorted_breakpoints.begin(), sorted_breakpoints.end(),
                [](const auto& a, const auto& b) { return naturalCompare(a.first, b.first); });

      for (const auto& [bp_name, total_duration] : sorted_breakpoints) {
        double percentage =
            (total_duration > 0) ? (static_cast<double>(total_duration) / total_log_duration) * 100 : 0.0;
        std::cout << GREY << "   [" << RESET << CYAN << bp_name << GREY << "] - Total: " << RESET << MAGENTA
                  << (total_duration / 1000) << "ms " << GREY << "(" << static_cast<int>(percentage) << "%)"
                  << "\n";
      }
    }

    std::cout << RESET;
  }

  static LogManager& getInstance() {
    static LogManager instance;
    return instance;
  }
};

class Logger {
 public:
  explicit Logger(const std::string& name) : data_{name}, last_log_time_{data_.creation_time}, is_logged_{false} {
  }

  ~Logger() {
    if (!is_logged_)
      std::cout << RED << "Logger: destroyed without call: " << data_.name << RESET << "\n";
    else
      LogManager::add_log(data_);
  }

  void add_breakpoint(const std::string& bpName) {
    is_logged_ = true;
    auto now = std::chrono::high_resolution_clock::now();
    data_.add_breakpoint(bpName, now);
    last_log_time_ = now;
  }

 private:
  bool is_logged_;
  LogData data_;
  std::chrono::high_resolution_clock::time_point last_log_time_;
};

}  // namespace logger

class ThreadPool {
 public:
  explicit ThreadPool(size_t num_threads)
      : num_threads_(num_threads), active_tasks_(0), is_terminated_(true), is_shutdown_(true) {
    workers_.reserve(num_threads_);
  }

  ~ThreadPool() {
    shutdown();
  }

  void reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!is_shutdown_ || !is_terminated_ || tasks_.size() != 0 || active_tasks_ != 0 || workers_.size() != 0) {
      std::cerr << "ThreadPool cannot reset due to the following state:\n"
                << "is_terminated_: " << std::boolalpha << is_terminated_ << "\n"
                << "tasks_.size(): " << tasks_.size() << "\n"
                << "active_tasks_: " << active_tasks_ << "\n"
                << "workers_.size(): " << workers_.size() << "\n";
      throw std::runtime_error("ThreadPool: Cannot reset.");
    }
    task_id_counter_ = 0;
    is_shutdown_ = false;
    is_terminated_ = false;
    global_task_.reset();
    executed_threads_index_.clear();
  }

  int enqueue_task(std::function<void()> task) {
    if (is_terminated_) {
      LOG(WARNING) << "ThreadPool::enqueue_task(): Task rejected: Pool terminated.";
      return -1;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    int task_id = task_id_counter_++;
    tasks_.emplace(TaskData(task_id, std::move(task)));

    LOG(INFO) << "ThreadPool::enqueue_task(): Task enqueued. Total tasks: " << tasks_.size();
    cv_.notify_one();

    create_worker_thread();

    return task_id;
  }

  void set_task_done_callback(std::function<void(int)> callback) {
    task_done_callback_ = std::move(callback);
  }

  void execute_on_all_threads(std::function<void()> task) {
    LOG(INFO) << "ThreadPool::execute_on_all_threads()";
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (global_task_) {
        LOG(WARNING) << "ThreadPool::execute_on_all_threads() A global task is already set.";
        return;
      }
      global_task_ = std::move(task);
    }
    cv_.notify_all();
  }

  void wait_for_completion() {
    LOG(INFO) << "ThreadPool: Waiting for task completion...";

    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait(lock, [this] { return (active_tasks_ == 0 && tasks_.empty()) || is_terminated_; });

    LOG(INFO) << "ThreadPool: All tasks completed or thread pool terminated.";
  }

  bool trigger_shutdown() {
    if (is_terminated_) {
      LOG(WARNING) << "ThreadPool::trigger_shutdown() Shutdown already in progress or completed.";
      return false;
    }

    LOG(INFO) << "ThreadPool::trigger_shutdown() Triggering shutdown.";
    {
      std::lock_guard<std::mutex> lock(mutex_);
      is_terminated_ = true;
      tasks_ = {};
      task_done_callback_ = nullptr;
      LOG(INFO) << "ThreadPool::trigger_shutdown() Tasks cleared, and termination flag set.";
    }

    cv_.notify_all();
    LOG(INFO) << "ThreadPool::trigger_shutdown() All threads notified for shutdown.";
    return true;
  }

  void shutdown() {
    if (is_shutdown_) {
      LOG(WARNING) << "ThreadPool::shutdown() Cannot shutdown.";
      return;
    }

    is_shutdown_ = true;

    LOG(INFO) << "ThreadPool::shutdown(): - Starting shutdown process.";

    trigger_shutdown();

    LOG(INFO) << "ThreadPool::shutdown(): Joining workers and clearing tasks.";
    for (auto& worker : workers_) {
      if (worker.get_stop_token().stop_possible()) {
        worker.request_stop();
      }
      if (worker.joinable()) {
        worker.join();
      }
    }
    workers_.clear();

    LOG(INFO) << "ThreadPool::shutdown(): Shutdown complete.";
  }

  bool isWorkerThread() const {
    const auto current_thread_id = get_thread_id();
    return std::any_of(workers_.begin(), workers_.end(), [&current_thread_id](const std::jthread& worker) {
      return worker.get_id() == current_thread_id;
    });
  }

  bool isTerminated() const {
    return is_terminated_;
  }

  bool isIdle() const {
    return active_tasks_ == 0 && tasks_.empty();
  }

  static std::thread::id get_thread_id() {
    return std::this_thread::get_id();
  }

  static std::string get_thread_id_str() {
    return thread_id_to_string(get_thread_id());
  }

  static std::string thread_id_to_string(std::thread::id id) {
    std::stringstream ss;
    ss << id;
    return ss.str();
  }

  static unsigned int get_optimal_thread_count() {
    return std::min(std::thread::hardware_concurrency(), 8u);
  }

 private:
  void create_worker_thread() {
    if (workers_.size() < num_threads_) {
      if (global_task_.has_value())
        throw std::runtime_error("ThreadPool: Cannot create worker, when global task exists.");

      workers_.emplace_back([this, index = workers_.size()](std::stop_token stop_token) {
        LOG(DEBUG) << "ThreadPool: Worker created.";
        while (!stop_token.stop_requested()) {
          TaskData task_data;

          {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this] { return is_terminated_ || global_task_.has_value() || !tasks_.empty(); });
            if (is_terminated_)
              break;

            if (global_task_.has_value()) {
              // Execute the global task once per thread.
              if (!executed_threads_index_.contains(index)) {
                (*global_task_)();
                executed_threads_index_.emplace(index);
              }

              // If all threads executed the global task, reset the global task.
              if (executed_threads_index_.size() == workers_.size()) {
                global_task_.reset();
                executed_threads_index_.clear();
              }

              continue;
            }

            if (!tasks_.empty()) {
              task_data = std::move(tasks_.front());
              tasks_.pop();
            }
          }

          if (task_data.callback) {
            ++active_tasks_;
            try {
              task_data.callback();
              if (task_done_callback_) {
                task_done_callback_(task_data.id);
              }
            } catch (const std::exception& e) {
              std::cerr << "Exception caught: " << e.what() << std::endl;
            } catch (...) {
              std::cerr << "Unknown exception caught" << std::endl;
            }
            --active_tasks_;
            cv_.notify_all();  // Notify waiting threads
          }
        }
        LOG(DEBUG) << "ThreadPool: Worker closed.";
      });
    }
  }

  struct TaskData {
    int id;
    std::function<void()> callback;

    TaskData() : id(-1), callback(nullptr) {
    }

    TaskData(int id, std::function<void()> callback) : id(id), callback(std::move(callback)) {
    }

    TaskData(const TaskData&) = delete;
    TaskData& operator=(const TaskData&) = delete;

    TaskData(TaskData&& other) noexcept : id(other.id), callback(std::move(other.callback)) {
    }

    TaskData& operator=(TaskData&& other) noexcept {
      if (this != &other) {
        id = other.id;
        callback = std::move(other.callback);
      }
      return *this;
    }
  };

  const size_t num_threads_;
  std::atomic<size_t> active_tasks_;
  std::atomic<bool> is_terminated_;
  std::atomic<bool> is_shutdown_;
  std::vector<std::jthread> workers_;
  std::atomic<int> task_id_counter_{0};
  std::queue<TaskData> tasks_;
  std::function<void(int)> task_done_callback_;
  std::optional<std::function<void()>> global_task_;
  std::unordered_set<int> executed_threads_index_;
  std::mutex mutex_;
  std::condition_variable cv_;
};

inline td::ConstBitPtr clone_const_bitptr(td::ConstBitPtr original, std::size_t bit_count) {
  unsigned char* new_memory = static_cast<unsigned char*>(std::malloc((bit_count + 7) / 8));

  td::BitPtr new_bitptr(new_memory);
  td::bitstring::bits_memcpy(new_bitptr, original, bit_count);

  return td::ConstBitPtr(new_memory);
};

}  // namespace solution
