#include "my-threader.hpp"


namespace solution {

using std::async;

// template<typename Callable>
void MyThreader::launch(LaunchFunction callable) {
  // callable();

  auto f = async(
      std::launch::async,
      callable
  );

  f.get();
}



};

