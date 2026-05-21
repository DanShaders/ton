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

add_executable(test-candidate-resolver test-candidate-resolver.cpp)
target_link_libraries(test-candidate-resolver PRIVATE tdactor tdutils validator td-test-main)

add_executable(test-consensus-actor test-consensus.cpp)
target_link_libraries(test-consensus-actor PRIVATE tdactor tdutils validator td-test-main)

add_executable(test-pool test-pool.cpp)
target_link_libraries(test-pool PRIVATE tdactor tdutils validator td-test-main)
