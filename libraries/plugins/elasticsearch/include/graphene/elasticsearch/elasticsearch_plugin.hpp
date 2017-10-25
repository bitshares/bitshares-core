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
#pragma once

#include <graphene/app/plugin.hpp>
#include <graphene/chain/database.hpp>

#include <graphene/chain/operation_history_object.hpp>

#include <fc/thread/future.hpp>

namespace graphene { namespace elasticsearch {
   using namespace chain;
   //using namespace graphene::db;
   //using boost::multi_index_container;
   //using namespace boost::multi_index;

//
// Plugins should #define their SPACE_ID's so plugins with
// conflicting SPACE_ID assignments can be compiled into the
// same binary (by simply re-assigning some of the conflicting #defined
// SPACE_ID's in a build script).
//
// Assignment of SPACE_ID's cannot be done at run-time because
// various template automagic depends on them being known at compile
// time.
//
#ifndef ELASTICSEARCH_SPACE_ID
#define ELASTICSEARCH_SPACE_ID 6
#endif

static size_t WriteCallback(void *contents, size_t size, size_t nmemb, void *userp)
{
   ((std::string*)userp)->append((char*)contents, size * nmemb);
   return size * nmemb;
}

namespace detail
{
    class elasticsearch_plugin_impl;
}

class elasticsearch_plugin : public graphene::app::plugin
{
   public:
      elasticsearch_plugin();
      virtual ~elasticsearch_plugin();

      std::string plugin_name()const override;
      virtual void plugin_set_program_options(
         boost::program_options::options_description& cli,
         boost::program_options::options_description& cfg) override;
      virtual void plugin_initialize(const boost::program_options::variables_map& options) override;
      virtual void plugin_startup() override;

      friend class detail::elasticsearch_plugin_impl;
      std::unique_ptr<detail::elasticsearch_plugin_impl> my;
};


struct operation_visitor
{
   typedef void result_type;

   share_type fee_amount;
   asset_id_type fee_asset;

   asset_id_type transfer_asset_id;
   share_type transfer_amount;

