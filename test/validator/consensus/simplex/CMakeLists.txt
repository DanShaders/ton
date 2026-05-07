add_executable(test-event-strings-simplex test-event-strings.cpp)
target_link_libraries(test-event-strings-simplex PRIVATE
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
ton_snapshot_test(test-event-strings-simplex)
