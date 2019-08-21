set( CMAKE_CURRENT_SOURCE_DIR ${INIT_SOURCE_DIR} )
set( CMAKE_CURRENT_BINARY_DIR ${INIT_BINARY_DIR} )

file( SHA256 "${embed_genesis_args}" chain_id )
message( STATUS "Generating egenesis" )

message( STATUS "Chain-id: ${chain_id}" )

set( generated_file_banner "/*** GENERATED FILE - DO NOT EDIT! ***/" )
set( genesis_json_hash "${chain_id}" )
configure_file( "${CMAKE_CURRENT_SOURCE_DIR}/egenesis_brief.cpp.tmpl"
                "${CMAKE_CURRENT_BINARY_DIR}/egenesis_brief.cpp" )

file( READ "${embed_genesis_args}" genesis_json )
string( LENGTH "${genesis_json}" genesis_json_length )
string( REGEX REPLACE "(\"|\\\\)" "\\\\\\1" genesis_json_escaped "${genesis_json}" )
string( REPLACE "\n" "\\n" genesis_json_escaped "${genesis_json_escaped}" )
string( REPLACE "\t" "\\t" genesis_json_escaped "${genesis_json_escaped}" )
set( genesis_json_array "\"${genesis_json_escaped}\"" )
set( genesis_json_array_height 1 )
set( genesis_json_array_width ${genesis_json_length} )

configure_file( "${CMAKE_CURRENT_SOURCE_DIR}/egenesis_full.cpp.tmpl"
                "${CMAKE_CURRENT_BINARY_DIR}/egenesis_full.cpp" )
