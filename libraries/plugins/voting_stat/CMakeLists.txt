file( GLOB HEADER "include/graphene/account_history/*.hpp" )

add_library( graphene_voting_stat voting_stat_plugin.cpp )

target_link_libraries( graphene_voting_stat graphene_chain graphene_app )
target_include_directories( graphene_voting_stat
                            PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include" )
                
if(MSVC)
    set_source_files_properties( voting_stat_plugin.cpp PROPERTIES COMPILE_FLAGS "/bigobj" )
endif(MSVC)

install( TARGETS
    graphene_voting_stat
    
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib )

INSTALL( FILES ${HEADERS} DESTINATION "include/graphene/voting_stat" )
