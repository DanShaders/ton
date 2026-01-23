#pragma once

#include "FFIAwaitable.h"
#include "FFIEventLoop.h"
#include "PublicOverlayClient.h"

namespace tonlib {

class FFIPublicOverlayClient {
 public:
  FFIPublicOverlayClient(FFIEventLoop& loop, PublicOverlayClientConfig config);

  ~FFIPublicOverlayClient() {
    if (!client_.empty()) {
      loop_.run_in_context([&]() { client_.reset(); });
    }
  }

  FFIAwaitable<td::Unit>* init();

  FFIEventLoop& loop() {
    return loop_;
  }

 private:
  FFIEventLoop& loop_;
  td::unique_ptr<td::Guard> counter_;
  td::actor::ActorOwn<PublicOverlayClient> client_;
};

}  // namespace tonlib
