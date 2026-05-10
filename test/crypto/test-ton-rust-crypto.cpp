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

// Parity tests for crypto/rust against the existing OpenSSL Ed25519 path
// and the libsodium ristretto255 path. These verify that the Rust FFI is a
// drop-in replacement at the bytes-on-the-wire level for the call sites we
// eventually want to migrate.
//
// Production code paths are NOT touched by enabling the Rust crate — these
// tests are the *only* consumer of crypto/ton-rust-crypto.h for now.

#include <array>
#include <cstring>
#include <string>
#include <vector>

#include <sodium.h>

#include "crypto/Ed25519.h"
#include "crypto/rust.h"

#include "td/utils/Slice.h"
#include "td/utils/Status.h"
#include "td/utils/logging.h"
#include "td/utils/tests.h"

namespace {

namespace rc = ton::crypto;

// RFC 8032 test vector 2 (TEST 2 in §7.1) — a non-empty message so we
// exercise more of the signing path than vector 1's empty input.
constexpr std::array<td::uint8, 32> kRfc8032Secret2 = {
    0x4c, 0xcd, 0x08, 0x9b, 0x28, 0xff, 0x96, 0xda, 0x9d, 0xb6, 0xc3, 0x46, 0xec, 0x11, 0x4e, 0x0f,
    0x5b, 0x8a, 0x31, 0x9f, 0x35, 0xab, 0xa6, 0x24, 0xda, 0x8c, 0xf6, 0xed, 0x4f, 0xb8, 0xa6, 0xfb,
};
constexpr std::array<td::uint8, 32> kRfc8032Public2 = {
    0x3d, 0x40, 0x17, 0xc3, 0xe8, 0x43, 0x89, 0x5a, 0x92, 0xb7, 0x0a, 0xa7, 0x4d, 0x1b, 0x7e, 0xbc,
    0x9c, 0x98, 0x2c, 0xcf, 0x2e, 0xc4, 0x96, 0x8c, 0xc0, 0xcd, 0x55, 0xf1, 0x2a, 0xf4, 0x66, 0x0c,
};
constexpr std::array<td::uint8, 1> kRfc8032Message2 = {0x72};
constexpr std::array<td::uint8, 64> kRfc8032Signature2 = {
    0x92, 0xa0, 0x09, 0xa9, 0xf0, 0xd4, 0xca, 0xb8, 0x72, 0x0e, 0x82, 0x0b, 0x5f, 0x64, 0x25, 0x40,
    0xa2, 0xb2, 0x7b, 0x54, 0x16, 0x50, 0x3f, 0x8f, 0xb3, 0x76, 0x22, 0x23, 0xeb, 0xdb, 0x69, 0xda,
    0x08, 0x5a, 0xc1, 0xe4, 0x3e, 0x15, 0x99, 0x6e, 0x45, 0x8f, 0x36, 0x13, 0xd0, 0xf1, 0x1d, 0x8c,
    0x38, 0x7b, 0x2e, 0xae, 0xb4, 0x30, 0x2a, 0xee, 0xb0, 0x0d, 0x29, 0x16, 0x12, 0xbb, 0x0c, 0x00,
};

td::Slice as_slice(const std::array<td::uint8, 32>& arr) {
  return td::Slice(arr.data(), arr.size());
}
td::Slice as_slice(const std::array<td::uint8, 64>& arr) {
  return td::Slice(arr.data(), arr.size());
}
td::Slice as_slice(const std::array<td::uint8, 1>& arr) {
  return td::Slice(arr.data(), arr.size());
}

}  // namespace

// ---- Ed25519 -------------------------------------------------------------

TEST(RustCrypto, ed25519_secret_to_public_matches_rfc8032) {
  auto pub = rc::ed25519_secret_to_public(as_slice(kRfc8032Secret2)).move_as_ok();
  ASSERT_EQ(td::Slice(pub).str(), as_slice(kRfc8032Public2).str());
}

TEST(RustCrypto, ed25519_sign_matches_rfc8032) {
  auto sig = rc::ed25519_sign(
                 as_slice(kRfc8032Secret2), as_slice(kRfc8032Message2))
                 .move_as_ok();
  ASSERT_EQ(td::Slice(sig).str(), as_slice(kRfc8032Signature2).str());
}

