file(GLOB HEADERS "include/graphene/hello/*.hpp")

add_library( graphene_hello
             ${HEADERS}
             hello_plugin.cpp
             hello_api.cpp
           )

target_link_libraries( graphene_hello graphene_chain graphene_app graphene_db )
target_include_directories( graphene_hello
                            PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include" )

install( TARGETS
   graphene_hello

   RUNTIME DESTINATION bin
   LIBRARY DESTINATION lib
   ARCHIVE DESTINATION lib
)
INSTALL( FILES ${HEADERS} DESTINATION "include/graphene/graphene_hello" )

