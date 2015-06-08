file(GLOB HEADERS "include/graphene/wallet/*.hpp")
add_library( graphene_wallet cache.cpp wallet.cpp ${HEADERS} )
target_link_libraries( graphene_wallet PRIVATE graphene_app graphene_net graphene_chain graphene_utilities fc ${CMAKE_DL_LIBS} ${PLATFORM_SPECIFIC_LIBS} )
target_include_directories( graphene_db PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include" )

if(MSVC)
  set_source_files_properties( wallet.cpp PROPERTIES COMPILE_FLAGS "/bigobj" )
endif(MSVC)
