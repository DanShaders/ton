# - Try to find ISA-L_crypto
# Once done this will define
#
#  ISA_L_CRYPTO_INCLUDE_DIR - the ISA-L_crypto include directory
#  ISA_L_CRYPTO_LIBRARY - Link these to use ISA-L_crypto

if (NOT ISA_L_CRYPTO_LIBRARY)
  find_path(
    ISA_L_CRYPTO_INCLUDE_DIR
    NAMES sha256_mb.h
    DOC "sha256_mb.h include dir"
  )

  find_library(
    ISA_L_CRYPTO_LIBRARY
    NAMES isa-l_crypto libisa-l_crypto
    DOC "isa-l_crypto library"
  )
endif()

if (ISA_L_CRYPTO_LIBRARY)
  message(STATUS "Found ISA-L_crypto: ${ISA_L_CRYPTO_LIBRARY}")
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(isa-l_crypto DEFAULT_MSG ISA_L_CRYPTO_INCLUDE_DIR ISA_L_CRYPTO_LIBRARY)
mark_as_advanced(ISA_L_CRYPTO_INCLUDE_DIR ISA_L_CRYPTO_LIBRARY)
