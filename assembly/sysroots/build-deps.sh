#!/usr/bin/env bash
set -euo pipefail

# Builds all third-party dependencies into a prefix.
#
# Required arguments (cmdline only):
#   --target=<triple>          e.g. x86_64-pc-linux-musl, x86_64-pc-windows-msvc
#   --source-dir=<path>        ton/src (for vendored sources in third-party/)
#   --build-dir=<path>         scratch build directory
#   --tarballs-dir=<path>      where to download tarballs
#
# Optional arguments (cmdline; --prefix/--destdir also honor env):
#   --prefix=<path>            install prefix (default: /usr/local; env: PREFIX)
#   --destdir=<path>           filesystem root for install (default: ''; env: DESTDIR)
#   --nproc=<n>                parallel jobs (default: nproc / hw.ncpu / 4)
#   --target-cflags=<flags>    target-specific cflags, prepended to $CFLAGS /
#                              $CXXFLAGS for autotools deps (CMake deps get
#                              their target wiring from the toolchain file
#                              instead, so this is not forwarded there)
#   --target-ldflags=<flags>   ditto for $LDFLAGS
#
# Build environment (passed through unchanged to build systems):
#   CC, CXX, AR, RANLIB
#   CFLAGS, CXXFLAGS, CPPFLAGS, LDFLAGS
#   CMAKE_TOOLCHAIN_FILE       toolchain file for CMake deps
#
# Usage: ./build-deps.sh <required args> [optional args] [dep1 dep2 ...]
#        With no positional deps, all deps are built.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null && pwd)"
source "$SCRIPT_DIR/common.sh"

# ===== Argument parsing =====
TARGET=""
SOURCE_DIR=""
BUILD_DIR=""
TARBALLS_DIR=""
NPROC=""
TARGET_CFLAGS=""
TARGET_LDFLAGS=""
prefix_arg=""
destdir_arg=""
DEPS_TO_BUILD=()

while [[ $# -gt 0 ]]; do
    case "$1" in
        --target=*)         TARGET="${1#*=}" ;;
        --source-dir=*)     SOURCE_DIR="${1#*=}" ;;
        --build-dir=*)      BUILD_DIR="${1#*=}" ;;
        --tarballs-dir=*)   TARBALLS_DIR="${1#*=}" ;;
        --nproc=*)          NPROC="${1#*=}" ;;
        --prefix=*)         prefix_arg="${1#*=}" ;;
        --destdir=*)        destdir_arg="${1#*=}" ;;
        --target-cflags=*)  TARGET_CFLAGS="${1#*=}" ;;
        --target-ldflags=*) TARGET_LDFLAGS="${1#*=}" ;;
        --*)                echo "Unknown option: $1" >&2; exit 1 ;;
        *)                  DEPS_TO_BUILD+=("$1") ;;
    esac
    shift
done

# Cmdarg wins over env, env wins over default.
PREFIX="${prefix_arg:-${PREFIX:-/usr/local}}"
DESTDIR="${destdir_arg:-${DESTDIR:-}}"
NPROC="${NPROC:-$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)}"

export DESTDIR

[ -n "$TARGET" ]       || { echo "ERROR: --target is required" >&2; exit 1; }
[ -n "$SOURCE_DIR" ]   || { echo "ERROR: --source-dir is required" >&2; exit 1; }
[ -n "$BUILD_DIR" ]    || { echo "ERROR: --build-dir is required" >&2; exit 1; }
[ -n "$TARBALLS_DIR" ] || { echo "ERROR: --tarballs-dir is required" >&2; exit 1; }

TARGET_TRIPLE="$TARGET"
unset HOST TARGET

SOURCE_DIR="$(cd "$SOURCE_DIR" >/dev/null && pwd)"
mkdir -p "$BUILD_DIR" "$TARBALLS_DIR"
BUILD_DIR="$(cd "$BUILD_DIR" >/dev/null && pwd)"
TARBALLS_DIR="$(cd "$TARBALLS_DIR" >/dev/null && pwd)"
if [ -n "$DESTDIR" ]; then
    mkdir -p "$DESTDIR"
    DESTDIR="$(cd "$DESTDIR" >/dev/null && pwd)"
fi

THIRD_PARTY="$SOURCE_DIR/third-party"

# Autotools deps merge target-specific flags into the inherited CFLAGS/LDFLAGS.
# Call inside a subshell so the merge doesn't leak to other deps.
apply_target_flags() {
    export CFLAGS="${TARGET_CFLAGS} ${CFLAGS:-}"
    export CXXFLAGS="${TARGET_CFLAGS} ${CXXFLAGS:-}"
    export LDFLAGS="${TARGET_LDFLAGS} ${LDFLAGS:-}"
}

