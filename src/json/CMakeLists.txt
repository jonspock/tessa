CMAKE_MINIMUM_REQUIRED(VERSION 3.1.0 FATAL_ERROR)

add_library(sporklib ${BLS_HEADERS}
  ${CMAKE_CURRENT_SOURCE_DIR}/json_file.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/sporkdata.cpp
)

# For now just on Mac
if (${CMAKE_SYSTEM_NAME} MATCHES "XDarwin")
  add_executable(bls_keytests  bls_key_tests.cpp)
  target_link_libraries(bls_keytests coin secp256k1 bls ecdsa ${Boost_LIBRARIES} event pthread relic_s sodium gmp) 
endif()
