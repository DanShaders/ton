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
#include <assert.h>

#include <openssl/evp.h>
#include <openssl/sha.h>
#include <openssl/opensslv.h>

#include "td/utils/Slice.h"

namespace digest {

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
struct OpensslEVP_SHA1 {
  using context = SHA_CTX;
  static constexpr auto transform = SHA1_Transform;
  enum { block_size = 64, digest_bytes = 20 };
  static constexpr uint32_t init_st[] { 0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0};
};

struct OpensslEVP_SHA256 {
  using context = SHA256_CTX;
  static constexpr auto transform = SHA256_Transform;
  enum { block_size = 64, digest_bytes = 32 };
  static constexpr uint32_t init_st[] { 0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                              0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
};

struct OpensslEVP_SHA512 {
  using context = SHA512_CTX;
  static constexpr auto transform = SHA512_Transform;
  enum { block_size = 128, digest_bytes = 64 };
  static constexpr uint64_t init_st[] { 0x6a09e667f3bcc908, 0xbb67ae8584caa73b,
    0x3c6ef372fe94f82b, 0xa54ff53a5f1d36f1, 0x510e527fade682d1,
    0x9b05688c2b3e6c1f, 0x1f83d9abfb41bd6b, 0x5be0cd19137e2179};
};
#pragma GCC diagnostic pop

template<size_t Len, typename T>
inline void copy_digest(uint8_t from[Len], uint8_t to[Len])
{
  // copy to local variable to ensure compiler doesn't worry from and to overlap
  uint8_t tmp[Len];
  memcpy(tmp, from, Len);
  for(size_t i = 0; i < Len / sizeof(T); i++)
  {
    T x;
    memcpy(&x, tmp + sizeof(x) * i, sizeof(x));
    if constexpr(sizeof(x) == 4)
      x = td::host_to_big_endian32(x);
    else
      x = td::host_to_big_endian64(x);
    memcpy(to + sizeof(x) * i, &x, sizeof(x));
  }
}

template <typename H, int Max = -1>
class HashCtx { // bounded case (unbounded implementation below)
  static_assert(Max > 0);
  // note that SHA-512 uses a 128-bit length for padding
  static constexpr size_t Min_pad = H::block_size == 64 ? 9 : 17;
  uint8_t buffer[(Max + Min_pad + H::block_size - 1) / H::block_size * H::block_size];
  size_t at = 0;
  public:
  HashCtx() { }
  enum { digest_bytes = H::digest_bytes };
  HashCtx(const void *data, std::size_t len) {
    feed(data, len);
  }
  void reset() {
    at = 0;
  }
  void feed(const void *data, std::size_t len) {
    size_t new_at = at + len; // compute before writing so the compiler doesn't
                              // worry that the memcpy may overwrite this->at
    CHECK(new_at <= Max);
    memcpy(buffer + at, data, len);
    at = new_at;
  }
  void feed(td::Slice slice) {
    feed(slice.data(), slice.size());
  }
  std::size_t extract(unsigned char hash[digest_bytes]) {
    size_t nblocks = (at + Min_pad + H::block_size - 1) / H::block_size;
    buffer[at] = 0x80;
    memset(buffer + at + 1, 0, nblocks * H::block_size - 8 - (at + 1));
    uint64_t be = td::host_to_big_endian64(8 * at);
    memcpy(buffer + nblocks * H::block_size - 8, &be, 8);
    uint8_t state[H::digest_bytes];
    memcpy(state, H::init_st, sizeof(state));
    for(size_t i = 0; i < nblocks; i++)
    {
      // surely OpenSSL won't break ABI compatibility by rearranging fields?
      // [hash]_Transform uses only the state array, which is always at offset 0
      H::transform((typename H::context*)&state, buffer + i * H::block_size);
    }
    copy_digest<H::digest_bytes,
      std::conditional_t<H::block_size == 64, uint32_t, uint64_t>
        >(state, hash);
    return digest_bytes;
  }
  std::size_t extract(td::MutableSlice slice) {
    return extract(slice.ubegin());
  }
  std::string extract() {
    unsigned char hash[digest_bytes];
    extract(hash);
    return std::string((char *)hash, digest_bytes);
  }
};

template <typename H>
class HashCtx<H, -1> { // unbounded case
  uint8_t digest[H::digest_bytes];
  uint8_t buffer[H::block_size];
  uint64_t total;
  void finish() {
    uint64_t nbits = 8 * total;
    buffer[total % sizeof(buffer)] = 0x80;
    constexpr size_t pad_to = H::block_size == 64 ? 56 : 112;
    size_t at = total % sizeof(buffer) + 1;
    if(at > pad_to)
    {
      memset(buffer + at, 0, H::block_size - at);
      H::transform((typename H::context*)&digest, buffer);
      at = 0;
    }
    memset(buffer + at, 0, H::block_size - 8 - at);
    uint64_t be = td::host_to_big_endian64(nbits);
    memcpy(buffer + H::block_size - 8, &be, 8);
    H::transform((typename H::context*)&digest, buffer);
  }
 public:
  enum { digest_bytes = H::digest_bytes };
  HashCtx() {
    reset();
  }
  HashCtx(const void *data, std::size_t len) {
    reset();
    feed(data, len);
  }
  ~HashCtx() {
  }
  void reset() {
    total = 0;
    memcpy(digest, H::init_st, sizeof(digest));
  }
  void feed(const void *data, std::size_t len) {
    const uint8_t* dat = (const uint8_t*)data;
    size_t at = total % sizeof(buffer);
    total += len;
    while(at + len >= sizeof(buffer))
    {
      size_t rem = sizeof(buffer) - at;
      memcpy(buffer + at, dat, rem);
      dat += rem;
      len -= rem;
      H::transform((typename H::context*)&digest, buffer);
      at = 0;
    }
    memcpy(buffer + at, dat, len);
  }
  void feed(td::Slice slice) {
    feed(slice.data(), slice.size());
  }
  std::size_t extract(unsigned char hash[digest_bytes]) {
    finish();
    copy_digest<H::digest_bytes,
      std::conditional_t<H::block_size == 64, uint32_t, uint64_t>
        >(digest, hash);
    return digest_bytes;
  }
  std::size_t extract(td::MutableSlice slice) {
    return extract(slice.ubegin());
  }
  std::string extract() {
    unsigned char hash[digest_bytes];
    extract(hash);
    return std::string((char *)hash, digest_bytes);
  }
};

typedef HashCtx<OpensslEVP_SHA1> SHA1;
typedef HashCtx<OpensslEVP_SHA256> SHA256;
typedef HashCtx<OpensslEVP_SHA512> SHA512;

template <typename T>
std::size_t hash_str(unsigned char buffer[T::digest_bytes], const void *data, std::size_t size) {
  T hasher(data, size);
  return hasher.extract(buffer);
}

template <typename T>
std::size_t hash_two_str(unsigned char buffer[T::digest_bytes], const void *data1, std::size_t size1, const void *data2,
                         std::size_t size2) {
  T hasher(data1, size1);
  hasher.feed(data2, size2);
  return hasher.extract(buffer);
}

template <typename T>
std::string hash_str(const void *data, std::size_t size) {
  T hasher(data, size);
  return hasher.extract();
}

template <typename T>
std::string hash_two_str(const void *data1, std::size_t size1, const void *data2, std::size_t size2) {
  T hasher(data1, size1);
  hasher.feed(data2, size2);
  return hasher.extract();
}
}  // namespace digest
