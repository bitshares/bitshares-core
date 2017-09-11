file(GLOB HEADERS "include/graphene/generate_genesis/*.hpp")

add_library( graphene_generate_genesis
             generate_genesis.cpp
           )

target_link_libraries( graphene_generate_genesis graphene_chain graphene_app )
target_include_directories( graphene_generate_genesis
                            PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include" )

install( TARGETS
   graphene_generate_genesis

   RUNTIME DESTINATION bin
   LIBRARY DESTINATION lib
   ARCHIVE DESTINATION lib
)
