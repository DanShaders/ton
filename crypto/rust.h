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
*/
#pragma once

// Thin C++ facade over the FFI exposed by the `ton-rust-crypto` crate
// (crypto/rust/). Mirrors a deliberately small subset of crypto/Ed25519.h
// and the libsodium ristretto255 calls used by TVM, in preparation for
// replacing OpenSSL ed25519 + libsodium curve25519 with ed25519-zebra +
// curve25519-dalek.
//
// Production paths still go through the existing OpenSSL / libsodium
// implementations; nothing here is wired to consensus or signature paths
// today. This header exists so we can build out tests, run parity, and
// flip individual call sites over deliberately later.
//
// Visibility: only included where TON_USE_RUST_CRYPTO is defined (set by
// the top-level CMake option of the same name).
//
// Error model: passing null pointers or otherwise-malformed arguments to
// the FFI is a programmer error and panics in Rust (caught by the FFI
// wrapper, surfaced as kStatusPanic). Callers should validate lengths and
// non-nullness in this header's wrappers before crossing the boundary; the
// wrappers below already do that for Slice/MutableSlice arguments.

#include <cstddef>
#include <cstdint>

#include "td/utils/Slice.h"
#include "td/utils/Status.h"
#include "td/utils/SharedSlice.h"

namespace ton::crypto {

// Status codes mirror crypto/rust/src/lib.rs. Keep these in sync.
inline constexpr std::uint32_t kStatusOk = 0;
inline constexpr std::uint32_t kStatusVerifyFailed = 2;
inline constexpr std::uint32_t kStatusDecodeFailed = 3;
inline constexpr std::uint32_t kStatusPanic = 0xFFFF'FFFFu;

// Aliases for the fixed-size byte arrays the FFI uses. They make the
// extern "C" signatures self-documenting and let callers pass `&array`
// without a reinterpret_cast.
using Bytes32 = std::uint8_t[32];
using Bytes64 = std::uint8_t[64];

// SigningKey / VerificationKey storage. The Rust types are
// #[derive(Copy, Clone)] with no Drop and assert `Unpin`, so they are
// trivially relocatable: bitwise copy/move is sound and there's no drop
// glue. C++ owns the raw bytes; the Rust FFI reinterpret-casts the same
// pointer back to its own typed view.
//
// Sizes/alignments below track ed25519_zebra::{SigningKey, VerificationKey}
// at the pinned crate version. The Rust crate has matching `const _`
// assertions — a crate bump that grows either struct fails at compile time
// on the Rust side first, then on this side once the dev tries to bump only
// half the pair. Updating both numbers in sync is mandatory.
struct SigningKey {
  alignas(8) std::uint8_t data[288];
};
struct VerificationKey {
  alignas(8) std::uint8_t data[192];
};
static_assert(sizeof(SigningKey::data) == 288);
static_assert(sizeof(VerificationKey::data) == 192);
static_assert(alignof(SigningKey) == 8);
static_assert(alignof(VerificationKey) == 8);

// FFI declarations. The Rust side uses `extern "C"` + `#[no_mangle]` so the
// names are unmangled on both sides.
extern "C" {

struct TonEd25519BatchInput {
  const std::uint8_t* message_ptr;
  std::size_t message_len;
  const std::uint8_t* public_keys_ptr;  // item_count * 32
  const std::uint8_t* signatures_ptr;   // item_count * 64
  std::size_t item_count;
  std::uint8_t* validity_out_ptr;        // item_count bytes; 1=valid, 0=invalid
};

std::uint32_t ton_ed25519_secret_to_public(
    const Bytes32* secret_ptr,
    Bytes32* out_public_ptr);

std::uint32_t ton_ed25519_sign(
    const Bytes32* secret_ptr,
    const std::uint8_t* message_ptr, std::size_t message_len,
    Bytes64* out_signature_ptr);

std::uint32_t ton_ed25519_verify(
    const Bytes32* public_key_ptr,
    const std::uint8_t* message_ptr, std::size_t message_len,
    const Bytes64* signature_ptr);

std::uint32_t ton_ed25519_batch_verify(const TonEd25519BatchInput* batch_ptr);

std::uint32_t ton_x25519_shared_secret_from_ed25519(
    const Bytes32* secret_ptr,
    const Bytes32* peer_public_ptr,
    Bytes32* out_secret_ptr);

std::uint32_t ton_ed25519_signing_key_init(
    SigningKey* storage_ptr,
    const Bytes32* secret_ptr);
std::uint32_t ton_ed25519_signing_key_sign(
    const SigningKey* storage_ptr,
    const std::uint8_t* message_ptr, std::size_t message_len,
    Bytes64* out_signature_ptr);
std::uint32_t ton_ed25519_signing_key_public(
    const SigningKey* storage_ptr,
    Bytes32* out_public_ptr);

std::uint32_t ton_ed25519_verification_key_init(
    VerificationKey* storage_ptr,
    const Bytes32* public_key_ptr);
std::uint32_t ton_ed25519_verification_key_verify(
    const VerificationKey* storage_ptr,
    const std::uint8_t* message_ptr, std::size_t message_len,
    const Bytes64* signature_ptr);

std::uint32_t ton_ristretto255_scalar_mult(
    const Bytes32* scalar_ptr,
    const Bytes32* point_ptr,
    Bytes32* out_ptr);

std::uint32_t ton_ristretto255_scalar_mult_base(
    const Bytes32* scalar_ptr,
    Bytes32* out_ptr);

}  // extern "C"

namespace detail {
// kStatusOk -> OK, anything else -> Status::Error with a brief tag. We do
// not return "invalid argument" because the FFI contract is that null/
// malformed pointers panic in Rust rather than being reported; the C++
// wrappers in this header pre-validate before crossing the boundary.
inline td::Status status_from_code(std::uint32_t code) {
  switch (code) {
    case kStatusOk:
      return td::Status::OK();
    case kStatusVerifyFailed:
      return td::Status::Error("signature verification failed");
    case kStatusDecodeFailed:
      return td::Status::Error("could not decode key/point");
    case kStatusPanic:
      return td::Status::Error("rust crypto FFI panicked");
    default:
      return td::Status::Error(PSLICE() << "rust crypto FFI returned unknown status " << code);
  }
}
}  // namespace detail

// ----- Adapters from td::Slice to fixed-size array pointers --------------

namespace detail {
template <std::size_t N>
[[nodiscard]] inline const std::uint8_t (*as_array_ptr(td::Slice s))[N] {
  return reinterpret_cast<const std::uint8_t(*)[N]>(s.ubegin());
}
template <std::size_t N>
[[nodiscard]] inline std::uint8_t (*as_array_ptr_mut(td::MutableSlice s))[N] {
  return reinterpret_cast<std::uint8_t(*)[N]>(s.ubegin());
}
}  // namespace detail

// ----- Ed25519 ------------------------------------------------------------

[[nodiscard]] inline td::Result<td::SecureString> ed25519_secret_to_public(td::Slice secret) {
  if (secret.size() != 32) {
    return td::Status::Error("ed25519 secret must be 32 bytes");
  }
  td::SecureString out(32);
  auto code = ton_ed25519_secret_to_public(
      detail::as_array_ptr<32>(secret),
      detail::as_array_ptr_mut<32>(out.as_mutable_slice()));
  if (code != kStatusOk) {
    return detail::status_from_code(code);
  }
  return out;
}

[[nodiscard]] inline td::Result<td::SecureString> ed25519_sign(td::Slice secret, td::Slice message) {
  if (secret.size() != 32) {
    return td::Status::Error("ed25519 secret must be 32 bytes");
  }
  td::SecureString out(64);
  auto code = ton_ed25519_sign(
      detail::as_array_ptr<32>(secret),
      message.ubegin(), message.size(),
      detail::as_array_ptr_mut<64>(out.as_mutable_slice()));
  if (code != kStatusOk) {
    return detail::status_from_code(code);
  }
  return out;
}

[[nodiscard]] inline td::Status ed25519_verify(td::Slice public_key, td::Slice message, td::Slice signature) {
  if (public_key.size() != 32) {
    return td::Status::Error("ed25519 public key must be 32 bytes");
  }
  if (signature.size() != 64) {
    return td::Status::Error("ed25519 signature must be 64 bytes");
  }
  return detail::status_from_code(ton_ed25519_verify(
      detail::as_array_ptr<32>(public_key),
      message.ubegin(), message.size(),
      detail::as_array_ptr<64>(signature)));
}

// ----- X25519 (Ed25519-derived shared secret) -----------------------------

[[nodiscard]] inline td::Result<td::SecureString> x25519_shared_secret_from_ed25519(
    td::Slice secret, td::Slice peer_public) {
  if (secret.size() != 32) {
    return td::Status::Error("ed25519 secret must be 32 bytes");
  }
  if (peer_public.size() != 32) {
    return td::Status::Error("ed25519 public key must be 32 bytes");
  }
  td::SecureString out(32);
  auto code = ton_x25519_shared_secret_from_ed25519(
      detail::as_array_ptr<32>(secret),
      detail::as_array_ptr<32>(peer_public),
      detail::as_array_ptr_mut<32>(out.as_mutable_slice()));
  if (code != kStatusOk) {
    return detail::status_from_code(code);
  }
  return out;
}

// ----- Ristretto255 -------------------------------------------------------

[[nodiscard]] inline td::Status ristretto255_scalar_mult(
    td::Slice scalar, td::Slice point, td::MutableSlice out) {
  if (scalar.size() != 32 || point.size() != 32 || out.size() != 32) {
    return td::Status::Error("ristretto255 scalar/point/out must each be 32 bytes");
  }
  return detail::status_from_code(ton_ristretto255_scalar_mult(
      detail::as_array_ptr<32>(scalar),
      detail::as_array_ptr<32>(point),
      detail::as_array_ptr_mut<32>(out)));
}

[[nodiscard]] inline td::Status ristretto255_scalar_mult_base(
    td::Slice scalar, td::MutableSlice out) {
  if (scalar.size() != 32 || out.size() != 32) {
    return td::Status::Error("ristretto255 scalar/out must each be 32 bytes");
  }
  return detail::status_from_code(ton_ristretto255_scalar_mult_base(
      detail::as_array_ptr<32>(scalar),
      detail::as_array_ptr_mut<32>(out)));
}

}  // namespace ton::crypto
