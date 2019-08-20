# Defines BitShares library target.
cmake_minimum_required( VERSION 3.2 FATAL_ERROR )
project( BitShares LANGUAGES CXX C)

set( BLOCKCHAIN_NAME "BitShares" )

set( CLI_CLIENT_EXECUTABLE_NAME graphene_client )
set( GUI_CLIENT_EXECUTABLE_NAME BitShares )
set( CUSTOM_URL_SCHEME "gcs" )
set( INSTALLER_APP_ID "68ad7005-8eee-49c9-95ce-9eed97e5b347" )

set( CMAKE_CXX_STANDARD 14 )
set( CMAKE_CXX_STANDARD_REQUIRED ON )

if( "${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU" )
  set( CMAKE_CXX_EXTENSIONS ON ) # for __int128 support
else( "${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU" )
  set( CMAKE_CXX_EXTENSIONS OFF )
endif( "${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU" )

# http://stackoverflow.com/a/18369825
if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
    if (CMAKE_CXX_COMPILER_VERSION VERSION_LESS 4.8)
        message(FATAL_ERROR "GCC version must be at least 4.8!")
    endif()
elseif ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
    if (CMAKE_CXX_COMPILER_VERSION VERSION_LESS 3.3)
        message(FATAL_ERROR "Clang version must be at least 3.3!")
    endif()
endif()

list( APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/CMakeModules" )

include(CheckCCompilerFlag)
include(Utils)

# function to help with cUrl
macro(FIND_CURL)
   if (NOT WIN32 AND NOT APPLE AND CURL_STATICLIB)
      set (OLD_SUFFIXES ${CMAKE_FIND_LIBRARY_SUFFIXES})
      set (CMAKE_FIND_LIBRARY_SUFFIXES .a)
      find_package(CURL REQUIRED)
      list(APPEND CURL_LIBRARIES ssl crypto)
      set (CMAKE_FIND_LIBRARY_SUFFIXES ${OLD_SUFFIXES})
   else (NOT WIN32 AND NOT APPLE AND CURL_STATICLIB)
      find_package(CURL REQUIRED)
   endif (NOT WIN32 AND NOT APPLE AND CURL_STATICLIB)
endmacro()

# Fortify source
if (CMAKE_COMPILER_IS_GNUCXX)
	if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
		message ("-- Setting optimizations for clang++")
		set(CMAKE_CXX_FLAGS_RELEASE "-D_FORTIFY_SOURCE=2 -O3 -DNDEBUG=1")
		set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "-D_FORTIFY_SOURCE=2 -O3 -DNDEBUG=1 -g")

		# check and add data execution prevention
		message ("-- Enabling data execution prevention")
		add_linker_flag("-fsanitize=safe-stack")

		# check and add Stack-based buffer overrun detection
		set(CMAKE_REQUIRED_FLAGS "-fstack-protector")
		check_c_compiler_flag("" HAVE_STACKPROTECTOR)
		if(HAVE_STACKPROTECTOR)
			message ("-- Enabling stack-based buffer overrun detection")
			add_flag_append(CMAKE_C_FLAGS "-fstack-protector")
			add_flag_append(CMAKE_CXX_FLAGS "-fstack-protector")
		endif()
	else ()
		message ("-- Setting optimizations for g++")
		set(CMAKE_CXX_FLAGS_RELEASE "-D_FORTIFY_SOURCE=2 -O3 -DNDEBUG=1")
		set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "-D_FORTIFY_SOURCE=2 -O3 -DNDEBUG=1 -g")

		# check and add data execution prevention
		set(CMAKE_REQUIRED_FLAGS "-Wl,-znoexecstack")
		check_c_compiler_flag("" HAVE_NOEXECSTACK)
		if(HAVE_NOEXECSTACK)
			message ("-- Enabling data execution prevention")
			add_linker_flag("-znoexecstack")
		endif()

		# check and add Stack-based buffer overrun detection
		set(CMAKE_REQUIRED_FLAGS "-fstack-protector-strong")
		check_c_compiler_flag("" HAVE_STACKPROTECTOR)
		if(HAVE_STACKPROTECTOR)
			message ("-- Enabling stack-based buffer overrun detection")
			add_flag_append(CMAKE_C_FLAGS "-fstack-protector-strong")
			add_flag_append(CMAKE_CXX_FLAGS "-fstack-protector-strong")
		endif()

	endif ()
endif ()

# check for Data relocation and Protection (RELRO)
set(CMAKE_REQUIRED_FLAGS "-Wl,-zrelro,-znow")
check_c_compiler_flag("" HAVE_RELROFULL)
if(HAVE_RELROFULL)
	message ("-- Enabling full data relocation and protection")
	add_linker_flag("-zrelro")
	add_linker_flag("-znow")
