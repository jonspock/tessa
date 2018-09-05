CMAKE_MINIMUM_REQUIRED(VERSION 3.10)
project(TESSA VERSION 0.9.0.0)

# Parse Version into components
string(REPLACE "." ";" VERSION_LIST ${TESSA_VERSION})
list(GET VERSION_LIST 0 TESSA_VERSION_MAJOR)
list(GET VERSION_LIST 1 TESSA_VERSION_MINOR)
list(GET VERSION_LIST 2 TESSA_VERSION_REVISION)
list(GET VERSION_LIST 3 TESSA_VERSION_BUILD)

# For projects using Git, use git version patch
# Find Git Version Patch
FIND_PROGRAM(GIT git)
IF(GIT)
  EXECUTE_PROCESS(
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMAND ${GIT} rev-parse --short HEAD 
    OUTPUT_VARIABLE GIT_OUT OUTPUT_STRIP_TRAILING_WHITESPACE
    )
  MESSAGE(STATUS "GIT-out  ${GIT_OUT}")
ENDIF(GIT)

IF(GIT)
  EXECUTE_PROCESS(
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMAND ${GIT} describe
    OUTPUT_VARIABLE DESC_OUT OUTPUT_STRIP_TRAILING_WHITESPACE
    )
  MESSAGE(STATUS "For ${GIT} describe : Got ${DESC_OUT}")
ENDIF(GIT)

SET(TESSA_VERSION_PATCH ${GIT_OUT})

include(CheckSymbolExists)
include(CheckCXXCompilerFlag)

# Add path for custom modules
set(CMAKE_MODULE_PATH
	${CMAKE_MODULE_PATH}
	${CMAKE_CURRENT_SOURCE_DIR}/cmake/modules
)


# If ccache is available, then use it.
find_program(CCACHE ccache)
if(CCACHE)
	set_property(GLOBAL PROPERTY RULE_LAUNCH_COMPILE ${CCACHE})
	set_property(GLOBAL PROPERTY RULE_LAUNCH_LINK ${CCACHE})
endif(CCACHE)


find_program(
  CLANG_TIDY_EXE
  NAMES "clang-tidy"
  DOC "Path to clang-tidy executable"
  )
if(NOT CLANG_TIDY_EXE)
  message(STATUS "clang-tidy not found.")
else()
  message(STATUS "clang-tidy found: ${CLANG_TIDY_EXE}")
  set(DO_CLANG_TIDY "${CLANG_TIDY_EXE}" "-checks=*,-clang-analyzer-alpha.*,-fuchsia-default-arguments,-readability-inconsistent-declaration-parameter-name,-google-runtime-references,-hicpp-braces-around-statements")
endif()

# Default visibility is hidden on all targets.
#set(CMAKE_C_VISIBILITY_PRESET hidden)
#set(CMAKE_CXX_VISIBILITY_PRESET hidden)

# Add the magic taret check and check-all
add_custom_target(check-all)
add_custom_target(check)

IF(NOT CMAKE_BUILD_TYPE)
  SET(CMAKE_BUILD_TYPE "Release")
  MESSAGE("Build type set to '${CMAKE_BUILD_TYPE}'")
ENDIF()

