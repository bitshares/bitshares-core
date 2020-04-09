get_target_property(GRAPHENE_CHAIN_SOURCE graphene_chain SOURCE_DIR)
get_target_property(GRAPHENE_CHAIN_BIN graphene_chain BINARY_DIR)

# This is here only to support CMake < 3.4
if (NOT GRAPHENE_CHAIN_SOURCE)
  set(GRAPHENE_CHAIN_SOURCE ${GRAPHENE_CHAIN_SOURCE_LEGACY})
endif()
# This is here only to support CMake < 3.4
if (NOT GRAPHENE_CHAIN_BIN)
  set(GRAPHENE_CHAIN_BIN ${GRAPHENE_CHAIN_BIN_LEGACY})
endif()

add_custom_target( build_hardfork_hpp
  COMMAND
    ${CMAKE_COMMAND}
    -DINIT_BINARY_DIR=${GRAPHENE_CHAIN_BIN}
    -DINIT_SOURCE_DIR=${GRAPHENE_CHAIN_SOURCE}
    -P ${CMAKE_CURRENT_SOURCE_DIR}/cat-parts.cmake
)

add_executable( member_enumerator member_enumerator.cpp )
if( UNIX AND NOT APPLE )
  set( rt_library rt )
endif()

# we only actually need Boost, but link against FC for now so we don't duplicate it.
target_link_libraries( member_enumerator PRIVATE fc graphene_app graphene_net graphene_chain graphene_egenesis_brief
                                                 graphene_utilities graphene_wallet ${CMAKE_DL_LIBS}
                                                 ${PLATFORM_SPECIFIC_LIBS} )
