/*
 * Copyright (c) 2015 Cryptonomex, Inc., and contributors.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:
 *
 * 1. Any modified source or binaries are used only with the BitShares network.
 *
 * 2. Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
 *
 * 3. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <graphene/chain/database.hpp>

#include <graphene/chain/account_object.hpp>
#include <graphene/chain/asset_object.hpp>
#include <graphene/chain/market_evaluator.hpp>
#include <graphene/chain/vesting_balance_object.hpp>
#include <graphene/chain/witness_object.hpp>

namespace graphene { namespace chain {

/**
 *  This method dumps the state of the blockchain in a semi-human readable form for the
 *  purpose of tracking down funds and mismatches in currency allocation
 */
void database::debug_dump()
{
   const auto& db = *this;
   const asset_dynamic_data_object& core_asset_data = db.get_core_asset().dynamic_asset_data_id(db);

   const auto& balance_index = db.get_index_type<account_balance_index>().indices();
   const simple_index<account_statistics_object>& statistics_index = db.get_index_type<simple_index<account_statistics_object>>();
   map<asset_id_type,share_type> total_balances;
   map<asset_id_type,share_type> total_debts;
   share_type core_in_orders;
   share_type reported_core_in_orders;

   for( const account_balance_object& a : balance_index )
   {
    //  idump(("balance")(a));
      total_balances[a.asset_type] += a.balance;
   }
   for( const account_statistics_object& s : statistics_index )
   {
    //  idump(("statistics")(s));
      reported_core_in_orders += s.total_core_in_orders;
   }
   for( const limit_order_object& o : db.get_index_type<limit_order_index>().indices() )
   {
 //     idump(("limit_order")(o));
      auto for_sale = o.amount_for_sale();
      if( for_sale.asset_id == asset_id_type() ) core_in_orders += for_sale.amount;
      total_balances[for_sale.asset_id] += for_sale.amount;
   }
   for( const call_order_object& o : db.get_index_type<call_order_index>().indices() )
   {
//      idump(("call_order")(o));
      auto col = o.get_collateral();
      if( col.asset_id == asset_id_type() ) core_in_orders += col.amount;
      total_balances[col.asset_id] += col.amount;
      total_debts[o.get_debt().asset_id] += o.get_debt().amount;
   }
   for( const asset_object& asset_obj : db.get_index_type<asset_index>().indices() )
   {
      total_balances[asset_obj.id] += asset_obj.dynamic_asset_data_id(db).accumulated_fees;
      total_balances[asset_id_type()] += asset_obj.dynamic_asset_data_id(db).fee_pool;
//      edump((total_balances[asset_obj.id])(asset_obj.dynamic_asset_data_id(db).current_supply ) );
   }

   if( total_balances[asset_id_type()].value != core_asset_data.current_supply.value )
   {
      edump( (total_balances[asset_id_type()].value)(core_asset_data.current_supply.value ));
   }


   /*
   const auto& vbidx = db.get_index_type<simple_index<vesting_balance_object>>();
   for( const auto& s : vbidx )
   {
//      idump(("vesting_balance")(s));
   }
   */
}

} }
