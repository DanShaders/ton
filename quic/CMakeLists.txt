add_library(quic STATIC quic-sender.cpp quic-server.cpp quic-pimpl.cpp quic-worker.cpp)
# liburing for the QuicWorker fast path. Linux only.
find_library(LIBURING_LIBRARY NAMES uring REQUIRED)
find_path(LIBURING_INCLUDE_DIR liburing.h REQUIRED)
target_include_directories(quic PRIVATE ${LIBURING_INCLUDE_DIR})

if (USE_BORINGSSL)
  # Use BoringSSL's headers/libs and ngtcp2's BoringSSL crypto wrapper. The
  # bundled OpenSSL is still built (we depend on ton_crypto_core which uses it)
  # but NOT included by the QUIC TU itself — BoringSSL and OpenSSL headers
  # can't coexist in the same translation unit.
  target_include_directories(quic PUBLIC
      $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
      $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>/..
  )
  target_include_directories(quic PRIVATE ${BORINGSSL_INCLUDE_DIR})
  target_compile_definitions(quic PRIVATE NGTCP2_USE_BORINGSSL)
  target_link_libraries(quic PUBLIC tl_api tdutils tdactor metrics ngtcp2_static ngtcp2_crypto_boringssl_static ${BORINGSSL_LIBRARIES} ton_crypto_core ${LIBURING_LIBRARY})
else()
  target_include_directories(quic PUBLIC
      $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
      $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>/..
      ${OPENSSL_INCLUDE_DIR}
  )
  target_link_libraries(quic PUBLIC tl_api tdutils tdactor metrics ngtcp2_static ngtcp2_crypto_ossl_static ${OPENSSL_LIBRARIES} ton_crypto_core ${LIBURING_LIBRARY})
  add_dependencies(quic OpenSSL)
endif()

add_executable(quic-example-client quic-example-client.cpp)
target_include_directories(quic-example-client PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>)
target_link_libraries(quic-example-client PUBLIC quic)

add_executable(quic-example-server quic-example-server.cpp)
target_include_directories(quic-example-server PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>)
target_link_libraries(quic-example-server PUBLIC quic)
