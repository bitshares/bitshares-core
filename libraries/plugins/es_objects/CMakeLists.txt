file(GLOB HEADERS "include/graphene/es_objects/*.hpp")

add_library( graphene_es_objects
        es_objects.cpp
           )

target_link_libraries( graphene_es_objects graphene_chain graphene_app curl )
target_include_directories( graphene_es_objects
                            PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include" )

if(MSVC)
  set_source_files_properties(es_objects.cpp PROPERTIES COMPILE_FLAGS "/bigobj" )
endif(MSVC)

install( TARGETS
   graphene_es_objects

   RUNTIME DESTINATION bin
   LIBRARY DESTINATION lib
   ARCHIVE DESTINATION lib
)
INSTALL( FILES ${HEADERS} DESTINATION "include/graphene/es_objects" )

