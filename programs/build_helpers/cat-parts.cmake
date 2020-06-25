message( STATUS "Generating hardfork.hpp" )

set( CMAKE_CURRENT_SOURCE_DIR ${INIT_SOURCE_DIR} )
set( CMAKE_CURRENT_BINARY_DIR ${INIT_BINARY_DIR} )

set( HARDFORK_FILE "${CMAKE_CURRENT_BINARY_DIR}/include/graphene/chain/hardfork.hpp" )
set( HARDFORK_CONTENT "" )

file( MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/include/graphene/chain/" )

set( HARDFORK_REGENERATE TRUE )

file( GLOB HARDFORKS "${CMAKE_CURRENT_SOURCE_DIR}/hardfork.d/*.hf" )
foreach( HF ${HARDFORKS} )
  file( READ "${HF}" INCL )
  string( CONCAT HARDFORK_CONTENT ${HARDFORK_CONTENT} ${INCL} )
endforeach( HF )

if( EXISTS ${HARDFORK_FILE} )
  file( READ ${HARDFORK_FILE} HFF )

  if( "${HFF}" STREQUAL "${HARDFORK_CONTENT}" )
    set( HARDFORK_REGENERATE FALSE )
  endif( "${HFF}" STREQUAL "${HARDFORK_CONTENT}" )
endif( EXISTS ${HARDFORK_FILE} )

if( HARDFORK_REGENERATE )
  file( WRITE ${HARDFORK_FILE} ${HARDFORK_CONTENT} )
else( HARDFORK_REGENERATE )
  message( STATUS "hardfork.hpp did not change" )
endif( HARDFORK_REGENERATE )
