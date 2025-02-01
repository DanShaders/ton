/*
 * Copyright (c) 2025, Dan Klishch <danilklishch@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.0
 */

#include "openssl/digest.hpp"
#include "vm/cells/DataCell.h"
#include "vm/ng/DataCellStorage.h"

namespace vm {

DataCell::~DataCell() {
  for (int i = 0; i < m_refs_cnt; ++i) {
    Ref{reinterpret_cast<Cell*>(m_refs[i] & pointer_mask), Ref<Cell>::acquire_t{}};
  }
}

namespace {

class CellChecker {
 public:
  CellChecker(bool is_special, td::Slice data, int bit_length, int refs_cnt)
      : m_is_special(is_special), m_refs_cnt(refs_cnt), m_data(data), m_bit_length(bit_length) {
  }

  virtual ~CellChecker() = default;

  td::Status check_and_compute_level_info() {
    m_type = Cell::SpecialType::Ordinary;

    if (m_is_special) {
      if (m_bit_length < 8) {
        return td::Status::Error("Not enough data for a special cell");
      }

      m_type = static_cast<Cell::SpecialType>(read_byte(0));
      if (m_type == Cell::SpecialType::Ordinary) {
        return td::Status::Error("Invalid special cell type");
      }
    }

    switch (m_type) {
      case Cell::SpecialType::Ordinary:
        TRY_STATUS(check_ordinary_cell());
        break;
      case Cell::SpecialType::PrunnedBranch:
        TRY_STATUS(check_prunned_branch());
        break;
      case Cell::SpecialType::Library:
        TRY_STATUS(check_library());
        break;
      case Cell::SpecialType::MerkleProof:
        TRY_STATUS(check_merkle_proof());
        break;
      case Cell::SpecialType::MerkleUpdate:
        TRY_STATUS(check_merkle_update());
        break;
      default:
        return td::Status::Error("Invalid special cell type");
    }

    for (int i = 0; i < max_level; ++i) {
      if (m_depth[i] < m_depth[i + 1]) {
        CHECK(false);
        return td::Status::Error("Depth array is invalid");
      }
    }
    if (m_depth[0] > CellTraits::max_depth) {
      return td::Status::Error("Depth is too big");
    }

    for (int i = 0; i < m_refs_cnt; ++i) {
      m_virtualization = std::max(m_virtualization, get_ref_virtualization(i));
    }
    if (!std::in_range<td::uint8>(m_virtualization)) {
      return td::Status::Error("Virtualization is too big to be stored in DataCell");
    }

    // If only hash was computed as documented...
    std::optional<int> last_computed_hash;

    for (int i = 0; i <= max_level; ++i) {
      if (!m_level_mask.is_significant(i + 1) && i != max_level) {
        continue;
      }

      compute_hash(i, last_computed_hash);
      for (int j = last_computed_hash.value_or(-1) + 1; j < i; ++j) {
        m_hash[j] = m_hash[i];
      }
      last_computed_hash = i;
    }

    return {};
  }

  // Getters for computed values
  Cell::SpecialType type() const {
    return m_type;
  }

  Cell::LevelMask level_mask() const {
    return m_level_mask;
  }

  td::uint8 virtualization() const {
    return static_cast<td::uint8>(m_virtualization);
  }

  std::array<td::uint16, 4> const& depths() const {
    return m_depth;
  }

  std::array<CellHash, 4> const& hashes() const {
    return m_hash;
  }

 private:
  static constexpr int max_level = CellTraits::max_level;

  static constexpr int hash_bytes = CellTraits::hash_bytes;
  static_assert(hash_bytes == sizeof(CellHash));

  static constexpr int depth_bytes = CellTraits::depth_bytes;
  static_assert(depth_bytes == 2);

  virtual Cell::LevelMask get_ref_level_mask(int idx) const = 0;
  virtual CellHash get_ref_hash(int idx, int level) const = 0;
  virtual td::uint16 get_ref_depth(int idx, int level) const = 0;
  virtual td::uint32 get_ref_virtualization(int idx) const = 0;

