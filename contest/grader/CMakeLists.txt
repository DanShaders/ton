cmake_minimum_required(VERSION 3.5 FATAL_ERROR)

add_executable(contest-grader grader.cpp)

if (USE_GPERFTOOLS)
  add_dependencies(contest-grader gperftools)
  target_link_libraries(contest-grader
    -Wl,--whole-archive
    ${GPERFTOOLS_PROFILER_LIB}
    -Wl,--no-whole-archive
  )
endif()

target_include_directories(contest-grader PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../..>)
target_link_libraries(contest-grader contest-solution tdutils tdactor ton_block ton_crypto tl-utils tl_api git terminal)
