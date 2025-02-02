/*
 * Copyright (c) 2025, Dan Klishch <danilklishch@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.0
 */

#include "vm/ng/CellView.h"

namespace vm {

CellResolutionResult NonnullCellView::resolve(VmStateInterface* interface, CanBeSpecial can_be_special) {
  static const auto prunned_cell_load_err = VmError{Excno::cell_und, "trying to load prunned cell"};
  static const auto unexpected_special_cell_err = VmError{Excno::cell_und, "unexpected special cell"};

  if (!interface) {
    if (m_cell->special_type() == Cell::SpecialType::PrunnedBranch && m_virtualization.get_virtualization() != 0) {
      return VmVirtError{m_virtualization.get_virtualization()};
    }

    if (can_be_special == CanBeSpecial::No && m_cell->is_special()) {
      if (m_cell->special_type() == Cell::SpecialType::PrunnedBranch) {
        return prunned_cell_load_err;
      } else if (m_cell->special_type() != Cell::SpecialType::Library) {
        return unexpected_special_cell_err;
      }
      return VmError{Excno::cell_und, "failed to load library cell (no vm_state_interface available)"};
    }

    return *this;
  }

  auto result = *this;

  interface->register_cell_load(hash());
  bool loading_library_recursively = false;

  while (true) {
    if (result.m_cell->special_type() == Cell::SpecialType::PrunnedBranch &&
        result.m_virtualization.get_virtualization() != 0) {
      return VmVirtError{result.m_virtualization.get_virtualization()};
    }

    if (can_be_special == CanBeSpecial::Yes || !result.m_cell->is_special()) {
      return result;
    }
    // CHECK(can_be_special == CanBeSpecial::No && result.m_cell->is_special());

    if (result.m_cell->special_type() == Cell::SpecialType::PrunnedBranch) {
      return prunned_cell_load_err;
    } else if (result.m_cell->special_type() != Cell::SpecialType::Library) {
      return unexpected_special_cell_err;
    }

    if (loading_library_recursively && interface->get_global_version() >= 5) {
      return VmError{Excno::cell_und, "failed to load library cell: recursive library cells are not allowed"};
    }
    loading_library_recursively = true;

    auto new_cell = interface->load_library(td::ConstBitPtr{result.data(), 8});
    if (new_cell.is_null()) {
      return VmError{Excno::cell_und, "failed to load library cell"};
    }
    result = NonnullCellView::create_from(*new_cell);
  }
}

}  // namespace vm
