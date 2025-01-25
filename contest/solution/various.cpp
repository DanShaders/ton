#include <thread>

#include "various.hpp"


namespace solution {

size_t render_thread_id(std::thread::id th) {
  auto res = std::hash<std::thread::id>{}(th);
  return res;
}

}
