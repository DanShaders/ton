// FFI surface that mirrors TON's C++ `crypto/Ed25519.h` and the libsodium
// ristretto255 calls used by TVM, implemented on top of the ed25519-zebra
// and curve25519-dalek crates.
//
// Conventions:
//   - Every entry point is `extern "C"` and returns a u32 status code.
//   - Output buffers are caller-allocated; lengths are fixed and documented.
//   - Null pointers and integer overflows on length math are programmer
//     errors and panic; the FFI shim catches the panic via `catch_unwind`
//     and surfaces it as STATUS_PANIC. There is no STATUS_INVALID_ARGUMENT;
//     the C++ wrapper (crypto/rust.h) is expected to validate lengths and
//     non-nullness before crossing the boundary.
//
// The C++ side lives at crypto/rust.h.

#![deny(unsafe_op_in_unsafe_fn)]
#![allow(clippy::missing_safety_doc)]

use std::convert::TryInto;
use std::panic::{catch_unwind, AssertUnwindSafe};
use std::ptr::NonNull;
use std::slice;

use curve25519_dalek::edwards::CompressedEdwardsY;
use curve25519_dalek::ristretto::CompressedRistretto;
use curve25519_dalek::scalar::Scalar;
use ed25519_zebra::{batch, Signature, SigningKey, VerificationKey, VerificationKeyBytes};
use rand::thread_rng;
use sha2::{Digest, Sha512};
use std::mem::{align_of, size_of};

// ---------------------------------------------------------------------------
// Status codes (kept in sync with crypto/rust.h).
// ---------------------------------------------------------------------------

pub const STATUS_OK: u32 = 0;
pub const STATUS_VERIFY_FAILED: u32 = 2;
pub const STATUS_DECODE_FAILED: u32 = 3;
pub const STATUS_PANIC: u32 = 0xFFFF_FFFF;

// ---------------------------------------------------------------------------
// Slice helpers. Null pointers panic; the FFI guard converts that to
// STATUS_PANIC. Length-zero with a null pointer is OK (synthesizes an empty
// slice) — that's a legitimate "empty message" case.
// ---------------------------------------------------------------------------

/// SAFETY: caller guarantees `(ptr, len)` describe a valid readable region
/// for the duration of the call when `len > 0`.
unsafe fn make_slice<'a>(ptr: *const u8, len: usize) -> &'a [u8] {
    if len == 0 {
        return &[];
    }
    assert!(!ptr.is_null(), "FFI input pointer is null with non-zero length");
    unsafe { slice::from_raw_parts(ptr, len) }
}

/// SAFETY: caller guarantees `(ptr, len)` describe a valid writable region
/// for the duration of the call when `len > 0`.
unsafe fn make_slice_mut<'a>(ptr: *mut u8, len: usize) -> &'a mut [u8] {
    if len == 0 {
        // Synthesize a non-null dangling pointer so the &mut [u8] is sound
        // (we never dereference it because len == 0).
        return unsafe {
            slice::from_raw_parts_mut(NonNull::<u8>::dangling().as_ptr(), 0)
        };
    }
    assert!(!ptr.is_null(), "FFI output pointer is null with non-zero length");
    unsafe { slice::from_raw_parts_mut(ptr, len) }
}

/// Read a fixed-size byte array through a typed pointer. `[u8; N]` has
/// alignment 1, so we only need a null check.
unsafe fn read_array<const N: usize>(ptr: *const [u8; N]) -> [u8; N] {
    assert!(!ptr.is_null(), "FFI input array pointer is null");
    unsafe { *ptr }
}

/// Write a fixed-size byte array through a typed pointer.
unsafe fn write_array<const N: usize>(ptr: *mut [u8; N], value: [u8; N]) {
    assert!(!ptr.is_null(), "FFI output array pointer is null");
    unsafe { *ptr = value };
}

fn guard<F: FnOnce() -> u32>(f: F) -> u32 {
    catch_unwind(AssertUnwindSafe(f)).unwrap_or(STATUS_PANIC)
}

