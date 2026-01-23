#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "tonlib/tonlibjson_export.h"

#include "tonlib_engine_console.h"

typedef struct TonlibPublicOverlayClient TonlibPublicOverlayClient;

#ifdef __cplusplus
extern "C" {
#endif

TONLIBJSON_EXPORT TonlibPublicOverlayClient *tonlib_public_overlay_client_init(TonlibEventLoop *loop,
                                                                               const char *config);

TONLIBJSON_EXPORT void tonlib_public_overlay_client_destroy(TonlibPublicOverlayClient *client);

TONLIBJSON_EXPORT bool tonlib_public_overlay_client_await_ready(TonlibPublicOverlayClient *client);

TONLIBJSON_EXPORT void tonlib_public_overlay_client_await_suspend(TonlibPublicOverlayClient *client,
                                                                  const void *continuation);

TONLIBJSON_EXPORT bool tonlib_public_overlay_client_is_error(TonlibPublicOverlayClient *client);

TONLIBJSON_EXPORT int tonlib_public_overlay_client_get_error_code(TonlibPublicOverlayClient *client);

TONLIBJSON_EXPORT const char *tonlib_public_overlay_client_get_error_message(TonlibPublicOverlayClient *client);

#ifdef __cplusplus
}
#endif