  td::uint8 read_byte(size_t i) {
    return m_data[i];
  }

  td::Status check_ordinary_cell() {
    for (int i = 0; i < m_refs_cnt; ++i) {
      m_level_mask = m_level_mask.apply_or(get_ref_level_mask(i));

      for (int j = 0; j <= max_level; ++j) {
        m_depth[j] = std::max(m_depth[j], get_ref_depth(i, j));
      }
    }

    if (m_refs_cnt != 0) {
      for (auto& depth : m_depth) {
        ++depth;
      }
    }

    return {};
  }

  td::Status check_prunned_branch() {
    if (m_refs_cnt != 0) {
      return td::Status::Error("Prunned branch cannot have references");
    }
    if (m_bit_length < 16) {
      return td::Status::Error("Length mismatch in a prunned branch");
    }

    m_level_mask = Cell::LevelMask{read_byte(1)};
    if (m_level_mask.get_level() == 0 || m_level_mask.get_level() > max_level) {
      return td::Status::Error("Invalid level mask in a prunned branch");
    }

    int hashes_count = m_level_mask.get_hash_i();
    auto expected_byte_size = 2 + hashes_count * (hash_bytes + depth_bytes);

    if (m_bit_length != static_cast<int>(expected_byte_size * 8)) {
      return td::Status::Error("Length mismatch in a prunned branch");
    }

    // depth[max_level] = 0;

    for (int i = max_level; i--;) {
      if (m_level_mask.is_significant(i + 1)) {
        td::uint16 stored_depth;
        int hashes_before = m_level_mask.apply(i).get_hash_i();
        auto offset = 2 + hashes_count * hash_bytes + hashes_before * depth_bytes;
        std::memcpy(&stored_depth, m_data.begin() + offset, 2);
        m_depth[i] = __builtin_bswap16(stored_depth);
      } else {
        m_depth[i] = m_depth[i + 1];
      }
    }

    return {};
  }

  td::Status check_library() {
    if (m_refs_cnt != 0) {
      return td::Status::Error("Library cell cannot have references");
    }
    if (m_bit_length != 8 * (1 + hash_bytes)) {
      return td::Status::Error("Length mismatch in a library cell");
    }

    return {};
  }

  td::Status check_merkle_child(int child_idx, int hash_offset, int depth_offset) {
    CellHash stored_hash;
    std::memcpy(&stored_hash, m_data.begin() + hash_offset, hash_bytes);
    if (stored_hash != get_ref_hash(child_idx, 0)) {
      return td::Status::Error("Invalid hash in a merkle proof or update");
    }

    td::uint16 stored_depth;
    std::memcpy(&stored_depth, m_data.begin() + depth_offset, depth_bytes);
    if (__builtin_bswap16(stored_depth) != get_ref_depth(child_idx, 0)) {
      return td::Status::Error("Invalid depth in a merkle proof or update");
    }

    for (int i = 0; i <= max_level; ++i) {
      m_depth[i] = std::max<td::uint16>(m_depth[i], get_ref_depth(child_idx, std::min(i + 1, max_level)) + 1);
    }

    return {};
  }

  td::Status check_merkle_proof() {
    if (m_refs_cnt != 1) {
      return td::Status::Error("Merkle proof must have exactly one reference");
    }
    if (m_bit_length != 8 * (1 + hash_bytes + depth_bytes)) {
      return td::Status::Error("Length mismatch in a merkle proof");
    }

    TRY_STATUS(check_merkle_child(0, 1, 1 + hash_bytes));

    m_level_mask = get_ref_level_mask(0).shift_right();

    return {};
  }

  td::Status check_merkle_update() {
    if (m_refs_cnt != 2) {
      return td::Status::Error("Merkle update must have exactly two references");
    }
    if (m_bit_length != 8 * (1 + (hash_bytes + depth_bytes) * 2)) {
      return td::Status::Error("Length mismatch in a merkle update");
    }

    TRY_STATUS(check_merkle_child(0, 1, 1 + 2 * hash_bytes));
    TRY_STATUS(check_merkle_child(1, 1 + hash_bytes, 1 + 2 * hash_bytes + 2));

    m_level_mask = get_ref_level_mask(0).apply_or(get_ref_level_mask(1)).shift_right();

    return {};
  }

