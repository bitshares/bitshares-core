
set_source_files_properties( "${CMAKE_CURRENT_BINARY_DIR}/include/graphene/chain/hardfork.hpp" PROPERTIES GENERATED TRUE )

file(GLOB HEADERS "include/graphene/chain/*.hpp")

if( GRAPHENE_DISABLE_UNITY_BUILD )
   set( GRAPHENE_DB_FILES
        db_balance.cpp
        db_block.cpp
        db_debug.cpp
        db_genesis.cpp
        db_getter.cpp
        db_init.cpp
        db_maint.cpp
        db_management.cpp
        db_market.cpp
        db_notify.cpp
        db_update.cpp
        db_witness_schedule.cpp
      )
   message( STATUS "Graphene database unity build disabled" )
else( GRAPHENE_DISABLE_UNITY_BUILD )
   set( GRAPHENE_DB_FILES
        database.cpp )
   message( STATUS "Graphene database unity build enabled" )
endif( GRAPHENE_DISABLE_UNITY_BUILD )

## SORT .cpp by most likely to change / break compile
add_library( graphene_chain

             # As database takes the longest to compile, start it first
             ${GRAPHENE_DB_FILES}
             fork_database.cpp

             genesis_state.cpp
             get_config.cpp
             exceptions.cpp

             evaluator.cpp
             liquidity_pool_evaluator.cpp
             samet_fund_evaluator.cpp
             credit_offer_evaluator.cpp
             balance_evaluator.cpp
             account_evaluator.cpp
             assert_evaluator.cpp
             witness_evaluator.cpp
             committee_member_evaluator.cpp
             asset_evaluator.cpp
             transfer_evaluator.cpp
             proposal_evaluator.cpp
             market_evaluator.cpp
             ticket_evaluator.cpp
             vesting_balance_evaluator.cpp
             withdraw_permission_evaluator.cpp
             worker_evaluator.cpp
             htlc_evaluator.cpp
             confidential_evaluator.cpp
             special_authority_evaluation.cpp
             custom_authority_evaluator.cpp
             buyback.cpp

             account_object.cpp
             asset_object.cpp
             fba_object.cpp
             market_object.cpp
             proposal_object.cpp
             vesting_balance_object.cpp
             ticket_object.cpp
             small_objects.cpp

             block_database.cpp

             is_authorized_asset.cpp

             ${HEADERS}
             "${CMAKE_CURRENT_BINARY_DIR}/include/graphene/chain/hardfork.hpp"
           )

add_dependencies( graphene_chain build_hardfork_hpp )
target_link_libraries( graphene_chain fc graphene_db graphene_protocol )
target_include_directories( graphene_chain
                            PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include" "${CMAKE_CURRENT_BINARY_DIR}/include" )

set( GRAPHENE_CHAIN_BIG_FILES
     db_init.cpp
     db_genesis.cpp
     db_block.cpp
     db_maint.cpp
     db_market.cpp
     database.cpp
     block_database.cpp
   )

if(MSVC)
  set_source_files_properties( ${GRAPHENE_CHAIN_BIG_FILES} PROPERTIES COMPILE_FLAGS "/bigobj" )
else( MSVC )
   if( MINGW )
      # Note: Even with the big-obj property and the -Os property set,
      #       "string table overflow" and "File too big" errors may still occur on database.cpp.
      #       Can set GRAPHENE_DISABLE_UNITY_BUILD to ON to get around the issue.
      set_source_files_properties( ${GRAPHENE_CHAIN_BIG_FILES} PROPERTIES COMPILE_FLAGS -Wa,-mbig-obj )
   endif( MINGW )
endif(MSVC)

# Support for CMake < 3.4
if (${CMAKE_VERSION} VERSION_LESS "3.4.0")
    MESSAGE(STATUS "Configuring for legacy CMake (CMake version ${CMAKE_VERSION} older than 3.4.0)")
    MESSAGE(STATUS "  Target propertes not supported (SOURCE_DIR, BINARY_DIR)")

    set(GRAPHENE_CHAIN_BIN_LEGACY "${PROJECT_BINARY_DIR}/libraries/chain" CACHE STRING "Path to fc chain")
    set(GRAPHENE_CHAIN_SOURCE_LEGACY "${PROJECT_SOURCE_DIR}/libraries/chain" CACHE STRING "Path to fc chain")
endif ()

INSTALL( TARGETS
   graphene_chain

   RUNTIME DESTINATION bin
   LIBRARY DESTINATION lib
   ARCHIVE DESTINATION lib
)
INSTALL( FILES ${HEADERS} DESTINATION "include/graphene/chain" )
