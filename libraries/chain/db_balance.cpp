/*
 * Copyright (c) 2015 Cryptonomex, Inc., and contributors.
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
#include <graphene/chain/vesting_balance_object.hpp>
#include <graphene/chain/witness_object.hpp>
#include <boost/range/algorithm.hpp>

namespace graphene { namespace chain {

asset database::get_balance(account_id_type owner, asset_id_type asset_id) const
{
   auto& index = get_index_type< primary_index< account_balance_index > >().get_secondary_index<balances_by_account_index>();
   auto abo = index.get_account_balance( owner, asset_id );
   if( !abo )
      return asset(0, asset_id);
   return abo->get_balance();
}

asset database::get_balance(const account_object& owner, const asset_object& asset_obj) const
{
   return get_balance(owner.get_id(), asset_obj.get_id());
}

string database::to_pretty_string( const asset& a )const
{
   return a.asset_id(*this).amount_to_pretty_string(a.amount);
}

void database::add_balance(account_id_type account, stored_value&& what )
{ try {
   if( what.get_amount() == 0 )
      return;
   FC_ASSERT( what.get_amount() > 0, "Cannot add a negative amount!" );

   auto& index = get_index_type< primary_index< account_balance_index > >().get_secondary_index<balances_by_account_index>();
   auto abo = index.get_account_balance( account, what.get_asset() );
   if( !abo )
      create<account_balance_object>([account,&what](account_balance_object& b) {
         b.owner = account;
         b.balance = std::move(what);
         if( b.balance.get_asset() == asset_id_type() ) // CORE asset
            b.maintenance_flag = true;
      });
   else
      modify(*abo, [&what] (account_balance_object& b) {
         if( b.balance.get_asset() == asset_id_type() ) // CORE asset
            b.maintenance_flag = true;
         b.balance += std::move(what);
      });
} FC_CAPTURE_AND_RETHROW( (account)(what) ) }

stored_value database::reduce_balance( account_id_type account, const asset& how_much )
{ try {
   if( how_much.amount == 0 )
      return stored_value( how_much.asset_id );
   FC_ASSERT( how_much.amount > 0, "Cannot reduce by a negative amount!" );

   auto& index = get_index_type< primary_index< account_balance_index > >().get_secondary_index<balances_by_account_index>();
   auto abo = index.get_account_balance( account, how_much.asset_id );
   FC_ASSERT( abo, "Insufficient Balance: ${a}'s balance of ${b} is less than required ${r}",
              ("a",account(*this).name)
              ("b",to_pretty_string(asset(0,how_much.asset_id)))
              ("r",to_pretty_string(how_much.amount)));
   FC_ASSERT( abo->get_amount() >= how_much.amount,
              "Insufficient Balance: ${a}'s balance of ${b} is less than required ${r}",
              ("a",account(*this).name)("b",to_pretty_string(abo->get_balance()))
              ("r",to_pretty_string(how_much.amount)));
   stored_value result;
   modify(*abo, [&how_much,&result] (account_balance_object& b) {
      if( b.balance.get_asset() == asset_id_type() ) // CORE asset
         b.maintenance_flag = true;
      result = b.balance.split( how_much.amount );
   });
   return result;
} FC_CAPTURE_AND_RETHROW( (account)(how_much) ) }

namespace detail {

   /**
    * Used as a key to search vesting_balance_object in the index
   */
   struct vbo_mfs_key
   {
      account_id_type   account_id;
      asset_id_type     asset_id;

      vbo_mfs_key(const account_id_type& account, const asset_id_type& asset):
         account_id(account),
         asset_id(asset)
      {}

      bool operator()(const vbo_mfs_key& k, const vesting_balance_object& vbo)const
      {
         return ( vbo.balance_type == vesting_balance_type::market_fee_sharing ) &&
                  ( k.asset_id == vbo.balance.get_asset() ) &&
                  ( k.account_id == vbo.owner );
      }

      uint64_t operator()(const vbo_mfs_key& k)const
      {
         return vbo_mfs_hash(k.account_id, k.asset_id);
      }
   };
} //detail

asset database::get_market_fee_vesting_balance(const account_id_type &account_id, const asset_id_type &asset_id)
{
   auto& vesting_balances = get_index_type<vesting_balance_index>().indices().get<by_vesting_type>();
   const auto& key = detail::vbo_mfs_key{account_id, asset_id};
   auto vbo_it = vesting_balances.find(key, key, key);

   if( vbo_it == vesting_balances.end() )
   {
      return asset(0, asset_id);
   }
   return vbo_it->balance.get_value();
}

