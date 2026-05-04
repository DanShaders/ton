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

add_executable(test-simplex test-simplex.cpp)
target_link_libraries(test-simplex PRIVATE tdactor tdutils validator td-test-main)
