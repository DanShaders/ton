#include "various.hpp"
#include "multithreading-guard.hpp"

namespace solution {

MultithreadingGuard::MultithreadingGuard(ContestValidateQuery& cvq, std::string name): cvq(cvq), name(name) {
  guard();
}
MultithreadingGuard::MultithreadingGuard(ContestValidateQuery* cvq, std::string name): cvq(*cvq), name(name) {
  guard();
}
MultithreadingGuard::~MultithreadingGuard() {
  release();
}

void MultithreadingGuard::guard() {
  LOG(ERROR) << name << ": MultithreadingGuard::guard obtained at thread " << render_thread_id(std::this_thread::get_id());
  cvq.enter_multithreading();
}
void MultithreadingGuard::release() {
  LOG(ERROR) << name << ": MultithreadingGuard::release performed at thread " << render_thread_id(std::this_thread::get_id());
  cvq.leave_multithreading();
}

}