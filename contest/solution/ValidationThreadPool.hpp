//NOTE: see README.md file

//#pragma once
//
//#include "contest-validate-query.hpp"
//#include <thread>
//#include <atomic>
//#include <map>
//#include <mutex>
//#include <future>
//#include <functional>
//#include <condition_variable>
//#include <vector>
//#include <string>
//#include <queue>
//
//class ValidationThreadPool {
//public:
//  static ValidationThreadPool& getInstance(size_t thread_count = std::thread::hardware_concurrency()) {
//    static ValidationThreadPool instance(thread_count);
//    return instance;
//  }
//  
//  ValidationThreadPool(const ValidationThreadPool&) = delete;
//  ValidationThreadPool& operator=(const ValidationThreadPool&) = delete;
//  
//  template<typename F>
//  void submitTask(F&& f) {
//    
//    auto task = std::make_shared<std::packaged_task<solution::AccountProcessingResult()>>
//    ([f = std::forward<F>(f), this]() -> solution::AccountProcessingResult {
//      if (cancel_flag_.load()) {
//        solution::AccountProcessingResult failed_result = solution::AccountProcessingResult();
//        return failed_result;
//      }
//      return f();
//    }
//     );
//    
//    task_results_.push_back(task->get_future());
//    
//    {
//      std::lock_guard<std::mutex> lock(queue_mutex_);
//      if (stop_flag_) {
//        throw std::runtime_error("Submitting task to stopped ValidationThreadPool");
//      }
//      
//      tasks_.emplace([task]() {
//        (*task)();
//      });
//    }
//    
//    condition_.notify_one();
//  }
//  
//  // Wait for all submitted tasks and check results
//  std::vector<solution::AccountProcessingResult> waitForResults() {
//    
//    std::vector<solution::AccountProcessingResult> total_results;
//    std::vector<std::future<solution::AccountProcessingResult>> results;
//    
//    results = std::move(task_results_);
//    task_results_.clear();
//    
//    for (auto& future : results) {
//      solution::AccountProcessingResult task_result = future.get();
//      if (!task_result.success) {
//        // If a task failed, set cancellation flag
//        cancel_flag_.store(true);
//        reset();
//        return {};
//      } else {
//        total_results.emplace_back(std::move(task_result));
//      }
//    }
//    
//    reset();
//    return total_results;
//  }
//  
//  void reset() {
//    cancel_flag_.store(false);
//    task_results_.clear();
//  }
//  
//  void shutdown() {
//    {
//      std::lock_guard<std::mutex> lock(queue_mutex_);
//      stop_flag_ = true;
//    }
//    
//    condition_.notify_all();
//    
//    for (auto& worker : workers_) {
//      if (worker.joinable()) {
//        worker.join();
//      }
//    }
//    
//    task_results_.clear();
//  }
//  
//  ~ValidationThreadPool() {
//    shutdown();
//  }
//  
//private:
//  std::vector<std::thread> workers_;
//  std::queue<std::function<void()>> tasks_;
//  mutable std::mutex queue_mutex_;
//  std::condition_variable condition_;
//  bool stop_flag_;
//  
//  std::atomic<bool> cancel_flag_{false};
//  std::vector<std::future<solution::AccountProcessingResult>> task_results_;
//  
//  explicit ValidationThreadPool(size_t thread_count): stop_flag_(false) {
//    for (size_t i = 0; i < thread_count; ++i) {
//      workers_.emplace_back([this] {
//        workerFunction();
//      });
//    }
//  }
//  
//  void workerFunction() {
//    while (true) {
//      std::function<void()> task;
//      
//      {
//        std::unique_lock<std::mutex> lock(queue_mutex_);
//        
//        condition_.wait(lock, [this] {
//          return stop_flag_ || !tasks_.empty();
//        });
//        
//        if (stop_flag_ && tasks_.empty()) {
//          return;
//        }
//        
//        task = std::move(tasks_.front());
//        tasks_.pop();
//      }
//      
//      task();
//    }
//    
//  }
//  
//};
