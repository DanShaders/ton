#pragma once

#include "adnl/adnl-ext-client.h"
#include "auto/tl/lite_api.h"
#include "keys/keys.hpp"
#include "td/actor/actor.h"
#include "td/actor/coro_task.h"
#include "td/utils/port/IPAddress.h"

namespace tonlib {

class LiteClient : public td::actor::Actor {
 public:
  LiteClient(td::IPAddress address, ton::PublicKey server_public_key);

  void on_ready();
  void on_stop_ready();

  td::actor::Task<ton::tl_object_ptr<ton::lite_api::Object>> query(
      ton::tl_object_ptr<ton::lite_api::Function> function);

 private:
  td::IPAddress address_;
  ton::PublicKey server_public_key_;
  td::actor::ActorOwn<ton::adnl::AdnlExtClient> client_;
  bool ready_ = false;
  std::vector<td::Promise<td::Unit>> pending_ready_promises_;
};

}  // namespace tonlib
