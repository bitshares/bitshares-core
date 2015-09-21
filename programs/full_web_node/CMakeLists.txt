#cmake_minimum_required(VERSION 2.8.11)

#project(full_web_node)

# Find includes in corresponding build directories
set(CMAKE_INCLUDE_CURRENT_DIR ON)
# Instruct CMake to run moc automatically when needed.
set(CMAKE_AUTOMOC ON)

find_package(Qt5Core)
find_package(Qt5Quick)
find_package(Qt5WebEngine)

file(GLOB QML qml/*)
file(GLOB WEB web/*)
qt5_add_resources(QML_QRC qml/qml.qrc)
qt5_add_resources(WEB_QRC web/web.qrc)



set( APP_NAME "GrapheneTest3" )

set( CPACK_BUNDLE_NAME ${APP_NAME} )
set( CPACK_PACKAGE_NAME ${CPACK_BUNDLE_NAME} )
set( CPACK_PACKAGE_EXECUTABLES qt_wallet ${CPACK_BUNDLE_NAME} )
set( CPACK_PACKAGE_DESCRIPTION_SUMMARY "package description summary" )
set( CPACK_PACKAGE_VENDOR "BitShares" )
set( CPACK_PACKAGE_VERSION_MAJOR ${VERSION_MAJOR} )
set( CPACK_PACKAGE_VERSION_MINOR ${VERSION_MINOR} )
set( CPACK_PACKAGE_VERSION_PATCH ${VERSION_PATCH} )
set( CPACK_DMG_VOLUME_NAME ${CPACK_BUNDLE_NAME} )
set( CPACK_PACKAGE_FILE_NAME ${CPACK_BUNDLE_NAME} )
get_filename_component( QT_PATH ${Qt5Core_DIR}/../../../ ABSOLUTE )



IF( APPLE )
    set ( OSX_ICON_FILE "${CMAKE_CURRENT_SOURCE_DIR}/images/bitshares.icns" )
    SET_SOURCE_FILES_PROPERTIES( "${OSX_ICON_FILE}" PROPERTIES MACOSX_PACKAGE_LOCATION Resources )
    SET( MACOSX_BUNDLE_BUNDLE_NAME ${CPACK_BUNDLE_NAME} )
    SET( MACOSX_BUNDLE_EXECUTABLE_NAME ${CPACK_BUNDLE_NAME} )
    SET( MACOSX_BUNDLE_GUI_IDENTIFIER "org.cryptonomex.${CPACK_BUNDLE_NAME}" )
    SET( MACOSX_BUNDLE_ICON_FILE "bitshares.icns" )
    SET( MACOSX_BUNDLE_INFO_STRING "Graphene v${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_PATCH}" )
    SET( MACOSX_BUNDLE_SHORT_VERSION_STRING "${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_PATCH}" )
    SET( MACOSX_BUNDLE_LONG_VERSION_STRING "${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_PATCH}" )
    SET( MACOSX_BUNDLE_BUNDLE_VERSION "${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_PATCH}" )
    SET( MACOSX_BUNDLE_COPYRIGHT "(C) 2014 Cryptonomex" )
#    SET( MACOSX_BUNDLE_URL_HANDLER_NAME "${MACOSX_BUNDLE_GUI_IDENTIFIER}" )
#    SET( MACOSX_BUNDLE_URL_HANDLER_SCHEME "${CUSTOM_URL_SCHEME}" )
#    set_source_files_properties("images/bitshares.icns" PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
ENDIF()






add_executable( ${APP_NAME}  WIN32 MACOSX_BUNDLE BlockChain.cpp main.cpp ${QML_QRC} ${WEB_QRC} ${QML})

target_link_libraries(${APP_NAME} PRIVATE Qt5::Core Qt5::Quick Qt5::WebEngine
   graphene_chain graphene_egenesis_full graphene_utilities fc graphene_account_history graphene_market_history graphene_app )


#install( TARGETS
#   ${APP_NAME}
#   RUNTIME DESTINATION bin
#   LIBRARY DESTINATION lib
#   ARCHIVE DESTINATION lib
#)


ADD_CUSTOM_COMMAND(TARGET ${APP_NAME} POST_BUILD ${POST_BUILD_STEP_COMMANDS}
  COMMENT "Copying binaries and other files into target directory."
)

ADD_CUSTOM_COMMAND(TARGET ${APP_NAME} PRE_BUILD ${PRE_BUILD_STEP_COMMANDS}
  COMMENT "Copying static resource files to build directory."
)

include( DeployQt4 )
include( InstallRequiredSystemLibraries )
install( TARGETS ${APP_NAME} DESTINATION "." )

IF( APPLE )
  set( CPACK_GENERATOR "DragNDrop" )
  include( CPack )
  set( PLUGINS "")
  list( APPEND PLUGINS "${QT_PATH}/plugins/platforms/libqcocoa.dylib" )
  list( APPEND PLUGINS "${QT_PATH}/plugins/imageformats/libqjpeg.dylib" )
  install_qt4_executable( ${APP_NAME}.app "${PLUGINS}" )
ENDIF()

