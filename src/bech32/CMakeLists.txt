CMAKE_MINIMUM_REQUIRED(VERSION 3.1.0 FATAL_ERROR)

file(GLOB BECH_HEADERS ${CMAKE_CURRENT_SOURCE_DIR}/*.h)
source_group("BechHeaders" FILES ${BECH_HEADERS})

add_library(bech32 ${BECH_HEADERS}
  ${CMAKE_CURRENT_SOURCE_DIR}/bech32.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/bech_addr.cpp
)

add_executable(test_bech32 tests.cpp)
target_link_libraries(test_bech32 bech32)
