/*
 * Copyright (c) 2017 Cryptonomex, Inc., and contributors.
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

#include <graphene/chain/database.hpp>

#include <graphene/chain/account_object.hpp>
#include <graphene/chain/asset_object.hpp>
#include <graphene/chain/balance_object.hpp>
#include <graphene/chain/block_summary_object.hpp>
#include <graphene/chain/budget_record_object.hpp>
#include <graphene/chain/buyback_object.hpp>
#include <graphene/chain/chain_property_object.hpp>
#include <graphene/chain/committee_member_object.hpp>
#include <graphene/chain/confidential_object.hpp>
#include <graphene/chain/fba_object.hpp>
#include <graphene/chain/global_property_object.hpp>
#include <graphene/chain/liquidity_pool_object.hpp>
#include <graphene/chain/market_object.hpp>
#include <graphene/chain/operation_history_object.hpp>
#include <graphene/chain/proposal_object.hpp>
#include <graphene/chain/samet_fund_object.hpp>
#include <graphene/chain/special_authority_object.hpp>
#include <graphene/chain/ticket_object.hpp>
#include <graphene/chain/transaction_history_object.hpp>
#include <graphene/chain/vesting_balance_object.hpp>
#include <graphene/chain/withdraw_permission_object.hpp>
#include <graphene/chain/witness_object.hpp>
#include <graphene/chain/witness_schedule_object.hpp>
#include <graphene/chain/worker_object.hpp>
#include <graphene/chain/htlc_object.hpp>
#include <graphene/chain/custom_authority_object.hpp>

#include <graphene/chain/account_evaluator.hpp>
#include <graphene/chain/asset_evaluator.hpp>
#include <graphene/chain/assert_evaluator.hpp>
#include <graphene/chain/balance_evaluator.hpp>
#include <graphene/chain/committee_member_evaluator.hpp>
#include <graphene/chain/confidential_evaluator.hpp>
#include <graphene/chain/custom_evaluator.hpp>
#include <graphene/chain/liquidity_pool_evaluator.hpp>
#include <graphene/chain/market_evaluator.hpp>
#include <graphene/chain/proposal_evaluator.hpp>
#include <graphene/chain/samet_fund_evaluator.hpp>
#include <graphene/chain/ticket_evaluator.hpp>
#include <graphene/chain/transfer_evaluator.hpp>
#include <graphene/chain/vesting_balance_evaluator.hpp>
#include <graphene/chain/withdraw_permission_evaluator.hpp>
#include <graphene/chain/witness_evaluator.hpp>
#include <graphene/chain/worker_evaluator.hpp>
#include <graphene/chain/htlc_evaluator.hpp>
#include <graphene/chain/custom_authority_evaluator.hpp>

namespace graphene { namespace chain {

void database::initialize_evaluators()
{
   _operation_evaluators.resize(255);
   register_evaluator<account_create_evaluator>();
   register_evaluator<account_update_evaluator>();
   register_evaluator<account_upgrade_evaluator>();
   register_evaluator<account_whitelist_evaluator>();
   register_evaluator<committee_member_create_evaluator>();
   register_evaluator<committee_member_update_evaluator>();
   register_evaluator<committee_member_update_global_parameters_evaluator>();
   register_evaluator<custom_evaluator>();
   register_evaluator<asset_create_evaluator>();
   register_evaluator<asset_issue_evaluator>();
   register_evaluator<asset_reserve_evaluator>();
   register_evaluator<asset_update_evaluator>();
   register_evaluator<asset_update_bitasset_evaluator>();
   register_evaluator<asset_update_feed_producers_evaluator>();
   register_evaluator<asset_settle_evaluator>();
   register_evaluator<asset_global_settle_evaluator>();
   register_evaluator<assert_evaluator>();
   register_evaluator<limit_order_create_evaluator>();
   register_evaluator<limit_order_cancel_evaluator>();
   register_evaluator<call_order_update_evaluator>();
   register_evaluator<bid_collateral_evaluator>();
   register_evaluator<transfer_evaluator>();
   register_evaluator<override_transfer_evaluator>();
   register_evaluator<asset_fund_fee_pool_evaluator>();
   register_evaluator<asset_publish_feeds_evaluator>();
   register_evaluator<proposal_create_evaluator>();
   register_evaluator<proposal_update_evaluator>();
   register_evaluator<proposal_delete_evaluator>();
   register_evaluator<vesting_balance_create_evaluator>();
   register_evaluator<vesting_balance_withdraw_evaluator>();
   register_evaluator<witness_create_evaluator>();
   register_evaluator<witness_update_evaluator>();
   register_evaluator<withdraw_permission_create_evaluator>();
   register_evaluator<withdraw_permission_claim_evaluator>();
   register_evaluator<withdraw_permission_update_evaluator>();
   register_evaluator<withdraw_permission_delete_evaluator>();
   register_evaluator<worker_create_evaluator>();
   register_evaluator<balance_claim_evaluator>();
   register_evaluator<transfer_to_blind_evaluator>();
   register_evaluator<transfer_from_blind_evaluator>();
   register_evaluator<blind_transfer_evaluator>();
   register_evaluator<asset_claim_fees_evaluator>();
   register_evaluator<asset_update_issuer_evaluator>();
   register_evaluator<asset_claim_pool_evaluator>();
   register_evaluator<htlc_create_evaluator>();
   register_evaluator<htlc_redeem_evaluator>();
   register_evaluator<htlc_extend_evaluator>();
   register_evaluator<custom_authority_create_evaluator>();
   register_evaluator<custom_authority_update_evaluator>();
   register_evaluator<custom_authority_delete_evaluator>();
   register_evaluator<ticket_create_evaluator>();
   register_evaluator<ticket_update_evaluator>();
   register_evaluator<liquidity_pool_create_evaluator>();
   register_evaluator<liquidity_pool_delete_evaluator>();
   register_evaluator<liquidity_pool_deposit_evaluator>();
   register_evaluator<liquidity_pool_withdraw_evaluator>();
   register_evaluator<liquidity_pool_exchange_evaluator>();
   register_evaluator<samet_fund_create_evaluator>();
   register_evaluator<samet_fund_delete_evaluator>();
   register_evaluator<samet_fund_update_evaluator>();
   register_evaluator<samet_fund_borrow_evaluator>();
   register_evaluator<samet_fund_repay_evaluator>();
}

void database::initialize_indexes()
{
   reset_indexes();
   _undo_db.set_max_size( GRAPHENE_MIN_UNDO_HISTORY );

   //Protocol object indexes
   add_index< primary_index<asset_index, 13> >(); // 8192 assets per chunk
   add_index< primary_index<force_settlement_index> >();

   add_index< primary_index<account_index, 20> >(); // ~1 million accounts per chunk
   add_index< primary_index<committee_member_index, 8> >(); // 256 members per chunk
   add_index< primary_index<witness_index, 10> >(); // 1024 witnesses per chunk
   add_index< primary_index<limit_order_index > >();
   add_index< primary_index<call_order_index > >();
   add_index< primary_index<proposal_index > >();
   add_index< primary_index<withdraw_permission_index > >();
   add_index< primary_index<vesting_balance_index> >();
   add_index< primary_index<worker_index> >();
   add_index< primary_index<balance_index> >();
   add_index< primary_index<blinded_balance_index> >();
   add_index< primary_index< htlc_index> >();
   add_index< primary_index< custom_authority_index> >();
   add_index< primary_index<ticket_index> >();
   add_index< primary_index<liquidity_pool_index> >();
   add_index< primary_index<samet_fund_index> >();

   //Implementation object indexes
   add_index< primary_index<transaction_index                             > >();

   auto bal_idx = add_index< primary_index<account_balance_index          > >();
   bal_idx->add_secondary_index<balances_by_account_index>();

   add_index< primary_index<asset_bitasset_data_index,                 13 > >(); // 8192
   add_index< primary_index<simple_index<global_property_object          >> >();
   add_index< primary_index<simple_index<dynamic_global_property_object  >> >();
   add_index< primary_index<account_stats_index,                       20 > >(); // 1 Mi
   add_index< primary_index<simple_index<asset_dynamic_data_object       >> >();
   add_index< primary_index<simple_index<block_summary_object            >> >();
   add_index< primary_index<simple_index<chain_property_object          > > >();
   add_index< primary_index<simple_index<witness_schedule_object        > > >();
   add_index< primary_index<simple_index<budget_record_object           > > >();
   add_index< primary_index< special_authority_index                      > >();
   add_index< primary_index< buyback_index                                > >();
   add_index< primary_index<collateral_bid_index                          > >();
   add_index< primary_index< simple_index< fba_accumulator_object       > > >();
}

} }
