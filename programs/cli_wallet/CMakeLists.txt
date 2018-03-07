add_executable( cli_wallet main.cpp )
if( UNIX AND NOT APPLE )
  set(rt_library rt )
endif()

find_package( Gperftools QUIET )
if( GPERFTOOLS_FOUND )
    message( STATUS "Found gperftools; compiling cli_wallet with TCMalloc")
    list( APPEND PLATFORM_SPECIFIC_LIBS tcmalloc )
endif()

if(MSVC)
  target_link_libraries( cli_wallet
         PRIVATE graphene_app graphene_net graphene_chain graphene_egenesis_brief graphene_utilities graphene_wallet fc ${CMAKE_DL_LIBS} ${PLATFORM_SPECIFIC_LIBS} )
  set_source_files_properties( main.cpp PROPERTIES COMPILE_FLAGS "/bigobj" )
else (MSVC)
   add_library(editline STATIC IMPORTED)
   set_target_properties(editline PROPERTIES
         IMPORTED_LOCATION "${CMAKE_SOURCE_DIR}/libraries/fc/vendor/editline/src/project_editline-build/src/.libs/libeditline.a"
         INTERFACE_INCLUDE_DIRECTORIES "$(CMAKE_SOURCE_DIR}/libraries/fc/vendor/editline/include")

   target_link_libraries( cli_wallet
         PRIVATE graphene_app editline graphene_net graphene_chain graphene_egenesis_brief graphene_utilities graphene_wallet fc ${CMAKE_DL_LIBS} ${PLATFORM_SPECIFIC_LIBS} )

endif(MSVC)

install( TARGETS
   cli_wallet

   RUNTIME DESTINATION bin
   LIBRARY DESTINATION lib
   ARCHIVE DESTINATION lib
)
