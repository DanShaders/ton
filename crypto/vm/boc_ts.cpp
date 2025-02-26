#include "boc_ts.h"

#include "cells/CellSlice.h"

td::Result<vm::CellStorageStatTs::CellInfo> vm::CellStorageStatTs::dfs_visit_cells(Ref<vm::Cell> cell) {
  if (cell.is_null()) {
    return td::Status::Error("cell is null");
  }
  if (!seen.insert(cell->get_hash()).second) {
    return CellInfo{};
  }
  vm::CellSlice cs{vm::NoVm{}, std::move(cell)};
  ++cells;

  if (cells > limit_cells) {
    return td::Status::Error("too many cells");
  }

  bits += cs.size();
  if (bits > limit_bits) {
    return td::Status::Error("too many bits");
  }

  CellInfo res;
  while (cs.size_refs()) {
    auto ref_id = cs.refs_st++;
    auto child_ref = cs.cell->get_ref(ref_id)->virtualize(cs.child_virt());

    TRY_RESULT(child, dfs_visit_cells(child_ref));
    res.max_merkle_depth = std::max(res.max_merkle_depth, child.max_merkle_depth);
  }
  if (cs.special_type() == CellTraits::SpecialType::MerkleProof ||
      cs.special_type() == CellTraits::SpecialType::MerkleUpdate) {
    ++res.max_merkle_depth;
  }
  return res;
}