TEST(RustCrypto, ed25519_verify_rfc8032_signature) {
  rc::ed25519_verify(as_slice(kRfc8032Public2),
                       as_slice(kRfc8032Message2),
                       as_slice(kRfc8032Signature2))
      .ensure();
}

TEST(RustCrypto, ed25519_verify_rejects_tampered_signature) {
  std::array<td::uint8, 64> sig = kRfc8032Signature2;
  sig[0] ^= 0x01;
  auto status = rc::ed25519_verify(
      as_slice(kRfc8032Public2), as_slice(kRfc8032Message2), as_slice(sig));
  ASSERT_TRUE(status.is_error());
}

// Rust derives the same public key OpenSSL derives. This is the most
// load-bearing parity check for a future call-site migration; if this
// flips between backends we'd silently break key identity.
TEST(RustCrypto, parity_ed25519_secret_to_public_matches_openssl) {
  // Cycle through a handful of secrets — fixed deterministic ones plus the
  // RFC8032 vector — to broaden coverage without bringing an RNG in here.
  std::vector<std::array<td::uint8, 32>> secrets = {
      kRfc8032Secret2,
      {0},  // all-zero secret (still a valid scalar source)
  };
  for (int i = 0; i < 8; ++i) {
    std::array<td::uint8, 32> s{};
    for (size_t j = 0; j < s.size(); ++j) {
      s[j] = static_cast<td::uint8>((i * 31 + j * 7) & 0xff);
    }
    secrets.push_back(s);
  }

  for (const auto& secret : secrets) {
    auto rust_pub = rc::ed25519_secret_to_public(as_slice(secret)).move_as_ok();

    td::Ed25519::PrivateKey ossl_priv{td::SecureString(as_slice(secret))};
    auto ossl_pub = ossl_priv.get_public_key().move_as_ok().as_octet_string();

    ASSERT_EQ(td::Slice(rust_pub).str(), td::Slice(ossl_pub).str());
  }
}

// Rust signatures verify under OpenSSL and vice versa. Ed25519 is
// deterministic, so signatures should be byte-identical too.
TEST(RustCrypto, parity_ed25519_sign_matches_openssl) {
  std::array<td::uint8, 32> secret{};
  for (size_t i = 0; i < secret.size(); ++i) secret[i] = static_cast<td::uint8>(i + 1);

  std::vector<td::Slice> messages = {
      td::Slice(""),
      td::Slice("a"),
      td::Slice("the quick brown fox jumps over the lazy dog"),
      // Long message: 256 bytes of pattern.
      td::Slice(),  // placeholder, filled below
  };
  std::string long_msg(256, 'x');
  for (size_t i = 0; i < long_msg.size(); ++i) long_msg[i] = static_cast<char>(i ^ 0x5a);
  messages.back() = long_msg;

  td::Ed25519::PrivateKey ossl_priv{td::SecureString(as_slice(secret))};
  auto ossl_pub = ossl_priv.get_public_key().move_as_ok();

  for (const auto& msg : messages) {
    auto rust_sig = rc::ed25519_sign(as_slice(secret), msg).move_as_ok();
    auto ossl_sig = ossl_priv.sign(msg).move_as_ok();
    ASSERT_EQ(td::Slice(rust_sig).str(), td::Slice(ossl_sig).str());

    // Cross-verify both directions.
    rc::ed25519_verify(
        td::Slice(ossl_pub.as_octet_string()), msg, td::Slice(rust_sig))
        .ensure();
    ossl_pub.verify_signature(msg, td::Slice(ossl_sig)).ensure();
  }
}

// ---- X25519 (Ed25519-derived shared secret) ------------------------------

