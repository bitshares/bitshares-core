file(GLOB HEADERS "include/graphene/custom_operations/*.hpp")

add_library( graphene_custom_operations
        custom_operations_plugin.cpp
        custom_operations.cpp
        custom_evaluators.cpp
           )

target_link_libraries( graphene_custom_operations graphene_chain graphene_app )
target_include_directories( graphene_custom_operations
                            PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include" )

if(MSVC)
  set_source_files_properties(custom_operations_plugin.cpp custom_operations.cpp custom_evaluators.cpp
          PROPERTIES COMPILE_FLAGS "/bigobj" )
endif(MSVC)

install( TARGETS
   graphene_custom_operations

   RUNTIME DESTINATION bin
   LIBRARY DESTINATION lib
   ARCHIVE DESTINATION lib
)
INSTALL( FILES ${HEADERS} DESTINATION "include/graphene/custom_operations" )
