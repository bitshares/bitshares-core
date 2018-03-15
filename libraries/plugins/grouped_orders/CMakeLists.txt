file(GLOB HEADERS "include/graphene/grouped_orders/*.hpp")

add_library( graphene_grouped_orders
             grouped_orders_plugin.cpp
           )

target_link_libraries( graphene_grouped_orders graphene_chain graphene_app )
target_include_directories( graphene_grouped_orders
                            PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include" )

if(MSVC)
  set_source_files_properties( grouped_orders_plugin.cpp PROPERTIES COMPILE_FLAGS "/bigobj" )
endif(MSVC)

install( TARGETS
   graphene_grouped_orders

   RUNTIME DESTINATION bin
   LIBRARY DESTINATION lib
   ARCHIVE DESTINATION lib
)
INSTALL( FILES ${HEADERS} DESTINATION "include/graphene/grouped_orders" )

