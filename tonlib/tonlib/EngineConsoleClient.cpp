#include "auto/tl/ton_api.hpp"
#include "td/actor/coro_utils.h"

#include "EngineConsoleClient.h"

namespace tonlib {

bool is_engine_console_query(const ton::tl_object_ptr<ton::ton_api::Function>& function) {
  switch (function->get_id()) {
    case ton::ton_api::engine_validator_getActorTextStats::ID:
    case ton::ton_api::engine_validator_waitForLiteServer::ID:
    case ton::ton_api::engine_validator_waitForInitialSync::ID:
      return true;
    default:
      return false;
  }
}

class EngineConsoleClientCallback : public ton::adnl::AdnlExtClient::Callback {
 public:
  EngineConsoleClientCallback(td::actor::ActorId<EngineConsoleClient> id) : id_(std::move(id)) {
  }

  void on_ready() override {
    td::actor::send_closure(id_, &EngineConsoleClient::on_ready);
  }

  void on_stop_ready() override {
  }

 private:
  td::actor::ActorId<EngineConsoleClient> id_;
};

EngineConsoleClient::EngineConsoleClient(td::IPAddress address, ton::PublicKey server_public_key,
                                         ton::PrivateKey client_private_key)
    : address_(address)
    , server_public_key_(std::move(server_public_key))
    , client_private_key_(std::move(client_private_key)) {
}

EngineConsoleClient::~EngineConsoleClient() = default;

void EngineConsoleClient::start_up() {
  client_ = ton::adnl::AdnlExtClient::create(ton::adnl::AdnlNodeIdFull{server_public_key_}, client_private_key_,
                                             address_, std::make_unique<EngineConsoleClientCallback>(actor_id(this)));

  std::tie(ready_future_, ready_promise_) = td::actor::StartedTask<td::Unit>::make_bridge();
}

void EngineConsoleClient::on_ready() {
  ready_promise_.set_value({});
}

td::actor::Task<ton::tl_object_ptr<ton::ton_api::Object>> EngineConsoleClient::query(
    ton::tl_object_ptr<ton::ton_api::Function> object) {
  co_await ready_future_.get();

  auto query_bytes = ton::serialize_tl_object(object, true);
  auto wrapped_query = ton::serialize_tl_object(
      ton::create_tl_object<ton::ton_api::engine_validator_controlQuery>(std::move(query_bytes)), true);

  auto response =
      co_await td::actor::ask(client_, &ton::adnl::AdnlExtClient::send_query_cancellable, std::move(wrapped_query));

  auto result_obj = co_await ton::fetch_tl_object<ton::ton_api::Object>(response, true);
  co_return std::move(result_obj);
}

}  // namespace tonlib