void database::deposit_market_fee_vesting_balance(const account_id_type &account_id, stored_value&& delta)
{ try {
   FC_ASSERT( delta.get_amount() >= 0, "Invalid negative value for balance");

   if( delta.get_amount() == 0 )
      return;

   auto& vesting_balances = get_index_type<vesting_balance_index>().indices().get<by_vesting_type>();
   const auto& key = detail::vbo_mfs_key{account_id, delta.get_asset()};
   auto vbo_it = vesting_balances.find(key, key, key);

   auto block_time = head_block_time();

   if( vbo_it == vesting_balances.end() )
   {
      create<vesting_balance_object>([&account_id, &delta](vesting_balance_object &vbo) {
         vbo.owner = account_id;
         vbo.balance = std::move(delta);
         vbo.balance_type = vesting_balance_type::market_fee_sharing;
         vbo.policy = instant_vesting_policy{};
      });
   } else {
      modify( *vbo_it, [&block_time, &delta]( vesting_balance_object& vbo )
      {
         vbo.deposit_vested(block_time, std::move(delta));
      });
   }
} FC_CAPTURE_AND_RETHROW( (account_id)(delta) ) }

optional< vesting_balance_id_type > database::deposit_lazy_vesting(
   const optional< vesting_balance_id_type >& ovbid,
   stored_value&& amount, uint32_t req_vesting_seconds,
   vesting_balance_type balance_type,
   account_id_type req_owner,
   bool require_vesting )
{
   if( amount.get_amount() == 0 )
      return optional< vesting_balance_id_type >();

   fc::time_point_sec now = head_block_time();

   while( true )
   {
      if( !ovbid.valid() )
         break;
      const vesting_balance_object& vbo = (*ovbid)(*this);
      if( vbo.owner != req_owner )
         break;
      if( !vbo.policy.is_type< cdd_vesting_policy >() )
         break;
      if( vbo.policy.get< cdd_vesting_policy >().vesting_seconds != req_vesting_seconds )
         break;
      modify( vbo, [require_vesting,now,&amount]( vesting_balance_object& _vbo )
      {
         if( require_vesting )
            _vbo.deposit(now, std::move(amount) );
         else
            _vbo.deposit_vested(now, std::move(amount) );
      } );
      return optional< vesting_balance_id_type >();
   }

   const vesting_balance_object& vbo = create< vesting_balance_object >(
      [req_owner,&amount,balance_type,req_vesting_seconds,now,require_vesting]( vesting_balance_object& _vbo )
   {
      _vbo.owner = req_owner;
      _vbo.balance = std::move(amount);
      _vbo.balance_type = balance_type;

      cdd_vesting_policy policy;
      policy.vesting_seconds = req_vesting_seconds;
      policy.coin_seconds_earned = require_vesting ? 0 : _vbo.balance.get_amount().value * policy.vesting_seconds;
      policy.coin_seconds_earned_last_update = now;

      _vbo.policy = policy;
   } );

   return vbo.id;
}

void database::deposit_cashback(const account_object& acct, stored_value&& amount, bool require_vesting)
{
   // If we don't have a VBO, or if it has the wrong maturity
   // due to a policy change, cut it loose.

   if( amount.get_amount() == 0 )
      return;

   if( acct.get_id() == GRAPHENE_COMMITTEE_ACCOUNT || acct.get_id() == GRAPHENE_WITNESS_ACCOUNT ||
       acct.get_id() == GRAPHENE_RELAXED_COMMITTEE_ACCOUNT || acct.get_id() == GRAPHENE_NULL_ACCOUNT ||
       acct.get_id() == GRAPHENE_TEMP_ACCOUNT )
   {
      // The blockchain's accounts do not get cashback; it simply goes to the reserve pool.
      modify( get_core_dynamic_data(), [&amount](asset_dynamic_data_object& d) {
         d.current_supply.burn( std::move(amount) );
      });
      return;
   }

   optional< vesting_balance_id_type > new_vbid = deposit_lazy_vesting(
      acct.cashback_vb,
      std::move(amount),
      get_global_properties().parameters.cashback_vesting_period_seconds,
      vesting_balance_type::cashback,
      acct.id,
      require_vesting );

   if( new_vbid.valid() )
   {
      modify( acct, [&new_vbid]( account_object& _acct )
      {
         _acct.cashback_vb = *new_vbid;
      } );
      modify( acct.statistics( *this ), []( account_statistics_object& aso )
      {
         aso.has_cashback_vb = true;
      } );
   }

   return;
}

void database::deposit_witness_pay(const witness_object& wit, stored_value&& amount)
{
   if( amount.get_amount() == 0 )
      return;

   optional< vesting_balance_id_type > new_vbid = deposit_lazy_vesting(
      wit.pay_vb,
      std::move(amount),
      get_global_properties().parameters.witness_pay_vesting_seconds,
      vesting_balance_type::witness,
      wit.witness_account,
      true );

   if( new_vbid.valid() )
   {
      modify( wit, [&new_vbid]( witness_object& _wit )
      {
         _wit.pay_vb = *new_vbid;
      } );
   }

   return;
}

} }
