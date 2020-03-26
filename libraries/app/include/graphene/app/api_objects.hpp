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
#pragma once

#include <graphene/chain/account_object.hpp>
#include <graphene/chain/asset_object.hpp>
#include <graphene/chain/balance_object.hpp>
#include <graphene/chain/fba_object.hpp>
#include <graphene/chain/global_property_object.hpp>
#include <graphene/chain/vesting_balance_object.hpp>
#include <graphene/chain/market_object.hpp>
#include <graphene/chain/proposal_object.hpp>
#include <graphene/chain/withdraw_permission_object.hpp>
#include <graphene/chain/htlc_object.hpp>

#include <graphene/api_helper_indexes/api_helper_indexes.hpp>
#include <graphene/market_history/market_history_plugin.hpp>

#include <fc/optional.hpp>

namespace graphene { namespace app {
   using namespace graphene::chain;
   using namespace graphene::market_history;

   template<typename Master, typename ApiObject>
   class api_object : public Master
   {
   public:
      api_object() {}
      api_object( const Master& orig ) : Master(orig) {}

      virtual variant to_variant()const { return variant( static_cast<const ApiObject&>(*this), MAX_NESTING ); }
      virtual vector<char> pack()const  { return fc::raw::pack( static_cast<const ApiObject&>(*this) ); }
   };

   class account_balance_api_object : public api_object< account_balance_master, account_balance_api_object >
   {
      public:
         account_balance_api_object() {}
         account_balance_api_object( const account_balance_object& orig)
            : api_object(orig)
         {
            balance = orig.balance.get_value();
         }
         asset balance;
   };

   class account_statistics_api_object
      : public api_object< account_statistics_master, account_statistics_api_object >
   {
      public:
         account_statistics_api_object() {}
         account_statistics_api_object( const account_statistics_object& orig)
            : api_object(orig)
         {
            pending_fees = orig.pending_fees.get_amount();
            pending_vested_fees = orig.pending_vested_fees.get_amount();
         }
         share_type pending_fees;
         share_type pending_vested_fees;
   };

   class asset_dynamic_data_api_object : public api_object< asset_dynamic_data_master, asset_dynamic_data_api_object >
   {
   public:
      asset_dynamic_data_api_object() {}
      asset_dynamic_data_api_object( const asset_dynamic_data_object& orig )
         : api_object(orig)
      {
         current_supply = orig.current_supply.get_amount();
         confidential_supply = orig.confidential_supply.get_amount();
         accumulated_fees = orig.accumulated_fees.get_amount();
         fee_pool = orig.fee_pool.get_amount();
      }

      share_type current_supply;
      share_type confidential_supply;
      share_type accumulated_fees;
      share_type fee_pool;
   };

   class asset_bitasset_data_api_object : public api_object< asset_bitasset_data_master, asset_bitasset_data_api_object >
   {
   public:
      asset_bitasset_data_api_object() {}
      asset_bitasset_data_api_object( const asset_bitasset_data_object& orig )
         : api_object(orig)
      {
         settlement_fund = orig.settlement_fund.get_amount();
      }

      share_type settlement_fund;
   };

   class balance_api_object : public api_object< balance_master, balance_api_object >
   {
      public:
         balance_api_object() {}
         balance_api_object( const balance_object& orig)
            : api_object(orig)
         {
            balance = orig.balance.get_value();
         }
         asset balance;
   };

   class fba_accumulator_api_object : public api_object< fba_accumulator_master, fba_accumulator_api_object >
   {
   public:
      fba_accumulator_api_object() {}
      fba_accumulator_api_object( const fba_accumulator_object& orig )
         : api_object(orig)
      {
         accumulated_fba_fees = orig.accumulated_fba_fees.get_amount();
      }

      share_type accumulated_fba_fees;
   };

   class collateral_bid_api_object : public api_object< collateral_bid_master, collateral_bid_api_object >
   {
      public:
         collateral_bid_api_object() {}
         collateral_bid_api_object( const collateral_bid_object& orig)
            : api_object(orig)
         {
            inv_swan_price = orig.collateral_offered.get_value() / orig.debt_covered;
         }
         price inv_swan_price;
   };

   class limit_order_api_object : public api_object< limit_order_master, limit_order_api_object >
   {
      public:
         limit_order_api_object() {}
         limit_order_api_object( const limit_order_object& orig ) : api_object(orig)
         {
            for_sale = orig.for_sale.get_amount();
            deferred_fee = orig.deferred_fee.get_amount();
            deferred_paid_fee = orig.deferred_paid_fee.get_value();
         }
         share_type for_sale;
         share_type deferred_fee;
         asset deferred_paid_fee;
   };

