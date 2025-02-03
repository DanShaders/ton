/*
 * Copyright (c) 2025, Dan Klishch <danilklishch@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.0
 */

#pragma once

#include "td/utils/Span.h"
#include "vm/cells/Cell.h"
#include "vm/ng/LevelInfo.h"

namespace vm {

class NonnullCellView;

class DataCell : public Cell {
 public:
  DataCell(DataCell const&) = delete;
  DataCell(DataCell&&) = delete;

  virtual ~DataCell();

  static td::Result<Ref<DataCell>> create(td::Slice data, int bit_length, td::Span<Ref<Cell>> refs, bool is_special);
  static Ref<DataCell> create_pruned_branch(NonnullCellView view, int merkle_up_depth);

  virtual td::Result<LoadedCell> load_cell() const override final {
    return LoadedCell{
        .data_cell = Ref<DataCell>{this},
        .virt = {},
        .tree_node = {},
    };
  }

  virtual td::uint32 get_virtualization() const override final {
    return m_virtualization;
  }

  virtual CellUsageTree::NodePtr get_tree_node() const override final {
    return {};
  }

  virtual bool is_loaded() const override final {
    return true;
  }

  virtual LevelMask get_level_mask() const override final {
    return m_level_mask;
  }

  virtual bool is_data_cell() const override {
    return true;
  }

  // ===== Old interface begins =====
  static thread_local bool use_arena;

  static void store_depth(td::uint8* dest, td::uint16 depth) {
    td::bitstring::bits_store_long(dest, depth, depth_bits);
  }

  static td::uint16 load_depth(const td::uint8* src) {
    return td::bitstring::bits_load_ulong(src, depth_bits) & 0xffff;
  }

  unsigned get_refs_cnt() const {
    return m_refs_cnt;
  }

  unsigned get_bits() const {
    return m_bit_length;
  }

  unsigned size_refs() const {
    return m_refs_cnt;
  }

  unsigned size() const {
    return m_bit_length;
  }

  unsigned char const* get_data() const {
    return reinterpret_cast<unsigned char const*>(m_data);
  }

  Ref<Cell> get_ref(unsigned idx) const {
    return Ref<Cell>{reinterpret_cast<Cell*>(m_refs[std::min<td::uint32>(max_refs, idx)] & pointer_mask)};
  }

  Cell* get_ref_raw_ptr(unsigned idx) const {
    CHECK(false);  // TODO
  }

  Ref<Cell> reset_ref_unsafe(unsigned idx, Ref<Cell> ref, bool check_hash = true) {
    CHECK(false);  // TODO
  }

  bool is_special() const {
    return m_type != SpecialType::Ordinary;
  }

  SpecialType special_type() const {
    return m_type;
  }

  int get_serialized_size(bool with_hashes = false) const {
    return ((get_bits() + 23) >> 3) +
           (with_hashes ? get_level_mask().get_hashes_count() * (hash_bytes + depth_bytes) : 0);
  }

  size_t get_storage_size() const {
    CHECK(false);  // TODO
  }

  int serialize(unsigned char* buff, int buff_size, bool with_hashes = false) const;

  std::string serialize() const;

  std::string to_hex() const;

  static td::int64 get_total_data_cells() {
    CHECK(false);  // TODO
  }

  template <class StorerT>
  void store(StorerT& storer) const {
    storer.template store_binary<td::uint8>(construct_d1(max_level));
    storer.template store_binary<td::uint8>(construct_d2());
    storer.store_slice(td::Slice(get_data(), (get_bits() + 7) / 8));
  }
  // ===== Old interface ends =====

  // idx must be >= and < get_refs_cnt().
  std::variant<Cell const*, DataCell const*> ref_fast_path(int idx) const {
    auto reference = reinterpret_cast<Cell const*>(m_refs[idx] & pointer_mask);
    if (m_refs[idx] & pointer_tag) {
      return static_cast<DataCell const*>(reference);
    } else {
      return reference;
    }
  }

 protected:
  // BitReader relies on inline_data being aligned on 4-byte boundary and being at least
  // `max(ceil(bit_length / 32) * 4, 2)` bytes long.
  DataCell(int bit_length, int refs_cnt, Cell::SpecialType type, LevelMask level_mask, td::uint8 virtualization,
           std::span<char const> data, std::span<LevelInfo const> level_info)
      : m_bit_length(bit_length)
      , m_refs_cnt(static_cast<td::uint8>(refs_cnt))
      , m_type(type)
      , m_level(static_cast<td::uint8>(level_mask.get_level()))
      , m_virtualization(virtualization)
      , m_level_mask(level_mask)
      , m_data(data.data())
      , m_level_info(level_info.data()) {
  }

 private:
  static constexpr uintptr_t pointer_mask = ~static_cast<uintptr_t>(1);
  static constexpr uintptr_t pointer_tag = 1;

  virtual td::uint16 do_get_depth(td::uint32 level) const override final {
    return m_level_info[std::min<td::uint32>(m_level, level)].depth;
  }

  virtual const Hash do_get_hash(td::uint32 level) const override final {
    return m_level_info[std::min<td::uint32>(m_level, level)].hash;
  }

  td::uint8 construct_d1(td::uint32 level) const {
    return static_cast<td::uint8>(m_refs_cnt + (is_special() << 3) + (m_level_mask.apply(level).get_mask() << 5));
  }

  td::uint8 construct_d2() const {
    return static_cast<td::uint8>(m_bit_length / 8 + (m_bit_length + 7) / 8);
  }

  unsigned m_bit_length;

  td::uint8 m_refs_cnt;
  Cell::SpecialType m_type;
  td::uint8 m_level;
  td::uint8 m_virtualization;

  LevelMask m_level_mask;

  char const* m_data;
  LevelInfo const* m_level_info;
  std::array<uintptr_t, max_refs> m_refs = {};
};

inline std::ostream& operator<<(std::ostream& os, const DataCell& c) {
  return os << c.to_hex();
}

inline CellHash as_cell_hash(const Ref<DataCell>& cell) {
  return cell->get_hash();
}

}  // namespace vm
