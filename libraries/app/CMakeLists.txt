file(GLOB HEADERS "include/graphene/app/*.hpp")
file(GLOB EGENESIS_HEADERS "../egenesis/include/graphene/app/*.hpp")

add_library( graphene_app 
             api.cpp
             application.cpp
             util.cpp
             database_api.cpp
             plugin.cpp
             plugin_type.cpp
             config_util.cpp
             ${HEADERS}
             ${EGENESIS_HEADERS}
           )

# need to link graphene_debug_witness because plugins aren't sufficiently isolated #246
target_link_libraries( graphene_app fc graphene_db graphene_chain graphene_net graphene_utilities

#get_sub_class_name you need link graphene_witness
                       graphene_debug_witness
                       graphene_account_history
                       graphene_elasticsearch        
                       graphene_market_history
                       graphene_delayed_node
                       graphene_snapshot
                       graphene_es_objects
                       graphene_grouped_orders
                       graphene_template_plugin
#link end 
                       )
target_include_directories( graphene_app
                            PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include"
                            "${CMAKE_CURRENT_SOURCE_DIR}/../egenesis/include" 
#get_sub_class_name you need include 
                            "${CMAKE_CURRENT_SOURCE_DIR}/../plugins/witness/include"
                            "${CMAKE_CURRENT_SOURCE_DIR}/../plugins/debug_witness/include"
                            "${CMAKE_CURRENT_SOURCE_DIR}/../plugins/account_history/include"
                            "${CMAKE_CURRENT_SOURCE_DIR}/../plugins/elasticsearch/include"
                            "${CMAKE_CURRENT_SOURCE_DIR}/../plugins/market_history/include"
                            "${CMAKE_CURRENT_SOURCE_DIR}/../plugins/delayed_node/include"
                            "${CMAKE_CURRENT_SOURCE_DIR}/../plugins/snapshot/include"
                            "${CMAKE_CURRENT_SOURCE_DIR}/../plugins/es_objects/include"
                            "${CMAKE_CURRENT_SOURCE_DIR}/../plugins/grouped_orders/include"
                            "${CMAKE_CURRENT_SOURCE_DIR}/../plugins/template_plugin/include"
#include end
                            )

if(MSVC)
  set_source_files_properties( application.cpp api.cpp database_api.cpp PROPERTIES COMPILE_FLAGS "/bigobj" )
endif(MSVC)

INSTALL( TARGETS
   graphene_app

   RUNTIME DESTINATION bin
   LIBRARY DESTINATION lib
   ARCHIVE DESTINATION lib
)
INSTALL( FILES ${HEADERS} DESTINATION "include/graphene/app" )
