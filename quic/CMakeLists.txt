add_library(quic STATIC quic-sender.cpp quic-server.cpp quic-pimpl.cpp quic-worker.cpp)
target_include_directories(quic PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>/..
    ${OPENSSL_INCLUDE_DIR}
)
# liburing for the QuicWorker fast path. Linux only.
find_library(LIBURING_LIBRARY NAMES uring REQUIRED)
find_path(LIBURING_INCLUDE_DIR liburing.h REQUIRED)
target_include_directories(quic PRIVATE ${LIBURING_INCLUDE_DIR})
target_link_libraries(quic PUBLIC tl_api tdutils tdactor metrics ngtcp2_static ngtcp2_crypto_ossl_static ${OPENSSL_LIBRARIES} ton_crypto_core ${LIBURING_LIBRARY})
add_dependencies(quic OpenSSL)

add_executable(quic-example-client quic-example-client.cpp)
target_include_directories(quic-example-client PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>)
target_link_libraries(quic-example-client PUBLIC quic)

add_executable(quic-example-server quic-example-server.cpp)
target_include_directories(quic-example-server PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>)
target_link_libraries(quic-example-server PUBLIC quic)
