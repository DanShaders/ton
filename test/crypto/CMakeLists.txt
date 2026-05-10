# Only built when the Rust crypto crate is enabled. The tests link against
# both the Rust FFI (via ton_crypto) and the existing OpenSSL/libsodium
# paths so they can do parity checks.
if (NOT TON_USE_RUST_CRYPTO)
  return()
endif()

add_executable(test-ton-rust-crypto test-ton-rust-crypto.cpp)
target_link_libraries(test-ton-rust-crypto PRIVATE
  ton_crypto
  td-test-main
)
ton_test(test-ton-rust-crypto)
