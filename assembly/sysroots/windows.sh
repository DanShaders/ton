#!/usr/bin/env bash
set -euo pipefail

# Creates a Windows sysroot for cross-compilation from Linux using clang-cl
# (MSVC ABI) + lld-link, with the Windows SDK and CRT pulled in via xwin.
#
# Usage: ./windows.sh <build_dir> <target> [options]
#   Targets: windows-x86_64
#   Options:
#     --xwin-cache=<path>  Cache dir for xwin downloads
#                          (default: <build_dir>/tarballs/xwin)

if [ $# -lt 2 ]; then
    echo "Usage: $0 <build_dir> <target> [--xwin-cache=<path>]"
    echo "  Targets: windows-x86_64"
    exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null && pwd)"
source "$SCRIPT_DIR/common.sh"

BUILD_DIR="$(mkdir -p "$1" && cd "$1" && pwd)"
TARGET_NAME="$2"
shift 2

XWIN_CACHE=""
while [[ $# -gt 0 ]]; do
    case "$1" in
        --xwin-cache=*) XWIN_CACHE="${1#*=}" ;;
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
        echo "  Supported: windows-x86_64"
        exit 1
        ;;
esac

if ! command -v xwin >/dev/null; then
    echo "ERROR: 'xwin' not found on PATH." >&2
    echo "       Install via 'cargo install xwin' or your distro's package manager." >&2
    exit 1
fi

find_clang_cl "$LLVM_MAJOR_VERSION"

SYSROOT="$BUILD_DIR/sysroot-$TARGET_TRIPLE"
TARBALLS_DIR="$BUILD_DIR/tarballs"
BUILD_ROOT="$BUILD_DIR/sysroot-$TARGET_TRIPLE-build"
XWIN_SYSROOT="$SYSROOT/xwin"
: "${XWIN_CACHE:=$TARBALLS_DIR/xwin}"

mkdir -p "$TARBALLS_DIR" "$BUILD_ROOT" "$SYSROOT" "$XWIN_CACHE"
mkdir -p "$SYSROOT/usr/lib" "$SYSROOT/usr/include"

# ===== Splat Windows SDK + CRT into the sysroot via xwin =====
# --use-winsysroot-style produces a layout that clang-cl's -winsysroot can
# consume directly (Windows Kits/10/Include/<ver> + VC/Tools/MSVC/<ver>).
# --include-debug-libs because some deps (e.g. rocksdb in Debug config) link
# against the debug CRT.
if [ ! -d "$XWIN_SYSROOT/Windows Kits/10/Include/$WINDOWS_SDK_VERSION" ]; then
    echo "Splatting Windows SDK ($WINDOWS_SDK_VERSION) and CRT ($WINDOWS_CRT_VERSION) into $XWIN_SYSROOT..."
    xwin --accept-license --arch "$XWIN_ARCH" \
        --sdk-version "$WINDOWS_SDK_VERSION" \
        --crt-version "$WINDOWS_CRT_VERSION" \
        --cache-dir "$XWIN_CACHE" \
        splat --output "$XWIN_SYSROOT" \
        --preserve-ms-arch-notation --include-debug-libs --use-winsysroot-style
fi

# ===== Generate a thin lld-link wrapper that hardcodes /winsysroot: =====
# CMake's CMAKE_LINKER is strictly a single path on Windows (LINKER mode), with
# no immutable-args channel akin to CMAKE_<LANG>_COMPILER's _ARG1 plumbing.
# Our wrapper sidesteps that limitation by being a real binary-from-CMake's-POV
# while still injecting /winsysroot: on every link invocation.
mkdir -p "$SYSROOT/bin"
WRAPPER="$SYSROOT/bin/lld-link"
cat > "$WRAPPER" <<WRAPPER_EOF
#!/bin/sh
exec "$LINKER" "/winsysroot:$XWIN_SYSROOT" "\$@"
WRAPPER_EOF
chmod +x "$WRAPPER"
LINKER="$WRAPPER"
export LINKER

# ===== Generate the toolchain =====
# -winsysroot is required at compile (header search) and link (lib search),
# so it goes into the immutable CMAKE_<LANG>_COMPILER list. --target is set
# via CMAKE_<LANG>_COMPILER_TARGET inside the template.
export COMBINED_C_FLAGS="-winsysroot $XWIN_SYSROOT"
export COMBINED_CXX_FLAGS="-winsysroot $XWIN_SYSROOT"

export TOOLCHAIN_SYSTEM_NAME=Windows
export TOOLCHAIN_SYSTEM_PROCESSOR
export TOOLCHAIN_TARGET_TRIPLE="$TARGET_TRIPLE"
export TOOLCHAIN_SYSROOT="$SYSROOT"

TOOLCHAIN_FILE="$SYSROOT/Toolchain.cmake"
generate_toolchain_windows "$TOOLCHAIN_FILE"

# ===== Build third-party deps =====
echo "Building dependencies..."
export TARGET_TRIPLE
export PREFIX=/usr
export DESTDIR="$SYSROOT"
export SOURCE_DIR
export BUILD_DIR="$BUILD_ROOT"
export TARBALLS_DIR
export NPROC
export CMAKE_TOOLCHAIN_FILE="$TOOLCHAIN_FILE"

# Autotools-driven deps don't really run on Linux→Windows; build-deps.sh
# routes those through CMake wrappers (e.g. libsodium-cmake) for this target.
# CMake-driven deps unset CFLAGS/CXXFLAGS internally and rely on the toolchain
# file, so the values below only matter for the few deps that still call
# `make` directly (notably OpenSSL, which uses its own Configure→Makefile).
export CC="$CC -winsysroot \"$XWIN_SYSROOT\" --target=$TARGET_TRIPLE"
export CXX="$CXX -winsysroot \"$XWIN_SYSROOT\" --target=$TARGET_TRIPLE"
export CFLAGS="/O2"
export CXXFLAGS="/O2 -std:c++20"
export LDFLAGS=""

# For now we only build deps that maintain a sane CMake-based build. The
# autotools-driven ones (openssl, sodium, mhd, libbacktrace) need separate
# handling on Linux→Windows and are deliberately skipped. blst's custom
# build.sh hasn't been validated against clang-cl yet either. ngtcp2 also
# sits out for now: it's CMake-based but depends on the still-missing OpenSSL.
"$SCRIPT_DIR/build-deps.sh" \
    secp256k1 zlib lz4 crc32c rocksdb abseil

echo
echo "===== Windows sysroot created successfully ====="
echo "  Sysroot:   $SYSROOT"
echo "  Toolchain: $TOOLCHAIN_FILE"
echo
echo "To build TON:"
echo "  cmake -B build -DCMAKE_TOOLCHAIN_FILE=$TOOLCHAIN_FILE -DTON_DEPS_PREFIX=$SYSROOT/usr"
echo "  cmake --build build"
