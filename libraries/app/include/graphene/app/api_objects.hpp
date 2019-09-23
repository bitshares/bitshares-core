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
      account_object                   account;
      account_statistics_object        statistics;
      string                           registrar_name;
      string                           referrer_name;
      string                           lifetime_referrer_name;
      vector<variant>                  votes;
      optional<vesting_balance_object> cashback_balance;
      vector<account_balance_object>   balances;
      vector<vesting_balance_object>   vesting_balances;
      vector<limit_order_object>       limit_orders;
      vector<call_order_object>        call_orders;
      vector<force_settlement_object>  settle_orders;
      vector<proposal_object>          proposals;
      vector<asset_id_type>            assets;
      vector<withdraw_permission_object> withdraws_from;
      vector<withdraw_permission_object> withdraws_to;
      vector<htlc_object>              htlcs_from;
      vector<htlc_object>              htlcs_to;
      more_data                        more_data_available;
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

FC_REFLECT_DERIVED( graphene::app::extended_asset_object, (graphene::chain::asset_object),
                    (total_in_collateral)(total_backing_collateral) );
