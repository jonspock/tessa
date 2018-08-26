CMAKE_MINIMUM_REQUIRED(VERSION 3.1)

# Project Info
SET(PROJECT_NAME_SHORT   "ClubQt")
SET(PROJECT_NAME_LONG    "CMake Club Qt project")
SET(PROJECT_DESCRIPTION  "CMake build system template for Qt based projects")
SET(PROJECT_COPYRIGHT    "Copyright (C) 2018 ClubChain Developers")
SET(PROJECT_CONTACT      "tessacoin@protonmail.com")
SET(PROJECT_VENDOR       "Tessa Coin")
SET(ORG_WEBSITE          "http://tessacoin.org")

# Configuring build type
IF(NOT CMAKE_BUILD_TYPE)
    SET(CMAKE_BUILD_TYPE "Debug" CACHE STRING
        "Choose the type of build, options are: None Debug Release RelWithDebInfo MinSizeRel."
        FORCE)
    MESSAGE("Setting build type to '${CMAKE_BUILD_TYPE}'")
ELSE(NOT CMAKE_BUILD_TYPE)
    MESSAGE("Build type set to '${CMAKE_BUILD_TYPE}'")
ENDIF(NOT CMAKE_BUILD_TYPE)

# For projects using Git, use git version patch
IF("${CMAKE_BUILD_TYPE}" STREQUAL "Release")
    SET(PATCH_OUT "0")
ELSE("${CMAKE_BUILD_TYPE}" STREQUAL "Release")
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
ENDIF("${CMAKE_BUILD_TYPE}" STREQUAL "Release")

SET(PROJECT_VERSION_PATCH ${PATCH_OUT})

# Project Version
SET(CPACK_PACKAGE_VERSION_MAJOR "1")
SET(CPACK_PACKAGE_VERSION_MINOR "0")
SET(CPACK_PACKAGE_VERSION_PATCH ${PROJECT_VERSION_PATCH})
SET(CPACK_PACKAGE_VERSION ${CPACK_PACKAGE_VERSION_MAJOR}.${CPACK_PACKAGE_VERSION_MINOR}.${CPACK_PACKAGE_VERSION_PATCH})
SET(PROJECT_VERSION_MAJOR ${CPACK_PACKAGE_VERSION_MAJOR}.${CPACK_PACKAGE_VERSION_MINOR})
SET(PROJECT_VERSION ${CPACK_PACKAGE_VERSION})

MESSAGE(STATUS "Version: ${PROJECT_VERSION}")

STRING(TOLOWER ${PROJECT_NAME_SHORT} PROJECT_NAME_SHORT_LOWER)
PROJECT(${PROJECT_NAME_SHORT_LOWER}-${PROJECT_VERSION})

# System on which project is built
SITE_NAME(HOSTNAME)
SET(SYSNAME ${CMAKE_SYSTEM}-${CMAKE_SYSTEM_PROCESSOR})

# CMake Settings
SET(CMAKE_COLOR_MAKEFILE ON)
#SET(CMAKE_VERBOSE_MAKEFILE OFF)

set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
#set(CMAKE_AUTOUIC ON)

