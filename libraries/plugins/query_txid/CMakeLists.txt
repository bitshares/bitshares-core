file(GLOB HEADERS "include/graphene/query_txid/*.hpp")

add_library( graphene_query_txid
             query_txid_plugin.cpp
           )
find_path(LevelDB_INCLUDE_PATH NAMES leveldb/db.h leveldb/write_batch.h)
find_library(LevelDB_LIBRARY NAMES libleveldb.a)
find_library(Snappy_LIBRARY NAMES libsnappy.a)

if(LevelDB_INCLUDE_PATH AND LevelDB_LIBRARY AND Snappy_LIBRARY)
   target_link_libraries( graphene_query_txid graphene_chain graphene_app ${LevelDB_LIBRARY} ${Snappy_LIBRARY} )
   target_include_directories( graphene_query_txid
                              PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include" ${LevelDB_INCLUDE_PATH})
   install( TARGETS
      graphene_query_txid

      RUNTIME DESTINATION bin
      LIBRARY DESTINATION lib
      ARCHIVE DESTINATION lib
   )
else(LevelDB_INCLUDE_PATH AND LevelDB_LIBRARY AND Snappy_LIBRARY)
   message(FATAL_ERROR "You need leveldb and snappy")
endif()