if(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
  list(APPEND CMAKE_PREFIX_PATH /opt/local/opt/qt5)
endif()

if(CMAKE_CXX_FLAGS_RELEASE MATCHES DNDEBUG)
  string(REPLACE "-DNDEBUG" "" CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE}")
  string(REPLACE "-DNDEBUG" "" CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE}")
endif()

if(CMAKE_CXX_FLAGS_RELWITHDEBINFO MATCHES DNDEBUG)
  string(REPLACE "-DNDEBUG" "" CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELEASE}")
  string(REPLACE "-DNDEBUG" "" CMAKE_C_FLAGS_RELWITHDEBINFO "${CMAKE_C_FLAGS_RELEASE}")
endif()

if (POLICY CMP0048)
  # cmake warns if loaded from a min-3.0-required parent dir, so silence the warning:
  cmake_policy(SET CMP0048 NEW)
endif()


set(ROCKSDB_VER "5.14.2")

#---------------------------------------------------------------------------------------------------------
# Code coverage
#---------------------------------------------------------------------------------------------------------
if (${CMAKE_CXX_COMPILER_ID} STREQUAL "Clang")
    set(CMAKE_XCODE_ATTRIBUTE_GCC_VERSION "com.apple.compilers.llvm.clang.1_0")
    # Setup Code Coverage for XCode when requested
    if ($ENV{COVERAGE})
        message(STATUS "********* Setting up Xcode compile for Code coverage **********")
        set(CMAKE_XCODE_ATTRIBUTE_GCC_INSTRUMENT_PROGRAM_FLOW_ARCS "YES")
        set(CMAKE_XCODE_ATTRIBUTE_GCC_GENERATE_TEST_COVERAGE_FILES "YES")
        # Works for Clang->
				#set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -coverage")
    endif()
endif()

add_definitions("-DHAVE_CONFIG_H -D__STDC_FORMAT_MACROS -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=2")

# Enable warning
include(AddCompilerFlags)

add_c_compiler_flag(-Wnested-externs -Wstrict-prototypes)
add_compiler_flag(
	-Wall
	-Wextra
	-Wformat
	-Wvla
	-Wformat-security
	-Wno-cast-align
  -Wno-unused-function
  -Wpedantic
  # club added
	-Wno-unused-parameter
  -Wstack-protector
  -fstack-protector-all
)

#foreach(i ${CMAKE_CXX_COMPILE_FEATURES})
#  message("${i}")
#endforeach()

check_cxx_compiler_flag(-std=c++17 HAVE_FLAG_STD_CXX17)
if (HAVE_FLAG_STD_CXX17)
  set(CMAKE_CXX_STANDARD 17)
  message(STATUS "Compiler supports C++17")
else()
  message(STATUS "Compiler Doesn't support C++17")
  check_cxx_compiler_flag(-std=c++14 HAVE_FLAG_STD_CXX14)
  if (HAVE_FLAG_STD_CXX14)
    set(CMAKE_CXX_STANDARD 14)
    message(STATUS "Compiler supports C++14")
  else()
    check_cxx_compiler_flag(-std=c++11 HAVE_FLAG_STD_CXX11)
    if (HAVE_FLAG_STD_CXX11)
      set(CMAKE_CXX_STANDARD 11)
      message(STATUS "Compiler supports C++11")
    else()
      message(ERROR "Compile must at least support C++11")
    endif()
  endif()
endif()

IF (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
  add_compiler_flag(-Wrange-loop-analysis)
  # For 17 we need BOOST_NO_AUTO_PTR defined for boost
  check_symbol_exists(BOOST_NO_AUTO_PTR "sync.h" BOOST_NO_AUTO_PTR)
  if (BOOST_NO_AUTO_PTR)
	  add_definitions("-DOS_MACOSX -DENABLE_ZMQ")
  else()
    message(STATUS "Explicitly adding define for BOOST_NO_AUTO_PTR since not found")
	  add_definitions("-DOS_MACOSX -DENABLE_ZMQ -DBOOST_NO_AUTO_PTR")
  endif()
else()
	add_definitions("-Wno-cpp -Wno-unused-function")
	set(ANL anl)
endif()

add_definitions("-fPIC")

# run autogen.sh if missing header files from configure on Linux/Mac
if (EXISTS	"${CMAKE_CURRENT_SOURCE_DIR}/configure")
else()	
  execute_process(
	COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/autogen.sh
	WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
	)
endif()

# run configure if tessa_config.h doesn't exist, For Linux add "--with-incompatible-bdb"
if (EXISTS	"${CMAKE_CURRENT_BINARY_DIR}/src/tessa-config.h")
else()	
	IF (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
	  execute_process(
		COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/configure 
		WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
		)
else()
	  execute_process(
		COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/configure --with-unsupported-ssl --enable-module-recovery
		WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
		)
endif()
	  execute_process(
		COMMAND make
		WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/src/secp256k1
		)
endif()

SET(COMMON_A
  ./src/support/cleanse.cpp
  ./src/support/lockedpool.cpp
	./src/spork.cpp
	./src/accumulators.cpp
	./src/accumulatormap.cpp
	./src/accumulatorcheckpoints.cpp
	./src/stakeinput.cpp
	./src/mintpool.cpp
	./src/protocol.cpp
	./src/netbase.cpp
	./src/keystore.cpp
	./src/hash.cpp
	./src/core_write.cpp
	./src/core_read.cpp
	./src/compressor.cpp
	./src/coins.cpp
	./src/chainparams.cpp
	./src/base58.cpp
	./src/amount.cpp
  ./src/logging.cpp
  ./src/fs.cpp
	)

# No longer used    
add_custom_target(buildHeader
  COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/share/genbuild.sh ${CMAKE_CURRENT_SOURCE_DIR}/src/build.h ${CMAKE_CURRENT_SOURCE_DIR}
  )

SET(LIB_RAND
  ./src/rand_bignum.cpp
  ./src/random.cpp)


SET(UTIL_A
	./src/utiltime.cpp
	./src/utilmoneystr.cpp
	./src/util.cpp
	./src/sync.cpp
	./src/uint256.cpp
	./src/arith_uint256.cpp
	./src/clientversion.cpp
	./src/chainparamsbase.cpp
	)




SET(SERVER_A
	./src/addrman.cpp
	./src/bloom.cpp
	./src/chain.cpp
	./src/checkpoints.cpp
	./src/leveldbwrapper.cpp
	./src/dbwrapper.cpp
	./src/init.cpp
	./src/main.cpp
	./src/mainzero.cpp
	./src/mainfile.cpp
	./src/main_functions.cpp
	./src/warnings.cpp
  ./src/verifydb.cpp
	./src/block.cpp
	./src/blockundo.cpp
	./src/zerochain.cpp
	./src/merkleblock.cpp
	./src/miner.cpp
	./src/staker.cpp
	./src/net.cpp
	./src/noui.cpp
	./src/pow.cpp
	./src/rest.cpp
  )

SET(RPC
	./src/rpc/protocol.cpp
	./src/rpc/blockchain.cpp
	./src/rpc/client.cpp
	./src/rpc/dump.cpp
	./src/rpc/mining.cpp
	./src/rpc/misc.cpp
	./src/rpc/net.cpp
	./src/rpc/rawtransaction.cpp
	./src/rpc/server.cpp
	./src/rpc/wallet.cpp
	)


SET(CONSENSUS
	./src/utilstrencodings.cpp
	./src/uint256.cpp
	./src/hash.cpp
)	

SET(CONSENSUS_CRYPTO
	./src/crypto/chacha20.cpp
	./src/crypto/hmac_sha512.cpp
	./src/crypto/hmac_sha256.cpp
	./src/crypto/ripemd160.cpp
	./src/crypto/rfc6979_hmac_sha256.cpp
	./src/crypto/sha256.cpp
	./src/crypto/sha1.cpp
	./src/crypto/sha512.cpp
	./src/crypto/aes.cpp
	./src/crypto/ctaes/ctaes.c

  ./src/crypto/argon2/argon2.c
  ./src/crypto/argon2/best.c
  ./src/crypto/argon2/thread.c
  ./src/crypto/argon2/ref.c
  ./src/crypto/argon2/encoding.c
  ./src/crypto/argon2/core.c
  ./src/crypto/argon2/blake2b.c
)

SET(WALLET
  	./src/wallet/wallet_hdr.cpp
  	./src/wallet/wallet.cpp
	  ./src/wallet/wallettx.cpp
	  ./src/wallet/wallet_ismine.cpp
	  ./src/wallet/walletdb.cpp
	  ./src/wallet/wallet_functions.cpp
	  ./src/wallet/db.cpp
	  ./src/wallet/crypter.cpp
	  ./src/wallet/zerowallet.cpp
    )
  
SET(PRIM
  ./src/primitives/block.cpp
	./src/primitives/transaction.cpp
	./src/primitives/zerocoin.cpp
	./src/primitives/deterministicmint.cpp
  )

SET(SOURCES
#  	./src/main_hdr.cpp
  ./src/compat/strnlen.cpp
	./src/compat/glibcxx_compat.cpp
	./src/compat/glibc_sanity.cpp
	./src/kernel.cpp
	./src/miner.cpp
	./src/script/bitcoinconsensus.cpp
	./src/script/interpreter.cpp
	./src/script/script.cpp
	./src/script/script_error.cpp
	./src/script/sigcache.cpp
	./src/script/sign.cpp
	./src/script/standard.cpp
	./src/sporkdb.cpp
	./src/timedata.cpp
	./src/txdb.cpp
	./src/txmempool.cpp
	./src/denomination_functions.cpp
# NEW
	./src/zerotracker.cpp
  ./src/httprpc.cpp
  ./src/httpserver.cpp
  ./src/validationinterface.cpp
	)

SET(ZMQ_SOURCES
  ./src/zmq/zmqnotificationinterface.cpp
  ./src/zmq/zmqpublishnotifier.cpp
  ./src/zmq/zmqabstractnotifier.cpp
  )
  
SET(UNI_SOURCES
  ./src/univalue/lib/univalue_write.cpp
  ./src/univalue/lib/univalue_read.cpp
  ./src/univalue/lib/univalue.cpp
  )

if (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
  SET(SCH_SOURCES ./src/scheduler.cpp)
endif()

IF (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
  set(ROCKSDB_ROOT_DIR "/usr/local/Cellar/rocksdb/${ROCKSDB_VER}/")
endif()


find_package(sodium)
if (SODIUM_FOUND)
  message(STATUS "Found libsodium")
  message(STATUS "Sodium include dir = ${sodium_INCLUDE_DIR}")
endif()


set(Boost_USE_STATIC_LIBS ON)
find_package( Boost COMPONENTS  filesystem thread)
link_directories ( ${Boost_LIBRARY_DIRS} )


find_package(gmp)
if (GMP_FOUND)
  message(STATUS "Found libgmp")
endif()
  
include_directories(
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${CMAKE_CURRENT_SOURCE_DIR}/src/wallet
  ${CMAKE_CURRENT_SOURCE_DIR}/src/univalue/include
  ${CMAKE_CURRENT_SOURCE_DIR}/src/obj
  ${Boost_INCLUDE_DIRS}
  ${GMP_INCLUDE_DIR}
  ${ROCKSDB_ROOT_DIR}/include
  ${sodium_INCLUDE_DIR}

  ${CMAKE_CURRENT_SOURCE_DIR}/src/relic/include
  
  ${CMAKE_CURRENT_SOURCE_DIR}/src/liblmdb
  ${CMAKE_CURRENT_SOURCE_DIR}/src/secp256k1
  ${CMAKE_CURRENT_SOURCE_DIR}/src/secp256k1/include
  ${CMAKE_CURRENT_SOURCE_DIR}/src/qt/
  ${CMAKE_CURRENT_SOURCE_DIR}/src/qt/forms
  # For relic_conf.h file
  ${CMAKE_CURRENT_BINARY_DIR}
  # For config file tessa-config.h
  ${CMAKE_CURRENT_BINARY_DIR}/src
  )

#file(GLOB_RECURSE HEADERS *.h)
file(GLOB HEADERS ${CMAKE_CURRENT_SOURCE_DIR}/src/*.h)

file(GLOB CRYPTO_HEADERS ${CMAKE_CURRENT_SOURCE_DIR}/src/crypto/*.h)
source_group("CryptHeaders" FILES ${CRYPTO_HEADERS})

file(GLOB PRIMITIVE_HEADERS ${CMAKE_CURRENT_SOURCE_DIR}/src/primitives/*.h)
source_group("PrimitiveHeaders" FILES ${PRIMITIVE_HEADERS})

file(GLOB WALLET_HEADERS ${CMAKE_CURRENT_SOURCE_DIR}/src/wallet/*.h )
source_group("WalletHeaders" FILES ${WALLET_HEADERS})

file(GLOB RPC_HEADERS ${CMAKE_CURRENT_SOURCE_DIR}/src/rpc/*.h )
source_group("RpcHeaders" FILES ${RPC_HEADERS})

source_group("Wallet" FILES ${WALLET})
source_group("Rpc" FILES ${RPC})
source_group("Crypt" FILES ${CONSENSUS_CRYPTO})
source_group("Util" FILES ${UTIL_A})
source_group("Rand" FILES ${LIB_RAND})
source_group("Uni" FILES ${UNI_SOURCES})
source_group("Zmq" FILES ${ZMQ_SOURCES})
source_group("Primitives" FILES ${PRIM})
source_group("CommonA" FILES ${COMMON_A})

add_library(coin ${HEADERS} ${CRYPTO_HEADERS} ${PRIMITIVE_HEADERS} ${WALLET_HEADERS} ${RPC_HEADERS} 
  ${LIB_RAND} ${SOURCES} ${PRIM} ${WALLET} ${ZMQ_SOURCES} ${SCH_SOURCES} ${UNI_SOURCES} ${COMMON_A}
  ${UTIL_A} ${SERVER_A} ${RPC} ${CONSENSUS} ${CONSENSUS_CRYPTO})

if(CLANG_TIDY_EXE)
###  set_target_properties(coin PROPERTIES CXX_CLANG_TIDY "${DO_CLANG_TIDY}")
endif()

# not used
#add_dependencies(coin buildHeader)

add_subdirectory(src)

add_subdirectory(src/libzerocoin)
add_subdirectory(src/liblmdb)

set(SECP256
	./src/secp256k1/src/secp256k1.c
	./src/secp256k1/src/gen_context.c
	)
add_library(secp256k1 ${SECP256})

add_subdirectory(src/ecdsa)

link_directories(${LMDB_ROOT_DIR}/lib)
link_directories(${ROCKSDB_ROOT_DIR}/lib)

add_executable(tessa-cli ${CMAKE_CURRENT_SOURCE_DIR}/src/tessa-cli.cpp)
add_executable(tessad ${CMAKE_CURRENT_SOURCE_DIR}/src/tessad.cpp)

#------------------- ------------------- ------------------- ------------------- -------------------
# For Relic
#------------------- ------------------- ------------------- ------------------- -------------------
##SET(CMAKE_VERBOSE_MAKEFILE OFF)
set(WORD 64 CACHE INTEGER "")
set(TIMER "CYCLE" CACHE STRING "")
set(CHECK "off" CACHE STRING "")
set(VERBS "off" CACHE STRING "")
set(ALLOC "AUTO" CACHE STRING "")
set(SHLIB "OFF" CACHE STRING "")

set(FP_PRIME 381 CACHE INTEGER "")

IF (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
  set(DSEED "UDEV" CACHE STRING "")
  set(STBIN "OFF" CACHE STRING "")
  set(FP_QNRES "off" CACHE STRING "")
ELSE()
  set(DSEED "DEV" CACHE STRING "")
  set(STBIN "OFF" CACHE STRING "")
  set(FP_QNRES "on" CACHE STRING "")
ENDIF()

set(FP_METHD "INTEG;INTEG;INTEG;MONTY;LOWER;SLIDE" CACHE STRING "")
set(COMP "-O3 -funroll-loops -fomit-frame-pointer -march=native -mtune=native" CACHE STRING "")
set(FP_PMERS "off" CACHE STRING "")
set(FPX_METHD "INTEG;INTEG;LAZYR" CACHE STRING "")
set(EP_PLAIN "off" CACHE STRING "")
set(EP_SUPER "off" CACHE STRING "")
# Disable relic tests and benchmarks
set(TESTS 0 CACHE INTEGER "")
set(BENCH 0 CACHE INTEGER "")

set(PP_EXT "LAZYR" CACHE STRING "")
set(PP_METHD "LAZYR;OATEP" CACHE STRING "")

add_subdirectory(src/relic)
add_subdirectory(src/bls)

#------------------- ------------------- ------------------- ------------------- -------------------

SET(LIBS_LIST coin secp256k1 gmp zerocoin ecdsa lmdb rocksdb ${Boost_LIBRARIES} ${ANL}
  miniupnpc event event_pthreads pthread sodium)


target_link_libraries(tessa-cli coin zerocoin ecdsa lmdb rocksdb ${Boost_LIBRARIES} event pthread gmp sodium) # ${ANL} ?



IF (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
	target_link_libraries(tessad ${LIBS_LIST} zmq pthread )
else()
	target_link_libraries(tessad coin ${LIBS_LIST})
endif()

#	miniupnpc pthread)

add_subdirectory(src/qt)

#---------------------------------------------------------------------------------------------------------
# Create a target in Xcode to setup coverage script
#---------------------------------------------------------------------------------------------------------
if ($ENV{COVERAGE})
    add_custom_target(xcov COMMAND ./XcodeCoverage/exportenv.sh )
endif()

#add_subdirectory(src/test)