TEST(RustCrypto, parity_shared_secret_matches_openssl) {
  std::array<td::uint8, 32> alice_secret{};
  std::array<td::uint8, 32> bob_secret{};
  for (size_t i = 0; i < 32; ++i) {
    alice_secret[i] = static_cast<td::uint8>((i * 13 + 1) & 0xff);
    bob_secret[i] = static_cast<td::uint8>((i * 17 + 200) & 0xff);
  }

  td::Ed25519::PrivateKey alice_ossl{td::SecureString(as_slice(alice_secret))};
  td::Ed25519::PrivateKey bob_ossl{td::SecureString(as_slice(bob_secret))};
  auto alice_pub_ossl = alice_ossl.get_public_key().move_as_ok();
  auto bob_pub_ossl = bob_ossl.get_public_key().move_as_ok();

  // OpenSSL: Alice does ECDH with Bob's pubkey.
  auto ossl_secret = td::Ed25519::compute_shared_secret(
      bob_pub_ossl, alice_ossl).move_as_ok();

  // Rust: Alice does the same.
  auto rust_secret = rc::x25519_shared_secret_from_ed25519(
      as_slice(alice_secret),
      td::Slice(bob_pub_ossl.as_octet_string()))
      .move_as_ok();

  ASSERT_EQ(td::Slice(ossl_secret).str(), td::Slice(rust_secret).str());

  // Symmetry: Bob reaches the same secret using Alice's pubkey.
  auto rust_secret_other = rc::x25519_shared_secret_from_ed25519(
      as_slice(bob_secret),
      td::Slice(alice_pub_ossl.as_octet_string()))
      .move_as_ok();
  ASSERT_EQ(td::Slice(rust_secret).str(), td::Slice(rust_secret_other).str());
}

// ---- Ristretto255 -------------------------------------------------------

// libsodium's `crypto_scalarmult_ristretto255*` assumes a canonical scalar
// (< L). Its internal `ge25519_scalarmult` 4-bit recoding produces
// out-of-range chunks for `byte[31] > 0x10`, which silently corrupts the
// result. dalek's `Scalar::from_bytes_mod_order` correctly reduces mod L.
// So the two libraries disagree on non-canonical inputs by design — we
// constrain the parity inputs here to canonical scalars only. (A future
// migration of TVM's RIST255_MUL opcode will need to make a deliberate
// call about which behavior to preserve on non-canonical inputs, since
// it's consensus-observable.)
[[nodiscard]] inline std::array<td::uint8, 32> make_canonical_scalar(int seed) {
  std::array<td::uint8, 32> s{};
  for (size_t i = 0; i < s.size(); ++i) {
    s[i] = static_cast<td::uint8>((seed * 31 + i * 7) & 0xff);
  }
  // Force byte[31] strictly below L's top byte (0x10) — anything <= 0x0F is
  // unconditionally < L regardless of the lower bytes, so this is sufficient
  // without having to do a full canonical comparison.
  s[31] &= 0x0F;
  return s;
}

TEST(RustCrypto, parity_ristretto255_scalar_mult_base_matches_libsodium) {
  CHECK(sodium_init() != -1);

  std::vector<std::array<td::uint8, 32>> scalars;
  for (int i = 1; i <= 5; ++i) {
    scalars.push_back(make_canonical_scalar(i));
  }
  // Add a couple of explicit small canonical scalars too.
  std::array<td::uint8, 32> small{}; small[0] = 0x01; scalars.push_back(small);
  small[0] = 0x42; scalars.push_back(small);

  for (const auto& scalar : scalars) {
    std::array<td::uint8, 32> sodium_out{};
    if (crypto_scalarmult_ristretto255_base(sodium_out.data(), scalar.data()) != 0) {
      // libsodium rejected (e.g., zero scalar); skip — Rust should also reject.
      std::array<td::uint8, 32> rust_out{};
      auto status = rc::ristretto255_scalar_mult_base(
          as_slice(scalar), td::MutableSlice(reinterpret_cast<char*>(rust_out.data()), 32));
      ASSERT_TRUE(status.is_error());
      continue;
    }
    std::array<td::uint8, 32> rust_out{};
    rc::ristretto255_scalar_mult_base(
        as_slice(scalar),
        td::MutableSlice(reinterpret_cast<char*>(rust_out.data()), 32))
        .ensure();
    ASSERT_EQ(td::Slice(sodium_out.data(), 32).str(),
              td::Slice(rust_out.data(), 32).str());
  }
}

