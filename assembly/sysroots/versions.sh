#!/usr/bin/env bash

# ===== musl libc =====
MUSL_VERSION=1.2.5
MUSL_URL="https://musl.libc.org/releases/musl-${MUSL_VERSION}.tar.gz"
MUSL_SHA256="a9a118bbe84d8764da0ea0d28b3ab3fae8477fc7e4085d90102b8596fc7c75e4"
MUSL_PATCH1_URL="https://www.openwall.com/lists/musl/2025/02/13/1/1"
MUSL_PATCH1_SHA256="0896fcdb5125d9d0723f4e165f13c209830e7045a75cba4e858060837cb7292e"
MUSL_PATCH2_URL="https://www.openwall.com/lists/musl/2025/02/13/1/2"
MUSL_PATCH2_SHA256="0620fcee4e8a4e52ebe1ea75e2b51d2197ebda242489c0586924eafa9e9606a1"

# ===== Linux kernel headers =====
LINUX_HEADERS_VERSION=6.6.118
LINUX_HEADERS_URL="https://cdn.kernel.org/pub/linux/kernel/v6.x/linux-${LINUX_HEADERS_VERSION}.tar.xz"
LINUX_HEADERS_SHA256="4bdddce35474afc8d26f74ebfbcd0e1045ecd15f69e60f53529dba143374b17d"

# ===== LLVM =====
LLVM_MAJOR_VERSION=21
LLVM_VERSION=21.1.7
LLVM_URL="https://github.com/llvm/llvm-project/releases/download/llvmorg-${LLVM_VERSION}/llvm-project-${LLVM_VERSION}.src.tar.xz"
LLVM_SHA256="e5b65fd79c95c343bb584127114cb2d252306c1ada1e057899b6aacdd445899e"

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
