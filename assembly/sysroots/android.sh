#!/usr/bin/env bash
set -euo pipefail

# Creates an Android sysroot for cross-compilation from Linux.
#
# Usage: ./android.sh <build_dir> <target> [options]
#   Targets: android-arm64, android-x86_64, android-arm, android-x86
#   Options:
#     --ndk=<path>       Path to existing Android NDK (downloads if not set)
#     --api=<level>      Android API level (default: 21)

if [ $# -lt 2 ]; then
    echo "Usage: $0 <build_dir> <target> [--ndk=<path>] [--api=<level>]"
    echo "  Targets: android-arm64, android-x86_64, android-arm, android-x86"
    exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null && pwd)"
source "$SCRIPT_DIR/common.sh"

BUILD_DIR="$(mkdir -p "$1" && cd "$1" && pwd)"
TARGET_NAME="$2"
shift 2

NDK_PATH=""
API_LEVEL=21
while [[ $# -gt 0 ]]; do
    case "$1" in
        --ndk=*) NDK_PATH="${1#*=}" ;;
        --api=*) API_LEVEL="${1#*=}" ;;
        *) echo "Unknown option: $1"; exit 1 ;;
    esac
    shift
done

case "$TARGET_NAME" in
    android-arm64)
        TARGET_TRIPLE=aarch64-linux-android${API_LEVEL}
        ANDROID_ABI=arm64-v8a
        CLANG_PREFIX=aarch64-linux-android
        TOOLCHAIN_SYSTEM_PROCESSOR=aarch64
        ;;
    android-x86_64)
        TARGET_TRIPLE=x86_64-linux-android${API_LEVEL}
        ANDROID_ABI=x86_64
        CLANG_PREFIX=x86_64-linux-android
        TOOLCHAIN_SYSTEM_PROCESSOR=x86_64
        ;;
    android-arm)
        TARGET_TRIPLE=armv7a-linux-androideabi${API_LEVEL}
        ANDROID_ABI=armeabi-v7a
        CLANG_PREFIX=armv7a-linux-androideabi
        TOOLCHAIN_SYSTEM_PROCESSOR=armv7-a
        ;;
    android-x86)
        TARGET_TRIPLE=i686-linux-android${API_LEVEL}
        ANDROID_ABI=x86
        CLANG_PREFIX=i686-linux-android
        TOOLCHAIN_SYSTEM_PROCESSOR=i686
        ;;
    *)
        echo "Error: Unsupported target '$TARGET_NAME'"
        exit 1
        ;;
esac

# ===== Download NDK if needed =====
if [ -z "$NDK_PATH" ]; then
    NDK_PATH="$BUILD_DIR/android-ndk-$ANDROID_NDK_VERSION"
    if [ ! -d "$NDK_PATH" ]; then
        echo "Downloading Android NDK $ANDROID_NDK_VERSION..."
        download_verified "$ANDROID_NDK_URL" "$BUILD_DIR/android-ndk-$ANDROID_NDK_VERSION-linux.zip" "$ANDROID_NDK_SHA256"
        cd "$BUILD_DIR"
        unzip -q "android-ndk-$ANDROID_NDK_VERSION-linux.zip"
    fi
fi

if [ ! -d "$NDK_PATH" ]; then
    echo "ERROR: NDK not found at $NDK_PATH"
    exit 1
fi

# Find NDK toolchain
NDK_TOOLCHAIN="$NDK_PATH/toolchains/llvm/prebuilt/linux-x86_64"
if [ ! -d "$NDK_TOOLCHAIN" ]; then
    echo "ERROR: NDK toolchain not found at $NDK_TOOLCHAIN"
    exit 1
fi

NDK_BIN="$NDK_TOOLCHAIN/bin"
CC="$NDK_BIN/${CLANG_PREFIX}${API_LEVEL}-clang"
CXX="$NDK_BIN/${CLANG_PREFIX}${API_LEVEL}-clang++"
AR="$NDK_BIN/llvm-ar"
RANLIB="$NDK_BIN/llvm-ranlib"
export CC CXX AR RANLIB

SYSROOT="$BUILD_DIR/sysroot-android-$ANDROID_ABI"
BUILD_ROOT="$BUILD_DIR/sysroot-android-$ANDROID_ABI-build"

mkdir -p "$SYSROOT" "$BUILD_ROOT"

mkdir -p "$SYSROOT/usr/lib" "$SYSROOT/usr/include"

# ===== Generate toolchain =====
# For Android, we use the NDK's sysroot for system headers/libs
NDK_SYSROOT="$NDK_TOOLCHAIN/sysroot"

export TOOLCHAIN_SYSTEM_NAME=Android
export TOOLCHAIN_SYSTEM_PROCESSOR
export TOOLCHAIN_TARGET_TRIPLE="$TARGET_TRIPLE"
export TOOLCHAIN_SYSROOT="$NDK_SYSROOT"
export TOOLCHAIN_EXTRA_C_FLAGS="-fPIC"
export TOOLCHAIN_EXTRA_CXX_FLAGS="-fPIC"
export TOOLCHAIN_EXTRA_LINKER_FLAGS=""

TOOLCHAIN_FILE="$SYSROOT/Toolchain.cmake"
generate_toolchain "$TOOLCHAIN_FILE"

# Append Android-specific CMake settings
cat >> "$TOOLCHAIN_FILE" <<APPEND_EOF

# Android-specific settings
set(ANDROID TRUE)
set(CMAKE_ANDROID_NDK "$NDK_PATH")
set(CMAKE_ANDROID_ARCH_ABI "$ANDROID_ABI")
set(CMAKE_ANDROID_API $API_LEVEL)
set(CMAKE_ANDROID_STL_TYPE c++_static)
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
export CFLAGS="-fPIC -D__ANDROID_API__=$API_LEVEL"
export CXXFLAGS="$CFLAGS"
export LDFLAGS=""

"$SCRIPT_DIR/build-deps.sh"

echo ""
echo "===== Android sysroot created successfully ====="
echo "  Sysroot:   $SYSROOT"
echo "  Toolchain: $TOOLCHAIN_FILE"
echo ""
echo "To build TON:"
echo "  cmake -B build -DCMAKE_TOOLCHAIN_FILE=$TOOLCHAIN_FILE -DTON_DEPS_PREFIX=$SYSROOT/usr"
echo "  cmake --build build"