// ---------------------------------------------------------------------------
// Ed25519
// ---------------------------------------------------------------------------

/// Derive the 32-byte Ed25519 public key from a 32-byte secret seed.
#[no_mangle]
pub extern "C" fn ton_ed25519_secret_to_public(
    secret_ptr: *const [u8; 32],
    out_public_ptr: *mut [u8; 32],
) -> u32 {
    guard(|| {
        let secret = unsafe { read_array(secret_ptr) };
        let signing_key = SigningKey::from(secret);
        let pk_bytes: [u8; 32] = VerificationKeyBytes::from(&signing_key).into();
        unsafe { write_array(out_public_ptr, pk_bytes) };
        STATUS_OK
    })
}

/// Sign a message with a 32-byte Ed25519 secret seed. Output is 64 bytes.
#[no_mangle]
pub extern "C" fn ton_ed25519_sign(
    secret_ptr: *const [u8; 32],
    message_ptr: *const u8,
    message_len: usize,
    out_signature_ptr: *mut [u8; 64],
) -> u32 {
    guard(|| {
        let secret = unsafe { read_array(secret_ptr) };
        let message = unsafe { make_slice(message_ptr, message_len) };
        let signing_key = SigningKey::from(secret);
        let sig: [u8; 64] = signing_key.sign(message).into();
        unsafe { write_array(out_signature_ptr, sig) };
        STATUS_OK
    })
}

/// Verify a 64-byte Ed25519 signature against a 32-byte public key.
/// Returns STATUS_OK on valid, STATUS_VERIFY_FAILED on invalid,
/// STATUS_DECODE_FAILED if the public key fails canonical decoding.
#[no_mangle]
pub extern "C" fn ton_ed25519_verify(
    public_key_ptr: *const [u8; 32],
    message_ptr: *const u8,
    message_len: usize,
    signature_ptr: *const [u8; 64],
) -> u32 {
    guard(|| {
        let pk_bytes = unsafe { read_array(public_key_ptr) };
        let sig_bytes = unsafe { read_array(signature_ptr) };
        let message = unsafe { make_slice(message_ptr, message_len) };
        let vk = match VerificationKey::try_from(VerificationKeyBytes::from(pk_bytes)) {
            Ok(v) => v,
            Err(_) => return STATUS_DECODE_FAILED,
        };
        let signature = Signature::from(sig_bytes);
        match vk.verify(&signature, message) {
            Ok(()) => STATUS_OK,
            Err(_) => STATUS_VERIFY_FAILED,
        }
    })
}

// Batch verification kept structurally compatible with the prior
// ton-consensus-ed25519-batch-rs PoC. All signatures share one `message`
// (matches simplex certificate use case where each validator signs the same
// vote payload).

#[repr(C)]
pub struct TonEd25519BatchInput {
    pub message_ptr: *const u8,
    pub message_len: usize,
    pub public_keys_ptr: *const u8, // item_count * 32
    pub signatures_ptr: *const u8,  // item_count * 64
    pub item_count: usize,
    pub validity_out_ptr: *mut u8,  // item_count bytes; 1 = valid, 0 = invalid
}