# prepare_source <name> <version> <url> <sha256> [prepare_tarball options...]
# Downloads, verifies, and extracts a tarball into TARBALLS_DIR alongside the
# tarball itself (the extracted source is part of the download cache). Applies
# any patches passed via --patch=... and sets PREPARED_SRC (see prepare_tarball).
prepare_source() {
    local name="$1" version="$2" url="$3" sha256="$4"
    shift 4
    local tarball="$TARBALLS_DIR/${name}-${version}.tar.gz"

    (
        cd "$TARBALLS_DIR"
        download_verified "$url" "$tarball" "$sha256"
    )

    prepare_tarball "$TARBALLS_DIR/${name}-${version}" "$tarball" "$@"
}

# ===== CMake helper =====
cmake_build_install() {
    local name="$1" src="$2"
    shift 2

    echo "=== Building $name ==="
    (
        clean_and_enter "$BUILD_DIR/$name"
        cmake -S "$src" -B . \
            -DCMAKE_BUILD_TYPE=Release \
            -DCMAKE_INSTALL_PREFIX="$PREFIX" \
            -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
            "$@"
        cmake --build . --parallel "$NPROC"
        cmake --install .
    )
}

# ===========================================================================
# Dependencies
# ===========================================================================

# ===== OpenSSL =====
build_openssl() {
    echo "=== Building OpenSSL ${OPENSSL_VERSION} ==="

    prepare_source openssl "$OPENSSL_VERSION" "$OPENSSL_URL" "$OPENSSL_SHA256"
    # OpenSSL's ./Configure refuses to build out-of-tree, so give it a private
    # copy in BUILD_DIR rather than letting it scribble on the cached source.
    rm -rf "$BUILD_DIR/openssl-build"
    cp -a "$PREPARED_SRC" "$BUILD_DIR/openssl-build"
    (
        cd "$BUILD_DIR/openssl-build"
        apply_target_flags

        local configure_target=""
        local extra_args=""
        case "$TARGET_TRIPLE" in
            *-linux-musl*|*-linux-gnu*)
                if [[ "$TARGET_TRIPLE" == aarch64-* ]]; then
                    configure_target="linux-aarch64"
                else
                    configure_target="linux-x86_64"
                fi
                ;;
            *-apple-darwin*)
                if [[ "$TARGET_TRIPLE" == arm64-* || "$TARGET_TRIPLE" == aarch64-* ]]; then
                    configure_target="darwin64-arm64-cc"
                else
                    configure_target="darwin64-x86_64-cc"
                fi
                ;;
            *-windows-msvc*|*-pc-windows-msvc*)
                # OpenSSL's VC-WIN64A target wants nmake and a Windows-style perl,
                # neither of which we have when cross-compiling from Linux. The
                # mingw64 target produces a GNU Makefile that runs fine on Linux;
                # we get MSVC ABI by routing CC through the clang-msvc wrapper
                # generated by windows.sh (plain clang in --target=...-msvc mode
                # plus the ABI-relevant -D_MT/-D_DLL/--dependent-lib bits that
                # clang-cl normally adds for us). We also have to skip the Windows-
                # only artifacts (DLL providers, fuzz .exes) by limiting the build
                # to libcrypto.a / libssl.a and using `install_dev` instead of
                # `install_sw`.
                configure_target="mingw64"
                ;;
            *-mingw*)
                configure_target="mingw64"
                extra_args="-DSIO_UDP_NETRESET=SIO_UDP_CONNRESET"
                ;;
            *-android*)
                if [[ "$TARGET_TRIPLE" == aarch64-* || "$TARGET_TRIPLE" == arm64-* ]]; then
                    configure_target="android-arm64"
                elif [[ "$TARGET_TRIPLE" == arm-* || "$TARGET_TRIPLE" == armv7* ]]; then
                    configure_target="android-arm"
                elif [[ "$TARGET_TRIPLE" == x86_64-* ]]; then
                    configure_target="android-x86_64"
                elif [[ "$TARGET_TRIPLE" == i686-* || "$TARGET_TRIPLE" == x86-* ]]; then
                    configure_target="android-x86"
                fi
                ;;
            *-emscripten*|*-wasm*)
                configure_target="linux-generic32"
                extra_args="no-asm no-threads"
                ;;
            *)
                echo "ERROR: Cannot determine OpenSSL target for $TARGET_TRIPLE"
                exit 1
                ;;
        esac

        local configure_cmd="./Configure"

        if [[ "$TARGET_TRIPLE" == *-emscripten* || "$TARGET_TRIPLE" == *-wasm* ]]; then
            configure_cmd="emconfigure ./Configure"
        fi

        $configure_cmd $configure_target \
            --prefix="$PREFIX" --openssldir="/nonexistentdir" --release \
            no-shared no-dso no-unit-test no-tests no-apps enable-quic $extra_args

        # OpenSSL's Makefile uses `=` (not `?=`) for DESTDIR / RC, so env
        # values don't propagate — pass them as make args on the install lines.
        case "$TARGET_TRIPLE" in
            *-emscripten*|*-wasm*)
                emmake make depend
                emmake make -j"$NPROC"
                make "DESTDIR=$DESTDIR" install_sw -j"$NPROC"
                ;;
            *-windows-msvc*|*-pc-windows-msvc*)
                # build_libs stops at libcrypto.a / libssl.a (skips the .exe and
                # .dll artifacts that don't link cleanly on Linux→Windows).
                make RC=/usr/bin/llvm-windres build_libs -j"$NPROC"
                make RC=/usr/bin/llvm-windres "DESTDIR=$DESTDIR" install_dev -j"$NPROC"
                ;;
            *)
                make -j"$NPROC"
                make "DESTDIR=$DESTDIR" install_sw -j"$NPROC"
                ;;
        esac
    )
}

