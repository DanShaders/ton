#!/usr/bin/env bash
# Shared utility functions for sysroot creation scripts.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null && pwd)"
SOURCE_DIR="$(cd "$SCRIPT_DIR/../.." >/dev/null && pwd)"

source "$SCRIPT_DIR/versions.sh"

NPROC=$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)

# download_verified <url> <output_file> <sha256>
# Downloads a file and verifies its SHA256 checksum.
download_verified() {
    local url="$1" file="$2" expected_sha256="$3"

    if [ -f "$file" ]; then
        local actual
        actual=$(sha256sum "$file" | cut -d' ' -f1)
        if [ "$actual" = "$expected_sha256" ]; then
            return 0
        fi
        rm -f "$file"
    fi

    echo "Downloading $url..."
    curl -L --fail --output "$file" "$url"

    local actual
    actual=$(sha256sum "$file" | cut -d' ' -f1)
    if [ "$actual" != "$expected_sha256" ]; then
        echo "ERROR: Checksum mismatch for $file"
        echo "  Expected: $expected_sha256"
        echo "  Actual:   $actual"
        rm -f "$file"
        return 1
    fi
}

# prepare_tarball <base_dir> <tarball> [--patch=<file>]... [--clean=<cmd>]
# Idempotently extracts <tarball> (with --strip-components=1) into
# "<base_dir>-<patch-hash>" and applies the given patches. The hash is a short
# sha256 of `sha256sum <patches...>`, so concurrent runs from branches with
# different patch sets get distinct directories. Sets the global PREPARED_SRC
# to the resulting path.
#
# On success, writes the full patch fingerprint to <PREPARED_SRC>/.prepared.
# Subsequent calls with the same patches skip extraction (and run --clean, if
# given, inside <PREPARED_SRC> to reset build artifacts). Any failure before
# .prepared is written causes the next call to re-extract from scratch.
# <cmd> is run via `bash -c` from within <PREPARED_SRC>.
prepare_tarball() {
    local base_dir="$1" tarball="$2"
    shift 2
    local patches=() clean_cmd=""
    while [[ $# -gt 0 ]]; do
        case "$1" in
            --patch=*) patches+=("${1#*=}") ;;
            --clean=*) clean_cmd="${1#*=}" ;;
            *) echo "prepare_tarball: unknown arg: $1" >&2; return 1 ;;
        esac
        shift
    done

    local fingerprint=""
    if [ ${#patches[@]} -gt 0 ]; then
        fingerprint=$(sha256sum "${patches[@]}")
    fi
    local hash
    hash=$(printf '%s' "$fingerprint" | sha256sum | cut -c1-12)
    PREPARED_SRC="$base_dir-$hash"

    if [ -f "$PREPARED_SRC/.prepared" ] && [ "$(cat "$PREPARED_SRC/.prepared")" = "$fingerprint" ]; then
        if [ -n "$clean_cmd" ]; then
            (cd "$PREPARED_SRC" && bash -c "$clean_cmd")
        fi
        return 0
    fi

    rm -rf "$PREPARED_SRC"
    mkdir -p "$PREPARED_SRC"
    echo "Extracting $(basename "$tarball")..."
    tar xf "$tarball" -C "$PREPARED_SRC" --strip-components=1
    local p
    for p in "${patches[@]}"; do
        echo "Applying $(basename "$p")..."
        patch -p1 -d "$PREPARED_SRC" < "$p"
    done
    printf '%s' "$fingerprint" > "$PREPARED_SRC/.prepared"
}

# clean_and_enter <dir>
# Wipes <dir> (if any), recreates it empty, and changes the current shell's
# working directory to it. Intended for use inside a subshell so the cd doesn't
# leak — each consumer ends up with a fresh build directory at $PWD and can use
# "." for build-dir args (cmake -B, cmake --build, etc.).
clean_and_enter() {
    local dir="$1"
    rm -rf "$dir"
    mkdir -p "$dir"
    cd "$dir"
}

find_tool_with_pattern() {
    local name="$1"
    local pattern="$2"
    shift 2
    local candidates=("$@")

    for candidate in "${candidates[@]}"; do
        if [ -z "$candidate" ]; then
            continue
        fi
        local path
        path=$(command -v "$candidate" 2>/dev/null)
        if [ -n "$path" ]; then
            if "$path" --version 2>/dev/null | grep -q "$pattern"; then
                echo "Using $path for $name." >&2
                echo "$path"
                return 0
            fi
        fi
    done

    echo "ERROR: Could not find $name, tried: ${candidates[*]}." >&2
    return 1
}

# find_clang <major_version>
# Finds clang/clang++/llvm-ar/llvm-ranlib with the given major version.
# Exports CC, CXX, AR, RANLIB.
find_clang() {
    local version="$1"

    CC=$(find_tool_with_pattern "clang-$version" "clang version $version\." "${CC:-}" "clang-$version" "clang")
    CXX=$(find_tool_with_pattern "clang++-$version" "clang version $version\." "${CXX:-}" "clang++-$version" "clang++")
    RANLIB=$(find_tool_with_pattern "llvm-ranlib" "LLVM version $version\." "${RANLIB:-}" "llvm-ranlib" "llvm-ranlib-$version")
    AR=$(find_tool_with_pattern "llvm-ar" "LLVM version $version\." "${AR:-}" "llvm-ar" "llvm-ar-$version")

    export CC CXX RANLIB AR
}

# find_clang_cl <major_version>
# Finds clang-cl + llvm-lib for cross-compiling to Windows from Linux. Same
# binary handles C and CXX. No RANLIB on Windows (llvm-lib does the job of
# both ar and ranlib in one go).
find_clang_cl() {
    local version="$1"

    CC=$(find_tool_with_pattern "clang-cl-$version" "clang version $version\." "clang-cl-$version" "clang-cl")
    CXX=$CC
    AR=$(find_tool_with_pattern "llvm-lib" "" "llvm-lib" "llvm-lib-$version")
    LINKER=$(find_tool_with_pattern "lld-link" "LLD $version." "lld-link" "lld-link-$version")

    export CC CXX AR LINKER
}

# generate_toolchain <output_file>
# Generates a Unix-flavored CMake toolchain file from Toolchain.cmake.in.
# Expects: TOOLCHAIN_SYSTEM_NAME, TOOLCHAIN_SYSTEM_PROCESSOR, TOOLCHAIN_TARGET_TRIPLE,
#   TOOLCHAIN_SYSROOT, CC, CXX, AR, RANLIB
# Optional: COMPILE_OPTIONS, LINK_OPTIONS
generate_toolchain() {
    local output="$1"

    sed -e "s|@CMAKE_SYSTEM_NAME@|${TOOLCHAIN_SYSTEM_NAME}|g" \
        -e "s|@CMAKE_SYSTEM_PROCESSOR@|${TOOLCHAIN_SYSTEM_PROCESSOR}|g" \
        -e "s|@TARGET_TRIPLE@|${TOOLCHAIN_TARGET_TRIPLE}|g" \
        -e "s|@SYSROOT_PATH@|${TOOLCHAIN_SYSROOT}|g" \
        -e "s|@CC@|${CC}|g" \
        -e "s|@CXX@|${CXX}|g" \
        -e "s|@AR@|${AR}|g" \
        -e "s|@RANLIB@|${RANLIB}|g" \
        -e "s|@COMPILE_OPTIONS@|${COMPILE_OPTIONS:-}|g" \
        -e "s|@LINK_OPTIONS@|${LINK_OPTIONS:-}|g" \
        "$SCRIPT_DIR/ToolchainUnix.cmake.in" > "$output"
}

# generate_toolchain_windows <output_file>
# Generates a clang-cl + lld-link CMake toolchain file from
# ToolchainWindows.cmake.in.
# Expects: TOOLCHAIN_SYSTEM_NAME, TOOLCHAIN_SYSTEM_PROCESSOR,
#   TOOLCHAIN_TARGET_TRIPLE, TOOLCHAIN_SYSROOT, CC, CXX, AR, LINKER
# Optional: COMPILE_OPTIONS, LINK_OPTIONS
generate_toolchain_windows() {
    local output="$1"

    sed -e "s|@CMAKE_SYSTEM_NAME@|${TOOLCHAIN_SYSTEM_NAME}|g" \
        -e "s|@CMAKE_SYSTEM_PROCESSOR@|${TOOLCHAIN_SYSTEM_PROCESSOR}|g" \
        -e "s|@TARGET_TRIPLE@|${TOOLCHAIN_TARGET_TRIPLE}|g" \
        -e "s|@SYSROOT_PATH@|${TOOLCHAIN_SYSROOT}|g" \
        -e "s|@CC@|${CC}|g" \
        -e "s|@CXX@|${CXX}|g" \
        -e "s|@AR@|${AR}|g" \
        -e "s|@LINKER@|${LINKER}|g" \
        -e "s|@COMPILE_OPTIONS@|${COMPILE_OPTIONS:-}|g" \
        -e "s|@LINK_OPTIONS@|${LINK_OPTIONS:-}|g" \
        "$SCRIPT_DIR/ToolchainWindows.cmake.in" > "$output"
}
