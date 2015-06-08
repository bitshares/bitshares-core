# Find the BerkeleyDB includes and library
# Customizable variables:
#   BDB_ROOT_DIR
#     This variable points to the BerkeleyDB root directory. On Windows the
#     library location typically will have to be provided explicitly using the
#     -D command-line option. Alternatively, the DBROOTDIR environment variable
#     can be set.
#
#   BDB_STATIC_LIBS
#     Should be set to 1 if static version of libraries should be found. Defaults to 0 (shared libs).
#
# This module defines
# BDB_INCLUDE_DIR, where to find db.h, etc.
# BDB_LIBRARIES, the libraries needed to use BerkeleyDB.

IF (NOT DEFINED BDB_ROOT_DIR)
  SET (BDB_ROOT_DIR $ENV{DBROOTDIR})
ENDIF()

MESSAGE (STATUS "Using ${BDB_ROOT_DIR} as BerkeleyDB root")

IF(NOT DEFINED BDB_STATIC_LIBS)
  SET (BDB_STATIC_LIBS 0)
ENDIF()

FIND_PATH(BDB_INCLUDE_DIR NAMES db.h db_cxx.h
  HINTS "${BDB_ROOT_DIR}/include"
  PATHS ${BDB_ROOT_DIR} 
  /usr/include/libdb5
  /usr/include/db5
  /usr/include/libdb4
  /usr/include/db4
  /usr/local/include/libdb5
  /usr/local/include/db5
  /usr/local/include/libdb4
  /usr/local/include/db4
  PATH_SUFFIXES include
)

IF (WIN32)
  IF(NOT DEFINED BDB_VERSION)
    SET (DB_VERSION "60")
  ENDIF ()

  SET (BDB_LIB_BASENAME "libdb")

  IF (${BDB_STATIC_LIBS} EQUAL 1)
    SET (BDB_LIBS_SUFFIX_RELEASE "s")
    SET (BDB_LIBS_SUFFIX_DEBUG "sD")
  ELSE()
    SET (BDB_LIBS_SUFFIX_RELEASE "")
    SET (BDB_LIBS_SUFFIX_DEBUG "D")
  ENDIF()

ELSE (WIN32)
  IF(NOT DEFINED BDB_VERSION)
    SET (DB_VERSION "-6.0")
  ENDIF ()

  # On unix library in all versions have the same names.
  SET (BDB_LIBS_SUFFIX_RELEASE "")
  SET (BDB_LIBS_SUFFIX_DEBUG "")

  SET (BDB_LIB_BASENAME "db_cxx")
ENDIF (WIN32)

message (STATUS "Looking for: ${BDB_LIB_BASENAME}${DB_VERSION}${BDB_LIBS_SUFFIX_RELEASE}")
FIND_LIBRARY(BDB_LIBRARY_RELEASE "${BDB_LIB_BASENAME}${DB_VERSION}${BDB_LIBS_SUFFIX_RELEASE}" "${BDB_LIB_BASENAME}"
  HINTS "${BDB_ROOT_DIR}/lib" PATHS ${BDB_ROOT_DIR} ${BDB_INCLUDE_DIR} "/usr/local/lib" PATH_SUFFIXES lib
)

FIND_LIBRARY(BDB_LIBRARY_DEBUG "${BDB_LIB_BASENAME}${DB_VERSION}${BDB_LIBS_SUFFIX_DEBUG}" "${BDB_LIB_BASENAME}"
  HINTS "${BDB_ROOT_DIR}/lib" PATHS ${BDB_ROOT_DIR} ${BDB_INCLUDE_DIR} "/usr/local/lib" PATH_SUFFIXES lib
)

IF (BDB_LIBRARY_RELEASE AND BDB_LIBRARY_DEBUG )
  SET (_BDB_LIBRARY
    debug ${BDB_LIBRARY_DEBUG}
    optimized ${BDB_LIBRARY_RELEASE}
  )
ELSEIF(BDB_LIBRARY_RELEASE)
  SET (_BDB_LIBRARY ${BDB_LIBRARY_RELEASE})
ELSEIF(BDB_LIBRARY_DEBUG)
  SET (_BDB_LIBRARY ${BDB_LIBRARY_DEBUG})
ENDIF()

MESSAGE (STATUS ${_BDB_LIBRARY})

IF(_BDB_LIBRARY)
  LIST (APPEND BDB_LIBRARIES ${_BDB_LIBRARY})
ENDIF()

INCLUDE(FindPackageHandleStandardArgs)
FIND_PACKAGE_HANDLE_STANDARD_ARGS(BerkeleyDB 
  FOUND_VAR BerkeleyDB_FOUND 
  REQUIRED_VARS BDB_INCLUDE_DIR BDB_LIBRARIES
  FAIL_MESSAGE "Could not find Berkeley DB >= 4.1" )

