if (USE_EMSCRIPTEN OR EMSCRIPTEN)
  return()
endif()

set(BLOCHAIN_EXPLORER_SOURCE
    blockchain-explorer.cpp
    blockchain-explorer.hpp
    blockchain-explorer-http.cpp
    blockchain-explorer-http.hpp
    blockchain-explorer-query.cpp
    blockchain-explorer-query.hpp
)

add_executable(blockchain-explorer ${BLOCHAIN_EXPLORER_SOURCE})

target_link_libraries(blockchain-explorer tdactor adnllite tl_lite_api tl-lite-utils ton_crypto tonhttp)
target_link_libraries(blockchain-explorer lite-client-common)

install(TARGETS blockchain-explorer RUNTIME DESTINATION bin)
