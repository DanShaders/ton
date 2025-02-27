cmake_minimum_required(VERSION 3.5 FATAL_ERROR)

option(TDUTILS_MIME_TYPE "Generate mime types conversion (gperf is required)" ON)

if (WIN32)
  if (WINGETOPT_FOUND)
    set(TD_HAVE_GETOPT 1)
  endif()
else()
  set(TD_HAVE_GETOPT 1)
endif()

if (NOT DEFINED CMAKE_INSTALL_LIBDIR)
  set(CMAKE_INSTALL_LIBDIR "lib")
endif()

find_package(PkgConfig REQUIRED)
if (NOT ZLIB_FOUND)
  pkg_check_modules(ZLIB zlib)
endif()
if (ZLIB_FOUND)
  set(TD_HAVE_ZLIB 1)
  message(STATUS "Found ZLIB: ${ZLIB_INCLUDE_DIR} ${ZLIB_LIBRARIES}")

  # OpenSSL internally depends on zlib
  if (NOT OPENSSL_FOUND)
    find_package(OpenSSL)
  endif()
  if (OPENSSL_FOUND)
    set(TD_HAVE_OPENSSL 1)
  endif()
endif()

if (CRC32C_FOUND)
  set(TD_HAVE_CRC32C 1)
endif()

if (ABSL_FOUND)
  set(TD_HAVE_ABSL 1)
endif()

configure_file(utils/config.h.in utils/config.h @ONLY)

# TDUTILS
set_source_files_properties(${TDMIME_AUTO} PROPERTIES GENERATED TRUE)
if (CLANG OR GCC)
  set_property(SOURCE ${TDMIME_AUTO} APPEND_STRING PROPERTY COMPILE_FLAGS " -Wno-conversion")
elseif (MSVC)
  set_property(SOURCE ${TDMIME_AUTO} APPEND_STRING PROPERTY COMPILE_FLAGS " /wd4267")
endif()
if (CLANG)
  set_property(SOURCE ${TDMIME_AUTO} APPEND_STRING PROPERTY COMPILE_FLAGS " -Wno-deprecated-register")
endif()

