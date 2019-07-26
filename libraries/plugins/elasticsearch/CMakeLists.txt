file(GLOB HEADERS "include/graphene/elasticsearch/*.hpp")

add_library( graphene_elasticsearch
        elasticsearch_plugin.cpp
           )
if (NOT WIN32 AND NOT APPLE AND CURL_STATICLIB)
   set (OLD_SUFFIXES ${CMAKE_FIND_LIBRARY_SUFFIXES})
   set (CMAKE_FIND_LIBRARY_SUFFIXES .a)
   find_package(CURL REQUIRED)
   list(APPEND CURL_LIBRARIES ssl crypto)
   set (CMAKE_FIND_LIBRARY_SUFFIXES ${OLD_SUFFIXES})
else (NOT WIN32 AND NOT APPLE AND CURL_STATICLIB)
   find_package(CURL REQUIRED)
endif (NOT WIN32 AND NOT APPLE AND CURL_STATICLIB)

include_directories(${CURL_INCLUDE_DIRS})
if(MSVC)
  set_source_files_properties(elasticsearch_plugin.cpp PROPERTIES COMPILE_FLAGS "/bigobj" )
endif(MSVC)
if(CURL_STATICLIB)
  SET_TARGET_PROPERTIES(graphene_elasticsearch PROPERTIES
  COMPILE_DEFINITIONS "CURL_STATICLIB")
endif(CURL_STATICLIB)
target_link_libraries( graphene_elasticsearch graphene_chain graphene_app ${CURL_LIBRARIES} )
target_include_directories( graphene_elasticsearch
                            PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include" 
                            PUBLIC "${CURL_INCLUDE_DIR}" )


install( TARGETS
   graphene_elasticsearch

   RUNTIME DESTINATION bin
   LIBRARY DESTINATION lib
   ARCHIVE DESTINATION lib
)
INSTALL( FILES ${HEADERS} DESTINATION "include/graphene/elasticsearch" )

