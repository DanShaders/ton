#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "tonlib/tonlibjson_export.h"

#ifdef __cplusplus
#include <string>

namespace tonlib {

class FFIEventLoop;
template <typename T>
class FFIAwaitable;

}  // namespace tonlib

using TonlibEventLoop = tonlib::FFIEventLoop;
using TonlibResponse = tonlib::FFIAwaitable<std::string>;
#else
typedef struct TonlibEventLoop TonlibEventLoop;
typedef struct TonlibResponse TonlibResponse;
#endif

typedef struct TonlibLiteClient TonlibLiteClient;

#ifdef __cplusplus
extern "C" {
#endif

// ===== Lite Client =====
// TonlibLiteClient represents an instance of the lite client. It allows sending
// lite API queries to the connected liteserver.

// Creates a new lite client instance.
//
// `config` should be a JSON object with the following fields:
//   - "address": string, the liteserver address in "IP:port" format
//   - "server_public_key": object, a ton_api PublicKey TL JSON object
//     (e.g. {"@type": "pub.ed25519", "key": "<base64>"})
//
// If creation of the instance fails, the error can be obtained from
// `tonlib_lite_client_is_error` and related functions.
TONLIBJSON_EXPORT TonlibLiteClient *tonlib_lite_client_create(TonlibEventLoop *loop, const char *config);

// Destroys the lite client instance. Error instances must be destroyed as well.
TONLIBJSON_EXPORT void tonlib_lite_client_destroy(TonlibLiteClient *client);

// Returns true if the lite client instance did not initialize properly.
TONLIBJSON_EXPORT bool tonlib_lite_client_is_error(TonlibLiteClient *client);

// Returns the error code. Can only be called if `tonlib_lite_client_is_error` returned true.
TONLIBJSON_EXPORT int tonlib_lite_client_get_error_code(TonlibLiteClient *client);

// Returns the error message. Can only be called if `tonlib_lite_client_is_error` returned true.
TONLIBJSON_EXPORT const char *tonlib_lite_client_get_error_message(TonlibLiteClient *client);

// Sends a lite API query to the connected liteserver. Can only be called if
// `tonlib_lite_client_is_error` returned false.
//
// `query` must be a JSON-encoded lite_api function object (e.g.
// `{"@type": "liteServer.getMasterchainInfo"}`).
//
// Returns a TonlibResponse that resolves with a JSON-encoded lite_api response object.
// Errors returned by the liteserver are returned as `liteServer.error` objects via the
// success path.
TONLIBJSON_EXPORT TonlibResponse *tonlib_lite_client_request(TonlibLiteClient *client, const char *query);

#ifdef __cplusplus
}  // extern "C"
#endif