/// Verify a batch of Ed25519 signatures, all over the same message. Falls
/// back to per-item verification on batch failure to populate `validity_out`.
/// Returns STATUS_OK iff every signature was valid, STATUS_VERIFY_FAILED
/// if any signature was invalid.
#[no_mangle]
pub extern "C" fn ton_ed25519_batch_verify(
    batch_ptr: *const TonEd25519BatchInput,
) -> u32 {
    guard(|| {
        assert!(!batch_ptr.is_null(), "FFI batch input pointer is null");
        let batch = unsafe { &*batch_ptr };
        if batch.item_count == 0 {
            return STATUS_OK;
        }
        let pubkey_bytes_total = batch
            .item_count
            .checked_mul(32)
            .expect("item_count * 32 overflows usize");
        let sig_bytes_total = batch
            .item_count
            .checked_mul(64)
            .expect("item_count * 64 overflows usize");

        let message = unsafe { make_slice(batch.message_ptr, batch.message_len) };
        let public_keys = unsafe { make_slice(batch.public_keys_ptr, pubkey_bytes_total) };
        let signatures = unsafe { make_slice(batch.signatures_ptr, sig_bytes_total) };
        let validity = unsafe { make_slice_mut(batch.validity_out_ptr, batch.item_count) };

        validity.fill(0);

        let mut verifier = batch::Verifier::new();
        let mut items = Vec::with_capacity(batch.item_count);
        for idx in 0..batch.item_count {
            let pk: [u8; 32] = public_keys[idx * 32..idx * 32 + 32]
                .try_into()
                .expect("32-byte slice");
            let sig: [u8; 64] = signatures[idx * 64..idx * 64 + 64]
                .try_into()
                .expect("64-byte slice");
            let item = batch::Item::from((
                VerificationKeyBytes::from(pk),
                Signature::from(sig),
                message,
            ));
            verifier.queue(item.clone());
            items.push(item);
        }

        if verifier.verify(thread_rng()).is_ok() {
            validity.fill(1);
            return STATUS_OK;
        }

        // Batch failed; pinpoint which entries are invalid.
        let mut all_valid = true;
        for (slot, item) in validity.iter_mut().zip(items.into_iter()) {
            let ok = item.verify_single().is_ok();
            *slot = u8::from(ok);
            all_valid &= ok;
        }
        if all_valid {
            STATUS_OK
        } else {
            STATUS_VERIFY_FAILED
        }
    })
}

// ---------------------------------------------------------------------------
// Prepared signers/verifiers — caller-owned storage.
//
// `SigningKey` and `VerificationKey` are both #[derive(Copy, Clone)] with no
// Drop impl, so they're trivially copyable at the byte level. We expose
// their layout via `TON_ED25519_*_SIZE` / `TON_ED25519_*_ALIGN` so the C++
// side can allocate inline storage (a `std::byte[N]` aligned by `alignas`),
// avoiding a heap allocation on hot paths. The C++ wrapper checks the
// runtime size against its compile-time buffer size at first init.
//
// Why caller-owned? Caching the prepared form across many sign/verify calls
// is the whole point: SigningKey::from(seed) does SHA-512 + a basepoint
// scalar mult, and VerificationKey::try_from(bytes) does Edwards point
// decompression + on-curve check. With the storage owned by C++, neither
// us-side allocation nor a separate FFI free() is needed; the C++ object's
// destructor zeroes the buffer for secrets hygiene.
// ---------------------------------------------------------------------------

// Compile-time guarantees that the prepared types are POD-shaped enough to
// hand off to C++ as raw bytes:
//   - `Copy`        : implies no Drop and bitwise-copy is sound
//   - `Unpin`       : the closest Rust analog to C++ "trivially relocatable"
//                     — confirms no self-referential / pinned-pointer fields
//                     and so a memmove is a valid move
//   - `!needs_drop` : belt-and-suspenders; no drop glue exists at all.
//   - exact size / align match the numbers C++ has in `crypto/rust.h`. Each
//     language asserts its own expectation independently, so a crate bump
//     that grows either struct fails at compile time on whichever side is
//     stale (and forces an update on the other when paired).
//
// If a future ed25519-zebra version adds anything that breaks any of these,
// this `const _` block fails to compile and the FFI surface needs revisiting.
const _: () = {
    const fn assert_copy_unpin<T: Copy + Unpin>() {}
    assert_copy_unpin::<SigningKey>();
    assert_copy_unpin::<VerificationKey>();
    assert!(!std::mem::needs_drop::<SigningKey>());
    assert!(!std::mem::needs_drop::<VerificationKey>());
    assert!(size_of::<SigningKey>() == 288);
    assert!(size_of::<VerificationKey>() == 192);
    assert!(align_of::<SigningKey>() == 8);
    assert!(align_of::<VerificationKey>() == 8);
};

