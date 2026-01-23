#include "auto/tl/ton_api_json.h"
#include "tl-utils/tl-utils.hpp"
#include "tl/tl_json.h"

#include "FFIAwaitable.h"
#include "FFIEventLoop.h"
#include "FFIPublicOverlayClient.h"
#include "tonlib_public_overlay.h"

namespace {

td::Result<tonlib::PublicOverlayClientConfig> parse_config(const char *config) {
  std::string config_str = config;
  TRY_RESULT(json, td::json_decode(config_str));
  if (json.type() != td::JsonValue::Type::Object) {
    return td::Status::Error("Config must be a JSON object");
  }

  ton::ton_api::publicOverlayClient_config parsed_config;
  TRY_STATUS(from_json(parsed_config, json.get_object()));

  tonlib::PublicOverlayClientConfig result;
  result.db_root = std::move(parsed_config.db_root_);

  TRY_STATUS(result.bind_address.init_host_port(parsed_config.bind_address_));

  if (!parsed_config.client_private_key_) {
    return td::Status::Error("client_private_key is required in config");
  }
  auto client_private_key_slice = ton::serialize_tl_object(parsed_config.client_private_key_.get(), true);
  TRY_RESULT(parsed_client_private_key, ton::PrivateKey::import(client_private_key_slice));
  result.client_private_key = std::move(parsed_client_private_key);

  if (!parsed_config.dht_config_) {
    return td::Status::Error("dht_config is required in config");
  }
  TRY_RESULT(dht_config, ton::dht::Dht::create_global_config(std::move(parsed_config.dht_config_)));
  result.dht_config = std::move(dht_config);

  if (!parsed_config.zerostate_) {
    return td::Status::Error("zerostate is required in config");
  }
  result.zerostate = ton::ZeroStateIdExt(parsed_config.zerostate_->workchain_, parsed_config.zerostate_->root_hash_,
                                         parsed_config.zerostate_->file_hash_);

  result.shard_id = ton::ShardIdFull(parsed_config.zerostate_->workchain_, parsed_config.shard_);

  return result;
}

}  // namespace

struct TonlibPublicOverlayClient {
  std::unique_ptr<tonlib::FFIPublicOverlayClient> client;
  tonlib::FFIAwaitable<td::Unit> *init;
};

TonlibPublicOverlayClient *tonlib_public_overlay_client_init(TonlibEventLoop *loop, const char *config) {
  auto parsed_config = parse_config(config);
  if (parsed_config.is_error()) {
    return new TonlibPublicOverlayClient{
        .client = nullptr,
        .init = tonlib::FFIAwaitable<td::Unit>::create_resolved(*loop, parsed_config.move_as_error()),
    };
  }

  auto client = new TonlibPublicOverlayClient{
      .client = std::make_unique<tonlib::FFIPublicOverlayClient>(*loop, parsed_config.move_as_ok()),
      .init = nullptr,
  };
  client->init = client->client->init();
  return client;
}

void tonlib_public_overlay_client_destroy(TonlibPublicOverlayClient *client) {
  client->init->destroy();
  delete client;
}

bool tonlib_public_overlay_client_await_ready(TonlibPublicOverlayClient *client) {
  return client->init->await_ready();
}

void tonlib_public_overlay_client_await_suspend(TonlibPublicOverlayClient *client, const void *continuation) {
  client->init->await_suspend({continuation});
}

bool tonlib_public_overlay_client_is_error(TonlibPublicOverlayClient *client) {
  return client->init->result().is_error();
}

int tonlib_public_overlay_client_get_error_code(TonlibPublicOverlayClient *client) {
  return client->init->result().error().code();
}

const char *tonlib_public_overlay_client_get_error_message(TonlibPublicOverlayClient *client) {
  return client->init->result().error().message().data();
}
