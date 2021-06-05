file(GLOB HEADERS "include/graphene/protocol/*.hpp")

list(APPEND SOURCES account.cpp
                    assert.cpp
                    asset_ops.cpp
                    block.cpp
                    confidential.cpp
                    chain_parameters.cpp
                    fee_schedule.cpp
                    fee_schedule_calc.cpp
                    fee_schedule_reflect.cpp
                    memo.cpp
                    proposal.cpp
                    transfer.cpp
                    vote.cpp
                    witness.cpp
                    address.cpp
                    asset.cpp
                    authority.cpp
                    special_authority.cpp
                    restriction.cpp
                    custom_authority.cpp
                    committee_member.cpp
                    custom.cpp
                    market.cpp
                    liquidity_pool.cpp
                    samet_fund.cpp
                    ticket.cpp
                    operations.cpp
                    pts_address.cpp
                    small_ops.cpp
                    transaction.cpp
                    types.cpp
                    withdraw_permission.cpp
                    worker.cpp
                    htlc.cpp)


list(APPEND CUSTOM_AUTHS_FILES
     custom_authorities/create_predicate_fwd_1.cpp
     custom_authorities/create_predicate_fwd_2.cpp
     custom_authorities/create_predicate_fwd_3.cpp
     custom_authorities/restriction_predicate.cpp
     custom_authorities/list_1.cpp
     custom_authorities/list_2.cpp
     custom_authorities/list_3.cpp
     custom_authorities/list_4.cpp
     custom_authorities/list_5.cpp
     custom_authorities/list_6.cpp
     custom_authorities/list_7.cpp
     custom_authorities/list_8.cpp
     custom_authorities/list_9.cpp
     custom_authorities/list_10.cpp
     custom_authorities/list_11.cpp
     custom_authorities/list_12.cpp
     custom_authorities/list_13.cpp
     custom_authorities/list_14.cpp)

file(GLOB CUSTOM_AUTHS_HEADERS "custom_authorities/*.hxx")

add_library( graphene_protocol_custom_auths ${CUSTOM_AUTHS_FILES} ${CUSTOM_AUTHS_HEADERS} )
target_link_libraries( graphene_protocol_custom_auths fc )
target_include_directories( graphene_protocol_custom_auths PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include" )

if( MSVC )
   set_source_files_properties( ${CUSTOM_AUTHS_FILES} ${SOURCES} PROPERTIES COMPILE_FLAGS /bigobj )
else( MSVC )
   if( MINGW )
      set_source_files_properties( ${CUSTOM_AUTHS_FILES} ${SOURCES} PROPERTIES COMPILE_FLAGS -Wa,-mbig-obj )
   endif( MINGW )
endif( MSVC )

add_library( graphene_protocol ${SOURCES} ${HEADERS} )
target_link_libraries( graphene_protocol fc graphene_protocol_custom_auths )
target_include_directories( graphene_protocol PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include" )

install( TARGETS
   graphene_protocol

   RUNTIME DESTINATION bin
   LIBRARY DESTINATION lib
   ARCHIVE DESTINATION lib
)
install( FILES ${HEADERS} DESTINATION "include/graphene/protocol" )
