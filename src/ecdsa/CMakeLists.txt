CMAKE_MINIMUM_REQUIRED(VERSION 3.1.0 FATAL_ERROR)

file(GLOB ECDSA_HEADERS ${CMAKE_CURRENT_SOURCE_DIR}/*.hpp)
source_group("EcdsaHeaders" FILES ${ECDSA_HEADERS})

add_library(ecdsa ${ECDSA_HEADERS}
  ${CMAKE_CURRENT_SOURCE_DIR}/blocksignature.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/pubkey.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/key.cpp
)

if (ENABLE_TESTS)
  add_executable(keytests  key_tests.cpp)
  target_link_libraries(keytests coin secp256k1 ecdsa ${Boost_LIBRARIES} event pthread gmp sodium) 
endif()
