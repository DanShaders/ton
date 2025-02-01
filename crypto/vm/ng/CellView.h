/*
 * Copyright (c) 2025, Dan Klishch <danilklishch@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.0
 */

#pragma once

#include "vm/cells/DataCell.h"
#include "vm/ng/Noncopyable.h"

namespace vm {

class NonnullCellView {
  using VirtualizationParams = detail::VirtualizationParameters;

 public:
  TON_MAKE_DEFAULT_COPYABLE(NonnullCellView);
  TON_MAKE_DEFAULT_MOVABLE(NonnullCellView);

  static NonnullCellView create_from(Cell const& cell) {
    auto loaded_cell = cell.load_cell().move_as_ok();
    return NonnullCellView{*loaded_cell.data_cell.get(), loaded_cell.virt, std::move(loaded_cell.tree_node)};
  }

  int bit_length() const {
    return m_cell->get_bits();
  }

  td::uint8 const* data() const {
    return m_cell->get_data();
  }

  int refs_cnt() const {
    return m_cell->get_refs_cnt();
  }

  Cell::SpecialType special_type() const {
    return m_cell->special_type();
  }

  CellHash hash(int level = Cell::max_level) const {
    return m_cell->get_hash(std::min<int>(m_virtualization.get_level(), level));
  }

  td::uint16 depth(int level = Cell::max_level) const {
    return m_cell->get_depth(std::min<int>(m_virtualization.get_level(), level));
  }

  NonnullCellView ref(int idx) const {
    CHECK(idx >= 0 && idx < refs_cnt());
    auto loaded_cell = m_cell->get_ref(idx)->load_cell().move_as_ok();
    if (!loaded_cell.tree_node.empty()) {
      CHECK(m_node.empty());
    } else if (!m_node.empty()) {
      loaded_cell.tree_node = m_node.create_child(idx);
    }
    return NonnullCellView{
        *loaded_cell.data_cell,
        loaded_cell.virt.apply(m_child_virtualization),
        std::move(loaded_cell.tree_node),
    };
  }

 private:
  NonnullCellView(DataCell const& cell, VirtualizationParams virtualization, CellUsageTree::NodePtr&& tree_node)
      : m_cell(&cell), m_virtualization(virtualization), m_node(std::move(tree_node)) {
    if (!m_virtualization.empty()) {
      int child_level = m_virtualization.get_level() + (special_type() == Cell::SpecialType::MerkleProof ||
                                                        special_type() == Cell::SpecialType::MerkleUpdate);
      m_child_virtualization = {
          std::min<td::uint8>(CellTraits::max_level, static_cast<td::uint8>(child_level)),
          m_virtualization.get_virtualization(),
      };
    }
  }

  DataCell const* m_cell;
  VirtualizationParams m_virtualization;
  VirtualizationParams m_child_virtualization;
  CellUsageTree::NodePtr m_node;
};

}  // namespace vm
