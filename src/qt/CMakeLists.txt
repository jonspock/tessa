CMAKE_MINIMUM_REQUIRED(VERSION 3.1)

# Project Info
SET(PROJECT_NAME_SHORT   "TessaQt")
SET(PROJECT_NAME_LONG    "CMake Tessa Qt project")
SET(PROJECT_DESCRIPTION  "CMake build system template for Qt based projects")
SET(PROJECT_COPYRIGHT    "Copyright (C) 2018 TessaChain Developers")
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

# Project Version
SET(CPACK_PACKAGE_VERSION_MAJOR ${TESSA_VERSION_MAJOR})
SET(CPACK_PACKAGE_VERSION_MINOR ${TESSA_VERSION_MINOR})
SET(CPACK_PACKAGE_VERSION_PATCH ${TESSA_VERSION_REVISION})
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


find_package(QREncode)
if (QREncode_FOUND)
  add_definitions(-DUSE_QRCODE)
  message(STATUS "Found QREncode")
  set(QR_LIB qrencode)
else()
  message(STATUS " QREncode NOT FOUND ")
endif()  


unset(Qt5Enables)
foreach(comp Widgets Core Gui Network LinguistTools DBus)
  find_package(Qt5${comp} REQUIRED)
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

include_directories(${include_directories} 
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/forms
  )


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
	coinaddressvalidator.cpp
	coinamountfield.cpp
	coingui.cpp
	coinunits.cpp
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
  set(MAC_LIBS mac_stuff)
endif()


add_library(qt_stuff ${QT_FILES} ${UI_HEADERS} $<$<BOOL:${DARWIN}>:../rpc/client.cpp> )
# needed for linux compile integrity
target_link_libraries(qt_stuff ${FS_LIBS})

file(GLOB QT_TRANSLATIONS_FILES locale/*.ts)

SET(QT_TRANSLATIONS_FILES
	${CMAKE_CURRENT_SOURCE_DIR}/locale/coin_en.ts
	${CMAKE_CURRENT_SOURCE_DIR}/locale/coin_en_US.ts
	)

get_filename_component(QM_FILES_BINARY_DIR "${CMAKE_CURRENT_SOURCE_DIR}/locale" ABSOLUTE)
set_source_files_properties(${QT_TRANSLATIONS_FILES} PROPERTIES OUTPUT_LOCATION "${QM_FILES_BINARY_DIR}")

QT5_ADD_TRANSLATION( QM ${QT_TRANSLATIONS_FILES} )
add_custom_target (translations_target DEPENDS ${QM}) 

QT5_ADD_RESOURCES(QRC_RESOURCE coin.qrc)
QT5_ADD_RESOURCES(QRC_LOCALE_RESOURCE coin_locale.qrc)

add_executable(tessa-qt coin.cpp ${QM} ${QRC_RESOURCE} ${QRC_LOCALE_RESOURCE} ${GUI_FORM_HEADERS})

add_dependencies(tessa-qt translations_target)

if (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
  TARGET_LINK_LIBRARIES(tessa-qt ${LIBS_LIST} qt_stuff ${MAC_LIBS} ${Qt5_LIBRARIES} ${QR_LIB} ${ZMQ_LIB} "-framework Cocoa")
else()
	TARGET_LINK_LIBRARIES(tessa-qt ${LIBS_LIST} qt_stuff ${Qt5_LIBRARIES} ${ANL} ${QR_LIB} ${ZMQ_LIB} event event_pthreads pthread)
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
    SET(MACOSX_BUNDLE_BUNDLE_NAME tessa-qt)
    SET(MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION})
    SET(MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION})
    SET(MACOSX_BUNDLE_LONG_VERSION_STRING "Version ${PROJECT_VERSION}")
ENDIF(APPLE)

SET(CPACK_PACKAGE_EXECUTABLES "tessa-qt" "tessa-qt")
INCLUDE(CPack)


#INSTALL(TARGETS tessa-qt DESTINATION bin)
