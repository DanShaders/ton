/*
 * Copyright (c) 2026, TON CORE TECHNOLOGIES CO. L.L.C
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#pragma once

#include "collectors.h"

namespace ton::metrics {

#define TON_METRIC_DIRECTION_LIST(F) \
  F(in)                              \
  F(out)
TON_METRIC_DEFINE_LABEL(Direction, "direction", TON_METRIC_DIRECTION_LIST)
#undef TON_METRIC_DIRECTION_LIST

}  // namespace ton::metrics
