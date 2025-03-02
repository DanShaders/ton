cmake_minimum_required(VERSION 3.5 FATAL_ERROR)

if (NOT OPENSSL_FOUND)
  find_package(OpenSSL REQUIRED)
endif()

set(TON_CRYPTO_CORE_SOURCE
  Ed25519.cpp
  common/bigint.cpp
  common/refcnt.cpp
  common/refint.cpp
  common/bigexp.cpp
  common/bitstring.cpp
  common/util.cpp
  ellcurve/Ed25519.cpp
  ellcurve/Fp25519.cpp
  ellcurve/Montgomery.cpp
  ellcurve/TwEdwards.cpp
  openssl/bignum.cpp
  openssl/residue.cpp
  openssl/rand.cpp
  vm/boc.cpp
  tl/tlblib.cpp

  Ed25519.h
  common/AtomicRef.h
  common/bigint.hpp
  common/bitstring.h
  common/refcnt.hpp
  common/refint.h
  common/bigexp.h
  common/util.h
  common/linalloc.hpp
  common/promiseop.hpp

  ellcurve/Ed25519.h
  ellcurve/Fp25519.h
  ellcurve/Montgomery.h
  ellcurve/TwEdwards.h

  openssl/bignum.h
  openssl/digest.hpp
  openssl/rand.hpp
  openssl/residue.h

  tl/tlbc-aux.h
  tl/tlbc-data.h
  tl/tlblib.hpp

  keccak/keccak.h
  keccak/keccak.cpp

  vm/dict.cpp
  vm/cells/Cell.cpp
  vm/cells/CellBuilder.cpp
  vm/cells/CellHash.cpp
  vm/cells/CellSlice.cpp
  vm/cells/CellTraits.cpp
  vm/cells/CellUsageTree.cpp
  vm/cells/DataCell.cpp
  vm/cells/LevelMask.cpp
  vm/cells/MerkleProof.cpp
  vm/cells/MerkleUpdate.cpp

  vm/dict.h
  vm/cells/Cell.h
  vm/cells/CellBuilder.h
  vm/cells/CellHash.h
  vm/cells/CellSlice.h
  vm/cells/CellTraits.h
  vm/cells/CellUsageTree.h
  vm/cells/CellWithStorage.h
  vm/cells/DataCell.h
  vm/cells/ExtCell.h
  vm/cells/LevelMask.h
  vm/cells/MerkleProof.h
  vm/cells/MerkleUpdate.h
  vm/cells/PrunnedCell.h
  vm/cells/UsageCell.h
  vm/cells/VirtualCell.h
  vm/cells/VirtualizationParameters.h

  vm/cells.h
  vm/cellslice.h

  vm/db/StaticBagOfCellsDb.h
  vm/db/StaticBagOfCellsDb.cpp

  vm/Hasher.h
  vm/Hasher.cpp

  ellcurve/secp256k1.h
  ellcurve/secp256k1.cpp
  ellcurve/p256.h
  ellcurve/p256.cpp)

set(TON_CRYPTO_SOURCE
  vm/stack.cpp
  vm/atom.cpp
  vm/continuation.cpp
  vm/memo.cpp
  vm/dispatch.cpp
  vm/opctable.cpp
  vm/cp0.cpp
  vm/stackops.cpp
  vm/tupleops.cpp
  vm/arithops.cpp
  vm/cellops.cpp
  vm/contops.cpp
  vm/dictops.cpp
  vm/debugops.cpp
  vm/tonops.cpp
  vm/utils.cpp
  vm/vm.cpp
  vm/bls.cpp

  vm/arithops.h
  vm/atom.h
  vm/boc.h
  vm/boc-writers.h
  vm/box.hpp
  vm/cellops.h
  vm/continuation.h
  vm/contops.h
  vm/cp0.h
  vm/debugops.h
  vm/dictops.h
  vm/excno.hpp
  vm/fmt.hpp
  vm/log.h
  vm/memo.h
  vm/opctable.h
  vm/stack.hpp
  vm/stackops.h
  vm/tupleops.h
  vm/tonops.h
  vm/vmstate.h
  vm/utils.h
  vm/vm.h
  vm/bls.h)

set(TON_DB_SOURCE
  vm/db/DynamicBagOfCellsDb.cpp
  vm/db/CellStorage.cpp

  vm/db/DynamicBagOfCellsDb.h
  vm/db/CellHashTable.h
  vm/db/CellStorage.h
)

set(PARSER_SOURCE
  parser/srcread.cpp
  parser/lexer.cpp
  parser/symtable.cpp

  parser/srcread.h
  parser/lexer.h
  parser/symtable.h
)

