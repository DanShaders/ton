#pragma once

#include <string>
#include <functional>
#include <future>
#include <vector>

#include "various.hpp"


namespace solution {


using std::async;
using std::future;
using std::vector;
using std::atomic;


// Function type that can handle both regular functions and lambdas with no parameters and void return
using LaunchFunction = std::function<void()>;


class MyThreader {
 public:
	MyThreader();

  // template<typename Callable>
  void launch(LaunchFunction callable);
  void waitForAll();

  // bool hasError();
  // std::string getFirstError();

  const int MaxFuturesHeld = 200;

 private:
  atomic<int> futuresCount{0};
  vector<future<void> > futures;
  // DestructureLog logBeforeFuturesDestroyed { "Before futures destroyed" };
};


}
