#include "auto/tl/lite_api_json.h"
#include "auto/tl/ton_api_json.h"
#include "tl-utils/tl-utils.hpp"
#include "tl/tl_json.h"

#include "FFIAwaitable.h"
#include "FFIEventLoop.h"
#include "FFILiteClient.h"
#include "tonlib_lite_client.h"

// ===== Lite Client =====
namespace {

td::Result<tonlib::FFILiteClient> create_ffi_lite_client(TonlibEventLoop *loop, const char *config) {
  std::string config_str = config;
  TRY_RESULT(json, td::json_decode(config_str));
  if (json.type() != td::JsonValue::Type::Object) {
    return td::Status::Error("Config must be a JSON object");
  }

  ton::ton_api::liteClient_config parsed_config;
  TRY_STATUS(from_json(parsed_config, json.get_object()));

  td::IPAddress parsed_address;
  TRY_STATUS(parsed_address.init_host_port(parsed_config.address_));

  if (!parsed_config.server_public_key_) {
    return td::Status::Error("server_public_key is required in config");
  }
  auto server_public_key_slice = ton::serialize_tl_object(parsed_config.server_public_key_.get(), true);
  TRY_RESULT(parsed_server_public_key, ton::PublicKey::import(server_public_key_slice));

  return tonlib::FFILiteClient{*loop, parsed_address, parsed_server_public_key};
}

td::Result<ton::tl_object_ptr<ton::lite_api::Function>> parse_lite_query(const char *query) {
  std::string query_str = query;
  TRY_RESULT(json, td::json_decode(query_str));
  if (json.type() != td::JsonValue::Type::Object) {
    return td::Status::Error("Query must be a JSON object");
  }

  ton::tl_object_ptr<ton::lite_api::Function> parsed_query;
  TRY_STATUS(from_json(parsed_query, std::move(json)));

  return parsed_query;
}

}  // namespace

struct TonlibLiteClient {
  td::Result<tonlib::FFILiteClient> client;
};

TonlibLiteClient *tonlib_lite_client_create(TonlibEventLoop *loop, const char *config) {
  return new TonlibLiteClient{create_ffi_lite_client(loop, config)};
}

void tonlib_lite_client_destroy(TonlibLiteClient *client) {
  delete client;
}

bool tonlib_lite_client_is_error(TonlibLiteClient *client) {
  return client->client.is_error();
}

int tonlib_lite_client_get_error_code(TonlibLiteClient *client) {
  return client->client.error().code();
}

const char *tonlib_lite_client_get_error_message(TonlibLiteClient *client) {
  return client->client.error().message().data();
}

TonlibResponse *tonlib_lite_client_request(TonlibLiteClient *client, const char *query) {
  auto &lc = client->client.ok_ref();

  auto query_or_sync_error = parse_lite_query(query);

  if (query_or_sync_error.is_error()) {
    return TonlibResponse::create_resolved(lc.loop(), query_or_sync_error.move_as_error());
  }

  auto transform = [](ton::tl_object_ptr<ton::lite_api::Object> object) -> std::string {
    return td::json_encode<std::string>(td::ToJson(object));
  };

  auto [response, promise] =
      TonlibResponse::create_bridge<ton::tl_object_ptr<ton::lite_api::Object>>(lc.loop(), transform);
  lc.request(query_or_sync_error.move_as_ok(), std::move(promise));
  return response;
}
