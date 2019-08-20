message( STATUS "Generating egenesis" )

if( GRAPHENE_EGENESIS_JSON )
  set( embed_genesis_args "${GRAPHENE_EGENESIS_JSON}" )
else( GRAPHENE_EGENESIS_JSON )
  set( embed_genesis_args "genesis.json" )
endif( GRAPHENE_EGENESIS_JSON )

file( MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/tmp/" )
configure_file( "${embed_genesis_args}"
                "${CMAKE_CURRENT_BINARY_DIR}/tmp/genesis" )

file( SHA256 "${embed_genesis_args}" chain_id )
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

add_library( graphene_egenesis_none egenesis_none.cpp
             include/graphene/egenesis/egenesis.hpp )
add_library( graphene_egenesis_brief "${CMAKE_CURRENT_BINARY_DIR}/egenesis_brief.cpp"
             include/graphene/egenesis/egenesis.hpp )
add_library( graphene_egenesis_full  "${CMAKE_CURRENT_BINARY_DIR}/egenesis_full.cpp"
             include/graphene/egenesis/egenesis.hpp )

target_link_libraries( graphene_egenesis_none graphene_chain fc )
target_link_libraries( graphene_egenesis_brief graphene_chain fc )
target_link_libraries( graphene_egenesis_full graphene_chain fc )

target_include_directories( graphene_egenesis_none
   PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include" )
target_include_directories( graphene_egenesis_brief
   PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include" )
target_include_directories( graphene_egenesis_full
   PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include" )

INSTALL( TARGETS
   graphene_egenesis_none graphene_egenesis_brief graphene_egenesis_full

   RUNTIME DESTINATION bin
   LIBRARY DESTINATION lib
   ARCHIVE DESTINATION lib
)
