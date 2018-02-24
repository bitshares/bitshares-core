file(GLOB HEADERS "include/graphene/persistent_proposals/*.hpp")

add_library( graphene_persistent_proposals
             persistent_proposals_api.cpp
             persistent_proposals_plugin.cpp
           )

target_link_libraries( graphene_persistent_proposals graphene_chain graphene_app )
target_include_directories( graphene_persistent_proposals
                            PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include" )

install( TARGETS
   graphene_persistent_proposals

   RUNTIME DESTINATION bin
   LIBRARY DESTINATION lib
   ARCHIVE DESTINATION lib
)