IF (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
  add_definitions("-DENABLE_ZMQ")
endif()

unset(Qt5Enables)
foreach(comp Widgets Core Gui Network LinguistTools DBus)
  find_package(Qt5${comp})
  set(Qt5Enables "${Qt5Enables};Qt5${comp}_FOUND")
  if(Qt5${comp}_FOUND)
    MESSAGE(STATUS "FOUND QT5${comp}")
    include_directories(${include_directories} ${Qt5${comp}_INCLUDE_DIRS} ${QT_USE_FILE})
    add_definitions(${Qt5${comp}_DEFINITIONS})
    list(APPEND Qt5_LIBRARIES ${Qt5${comp}_LIBRARIES})
  else()
    MESSAGE(WARNING "Cant find Qt5${comp}")
  endif()
endforeach(comp)

find_package(Qt5DBus)
if (Qt5DBus_FOUND)
  include_directories(${include_directories} "/usr/local/opt/qt5/include/QtDBus/")
  add_compile_options("-DUSE_DBUS")
endif()

# Why isn't this done automatically??
if (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
  set(CMAKE_AUTOMOC_MOC_OPTIONS "-DQ_OS_MAC")
endif()


file(GLOB LOCAL_QT_HEADERS ${CMAKE_CURRENT_SOURCE_DIR}/src/*.h)
source_group("QtHeaders" FILES ${LOCAL_QT_HEADERS})

set(GUI_FORMS
  ${CMAKE_CURRENT_SOURCE_DIR}/forms/addressbookpage.ui
  ${CMAKE_CURRENT_SOURCE_DIR}/forms/askpassphrasedialog.ui
  ${CMAKE_CURRENT_SOURCE_DIR}/forms/coincontroldialog.ui
  ${CMAKE_CURRENT_SOURCE_DIR}/forms/zerocontroldialog.ui
  ${CMAKE_CURRENT_SOURCE_DIR}/forms/blockexplorer.ui
  ${CMAKE_CURRENT_SOURCE_DIR}/forms/editaddressdialog.ui
  ${CMAKE_CURRENT_SOURCE_DIR}/forms/helpmessagedialog.ui
  ${CMAKE_CURRENT_SOURCE_DIR}/forms/intro.ui
  ${CMAKE_CURRENT_SOURCE_DIR}/forms/multisenddialog.ui
  ${CMAKE_CURRENT_SOURCE_DIR}/forms/multisigdialog.ui
  ${CMAKE_CURRENT_SOURCE_DIR}/forms/privacydialog.ui
  ${CMAKE_CURRENT_SOURCE_DIR}/forms/openuridialog.ui
  ${CMAKE_CURRENT_SOURCE_DIR}/forms/optionsdialog.ui
  ${CMAKE_CURRENT_SOURCE_DIR}/forms/overviewpage.ui
  ${CMAKE_CURRENT_SOURCE_DIR}/forms/receivecoinsdialog.ui
  ${CMAKE_CURRENT_SOURCE_DIR}/forms/receiverequestdialog.ui
  ${CMAKE_CURRENT_SOURCE_DIR}/forms/rpcconsole.ui
  ${CMAKE_CURRENT_SOURCE_DIR}/forms/sendcoinsdialog.ui
  ${CMAKE_CURRENT_SOURCE_DIR}/forms/sendcoinsentry.ui
  ${CMAKE_CURRENT_SOURCE_DIR}/forms/signverifymessagedialog.ui
  ${CMAKE_CURRENT_SOURCE_DIR}/forms/transactiondescdialog.ui
  )
  
QT5_WRAP_UI( UI_HEADERS ${GUI_FORMS})

SET(QT_FILES
	../script/standard.cpp
	../rpc/client.cpp
	multisenddialog.cpp
	multisigdialog.cpp
	bantablemodel.cpp
	blockexplorer.cpp
	bitcoinaddressvalidator.cpp
	bitcoinamountfield.cpp
	bitcoingui.cpp
	bitcoinunits.cpp
	clientmodel.cpp
	csvmodelwriter.cpp
	guiutil.cpp
	intro.cpp
	networkstyle.cpp
	notificator.cpp
	optionsdialog.cpp
	optionsmodel.cpp
	peertablemodel.cpp
	platformstyle.cpp
	qvalidatedlineedit.cpp
	qvaluecombobox.cpp
	rpcconsole.cpp
	splashscreen.cpp
	trafficgraphwidget.cpp
	utilitydialog.cpp
	winshutdownmonitor.cpp
	addressbookpage.cpp
	addresstablemodel.cpp
	askpassphrasedialog.cpp
	coincontroldialog.cpp
	zerocontroldialog.cpp
	coincontroltreewidget.cpp
  privacydialog.cpp
	editaddressdialog.cpp
	openuridialog.cpp
	overviewpage.cpp
	receivecoinsdialog.cpp
	receiverequestdialog.cpp
	recentrequeststablemodel.cpp
	sendcoinsdialog.cpp
	sendcoinsentry.cpp
	signverifymessagedialog.cpp
	transactiondesc.cpp
	transactiondescdialog.cpp
	transactionfilterproxy.cpp
	transactionrecord.cpp
	transactiontablemodel.cpp
	transactionview.cpp
	walletframe.cpp
	walletmodel.cpp
	walletmodeltransaction.cpp
	walletview.cpp
	)

  

if (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
add_library(mac_stuff
	macdockiconhandler.mm
	macnotificationhandler.mm
	)
set_property(TARGET mac_stuff PROPERTY CXX_STANDARD 11)
endif()


if (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
	ADD_LIBRARY(qt_stuff ${QT_FILES} ${UI_HEADERS})
else()
	ADD_LIBRARY(qt_stuff ${QT_FILES} ../rpc/client.cpp ${UI_HEADERS})
endif()

set_property(TARGET qt_stuff PROPERTY CXX_STANDARD 11)
#target_include_directories(qt_stuff PUBLIC {OPENSSL_INCLUDE_DIR})
file(GLOB QT_TRANSLATIONS_FILES locale/*.ts)

SET(QT_TRANSLATIONS_FILES
	${CMAKE_CURRENT_SOURCE_DIR}/locale/club_en.ts
	${CMAKE_CURRENT_SOURCE_DIR}/locale/club_en_US.ts
	)

get_filename_component(QM_FILES_BINARY_DIR "${CMAKE_CURRENT_SOURCE_DIR}/locale" ABSOLUTE)
set_source_files_properties(${QT_TRANSLATIONS_FILES} PROPERTIES OUTPUT_LOCATION "${QM_FILES_BINARY_DIR}")

QT5_ADD_TRANSLATION( QM ${QT_TRANSLATIONS_FILES} )
add_custom_target (translations_target DEPENDS ${QM}) 

QT5_ADD_RESOURCES(QRC_RESOURCE club.qrc)
QT5_ADD_RESOURCES(QRC_LOCALE_RESOURCE club_locale.qrc)

add_executable(club-qt club.cpp ${QM} ${QRC_RESOURCE} ${QRC_LOCALE_RESOURCE} ${GUI_FORM_HEADERS})
add_dependencies(club-qt translations_target)
set_property(TARGET club-qt PROPERTY CXX_STANDARD 11)

if (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
  # ORIG TARGET_LINK_LIBRARIES(club-qt coin qt_stuff mac_stuff db_cxx db secp256k1 bitcoin_crypto zerocoin rocksdb rand ${Boost_LIBRARIES} ${ANL} qrencode miniupnpc /usr/local/lib/libevent.a /usr/local/lib/libevent_pthreads.a  /usr/local/lib/libzmq.a pthread "-framework Cocoa")

# STATIC LINK SPECIFIC FOR MAC-DEPLOY TARGET
  
## Ref  TARGET_LINK_LIBRARIES(club-qt coin qt_stuff mac_stuff ${Qt5_LIBRARIES} lmdb secp256k1 gmp zerocoin rocksdb rand ${Boost_LIBRARIES} ${ANL} /usr/local/opt/protobuf/lib/libprotobuf.a /usr/local/lib/libqrencode.a /usr/local/lib/libminiupnpc.a /usr/local/lib/libevent.a /usr/local/lib/libevent_pthreads.a  /usr/local/lib/libzmq.a "-framework Cocoa")
  TARGET_LINK_LIBRARIES(club-qt ${LIBS_LIST} qt_stuff mac_stuff ${Qt5_LIBRARIES} qrencode /usr/local/lib/libzmq.a "-framework Cocoa")
else()
	TARGET_LINK_LIBRARIES(club-qt coin qt_stuff ${Qt5_LIBRARIES} lmdb secp256k1 gmp zerocoin rocksdb rand ${Boost_LIBRARIES} ${ANL} miniupnpc qrencode event event_pthreads pthread )
endif()

#############################################################
# Enable packaging

INCLUDE(InstallRequiredSystemLibraries)

SET(CPACK_SYSTEM_NAME ${CMAKE_SYSTEM_NAME})
SET(CPACK_PACKAGE_NAME ${PROJECT_NAME_SHORT})
SET(CPACK_PACKAGE_DESCRIPTION "${PROJECT_NAME_LONG}")
SET(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${PROJECT_NAME_SHORT} - ${PROJECT_DESCRIPTION}")
SET(CPACK_PACKAGE_VENDOR ${PROJECT_VENDOR})
SET(CPACK_PACKAGE_CONTACT ${PROJECT_CONTACT})
SET(CPACK_PACKAGE_DESCRIPTION_FILE "${CMAKE_SOURCE_DIR}/README.md")
SET(CPACK_RESOURCE_FILE_README ${CMAKE_SOURCE_DIR}/README.md)
SET(CPACK_PACKAGE_INSTALL_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")

# Force Package Name
SET(CPACK_PACKAGE_FILE_NAME ${PROJECT_NAME}-${CPACK_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR})

# Mac App Bundle
IF(APPLE)
    SET(CPACK_GENERATOR "DragNDrop")
    # Libraries are bundled directly
    SET(CPACK_COMPONENT_LIBRARIES_HIDDEN TRUE)
    # Bundle Properties
    SET(MACOSX_BUNDLE_BUNDLE_NAME club-qt)
    SET(MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION})
    SET(MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION})
    SET(MACOSX_BUNDLE_LONG_VERSION_STRING "Version ${PROJECT_VERSION}")
ENDIF(APPLE)

SET(CPACK_PACKAGE_EXECUTABLES "club-qt" "club-qt")
INCLUDE(CPack)


#INSTALL(TARGETS club-qt DESTINATION bin)
