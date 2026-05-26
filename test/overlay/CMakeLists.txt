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
