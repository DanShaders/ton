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
            local version
            version=$("$path" --version 2>/dev/null | head -n1)
            if echo "$version" | grep -q "$pattern"; then
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
# Sets CC, CXX, AR, RANLIB.
find_clang() {
    local version="$1"

    CC=$(find_tool_with_pattern "clang-$version" "clang version $version\." "${CC:-}" "clang-$version" "clang")
    CXX=$(find_tool_with_pattern "clang++-$version" "clang version $version\." "${CXX:-}" "clang++-$version" "clang++")
    RANLIB=$(find_tool_with_pattern "llvm-ranlib" "LLVM version $version\." "${RANLIB:-}" "llvm-ranlib" "llvm-ranlib-$version")
    AR=$(find_tool_with_pattern "llvm-ar" "LLVM version $version\." "${AR:-}" "llvm-ar" "llvm-ar-$version")

    export CC CXX RANLIB AR
}

find_clang_cl() {
    local version="$1"

    CC=$(find_tool_with_pattern "clang-cl-$version" "clang version $version\." "clang-cl-$version" "clang-cl")
    CXX=$CC
    AR=$(find_tool_with_pattern "llvm-lib" "" "llvm-lib" "llvm-lib-$version")
    LINKER=$(find_tool_with_pattern "lld-link" "LLD $version." "lld-link" "lld-link-$version")

    export CC CXX AR LINKER
}

# generate_toolchain <output_file>
# Generates a Unix CMake toolchain file from ToolchainUnix.cmake.in.
# Expects: TOOLCHAIN_SYSTEM_NAME, TOOLCHAIN_SYSTEM_PROCESSOR, TOOLCHAIN_TARGET_TRIPLE,
#   TOOLCHAIN_SYSROOT, CC, CXX, AR, RANLIB
#   Optional: TOOLCHAIN_EXTRA_C_FLAGS, TOOLCHAIN_EXTRA_CXX_FLAGS, TOOLCHAIN_EXTRA_LINKER_FLAGS
generate_toolchain() {
    local output="$1"

    sed -e "s|@CMAKE_SYSTEM_NAME@|${TOOLCHAIN_SYSTEM_NAME}|g" \
        -e "s|@CMAKE_SYSTEM_PROCESSOR@|${TOOLCHAIN_SYSTEM_PROCESSOR}|g" \
        -e "s|@TARGET_TRIPLE@|${TOOLCHAIN_TARGET_TRIPLE}|g" \
        -e "s|@SYSROOT_PATH@|${TOOLCHAIN_SYSROOT}|g" \
        -e "s|@CC@|${CC}|g" \
        -e "s|@CXX@|${CXX}|g" \
        -e "s|@AR@|${AR}|g" \
        -e "s|@RANLIB@|${RANLIB:-}|g" \
        -e "s|@EXTRA_C_FLAGS@|${TOOLCHAIN_EXTRA_C_FLAGS:-}|g" \
        -e "s|@EXTRA_CXX_FLAGS@|${TOOLCHAIN_EXTRA_CXX_FLAGS:-}|g" \
        -e "s|@EXTRA_LINKER_FLAGS@|${TOOLCHAIN_EXTRA_LINKER_FLAGS:-}|g" \
        "$SCRIPT_DIR/ToolchainUnix.cmake.in" > "$output"
}

# generate_toolchain_windows <output_file>
# Generates a Windows CMake toolchain file from ToolchainWindows.cmake.in.
# Expects: TOOLCHAIN_SYSTEM_NAME, TOOLCHAIN_SYSTEM_PROCESSOR, TOOLCHAIN_TARGET_TRIPLE,
#   TOOLCHAIN_SYSROOT, CC, CXX, AR, LINKER
#   Optional: TOOLCHAIN_EXTRA_C_FLAGS, TOOLCHAIN_EXTRA_CXX_FLAGS, TOOLCHAIN_EXTRA_LINKER_FLAGS
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
        -e "s|@EXTRA_C_FLAGS@|${TOOLCHAIN_EXTRA_C_FLAGS:-}|g" \
        -e "s|@EXTRA_RC_FLAGS@|${TOOLCHAIN_EXTRA_RC_FLAGS:-}|g" \
        -e "s|@EXTRA_CXX_FLAGS@|${TOOLCHAIN_EXTRA_CXX_FLAGS:-}|g" \
        -e "s|@EXTRA_LINKER_FLAGS@|${TOOLCHAIN_EXTRA_LINKER_FLAGS:-}|g" \
        "$SCRIPT_DIR/ToolchainWindows.cmake.in" > "$output"
}
