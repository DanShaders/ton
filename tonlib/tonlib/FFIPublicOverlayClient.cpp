#include "FFIPublicOverlayClient.h"

namespace tonlib {

FFIPublicOverlayClient::FFIPublicOverlayClient(FFIEventLoop& loop, PublicOverlayClientConfig config)
    : loop_(loop), counter_(loop.new_actor()) {
  loop.run_in_context(
      [&] { client_ = td::actor::create_actor<PublicOverlayClient>("PublicOverlayClient", std::move(config)); });
}

FFIAwaitable<td::Unit>* FFIPublicOverlayClient::init() {
  auto bridge = FFIAwaitable<td::Unit>::create_bridge<td::Unit>(loop_, std::identity{});
  loop_.run_in_context(
      [&] { td::actor::send_closure(client_, &PublicOverlayClient::init, std::move(bridge.promise)); });
  return bridge.awaitable;
}

}  // namespace tonlib
