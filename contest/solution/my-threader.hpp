#pragma once

#include <vector>
#include <string>
#include <functional>
#include <future>
#include <chrono>
#include <fstream>
#include "BS_thread_pool.hpp"

#include "various.hpp"
#include "settings.hpp"



namespace solution {


using std::async;
using std::future;
using std::vector;
using std::string;
using std::atomic;
using std::ofstream;
// using std::ios;
using std::endl;
using std::chrono::high_resolution_clock;
using std::chrono::microseconds;
using std::chrono::duration_cast;
using std::chrono::_V2::system_clock;


// Function type that can handle both regular functions and lambdas with no parameters and void return
using LaunchFunction = std::function<void()>;


class MyThreader {
 public:
	MyThreader(BS::thread_pool<>& pool);

  // template<typename Callable>
  void launch(LaunchFunction callable);
  void launchAndProfile(string name, LaunchFunction callable);
  void waitForAll();
  void writeProfileToFile(const string& filename, int testIndex);

  // bool hasError();
  // std::string getFirstError();

  const int MaxFuturesHeld = 200;

  BS::thread_pool<>& pool;
 private:

  atomic<int> futuresCount{0};
  vector<future<void> > futures;
  vector<std::chrono::_V2::system_clock::time_point> startTimes;
  vector<std::chrono::_V2::system_clock::time_point> endTimes;
  vector<string> names;
  // DestructureLog logBeforeFuturesDestroyed { "Before futures destroyed" };
};


}
