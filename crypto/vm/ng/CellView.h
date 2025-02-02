/*
 * Copyright (c) 2025, Dan Klishch <danilklishch@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.0
 */

#pragma once

#include "vm/cells/CellSlice.h"
#include "vm/cells/DataCell.h"
#include "vm/excno.hpp"
#include "vm/ng/BitReader.h"
#include "vm/ng/Noncopyable.h"

namespace vm {

namespace detail {

template <typename... Ts>
struct OverloadSet : Ts... {
  using Ts::operator()...;
};

template <typename... Ts>
OverloadSet(Ts...) -> OverloadSet<Ts...>;

template <typename Variant, typename... Visitors>
decltype(auto) visit(Variant&& variant, Visitors&&... visitors) {
  return std::visit(detail::OverloadSet{std::forward<Visitors>(visitors)...}, std::forward<Variant>(variant));
}

}  // namespace detail

struct CellResolutionResult;

class NonnullCellView {
  using VirtualizationParams = detail::VirtualizationParameters;

 public:
  enum class CanBeSpecial {
    Yes,
    No,
  };

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
    return detail::visit(
        m_cell->ref_fast_path(idx),
        [&](DataCell const* cell) {
          return NonnullCellView{
              *cell,
              m_child_virtualization,
              m_node.empty() ? CellUsageTree::NodePtr{} : m_node.create_child(idx),
          };
        },
        [&](Cell const* cell) {
          auto loaded_cell = cell->load_cell().move_as_ok();
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
        });
  }

  BitReader data_bit_reader() const {
    return BitReader{reinterpret_cast<td::uint32 const*>(data()), bit_length()};
  }

  Ref<CellSlice> as_ref_slice() const {
    return td::make_ref<CellSlice>(Cell::LoadedCell{
        .data_cell = Ref<DataCell>{m_cell},
        .virt = m_virtualization,
        .tree_node = m_node,
    });
  }

  CellResolutionResult resolve(VmStateInterface* interface, CanBeSpecial);

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

struct CellResolutionResult {
  template <typename T>
  CellResolutionResult(T&& result) : result(std::forward<T>(result)) {
  }

  NonnullCellView unwrap_or_throw() {
    return detail::visit(
        std::move(result), [](NonnullCellView result) { return result; },
        [](auto&& other) -> NonnullCellView { throw std::move(other); });
  }

  std::variant<NonnullCellView, VmError, VmVirtError> result;
};

}  // namespace vm
