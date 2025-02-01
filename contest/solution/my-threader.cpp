#include "my-threader.hpp"


namespace solution {



MyThreader::MyThreader() {
  futures.resize(MaxFuturesHeld);
}


// template<typename Callable>
void MyThreader::launch(LaunchFunction callable) {
  // callable();

  int fi = futuresCount++;
  futures[fi] = async(
      std::launch::async,
      callable
  );

  // f.get();
}

void MyThreader::waitForAll() {
  try {
    for (int i = 0; i < futuresCount; i++) {
      futures[i].get();
    }
    // Even when futures get added, futuresCount will increase accordingly
    // and the loop will continue until it runs out of all the futures
  } catch (...) {
    // throw caught exception
    throw;
  }
}



};

