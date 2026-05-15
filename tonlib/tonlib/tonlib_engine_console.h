#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "tonlib_event_loop.h"

typedef struct TonlibEngineConsole TonlibEngineConsole;

#ifdef __cplusplus
extern "C" {
#endif

// ===== Engine Console =====
// TonlibEngineConsole represents an instance of the engine console client. It allows sending
// control queries to the connected validator engine.

// Creates a new engine console client instance.
//
// `config` should be a JSON-encoded `engineConsoleClient.config` object. If creation of the
// instance fails, the error can be obtained from `tonlib_engine_console_is_error` and related
// functions.
TONLIBJSON_EXPORT TonlibEngineConsole *tonlib_engine_console_create(TonlibEventLoop *loop, const char *config);

// Destroys the engine console client instance. Error instances must be destroyed as well.
TONLIBJSON_EXPORT void tonlib_engine_console_destroy(TonlibEngineConsole *console);

// Returns true if the engine console instance did not initialize properly.
TONLIBJSON_EXPORT bool tonlib_engine_console_is_error(TonlibEngineConsole *console);

// Returns the error code. Can only be called if `tonlib_engine_console_is_error` returned true.
TONLIBJSON_EXPORT int tonlib_engine_console_get_error_code(TonlibEngineConsole *console);

// Returns the error message. Can only be called if `tonlib_engine_console_is_error` returned true.
TONLIBJSON_EXPORT const char *tonlib_engine_console_get_error_message(TonlibEngineConsole *console);

// Sends a control query to the connected validator engine. Can only be called if
// `tonlib_engine_console_is_error` returned false.
//
// `query` must be a JSON-encoded control query object.
//
// Only errors produced locally will be reported as failed responses. On success, response resolves
// into a JSON-encoded TL object. It might be either a successful response with type determined by
// the TL scheme or an `engine.validator.controlQueryError` object if remote has encountered an
// error.
TONLIBJSON_EXPORT TonlibResponse *tonlib_engine_console_request(TonlibEngineConsole *console, const char *query);

#ifdef __cplusplus
}  // extern "C"
#endif
