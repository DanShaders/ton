cmake_minimum_required(VERSION 3.5 FATAL_ERROR)

add_library(contest-solution STATIC solution.hpp solution.cpp contest-validate-query.cpp contest-validate-query.hpp snmalloc.cpp)

if (SNMALLOC_FOUND)
  add_dependencies(contest-solution snmalloc)
  target_compile_definitions(contest-solution PRIVATE SNMALLOC_ENABLED=1 SNMALLOC_USE_WAIT_ON_ADDRESS=1)
  target_include_directories(contest-solution PUBLIC $<TARGET_PROPERTY:snmalloc,INTERFACE_INCLUDE_DIRECTORIES>)
endif()

target_include_directories(contest-solution PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../..>)
target_include_directories(contest-solution PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../../validator>)
target_link_libraries(contest-solution PRIVATE tdactor tdutils ton_block ton_validator validator ton_crypto)