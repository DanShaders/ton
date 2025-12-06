#!/usr/bin/env bash
set -euo pipefail

# Creates a portable Linux sysroot for cross-compilation using musl libc.
#
# Usage: ./linux.sh <build_dir> <target> [options]
#   Targets: linux-x86_64, linux-aarch64
#   Options:
#     --sanitizer=<type>   Enable sanitizer (address, thread, undefined)
#     --arch=<march>       Architecture flag (e.g., native, x86-64-v3)

if [ $# -lt 2 ]; then
    echo "Usage: $0 <build_dir> <target> [--sanitizer=<type>] [--arch=<march>]"
    echo "  Targets: linux-x86_64, linux-aarch64"
    exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null && pwd)"
source "$SCRIPT_DIR/common.sh"

BUILD_DIR="$(mkdir -p "$1" && cd "$1" && pwd)"
TARGET_NAME="$2"
shift 2

# Parse options
SANITIZER=""
ARCH_FLAG=""
while [[ $# -gt 0 ]]; do
    case "$1" in
        --sanitizer=*) SANITIZER="${1#*=}" ;;
        --arch=*)      ARCH_FLAG="${1#*=}" ;;
        *) echo "Unknown option: $1"; exit 1 ;;
    esac
    shift
done

case "$TARGET_NAME" in
    linux-aarch64)
        TARGET_TRIPLE=aarch64-pc-linux-musl
        LINUX_ARCH=arm64
        LLVM_ARCH=AArch64
        TOOLCHAIN_SYSTEM_PROCESSOR=aarch64
        ;;
    linux-x86_64)
        TARGET_TRIPLE=x86_64-pc-linux-musl
        LINUX_ARCH=x86_64
        LLVM_ARCH=X86
        TOOLCHAIN_SYSTEM_PROCESSOR=x86_64
        ;;
    *)
        echo "Error: Unsupported target '$TARGET_NAME'"
        echo "  Supported: linux-x86_64, linux-aarch64"
        exit 1
        ;;
esac

# Find clang
find_clang "$LLVM_MAJOR_VERSION"

TARBALLS_DIR="$BUILD_DIR/tarballs"
BUILD_ROOT="$BUILD_DIR/sysroot-$TARGET_TRIPLE-build"
SYSROOT="$BUILD_DIR/sysroot-$TARGET_TRIPLE"

mkdir -p "$TARBALLS_DIR" "$BUILD_ROOT"

# ===== Download all sources =====
cd "$TARBALLS_DIR"
download_verified "$MUSL_URL" "musl-$MUSL_VERSION.tar.gz" "$MUSL_SHA256"
download_verified "$MUSL_PATCH1_URL" "musl-patch-1.patch" "$MUSL_PATCH1_SHA256"
download_verified "$MUSL_PATCH2_URL" "musl-patch-2.patch" "$MUSL_PATCH2_SHA256"
download_verified "$LINUX_HEADERS_URL" "linux-$LINUX_HEADERS_VERSION.tar.xz" "$LINUX_HEADERS_SHA256"
download_verified "$LLVM_URL" "llvm-project-$LLVM_VERSION.src.tar.xz" "$LLVM_SHA256"

# ===== Extract sources =====
cd "$BUILD_ROOT"

# It is faster to re-extract Linux kernel source than to try cleaning it...
rm -rf "linux-$LINUX_HEADERS_VERSION"
echo "Extracting linux-$LINUX_HEADERS_VERSION.tar.xz..."
tar xf "$TARBALLS_DIR/linux-$LINUX_HEADERS_VERSION.tar.xz"

MUSL_EXTRACTED=false
if [ ! -d "musl-$MUSL_VERSION" ]; then
    echo "Extracting musl-$MUSL_VERSION.tar.gz..."
    tar xf "$TARBALLS_DIR/musl-$MUSL_VERSION.tar.gz"
    MUSL_EXTRACTED=true
else
    make -C "musl-$MUSL_VERSION" clean
fi

if [ ! -d "llvm-project-$LLVM_VERSION.src" ]; then
    echo "Extracting llvm-project-$LLVM_VERSION.src.tar.xz..."
    tar xf "$TARBALLS_DIR/llvm-project-$LLVM_VERSION.src.tar.xz"
else
    rm -rf "llvm-project-$LLVM_VERSION.src/build-resource-headers"
    rm -rf "llvm-project-$LLVM_VERSION.src/build-builtins"
    rm -rf "llvm-project-$LLVM_VERSION.src/build-runtime"
fi

# ===== Build sysroot =====
rm -rf "$SYSROOT"
mkdir -p "$SYSROOT/usr/bin" "$SYSROOT/usr/lib"
ln -sf "$SYSROOT/usr/lib" "$SYSROOT/usr/lib64"

echo "Installing Linux kernel headers..."
(
    cd "linux-$LINUX_HEADERS_VERSION"
    make headers_install INSTALL_HDR_PATH="$SYSROOT/usr" ARCH="$LINUX_ARCH" "HOSTCC=$CC" -j"$NPROC"
)

