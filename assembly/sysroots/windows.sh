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

# ===== Generate the autotools-only clang wrapper =====
# Autotools-driven deps (notably OpenSSL via the mingw64 target) feed GCC-style
# flags that clang-cl rejects but plain clang in --target=x86_64-pc-windows-msvc
# mode accepts. That mode does not understand /winsysroot, so the wrapper
# expands it manually into -isystem/-L into the xwin layout, plus the
# ABI-relevant bits clang-cl auto-adds (-D_MT/-D_DLL, --dependent-lib for the
# dynamic CRT) so its objects are ABI-compatible with the rest of the sysroot.
# See README/comments for the full enumeration of clang-cl's implicit cc1 args.
mkdir -p "$SYSROOT/bin"

CLANG_MSVC_WRAPPER="$SYSROOT/bin/clang-msvc"
cat > "$CLANG_MSVC_WRAPPER" <<WRAPPER_EOF
#!/bin/sh
exec /usr/bin/clang --target=$TARGET_TRIPLE -D_MT -D_DLL \\
    -Xclang --dependent-lib=msvcrt -Xclang --dependent-lib=oldnames \\
    -isystem "$XWIN_SYSROOT/VC/Tools/MSVC/$WINDOWS_CRT_VERSION/include" \\
    -isystem "$XWIN_SYSROOT/Windows Kits/10/Include/$WINDOWS_SDK_VERSION/ucrt" \\
    -isystem "$XWIN_SYSROOT/Windows Kits/10/Include/$WINDOWS_SDK_VERSION/shared" \\
    -isystem "$XWIN_SYSROOT/Windows Kits/10/Include/$WINDOWS_SDK_VERSION/um" \\
    -L"$XWIN_SYSROOT/VC/Tools/MSVC/$WINDOWS_CRT_VERSION/lib/x64" \\
    -L"$XWIN_SYSROOT/Windows Kits/10/Lib/$WINDOWS_SDK_VERSION/ucrt/x64" \\
    -L"$XWIN_SYSROOT/Windows Kits/10/Lib/$WINDOWS_SDK_VERSION/um/x64" \\
    -fuse-ld=lld-link \\
    "\$@"
WRAPPER_EOF
chmod +x "$CLANG_MSVC_WRAPPER"

# ===== Generate the toolchain =====
# /winsysroot: is required at compile (header search) for clang-cl and at link
# (lib search) for lld-link. add_compile_options / add_link_options route it
# to each context — at link, CMake's MSVC link rule expands <LINK_FLAGS> on
# the `vs_link_exe -- <linker> ...` line, so flags do reach the linker even
# though clang-cl isn't driving it.
export COMPILE_OPTIONS="/winsysroot:$XWIN_SYSROOT"
export LINK_OPTIONS="/winsysroot:$XWIN_SYSROOT"

export TOOLCHAIN_SYSTEM_NAME=Windows
export TOOLCHAIN_SYSTEM_PROCESSOR
export TOOLCHAIN_TARGET_TRIPLE="$TARGET_TRIPLE"
export TOOLCHAIN_SYSROOT="$SYSROOT"

TOOLCHAIN_FILE="$SYSROOT/Toolchain.cmake"
generate_toolchain_windows "$TOOLCHAIN_FILE"

# ===== Build third-party deps =====
echo "Building dependencies..."
export CMAKE_TOOLCHAIN_FILE="$TOOLCHAIN_FILE"

# CC/CXX matter only for the deps that go through autotools (currently just
# OpenSSL via the mingw64 target). CMake deps use the toolchain file, which
# overrides CC/CXX. The clang-msvc wrapper already bakes in /winsysroot via
# -isystem/-L, so no extra --target-cflags/--target-ldflags are needed.
export CC="$CLANG_MSVC_WRAPPER"
export CXX="$CLANG_MSVC_WRAPPER"

# sodium, mhd, libbacktrace still need separate handling on Linux→Windows
# (autotools-only; would need similar treatment to OpenSSL but their
# Configure scripts have additional Windows quirks). blst's custom build.sh
# also hasn't been validated against clang-cl yet.
"$SCRIPT_DIR/build-deps.sh" \
    --target="$TARGET_TRIPLE" \
    --source-dir="$SOURCE_DIR" \
    --build-dir="$BUILD_ROOT" \
    --tarballs-dir="$TARBALLS_DIR" \
    --nproc="$NPROC" \
    --prefix=/usr \
    --destdir="$SYSROOT" \
    openssl secp256k1 zlib lz4 crc32c rocksdb abseil ngtcp2

echo
echo "===== Windows sysroot created successfully ====="
echo "  Sysroot:   $SYSROOT"
echo "  Toolchain: $TOOLCHAIN_FILE"
echo
echo "To build TON:"
echo "  cmake -B build -DCMAKE_TOOLCHAIN_FILE=$TOOLCHAIN_FILE -DTON_DEPS_PREFIX=$SYSROOT/usr"
echo "  cmake --build build"
