#pragma once

#include <vector>
#include <string>
#include <functional>
#include <future>
#include <chrono>
#include <fstream>

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
  static Ref<Cell> generate(Ref<Cell> from, Ref<Cell> to, CellUsageTree *usage_tree);
  static std::pair<Ref<Cell>, Ref<Cell>> generate_raw(Ref<Cell> from, Ref<Cell> to, CellUsageTree *usage_tree);
};


class MyMerkleProof {
 public:
  using IsPrunnedFunction = std::function<bool(const Ref<Cell> &)>;

  static Ref<Cell> generate_raw(Ref<Cell> cell, IsPrunnedFunction is_prunned);
  static Ref<Cell> generate_raw(Ref<Cell> cell, CellUsageTree *usage_tree);
};


}

