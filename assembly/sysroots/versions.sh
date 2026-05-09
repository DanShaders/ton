#!/usr/bin/env bash

# ===== musl libc =====
MUSL_VERSION=1.2.6
MUSL_URL="https://musl.libc.org/releases/musl-${MUSL_VERSION}.tar.gz"
MUSL_SHA256="d585fd3b613c66151fc3249e8ed44f77020cb5e6c1e635a616d3f9f82460512a"
MUSL_PATCH1_URL="https://www.openwall.com/lists/musl/2026/04/10/3/1"
MUSL_PATCH1_SHA256="1ee29f64f9ca8e8ad7c349779d661ff6b52126a27575d3586981357a52c406fb"
MUSL_PATCH2_URL="https://www.openwall.com/lists/musl/2026/04/03/2/1"
MUSL_PATCH2_SHA256="444fa70e52ca158fb7d4bad560637790bbf8f72e80b82fff840dd66fa83091e3"

# ===== Linux kernel headers =====
LINUX_HEADERS_VERSION=6.6.138
LINUX_HEADERS_URL="https://cdn.kernel.org/pub/linux/kernel/v6.x/linux-${LINUX_HEADERS_VERSION}.tar.xz"
LINUX_HEADERS_SHA256="add06b5fdb655c7e575fbfa29e7bab23a3c36c5388e77fa759ed4b0d1a55a80f"

# ===== LLVM =====
LLVM_MAJOR_VERSION=22
LLVM_VERSION=22.1.5
LLVM_URL="https://github.com/llvm/llvm-project/releases/download/llvmorg-${LLVM_VERSION}/llvm-project-${LLVM_VERSION}.src.tar.xz"
LLVM_SHA256="7972b87b705a003ce70ab55f9f0fb495d156887cba0eb296d284731139118e2c"

# ===== OpenSSL =====
OPENSSL_VERSION=3.5.5
OPENSSL_URL="https://github.com/openssl/openssl/releases/download/openssl-${OPENSSL_VERSION}/openssl-${OPENSSL_VERSION}.tar.gz"
OPENSSL_SHA256="b28c91532a8b65a1f983b4c28b7488174e4a01008e29ce8e69bd789f28bc2a89"

# ===== libsodium =====
LIBSODIUM_VERSION=1.0.18
LIBSODIUM_URL="https://github.com/jedisct1/libsodium/releases/download/${LIBSODIUM_VERSION}-RELEASE/libsodium-${LIBSODIUM_VERSION}.tar.gz"
LIBSODIUM_SHA256="6f504490b342a4f8a4c4a02fc9b866cbef8622d5df4e5452b46be121e46636c1"

# ===== libmicrohttpd =====
LIBMICROHTTPD_VERSION=1.0.1
LIBMICROHTTPD_URL="https://github.com/Karlson2k/libmicrohttpd/releases/download/v${LIBMICROHTTPD_VERSION}/libmicrohttpd-${LIBMICROHTTPD_VERSION}.tar.gz"
LIBMICROHTTPD_SHA256="a89e09fc9b4de34dde19f4fcb4faaa1ce10299b9908db1132bbfa1de47882b94"

# ===== Android NDK =====
ANDROID_NDK_VERSION=r27d
ANDROID_NDK_URL="https://dl.google.com/android/repository/android-ndk-${ANDROID_NDK_VERSION}-linux.zip"
ANDROID_NDK_SHA256="0000000000000000000000000000000000000000000000000000000000000000"

# ===== Emscripten =====
EMSDK_VERSION=4.0.17
EMSDK_SHA256="0000000000000000000000000000000000000000000000000000000000000000"

# ===== xwin =====
XWIN_VERSION=0.6.5
XWIN_URL="https://github.com/Jake-Shadle/xwin/releases/download/${XWIN_VERSION}/xwin-${XWIN_VERSION}-x86_64-unknown-linux-musl.tar.gz"
XWIN_SHA256="0000000000000000000000000000000000000000000000000000000000000000"
