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
#include <graphene/chain/limit_order_object.hpp>
#include <graphene/chain/call_order_object.hpp>
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
      idump(("balance")(a));
      total_balances[a.asset_type] += a.balance;
   }
   for( const account_statistics_object& s : statistics_index )
   {
      idump(("statistics")(s));
      reported_core_in_orders += s.total_core_in_orders;
   }
   for( const limit_order_object& o : db.get_index_type<limit_order_index>().indices() )
   {
      idump(("limit_order")(o));
      auto for_sale = o.amount_for_sale();
      if( for_sale.asset_id == asset_id_type() ) core_in_orders += for_sale.amount;
      total_balances[for_sale.asset_id] += for_sale.amount;
   }
   for( const call_order_object& o : db.get_index_type<call_order_index>().indices() )
   {
      idump(("call_order")(o));
      auto col = o.get_collateral();
      if( col.asset_id == asset_id_type() ) core_in_orders += col.amount;
      total_balances[col.asset_id] += col.amount;
      total_debts[o.get_debt().asset_id] += o.get_debt().amount;
   }
   for( const asset_object& asset_obj : db.get_index_type<asset_index>().indices() )
   {
      total_balances[asset_obj.id] += asset_obj.dynamic_asset_data_id(db).accumulated_fees;
      total_balances[asset_id_type()] += asset_obj.dynamic_asset_data_id(db).fee_pool;
   }
   for( const witness_object& witness_obj : db.get_index_type<witness_index>().indices() )
   {
      //idump((witness_obj));
      total_balances[asset_id_type()] += witness_obj.accumulated_income;
   }
   if( total_balances[asset_id_type()].value != core_asset_data.current_supply.value )
   {
      edump( (total_balances[asset_id_type()].value)(core_asset_data.current_supply.value ));
   }

   const auto& vbidx = db.get_index_type<simple_index<vesting_balance_object>>();
   for( const auto& s : vbidx )
      idump(("vesting_balance")(s));
}

} }