unsafe fn check_aligned<T>(ptr: *const T) {
    assert!(!ptr.is_null(), "FFI storage pointer is null");
    assert_eq!(
        ptr as usize % align_of::<T>(),
        0,
        "FFI storage pointer is misaligned"
    );
}

/// Initialize a SigningKey at `storage_ptr` from a 32-byte secret seed.
/// `storage_ptr` must point to at least TON_ED25519_SIGNING_KEY_SIZE bytes
/// aligned to TON_ED25519_SIGNING_KEY_ALIGN. The contents of the buffer
/// before init are ignored (we overwrite, not drop — SigningKey has no Drop).
#[no_mangle]
pub extern "C" fn ton_ed25519_signing_key_init(
    storage_ptr: *mut SigningKey,
    secret_ptr: *const [u8; 32],
) -> u32 {
    guard(|| {
        let secret = unsafe { read_array(secret_ptr) };
        unsafe { check_aligned(storage_ptr) };
        // ptr::write — no destructor runs (SigningKey: Copy, no Drop), and
        // the prior buffer contents (whatever C++ had there) are simply
        // overwritten, no double-drop concerns.
        unsafe { storage_ptr.write(SigningKey::from(secret)) };
        STATUS_OK
    })
}

/// Sign with a previously-initialized SigningKey at `storage_ptr`.
#[no_mangle]
pub extern "C" fn ton_ed25519_signing_key_sign(
    storage_ptr: *const SigningKey,
    message_ptr: *const u8,
    message_len: usize,
    out_signature_ptr: *mut [u8; 64],
) -> u32 {
    guard(|| {
        unsafe { check_aligned(storage_ptr) };
        let signing_key: &SigningKey = unsafe { &*storage_ptr };
        let message = unsafe { make_slice(message_ptr, message_len) };
        let sig: [u8; 64] = signing_key.sign(message).into();
        unsafe { write_array(out_signature_ptr, sig) };
        STATUS_OK
    })
}

/// Read the cached public key out of a previously-initialized SigningKey.
/// This is just a copy of the `vk` field — no expansion, very cheap.
#[no_mangle]
pub extern "C" fn ton_ed25519_signing_key_public(
    storage_ptr: *const SigningKey,
    out_public_ptr: *mut [u8; 32],
) -> u32 {
    guard(|| {
        unsafe { check_aligned(storage_ptr) };
        let signing_key: &SigningKey = unsafe { &*storage_ptr };
        let pk_bytes: [u8; 32] = VerificationKeyBytes::from(signing_key).into();
        unsafe { write_array(out_public_ptr, pk_bytes) };
        STATUS_OK
    })
}

/// Initialize a VerificationKey at `storage_ptr` from a 32-byte public key.
/// Returns STATUS_DECODE_FAILED if the bytes don't form a valid point.
#[no_mangle]
pub extern "C" fn ton_ed25519_verification_key_init(
    storage_ptr: *mut VerificationKey,
    public_key_ptr: *const [u8; 32],
) -> u32 {
    guard(|| {
        let pk_bytes = unsafe { read_array(public_key_ptr) };
        let vk = match VerificationKey::try_from(VerificationKeyBytes::from(pk_bytes)) {
            Ok(v) => v,
            Err(_) => return STATUS_DECODE_FAILED,
        };
        unsafe { check_aligned(storage_ptr) };
        unsafe { storage_ptr.write(vk) };
        STATUS_OK
    })
}

/// Verify a 64-byte signature with a previously-initialized VerificationKey.
#[no_mangle]
pub extern "C" fn ton_ed25519_verification_key_verify(
    storage_ptr: *const VerificationKey,
    message_ptr: *const u8,
    message_len: usize,
    signature_ptr: *const [u8; 64],
) -> u32 {
    guard(|| {
        unsafe { check_aligned(storage_ptr) };
        let vk: &VerificationKey = unsafe { &*storage_ptr };
        let sig_bytes = unsafe { read_array(signature_ptr) };
        let message = unsafe { make_slice(message_ptr, message_len) };
        match vk.verify(&Signature::from(sig_bytes), message) {
            Ok(()) => STATUS_OK,
            Err(_) => STATUS_VERIFY_FAILED,
        }
    })
}

