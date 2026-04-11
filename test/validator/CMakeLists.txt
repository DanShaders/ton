add_subdirectory(consensus)

add_executable(test-shard-ext-pool test-shard-ext-pool.cpp)

target_link_libraries(test-shard-ext-pool PRIVATE
    tdutils
    tdactor
    ton_crypto
    ton_validator
    td-test-main
)

ton_test(test-shard-ext-pool)
