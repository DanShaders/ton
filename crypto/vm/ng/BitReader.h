/*
 * Copyright (c) 2025, Dan Klishch <danilklishch@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.0
 */

#include <algorithm>
#include <bit>
#include <optional>

#include "common/bitstring.h"
#include "td/utils/check.h"
#include "td/utils/int_types.h"

namespace vm {

class BitReader {
  using Word = td::uint32;
  using DoubleWord = td::uint64;
  static constexpr int bits_in_word = sizeof(Word) * 8;

 public:
  BitReader(Word const* m_data, int bit_length) : m_refill_pointer(m_data + 2), m_bit_length(bit_length) {
    m_current_and_next_word = __builtin_bswap64(DoubleWord{m_data[0]} | DoubleWord{m_data[1]} << bits_in_word);
  }

  int remaining_bits() const {
    return m_bit_length - m_offset;
  }

  std::optional<td::uint32> read_le32(int bits) {
    CHECK(bits <= bits_in_word);
    if (m_offset + bits > m_bit_length) {
      return std::nullopt;
    }
    auto res = m_current_and_next_word << m_offset >> (2 * bits_in_word - bits);
    m_offset += bits;
    if (m_offset >= bits_in_word) {
      refill_low();
    }
    return res;
  }

  std::optional<bool> read_bit() {
    auto bit = read_le32(1);
    return bit ? std::make_optional<bool>(*bit) : std::nullopt;
  }

  bool advance_le32(int bits) {
    CHECK(bits <= bits_in_word);
    if (m_offset + bits > m_bit_length) {
      return false;
    }
    m_offset += bits;
    if (m_offset >= bits_in_word) {
      refill_low();
    }
    return true;
  }

  int skip_ones() {
    int count = std::min(m_bit_length - m_offset, std::countl_one(m_current_and_next_word << m_offset));
    m_offset += count;
    if (m_offset == 2 * bits_in_word) {
      refill_both();
      while (m_bit_length >= 2 * bits_in_word && m_current_and_next_word == ~static_cast<DoubleWord>(0)) {
        m_offset = 2 * bits_in_word;
        count += m_offset;
        refill_both();
      }
      m_offset = std::min(m_bit_length, std::countl_one(m_current_and_next_word));
      count += m_offset;
    } else if (m_offset >= bits_in_word) {
      refill_low();
    }
    return count;
  }

  bool has_prefix(td::ConstBitPtr prefix, int length) const {
    if (length > m_bit_length) {
      return false;
    }
    return td::ConstBitPtr{reinterpret_cast<td::uint8 const*>(m_refill_pointer - 2), m_offset}.equals(prefix, length);
  }

 private:
  void refill_low() {
    m_current_and_next_word <<= bits_in_word;
    m_offset -= bits_in_word;
    m_bit_length -= bits_in_word;
    if (m_bit_length > bits_in_word) {
      m_current_and_next_word |= __builtin_bswap32(*(m_refill_pointer++));
    }
  }

  void refill_both() {
    DCHECK(m_offset = 2 * bits_in_word);
    m_offset = 0;
    m_bit_length -= 2 * bits_in_word;
    if (m_bit_length > 0) {
      m_current_and_next_word = __builtin_bswap32(*(m_refill_pointer++));
      m_current_and_next_word <<= bits_in_word;
    }
    if (m_bit_length > bits_in_word) {
      m_current_and_next_word |= __builtin_bswap32(*(m_refill_pointer++));
    }
  }

  DoubleWord m_current_and_next_word;
  Word const* m_refill_pointer;
  int m_bit_length;
  int m_offset = 0;
};

}  // namespace vm
