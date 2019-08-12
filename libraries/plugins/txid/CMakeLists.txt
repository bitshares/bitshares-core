file(GLOB HEADERS "include/graphene/plugins/txid/*.hpp")

add_library( graphene_txid_plugin
             txid_plugin.cpp
           )

target_link_libraries( graphene_txid_plugin graphene_chain graphene_app )
target_include_directories( graphene_txid_plugin
                            PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include" )

if(MSVC)
  set_source_files_properties( txid_plugin.cpp PROPERTIES COMPILE_FLAGS "/bigobj" )
endif(MSVC)

install( TARGETS
   graphene_txid_plugin

   RUNTIME DESTINATION bin
   LIBRARY DESTINATION lib
   ARCHIVE DESTINATION lib
)
INSTALL( FILES ${HEADERS} DESTINATION "include/graphene/plugins/txid" )

