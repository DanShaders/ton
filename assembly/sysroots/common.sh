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
# Exports CC, CXX, AR, RANLIB.
find_clang() {
    local version="$1"

    CC=$(find_tool_with_pattern "clang-$version" "clang version $version\." "${CC:-}" "clang-$version" "clang")
    CXX=$(find_tool_with_pattern "clang++-$version" "clang version $version\." "${CXX:-}" "clang++-$version" "clang++")
    RANLIB=$(find_tool_with_pattern "llvm-ranlib" "LLVM" "${RANLIB:-}" "llvm-ranlib" "llvm-ranlib-$version")
    AR=$(find_tool_with_pattern "llvm-ar" "LLVM" "${AR:-}" "llvm-ar" "llvm-ar-$version")

    export CC CXX RANLIB AR
}

# generate_toolchain <output_file> [extra_args...]
# Generates a CMake toolchain file from the template.
# Expects the following variables:
#   TOOLCHAIN_SYSTEM_NAME, TOOLCHAIN_SYSTEM_PROCESSOR, TOOLCHAIN_TARGET_TRIPLE,
#   TOOLCHAIN_SYSROOT, CC, CXX, AR, RANLIB
# The following arguments are optional:
#   COMBINED_C_FLAGS, COMBINED_CXX_FLAGS, COMBINED_ASM_FLAGS
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
        -e "s|@COMBINED_C_FLAGS@|${COMBINED_C_FLAGS:-}|g" \
        -e "s|@COMBINED_CXX_FLAGS@|${COMBINED_CXX_FLAGS:-}|g" \
        -e "s|@COMBINED_ASM_FLAGS@|${COMBINED_ASM_FLAGS:-}|g" \
        "$SCRIPT_DIR/Toolchain.cmake.in" > "$output"
}
