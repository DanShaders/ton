if (NOT ISA_L_CRYPTO_LIBRARY)
    set(ISA_L_CRYPTO_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third-party/isa-l_crypto)
    set(ISA_L_CRYPTO_BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR}/third-party/isa-l_crypto)
    set(ISA_L_CRYPTO_INCLUDE_DIR ${ISA_L_CRYPTO_BINARY_DIR}/include)

    file(MAKE_DIRECTORY ${ISA_L_CRYPTO_BINARY_DIR})
    file(MAKE_DIRECTORY "${ISA_L_CRYPTO_BINARY_DIR}/include")

    if (MSVC)
      set(ISA_L_CRYPTO_BINARY_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third-party/isa-l_crypto)
      set(ISA_L_CRYPTO_LIBRARY ${ISA_L_CRYPTO_SOURCE_DIR}/build/src/Release/isa-l_crypto.lib)
      set(ISA_L_CRYPTO_INCLUDE_DIR ${ISA_L_CRYPTO_BINARY_DIR}/include)
      add_custom_command(
        WORKING_DIRECTORY ${ISA_L_CRYPTO_SOURCE_DIR}
        COMMAND nmake -f Makefile.nmake
        COMMAND nmake perfs -f Makefile.nmake
        COMMAND cp "${ISA_L_CRYPTO_SOURCE_DIR}/isa-l_crypto_static.lib" "${ISA_L_CRYPTO_LIBRARY}"
        COMMENT "Build isa-l_crypto"
        DEPENDS ${ISA_L_CRYPTO_SOURCE_DIR}
        OUTPUT ${ISA_L_CRYPTO_LIBRARY}
      )
    elseif (EMSCRIPTEN)
      set(ISA_L_CRYPTO_BINARY_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third-party/isa-l_crypto)
      set(ISA_L_CRYPTO_LIBRARY ${ISA_L_CRYPTO_BINARY_DIR}/.libs/libisal_crypto.a)
      set(ISA_L_CRYPTO_INCLUDE_DIR ${ISA_L_CRYPTO_SOURCE_DIR}/include)
      add_custom_command(
          WORKING_DIRECTORY ${ISA_L_CRYPTO_SOURCE_DIR}
          COMMAND emmake make clean
          COMMAND emmake make -f Makefile.unx
          COMMENT "Build isa-l_crypto with emscripten"
          DEPENDS ${ISA_L_CRYPTO_SOURCE_DIR}
          OUTPUT ${ISA_L_CRYPTO_LIBRARY}
      )
    else()
      if (NOT NIX)
        set(ISA_L_CRYPTO_BINARY_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third-party/isa-l_crypto)
        set(ISA_L_CRYPTO_LIBRARY ${ISA_L_CRYPTO_BINARY_DIR}/lib/libisal_crypto.a)
        set(ISA_L_CRYPTO_INCLUDE_DIR ${ISA_L_CRYPTO_BINARY_DIR}/include)
        add_custom_command(
            WORKING_DIRECTORY ${ISA_L_CRYPTO_SOURCE_DIR}
            COMMAND ./autogen.sh
            COMMAND ./configure --prefix ${ISA_L_CRYPTO_BINARY_DIR}
            COMMAND make -f Makefile.unx -j16
            COMMAND make install
            COMMENT "Build isa-l_crypto"
            DEPENDS ${ISA_L_CRYPTO_SOURCE_DIR}
            OUTPUT ${ISA_L_CRYPTO_LIBRARY}
        )
      endif()
    endif()
else()
   message(STATUS "Use isa-l_crypto: ${ISA_L_CRYPTO_LIBRARY}")
endif()

add_custom_target(isa-l_crypto DEPENDS ${ISA_L_CRYPTO_LIBRARY})