else()
	#if full relro is not available, try partial relro
	set(CMAKE_REQUIRED_FLAGS "-Wl,-zrelro")
	check_c_compiler_flag("" HAVE_RELROPARTIAL)
	if(HAVE_RELROPARTIAL)
		message ("-- Enabling partial data relocation and protection")
		add_linker_flag("-zrelro")
	endif()
endif()

# position independent executetable (PIE)
# position independent code (PIC)
add_definitions (-fPIC)

set(CMAKE_EXPORT_COMPILE_COMMANDS "ON")
set(GRAPHENE_EGENESIS_JSON "${CMAKE_CURRENT_SOURCE_DIR}/libraries/egenesis/genesis.json" )

#set (ENABLE_INSTALLER 1)
#set (USE_PCH 1)

if (USE_PCH)
  include (cotire)
endif(USE_PCH)

option(USE_PROFILER "Build with GPROF support(Linux)." OFF)

IF( NOT WIN32 )
  list( APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/libraries/fc/CMakeModules" )
ENDIF( NOT WIN32 )
list( APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/libraries/fc/GitVersionGen" )
include( GetGitRevisionDescription )
get_git_head_revision( GIT_REFSPEC GIT_SHA2 )

SET(BOOST_COMPONENTS)
LIST(APPEND BOOST_COMPONENTS thread
                             iostreams
                             date_time
                             system
                             filesystem
                             program_options
                             chrono
                             unit_test_framework
                             context)
# boost::endian is also required, but FindBoost can't handle header-only libs
SET( Boost_USE_STATIC_LIBS ON CACHE STRING "ON or OFF" )

IF( WIN32 )
  SET(BOOST_ROOT $ENV{BOOST_ROOT})
  set(Boost_USE_MULTITHREADED ON)
  set(BOOST_ALL_DYN_LINK OFF) # force dynamic linking for all libraries
ENDIF(WIN32)

FIND_PACKAGE(Boost 1.57 REQUIRED COMPONENTS ${BOOST_COMPONENTS})
# For Boost 1.53 on windows, coroutine was not in BOOST_LIBRARYDIR and do not need it to build,  but if boost versin >= 1.54, find coroutine otherwise will cause link errors
IF(NOT "${Boost_VERSION}" MATCHES "1.53(.*)")
   SET(BOOST_LIBRARIES_TEMP ${Boost_LIBRARIES})
   FIND_PACKAGE(Boost 1.54 REQUIRED COMPONENTS coroutine)
   LIST(APPEND BOOST_COMPONENTS coroutine)
   SET(Boost_LIBRARIES ${BOOST_LIBRARIES_TEMP} ${Boost_LIBRARIES})
ENDIF()

if( WIN32 )

    message( STATUS "Configuring BitShares on WIN32")
    set( DB_VERSION 60 )
    set( BDB_STATIC_LIBS 1 )

    set( ZLIB_LIBRARIES "" )
    SET( DEFAULT_EXECUTABLE_INSTALL_DIR bin/ )

    set(CRYPTO_LIB)

    if( MSVC )
	add_definitions(-DWIN32_LEAN_AND_MEAN)
        #looks like this flag can have different default on some machines.
        SET(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} /SAFESEH:NO")
        SET(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /SAFESEH:NO")

        # Probably cmake has a bug and vcxproj generated for executable in Debug conf. has disabled debug info
        set(CMAKE_EXE_LINKER_FLAGS_DEBUG "${CMAKE_EXE_LINKER_FLAGS_DEBUG} /DEBUG")
    endif ( MSVC )

    # On windows tcl should be installed to the directory pointed by setenv.bat script
    SET(TCL_INCLUDE_PATH $ENV{TCL_ROOT}/include)
    MESSAGE(STATUS "tcl INCLUDE PATH: ${TCL_INCLUDE_PATH}")

    FIND_PACKAGE(TCL)
    MESSAGE(STATUS "tcl_library: ${TCL_LIBRARY}")

    SET(TCL_LIBS "optimized;${TCL_LIBRARY};debug;")
    get_filename_component(TCL_LIB_PATH "${TCL_LIBRARY}" PATH)
    get_filename_component(TCL_LIB_NAME "${TCL_LIBRARY}" NAME_WE)
    get_filename_component(TCL_LIB_EXT "${TCL_LIBRARY}" EXT)

    SET(TCL_LIBS "${TCL_LIBS}${TCL_LIB_PATH}/${TCL_LIB_NAME}g${TCL_LIB_EXT}")
    SET(TCL_LIBRARY ${TCL_LIBS})

else( WIN32 ) # Apple AND Linux

    if( APPLE )
        # Apple Specific Options Here
        message( STATUS "Configuring BitShares on OS X" )
        set( CMAKE_CXX_FLAGS "${CMAKE_C_FLAGS} -stdlib=libc++ -Wall" )
    else( APPLE )
        # Linux Specific Options Here
        message( STATUS "Configuring BitShares on Linux" )
        set( CMAKE_CXX_FLAGS "${CMAKE_C_FLAGS} -Wall" )
        if(USE_PROFILER)
            set( CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pg" )
        endif( USE_PROFILER )
        set( rt_library rt )
        set( pthread_library pthread)
        if ( NOT DEFINED crypto_library )
          # I'm not sure why this is here, I guess someone has openssl and can't detect it with find_package()?
          # if you have a normal install, you can define crypto_library to the empty string to avoid a build error
          set( crypto_library crypto)
        endif ()
        if ( FULL_STATIC_BUILD )
          set( CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static-libstdc++ -static-libgcc")
        endif ( FULL_STATIC_BUILD )
    endif( APPLE )

    if( "${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU" )
        set( CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-builtin-memcmp" )
    elseif( "${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang" )
        if( CMAKE_CXX_COMPILER_VERSION VERSION_EQUAL 4.0.0 OR CMAKE_CXX_COMPILER_VERSION VERSION_GREATER 4.0.0 )
            set( CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-invalid-partial-specialization" )
        endif()
    endif()

    if( "${CMAKE_GENERATOR}" STREQUAL "Ninja" )
        if( "${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang" )
            set( CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fcolor-diagnostics" )
        endif()
    endif()

    # based on http://www.delorie.com/gnu/docs/gdb/gdb_70.html
    # uncomment this line to tell GDB about macros (slows compile times)
    # set( CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -gdwarf-2 -g3" )

endif( WIN32 )

set(ENABLE_COVERAGE_TESTING FALSE CACHE BOOL "Build BitShares for code coverage analysis")

if(ENABLE_COVERAGE_TESTING)
    SET(CMAKE_CXX_FLAGS "--coverage ${CMAKE_CXX_FLAGS}")
endif()

add_subdirectory( libraries )
add_subdirectory( programs )
add_subdirectory( tests )


if (ENABLE_INSTALLER)

set(VERSION_MAJOR 0)
set(VERSION_MINOR 1)
set(VERSION_PATCH 0)


include(InstallRequiredSystemLibraries)

set(CPACK_OUTPUT_FILE_PREFIX ${CMAKE_BINARY_DIR}/packages)
set(CMAKE_INSTALL_PREFIX ${CMAKE_BINARY_DIR}/install)

SET(CPACK_PACKAGE_DIRECTORY "${CMAKE_INSTALL_PREFIX}")
set(CPACK_PACKAGE_NAME "graphene")
set(CPACK_PACKAGE_VENDOR "Cryptonomex, Inc.")
set(CPACK_PACKAGE_VERSION_MAJOR "${VERSION_MAJOR}")
set(CPACK_PACKAGE_VERSION_MINOR "${VERSION_MINOR}")
set(CPACK_PACKAGE_VERSION_PATCH "${VERSION_PATCH}")
set(CPACK_PACKAGE_VERSION "${CPACK_PACKAGE_VERSION_MAJOR}.${CPACK_PACKAGE_VERSION_MINOR}.${CPACK_PACKAGE_VERSION_PATCH}")
set(CPACK_PACKAGE_DESCRIPTION "A client for the BitShares network")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "A client for the BitShares network")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE.md")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "BitShares ${CPACK_PACKAGE_VERSION}")

if(WIN32)
 SET(CPACK_GENERATOR "ZIP;NSIS")
 set(CPACK_PACKAGE_NAME "BitShares") # override above
 set(CPACK_NSIS_EXECUTABLES_DIRECTORY .)
 set(CPACK_NSIS_PACKAGE_NAME "BitShares v${CPACK_PACKAGE_VERSION}")
 set(CPACK_NSIS_DISPLAY_NAME "${CPACK_NSIS_PACKAGE_NAME}")
 set(CPACK_NSIS_DEFINES "  !define MUI_STARTMENUPAGE_DEFAULTFOLDER \\\"BitShares\\\"")
 # it seems like windows zip files usually don't have a single directory inside them, unix tgz frequently do
 SET(CPACK_INCLUDE_TOPLEVEL_DIRECTORY 0)

endif(WIN32)

if(APPLE)
  set(CPACK_GENERATOR "DragNDrop")
endif()

if(LINUX)
  # Linux gets a .tgz
  SET(CPACK_GENERATOR "TGZ")
  SET(CPACK_INCLUDE_TOPLEVEL_DIRECTORY 1)
endif(LINUX)

 include(CPack)
endif(ENABLE_INSTALLER)

MESSAGE( STATUS "" )
MESSAGE( STATUS "PROFILER: ${USE_PROFILER}" )
MESSAGE( STATUS "" )
