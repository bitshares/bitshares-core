
#include <graphene/chain/account_object.hpp>
#include <graphene/chain/asset_object.hpp>
#include <graphene/chain/block_summary_object.hpp>
#include <graphene/chain/call_order_object.hpp>
#include <graphene/chain/delegate_object.hpp>
#include <graphene/chain/global_property_object.hpp>
#include <graphene/chain/limit_order_object.hpp>
#include <graphene/chain/operation_history_object.hpp>
#include <graphene/chain/proposal_object.hpp>
#include <graphene/chain/transaction_object.hpp>
#include <graphene/chain/vesting_balance_object.hpp>
#include <graphene/chain/withdraw_permission_object.hpp>
#include <graphene/chain/witness_object.hpp>
#include <graphene/chain/witness_schedule_object.hpp>
#include <graphene/chain/worker_object.hpp>

namespace graphene { namespace chain {

// C++ requires that static class variables declared and initialized
// in headers must also have a definition in a single source file,
// else linker errors will occur [1].
//
// The purpose of this source file is to collect such definitions in
// a single place.
//
// [1] http://stackoverflow.com/questions/8016780/undefined-reference-to-static-constexpr-char

const uint8_t account_object::space_id;
const uint8_t account_object::type_id;

const uint8_t asset_object::space_id;
const uint8_t asset_object::type_id;

const uint8_t block_summary_object::space_id;
const uint8_t block_summary_object::type_id;

const uint8_t call_order_object::space_id;
const uint8_t call_order_object::type_id;

const uint8_t delegate_object::space_id;
const uint8_t delegate_object::type_id;

const uint8_t force_settlement_object::space_id;
const uint8_t force_settlement_object::type_id;

const uint8_t global_property_object::space_id;
const uint8_t global_property_object::type_id;

const uint8_t limit_order_object::space_id;
const uint8_t limit_order_object::type_id;

const uint8_t operation_history_object::space_id;
const uint8_t operation_history_object::type_id;

const uint8_t proposal_object::space_id;
const uint8_t proposal_object::type_id;

const uint8_t transaction_object::space_id;
const uint8_t transaction_object::type_id;

const uint8_t vesting_balance_object::space_id;
const uint8_t vesting_balance_object::type_id;

const uint8_t withdraw_permission_object::space_id;
const uint8_t withdraw_permission_object::type_id;

const uint8_t witness_object::space_id;
const uint8_t witness_object::type_id;

const uint8_t witness_schedule_object::space_id;
const uint8_t witness_schedule_object::type_id;

const uint8_t worker_object::space_id;
const uint8_t worker_object::type_id;

} }
