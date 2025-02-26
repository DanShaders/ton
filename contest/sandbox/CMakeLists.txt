cmake_minimum_required(VERSION 3.0.2 FATAL_ERROR)

add_executable(sandbox sandbox.cpp)
target_link_libraries(sandbox PRIVATE tdactor ton_crypto tdutils) 