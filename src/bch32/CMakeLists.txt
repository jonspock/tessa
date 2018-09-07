CMAKE_MINIMUM_REQUIRED(VERSION 3.1.0 FATAL_ERROR)

file(GLOB BECH_HEADERS ${CMAKE_CURRENT_SOURCE_DIR}/*.h)
source_group("BechHeaders" FILES ${BECH_HEADERS})

add_library(bch32 ${BECH_HEADERS}
  ${CMAKE_CURRENT_SOURCE_DIR}/bch32.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/bch_addr.cpp
)

add_executable(test_bch32 tests.cpp)
target_link_libraries(test_bch32 bch32)
