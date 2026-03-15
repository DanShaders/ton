#!/usr/bin/env bash
set -euo pipefail

# Creates a WASM/Emscripten sysroot for cross-compilation.
#
# Usage: ./wasm.sh <build_dir> [options]
#   Options:
#     --emsdk=<path>     Path to existing emsdk (clones if not set)

if [ $# -lt 1 ]; then
    echo "Usage: $0 <build_dir> [--emsdk=<path>]"
    exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null && pwd)"
source "$SCRIPT_DIR/common.sh"

BUILD_DIR="$(mkdir -p "$1" && cd "$1" && pwd)"
shift

EMSDK_PATH=""
while [[ $# -gt 0 ]]; do
    case "$1" in
        --emsdk=*) EMSDK_PATH="${1#*=}" ;;
        *) echo "Unknown option: $1"; exit 1 ;;
    esac
    shift
done

TARGET_TRIPLE=wasm32-unknown-emscripten

# ===== Set up emsdk =====
if [ -z "$EMSDK_PATH" ]; then
    EMSDK_PATH="$BUILD_DIR/emsdk"
    if [ ! -d "$EMSDK_PATH" ]; then
        echo "Cloning emsdk..."
        git clone https://github.com/emscripten-core/emsdk.git "$EMSDK_PATH"
    fi
fi

cd "$EMSDK_PATH"
./emsdk install "$EMSDK_VERSION"
./emsdk activate "$EMSDK_VERSION"
source "$EMSDK_PATH/emsdk_env.sh"

CC=$(which emcc)
CXX=$(which em++)
AR=$(which emar)
RANLIB=$(which emranlib)
export CC CXX AR RANLIB

SYSROOT="$BUILD_DIR/sysroot-wasm"
BUILD_ROOT="$BUILD_DIR/sysroot-wasm-build"
EMSCRIPTEN_TOOLCHAIN="$EMSDK_PATH/upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake"

mkdir -p "$SYSROOT" "$BUILD_ROOT" "$SYSROOT/usr/lib" "$SYSROOT/usr/include"

# ===== Build deps =====
echo "Building dependencies..."
export TARGET_TRIPLE
export PREFIX=/usr
export DESTDIR="$SYSROOT"
export SOURCE_DIR
export BUILD_DIR="$BUILD_ROOT"
export TARBALLS_DIR="$BUILD_DIR/tarballs"
export NPROC
export CMAKE_TOOLCHAIN_FILE="$EMSCRIPTEN_TOOLCHAIN"
export CFLAGS=""
export CXXFLAGS=""
export LDFLAGS=""

# For WASM, skip deps that don't make sense
"$SCRIPT_DIR/build-deps.sh" openssl sodium blst secp256k1 zlib lz4 crc32c tl-parser

# ===== Generate a convenience toolchain that wraps Emscripten's =====
TOOLCHAIN_FILE="$SYSROOT/Toolchain.cmake"
cat > "$TOOLCHAIN_FILE" <<TOOLCHAIN_EOF
# Include Emscripten's toolchain
include($EMSCRIPTEN_TOOLCHAIN)

# Point to our pre-built deps
list(APPEND CMAKE_PREFIX_PATH "$SYSROOT/usr")
set(CMAKE_FIND_ROOT_PATH "$SYSROOT/usr" "\${CMAKE_FIND_ROOT_PATH}")

# Pre-built dependency locations
set(OPENSSL_ROOT_DIR "$SYSROOT/usr" CACHE PATH "" FORCE)
set(OPENSSL_INCLUDE_DIR "$SYSROOT/usr/include" CACHE PATH "" FORCE)
set(OPENSSL_CRYPTO_LIBRARY "$SYSROOT/usr/lib/libcrypto.a" CACHE FILEPATH "" FORCE)
set(OPENSSL_SSL_LIBRARY "$SYSROOT/usr/lib/libssl.a" CACHE FILEPATH "" FORCE)
set(OPENSSL_FOUND TRUE CACHE BOOL "" FORCE)

set(SODIUM_INCLUDE_DIR "$SYSROOT/usr/include" CACHE PATH "" FORCE)
set(SODIUM_LIBRARY_RELEASE "$SYSROOT/usr/lib/libsodium.a" CACHE FILEPATH "" FORCE)
set(SODIUM_FOUND TRUE CACHE BOOL "" FORCE)

set(ZLIB_INCLUDE_DIR "$SYSROOT/usr/include" CACHE PATH "" FORCE)
set(ZLIB_LIBRARY "$SYSROOT/usr/lib/libz.a" CACHE FILEPATH "" FORCE)
set(ZLIB_FOUND TRUE CACHE BOOL "" FORCE)
TOOLCHAIN_EOF

echo ""
echo "===== WASM sysroot created successfully ====="
echo "  Sysroot:   $SYSROOT"
echo "  Toolchain: $TOOLCHAIN_FILE"
echo ""
echo "To build TON:"
echo "  cmake -B build -DCMAKE_TOOLCHAIN_FILE=$TOOLCHAIN_FILE -DTON_DEPS_PREFIX=$SYSROOT/usr -DUSE_EMSCRIPTEN=ON"
echo "  cmake --build build"
