#pragma once

#include <string>
#include <functional>
#include <future>


namespace solution {


// Function type that can handle both regular functions and lambdas with no parameters and void return
using LaunchFunction = std::function<void()>;


class MyThreader {
 public:
	MyThreader() { }

  // template<typename Callable>
  void launch(LaunchFunction callable);

  // bool hasError();
  // std::string getFirstError();

 private:

};


}
