#pragma once

#include "adnl/adnl.h"
#include "dht/dht.h"
#include "keyring/keyring.h"
#include "keys/keys.hpp"
#include "overlay/overlay.h"
#include "td/actor/actor.h"
#include "td/actor/coro_task.h"
#include "td/utils/port/IPAddress.h"
#include "ton/ton-types.h"

namespace tonlib {

struct PublicOverlayClientConfig {
  std::string db_root;
  td::IPAddress bind_address;
  ton::PrivateKey client_private_key;
  std::shared_ptr<ton::dht::DhtGlobalConfig> dht_config;
  ton::ZeroStateIdExt zerostate;
  ton::ShardIdFull shard_id;
};

class PublicOverlayClient : public td::actor::Actor {
 public:
  PublicOverlayClient(PublicOverlayClientConfig config);

  td::actor::Task<td::Unit> init();

 private:
  PublicOverlayClientConfig config_;

  ton::adnl::AdnlNodeIdShort local_id_;
  ton::overlay::OverlayIdFull overlay_id_;

  td::actor::ActorOwn<ton::adnl::AdnlNetworkManager> network_manager_;
  td::actor::ActorOwn<ton::keyring::Keyring> keyring_;
  td::actor::ActorOwn<ton::adnl::Adnl> adnl_;
  td::actor::ActorOwn<ton::dht::Dht> dht_;
  td::actor::ActorOwn<ton::overlay::Overlays> overlays_;
};

}  // namespace tonlib