set(TLB_BLOCK_AUTO
  ${CMAKE_CURRENT_SOURCE_DIR}/block/block-auto.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/block/block-auto.h
)

set(BLOCK_SOURCE
  block/block.cpp
  block/block-parse.cpp
  block/mc-config.cpp
  block/output-queue-merger.cpp
  block/transaction.cpp
  block/precompiled-smc/PrecompiledSmartContract.cpp
  ${TLB_BLOCK_AUTO}

  block/block.h
  block/block-parse.h
  block/output-queue-merger.h
  block/transaction.h
  block/precompiled-smc/PrecompiledSmartContract.h
  block/precompiled-smc/common.h
)


set(USE_EMSCRIPTEN ${USE_EMSCRIPTEN} PARENT_SCOPE)

add_library(ton_crypto_core STATIC ${TON_CRYPTO_CORE_SOURCE})
target_include_directories(ton_crypto_core PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>)
target_link_libraries(ton_crypto_core PUBLIC ${OPENSSL_CRYPTO_LIBRARY} tdutils tddb)
if (NOT WIN32)
  target_link_libraries(ton_crypto_core PUBLIC dl z)
endif()
target_include_directories(ton_crypto_core SYSTEM PUBLIC $<BUILD_INTERFACE:${OPENSSL_INCLUDE_DIR}>)

add_library(ton_crypto STATIC ${TON_CRYPTO_SOURCE})
target_include_directories(ton_crypto PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>	$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>)
target_link_libraries(ton_crypto PUBLIC ${OPENSSL_CRYPTO_LIBRARY} ton_crypto_core ton_block)
if (USE_EMSCRIPTEN)
  target_link_options(ton_crypto PRIVATE -fexceptions)
  target_compile_options(ton_crypto PRIVATE -fexceptions)
endif()
if (NOT WIN32)
  find_library(DL dl)
  if (DL)
    target_link_libraries(ton_crypto PUBLIC dl z)
  else()
    target_link_libraries(ton_crypto PUBLIC z)
  endif()
endif()
target_include_directories(ton_crypto SYSTEM PUBLIC $<BUILD_INTERFACE:${OPENSSL_INCLUDE_DIR}>)

add_dependencies(ton_crypto blst)
add_dependencies(ton_crypto_core secp256k1)

target_include_directories(ton_crypto PRIVATE ${BLST_INCLUDE_DIR})
target_link_libraries(ton_crypto PRIVATE ${BLST_LIB})

target_include_directories(ton_crypto_core PUBLIC $<BUILD_INTERFACE:${SECP256K1_INCLUDE_DIR}>)

if (MSVC)
  find_package(Sodium REQUIRED)
  target_compile_definitions(ton_crypto PUBLIC SODIUM_STATIC)
  target_link_libraries(ton_crypto_core PUBLIC ${SECP256K1_LIBRARY})
  target_link_libraries(ton_crypto PUBLIC ${SECP256K1_LIBRARY})
elseif (EMSCRIPTEN)
  target_link_libraries(ton_crypto_core PUBLIC $<BUILD_INTERFACE:${SECP256K1_LIBRARY}>)
  target_link_libraries(ton_crypto PUBLIC $<BUILD_INTERFACE:${SECP256K1_LIBRARY}>)
else()
  if (NOT SODIUM_FOUND)
    find_package(Sodium REQUIRED)
  else()
    message(STATUS "Using Sodium ${SODIUM_LIBRARY_RELEASE}")
  endif()
  target_compile_definitions(ton_crypto PUBLIC SODIUM_STATIC)
  target_link_libraries(ton_crypto_core PUBLIC ${SECP256K1_LIBRARY})
  target_link_libraries(ton_crypto PUBLIC ${SECP256K1_LIBRARY})
endif()

target_include_directories(ton_crypto_core PUBLIC $<BUILD_INTERFACE:${SODIUM_INCLUDE_DIR}>)
target_link_libraries(ton_crypto PUBLIC ${SODIUM_LIBRARY_RELEASE})

add_library(ton_db STATIC ${TON_DB_SOURCE})
target_include_directories(ton_db PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
	$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>)
target_link_libraries(ton_db PUBLIC tdutils tddb ton_crypto)

add_library(src_parser ${PARSER_SOURCE})
target_include_directories(src_parser PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>)
target_link_libraries(src_parser PUBLIC ton_crypto_core)

add_library(ton_block STATIC ${BLOCK_SOURCE})
target_include_directories(ton_block PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/block> $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>)
target_link_libraries(ton_block PUBLIC ton_crypto_core tdactor tl_api)
if (USE_EMSCRIPTEN)
  target_link_options(ton_block PRIVATE -fexceptions)
  target_compile_options(ton_block PRIVATE -fexceptions)
endif()
