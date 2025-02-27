cmake_minimum_required(VERSION 3.5 FATAL_ERROR)

if (NOT OPENSSL_FOUND)
  find_package(OpenSSL REQUIRED)
endif()

set(TON_VALIDATOR_SOURCE
  impl/message-queue.cpp
  impl/shard.cpp

  impl/message-queue.hpp
  impl/shard.hpp

  interfaces/message-queue.h
  interfaces/shard.h
)

add_library(ton_validator STATIC ${TON_VALIDATOR_SOURCE})

target_include_directories(ton_validator PUBLIC 
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}> 
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>/.. 
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>/../crypto 
  ${OPENSSL_INCLUDE_DIR}
)

target_link_libraries(ton_validator PRIVATE tdutils tdactor ton_crypto ton_block)
