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
#pragma once
#include <graphene/chain/types.hpp>
#include <graphene/chain/config.hpp>

namespace graphene { namespace chain {

   struct asset
   {
      asset( share_type a = 0, asset_id_type id = asset_id_type() )
      :amount(a),asset_id(id){}

      share_type    amount;
      asset_id_type asset_id;

      asset& operator += ( const asset& o )
      {
         FC_ASSERT( asset_id == o.asset_id );
         amount += o.amount;
         return *this;
      }
      asset& operator -= ( const asset& o )
      {
         FC_ASSERT( asset_id == o.asset_id );
         amount -= o.amount;
         return *this;
      }
      asset operator -()const { return asset( -amount, asset_id ); }
      friend bool operator == ( const asset& a, const asset& b )
      {
         return tie(a.asset_id,a.amount) == tie(b.asset_id,b.amount);
      }
      friend bool operator >= ( const asset& a, const asset& b )
      {
         FC_ASSERT( a.asset_id == b.asset_id );
         return a.amount >= b.amount;
      }
      friend bool operator > ( const asset& a, const asset& b )
      {
         FC_ASSERT( a.asset_id == b.asset_id );
         return a.amount > b.amount;
      }
      friend asset operator - ( const asset& a, const asset& b )
      {
         FC_ASSERT( a.asset_id == b.asset_id );
         return asset( a.amount - b.amount, a.asset_id );
      }
      friend asset operator + ( const asset& a, const asset& b )
      {
         FC_ASSERT( a.asset_id == b.asset_id );
         return asset( a.amount + b.amount, a.asset_id );
      }

   };

   /**
    * @brief The price struct stores asset prices in the Graphene system.
    *
    * A price is defined as a ratio between two assets, and represents a possible exchange rate between those two
    * assets. prices are generally not stored in any simplified form, i.e. a price of (1000 CORE)/(20 USD) is perfectly
    * normal.
    *
    * The assets within a price are labeled base and quote. Throughout the Graphene code base, the convention used is
    * that the base asset is the asset being sold, and the quote asset is the asset being purchased, where the price is
    * represented as base/quote, so in the example price above the seller is looking to sell CORE asset and get USD in
    * return.
    */
   struct price
   {
      price(const asset& base = asset(), const asset quote = asset())
         : base(base),quote(quote){}

      asset base;
      asset quote;

      static price max(asset_id_type base, asset_id_type quote );
      static price min(asset_id_type base, asset_id_type quote );

      static price call_price(const asset& debt, const asset& collateral, uint16_t collateral_ratio);

      /// The unit price for an asset type A is defined to be a price such that for any asset m, m*A=m
      static price unit_price(asset_id_type a = asset_id_type()) { return price(asset(1, a), asset(1, a)); }

      price max()const { return price::max( base.asset_id, quote.asset_id ); }
      price min()const { return price::min( base.asset_id, quote.asset_id ); }

      double to_real()const { return double(base.amount.value)/double(quote.amount.value); }

      bool is_null()const;
      void validate()const;
   };

   price operator / ( const asset& base, const asset& quote );
   inline price operator~( const price& p ) { return price{p.quote,p.base}; }

   bool  operator <  ( const asset& a, const asset& b );
   bool  operator <= ( const asset& a, const asset& b );
   bool  operator <  ( const price& a, const price& b );
   bool  operator <= ( const price& a, const price& b );
   bool  operator >  ( const price& a, const price& b );
   bool  operator >= ( const price& a, const price& b );
   bool  operator == ( const price& a, const price& b );
   bool  operator != ( const price& a, const price& b );
   asset operator *  ( const asset& a, const price& b );

   /**
    *  @class price_feed
    *  @brief defines market parameters for shorts and margin positions
    */
   struct price_feed
   {
      /**
       * This is the lowest price at which margin positions will be forced to sell their collateral. This does not
       * directly affect the price at which margin positions will be called; it is only a safety to prevent calls at
       * unreasonable prices.
       */
      price call_limit;
      /**
       * Short orders will only be matched against bids above this price.
       */
      price short_limit;
      /**
       * Forced settlements will evaluate using this price.
       */
      price settlement_price;
      /**
       * Maximum number of seconds margin positions should be able to remain open.
       */
      uint32_t max_margin_period_sec = GRAPHENE_DEFAULT_MARGIN_PERIOD_SEC;

      /**
       *  Required maintenance collateral is defined
       *  as a fixed point number with a maximum value of 10.000
       *  and a minimum value of 1.000.
       *
       *  This value must be greater than required_maintenance_collateral or
       *  a margin call would be triggered immediately.
       *
       *  Default requirement is $2 of collateral per $1 of debt based
       *  upon the premise that both parties to every trade should bring
       *  equal value to the table.
       */
      uint16_t required_initial_collateral = GRAPHENE_DEFAULT_INITIAL_COLLATERAL_RATIO;

      /**
       *  Required maintenance collateral is defined
       *  as a fixed point number with a maximum value of 10.000
       *  and a minimum value of 1.000.
       *
       *  A black swan event occurs when value_of_collateral equals
       *  value_of_debt, to avoid a black swan a margin call is
       *  executed when value_of_debt * required_maintenance_collateral
       *  equals value_of_collateral using rate.
       *
       *  Default requirement is $1.75 of collateral per $1 of debt
       */
      uint16_t required_maintenance_collateral = GRAPHENE_DEFAULT_MAINTENANCE_COLLATERAL_RATIO;

      friend bool operator < ( const price_feed& a, const price_feed& b )
      {
         return std::tie( a.call_limit.base.asset_id, a.call_limit.quote.asset_id ) <
                std::tie( b.call_limit.base.asset_id, b.call_limit.quote.asset_id );
      }

      friend bool operator == ( const price_feed& a, const price_feed& b )
      {
         return std::tie( a.call_limit.base.asset_id, a.call_limit.quote.asset_id ) ==
                std::tie( b.call_limit.base.asset_id, b.call_limit.quote.asset_id );
      }

      void validate() const;
   };

} }

FC_REFLECT( graphene::chain::asset, (amount)(asset_id) )
FC_REFLECT( graphene::chain::price, (base)(quote) )
#define GRAPHENE_PRICE_FEED_FIELDS (call_limit)(short_limit)(settlement_price)(max_margin_period_sec)\
   (required_initial_collateral)(required_maintenance_collateral)
FC_REFLECT( graphene::chain::price_feed, GRAPHENE_PRICE_FEED_FIELDS )
