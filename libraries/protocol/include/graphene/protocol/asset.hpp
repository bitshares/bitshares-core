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
#include <graphene/protocol/types.hpp>

namespace graphene { namespace protocol {

   extern const int64_t scaled_precision_lut[];

   struct price;

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
         return std::tie( a.asset_id, a.amount ) == std::tie( b.asset_id, b.amount );
      }
      friend bool operator < ( const asset& a, const asset& b )
      {
         FC_ASSERT( a.asset_id == b.asset_id );
         return a.amount < b.amount;
      }
      friend inline bool operator <= ( const asset& a, const asset& b )
      {
         return !(b < a);
      }

      friend inline bool operator != ( const asset& a, const asset& b )
      {
         return !(a == b);
      }
      friend inline bool operator > ( const asset& a, const asset& b )
      {
         return (b < a);
      }
      friend inline bool operator >= ( const asset& a, const asset& b )
      {
         return !(a < b);
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

      static share_type scaled_precision( uint8_t precision )
      {
         FC_ASSERT( precision < 19 );
         return scaled_precision_lut[ precision ];
      }

      asset multiply_and_round_up( const price& p )const; ///< Multiply and round up
   };

   /**
    * @brief The price struct stores asset prices in the BitShares system.
    *
    * A price is defined as a ratio between two assets, and represents a possible exchange rate between those two
    * assets. prices are generally not stored in any simplified form, i.e. a price of (1000 CORE)/(20 USD) is perfectly
    * normal.
    *
    * The assets within a price are labeled base and quote. Throughout the BitShares code base, the convention used is
    * that the base asset is the asset being sold, and the quote asset is the asset being purchased, where the price is
    * represented as base/quote, so in the example price above the seller is looking to sell CORE asset and get USD in
    * return.
    */
   struct price
   {
      explicit price(const asset& _base = asset(), const asset& _quote = asset())
         : base(_base),quote(_quote){}

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
      /// @brief Check if the object is valid
      /// @param check_upper_bound Whether to check if the amounts in the price are too large
      void validate( bool check_upper_bound = false )const;
   };

   price operator / ( const asset& base, const asset& quote );
   inline price operator~( const price& p ) { return price{p.quote,p.base}; }

   bool  operator <  ( const price& a, const price& b );
   bool  operator == ( const price& a, const price& b );

   inline bool  operator >  ( const price& a, const price& b ) { return (b < a); }
   inline bool  operator <= ( const price& a, const price& b ) { return !(b < a); }
   inline bool  operator >= ( const price& a, const price& b ) { return !(a < b); }
   inline bool  operator != ( const price& a, const price& b ) { return !(a == b); }

   asset operator *  ( const asset& a, const price& b ); ///< Multiply and round down

   price operator *  ( const price& p, const ratio_type& r );
   price operator /  ( const price& p, const ratio_type& r );

   inline price& operator *=  ( price& p, const ratio_type& r )
   { return p = p * r; }
   inline price& operator /=  ( price& p, const ratio_type& r )
   { return p = p / r; }

   /**
    *  @class price_feed
    *  @brief defines market parameters for margin positions
    */
   struct price_feed
   {
      /**
       *  Required maintenance collateral is defined
       *  as a fixed point number with a maximum value of 10.000
       *  and a minimum value of 1.000.  (denominated in GRAPHENE_COLLATERAL_RATIO_DENOM)
       *
       *  A black swan event occurs when value_of_collateral equals
       *  value_of_debt * MSSR.  To avoid a black swan a margin call is
       *  executed when value_of_debt * required_maintenance_collateral
       *  equals value_of_collateral using rate.
       *
       *  Default requirement is $1.75 of collateral per $1 of debt
       *
       *  BlackSwan ---> SQR ---> MCR ----> SP
       */
      ///@{
      /**
       * Forced settlements will evaluate using this price, defined as BITASSET / COLLATERAL
       */
      price settlement_price;

      /// Price at which automatically exchanging this asset for CORE from fee pool occurs (used for paying fees)
      price core_exchange_rate;

      /** Fixed point between 1.000 and 10.000, implied fixed point denominator is GRAPHENE_COLLATERAL_RATIO_DENOM */
      uint16_t maintenance_collateral_ratio = GRAPHENE_DEFAULT_MAINTENANCE_COLLATERAL_RATIO;

      /** Fixed point between 1.000 and 10.000, implied fixed point denominator is GRAPHENE_COLLATERAL_RATIO_DENOM */
      uint16_t maximum_short_squeeze_ratio = GRAPHENE_DEFAULT_MAX_SHORT_SQUEEZE_RATIO;

      /**
       * This is the price at which a call order will relinquish COLLATERAL when margin called. It is
       * also the price that establishes the minimum amount of collateral per debt that call orders must
       * maintain to avoid possibility of black swan.  A call order maintaining less collateral per debt
       * than this price is unable to meet the combined obligation to sell collateral at the Margin Call
       * Offer Price (MCOP) *AND* to pay the margin call fee. The MSSP is related to the MCOP, but the
       * MSSP accounts for the need to reserve extra collateral to pay the margin call fee, whereas the
       * MCOP only accounts for the collateral to be traded to the call buyer.  Prior to the
       * introduction of the Margin Call Fee Ratio (MCFR) with BSIP-74, the two prices (MSSP and MCOP)
       * were identical, and MSSP could be thought of as "the price at which you are forced to sell
       * collateral if margin called," but this latter concept is now embodied by the MCOP.
       *
       * The Maximum Short Squeeze Price is computed as follows, in units of DEBT per COLLATERAL:
       *
       *   MSSP = settlement_price / MSSR;
       *
       * @return The MSSP in units of DEBT per COLLATERAL.
       */
      price max_short_squeeze_price()const;
      /**
       * Older implementation of max_short_squeeze_price() due to hardfork changes. It came with
       * the following commentary:
       *
       * When selling collateral to pay off debt, the least amount of debt to receive should be
       *  min_usd = max_short_squeeze_price() * collateral
       *
       *  This is provided to ensure that a black swan cannot be trigged due to poor liquidity alone, it
       *  must be confirmed by having the max_short_squeeze_price() move below the black swan price.
       * @returns the Maximum Short Squeeze price for this asset
       */
      price max_short_squeeze_price_before_hf_1270()const;

