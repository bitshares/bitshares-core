add_executable( backup main.cpp )
if( UNIX AND NOT APPLE )
  set(rt_library rt )
endif()


target_link_libraries( backup
      PRIVATE graphene_app  graphene_chain    fc ${CMAKE_DL_LIBS} ${PLATFORM_SPECIFIC_LIBS} )

if(MSVC)
  set_source_files_properties( main.cpp PROPERTIES COMPILE_FLAGS "/bigobj" )
endif(MSVC)

install( TARGETS
   backup

   RUNTIME DESTINATION bin
   LIBRARY DESTINATION lib
   ARCHIVE DESTINATION lib
)
