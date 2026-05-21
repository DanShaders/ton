add_executable(test-consensus test-consensus.cpp)
target_link_libraries(test-consensus PRIVATE
    adnl
    adnltest
    dht
    overlay
    rldp
    tdactor
    tdutils
    tl_api
    ton_validator
    validator
)

add_executable(test-event-strings test-event-strings.cpp)
target_link_libraries(test-event-strings PRIVATE
    adnl
    keyring
    overlay
    td-test-main
    tdactor
    tdutils
    tl_api
    ton_validator
    validator
)
ton_snapshot_test(test-event-strings)

add_subdirectory(simplex)