# In order not to force users to build a specific distribution of Clang that is capable of
# cross-compiling to the *-pc-linux-musl targets, we will hijack the system Clang to become a
# cross-compiler. To do that, in theory, it is enough to compile and install compiler-rt and libc++
# to the sysroot. Clang (or GCC to that regard), however, won't be using compiler-rt and libc++ from
# the sysroot by default (with the official explanation being that "well, aktshually, usually
# sysroots have broken standard c++ library"). To counter this, we can pass -no-canonical-prefixes.
# Unfortunately, the flag will also prevent Clang from finding its bundled resource headers as it
# will try to search for them in sysroot. We _can_ fix this by separately passing --resource-dir
# but this will then break compiler-rt detection again for invocations with merged C{,XX}FLAGS and
# LDFLAGS that try to do compile & link in a single command. So, instead, we just employ the path of
# least resistance and install Clang resource headers into the sysroot as well.
echo "Installing Clang resource headers..."
(
    cd "llvm-project-$LLVM_VERSION.src"
    cmake -S llvm -B build-resource-headers \
        -DCMAKE_BUILD_TYPE=Release \
        -DLLVM_ENABLE_PROJECTS="clang" \
        -DCMAKE_INSTALL_PREFIX=/usr \
        "-DLLVM_TARGETS_TO_BUILD=$LLVM_ARCH"
    DESTDIR="$SYSROOT" cmake --build build-resource-headers --target install-clang-resource-headers
)

# Clang likes to find its bundled resources relative to its own path, so since we intend on
# installing everything into sysroot regardless, we symlink clang into sysroot. We can force clang
# to think it is installed there by passing -no-canonical-prefixes. On the other hand, the symlinked
# clang is a perfectly working host compiler as it internally resolves its real path if the
# mentioned flag is absent.
ln -sf "$CC" "$SYSROOT/usr/bin/clang-host"
CC="$SYSROOT/usr/bin/clang-host"
ln -sf "$CXX" "$SYSROOT/usr/bin/clang++-host"
CXX="$SYSROOT/usr/bin/clang++-host"
export CC CXX

echo "Installing musl headers..."
(
    cd "musl-$MUSL_VERSION"

    if [ "$MUSL_EXTRACTED" = true ]; then
        patch -p1 < "$TARBALLS_DIR/musl-patch-1.patch"
        patch -p1 < "$TARBALLS_DIR/musl-patch-2.patch"
    fi

    # We don't have compiler-rt builtins, so C compiler is non-functional. To compile the builtins,
    # we need C library headers. Musl provides a target to install headers but it is unfortunately
    # gated by ./configure that needs _a_ compiler, so here we create a best-effort clang invocation
    # that is good enough for `./configure`.
    CC="$CC -no-canonical-prefixes --target=$TARGET_TRIPLE --sysroot=$SYSROOT -fuse-ld=lld --rtlib=compiler-rt" \
    ./configure --prefix=/usr "--target=$TARGET_TRIPLE"
    DESTDIR="$SYSROOT" make install-headers -j"$NPROC"
)

# ===== Create initial CMake toolchain (needed for compiler-rt build) =====
TOOLCHAIN_FILE="$SYSROOT/Toolchain.cmake"

# To build compiler-rt, it is easier to create a proper CMake toolchain file already.
cat > "$TOOLCHAIN_FILE" <<TOOLCHAIN_EOF
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR $TOOLCHAIN_SYSTEM_PROCESSOR)

set(CMAKE_SYSROOT $SYSROOT)

set(CMAKE_C_COMPILER $CC)
set(CMAKE_C_COMPILER_TARGET $TARGET_TRIPLE)
set(CMAKE_CXX_COMPILER $CXX)
set(CMAKE_CXX_COMPILER_TARGET $TARGET_TRIPLE)
set(CMAKE_ASM_COMPILER $CC)
set(CMAKE_ASM_COMPILER_TARGET $TARGET_TRIPLE)
set(CMAKE_AR $AR)
set(CMAKE_RANLIB $RANLIB)

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE BOTH)

set(CMAKE_C_FLAGS_INIT "-no-canonical-prefixes")
set(CMAKE_CXX_FLAGS_INIT "-no-canonical-prefixes")
set(CMAKE_EXE_LINKER_FLAGS_INIT "-no-canonical-prefixes -rtlib=compiler-rt -fuse-ld=lld")
if (NOT TON_COMPILING_LIBCXX)
    set(CMAKE_CXX_FLAGS_INIT "\${CMAKE_CXX_FLAGS_INIT} -stdlib=libc++")
    set(CMAKE_EXE_LINKER_FLAGS_INIT "\${CMAKE_EXE_LINKER_FLAGS_INIT} -stdlib=libc++")
