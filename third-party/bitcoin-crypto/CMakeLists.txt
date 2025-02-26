cmake_minimum_required(VERSION 3.5 FATAL_ERROR)

project(bitcoin-crypto)

list(APPEND CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake/module)

include(cmake/introspection.cmake)

configure_file(${PROJECT_SOURCE_DIR}/cmake/bitcoin-build-config.h.in ${PROJECT_SOURCE_DIR}/src/bitcoin-crypto/bitcoin-build-config.h USE_SOURCE_PERMISSIONS @ONLY)

include_directories(${PROJECT_SOURCE_DIR}/src)
add_subdirectory(src/bitcoin-crypto/crypto)

add_library(bitcoincrypto INTERFACE)
target_link_libraries(bitcoincrypto INTERFACE bitcoin_crypto)
target_include_directories(bitcoincrypto INTERFACE ${PROJECT_SOURCE_DIR}/src)
