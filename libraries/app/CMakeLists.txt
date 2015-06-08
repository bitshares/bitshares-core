file(GLOB HEADERS "include/graphene/app/*.hpp")

add_library( graphene_app 
             api.cpp
             application.cpp
             plugin.cpp
           )

target_link_libraries( graphene_app graphene_chain fc graphene_db graphene_net graphene_time graphene_utilities )
target_include_directories( graphene_app
                            PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include" )

if(MSVC)
  set_source_files_properties( application.cpp PROPERTIES COMPILE_FLAGS "/bigobj" )
endif(MSVC)