  void compute_hash(int level, std::optional<int> last_computed_hash) {
    if (level != max_level && m_type == Cell::SpecialType::PrunnedBranch) {
      int hashes_before = m_level_mask.apply(level).get_hash_i();
      auto offset = 2 + hashes_before * hash_bytes;
      std::memcpy(&m_hash[level], m_data.begin() + offset, hash_bytes);
      return;
    }

    digest::SHA256 hasher;

    auto d1 = m_refs_cnt + (m_is_special << 3) + (m_level_mask.apply(level).get_mask() << 5);
    auto d2 = (m_bit_length >> 3 << 1) + ((m_bit_length & 7) != 0);
    td::uint8 header[] = {static_cast<td::uint8>(d1), static_cast<td::uint8>(d2)};
    hasher.feed(header, 2);

    if (last_computed_hash.has_value() && m_type != Cell::SpecialType::PrunnedBranch) {
      hasher.feed(m_hash[last_computed_hash.value()].as_slice());
    } else {
      hasher.feed(m_data.substr(0, m_bit_length / 8));
      if (m_bit_length % 8 != 0) {
        td::uint8 last_byte = m_data[m_bit_length / 8];
        last_byte >>= 7 - m_bit_length % 8;
        last_byte |= 1;
        last_byte <<= 7 - m_bit_length % 8;
        hasher.feed({&last_byte, 1});
      }
    }

    auto child_level = (m_type == Cell::SpecialType::MerkleUpdate || m_type == Cell::SpecialType::MerkleProof
                            ? std::min(max_level, level + 1)
                            : level);

    for (int i = 0; i < m_refs_cnt; ++i) {
      td::uint16 depth = get_ref_depth(i, child_level);
      depth = __builtin_bswap16(depth);
      td::uint8 stored_depth[depth_bytes];
      std::memcpy(stored_depth, &depth, depth_bytes);

      hasher.feed(stored_depth, depth_bytes);
    }

    for (int i = 0; i < m_refs_cnt; ++i) {
      hasher.feed(get_ref_hash(i, child_level).as_slice());
    }

    hasher.extract(m_hash[level].as_slice());
  }

  bool m_is_special;
  Cell::SpecialType m_type;
  int m_refs_cnt;
  td::Slice m_data;
  int m_bit_length;

