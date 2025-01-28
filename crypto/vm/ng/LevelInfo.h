/*
 * Copyright (c) 2025, Dan Klishch <danilklishch@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.0
 */

#pragma once

#include "vm/cells/CellHash.h"

namespace vm {

struct LevelInfo {
  CellHash hash;
  td::uint16 depth;
};

}  // namespace vm
