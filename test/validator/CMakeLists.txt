add_executable(test-runtime test-runtime.cpp)

target_link_libraries(test-runtime PRIVATE
    validator
    tdutils
    tdactor
    td-test-main
)

ton_test(test-runtime test-runtime)

add_executable(stress-pool stress-pool.cpp)

target_link_libraries(stress-pool PRIVATE
    overlay
    tdutils
    tdactor
    td-test-main
    adnl
    rldp
    tl_api
    dht
    validator
    ton_validator
)

add_test(stress-pool stress-pool)
