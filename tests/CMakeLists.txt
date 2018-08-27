file(GLOB COMMON_SOURCES "common/*.cpp")

find_package( Gperftools QUIET )
if( GPERFTOOLS_FOUND )
    message( STATUS "Found gperftools; compiling tests with TCMalloc")
    list( APPEND PLATFORM_SPECIFIC_LIBS tcmalloc )
endif()

file(GLOB UNIT_TESTS "tests/*.cpp")
add_executable( chain_test ${UNIT_TESTS} ${COMMON_SOURCES} )
target_link_libraries( chain_test graphene_chain graphene_app graphene_account_history graphene_elasticsearch graphene_es_objects graphene_egenesis_none fc graphene_wallet ${PLATFORM_SPECIFIC_LIBS} )
if(MSVC)
  set_source_files_properties( tests/serialization_tests.cpp PROPERTIES COMPILE_FLAGS "/bigobj" )
endif(MSVC)

file(GLOB PERFORMANCE_TESTS "performance/*.cpp")
add_executable( performance_test ${PERFORMANCE_TESTS} ${COMMON_SOURCES} )
target_link_libraries( performance_test graphene_chain graphene_app graphene_account_history graphene_elasticsearch graphene_es_objects graphene_egenesis_none fc ${PLATFORM_SPECIFIC_LIBS} )

file(GLOB BENCH_MARKS "benchmarks/*.cpp")
add_executable( chain_bench ${BENCH_MARKS} ${COMMON_SOURCES} )
target_link_libraries( chain_bench graphene_chain graphene_app graphene_account_history graphene_elasticsearch graphene_es_objects graphene_egenesis_none fc ${PLATFORM_SPECIFIC_LIBS} )

file(GLOB APP_SOURCES "app/main.cpp")
add_executable( app_test ${APP_SOURCES} )
target_link_libraries( app_test graphene_app graphene_account_history graphene_net graphene_witness graphene_chain graphene_egenesis_none fc ${PLATFORM_SPECIFIC_LIBS} )

file(GLOB SYS_SOURCES "app/system_test.cpp")
add_executable( system_test ${SYS_SOURCES} )
target_link_libraries( system_test graphene_app graphene_wallet graphene_account_history graphene_net graphene_witness graphene_chain graphene_egenesis_none fc ${PLATFORM_SPECIFIC_LIBS} )

file(GLOB CLI_SOURCES "cli/*.cpp")
add_executable( cli_test ${CLI_SOURCES} )
target_link_libraries( cli_test graphene_app graphene_wallet graphene_witness graphene_account_history graphene_net graphene_chain graphene_egenesis_none fc ${PLATFORM_SPECIFIC_LIBS} )

file(GLOB ES_SOURCES "elasticsearch/*.cpp")
add_executable( es_test ${ES_SOURCES} ${COMMON_SOURCES} )
target_link_libraries( es_test graphene_chain graphene_app graphene_account_history graphene_elasticsearch graphene_es_objects graphene_egenesis_none fc ${PLATFORM_SPECIFIC_LIBS} )

add_subdirectory( generate_empty_blocks )
