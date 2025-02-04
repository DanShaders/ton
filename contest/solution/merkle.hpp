#pragma once

#include <vector>
#include <string>
#include <functional>
#include <future>
#include <chrono>
#include <fstream>
#include "BS_thread_pool.hpp"

#include "vm/cells/Cell.h"
#include "vm/cells/CellSlice.h"
#include "vm/cells/CellBuilder.h"

#include "td/utils/Status.h"

#include "various.hpp"
#include "settings.hpp"


namespace solution {

using namespace ton;
using namespace ton::validator;
using vm::Cell;
using vm::CellSlice;
using vm::CellUsageTree;
using vm::CellBuilder;



class MyMerkleUpdate {
 public:
  static Ref<Cell> generate(BS::thread_pool<>& pool, Ref<Cell> from, Ref<Cell> to, CellUsageTree *usage_tree);
  static std::pair<Ref<Cell>, Ref<Cell>> generate_raw(BS::thread_pool<>& pool, Ref<Cell> from, Ref<Cell> to, CellUsageTree *usage_tree);
};


class MyMerkleProof {
 public:
  using IsPrunnedFunction = std::function<bool(const Ref<Cell> &)>;

  static Ref<Cell> generate_raw(BS::thread_pool<>& pool, Ref<Cell> cell, IsPrunnedFunction is_prunned);
  static Ref<Cell> generate_raw(BS::thread_pool<>& pool, Ref<Cell> cell, CellUsageTree *usage_tree);
};


}