   void operator()( const graphene::chain::transfer_operation& o )
   {
      fee_asset = o.fee.asset_id;
      fee_amount = o.fee.amount;

      transfer_asset_id = o.amount.asset_id;
      transfer_amount = o.amount.amount;
   }
   void operator()( const graphene::chain::limit_order_create_operation& o )
   {
      fee_asset = o.fee.asset_id;
      fee_amount = o.fee.amount;
   }
   void operator()( const graphene::chain::limit_order_cancel_operation& o )
   {
      fee_asset = o.fee.asset_id;
      fee_amount = o.fee.amount;
   }
   void operator()( const graphene::chain::call_order_update_operation& o )
   {
      fee_asset = o.fee.asset_id;
      fee_amount = o.fee.amount;
   }
   void operator()( const graphene::chain::fill_order_operation& o )
   {
      fee_asset = o.fee.asset_id;
      fee_amount = o.fee.amount;
   }
   void operator()( const graphene::chain::account_create_operation& o )
   {
      fee_asset = o.fee.asset_id;
      fee_amount = o.fee.amount;
   }
   void operator()( const graphene::chain::account_update_operation& o )
   {
      fee_asset = o.fee.asset_id;
      fee_amount = o.fee.amount;
   }
   void operator()( const graphene::chain::account_whitelist_operation& o )
   {
      fee_asset = o.fee.asset_id;
      fee_amount = o.fee.amount;
   }
   void operator()( const graphene::chain::account_upgrade_operation& o )
   {
      fee_asset = o.fee.asset_id;
      fee_amount = o.fee.amount;
   }
   void operator()( const graphene::chain::account_transfer_operation& o )
   {
      fee_asset = o.fee.asset_id;
      fee_amount = o.fee.amount;
   }
   void operator()( const graphene::chain::asset_create_operation& o )
   {
      fee_asset = o.fee.asset_id;
      fee_amount = o.fee.amount;
   }
   void operator()( const graphene::chain::asset_update_operation& o )
   {
      fee_asset = o.fee.asset_id;
      fee_amount = o.fee.amount;
   }
   void operator()( const graphene::chain::asset_update_bitasset_operation& o )
   {
      fee_asset = o.fee.asset_id;
      fee_amount = o.fee.amount;
   }
   void operator()( const graphene::chain::asset_update_feed_producers_operation& o )
   {
      fee_asset = o.fee.asset_id;
      fee_amount = o.fee.amount;
   }
   void operator()( const graphene::chain::asset_issue_operation& o )
   {
      fee_asset = o.fee.asset_id;
      fee_amount = o.fee.amount;
   }
   void operator()( const graphene::chain::asset_reserve_operation& o )
   {
      fee_asset = o.fee.asset_id;
      fee_amount = o.fee.amount;
   }
   void operator()( const graphene::chain::asset_fund_fee_pool_operation& o )
   {
      fee_asset = o.fee.asset_id;
      fee_amount = o.fee.amount;
   }
   void operator()( const graphene::chain::asset_settle_operation& o )
   {
      fee_asset = o.fee.asset_id;
      fee_amount = o.fee.amount;
   }
   void operator()( const graphene::chain::asset_global_settle_operation& o )
   {
      fee_asset = o.fee.asset_id;
      fee_amount = o.fee.amount;
   }
   void operator()( const graphene::chain::asset_publish_feed_operation& o )
   {
      fee_asset = o.fee.asset_id;
      fee_amount = o.fee.amount;
   }
   void operator()( const graphene::chain::witness_create_operation& o )
   {
      fee_asset = o.fee.asset_id;
      fee_amount = o.fee.amount;
   }
   void operator()( const graphene::chain::witness_update_operation& o )
   {
      fee_asset = o.fee.asset_id;
      fee_amount = o.fee.amount;
   }
   void operator()( const graphene::chain::proposal_create_operation& o )
   {
      fee_asset = o.fee.asset_id;
      fee_amount = o.fee.amount;
   }
   void operator()( const graphene::chain::proposal_update_operation& o )
   {
      fee_asset = o.fee.asset_id;
      fee_amount = o.fee.amount;
   }
   void operator()( const graphene::chain::proposal_delete_operation& o )
   {
      fee_asset = o.fee.asset_id;
      fee_amount = o.fee.amount;
   }
   void operator()( const graphene::chain::withdraw_permission_create_operation& o )
   {
      fee_asset = o.fee.asset_id;
      fee_amount = o.fee.amount;
   }
   void operator()( const graphene::chain::withdraw_permission_update_operation& o )
   {
      fee_asset = o.fee.asset_id;
      fee_amount = o.fee.amount;
   }
   void operator()( const graphene::chain::withdraw_permission_claim_operation& o )
   {
      fee_asset = o.fee.asset_id;
      fee_amount = o.fee.amount;
   }
   void operator()( const graphene::chain::withdraw_permission_delete_operation& o )
   {
      fee_asset = o.fee.asset_id;
      fee_amount = o.fee.amount;
   }
   void operator()( const graphene::chain::committee_member_create_operation& o )
   {
      fee_asset = o.fee.asset_id;
      fee_amount = o.fee.amount;
   }
   void operator()( const graphene::chain::committee_member_update_operation& o )
   {
      fee_asset = o.fee.asset_id;
      fee_amount = o.fee.amount;
   }
   void operator()( const graphene::chain::committee_member_update_global_parameters_operation& o )
   {
      fee_asset = o.fee.asset_id;
      fee_amount = o.fee.amount;
   }
   void operator()( const graphene::chain::vesting_balance_create_operation& o )
   {
      fee_asset = o.fee.asset_id;
      fee_amount = o.fee.amount;
   }
   void operator()( const graphene::chain::vesting_balance_withdraw_operation& o )
   {
      fee_asset = o.fee.asset_id;
      fee_amount = o.fee.amount;
   }
   void operator()( const graphene::chain::worker_create_operation& o )
   {
      fee_asset = o.fee.asset_id;
      fee_amount = o.fee.amount;
   }
   void operator()( const graphene::chain::custom_operation& o )
   {
      fee_asset = o.fee.asset_id;
      fee_amount = o.fee.amount;
   }
   void operator()( const graphene::chain::assert_operation& o )
   {
      fee_asset = o.fee.asset_id;
      fee_amount = o.fee.amount;
   }
   void operator()( const graphene::chain::balance_claim_operation& o )
   {
      fee_asset = o.fee.asset_id;
      fee_amount = o.fee.amount;
   }
   void operator()( const graphene::chain::override_transfer_operation& o )
   {
      fee_asset = o.fee.asset_id;
      fee_amount = o.fee.amount;
   }
   void operator()( const graphene::chain::transfer_to_blind_operation& o )
   {
      fee_asset = o.fee.asset_id;
      fee_amount = o.fee.amount;
   }
   void operator()( const graphene::chain::blind_transfer_operation& o )
   {
      fee_asset = o.fee.asset_id;
      fee_amount = o.fee.amount;
   }
   void operator()( const graphene::chain::transfer_from_blind_operation& o )
   {
      fee_asset = o.fee.asset_id;
      fee_amount = o.fee.amount;
   }
   void operator()( const graphene::chain::asset_settle_cancel_operation& o )
   {
      fee_asset = o.fee.asset_id;
      fee_amount = o.fee.amount;
   }
   void operator()( const graphene::chain::asset_claim_fees_operation& o )
   {
      fee_asset = o.fee.asset_id;
      fee_amount = o.fee.amount;
   }
   void operator()( const graphene::chain::fba_distribute_operation& o )
   {
      fee_asset = o.fee.asset_id;
      fee_amount = o.fee.amount;
   }
   void operator()( const graphene::chain::bid_collateral_operation& o )
   {
      fee_asset = o.fee.asset_id;
      fee_amount = o.fee.amount;
   }
   void operator()( const graphene::chain::execute_bid_operation& o )
   {
      fee_asset = o.fee.asset_id;
      fee_amount = o.fee.amount;
   }

};


} } //graphene::elasticsearch
