#include "adnl/adnl-ext-client.h"
#include "auto/tl/lite_api.hpp"
#include "tl-utils/lite-utils.hpp"

#include "LiteClient.h"

namespace tonlib {

class LiteClientCallback : public ton::adnl::AdnlExtClient::Callback {
 public:
  LiteClientCallback(td::actor::ActorId<LiteClient> id) : id_(std::move(id)) {
  }

  void on_ready() override {
    td::actor::send_closure(id_, &LiteClient::on_ready);
  }

  void on_stop_ready() override {
    td::actor::send_closure(id_, &LiteClient::on_stop_ready);
  }

 private:
  td::actor::ActorId<LiteClient> id_;
};

LiteClient::LiteClient(td::IPAddress address, ton::PublicKey server_public_key)
    : address_(address), server_public_key_(std::move(server_public_key)) {
}

void LiteClient::on_ready() {
  ready_ = true;
  for (auto& promise : pending_ready_promises_) {
    promise.set_value(td::Unit());
  }
  pending_ready_promises_.clear();
}

void LiteClient::on_stop_ready() {
  for (auto& promise : pending_ready_promises_) {
    promise.set_error(td::Status::Error("Connection closed"));
  }
  pending_ready_promises_.clear();
  ready_ = false;
  client_ = {};
}

td::actor::Task<ton::tl_object_ptr<ton::lite_api::Object>> LiteClient::query(
    ton::tl_object_ptr<ton::lite_api::Function> object) {
  if (!ready_) {
    if (client_.empty()) {
      client_ = ton::adnl::AdnlExtClient::create(ton::adnl::AdnlNodeIdFull{server_public_key_}, address_,
                                                 std::make_unique<LiteClientCallback>(actor_id(this)));
    }

    auto [ready_awaiter, ready_promise] = td::actor::StartedTask<td::Unit>::make_bridge();
    pending_ready_promises_.push_back(std::move(ready_promise));
    co_await std::move(ready_awaiter);
  }

  auto query_bytes = ton::serialize_tl_object(object, true);
  auto wrapped_query =
      ton::serialize_tl_object(ton::create_tl_object<ton::lite_api::liteServer_query>(std::move(query_bytes)), true);

  auto [response_awaiter, response_promise] = td::actor::StartedTask<td::BufferSlice>::make_bridge();
  td::actor::send_closure(client_, &ton::adnl::AdnlExtClient::send_query, "query", std::move(wrapped_query),
                          td::Timestamp::in(10.0), std::move(response_promise));
  auto response = co_await std::move(response_awaiter);

  auto result_obj = co_await ton::fetch_tl_object<ton::lite_api::Object>(response, true);
  co_return std::move(result_obj);
}

}  // namespace tonlib
