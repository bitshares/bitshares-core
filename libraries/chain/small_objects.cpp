/*
 * Copyright (c) 2019 BitShares Blockchain Foundation, and contributors.
 *
 * The MIT License
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include <graphene/protocol/fee_schedule.hpp>

#include <graphene/chain/balance_object.hpp>
#include <graphene/chain/block_summary_object.hpp>
#include <graphene/chain/budget_record_object.hpp>
#include <graphene/chain/buyback_object.hpp>
#include <graphene/chain/chain_property_object.hpp>
#include <graphene/chain/committee_member_object.hpp>
#include <graphene/chain/confidential_object.hpp>
#include <graphene/chain/fba_object.hpp>
#include <graphene/chain/global_property_object.hpp>
#include <graphene/chain/htlc_object.hpp>
#include <graphene/chain/operation_history_object.hpp>
#include <graphene/chain/special_authority_object.hpp>
#include <graphene/chain/transaction_history_object.hpp>
#include <graphene/chain/withdraw_permission_object.hpp>
#include <graphene/chain/witness_object.hpp>
#include <graphene/chain/witness_schedule_object.hpp>
#include <graphene/chain/worker_object.hpp>

#include <fc/io/raw.hpp>

namespace graphene { namespace chain {

class balance_backup : public balance_master
{
   private:
      balance_backup( const balance_object& original )
         : balance_master( original )
      {
         balance = original.balance.get_value();
      }
      asset balance;
      friend class balance_object;
};

unique_ptr<object> balance_object::backup()const
{
   return std::make_unique<balance_backup>( *this );
}

void balance_object::restore( object& obj )
{
   const auto& backup = static_cast<balance_backup&>(obj);
   balance.restore( backup.balance );
   static_cast<balance_master&>(*this) = std::move( backup );
}

class dynamic_global_property_backup : public dynamic_global_property_master
{
   private:
      dynamic_global_property_backup( const dynamic_global_property_object& original )
         : dynamic_global_property_master( original )
      {
         witness_budget = original.witness_budget.get_value();
      }
      asset witness_budget;
      friend class dynamic_global_property_object;
};

unique_ptr<object> dynamic_global_property_object::backup()const
{
   return std::make_unique<dynamic_global_property_backup>( *this );
}

void dynamic_global_property_object::restore( object& obj )
{
   const auto& backup = static_cast<dynamic_global_property_backup&>(obj);
   balance.restore( backup.witness_budget ) );
   static_cast<dynamic_global_property_master&>(*this) = std::move( backup );
}

class htlc_backup : public htlc_master
{
   private:
      htlc_backup( const htlc_object& original )
         : htlc_master( original )
      {
         transfer = original.transfer;
         amount = original.transfer.amount.get_value();
      }
      asset amount;
      transfer_info_master transfer;
      friend class htlc_object;
};

unique_ptr<object> htlc_object::backup()const
{
   return std::make_unique<htlc_backup>( *this );
}

void htlc_object::restore( object& obj )
{
   const auto& backup = static_cast<htlc_backup&>(obj);
   static_cast<transfer_info_master&>(transfer) = backup.transfer;
   transfer.amount.restore( backup.amount );
   static_cast<htlc_master&>(*this) = std::move( backup );
}

} } // graphene::chain

FC_REFLECT_DERIVED_NO_TYPENAME( graphene::chain::balance_master, (graphene::db::object),
                    (owner)(vesting_policy)(last_claim_date) )
FC_REFLECT_DERIVED_NO_TYPENAME( graphene::chain::balance_object, (graphene::chain::balance_master),
                    (balance) )

FC_REFLECT_DERIVED_NO_TYPENAME( graphene::chain::block_summary_object, (graphene::db::object), (block_id) )

FC_REFLECT_DERIVED_NO_TYPENAME(
   graphene::chain::budget_record, BOOST_PP_SEQ_NIL,
   (time_since_last_budget)
   (from_initial_reserve)
   (from_accumulated_fees)
   (from_unused_witness_budget)
   (requested_witness_budget)
   (total_budget)
   (witness_budget)
   (worker_budget)
   (leftover_worker_funds)
   (supply_delta)
)

FC_REFLECT_DERIVED_NO_TYPENAME(
   graphene::chain::budget_record_object,
   (graphene::db::object),
   (time)
   (record)
)

FC_REFLECT_DERIVED_NO_TYPENAME( graphene::chain::buyback_object, (graphene::db::object), (asset_to_buy) )


FC_REFLECT_DERIVED_NO_TYPENAME( graphene::chain::immutable_chain_parameters, BOOST_PP_SEQ_NIL,
   (min_committee_member_count)
   (min_witness_count)
   (num_special_accounts)
   (num_special_assets)
)

FC_REFLECT_DERIVED_NO_TYPENAME( graphene::chain::chain_property_object, (graphene::db::object),
                    (chain_id)
                    (immutable_parameters)
                  )

FC_REFLECT_DERIVED_NO_TYPENAME( graphene::chain::committee_member_object, (graphene::db::object),
                    (committee_member_account)(vote_id)(total_votes)(url) )

FC_REFLECT_DERIVED_NO_TYPENAME( graphene::chain::blinded_balance_object, (graphene::db::object),
                                (commitment)(asset_id)(owner) )

FC_REFLECT_DERIVED_NO_TYPENAME( graphene::chain::dynamic_global_property_master, (graphene::db::object),
                    (head_block_number)
                    (head_block_id)
                    (time)
                    (current_witness)
                    (next_maintenance_time)
                    (last_budget_time)
                    (accounts_registered_this_interval)
                    (recently_missed_count)
                    (current_aslot)
                    (recent_slots_filled)
                    (dynamic_flags)
                    (last_irreversible_block_num)
                  )
FC_REFLECT_DERIVED_NO_TYPENAME( graphene::chain::dynamic_global_property_object, (graphene::chain::dynamic_global_property_master),
                    (witness_budget)
                  )

FC_REFLECT_DERIVED_NO_TYPENAME( graphene::chain::global_property_object, (graphene::db::object),
                    (parameters)
                    (pending_parameters)
                    (next_available_vote_id)
                    (active_committee_members)
                    (active_witnesses)
                  )

FC_REFLECT( graphene::chain::htlc_master::transfer_info_master, (from) (to) )
FC_REFLECT_DERIVED_NO_TYPENAME( graphene::chain::htlc_object::condition_info::hash_lock_info, BOOST_PP_SEQ_NIL,
   (preimage_hash) (preimage_size) )
FC_REFLECT_DERIVED_NO_TYPENAME( graphene::chain::htlc_object::condition_info::time_lock_info, BOOST_PP_SEQ_NIL,
   (expiration) )
FC_REFLECT_DERIVED_NO_TYPENAME( graphene::chain::htlc_object::condition_info, BOOST_PP_SEQ_NIL,
   (hash_lock)(time_lock) )
FC_REFLECT_DERIVED_NO_TYPENAME( graphene::chain::htlc_master, (graphene::db::object),
               (conditions) )
FC_REFLECT_DERIVED_NO_TYPENAME( graphene::chain::htlc_object::transfer_info,
                                (graphene::chain::htlc_master::transfer_info_master),
                                (from) (to) )
FC_REFLECT_DERIVED_NO_TYPENAME( graphene::chain::htlc_object, (graphene::chain::htlc_master),
               (transfer) )

FC_REFLECT_DERIVED_NO_TYPENAME( graphene::chain::operation_history_object, (graphene::chain::object),
                    (op)(result)(block_num)(trx_in_block)(op_in_trx)(virtual_op) )

FC_REFLECT_DERIVED_NO_TYPENAME( graphene::chain::account_transaction_history_object, (graphene::chain::object),
                    (account)(operation_id)(sequence)(next) )

FC_REFLECT_DERIVED_NO_TYPENAME(
   graphene::chain::special_authority_object,
   (graphene::db::object),
   (account)
)

FC_REFLECT_DERIVED_NO_TYPENAME( graphene::chain::transaction_history_object, (graphene::db::object), (trx)(trx_id) )

FC_REFLECT_DERIVED_NO_TYPENAME( graphene::chain::withdraw_permission_object, (graphene::db::object),
                    (withdraw_from_account)
                    (authorized_account)
                    (withdrawal_limit)
                    (withdrawal_period_sec)
                    (period_start_time)
                    (expiration)
                    (claimed_this_period)
                 )

FC_REFLECT_DERIVED_NO_TYPENAME( graphene::chain::witness_object, (graphene::db::object),
                    (witness_account)
                    (last_aslot)
                    (signing_key)
                    (pay_vb)
                    (vote_id)
                    (total_votes)
                    (url)
                    (total_missed)
                    (last_confirmed_block_num)
                  )

FC_REFLECT_DERIVED_NO_TYPENAME(
   graphene::chain::witness_schedule_object,
   (graphene::db::object),
   (current_shuffled_witnesses)
)


FC_REFLECT_DERIVED_NO_TYPENAME( graphene::chain::refund_worker_type, BOOST_PP_SEQ_NIL, (total_burned) )
FC_REFLECT_DERIVED_NO_TYPENAME( graphene::chain::vesting_balance_worker_type, BOOST_PP_SEQ_NIL, (balance) )
FC_REFLECT_DERIVED_NO_TYPENAME( graphene::chain::burn_worker_type, BOOST_PP_SEQ_NIL, (total_burned) )
FC_REFLECT_DERIVED_NO_TYPENAME( graphene::chain::worker_object, (graphene::db::object),
                    (worker_account)
                    (work_begin_date)
                    (work_end_date)
                    (daily_pay)
                    (worker)
                    (vote_for)
                    (vote_against)
                    (total_votes_for)
                    (total_votes_against)
                    (name)
                    (url)
                  )

GRAPHENE_IMPLEMENT_EXTERNAL_SERIALIZATION( graphene::chain::balance_master )
GRAPHENE_IMPLEMENT_EXTERNAL_SERIALIZATION( graphene::chain::balance_object )
GRAPHENE_IMPLEMENT_EXTERNAL_SERIALIZATION( graphene::chain::block_summary_object )
GRAPHENE_IMPLEMENT_EXTERNAL_SERIALIZATION( graphene::chain::budget_record )
GRAPHENE_IMPLEMENT_EXTERNAL_SERIALIZATION( graphene::chain::budget_record_object )
GRAPHENE_IMPLEMENT_EXTERNAL_SERIALIZATION( graphene::chain::buyback_object )
GRAPHENE_IMPLEMENT_EXTERNAL_SERIALIZATION( graphene::chain::immutable_chain_parameters )
GRAPHENE_IMPLEMENT_EXTERNAL_SERIALIZATION( graphene::chain::chain_property_object )
GRAPHENE_IMPLEMENT_EXTERNAL_SERIALIZATION( graphene::chain::committee_member_object )
GRAPHENE_IMPLEMENT_EXTERNAL_SERIALIZATION( graphene::chain::blinded_balance_object )
GRAPHENE_IMPLEMENT_EXTERNAL_SERIALIZATION( graphene::chain::dynamic_global_property_master )
GRAPHENE_IMPLEMENT_EXTERNAL_SERIALIZATION( graphene::chain::dynamic_global_property_object )
GRAPHENE_IMPLEMENT_EXTERNAL_SERIALIZATION( graphene::chain::global_property_object )
GRAPHENE_IMPLEMENT_EXTERNAL_SERIALIZATION( graphene::chain::htlc_master )
GRAPHENE_IMPLEMENT_EXTERNAL_SERIALIZATION( graphene::chain::htlc_object )
GRAPHENE_IMPLEMENT_EXTERNAL_SERIALIZATION( graphene::chain::operation_history_object )
GRAPHENE_IMPLEMENT_EXTERNAL_SERIALIZATION( graphene::chain::account_transaction_history_object )
GRAPHENE_IMPLEMENT_EXTERNAL_SERIALIZATION( graphene::chain::special_authority_object )
GRAPHENE_IMPLEMENT_EXTERNAL_SERIALIZATION( graphene::chain::transaction_history_object )
GRAPHENE_IMPLEMENT_EXTERNAL_SERIALIZATION( graphene::chain::withdraw_permission_object )
GRAPHENE_IMPLEMENT_EXTERNAL_SERIALIZATION( graphene::chain::witness_object )
GRAPHENE_IMPLEMENT_EXTERNAL_SERIALIZATION( graphene::chain::witness_schedule_object )
GRAPHENE_IMPLEMENT_EXTERNAL_SERIALIZATION( graphene::chain::worker_object )
