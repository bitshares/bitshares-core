file(GLOB HEADERS "include/graphene/chain/*.hpp")

## SORT .cpp by most likely to change / break compile
add_library( graphene_chain
             types.cpp
             address.cpp
             asset.cpp

             operations.cpp

             evaluator.cpp
             global_parameters_evaluator.cpp
             account_evaluator.cpp
             witness_evaluator.cpp
             delegate_evaluator.cpp
             asset_evaluator.cpp
             transfer_evaluator.cpp
             proposal_evaluator.cpp
             call_order_evaluator.cpp
             limit_order_evaluator.cpp
             vesting_balance_evaluator.cpp
             withdraw_permission_evaluator.cpp
             worker_evaluator.cpp

             key_object.cpp
             account_object.cpp
             asset_object.cpp
             proposal_object.cpp
             vesting_balance_object.cpp
             worker_object.cpp

             transaction.cpp
             block.cpp

             transaction_evaluation_state.cpp
             fork_database.cpp

             db_balance.cpp
             db_block.cpp
             db_debug.cpp
             db_getter.cpp
             db_init.cpp
             db_maint.cpp
             db_management.cpp
             db_market.cpp
             db_update.cpp
             db_witness_schedule.cpp

             ${HEADERS}
           )

target_link_libraries( graphene_chain fc graphene_db leveldb )
target_include_directories( graphene_chain
                            PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include" )

if(MSVC)
  set_source_files_properties( db_init.cpp db_block.cpp PROPERTIES COMPILE_FLAGS "/bigobj" )
endif(MSVC)
