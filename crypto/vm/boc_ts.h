#pragma once
#include "boc.h"
#include "cells/Cell.h"
#include "common/refcnt.hpp"

namespace vm {
struct CellStorageStatTs {
  using CellInfo = CellStorageStat::CellInfo;

  unsigned long long bits{0};
  unsigned long long cells{0};
  td::HashSet<vm::Cell::Hash> seen;

  td::Result<CellInfo> dfs_visit_cells(td::Ref<vm::Cell> cell);

  unsigned long long limit_cells = std::numeric_limits<unsigned long long>::max();
  unsigned long long limit_bits = std::numeric_limits<unsigned long long>::max();
};
}  // namespace vm
