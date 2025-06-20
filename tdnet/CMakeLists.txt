set(TDNET_SOURCE
  td/net/FdListener.cpp
  td/net/TcpListener.cpp
  td/net/UdpServer.cpp

  td/net/FdListener.h
  td/net/TcpListener.h
  td/net/UdpServer.h
)

add_library(tdnet STATIC ${TDNET_SOURCE})
target_include_directories(tdnet PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>)
target_link_libraries(tdnet PUBLIC tdactor)

set(NET_TEST_SOURCE
  ${CMAKE_CURRENT_SOURCE_DIR}/test/net-test.cpp
  PARENT_SCOPE
)
