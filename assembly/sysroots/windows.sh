#!/usr/bin/env bash
set -euo pipefail

# Creates a Windows sysroot for cross-compilation from Linux using xwin + clang-cl (MSVC ABI).
#
# Usage: ./windows.sh <build_dir> <target> [options]
#   Targets: windows-x86_64
#   Options:
#     --sanitizer=<type>   Enable sanitizer (address, undefined)

if [ $# -lt 2 ]; then
    echo "Usage: $0 <build_dir> <target> [--sanitizer=<type>]"
    echo "  Targets: windows-x86_64"
    exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null && pwd)"
source "$SCRIPT_DIR/common.sh"

BUILD_DIR="$(mkdir -p "$1" && cd "$1" && pwd)"
TARGET_NAME="$2"
shift 2

SANITIZER=""
while [[ $# -gt 0 ]]; do
    case "$1" in
        --sanitizer=*) SANITIZER="${1#*=}" ;;
        *) echo "Unknown option: $1"; exit 1 ;;
    esac
    shift
done

case "$TARGET_NAME" in
    windows-x86_64)
        TARGET_TRIPLE=x86_64-pc-windows-msvc
        TOOLCHAIN_SYSTEM_PROCESSOR=AMD64
        XWIN_ARCH=x86_64
        ;;
    *)
        echo "Error: Unsupported target '$TARGET_NAME'"
        exit 1
        ;;
esac

find_clang_cl "$LLVM_MAJOR_VERSION"

SYSROOT="$BUILD_DIR/sysroot-$TARGET_TRIPLE"
TARBALLS_DIR="$BUILD_DIR/tarballs"
BUILD_ROOT="$BUILD_DIR/sysroot-$TARGET_TRIPLE-build"

mkdir -p "$TARBALLS_DIR" "$BUILD_ROOT" "$SYSROOT"

# ===== Download Windows SDK + CRT via xwin =====
XWIN_SYSROOT="$SYSROOT/xwin"
if [ ! -d "$XWIN_SYSROOT/Windows Kits/10/Include/$WINDOWS_SDK_VERSION" ]; then
    echo "Downloading Windows SDK and CRT via xwin..."
    xwin --accept-license --arch "$XWIN_ARCH" \
        --sdk-version $WINDOWS_SDK_VERSION --crt-version $WINDOWS_CRT_VERISON \
        --cache-dir "$TARBALLS_DIR/xwin" \
        splat --output "$XWIN_SYSROOT" \
        --preserve-ms-arch-notation --include-debug-libs --use-winsysroot-style
fi

mkdir -p "$SYSROOT/usr/lib" "$SYSROOT/usr/include"

# ===== Generate toolchain =====
if [ -n "$SANITIZER" ]; then
    CFLAGS="$CFLAGS -fsanitize=$SANITIZER"
    CXXFLAGS="$CXXFLAGS -fsanitize=$SANITIZER"
fi

export TOOLCHAIN_SYSTEM_NAME=Windows
export TOOLCHAIN_SYSTEM_PROCESSOR
export TOOLCHAIN_TARGET_TRIPLE="$TARGET_TRIPLE"
export TOOLCHAIN_SYSROOT="$SYSROOT"
export TOOLCHAIN_EXTRA_C_FLAGS="-winsysroot $XWIN_SYSROOT"
export TOOLCHAIN_EXTRA_RC_FLAGS="-winsysroot $XWIN_SYSROOT"
export TOOLCHAIN_EXTRA_CXX_FLAGS="-winsysroot $XWIN_SYSROOT"
export TOOLCHAIN_EXTRA_LINKER_FLAGS="-winsysroot:$XWIN_SYSROOT"

TOOLCHAIN_FILE="$SYSROOT/Toolchain.cmake"
generate_toolchain_windows "$TOOLCHAIN_FILE"

# ===== Build deps =====
echo "Building dependencies..."
export TARGET_TRIPLE
export PREFIX=/usr
export DESTDIR="$SYSROOT"
export SOURCE_DIR
export BUILD_DIR="$BUILD_ROOT"
export TARBALLS_DIR
export NPROC
export CMAKE_TOOLCHAIN_FILE="$TOOLCHAIN_FILE"
export CC="$CC -winsysroot $XWIN_SYSROOT --target=$TARGET_TRIPLE"
export CXX="$CXX -winsysroot $XWIN_SYSROOT --target=$TARGET_TRIPLE"
export CFLAGS="/O2"
export CXXFLAGS="/O2 -std=c++20"
export LDFLAGS="-fuse-ld=lld-link"

"$SCRIPT_DIR/build-deps.sh"
