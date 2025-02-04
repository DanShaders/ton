/*
    This file is part of TON Blockchain Library.

    TON Blockchain Library is free software: you can redistribute it and/or modify
    it under the terms of the GNU Lesser General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.

    TON Blockchain Library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public License
    along with TON Blockchain Library.  If not, see <http://www.gnu.org/licenses/>.

    Copyright 2017-2020 Telegram Systems LLP
*/
#pragma once
#include "vm/cells/CellTraits.h"
#include "common/bitstring.h"

#include "td/utils/as.h"

#include <array>
namespace td {
class StringBuilder;
}

namespace vm {
struct CellHash {
 public:
  td::Slice as_slice() const {
    return td::Slice(reinterpret_cast<const td::uint8*>(hash_.data()), CellTraits::hash_bytes);
  }
  td::MutableSlice as_slice() {
    return td::MutableSlice(reinterpret_cast<td::uint8*>(hash_.data()), CellTraits::hash_bytes);
  }
  bool operator==(const CellHash& other) const {
    return hash_ == other.hash_;
  }
  bool operator<(const CellHash& other) const {
    return hash_ < other.hash_;
  }
  bool operator!=(const CellHash& other) const {
    return hash_ != other.hash_;
  }
  std::string to_hex() const {
    return td::ConstBitPtr{reinterpret_cast<const td::uint8*>(hash_.data())}.to_hex(CellTraits::hash_bits);
  }
  friend td::StringBuilder& operator<<(td::StringBuilder& sb, const CellHash& hash);
  td::ConstBitPtr bits() const {
    return td::ConstBitPtr{reinterpret_cast<const td::uint8*>(hash_.data())};
  }
  td::BitPtr bits() {
    return td::BitPtr{reinterpret_cast<td::uint8*>(hash_.data())};
  }
  td::BitSlice as_bitslice() const {
    return td::BitSlice{reinterpret_cast<const td::uint8*>(hash_.data()), (unsigned int)CellTraits::hash_bits};
  }
  const std::array<td::uint64, CellTraits::hash_bytes / 8>& as_array() const {
    return hash_;
  }

  static CellHash from_slice(td::Slice slice) {
    CellHash res;
    CHECK(slice.size() == CellTraits::hash_bytes);
    td::MutableSlice(reinterpret_cast<td::uint8*>(res.hash_.data()), CellTraits::hash_bytes).copy_from(slice);
    return res;
  }

  td::uint64 hash() const {
    return hash_[1];
  }

 private:
  std::array<td::uint64, CellTraits::hash_bytes / 8> hash_;
  static_assert(sizeof(hash_) == CellTraits::hash_bytes);
};
}  // namespace vm

inline size_t cell_hash_slice_hash(td::Slice hash) {
  // use offset 8, because in db keys are grouped by first bytes.
  return td::as<size_t>(hash.substr(8, 8).ubegin());
}
namespace std {
template <>
struct hash<vm::CellHash> {
  typedef vm::CellHash argument_type;
  typedef td::uint64 result_type;
  static_assert(sizeof(size_t) == sizeof(result_type));
  result_type operator()(argument_type const& s) const noexcept {
    return s.hash();
  }
};
}  // namespace std
namespace vm {
template <class H>
H AbslHashValue(H h, const CellHash& cell_hash) {
  return H::combine(std::move(h), std::hash<vm::CellHash>()(cell_hash));
}
}  // namespace vm
