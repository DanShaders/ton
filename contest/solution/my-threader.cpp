#include "my-threader.hpp"


namespace solution {


// template<typename Callable>
void MyThreader::launch(LaunchFunction callable) {
  callable();
}



};

