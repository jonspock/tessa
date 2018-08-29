CMAKE_MINIMUM_REQUIRED(VERSION 3.1.0 FATAL_ERROR)

include_directories(
  ${INCLUDE_DIRECTORIES}
  ${CMAKE_CURRENT_SOURCE_DIR}/../relic/include
  ${CMAKE_BINARY_DIR}/../relic/include
  )

file(GLOB BLS_HEADERS ${CMAKE_CURRENT_SOURCE_DIR}/*.hpp)
source_group("BlsHeaders" FILES ${BLS_HEADERS})

add_library(bls_chain ${CMAKE_CURRENT_SOURCE_DIR}/chaincode.cpp)

add_library(bls ${BLS_HEADERS}
  ${CMAKE_CURRENT_SOURCE_DIR}/extendedpublickey.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/extendedprivatekey.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/chaincode.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/blssignature.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/blspublickey.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/blsprivatekey.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/bls.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/aggregationinfo.cpp
)


