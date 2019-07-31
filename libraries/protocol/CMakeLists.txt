file(GLOB HEADERS "include/graphene/protocol/*.hpp")

list(APPEND SOURCES account.cpp
                    assert.cpp
                    asset_ops.cpp
                    block.cpp
                    confidential.cpp
                    chain_parameters.cpp
                    fee_schedule.cpp
                    memo.cpp
                    proposal.cpp
                    transfer.cpp
                    vote.cpp
                    witness.cpp
                    address.cpp
                    asset.cpp
                    authority.cpp
                    special_authority.cpp
                    committee_member.cpp
                    custom.cpp
                    market.cpp
                    operations.cpp
                    pts_address.cpp
                    small_ops.cpp
                    transaction.cpp
                    types.cpp
                    withdraw_permission.cpp
                    worker.cpp
                    htlc.cpp)


add_library( graphene_protocol ${SOURCES} ${HEADERS} )
target_link_libraries( graphene_protocol fc )
target_include_directories( graphene_protocol PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include" )

install( TARGETS
   graphene_protocol

   RUNTIME DESTINATION bin
   LIBRARY DESTINATION lib
   ARCHIVE DESTINATION lib
)
install( FILES ${HEADERS} DESTINATION "include/graphene/protocol" )