else()
    set(CMAKE_EXE_LINKER_FLAGS_INIT "\${CMAKE_EXE_LINKER_FLAGS_INIT} -nostdlib++")
endif()
set(CMAKE_SHARED_LINKER_FLAGS_INIT "\${CMAKE_EXE_LINKER_FLAGS_INIT}")
TOOLCHAIN_EOF

echo "Building compiler-rt builtins..."
(
    cd "llvm-project-$LLVM_VERSION.src"

    cmake -S runtimes -B build-builtins \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN_FILE" \
        -DCMAKE_INSTALL_PREFIX=/usr/lib/clang/$LLVM_MAJOR_VERSION \
        -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY \
        -DLLVM_ENABLE_RUNTIMES="compiler-rt" \
        -DCOMPILER_RT_DEFAULT_TARGET_ONLY=On \
        -DCOMPILER_RT_BUILD_SANITIZERS=Off \
        -DCOMPILER_RT_BUILD_XRAY=Off \
        -DCOMPILER_RT_BUILD_LIBFUZZER=Off \
        -DCOMPILER_RT_BUILD_PROFILE=Off \
        -DCOMPILER_RT_BUILD_CTX_PROFILE=Off \
        -DCOMPILER_RT_BUILD_MEMPROF=Off \
        -DCOMPILER_RT_BUILD_ORC=Off \
        -DCOMPILER_RT_BUILD_GWP_ASAN=Off \
        -DCOMPILER_RT_EXCLUDE_ATOMIC_BUILTIN=Off
    DESTDIR="$SYSROOT" cmake --build build-builtins --target install
)

echo "Building musl libc..."
(
    cd "musl-$MUSL_VERSION"

    # With compiler-rt builtins in place, we can finally properly configure and build musl.
    CC="$CC -no-canonical-prefixes --target=$TARGET_TRIPLE --sysroot=$SYSROOT -fuse-ld=lld -rtlib=compiler-rt" \
    ./configure --prefix=/usr "--target=$TARGET_TRIPLE"
    DESTDIR="$SYSROOT" make install -j"$NPROC"
)

echo "Building libc++ and libc++abi..."
(
    cd "llvm-project-$LLVM_VERSION.src"

    # And libc and builtins allow us to build C++ runtime libraries and libc++.
    # Since we don't want to pass -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY (it breaks symbol
    # detection, we did not care about this in builtins build but libc++ does care), we need to
    # additionally pass -nostdlib++ to linker to make it "work".
    cmake -S runtimes -B build-runtime \
        -DCMAKE_BUILD_TYPE=Release \
        -DTON_COMPILING_LIBCXX=On \
        -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN_FILE" \
        -DCMAKE_INSTALL_PREFIX="$SYSROOT/usr" \
        -DLLVM_ENABLE_RUNTIMES="libcxx;libcxxabi;libunwind" \
        -DLIBCXX_HAS_MUSL_LIBC=On \
        -DLIBUNWIND_USE_COMPILER_RT=On \
        -DLIBCXXABI_USE_COMPILER_RT=On \
        -DLIBCXX_USE_COMPILER_RT=On
    cmake --build build-runtime --target install
)

# ===== Write the final toolchain with full flags =====
CFLAGS="-no-canonical-prefixes"
CXXFLAGS="-no-canonical-prefixes -stdlib=libc++"
LDFLAGS="-no-canonical-prefixes -rtlib=compiler-rt -fuse-ld=lld -stdlib=libc++"

if [ -n "$SANITIZER" ]; then
    CFLAGS="$CFLAGS -fsanitize=$SANITIZER"
    CXXFLAGS="$CXXFLAGS -fsanitize=$SANITIZER"
    LDFLAGS="$LDFLAGS -fsanitize=$SANITIZER"
fi
if [ -n "$ARCH_FLAG" ]; then
    CFLAGS="$CFLAGS -march=$ARCH_FLAG"
    CXXFLAGS="$CXXFLAGS -march=$ARCH_FLAG"
fi

export TOOLCHAIN_SYSTEM_NAME=Linux
export TOOLCHAIN_SYSTEM_PROCESSOR
export TOOLCHAIN_TARGET_TRIPLE="$TARGET_TRIPLE"
export TOOLCHAIN_SYSROOT="$SYSROOT"
export TOOLCHAIN_EXTRA_C_FLAGS="$CFLAGS"
export TOOLCHAIN_EXTRA_CXX_FLAGS="$CXXFLAGS"
export TOOLCHAIN_EXTRA_LINKER_FLAGS="$LDFLAGS"
generate_toolchain "$TOOLCHAIN_FILE"

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
extra_flags="--sysroot=$SYSROOT --target=$TARGET_TRIPLE"
export CFLAGS="$extra_flags -O3 -Wall $CFLAGS"
export CXXFLAGS="$extra_flags -O3 -Wall -std=c++20 $CXXFLAGS"
export LDFLAGS="$extra_flags $LDFLAGS"

"$SCRIPT_DIR/build-deps.sh"
