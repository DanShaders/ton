#pragma once

#include <thread>
#include <string>
#include <future>
#include <vector>

#include "shard.hpp" // Include something to be able to LOG

#include "settings.hpp"


namespace solution {

using namespace ton;

using std::async;
using std::future;
using std::vector;


size_t render_thread_id(std::thread::id th);


class DestructureLog {
 public:
  std::string message;

  DestructureLog(std::string message): message(message) { }
  ~DestructureLog() {
    LOG(ERROR) << message;
  }
};


}
