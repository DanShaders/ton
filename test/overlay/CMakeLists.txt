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
