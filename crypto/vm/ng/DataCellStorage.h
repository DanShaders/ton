/*
 * Copyright (c) 2025, Dan Klishch <danilklishch@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.0
 */

#pragma once

#include "vm/ng/LevelInfo.h"

namespace vm {

template <typename DataCellWithTrailer, size_t level, size_t inline_data_length>
class DataCellWithInlineStorage final : public DataCellWithTrailer {
 public:
  template <typename... Args>
  DataCellWithInlineStorage(Args&&... args)
      : DataCellWithTrailer(std::forward<Args>(args)..., m_inline_data, m_level_info) {
  }

 private:
  std::array<LevelInfo, level + 1> m_level_info;
  alignas(td::uint32) std::array<char, inline_data_length> m_inline_data;
};

template <size_t level, int bit_length_hi>
auto dispatch_bit_length_lo(int bit_length, auto&& func) {
  int bit_length_lo = bit_length >> 6 & 3;
  if (bit_length_lo == 0) {
    return func.template operator()<level, (bit_length_hi << 5) + (0 << 3) + 8>();
  } else if (bit_length_lo == 1) {
    return func.template operator()<level, (bit_length_hi << 5) + (1 << 3) + 8>();
  } else if (bit_length_lo == 2) {
    return func.template operator()<level, (bit_length_hi << 5) + (2 << 3) + 8>();
  } else {
    return func.template operator()<level, (bit_length_hi << 5) + (3 << 3) + 8>();
  }
}

template <size_t level>
auto dispatch_bit_length_hi(int bit_length, auto&& func) {
  int bit_length_hi = bit_length >> 8 & 3;
  if (bit_length_hi == 0) {
    return dispatch_bit_length_lo<level, 0>(bit_length, func);
  } else if (bit_length_hi == 1) {
    return dispatch_bit_length_lo<level, 1>(bit_length, func);
  } else if (bit_length_hi == 2) {
    return dispatch_bit_length_lo<level, 2>(bit_length, func);
  } else {
    return dispatch_bit_length_lo<level, 3>(bit_length, func);
  }
}

auto dispatch(int level, size_t hash_count, auto&& func) {
  if (hash_count == 1) {
    return dispatch_bit_length_hi<1>(level, func);
  } else if (hash_count == 2) {
    return dispatch_bit_length_hi<2>(level, func);
  } else if (hash_count == 3) {
    return dispatch_bit_length_hi<3>(level, func);
  } else {
    CHECK(hash_count == 4);
    return dispatch_bit_length_hi<4>(level, func);
  }
}

}  // namespace vm
