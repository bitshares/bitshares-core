
add_library( cybex_query_plugin 
             query_plugin.cpp
             parse_url.cpp
           )

target_link_libraries( cybex_query_plugin graphene_chain graphene_app )
target_include_directories( cybex_query_plugin 
                            PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include" )

