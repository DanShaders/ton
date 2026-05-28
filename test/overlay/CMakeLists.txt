add_executable(bench-broadcast-twostep bench-broadcast-twostep.cpp)
target_link_libraries(bench-broadcast-twostep PRIVATE
    overlay
    tdutils
    tdactor
    adnl
    quic
    tl_api
    dht
    keyring
)

add_executable(bench-quic-throughput bench-quic-throughput.cpp)
target_link_libraries(bench-quic-throughput PRIVATE
    overlay
    tdutils
    tdactor
    adnl
    quic
    tl_api
    dht
    keyring
)

add_executable(bench-fec-split bench-fec-split.cpp)
target_link_libraries(bench-fec-split PRIVATE
    tdfec
    tdutils
)

# When TON_USE_MIMALLOC=ON, link mimalloc FIRST so its malloc/free override
# the libc ones for the QUIC benches. (For broader use, mimalloc would need
# to be wired into every binary; here we only care about the benches.)
if (TON_USE_MIMALLOC)
  target_link_libraries(bench-broadcast-twostep PRIVATE ${MIMALLOC_LIBRARY})
  target_link_libraries(bench-quic-throughput PRIVATE ${MIMALLOC_LIBRARY})
endif()