set(TDUTILS_SOURCE
  utils/port/Clocks.cpp
  utils/port/FileFd.cpp
  utils/port/IPAddress.cpp
  utils/port/MemoryMapping.cpp
  utils/port/path.cpp
  utils/port/PollFlags.cpp
  utils/port/rlimit.cpp
  utils/port/signals.cpp
  utils/port/sleep.cpp
  utils/port/SocketFd.cpp
  utils/port/Stat.cpp
  utils/port/StdStreams.cpp
  utils/port/thread_local.cpp
  utils/port/user.cpp
  utils/port/uname.cpp
  utils/port/user.cpp
  utils/port/wstring_convert.cpp

  utils/port/detail/Epoll.cpp
  utils/port/detail/EventFdBsd.cpp
  utils/port/detail/EventFdLinux.cpp
  utils/port/detail/EventFdWindows.cpp
  utils/port/detail/Iocp.cpp
  utils/port/detail/KQueue.cpp
  utils/port/detail/NativeFd.cpp
  utils/port/detail/Poll.cpp
  utils/port/detail/Select.cpp
  utils/port/detail/ThreadIdGuard.cpp
  utils/port/detail/ThreadPthread.cpp
  utils/port/detail/WineventPoll.cpp

  utils/base64.cpp
  utils/BigNum.cpp
  utils/buffer.cpp
  utils/check.cpp
  utils/crypto.cpp
  utils/filesystem.cpp
  utils/logging.cpp
  utils/misc.cpp
  utils/MpmcQueue.cpp
  utils/OptionParser.cpp
  utils/PathView.cpp
  utils/Random.cpp
  utils/SharedSlice.cpp
  utils/Slice.cpp
  utils/StackAllocator.cpp
  utils/Status.cpp
  utils/StringBuilder.cpp
  utils/Time.cpp
  utils/Timer.cpp
  utils/tl_parsers.cpp
  utils/unicode.cpp
  utils/utf8.cpp

  utils/port/Clocks.h
  utils/port/config.h
  utils/port/CxCli.h
  utils/port/EventFd.h
  utils/port/EventFdBase.h
  utils/port/FileFd.h
  utils/port/IPAddress.h
  utils/port/IoSlice.h
  utils/port/MemoryMapping.h
  utils/port/path.h
  utils/port/platform.h
  utils/port/Poll.h
  utils/port/PollBase.h
  utils/port/PollFlags.h
  utils/port/StdStreams.h
  utils/port/rlimit.h
  utils/port/RwMutex.h
  utils/port/signals.h
  utils/port/sleep.h
  utils/port/SocketFd.h
  utils/port/Stat.h
  utils/port/thread.h
  utils/port/thread_local.h
  utils/port/user.h
  utils/port/uname.h
  utils/port/user.h
  utils/port/wstring_convert.h

  utils/port/detail/Epoll.h
  utils/port/detail/EventFdBsd.h
  utils/port/detail/EventFdLinux.h
  utils/port/detail/EventFdWindows.h
  utils/port/detail/Iocp.h
  utils/port/detail/KQueue.h
  utils/port/detail/NativeFd.h
  utils/port/detail/Poll.h
  utils/port/detail/PollableFd.h
  utils/port/detail/Select.h
  utils/port/detail/skip_eintr.h
  utils/port/detail/ThreadIdGuard.h
  utils/port/detail/ThreadPthread.h
  utils/port/detail/ThreadStl.h
  utils/port/detail/WineventPoll.h

  utils/as.h
  utils/base64.h
  utils/benchmark.h
  utils/BigNum.h
  utils/bits.h
  utils/buffer.h
  utils/check.h
  utils/Closure.h
  utils/common.h
  utils/ConcurrentHashTable.h
  utils/Container.h
  utils/Context.h
  utils/crypto.h
  utils/Destructor.h
  utils/filesystem.h
  utils/format.h
  utils/Hash.h
  utils/HashMap.h
  utils/HashSet.h
  utils/HazardPointers.h
  utils/Heap.h
  utils/int_types.h
  utils/invoke.h
  utils/List.h
  utils/logging.h
  utils/misc.h
  utils/MovableValue.h
  utils/MpmcQueue.h
  utils/MpmcWaiter.h
  utils/MpscPollableQueue.h
  utils/MpscLinkQueue.h
  utils/Named.h
  utils/ObjectPool.h
  utils/Observer.h
  utils/optional.h
  utils/OptionParser.h
  utils/overloaded.h
  utils/Parser.h
  utils/PathView.h
  utils/queue.h
  utils/Random.h
  utils/ScopeGuard.h
  utils/SharedObjectPool.h
  utils/SharedSlice.h
  utils/Slice-decl.h
  utils/Slice.h
  utils/Span.h
  utils/SpinLock.h
  utils/StackAllocator.h
  utils/Status.h
  utils/StealingQueue.h
  utils/Storer.h
  utils/StorerBase.h
  utils/StringBuilder.h
  utils/ThreadLocalStorage.h
  utils/ThreadSafeCounter.h
  utils/Time.h
  utils/date.h
  utils/TimedStat.h
  utils/Timer.h
  utils/tl_helpers.h
  utils/tl_parsers.h
  utils/tl_storers.h
  utils/type_traits.h
  utils/UInt.h
  utils/uint128.h
  utils/unicode.h
  utils/unique_ptr.h
  utils/utf8.h
  utils/Variant.h
  utils/VectorQueue.h
)

#RULES
#LIBRARIES
add_library(tdutils STATIC ${TDUTILS_SOURCE})
if (WIN32)
  # find_library(WS2_32_LIBRARY ws2_32)
  # find_library(MSWSOCK_LIBRARY Mswsock)
  # target_link_libraries(tdutils PRIVATE ${WS2_32_LIBRARY} ${MSWSOCK_LIBRARY})
  target_link_libraries(tdutils PRIVATE ws2_32 Mswsock Normaliz psapi DbgHelp)
endif()
if (NOT CMAKE_CROSSCOMPILING AND TDUTILS_MIME_TYPE)
  add_dependencies(tdutils tdmime_auto)
