file(GLOB HEADERS "include/graphene/api_helper_indexes/*.hpp")

add_library( graphene_api_helper_indexes
        api_helper_indexes.cpp
           )

target_link_libraries( graphene_api_helper_indexes graphene_chain graphene_app )
target_include_directories( graphene_api_helper_indexes
                            PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include" )

if(MSVC)
  set_source_files_properties(api_helper_indexes.cpp PROPERTIES COMPILE_FLAGS "/bigobj" )
endif(MSVC)

install( TARGETS
   graphene_api_helper_indexes

   RUNTIME DESTINATION bin
   LIBRARY DESTINATION lib
   ARCHIVE DESTINATION lib
)
INSTALL( FILES ${HEADERS} DESTINATION "include/graphene/api_helper_indexes" )
