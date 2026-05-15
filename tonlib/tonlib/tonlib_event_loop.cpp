#include "FFIAwaitable.h"
#include "FFIEventLoop.h"
#include "tonlib_event_loop.h"

// ===== Event loop =====
TonlibEventLoop *tonlib_event_loop_create(int threads) {
  return new tonlib::FFIEventLoop{threads};
}

void tonlib_event_loop_destroy(TonlibEventLoop *loop) {
  delete loop;
}

void tonlib_event_loop_cancel(TonlibEventLoop *loop) {
  loop->cancel();
}

const void *tonlib_event_loop_wait(TonlibEventLoop *loop, double timeout) {
  auto result = loop->wait(timeout);
  if (!result.has_value()) {
    return nullptr;
  }
  return result->ptr();
}

// ===== Response =====
void tonlib_response_destroy(TonlibResponse *response) {
  response->destroy();
}

bool tonlib_response_await_ready(TonlibResponse *response) {
  return response->await_ready();
}

void tonlib_response_await_suspend(TonlibResponse *response, const void *continuation) {
  response->await_suspend({continuation});
}

bool tonlib_response_is_error(TonlibResponse *response) {
  return response->result().is_error();
}

int tonlib_response_get_error_code(TonlibResponse *response) {
  return response->result().error().code();
}

const char *tonlib_response_get_error_message(TonlibResponse *response) {
  return response->result().error().message().data();
}

const char *tonlib_response_get_response(TonlibResponse *response) {
  return response->result().ok().data();
}