   class call_order_api_object : public api_object< call_order_master, call_order_api_object >
   {
      public:
         call_order_api_object() {}
         call_order_api_object( const call_order_object& orig) : api_object(orig)
         {
            debt = orig.debt.get_amount();
            collateral = orig.collateral.get_amount();
         }
         share_type debt;
         share_type collateral;
   };

   class force_settlement_api_object : public api_object< force_settlement_master, force_settlement_api_object >
   {
      public:
         force_settlement_api_object() {}
         force_settlement_api_object( const force_settlement_object& orig) : api_object(orig)
         {
            balance = orig.balance.get_value();
         }
         asset balance;
   };

   class htlc_api_object : public api_object< htlc_master, htlc_api_object >
   {
      public:
         htlc_api_object() {}
         htlc_api_object( const htlc_object& orig) : api_object(orig)
         {
            static_cast<transfer_info_master&>(transfer) = orig.transfer;
            transfer.amount = orig.transfer.amount.get_amount();
            transfer.asset_id = orig.transfer.amount.get_asset();
         }

         struct transfer_info : transfer_info_master {
            share_type amount;
            asset_id_type asset_id;
         } transfer;
   };

   class vesting_balance_api_object : public api_object< vesting_balance_master, vesting_balance_api_object >
   {
      public:
         vesting_balance_api_object() {}
         vesting_balance_api_object( const vesting_balance_object& orig) : api_object(orig)
         {
            balance = orig.balance.get_value();
         }
         asset balance;
   };

   class dynamic_global_property_api_object : public api_object< dynamic_global_property_master, dynamic_global_property_api_object >
   {
   public:
      dynamic_global_property_api_object() {}
      dynamic_global_property_api_object( const dynamic_global_property_object& orig ) : api_object(orig)
      {
         witness_budget = orig.witness_budget.get_value();
      }
      asset witness_budget;
   };

   struct more_data
   {
      bool balances = false;
      bool vesting_balances = false;
      bool limit_orders = false;
      bool call_orders = false;
      bool settle_orders = false;
      bool proposals = false;
      bool assets = false;
      bool withdraws_from = false;
      bool withdraws_to = false;
      bool htlcs_from = false;
      bool htlcs_to = false;
   };

   struct full_account
   {
      account_object                       account;
      account_statistics_api_object        statistics;
      string                               registrar_name;
      string                               referrer_name;
      string                               lifetime_referrer_name;
      vector<variant>                      votes;
      optional<vesting_balance_api_object> cashback_balance;
      vector<account_balance_api_object>   balances;
      vector<vesting_balance_api_object>   vesting_balances;
      vector<limit_order_api_object>       limit_orders;
      vector<call_order_api_object>        call_orders;
      vector<force_settlement_api_object>  settle_orders;
      vector<proposal_object>              proposals;
      vector<asset_id_type>                assets;
      vector<withdraw_permission_object>   withdraws_from;
      vector<withdraw_permission_object>   withdraws_to;
      vector<htlc_api_object>              htlcs_from;
      vector<htlc_api_object>              htlcs_to;
      more_data                            more_data_available;
   };

   struct order
   {
      string                     price;
      string                     quote;
      string                     base;
   };

   struct order_book
   {
     string                      base;
     string                      quote;
     vector< order >             bids;
     vector< order >             asks;
   };

   struct market_ticker
   {
      time_point_sec             time;
      string                     base;
      string                     quote;
      string                     latest;
      string                     lowest_ask;
      string                     highest_bid;
      string                     percent_change;
      string                     base_volume;
      string                     quote_volume;

      market_ticker() {}
      market_ticker(const market_ticker_object& mto,
                    const fc::time_point_sec& now,
                    const asset_object& asset_base,
                    const asset_object& asset_quote,
                    const order_book& orders);
      market_ticker(const fc::time_point_sec& now,
                    const asset_object& asset_base,
                    const asset_object& asset_quote);
   };

   struct market_volume
   {
      time_point_sec             time;
      string                     base;
      string                     quote;
      string                     base_volume;
      string                     quote_volume;
   };

   struct market_trade
   {
      int64_t                    sequence = 0;
      fc::time_point_sec         date;
      string                     price;
      string                     amount;
      string                     value;
      account_id_type            side1_account_id = GRAPHENE_NULL_ACCOUNT;
      account_id_type            side2_account_id = GRAPHENE_NULL_ACCOUNT;
   };

   struct extended_asset_object : asset_object
   {
      extended_asset_object() {}
      explicit extended_asset_object( const asset_object& a ) : asset_object( a ) {}
      explicit extended_asset_object( asset_object&& a ) : asset_object( std::move(a) ) {}