// ---------------------------------------------------------------------------
// X25519 shared secret derived from Ed25519 keys (TON's compute_shared_secret).
// ---------------------------------------------------------------------------

/// Mirrors `Ed25519::compute_shared_secret`:
///   1. SHA-512(secret)[0..32], with X25519 clamping
///   2. decode peer's Ed25519 public key as Edwards point, convert to Montgomery
///   3. X25519: scalar * point
///
/// Inputs:  32-byte Ed25519 secret seed, 32-byte peer Ed25519 public key.
/// Output:  32-byte shared secret.
#[no_mangle]
pub extern "C" fn ton_x25519_shared_secret_from_ed25519(
    secret_ptr: *const [u8; 32],
    peer_public_ptr: *const [u8; 32],
    out_secret_ptr: *mut [u8; 32],
) -> u32 {
    guard(|| {
        let secret = unsafe { read_array(secret_ptr) };
        let peer_pub = unsafe { read_array(peer_public_ptr) };

        let h = Sha512::digest(secret);
        let scalar_unclamped: [u8; 32] = h[..32]
            .try_into()
            .expect("SHA-512 output has at least 32 bytes");

        // Decode Ed25519 public as compressed Edwards y; reject non-canonical /
        // off-curve points.
        let edwards = match CompressedEdwardsY::from_slice(&peer_pub) {
            Ok(c) => match c.decompress() {
                Some(p) => p,
                None => return STATUS_DECODE_FAILED,
            },
            Err(_) => return STATUS_DECODE_FAILED,
        };
        let montgomery = edwards.to_montgomery();
        let shared = montgomery.mul_clamped(scalar_unclamped);
        let bytes = shared.to_bytes();
        unsafe { write_array(out_secret_ptr, bytes) };
        STATUS_OK
    })
}

// ---------------------------------------------------------------------------
// Ristretto255 (used by TVM RIST255_MUL / RIST255_MULBASE opcodes).
// ---------------------------------------------------------------------------

/// scalar * point (ristretto255). Mirrors libsodium's
/// `crypto_scalarmult_ristretto255`: returns STATUS_VERIFY_FAILED if the
/// resulting point is the identity (matches libsodium's -1 return).
#[no_mangle]
pub extern "C" fn ton_ristretto255_scalar_mult(
    scalar_ptr: *const [u8; 32],
    point_ptr: *const [u8; 32],
    out_ptr: *mut [u8; 32],
) -> u32 {
    guard(|| {
        let scalar_bytes = unsafe { read_array(scalar_ptr) };
        let point_bytes = unsafe { read_array(point_ptr) };
        let scalar = Scalar::from_bytes_mod_order(scalar_bytes);
        let point = match CompressedRistretto::from_slice(&point_bytes) {
            Ok(c) => match c.decompress() {
                Some(p) => p,
                None => return STATUS_DECODE_FAILED,
            },
            Err(_) => return STATUS_DECODE_FAILED,
        };
        let result = point * scalar;
        let compressed = result.compress();
        if compressed.as_bytes().iter().all(|b| *b == 0) {
            return STATUS_VERIFY_FAILED;
        }
        unsafe { write_array(out_ptr, *compressed.as_bytes()) };
        STATUS_OK
    })
}

/// scalar * G (ristretto255 base point). Mirrors libsodium's
/// `crypto_scalarmult_ristretto255_base`.
#[no_mangle]
pub extern "C" fn ton_ristretto255_scalar_mult_base(
    scalar_ptr: *const [u8; 32],
    out_ptr: *mut [u8; 32],
) -> u32 {
    guard(|| {
        let scalar_bytes = unsafe { read_array(scalar_ptr) };
        let scalar = Scalar::from_bytes_mod_order(scalar_bytes);
        let result = curve25519_dalek::constants::RISTRETTO_BASEPOINT_TABLE * &scalar;
        let compressed = result.compress();
        if compressed.as_bytes().iter().all(|b| *b == 0) {
            return STATUS_VERIFY_FAILED;
        }
        unsafe { write_array(out_ptr, *compressed.as_bytes()) };
        STATUS_OK
    })
}