TEST(RustCrypto, parity_ristretto255_scalar_mult_matches_libsodium) {
  CHECK(sodium_init() != -1);

  // Build a random-but-fixed valid point by computing 7*G via libsodium.
  std::array<td::uint8, 32> seven_scalar{};
  seven_scalar[0] = 7;
  std::array<td::uint8, 32> point{};
  CHECK(crypto_scalarmult_ristretto255_base(point.data(), seven_scalar.data()) == 0);

  // Use canonical scalars only — see the comment on make_canonical_scalar
  // above for why libsodium and dalek diverge on non-canonical inputs.
  for (int seed = 1; seed <= 5; ++seed) {
    auto scalar = make_canonical_scalar(seed);
    std::array<td::uint8, 32> sodium_out{};
    CHECK(crypto_scalarmult_ristretto255(sodium_out.data(), scalar.data(), point.data()) == 0);

    std::array<td::uint8, 32> rust_out{};
    rc::ristretto255_scalar_mult(
        as_slice(scalar), as_slice(point),
        td::MutableSlice(reinterpret_cast<char*>(rust_out.data()), 32))
        .ensure();
    ASSERT_EQ(td::Slice(sodium_out.data(), 32).str(),
              td::Slice(rust_out.data(), 32).str());
  }
}

// ---- Prepared SigningKey / VerificationKey ------------------------------

TEST(RustCrypto, prepared_signing_key_round_trip) {
  // Caller-owned storage — no allocation, just stack bytes.
  ton::crypto::SigningKey sk{};
  std::array<td::uint8, 32> secret{};
  for (size_t i = 0; i < secret.size(); ++i) secret[i] = static_cast<td::uint8>(i + 17);

  ASSERT_EQ(ton::crypto::ton_ed25519_signing_key_init(
                &sk, reinterpret_cast<const ton::crypto::Bytes32*>(secret.data())),
            ton::crypto::kStatusOk);

  // Public key from prepared signer must match the secret-only path.
  std::array<td::uint8, 32> pub_prepared{};
  ASSERT_EQ(ton::crypto::ton_ed25519_signing_key_public(
                &sk, reinterpret_cast<ton::crypto::Bytes32*>(pub_prepared.data())),
            ton::crypto::kStatusOk);
  auto pub_oneshot = rc::ed25519_secret_to_public(as_slice(secret)).move_as_ok();
  ASSERT_EQ(td::Slice(pub_prepared.data(), 32).str(), td::Slice(pub_oneshot).str());

  // Sign via prepared signer; signature must be byte-equal to the one-shot
  // path (ed25519 is deterministic).
  std::string msg = "ton prepared signer message";
  std::array<td::uint8, 64> sig_prepared{};
  ASSERT_EQ(ton::crypto::ton_ed25519_signing_key_sign(
                &sk,
                reinterpret_cast<const td::uint8*>(msg.data()), msg.size(),
                reinterpret_cast<ton::crypto::Bytes64*>(sig_prepared.data())),
            ton::crypto::kStatusOk);
  auto sig_oneshot = rc::ed25519_sign(as_slice(secret), td::Slice(msg)).move_as_ok();
  ASSERT_EQ(td::Slice(sig_prepared.data(), 64).str(), td::Slice(sig_oneshot).str());
}

TEST(RustCrypto, prepared_verification_key_verifies_signature) {
  std::array<td::uint8, 32> secret{};
  for (size_t i = 0; i < secret.size(); ++i) secret[i] = static_cast<td::uint8>(i * 3 + 7);

  auto pub = rc::ed25519_secret_to_public(as_slice(secret)).move_as_ok();
  std::string msg = "verify via prepared VerificationKey";
  auto sig = rc::ed25519_sign(as_slice(secret), td::Slice(msg)).move_as_ok();

  ton::crypto::VerificationKey vk{};
  ASSERT_EQ(ton::crypto::ton_ed25519_verification_key_init(
                &vk, reinterpret_cast<const ton::crypto::Bytes32*>(pub.data())),
            ton::crypto::kStatusOk);
  ASSERT_EQ(ton::crypto::ton_ed25519_verification_key_verify(
                &vk,
                reinterpret_cast<const td::uint8*>(msg.data()), msg.size(),
                reinterpret_cast<const ton::crypto::Bytes64*>(sig.data())),
            ton::crypto::kStatusOk);

  // Tampered signature → STATUS_VERIFY_FAILED.
  std::array<td::uint8, 64> tampered{};
  std::memcpy(tampered.data(), sig.data(), 64);
  tampered[0] ^= 0xff;
  ASSERT_EQ(ton::crypto::ton_ed25519_verification_key_verify(
                &vk,
                reinterpret_cast<const td::uint8*>(msg.data()), msg.size(),
                reinterpret_cast<const ton::crypto::Bytes64*>(tampered.data())),
            ton::crypto::kStatusVerifyFailed);
}

