/*
 * Copyright (c) 2020, Ali Mohammad Pur <mpfard@serenityos.org>
 * Copyright (c) 2023, Jelle Raaijmakers <jelle@gmta.nl>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "crypto/openssl/serenity/sha2.hpp"

namespace digest::serenity {

namespace {

using u32x4 = u32 __attribute__((vector_size(16)));
using i32x4 = i32 __attribute__((vector_size(16)));

template <typename T>
T load(void const* ptr) {
  return *reinterpret_cast<T const*>(ptr);
}

template <typename T>
T load_unaligned(void const* ptr) {
  T result;
  memcpy(&result, ptr, sizeof(T));
  return result;
}

template <typename T>
void store(void* ptr, T value) {
  *reinterpret_cast<T*>(ptr) = value;
}

alignas(16) constexpr static u32 round_constants[64]{
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

void transform(char const* data, u32 state[8]) {
  u32x4 states[]{load<u32x4>(&state[0]), load<u32x4>(&state[4])};
  u32x4 old[]{states[0], states[1]};
  u32x4 msgs[4]{};

#pragma GCC unroll(16)
  for (int i = 0; i < 16; ++i) {
    u32x4 msg{};
    if (i < 4) {
      msgs[i] = load_unaligned<u32x4>(data + i * 16);
      msgs[i] = {
          __builtin_bswap32(msgs[i][0]),
          __builtin_bswap32(msgs[i][1]),
          __builtin_bswap32(msgs[i][2]),
          __builtin_bswap32(msgs[i][3]),
      };
      msg = msgs[i] + load<u32x4>(&round_constants[i * 4]);
    } else {
      msgs[(i + 0) % 4] = __builtin_ia32_sha256msg1(msgs[(i + 0) % 4], msgs[(i + 1) % 4]);
      auto tmp = u32x4{msgs[(i + 2) % 4][1], msgs[(i + 2) % 4][2], msgs[(i + 2) % 4][3], msgs[(i + 3) % 4][0]};
      msgs[(i + 0) % 4] += tmp;
      msgs[(i + 0) % 4] = __builtin_ia32_sha256msg2(msgs[(i + 0) % 4], msgs[(i + 3) % 4]);
      msg = msgs[(i + 0) % 4] + load<u32x4>(&round_constants[i * 4]);
    }
    states[1] = __builtin_ia32_sha256rnds2(states[1], states[0], msg);
    msg = u32x4{msg[2], msg[3], 0, 0};
    states[0] = __builtin_ia32_sha256rnds2(states[0], states[1], msg);
  }
  states[0] += old[0];
  states[1] += old[1];

  store(&state[0], states[0]);
  store(&state[4], states[1]);
}

}  // namespace

void SHA256::update(char const* data, size_t length) {
  if (m_data_length != 0) {
    if (m_data_length + length < block_size) {
      std::memcpy(m_data_buffer + m_data_length, data, length);
      m_data_length += length;
      return;
    } else {
      size_t copy_bytes = block_size - m_data_length;
      std::memcpy(m_data_buffer + m_data_length, data, copy_bytes);
      transform(m_data_buffer, m_state);
      ++m_processed_blocks;
      m_data_length = 0;
      data += copy_bytes;
      length -= copy_bytes;
    }
  }

  while (length >= block_size) {
    transform(data, m_state);
    ++m_processed_blocks;
    data += block_size;
    length -= block_size;
  }

  m_data_length = length;
  std::memcpy(m_data_buffer, data, m_data_length);
}

void SHA256::digest(td::MutableSlice out) {
  CHECK(out.size() == digest_size);

  size_t i = m_data_length;

  if (i < final_block_data_size) {
    m_data_buffer[i++] = 0x80;
    while (i < final_block_data_size)
      m_data_buffer[i++] = 0x00;
  } else {
    // First, complete a block with some padding.
    m_data_buffer[i++] = 0x80;
    while (i < block_size)
      m_data_buffer[i++] = 0x00;
    transform(m_data_buffer, m_state);

    // Then start another block with BlockSize - 8 bytes of zeros
    std::memset(m_data_buffer, 0, final_block_data_size);
  }

  // append total message length
  u64 bit_length = (m_processed_blocks * block_size + m_data_length) * 8;
  for (int i = 0; i < 8; ++i)
    m_data_buffer[block_size - 1 - i] = bit_length >> (i * 8) & 0xff;

  transform(m_data_buffer, m_state);

  int permutation[] = {3, 2, 7, 6, 1, 0, 5, 4};

  for (int j = 0; j < 8; ++j)
    for (i = 0; i < 4; ++i)
      out[i + 4 * j] = (m_state[permutation[j]] >> (24 - i * 8)) & 0xff;
}

}  // namespace digest::serenity
