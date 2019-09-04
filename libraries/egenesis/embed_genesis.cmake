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

# Split the escaped JSON string into chunks (each less than max string literal length supported by MSVC).
# Chunks must not end with a backslash to avoid splitting escapes.
string( RANDOM LENGTH 10000 _dots )
string( REGEX REPLACE "." "." _dots "${_dots}" )
string( REGEX MATCHALL "(${_dots}(.|..|...)?[^\\\\])" _chunks "${genesis_json_escaped}" )
string( LENGTH "${_chunks}" _chunks_len )
string( REGEX REPLACE ";" "" _seen "${_chunks}" )
string( LENGTH "${_seen}" _seen )
string( SUBSTRING "${genesis_json_escaped}" ${_seen} -1 _rest )
string( REGEX REPLACE ";" "\",\n\"" genesis_json_array "${_chunks}" )
set( genesis_json_array "\"${genesis_json_array}\",\n\"${_rest}\"" )
set( genesis_json_array_height "${_chunks_len} - ${_seen} + 2" )

configure_file( "${CMAKE_CURRENT_SOURCE_DIR}/egenesis_full.cpp.tmpl"
                "${CMAKE_CURRENT_BINARY_DIR}/egenesis_full.cpp" )
