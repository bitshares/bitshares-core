/*
 * Copyright (c) 2015, Cryptonomex, Inc.
 * All rights reserved.
 *
 * This source code is provided for evaluation in private test networks only, until September 8, 2015. After this date, this license expires and
 * the code may not be used, modified or distributed for any purpose. Redistribution and use in source and binary forms, with or without modification,
 * are permitted until September 8, 2015, provided that the following conditions are met:
 *
 * 1. The code and/or derivative works are used only for private test networks consisting of no more than 10 P2P nodes.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <graphene/chain/database.hpp>

#include <graphene/chain/account_object.hpp>
#include <graphene/chain/asset_object.hpp>
#include <graphene/chain/vesting_balance_object.hpp>

namespace graphene { namespace chain {

asset database::get_balance(account_id_type owner, asset_id_type asset_id) const
{
   auto& index = get_index_type<account_balance_index>().indices().get<by_balance>();
   auto itr = index.find(boost::make_tuple(owner, asset_id));
   if( itr == index.end() )
      return asset(0, asset_id);
   return itr->get_balance();
}

asset database::get_balance(const account_object& owner, const asset_object& asset_obj) const
{
   return get_balance(owner.get_id(), asset_obj.get_id());
}

string database::to_pretty_string( const asset& a )const
{
   return a.asset_id(*this).amount_to_pretty_string(a.amount);
}

void database::adjust_balance(account_id_type account, asset delta )
{ try {
   if( delta.amount == 0 )
      return;

   auto& index = get_index_type<account_balance_index>().indices().get<by_balance>();
   auto itr = index.find(boost::make_tuple(account, delta.asset_id));
   if(itr == index.end())
   {
      FC_ASSERT(delta.amount > 0);
      create<account_balance_object>([account,&delta](account_balance_object& b) {
         b.owner = account;
         b.asset_type = delta.asset_id;
         b.balance = delta.amount.value;
      });
   } else {
      if( delta.amount < 0 )
         FC_ASSERT( itr->get_balance() >= -delta, "Insufficient Balance: ${b} is less than required ${r}", ("b",to_pretty_string(itr->get_balance()))("r",to_pretty_string(-delta)));
      modify(*itr, [delta](account_balance_object& b) {
         b.adjust_balance(delta);
      });
   }

} FC_CAPTURE_AND_RETHROW( (account)(delta) ) }

void database::adjust_balance(const account_object& account, asset delta )
{
   adjust_balance( account.id, delta);
}

void database::adjust_core_in_orders( const account_object& acnt, asset delta )
{
   if( delta.asset_id == asset_id_type(0) && delta.amount != 0 )
   {
      modify( acnt.statistics(*this), [&](account_statistics_object& stat){
          stat.total_core_in_orders += delta.amount;
      });
   }
}

void database::deposit_cashback(const account_object& acct, share_type amount, bool require_vesting)
{
   // If we don't have a VBO, or if it has the wrong maturity
   // due to a policy change, cut it loose.

   if( amount == 0 )
      return;

   if( acct.get_id() == GRAPHENE_COMMITTEE_ACCOUNT || acct.get_id() == GRAPHENE_WITNESS_ACCOUNT ||
       acct.get_id() == GRAPHENE_RELAXED_COMMITTEE_ACCOUNT || acct.get_id() == GRAPHENE_NULL_ACCOUNT ||
       acct.get_id() == GRAPHENE_TEMP_ACCOUNT )
   {
      // The blockchain's accounts do not get cashback; it simply goes to the reserve pool.
      modify(get(asset_id_type()).dynamic_asset_data_id(*this), [amount](asset_dynamic_data_object& d) {
         d.current_supply -= amount;
      });
      return;
   }

   uint32_t global_vesting_seconds = get_global_properties().parameters.cashback_vesting_period_seconds;
   fc::time_point_sec now = head_block_time();

   while( true )
   {
      if( !acct.cashback_vb.valid() )
         break;
      const vesting_balance_object& cashback_vb = (*acct.cashback_vb)(*this);
      if( cashback_vb.policy.which() != vesting_policy::tag< cdd_vesting_policy >::value )
         break;
      if( cashback_vb.policy.get< cdd_vesting_policy >().vesting_seconds != global_vesting_seconds )
         break;

      modify( cashback_vb, [&]( vesting_balance_object& obj )
      {
         if( require_vesting )
            obj.deposit(now, amount);
         else
            obj.deposit_vested(now, amount);
      } );
      return;
   }

   const vesting_balance_object& cashback_vb = create< vesting_balance_object >( [&]( vesting_balance_object& obj )
   {
      obj.owner = acct.id;
      obj.balance = amount;

      cdd_vesting_policy policy;
      policy.vesting_seconds = global_vesting_seconds;
      policy.coin_seconds_earned = require_vesting? 0 : amount.value * policy.vesting_seconds;
      policy.coin_seconds_earned_last_update = now;

      obj.policy = policy;
   } );

   modify( acct, [&]( account_object& _acct )
   {
      _acct.cashback_vb = cashback_vb.id;
   } );

   return;
}

} }
