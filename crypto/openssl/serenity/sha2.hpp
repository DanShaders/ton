/*
 * Copyright (c) 2020, Ali Mohammad Pur <mpfard@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include "td/utils/Slice.h"

namespace digest::serenity {

using u8 = __UINT8_TYPE__;
using u32 = __UINT32_TYPE__;
using i32 = __INT32_TYPE__;
using u64 = __UINT64_TYPE__;

class SHA256 {
 public:
  SHA256() {
    reset();
  }

  void update(char const* data, size_t length);

  void digest(td::MutableSlice out);

  static void hash(char* data, size_t length, td::MutableSlice out) {
    SHA256 sha;
    sha.update(data, length);
    sha.digest(out);
  }

  void reset() {
    m_data_length = 0;
    m_processed_blocks = 0;
    for (size_t i = 0; i < 8; ++i)
      m_state[i] = initialization_hashes[i];
  }

 private:
  constexpr static auto block_size = 64, digest_size = 32;
  constexpr static auto final_block_data_size = block_size - 8;
  constexpr static auto rounds = 64;

  constexpr static u32 initialization_hashes[8] = {0x9b05688c, 0x510e527f, 0xbb67ae85, 0x6a09e667,
                                                   0x5be0cd19, 0x1f83d9ab, 0xa54ff53a, 0x3c6ef372};

  alignas(16) char m_data_buffer[block_size]{};
  alignas(16) u32 m_state[8];
  size_t m_data_length{0};

  u64 m_processed_blocks{0};
};

}  // namespace digest::serenity