      /**
       * Compute price at which margin calls offer to sell collateral.
       *
       * Margin calls offer a greater amount of COLLATERAL asset to the market to buy back DEBT
       * asset than would otherwise be required in a fair exchange at the settlement_price.
       * (I.e. they sell collateral "cheaper" than its price feed value.) This is done to attract a
       * quick buyer of the call in order to preserve healthy collateralization of the DEBT asset
       * overall.  The price at which the call is offered, in comparison to the settlement price, is
       * determined by the Maximum Short Squeeze Ratio (MSSR) and the Margin Call Fee Ratio (MCFR)
       * as follows, in units of DEBT per COLLATERAL:
       *
       *   MCOP = settlement_price / (MSSR - MCFR);
       *
       * Compare with Maximum Short Squeeze Price (MSSP), which is computed as follows:
       *
       *   MSSP = settlement_price / MSSR;
       *
       * Since BSIP-74, we distinguish between Maximum Short Squeeze Price (MSSP) and Margin Call
       * Order Price (MCOP). Margin calls previously offered collateral at the MSSP, but now they
       * offer slightly less collateral per debt if Margin Call Fee Ratio (MCFR) is set, because
       * the call order must reserve some collateral to pay the fee.  We must still retain the
       * concept of MSSP, as it communicates the minimum collateralization before black swan may be
       * triggered, but we add this new method to calculate MCOP.
       *
       * Note that when we calculate the MCOP, we enact a price floor to ensure the margin call never
       * offers LESS collateral than the DEBT is worth. As such, it's important to calculate the
       * realized fee, when trading at the offer price, as a delta between total relinquished collateral
       * (DEBT*MSSP) and collateral sold to the buyer (DEBT*MCOP).  If you instead try to calculate the
       * fee by direct multiplication of MCFR, you will get the wrong answer if the price was
       * floored. (Fee is truncated when price is floored.)
       *
       * @param margin_call_fee_ratio MCFR value currently in effect. If zero or unset, returns
       *    same result as @ref max_short_squeeze_price().
       *
       * @return The MCOP in units of DEBT per COLLATERAL.
       */
      price margin_call_order_price(const fc::optional<uint16_t> margin_call_fee_ratio)const;

      /**
       * Ratio between max_short_squeeze_price and margin_call_order_price.
       *
       * This ratio, if it multiplied margin_call_order_price (expressed in DEBT/COLLATERAL), would
       * yield the max_short_squeeze_price, apart perhaps for truncation (rounding) error.
       *
       * When a margin call is taker, matching an existing order on the books, it is possible the call
       * gets a better realized price than the order price that it offered at.  In this case, the margin
       * call fee is proportionaly reduced. This ratio is used to calculate the price at which the call
       * relinquishes collateral (to meet both trade and fee obligations) based on actual realized match
       * price.
       *
       * This function enacts the same flooring as margin_call_order_price() (MSSR - MCFR is floored at
       * 1.00).  This ensures we apply the same fee truncation in the taker case as the maker case.
       *
       * @return (MSSR - MCFR) / MSSR
       */
      ratio_type margin_call_pays_ratio(const fc::optional<uint16_t> margin_call_fee_ratio)const;

      /// Call orders with collateralization (aka collateral/debt) not greater than this value are in margin call
      /// territory.
      /// Calculation: ~settlement_price * maintenance_collateral_ratio / GRAPHENE_COLLATERAL_RATIO_DENOM
      price maintenance_collateralization()const;

      /// Whether the parameters that affect margin calls in this price feed object are the same as the parameters
      /// in the passed-in object
      bool margin_call_params_equal( const price_feed& b ) const
      {
         if( this == &b )
            return true;
         return std::tie(   settlement_price,   maintenance_collateral_ratio,   maximum_short_squeeze_ratio ) ==
                std::tie( b.settlement_price, b.maintenance_collateral_ratio, b.maximum_short_squeeze_ratio );
      }
      ///@}

      void validate() const;
      bool is_for( asset_id_type asset_id ) const;
   };

} }

FC_REFLECT( graphene::protocol::asset, (amount)(asset_id) )
FC_REFLECT( graphene::protocol::price, (base)(quote) )

FC_REFLECT( graphene::protocol::price_feed,
            (settlement_price)(maintenance_collateral_ratio)(maximum_short_squeeze_ratio)(core_exchange_rate) )

GRAPHENE_DECLARE_EXTERNAL_SERIALIZATION( graphene::protocol::asset )
GRAPHENE_DECLARE_EXTERNAL_SERIALIZATION( graphene::protocol::price )
GRAPHENE_DECLARE_EXTERNAL_SERIALIZATION( graphene::protocol::price_feed )