// ---- Batch verify --------------------------------------------------------

TEST(RustCrypto, ed25519_batch_verify_all_valid) {
  // Generate a batch of (pubkey, signature) pairs over the same message.
  constexpr int kBatch = 8;
  std::vector<std::array<td::uint8, 32>> secrets(kBatch);
  std::vector<std::array<td::uint8, 32>> publics(kBatch);
  std::vector<std::array<td::uint8, 64>> sigs(kBatch);
  std::string msg = "ton consensus simplex vote payload";

  for (int i = 0; i < kBatch; ++i) {
    for (size_t j = 0; j < 32; ++j) {
      secrets[i][j] = static_cast<td::uint8>((i * 41 + j * 13 + 1) & 0xff);
    }
    auto p = rc::ed25519_secret_to_public(as_slice(secrets[i])).move_as_ok();
    std::memcpy(publics[i].data(), p.data(), 32);
    auto s = rc::ed25519_sign(as_slice(secrets[i]), td::Slice(msg)).move_as_ok();
    std::memcpy(sigs[i].data(), s.data(), 64);
  }

  std::vector<td::uint8> flat_pubs(kBatch * 32);
  std::vector<td::uint8> flat_sigs(kBatch * 64);
  for (int i = 0; i < kBatch; ++i) {
    std::memcpy(flat_pubs.data() + i * 32, publics[i].data(), 32);
    std::memcpy(flat_sigs.data() + i * 64, sigs[i].data(), 64);
  }
  std::vector<td::uint8> validity(kBatch, 0);

  rc::TonEd25519BatchInput input{};
  input.message_ptr = reinterpret_cast<const td::uint8*>(msg.data());
  input.message_len = msg.size();
  input.public_keys_ptr = flat_pubs.data();
  input.signatures_ptr = flat_sigs.data();
  input.item_count = kBatch;
  input.validity_out_ptr = validity.data();

  auto status = rc::ton_ed25519_batch_verify(&input);
  ASSERT_EQ(status, rc::kStatusOk);
  for (auto v : validity) ASSERT_EQ(static_cast<int>(v), 1);
}

TEST(RustCrypto, ed25519_batch_verify_pinpoints_invalid) {
  constexpr int kBatch = 4;
  std::vector<std::array<td::uint8, 32>> secrets(kBatch);
  std::vector<std::array<td::uint8, 32>> publics(kBatch);
  std::vector<std::array<td::uint8, 64>> sigs(kBatch);
  std::string msg = "another vote";

  for (int i = 0; i < kBatch; ++i) {
    for (size_t j = 0; j < 32; ++j) secrets[i][j] = static_cast<td::uint8>(i * 11 + j);
    auto p = rc::ed25519_secret_to_public(as_slice(secrets[i])).move_as_ok();
    std::memcpy(publics[i].data(), p.data(), 32);
    auto s = rc::ed25519_sign(as_slice(secrets[i]), td::Slice(msg)).move_as_ok();
    std::memcpy(sigs[i].data(), s.data(), 64);
  }
  // Tamper with index 2.
  sigs[2][0] ^= 0xff;

  std::vector<td::uint8> flat_pubs(kBatch * 32);
  std::vector<td::uint8> flat_sigs(kBatch * 64);
  for (int i = 0; i < kBatch; ++i) {
    std::memcpy(flat_pubs.data() + i * 32, publics[i].data(), 32);
    std::memcpy(flat_sigs.data() + i * 64, sigs[i].data(), 64);
  }
  std::vector<td::uint8> validity(kBatch, 0);

  rc::TonEd25519BatchInput input{};
  input.message_ptr = reinterpret_cast<const td::uint8*>(msg.data());
  input.message_len = msg.size();
  input.public_keys_ptr = flat_pubs.data();
  input.signatures_ptr = flat_sigs.data();
  input.item_count = kBatch;
  input.validity_out_ptr = validity.data();

  auto status = rc::ton_ed25519_batch_verify(&input);
  ASSERT_EQ(status, rc::kStatusVerifyFailed);
  ASSERT_EQ(static_cast<int>(validity[0]), 1);
  ASSERT_EQ(static_cast<int>(validity[1]), 1);
  ASSERT_EQ(static_cast<int>(validity[2]), 0);
  ASSERT_EQ(static_cast<int>(validity[3]), 1);
}
