CMAKE_MINIMUM_REQUIRED(VERSION 2.8)

IF(NOT CMAKE_BUILD_TYPE)
  SET(CMAKE_BUILD_TYPE "Release")
  MESSAGE("Build type set to '${CMAKE_BUILD_TYPE}'")
ENDIF()

if(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
  list(APPEND CMAKE_PREFIX_PATH /opt/local/opt/qt5)
endif()

# For projects using Git, use git version patch
# Find Git Version Patch
FIND_PROGRAM(GIT git)
IF(GIT)
  EXECUTE_PROCESS(
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMAND ${GIT} rev-parse --short HEAD 
    OUTPUT_VARIABLE GIT_OUT OUTPUT_STRIP_TRAILING_WHITESPACE
    )
  SET(PATCH_OUT "0-${GIT_OUT}")
ELSE(GIT)
  SET(PATCH_OUT "0")
ENDIF(GIT)

SET(CLUB_VERSION_PATCH ${PATCH_OUT})


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

project(CLUB VERSION 0.9.0.0)

# Project Version - check later
SET(CLUB_VERSION_MAJOR "0")
SET(CLUB_VERSION_MINOR "9")
SET(CLUB_VERSION_REVISION "0")
SET(CLUB_VERSION_BUILD "0")


# If ccache is available, then use it.
find_program(CCACHE ccache)
if(CCACHE)
	set_property(GLOBAL PROPERTY RULE_LAUNCH_COMPILE ${CCACHE})
	set_property(GLOBAL PROPERTY RULE_LAUNCH_LINK ${CCACHE})
endif(CCACHE)


set(OPENSSL_VER "1.0.2n")
set(ROCKSDB_VER "5.11.3")

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

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ")


IF (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
	add_compile_options(-Wno-potentially-evaluated-expression -Wno-unused-const-variable -Wno-unused-function -Wno-deprecated-declarations -Wno-inaccessible-base -Wno-unused-local-typedefs -Wno-logical-op-parentheses -Wno-shift-negative-value -Wno-array-bounds)
	add_definitions("-DOS_MACOSX -DENABLE_ZMQ")
#	add_definitions("-Werror")
else()
	add_definitions("-Wno-cpp -Wno-unused-function")
	set(ANL anl)
endif()

add_definitions("-DHAVE_BUILD_INFO -DHAVE_CONFIG_H -D__STDC_FORMAT_MACROS -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=2")

add_definitions("-Wall -Wextra -Wformat -Wformat-security -Wno-unused-parameter -Wstack-protector -fstack-protector-all -fPIC")
#-fvisibility=hidden")

# run autogen.sh if missing header files from configure on Linux/Mac
if (EXISTS	"${CMAKE_CURRENT_SOURCE_DIR}/configure")
else()	
  execute_process(
	COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/autogen.sh
	WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
	)
endif()

# run configure if club_config.h doesn't exist, For Linux add "--with-incompatible-bdb"
if (EXISTS	"${CMAKE_CURRENT_SOURCE_DIR}/src/config/club-config.h")
else()	
	IF (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
	  execute_process(
		COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/configure 
		WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
		)
else()
	  execute_process(
		COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/configure  --with-incompatible-bdb --enable-module-recovery
		WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
		)
	endif()
endif()


SET(COMMON_A
  ./src/support/cleanse.cpp
	./src/spork.cpp
	./src/accumulators.cpp
	./src/accumulatormap.cpp
	./src/accumulatorcheckpoints.cpp
	./src/stakeinput.cpp
	./src/mintpool.cpp
	./src/protocol.cpp
	./src/pubkey.cpp
	./src/netbase.cpp
	./src/keystore.cpp
	./src/key.cpp
	./src/hash.cpp
	./src/core_write.cpp
	./src/core_read.cpp
	./src/compressor.cpp
	./src/coins.cpp
	./src/chainparams.cpp
	./src/base58.cpp
	./src/amount.cpp
	./src/allocators.cpp
  ./src/logging.cpp
  ./src/fs.cpp
	)

if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/clientversion.h.in")
  configure_file(${CMAKE_CURRENT_SOURCE_DIR}/src/clientversion.h.in ${CMAKE_CURRENT_SOURCE_DIR}/src/clientversion.h @ONLY)
else()
  message(WARNING "Using Versions defined in clientversion.h - is it up to date?")
endif()
   
    
add_custom_target(
  buildHeader
  COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/share/genbuild.sh ${CMAKE_CURRENT_SOURCE_DIR}/src/obj/build.h ${CMAKE_CURRENT_SOURCE_DIR}
  )


SET(UTIL_A
	./src/utiltime.cpp
	./src/utilmoneystr.cpp
	./src/util.cpp
	./src/sync.cpp
	./src/uint256.cpp
	./src/arith_uint256.cpp
	./src/random.cpp
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
	./src/rpc/rpcprotocol.cpp
	./src/rpc/rpcblockchain.cpp
	./src/rpc/rpcclient.cpp
	./src/rpc/rpcdump.cpp
	./src/rpc/rpcmining.cpp
	./src/rpc/rpcmisc.cpp
	./src/rpc/rpcnet.cpp
	./src/rpc/rpcrawtransaction.cpp
	./src/rpc/rpcserver.cpp
	./src/rpc/rpcwallet.cpp
	)


SET(CONSENSUS
	./src/utilstrencodings.cpp
	./src/pubkey.cpp
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
  # temp
  	./src/main_hdr.cpp
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
  ./src/blocksignature.cpp
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

SET(SCH_SOURCES
  ./src/scheduler.cpp
  )

IF (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
  set(OPENSSL_ROOT_DIR "/usr/local/Cellar/openssl/${OPENSSL_VER}/")
  set(ROCKSDB_ROOT_DIR "/usr/local/Cellar/rocksdb/${ROCKSDB_VER}/")
endif()

find_package(OpenSSL)
if (OPENSSL_FOUND)
  message(STATUS "Found OpenSSL")
endif()  

set(Boost_USE_STATIC_LIBS ON)
find_package( Boost COMPONENTS  filesystem thread)
link_directories ( ${Boost_LIBRARY_DIRS} )

include_directories(
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${CMAKE_CURRENT_SOURCE_DIR}/src/config
  ${CMAKE_CURRENT_SOURCE_DIR}/src/univalue/include
  ${CMAKE_CURRENT_SOURCE_DIR}/src/obj
  ${Boost_INCLUDE_DIRS}
  ${OPENSSL_INCLUDE_DIR}
  ${ROCKSDB_ROOT_DIR}/include
  ${CMAKE_CURRENT_SOURCE_DIR}/src/liblmdb
  ${CMAKE_CURRENT_SOURCE_DIR}/src/secp256k1
  ${CMAKE_CURRENT_SOURCE_DIR}/src/secp256k1/include
  ${CMAKE_CURRENT_SOURCE_DIR}/src/qt/
  ${CMAKE_CURRENT_SOURCE_DIR}/src/qt/forms
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
source_group("Uni" FILES ${UNI_SOURCES})
source_group("Zmq" FILES ${ZMQ_SOURCES})
source_group("Primitives" FILES ${PRIM})
source_group("CommonA" FILES ${COMMON_A})

IF (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
  add_library(coin ${HEADERS} ${CRYPTO_HEADERS} ${PRIMITIVE_HEADERS} ${WALLET_HEADERS} ${RPC_HEADERS} ${SOURCES} ${PRIM} ${WALLET} ${ZMQ_SOURCES} ${SCH_SOURCES} ${UNI_SOURCES} ${COMMON_A} ${UTIL_A} ${SERVER_A} ${RPC} ${CONSENSUS} ${CONSENSUS_CRYPTO})
ELSE()
  add_library(coin ${HEADERS} ${CRYPTO_HEADERS} ${PRIMITIVE_HEADERS} ${WALLET_HEADERS} ${RPC_HEADERS} ${SOURCES} ${PRIM} ${WALLET} ${SCH_SOURCES} ${UNI_SOURCES} ${COMMON_A} ${UTIL_A} ${SERVER_A} ${RPC} ${CONSENSUS} ${CONSENSUS_CRYPTO})
ENDIF()

set_property(TARGET coin PROPERTY CXX_STANDARD 11)
add_dependencies(coin buildHeader)

add_subdirectory(src/libzerocoin)
add_subdirectory(src/liblmdb)

set(SECP256
	./src/secp256k1/src/secp256k1.c
	./src/secp256k1/src/gen_context.c
	)
add_library(secp256k1 ${SECP256})

link_directories(${LMDB_ROOT_DIR}/lib)
link_directories(${ROCKSDB_ROOT_DIR}/lib)

add_executable(club-cli ${CMAKE_CURRENT_SOURCE_DIR}/src/club-cli.cpp)
set_property(TARGET club-cli PROPERTY CXX_STANDARD 11)
add_executable(clubd ${CMAKE_CURRENT_SOURCE_DIR}/src/clubd.cpp)
set_property(TARGET clubd PROPERTY CXX_STANDARD 11)

target_link_libraries(club-cli coin zerocoin lmdb rocksdb ${OPENSSL_LIBRARIES} ${Boost_LIBRARIES} event pthread) # ${ANL} ?

SET(LIBS_LIST coin secp256k1 gmp zerocoin lmdb rocksdb ${OPENSSL_LIBRARIES} ${Boost_LIBRARIES} ${ANL} miniupnpc event event_pthreads pthread)

IF (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
	target_link_libraries(clubd ${LIBS_LIST} /usr/local/lib/libzmq.a pthread )
else()
	target_link_libraries(clubd coin ${LIBS_LIST})
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