      optional<share_type> total_in_collateral;
      optional<share_type> total_backing_collateral;
   };

} }

FC_REFLECT( graphene::app::more_data,
            (balances) (vesting_balances) (limit_orders) (call_orders)
            (settle_orders) (proposals) (assets) (withdraws_from) (withdraws_to) (htlcs_from) (htlcs_to)
          )

FC_REFLECT( graphene::app::full_account,
            (account)
            (statistics)
            (registrar_name)
            (referrer_name)
            (lifetime_referrer_name)
            (votes)
            (cashback_balance)
            (balances)
            (vesting_balances)
            (limit_orders)
            (call_orders)
            (settle_orders)
            (proposals)
            (assets)
            (withdraws_from)
            (withdraws_to)
            (htlcs_from)
            (htlcs_to)
            (more_data_available)
          )

FC_REFLECT( graphene::app::order, (price)(quote)(base) );
FC_REFLECT( graphene::app::order_book, (base)(quote)(bids)(asks) );
FC_REFLECT( graphene::app::market_ticker,
            (time)(base)(quote)(latest)(lowest_ask)(highest_bid)(percent_change)(base_volume)(quote_volume) );
FC_REFLECT( graphene::app::market_volume, (time)(base)(quote)(base_volume)(quote_volume) );
FC_REFLECT( graphene::app::market_trade, (sequence)(date)(price)(amount)(value)(side1_account_id)(side2_account_id) );

FC_REFLECT_DERIVED( graphene::app::vesting_balance_api_object,
                    (graphene::chain::vesting_balance_master), (balance) );

FC_REFLECT_DERIVED( graphene::app::extended_asset_object, (graphene::chain::asset_object),
                    (total_in_collateral)(total_backing_collateral) );

FC_REFLECT_TYPENAME( graphene::app::account_balance_api_object )
FC_REFLECT_TYPENAME( graphene::app::account_statistics_api_object )
FC_REFLECT_TYPENAME( graphene::app::asset_bitasset_data_api_object )
FC_REFLECT_TYPENAME( graphene::app::asset_dynamic_data_api_object )
FC_REFLECT_TYPENAME( graphene::app::balance_api_object )
FC_REFLECT_TYPENAME( graphene::app::fba_accumulator_api_object )
FC_REFLECT_TYPENAME( graphene::app::limit_order_api_object )
FC_REFLECT_TYPENAME( graphene::app::call_order_api_object )
FC_REFLECT_TYPENAME( graphene::app::force_settlement_api_object )
FC_REFLECT_TYPENAME( graphene::app::collateral_bid_api_object )
FC_REFLECT_TYPENAME( graphene::app::htlc_api_object::transfer_info )
FC_REFLECT_TYPENAME( graphene::app::htlc_api_object )
FC_REFLECT_TYPENAME( graphene::app::dynamic_global_property_api_object )

GRAPHENE_DECLARE_EXTERNAL_SERIALIZATION( graphene::app::account_balance_api_object )
GRAPHENE_DECLARE_EXTERNAL_SERIALIZATION( graphene::app::account_statistics_api_object )
GRAPHENE_DECLARE_EXTERNAL_SERIALIZATION( graphene::app::asset_bitasset_data_api_object )
GRAPHENE_DECLARE_EXTERNAL_SERIALIZATION( graphene::app::asset_dynamic_data_api_object )
GRAPHENE_DECLARE_EXTERNAL_SERIALIZATION( graphene::app::balance_api_object )
GRAPHENE_DECLARE_EXTERNAL_SERIALIZATION( graphene::app::fba_accumulator_api_object )
GRAPHENE_DECLARE_EXTERNAL_SERIALIZATION( graphene::app::limit_order_api_object )
GRAPHENE_DECLARE_EXTERNAL_SERIALIZATION( graphene::app::call_order_api_object )
GRAPHENE_DECLARE_EXTERNAL_SERIALIZATION( graphene::app::force_settlement_api_object )
GRAPHENE_DECLARE_EXTERNAL_SERIALIZATION( graphene::app::collateral_bid_api_object )
GRAPHENE_DECLARE_EXTERNAL_SERIALIZATION( graphene::app::htlc_api_object::transfer_info )
GRAPHENE_DECLARE_EXTERNAL_SERIALIZATION( graphene::app::htlc_api_object )
GRAPHENE_DECLARE_EXTERNAL_SERIALIZATION( graphene::app::vesting_balance_api_object )
GRAPHENE_DECLARE_EXTERNAL_SERIALIZATION( graphene::app::dynamic_global_property_api_object )
