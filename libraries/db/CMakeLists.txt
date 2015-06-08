file(GLOB HEADERS "include/graphene/db/*.hpp")
add_library( graphene_db undo_database.cpp index.cpp object_database.cpp upgrade_leveldb.cpp ${HEADERS} )
target_link_libraries( graphene_db fc leveldb )
target_include_directories( graphene_db PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include" )