# ===== libsodium =====
build_sodium() {
    echo "=== Building libsodium ${LIBSODIUM_VERSION} ==="

    prepare_source libsodium "$LIBSODIUM_VERSION" "$LIBSODIUM_URL" "$LIBSODIUM_SHA256"
    local src="$PREPARED_SRC"
    (
        clean_and_enter "$BUILD_DIR/libsodium-build"
        apply_target_flags

        local configure_args="--prefix=$PREFIX --with-pic --enable-static --disable-shared"

        case "$TARGET_TRIPLE" in
            *-emscripten*|*-wasm*)
                emconfigure "$src/configure" $configure_args --disable-ssp
                emmake make -j"$NPROC"
                emmake make install
                exit 0
                ;;
        esac

        if [ -n "$TARGET_TRIPLE" ]; then
            configure_args="$configure_args --host=$TARGET_TRIPLE"
        fi

        "$src/configure" $configure_args
        make -j"$NPROC"
        make install
    )
}

# ===== libmicrohttpd =====
build_mhd() {
    echo "=== Building libmicrohttpd ${LIBMICROHTTPD_VERSION} ==="

    if [[ "$TARGET_TRIPLE" == *-emscripten* || "$TARGET_TRIPLE" == *-wasm* ]]; then
        echo "Skipping libmicrohttpd (not supported on Emscripten)"
        return
    fi

    prepare_source libmicrohttpd "$LIBMICROHTTPD_VERSION" "$LIBMICROHTTPD_URL" "$LIBMICROHTTPD_SHA256"
    local src="$PREPARED_SRC"
    (
        clean_and_enter "$BUILD_DIR/libmicrohttpd-build"
        apply_target_flags

        local configure_args="--prefix=$PREFIX --enable-static --disable-shared --disable-tests --disable-benchmark --disable-https --with-pic --disable-doc"

        if [ -n "$TARGET_TRIPLE" ]; then
            configure_args="$configure_args --host=$TARGET_TRIPLE"
        fi

        "$src/configure" $configure_args
        make -j"$NPROC"
        make install
    )
}

# ===== BLST =====
build_blst() {
    echo "=== Building BLST ==="

    local blst_src="$SOURCE_DIR/third-party/blst"
    (
        clean_and_enter "$BUILD_DIR/blst-build"
        apply_target_flags

        case "$TARGET_TRIPLE" in
            *-emscripten*|*-wasm*)
                emcc -O2 -fno-builtin -fPIC -Wall -Wextra -Werror -D__BLST_NO_ASM__ \
                    -c "$blst_src/src/server.c" -o server.o
                emar rcs libblst.a server.o
                emranlib libblst.a
                ;;
            *)
                "$blst_src/build.sh"
                ;;
        esac

        mkdir -p "$DESTDIR$PREFIX/lib" "$DESTDIR$PREFIX/include"
        cp libblst.a "$DESTDIR$PREFIX/lib/"
        cp "$blst_src/bindings/blst.h" "$DESTDIR$PREFIX/include/"
        cp "$blst_src/bindings/blst_aux.h" "$DESTDIR$PREFIX/include/"
        cp -r "$blst_src/bindings/blst.hpp" "$DESTDIR$PREFIX/include/" 2>/dev/null || true
    )
}

# ===== secp256k1 =====
build_secp256k1() {
    cmake_build_install secp256k1 "$THIRD_PARTY/secp256k1" \
        -DSECP256K1_ENABLE_MODULE_RECOVERY=ON \
        -DSECP256K1_BUILD_BENCHMARK=OFF \
        -DSECP256K1_BUILD_TESTS=OFF \
        -DSECP256K1_BUILD_EXHAUSTIVE_TESTS=OFF \
        -DBUILD_SHARED_LIBS=OFF
}

