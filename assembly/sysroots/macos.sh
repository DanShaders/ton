#!/usr/bin/env bash
set -euo pipefail

# Creates a macOS sysroot for cross-compilation from Linux.
#
# Requires an Apple SDK. Provide via APPLE_SDK_PATH env var or use osxcross.
#
# Usage: ./macos.sh <build_dir> <target> [options]
#   Targets: macos-arm64, macos-x86_64
#   Options:
#     --sdk=<path>         Path to macOS SDK (e.g., MacOSX14.0.sdk)
#     --sanitizer=<type>   Enable sanitizer (address, undefined)

if [ $# -lt 2 ]; then
    echo "Usage: $0 <build_dir> <target> [--sdk=<path>] [--sanitizer=<type>]"
    echo "  Targets: macos-arm64, macos-x86_64"
    exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null && pwd)"
source "$SCRIPT_DIR/common.sh"

BUILD_DIR="$(mkdir -p "$1" && cd "$1" && pwd)"
TARGET_NAME="$2"
shift 2

SANITIZER=""
SDK_PATH="${APPLE_SDK_PATH:-}"
while [[ $# -gt 0 ]]; do
    case "$1" in
        --sdk=*)       SDK_PATH="${1#*=}" ;;
        --sanitizer=*) SANITIZER="${1#*=}" ;;
        *) echo "Unknown option: $1"; exit 1 ;;
    esac
    shift
done

if [ -z "$SDK_PATH" ]; then
    echo "ERROR: macOS SDK required. Set APPLE_SDK_PATH or pass --sdk=<path>"
    echo "  You can extract an SDK from Xcode or use osxcross to package one."
    exit 1
fi

if [ ! -d "$SDK_PATH" ]; then
    echo "ERROR: SDK path does not exist: $SDK_PATH"
    exit 1
fi

case "$TARGET_NAME" in
    macos-arm64)
        TARGET_TRIPLE=arm64-apple-darwin
        TOOLCHAIN_SYSTEM_PROCESSOR=arm64
        MIN_VERSION="11.0"
        ;;
    macos-x86_64)
        TARGET_TRIPLE=x86_64-apple-darwin
        TOOLCHAIN_SYSTEM_PROCESSOR=x86_64
        MIN_VERSION="10.15"
        ;;
    *)
        echo "Error: Unsupported target '$TARGET_NAME'"
        exit 1
        ;;
esac

find_clang "$LLVM_MAJOR_VERSION"

SYSROOT="$BUILD_DIR/sysroot-$TARGET_TRIPLE"
BUILD_ROOT="$BUILD_DIR/sysroot-$TARGET_TRIPLE-build"

mkdir -p "$SYSROOT" "$BUILD_ROOT"

mkdir -p "$SYSROOT/usr/lib" "$SYSROOT/usr/include"

# macOS cross-compilation flags
DARWIN_FLAGS="--target=$TARGET_TRIPLE"
DARWIN_FLAGS="$DARWIN_FLAGS -isysroot $SDK_PATH"
DARWIN_FLAGS="$DARWIN_FLAGS -mmacosx-version-min=$MIN_VERSION"
DARWIN_FLAGS="$DARWIN_FLAGS -fuse-ld=lld"

EXTRA_C_FLAGS="$DARWIN_FLAGS"
EXTRA_CXX_FLAGS="$DARWIN_FLAGS -stdlib=libc++"
EXTRA_LINKER_FLAGS="$DARWIN_FLAGS -stdlib=libc++"

if [ -n "$SANITIZER" ]; then
    EXTRA_C_FLAGS="$EXTRA_C_FLAGS -fsanitize=$SANITIZER"
    EXTRA_CXX_FLAGS="$EXTRA_CXX_FLAGS -fsanitize=$SANITIZER"
    EXTRA_LINKER_FLAGS="$EXTRA_LINKER_FLAGS -fsanitize=$SANITIZER"
fi

# ===== Generate toolchain =====
# For macOS, the "sysroot" in CMake terms is the Apple SDK
export TOOLCHAIN_SYSTEM_NAME=Darwin
export TOOLCHAIN_SYSTEM_PROCESSOR
export TOOLCHAIN_TARGET_TRIPLE="$TARGET_TRIPLE"
export TOOLCHAIN_SYSROOT="$SDK_PATH"
export TOOLCHAIN_EXTRA_C_FLAGS="$EXTRA_C_FLAGS"
export TOOLCHAIN_EXTRA_CXX_FLAGS="$EXTRA_CXX_FLAGS"
export TOOLCHAIN_EXTRA_LINKER_FLAGS="$EXTRA_LINKER_FLAGS"

TOOLCHAIN_FILE="$SYSROOT/Toolchain.cmake"
generate_toolchain "$TOOLCHAIN_FILE"

# Append macOS-specific CMake settings
cat >> "$TOOLCHAIN_FILE" <<APPEND_EOF

# macOS-specific settings
set(CMAKE_OSX_DEPLOYMENT_TARGET "$MIN_VERSION")
set(CMAKE_OSX_SYSROOT "$SDK_PATH")
set(APPLE TRUE)
APPEND_EOF

# ===== Build deps =====
echo "Building dependencies..."
export TARGET_TRIPLE
export PREFIX=/usr
export DESTDIR="$SYSROOT"
export SOURCE_DIR
export BUILD_DIR="$BUILD_ROOT"
export TARBALLS_DIR="$BUILD_DIR/tarballs"
export NPROC
export CMAKE_TOOLCHAIN_FILE="$TOOLCHAIN_FILE"
export CFLAGS="$EXTRA_C_FLAGS"
export CXXFLAGS="$EXTRA_CXX_FLAGS"
export LDFLAGS="$EXTRA_LINKER_FLAGS"

"$SCRIPT_DIR/build-deps.sh"

echo ""
echo "===== macOS sysroot created successfully ====="
echo "  Sysroot:   $SYSROOT"
echo "  Toolchain: $TOOLCHAIN_FILE"
echo ""
echo "To build TON:"
echo "  cmake -B build -DCMAKE_TOOLCHAIN_FILE=$TOOLCHAIN_FILE -DTON_DEPS_PREFIX=$SYSROOT/usr"
echo "  cmake --build build"