endif()

if (DEFINED CMAKE_THREAD_LIBS_INIT)
  target_link_libraries(tdutils PUBLIC ${CMAKE_THREAD_LIBS_INIT})
endif()
target_include_directories(tdutils PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}> $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>)

if (OPENSSL_FOUND)
  target_link_libraries(tdutils PRIVATE ${OPENSSL_CRYPTO_LIBRARY} ${CMAKE_DL_LIBS} ${ZLIB_LIBRARIES})
  target_include_directories(tdutils SYSTEM PRIVATE ${OPENSSL_INCLUDE_DIR})
endif()

if (ZLIB_FOUND)
  target_link_libraries(tdutils PRIVATE ${ZLIB_LIBRARIES})
  target_include_directories(tdutils SYSTEM PRIVATE ${ZLIB_INCLUDE_DIR})
endif()

if (CRC32C_FOUND)
  target_link_libraries(tdutils PRIVATE crc32c)
endif()

target_link_libraries_system(tdutils absl::flat_hash_map absl::flat_hash_set absl::hash)

if (CMAKE_HOST_SYSTEM_NAME MATCHES "NetBSD")
  target_link_libraries(tdutils PUBLIC /usr/pkg/gcc5/i486--netbsdelf/lib/libatomic.so)
endif()

install(TARGETS tdutils EXPORT TdTargets
  LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
)


#SOURCE SETS
set(TDACTOR_SOURCE
  actor/core/ActorExecutor.cpp
  actor/core/ActorTypeStat.cpp
  actor/core/CpuWorker.cpp
  actor/core/IoWorker.cpp
  actor/core/Scheduler.cpp

  actor/ActorStats.cpp
  actor/MultiPromise.cpp

  actor/actor.h
  actor/ActorId.h
  actor/ActorOwn.h
  actor/ActorShared.h
  actor/ActorStats.h
  actor/common.h
  actor/PromiseFuture.h
  actor/MultiPromise.h

  actor/core/Actor.h
  actor/core/ActorExecuteContext.h
  actor/core/ActorExecutor.h
  actor/core/ActorInfo.h
  actor/core/ActorInfoCreator.h
  actor/core/ActorLocker.h
  actor/core/ActorMailbox.h
  actor/core/ActorMessage.h
  actor/core/ActorSignals.h
  actor/core/ActorState.h
  actor/core/ActorTypeStat.h
  actor/core/CpuWorker.h
  actor/core/Context.h
  actor/core/IoWorker.h
  actor/core/Scheduler.h
  actor/core/SchedulerContext.h
  actor/core/SchedulerId.h
  actor/core/SchedulerMessage.h
)

#RULES

#LIBRARIES

add_library(tdactor STATIC ${TDACTOR_SOURCE})
target_include_directories(tdactor PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>)
target_link_libraries(tdactor PUBLIC tdutils)

# END-INTERNAL
install(TARGETS tdactor EXPORT TdTargets
  LIBRARY DESTINATION lib
  ARCHIVE DESTINATION lib
  RUNTIME DESTINATION bin
  INCLUDES DESTINATION include
)

set(TDDB_SOURCE
  db/BlobView.cpp
  db/BlobView.h
  db/KeyValue.h
)

#RULES

add_library(tddb STATIC ${TDDB_SOURCE})
target_include_directories(tddb PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>)
target_link_libraries(tddb PUBLIC tdutils tdactor)


# BEGIN-INTERNAL
#add_subdirectory(benchmark)

# END-INTERNAL
install(TARGETS tddb EXPORT TdTargets
  LIBRARY DESTINATION lib
  ARCHIVE DESTINATION lib
  RUNTIME DESTINATION bin
  INCLUDES DESTINATION include
)

set(TDPG_SOURCE
  pg/ton_api.cpp
  pg/ton_api.hpp
  pg/tl_object_parse.h
  pg/tl_object_store.h
  pg/TlObject.h
  pg/ton_api.h
)
add_library(tl_api STATIC ${TDPG_SOURCE})
target_include_directories(tl_api PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}>)
target_link_libraries(tl_api PUBLIC tdutils)


