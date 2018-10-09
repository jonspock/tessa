CMAKE_MINIMUM_REQUIRED(VERSION 3.1.0 FATAL_ERROR)

include_directories(
  ${INCLUDE_DIRECTORIES}
  ${CMAKE_CURRENT_SOURCE_DIR}/../relic/include
  ${CMAKE_CURRENT_SOURCE_DIR}/../
  ${CMAKE_BINARY_DIR} # for relic_conf.h
  )

file(GLOB BLS_HEADERS ${CMAKE_CURRENT_SOURCE_DIR}/*.hpp)
file(GLOB OTHER_HEADERS ${CMAKE_CURRENT_SOURCE_DIR}/*.h)
source_group("BlsHeaders" FILES ${BLS_HEADERS} ${OTHER_HEADERS})

add_library(bls ${BLS_HEADERS} ${OTHER_HEADERS}
  #  ${CMAKE_CURRENT_SOURCE_DIR}/extendedpublickey.cpp
  #  ${CMAKE_CURRENT_SOURCE_DIR}/extendedprivatekey.cpp
  #  ${CMAKE_CURRENT_SOURCE_DIR}/chaincode.cpp
  
  ${CMAKE_CURRENT_SOURCE_DIR}/privatekey.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/signature.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/publickey.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/bls.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/aggregationinfo.cpp

  # legacy functions
  ${CMAKE_CURRENT_SOURCE_DIR}/ecdsa.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/blocksignature.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/extkey.cpp

  # Bitcoin wrappers
  ${CMAKE_CURRENT_SOURCE_DIR}/pubkey.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/privkey.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/key.cpp
  #
  )
target_link_libraries(bls relic_s sodium)


SET(ENABLE_TESTS false)
# For now just on Mac
if (ENABLE_TESTS)
  add_executable(runtest test.cpp)
  add_executable(runbench test-bench.cpp)

  target_link_libraries(runtest bls relic_s sodium)
  target_link_libraries(runbench bls relic_s sodium)

  #add_executable(bls_keytests  bls_key_tests.cpp)
  #target_link_libraries(bls_keytests coin secp256k1 bls ecdsa ${Boost_LIBRARIES} event pthread relic_s sodium gmp) 
endif()