// ---------------------------------------------------------------------------
// Tests (Rust-side; runs under `cargo test`, separate from the C++ parity test).
// ---------------------------------------------------------------------------

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn ed25519_round_trip() {
        let secret = [42u8; 32];
        let mut public = [0u8; 32];
        assert_eq!(
            ton_ed25519_secret_to_public(&secret, &mut public),
            STATUS_OK
        );

        let msg = b"hello ton";
        let mut sig = [0u8; 64];
        assert_eq!(
            ton_ed25519_sign(&secret, msg.as_ptr(), msg.len(), &mut sig),
            STATUS_OK
        );

        assert_eq!(
            ton_ed25519_verify(&public, msg.as_ptr(), msg.len(), &sig),
            STATUS_OK
        );

        sig[0] ^= 1;
        assert_eq!(
            ton_ed25519_verify(&public, msg.as_ptr(), msg.len(), &sig),
            STATUS_VERIFY_FAILED
        );
    }

    #[test]
    fn ristretto255_base_round_trip() {
        let scalar = [7u8; 32];
        let mut p = [0u8; 32];
        assert_eq!(
            ton_ristretto255_scalar_mult_base(&scalar, &mut p),
            STATUS_OK
        );
        assert!(p.iter().any(|b| *b != 0));
    }

    #[test]
    fn shared_secret_symmetric() {
        let alice_sec = [1u8; 32];
        let bob_sec = [2u8; 32];
        let mut alice_pub = [0u8; 32];
        let mut bob_pub = [0u8; 32];
        ton_ed25519_secret_to_public(&alice_sec, &mut alice_pub);
        ton_ed25519_secret_to_public(&bob_sec, &mut bob_pub);

        let mut k1 = [0u8; 32];
        let mut k2 = [0u8; 32];
        assert_eq!(
            ton_x25519_shared_secret_from_ed25519(&alice_sec, &bob_pub, &mut k1),
            STATUS_OK
        );
        assert_eq!(
            ton_x25519_shared_secret_from_ed25519(&bob_sec, &alice_pub, &mut k2),
            STATUS_OK
        );
        assert_eq!(k1, k2, "ECDH must be symmetric");
    }

    #[test]
    fn null_pointer_panics_into_status() {
        // The FFI guard catches the panic and surfaces it as STATUS_PANIC
        // rather than UB. C++ wrappers are expected never to trip this.
        let mut out = [0u8; 32];
        assert_eq!(
            ton_ed25519_secret_to_public(std::ptr::null(), &mut out),
            STATUS_PANIC
        );
    }

    #[test]
    fn prepared_signing_key_round_trip() {
        let mut sk_storage = std::mem::MaybeUninit::<SigningKey>::uninit();
        let secret = [9u8; 32];
        assert_eq!(
            ton_ed25519_signing_key_init(sk_storage.as_mut_ptr(), &secret),
            STATUS_OK
        );

        let mut pub_out = [0u8; 32];
        assert_eq!(
            ton_ed25519_signing_key_public(sk_storage.as_ptr(), &mut pub_out),
            STATUS_OK
        );

        let msg = b"signed via prepared SigningKey";
        let mut sig = [0u8; 64];
        assert_eq!(
            ton_ed25519_signing_key_sign(
                sk_storage.as_ptr(), msg.as_ptr(), msg.len(), &mut sig
            ),
            STATUS_OK
        );

        let mut vk_storage = std::mem::MaybeUninit::<VerificationKey>::uninit();
        assert_eq!(
            ton_ed25519_verification_key_init(vk_storage.as_mut_ptr(), &pub_out),
            STATUS_OK
        );
        assert_eq!(
            ton_ed25519_verification_key_verify(
                vk_storage.as_ptr(), msg.as_ptr(), msg.len(), &sig
            ),
            STATUS_OK
        );
    }
}
