#include "FFILiteClient.h"

namespace tonlib {

FFILiteClient::FFILiteClient(FFIEventLoop& loop, td::IPAddress address, ton::PublicKey server_public_key)
    : loop_(loop), counter_(loop.new_actor()) {
  loop_.run_in_context([&] {
    client_ = td::actor::create_actor<LiteClient>("LiteClient", address, std::move(server_public_key));
  });
}

void FFILiteClient::request(ton::tl_object_ptr<ton::lite_api::Function> query,
                            td::Promise<ton::tl_object_ptr<ton::lite_api::Object>> promise) {
  loop_.run_in_context(
      [client = this->client_.get(), query = std::move(query), promise = std::move(promise)]() mutable {
        td::actor::send_closure(client, &LiteClient::query, std::move(query), std::move(promise));
      });
}

}  // namespace tonlib