  Cell::LevelMask m_level_mask;
  td::uint32 m_virtualization = 0;
  std::array<td::uint16, max_level + 1> m_depth = {};
  std::array<CellHash, max_level + 1> m_hash = {};
};

Ref<DataCell> allocate_cell(int bit_length, Cell::LevelMask level_mask, int refs_cnt, Cell::SpecialType type,
                            td::uint8 virtualization) {
  auto result = dispatch(bit_length, level_mask.get_level() + 1,
                         [&]<size_t hash_count, size_t inline_data_length>() -> DataCell* {
                           return new DataCellWithInlineStorage<DataCell, hash_count, inline_data_length>{
                               bit_length, refs_cnt, type, level_mask, virtualization};
                         });
  return Ref<DataCell>{result, Ref<DataCell>::acquire_t{}};
}

}  // namespace

td::Result<Ref<DataCell>> DataCell::create(td::Slice data, int bit_length, td::Span<Ref<Cell>> refs, bool is_special) {
  CHECK(std::cmp_greater_equal(data.size() * 8, bit_length));
  if (std::cmp_greater(refs.size(), 4)) {
    return td::Status::Error("Too many references");
  }
  if (std::cmp_greater(bit_length, 1023)) {
    return td::Status::Error("Too many data bits");
  }

  class Checker final : public CellChecker {
   public:
    Checker(bool is_special, td::Slice data, int bit_length, td::Span<Ref<Cell>> refs)
        : CellChecker(is_special, data, bit_length, static_cast<int>(refs.size())), m_refs(refs) {
    }

   private:
    virtual Cell::LevelMask get_ref_level_mask(int idx) const override {
      return m_refs[idx]->get_level_mask();
    }

    virtual CellHash get_ref_hash(int idx, int level) const override {
      return m_refs[idx]->get_hash(level);
    }

    virtual td::uint16 get_ref_depth(int idx, int level) const override {
      return m_refs[idx]->get_depth(level);
    }

    virtual td::uint32 get_ref_virtualization(int idx) const override {
      return m_refs[idx]->get_virtualization();
    }

   private:
    td::Span<Ref<Cell>> m_refs;
  };

  Checker checker{is_special, data, bit_length, refs};
  TRY_STATUS(checker.check_and_compute_level_info());

  auto result = allocate_cell(bit_length, checker.level_mask(), static_cast<int>(refs.size()), checker.type(),
                              checker.virtualization());
  auto& cell = result.write();

  auto mutable_data = const_cast<char*>(cell.m_data);
  std::memcpy(mutable_data, data.data(), (bit_length + 7) / 8);
  if (bit_length % 8 != 0) {
    auto& last_byte = mutable_data[bit_length / 8];
    last_byte >>= (7 - bit_length % 8);
    last_byte |= 1;
    last_byte <<= (7 - bit_length % 8);
  }

  auto mutable_level_info = const_cast<LevelInfo*>(cell.m_level_info);
  for (int i = 0; i <= cell.m_level; ++i) {
    mutable_level_info[i] = {
        .hash = checker.hashes()[i],
        .depth = checker.depths()[i],
    };
  }

  for (int i = 0; i < cell.m_refs_cnt; ++i) {
    auto ref = reinterpret_cast<uintptr_t>(Ref{refs[i]}.release());
    cell.m_refs[i] = ref | (refs[i]->is_data_cell() ? pointer_tag : 0);
  }

  return result;
}

thread_local bool DataCell::use_arena = false;

int DataCell::serialize(unsigned char* buff, int buff_size, bool with_hashes) const {
  int len = get_serialized_size(with_hashes);
  if (len > buff_size) {
    return 0;
  }
  buff[0] = static_cast<unsigned char>(construct_d1(max_level) | (with_hashes * 16));
  buff[1] = construct_d2();
  int hs = 0;
  if (with_hashes) {
    hs = (get_level_mask().get_hashes_count()) * (hash_bytes + depth_bytes);
    assert(len >= 2 + hs);
    std::memset(buff + 2, 0, hs);
    auto dest = td::MutableSlice(buff + 2, hs);
    auto level = get_level();
    // TODO: optimize for prunned branch
    for (unsigned i = 0; i <= level; i++) {
      if (!get_level_mask().is_significant(i)) {
        continue;
      }
      dest.copy_from(get_hash(i).as_slice());
      dest.remove_prefix(hash_bytes);
    }
    for (unsigned i = 0; i <= level; i++) {
      if (!get_level_mask().is_significant(i)) {
        continue;
      }
      store_depth(dest.ubegin(), get_depth(i));
      dest.remove_prefix(depth_bytes);
    }
    buff += hs;
    len -= hs;
  }
  std::memcpy(buff + 2, get_data(), len - 2);
  return len + hs;
}

std::string DataCell::serialize() const {
  unsigned char buff[max_serialized_bytes];
  int len = serialize(buff, sizeof(buff));
  return std::string(buff, buff + len);
}

std::string DataCell::to_hex() const {
  unsigned char buff[max_serialized_bytes];
  int len = serialize(buff, sizeof(buff));
  char hex_buff[max_serialized_bytes * 2 + 1];
  for (int i = 0; i < len; i++) {
    snprintf(hex_buff + 2 * i, sizeof(hex_buff) - 2 * i, "%02x", buff[i]);
  }
  return hex_buff;
}

}  // namespace vm
