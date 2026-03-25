#pragma once

#include "auto/tl/lite_api.h"
#include "keys/keys.hpp"
#include "td/utils/port/IPAddress.h"

#include "FFIEventLoop.h"
#include "LiteClient.h"

namespace tonlib {

class FFILiteClient {
 public:
  FFILiteClient(FFIEventLoop& loop, td::IPAddress address, ton::PublicKey server_public_key);

  FFILiteClient(FFILiteClient&&) = default;

  ~FFILiteClient() {
    if (!client_.empty()) {
      loop_.run_in_context([client = std::move(client_)]() mutable { client.reset(); });
    }
  }

  void request(ton::tl_object_ptr<ton::lite_api::Function> query,
               td::Promise<ton::tl_object_ptr<ton::lite_api::Object>> promise);

  FFIEventLoop& loop() {
    return loop_;
  }

 private:
  FFIEventLoop& loop_;
  td::unique_ptr<td::Guard> counter_;
  td::actor::ActorOwn<LiteClient> client_;
};

}  // namespace tonlib
