if( GRAPHENE_EGENESIS_JSON )
  set( embed_genesis_args "${GRAPHENE_EGENESIS_JSON}" )
else( GRAPHENE_EGENESIS_JSON )
  set( embed_genesis_args "genesis.json" )
endif( GRAPHENE_EGENESIS_JSON )

add_custom_target( build_egenesis_cpp
   BYPRODUCTS
      "${CMAKE_CURRENT_BINARY_DIR}/egenesis_brief.cpp"
      "${CMAKE_CURRENT_BINARY_DIR}/egenesis_full.cpp"
   WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
   COMMAND ${CMAKE_COMMAND}
        -DINIT_BINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}
        -DINIT_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
        -Dembed_genesis_args=${embed_genesis_args}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/embed_genesis.cmake
   COMMENT "Generating egenesis"
   DEPENDS
      "${GRAPHENE_EGENESIS_JSON}"
      "${CMAKE_CURRENT_SOURCE_DIR}/egenesis_brief.cpp.tmpl"
      "${CMAKE_CURRENT_SOURCE_DIR}/egenesis_full.cpp.tmpl"
)

add_library( graphene_egenesis_none egenesis_none.cpp
             include/graphene/egenesis/egenesis.hpp )
add_library( graphene_egenesis_brief "${CMAKE_CURRENT_BINARY_DIR}/egenesis_brief.cpp"
             include/graphene/egenesis/egenesis.hpp )
add_dependencies( graphene_egenesis_brief build_egenesis_cpp )
add_library( graphene_egenesis_full  "${CMAKE_CURRENT_BINARY_DIR}/egenesis_full.cpp"
             include/graphene/egenesis/egenesis.hpp )
add_dependencies( graphene_egenesis_full build_egenesis_cpp )

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