# ===== zlib =====
build_zlib() {
    cmake_build_install zlib "$THIRD_PARTY/zlib" \
        -DZLIB_BUILD_EXAMPLES=OFF
}

# ===== lz4 =====
build_lz4() {
    cmake_build_install lz4 "$THIRD_PARTY/lz4/build/cmake" \
        -DLZ4_BUILD_CLI=OFF \
        -DLZ4_BUILD_LEGACY_LZ4C=OFF \
        -DBUILD_SHARED_LIBS=OFF \
        -DBUILD_STATIC_LIBS=ON \
        -DLZ4_BUNDLED_MODE=ON \
        -DCMAKE_POLICY_VERSION_MINIMUM=3.10
}

# ===== libbacktrace =====
build_libbacktrace() {
    case "$TARGET_TRIPLE" in
        *-android*|*-emscripten*|*-wasm*)
            echo "Skipping libbacktrace (not supported on $TARGET_TRIPLE)"
            return
            ;;
    esac

    local src="$THIRD_PARTY/libbacktrace"
    (
        clean_and_enter "$BUILD_DIR/libbacktrace"
        apply_target_flags

        local configure_args="--prefix=$PREFIX --enable-static --disable-shared --with-pic"
        if [ -n "$TARGET_TRIPLE" ]; then
            configure_args="$configure_args --host=$TARGET_TRIPLE"
        fi

        "$src/configure" $configure_args
        make -j"$NPROC"
        make install
    )
}

# ===== crc32c =====
build_crc32c() {
    echo "=== Building crc32c ${CRC32C_VERSION} ==="

    prepare_source crc32c "$CRC32C_VERSION" "$CRC32C_URL" "$CRC32C_SHA256" \
        --patch="$SCRIPT_DIR/patches/crc32c-clang-cl-msvc-sim.patch"

    cmake_build_install crc32c "$PREPARED_SRC" \
        -DCRC32C_BUILD_TESTS=OFF \
        -DCRC32C_BUILD_BENCHMARKS=OFF \
        -DCRC32C_USE_GLOG=OFF \
        -DCRC32C_INSTALL=ON \
        -DCMAKE_POLICY_VERSION_MINIMUM=3.10
}

# ===== RocksDB =====
build_rocksdb() {
    local portable_arg=()
    case "$TARGET_TRIPLE" in
        *-android*) portable_arg=(-DPORTABLE=ON) ;;
    esac

    cmake_build_install rocksdb "$THIRD_PARTY/rocksdb" \
        -DWITH_GFLAGS=OFF \
        -DWITH_TESTS=OFF \
        -DWITH_TOOLS=OFF \
        -DWITH_BENCHMARK_TOOLS=OFF \
        -DWITH_CORE_TOOLS=OFF \
        -DUSE_RTTI=ON \
        -DFAIL_ON_WARNINGS=OFF \
        -DROCKSDB_INSTALL_ON_WINDOWS=ON \
        "${portable_arg[@]}"
}

# ===== Abseil =====
build_abseil() {
    cmake_build_install abseil "$THIRD_PARTY/abseil-cpp" \
        -DABSL_PROPAGATE_CXX_STD=ON \
        -DABSL_BUILD_TESTING=OFF \
        -DABSL_ENABLE_INSTALL=ON
}

# ===== ngtcp2 =====
build_ngtcp2() {
    cmake_build_install ngtcp2 "$THIRD_PARTY/ngtcp2" \
        -DBUILD_TESTING=OFF \
        -DENABLE_SHARED_LIB=OFF \
        -DENABLE_STATIC_LIB=ON
}

# ===== Main =====
ALL_DEPS=(openssl sodium mhd blst secp256k1 zlib lz4 libbacktrace crc32c rocksdb abseil ngtcp2)

if [ ${#DEPS_TO_BUILD[@]} -eq 0 ]; then
    DEPS_TO_BUILD=("${ALL_DEPS[@]}")
fi

for dep in "${DEPS_TO_BUILD[@]}"; do
    case "$dep" in
        openssl)        build_openssl ;;
        sodium)         build_sodium ;;
        mhd)            build_mhd ;;
        blst)           build_blst ;;
        secp256k1)      build_secp256k1 ;;
        zlib)           build_zlib ;;
        lz4)            build_lz4 ;;
        libbacktrace)   build_libbacktrace ;;
        crc32c)         build_crc32c ;;
        rocksdb)        build_rocksdb ;;
        abseil)         build_abseil ;;
        ngtcp2)         build_ngtcp2 ;;
        *)
            echo "ERROR: Unknown dependency '$dep'"
            echo "Available: ${ALL_DEPS[*]}"
            exit 1
            ;;
    esac
done

echo "=== All dependencies built successfully ==="
