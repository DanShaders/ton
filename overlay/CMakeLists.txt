set(OVERLAY_SOURCE
  broadcast-fec.cpp
  broadcast-simple.cpp
  broadcast-twostep.cpp
  overlay-manager.cpp
  overlay.cpp
  overlay-peers.cpp

  broadcast-fec.hpp
  broadcast-simple.hpp
  broadcast-twostep.hpp
  overlay-manager.h
  overlay.h
  overlay.hpp
  overlays.h
)

add_library(overlay STATIC ${OVERLAY_SOURCE})

target_include_directories(overlay PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>/..
  ${OPENSSL_INCLUDE_DIR}
)
target_link_libraries(overlay PRIVATE tdutils tdactor adnl tl_api dht fec)
# TODO(danklishch): vendor BLAKE3 under third-party/ before merging. For now,
# rely on the system libblake3 (Arch package `libblake3`) — used to hash
# two-step broadcast bundles in place of SHA-256.
target_link_libraries(overlay PRIVATE blake3)

