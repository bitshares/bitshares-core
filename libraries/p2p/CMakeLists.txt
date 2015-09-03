file(GLOB HEADERS "include/graphene/p2p/*.hpp")

set(SOURCES node.cpp
            stcp_socket.cpp
            peer_connection.cpp
            message_oriented_connection.cpp)

add_library( graphene_p2p  ${SOURCES} ${HEADERS} )

target_link_libraries( graphene_p2p  
  PUBLIC fc graphene_db )
target_include_directories( graphene_p2p  
  PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include"
  PRIVATE "${CMAKE_SOURCE_DIR}/libraries/chain/include"
)

#if(MSVC)
#  set_source_files_properties( node.cpp PROPERTIES COMPILE_FLAGS "/bigobj" )
#endif(MSVC)

#if (USE_PCH)
#  set_target_properties(graphene_p2p  PROPERTIES COTIRE_ADD_UNITY_BUILD FALSE)
#  cotire(graphene_p2p )
#endif(USE_PCH)

install( TARGETS
   graphene_p2p 

   RUNTIME DESTINATION bin
   LIBRARY DESTINATION lib
   ARCHIVE DESTINATION lib
)
