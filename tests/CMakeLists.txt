file(GLOB COMMON_SOURCES "common/*.cpp")

file(GLOB UNIT_TESTS "tests/*.cpp")
add_executable( chain_test ${UNIT_TESTS} ${COMMON_SOURCES} )
target_link_libraries( chain_test graphene_chain graphene_app graphene_account_history fc z)

file(GLOB PERFORMANCE_TESTS "performance/*.cpp")
add_executable( performance_test ${PERFORMANCE_TESTS} ${COMMON_SOURCES} )
target_link_libraries( performance_test graphene_chain graphene_app graphene_account_history fc z)

file(GLOB BENCH_MARKS "benchmarks/*.cpp")
add_executable( chain_bench ${BENCH_MARKS} ${COMMON_SOURCES} )
target_link_libraries( chain_bench graphene_chain graphene_account_history graphene_time fc )

file(GLOB APP_SOURCES "app/*.cpp")
add_executable( app_test ${APP_SOURCES} )
target_link_libraries( app_test graphene_account_history graphene_app graphene_net graphene_chain graphene_time fc )

file(GLOB INTENSE_SOURCES "intense/*.cpp")
add_executable( intense_test ${INTENSE_SOURCES} ${COMMON_SOURCES} )
target_link_libraries( intense_test graphene_chain graphene_app graphene_account_history fc )
