# ton_add_rust_crate(NAME crate_name MANIFEST_PATH path/to/Cargo.toml [FEATURES f1 f2 ...])
#
# Builds a Rust static-library crate via cargo and exposes it as a STATIC IMPORTED
# CMake target. MANIFEST_PATH is relative to CMAKE_CURRENT_SOURCE_DIR.
#
# Adapted from Ladybird's Meta/CMake/rust_crate.cmake (BSD-2-Clause). Differences:
#   - drops the cbindgen FFI-header sync helper (TON's FFI surfaces are small
#     enough to maintain by hand and review on changes)
#   - no workspace-Cargo.lock walking (TON crates are self-contained)
#   - one shared cargo target dir per build, partitioned by triple/profile,
#     so multiple ton_add_rust_crate() callers don't fight over target/
#
# Why DEPFILE: cargo emits a Make-style depfile via `--emit=dep-info`, which
# CMake/ninja can consume to track .rs and dependency-crate sources without
# us having to GLOB anything from CMake (which would never refresh on
# Cargo.lock-driven dep additions anyway).
function(ton_add_rust_crate)
    # CMP0116 (CMake 3.20+) makes Ninja transform DEPFILE paths from build-
    # relative to top-build-dir-relative. Cargo emits absolute paths in its
    # depfile so either policy works in practice, but we explicitly opt into
    # the new behavior (a) to silence the configure warning and (b) so this
    # helper keeps working when the project bumps cmake_minimum_required past
    # the policy's introduction. Function-scoped — doesn't leak.
    cmake_policy(SET CMP0116 NEW)

    cmake_parse_arguments(PARSE_ARGV 0 ARG "" "NAME;MANIFEST_PATH" "FEATURES")
    if(NOT ARG_NAME)
        message(FATAL_ERROR "ton_add_rust_crate: NAME is required")
    endif()
    if(NOT ARG_MANIFEST_PATH)
        message(FATAL_ERROR "ton_add_rust_crate: MANIFEST_PATH is required")
    endif()

    set(manifest_abs "${CMAKE_CURRENT_SOURCE_DIR}/${ARG_MANIFEST_PATH}")
    get_filename_component(crate_dir "${manifest_abs}" DIRECTORY)

    find_program(TON_CARGO cargo REQUIRED)
    find_program(TON_RUSTC rustc REQUIRED)

    if(NOT DEFINED CACHE{TON_RUST_HOST_TRIPLE})
        execute_process(
            COMMAND "${TON_RUSTC}" -vV
            OUTPUT_VARIABLE _rustc_verbose
            RESULT_VARIABLE _rustc_res
        )
        if(NOT _rustc_res EQUAL 0)
            message(FATAL_ERROR "ton_add_rust_crate: failed to query rustc -vV")
        endif()
        string(REGEX MATCH "host: ([^\n]+)" _ "${_rustc_verbose}")
        string(STRIP "${CMAKE_MATCH_1}" _host_triple)
        set(TON_RUST_HOST_TRIPLE "${_host_triple}" CACHE INTERNAL "Rust host target triple")
    endif()
    set(rust_target "${TON_RUST_HOST_TRIPLE}")

    string(REPLACE "-" "_" target_underscore "${rust_target}")
    string(TOUPPER "${target_underscore}" target_upper)

    string(TOUPPER "${CMAKE_BUILD_TYPE}" build_type_upper)
    if(build_type_upper STREQUAL "DEBUG")
        set(cargo_profile_flag "")
        set(cargo_profile_dir "debug")
    else()
        set(cargo_profile_flag "--release")
        set(cargo_profile_dir "release")
    endif()

    set(cargo_features "")
    if(ARG_FEATURES)
        list(JOIN ARG_FEATURES "," _features_csv)
        set(cargo_features "--features=${_features_csv}")
    endif()

    set(cargo_target_dir "${CMAKE_BINARY_DIR}/cargo/build")
    set(cargo_output_dir "${cargo_target_dir}/${rust_target}/${cargo_profile_dir}")

    if(WIN32)
        set(output_lib "${cargo_output_dir}/${ARG_NAME}.lib")
        set(depfile    "${cargo_output_dir}/${ARG_NAME}.d")
    else()
        set(output_lib "${cargo_output_dir}/lib${ARG_NAME}.a")
        set(depfile    "${cargo_output_dir}/lib${ARG_NAME}.d")
    endif()

    # Cargo respects these env vars when invoking host tooling for native deps
    # and when picking a linker. We forward the CMake compiler/linker so the
    # Rust build sees the same toolchain we're already using for C/C++.
    set(cargo_env
        "CC_${target_underscore}=${CMAKE_C_COMPILER}"
        "CXX_${target_underscore}=${CMAKE_CXX_COMPILER}"
        "CARGO_BUILD_RUSTC=${TON_RUSTC}"
    )
    if(NOT WIN32)
        # On Windows-MSVC, rustc invokes lld-link / link.exe directly with MSVC
        # syntax — overriding CARGO_TARGET_*_LINKER with our compiler driver
        # (clang-cl) breaks the link because rustc passes /-style flags.
        list(APPEND cargo_env
            "CARGO_TARGET_${target_upper}_LINKER=${CMAKE_C_COMPILER}"
            "AR_${target_underscore}=${CMAKE_AR}"
        )
    endif()
    if(APPLE AND CMAKE_OSX_SYSROOT)
        list(APPEND cargo_env "SDKROOT=${CMAKE_OSX_SYSROOT}")
    endif()

    add_custom_command(
        OUTPUT "${output_lib}"
        COMMAND
            ${CMAKE_COMMAND} -E env ${cargo_env}
            "${TON_CARGO}"
                rustc
                --lib
                "--target=${rust_target}"
                --manifest-path "${manifest_abs}"
                --target-dir "${cargo_target_dir}"
                --locked
                ${cargo_features}
                ${cargo_profile_flag}
                --
                --emit=dep-info,link
                -Cdefault-linker-libraries=yes
        DEPENDS "${manifest_abs}" "${crate_dir}/Cargo.lock"
        DEPFILE "${depfile}"
        COMMENT "cargo build ${ARG_NAME}"
        WORKING_DIRECTORY "${crate_dir}"
        USES_TERMINAL
        COMMAND_EXPAND_LISTS
    )

    add_custom_target(${ARG_NAME}-build DEPENDS "${output_lib}")
    add_library(${ARG_NAME} STATIC IMPORTED GLOBAL)
    set_target_properties(${ARG_NAME} PROPERTIES IMPORTED_LOCATION "${output_lib}")
    add_dependencies(${ARG_NAME} ${ARG_NAME}-build)

    # Rust's std on Windows-MSVC pulls in these system import libs; on UNIX
    # we propagate pthreads/dl/m which Rust's std also expects.
    if(WIN32)
        set_target_properties(${ARG_NAME} PROPERTIES
            INTERFACE_LINK_LIBRARIES "kernel32;ntdll;Ws2_32;userenv;Bcrypt"
        )
    else()
        set_target_properties(${ARG_NAME} PROPERTIES
            INTERFACE_LINK_LIBRARIES "${CMAKE_THREAD_LIBS_INIT};${CMAKE_DL_LIBS};m"
        )
    endif()
endfunction()
