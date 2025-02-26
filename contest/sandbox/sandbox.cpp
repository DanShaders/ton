#include "td/actor/actor.h"
#include "td/actor/ActorId.h"
#include "td/actor/core/Scheduler.h"
#include "td/utils/OptionParser.h"
#include "td/utils/Time.h"
#include "td/utils/logging.h"
#include "td/utils/port/signals.h"
#include "td/utils/format.h"
#include "td/actor/PromiseFuture.h"
#include "td/actor/MultiPromise.h"
#include "td/utils/Status.h"

#include <vector>
#include <memory>
#include <iostream>

class ValidationActor : public td::actor::Actor {
 public:
  ValidationActor(size_t worker_id, size_t start_idx, size_t end_idx, const std::vector<int>& data, td::Promise<std::vector<int>> promise) 
      : worker_id_(worker_id)
      , start_idx_(start_idx)
      , end_idx_(end_idx)
      , data_ref_(data)  // Store reference to original data
      , promise_(std::move(promise)) {}

  ~ValidationActor() override = default;

  void start_up() override {
    LOG(INFO) << "Starting worker " << worker_id_ << " for range [" << start_idx_ << ", " << end_idx_ << ")";
    
    // Validate numbers in the chunk
    std::vector<int> invalid_numbers;
    for (int j = 0; j < 100; ++j) {
        for (size_t i = start_idx_; i < end_idx_; i++) {
            if (data_ref_[i] < 0) {  // Example validation: negative numbers are invalid
                invalid_numbers.push_back(data_ref_[i]);
            }
        }
    }
    LOG(INFO) << "Worker " << worker_id_ << " validated chunk, found " << invalid_numbers.size() << " invalid numbers";
    promise_.set_value(std::move(invalid_numbers));
    stop();
  }

 private:
  size_t worker_id_;
  size_t start_idx_;
  size_t end_idx_;
  const std::vector<int>& data_ref_;  // Reference to the original data
  td::Promise<std::vector<int>> promise_;
};

class MasterActor : public td::actor::Actor {
 public:
  static constexpr size_t NUM_CHUNKS = 2;  // Number of parallel workers

  MasterActor(std::vector<int> data, td::Promise<std::vector<int>> promise)
      : data_(std::move(data)), promise_(std::move(promise)) {
    std::cout << "Created master actor" << std::endl;
  }

  void start_up() override {
    std::cout << "Starting master actor" << std::endl;
    
    // Calculate chunk sizes
    size_t chunk_size = data_.size() / NUM_CHUNKS;
    size_t remainder = data_.size() % NUM_CHUNKS;
    pending_workers_ = NUM_CHUNKS;
    
    std::cout << "Creating " << NUM_CHUNKS << " worker actors" << std::endl;
    // Create worker actors
    for (size_t i = 0; i < NUM_CHUNKS; i++) {
      size_t start_idx = i * chunk_size;
      size_t end_idx = (i + 1) * chunk_size;
      if (i == NUM_CHUNKS - 1) {
        end_idx += remainder;  // Add remainder to last chunk
      }
      
      std::cout << "Creating worker " << i << " for range [" << start_idx << ", " << end_idx << ")" << std::endl;
      td::actor::create_actor<ValidationActor>(
          td::actor::ActorOptions().with_name(PSTRING() << "Worker" << i),
          i,
          start_idx,
          end_idx,
          data_,  // Pass reference to the whole vector
          td::Promise<std::vector<int>>{[this](td::Result<std::vector<int>> R) {
            if (R.is_error()) {
              on_worker_error(R.move_as_error());
            } else {
              on_worker_result(R.move_as_ok());
            }
          }}
      ).release();
    }
  }

 private:
  void on_worker_error(td::Status error) {
    if (!promise_) {
      return;  // Already finished with error
    }
    promise_.set_error(std::move(error));
    promise_ = {};
    stop();
  }

  void on_worker_result(std::vector<int> invalid_indices) {
    std::cout << "Master received chunk results with " << invalid_indices.size() << " invalid numbers" << std::endl;
    invalid_results_.insert(invalid_results_.end(), invalid_indices.begin(), invalid_indices.end());
    pending_workers_--;
    
    std::cout << "Master has " << pending_workers_ << " pending workers" << std::endl;
    
    if (pending_workers_ == 0) {
      std::cout << "All workers finished. Found " << invalid_results_.size() << " invalid numbers at indices:" << std::endl;
      for (int idx : invalid_results_) {
        std::cout << idx << std::endl;
      }
      promise_.set_value(std::move(invalid_results_));
      promise_ = {};
      stop();
    }
  }
  
  std::vector<int> data_;
  std::vector<int> invalid_results_;
  size_t pending_workers_{0};
  td::Promise<std::vector<int>> promise_;
};

int main(int argc, char* argv[]) {
  SET_VERBOSITY_LEVEL(verbosity_INFO);
  td::set_default_failure_signal_handler().ensure();

  td::OptionParser p;
  p.set_description("Parallel validation example");
  
  std::string threads_str = "2";
  p.add_checked_option('t', "threads", "number of threads (default: 2)", [&](td::Slice arg) {
    threads_str = arg.str();
    return td::Status::OK();
  });

  auto status = p.run(argc, argv);
  if (status.is_error()) {
    std::cerr << "Failed to parse options" << std::endl;
    return 1;
  }

  std::cout << "Starting sandbox with " << threads_str << " threads" << std::endl;

  // Example data to validate
  std::vector<int> data;
  for (int i = 1; i <= 100000000; i++) {
    data.push_back(i);
  }
  std::cout << "Created test data with " << data.size() << " elements" << std::endl;
  
  // Create scheduler with 2 threads
  auto scheduler = td::actor::core::Scheduler(
      std::make_shared<td::actor::core::SchedulerGroupInfo>(1),
      td::actor::core::SchedulerId{0},
      5  // Number of CPU threads
  );
  
  std::cout << "Starting scheduler" << std::endl;
  scheduler.start();

  bool is_finished = false;
  std::vector<int> result;
  
  // Start validation in scheduler context
  scheduler.run_in_context([&] {
    std::cout << "Creating master actor" << std::endl;
    // Create master actor with promise
    auto promise = td::Promise<std::vector<int>>{[&is_finished, &result](td::Result<std::vector<int>> R) {
      if (R.is_error()) {
        LOG(ERROR) << "Validation failed: " << R.error();
      } else {
        result = R.move_as_ok();
        std::cout << "Got validation result with " << result.size() << " invalid numbers" << std::endl;
      }
      is_finished = true;
    }};

    td::actor::create_actor<MasterActor>(
        "Master",
        std::move(data),
        std::move(promise)
    ).release();
  });
  
  std::cout << "Starting main loop with 10 second timeout" << std::endl;
  double start_time = td::Time::now();
  
  // Run until we get the result or timeout
  while (!is_finished) {
    if (!scheduler.run(0)) {  // Run without delay to process messages faster
      break;
    }
    if (td::Time::now() - start_time > 10.0) {
      LOG(ERROR) << "Timeout after 10 seconds";
      return 1;
    }
  }

  std::cout << "Finished in " << (td::Time::now() - start_time) << " seconds" << std::endl;
  if (!result.empty()) {
    std::cout << "Found invalid numbers:" << std::endl;
    for (int num : result) {
      std::cout << num << " ";
    }
    std::cout << std::endl;
  }

  return 0;
} 