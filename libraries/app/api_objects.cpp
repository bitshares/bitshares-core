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

#include <graphene/app/api_objects.hpp>
#include <graphene/app/util.hpp>

namespace graphene { namespace app {

order::order( const string& _price,
              const string& _quote,
              const string& _base,
              const limit_order_id_type& _id,
              const account_id_type& _oid,
              const string& _oname,
              const time_point_sec& _exp )
: price( _price ),
  quote( _quote ),
  base( _base ),
  id( _id ),
  owner_id( _oid ),
  owner_name( _oname ),
  expiration( _exp )
{
   // Nothing to do
}

order_book::order_book( const string& _base, const string& _quote )
: base( _base ), quote( _quote )
{
   // Do nothing else
}

market_ticker::market_ticker(const market_ticker_object& mto,
                             const fc::time_point_sec& now,
                             const asset_object& asset_base,
                             const asset_object& asset_quote,
                             const order_book& orders)
{
   time = now;
   mto_id = mto.id;
   base = asset_base.symbol;
   quote = asset_quote.symbol;
   percent_change = "0";
   lowest_ask = "0";
   lowest_ask_base_size = "0";
   lowest_ask_quote_size = "0";
   highest_bid = "0";
   highest_bid_base_size = "0";
   highest_bid_quote_size = "0";
   fc::uint128_t bv;
   fc::uint128_t qv;
   price latest_price = asset( mto.latest_base, mto.base ) / asset( mto.latest_quote, mto.quote );
   if( mto.base != asset_base.id )
      latest_price = ~latest_price;
   latest = price_to_string( latest_price, asset_base, asset_quote );
   if( mto.last_day_base != 0 && mto.last_day_quote != 0 // has trade data before 24 hours
       && ( mto.last_day_base != mto.latest_base || mto.last_day_quote != mto.latest_quote ) ) // price changed
   {
      price last_day_price = asset( mto.last_day_base, mto.base ) / asset( mto.last_day_quote, mto.quote );
      if( mto.base != asset_base.id )
         last_day_price = ~last_day_price;
      percent_change = price_diff_percent_string( last_day_price, latest_price );
   }
   if( asset_base.id == mto.base )
   {
      bv = mto.base_volume;
      qv = mto.quote_volume;
   }
   else
   {
      bv = mto.quote_volume;
      qv = mto.base_volume;
   }
   base_volume = uint128_amount_to_string( bv, asset_base.precision );
   quote_volume = uint128_amount_to_string( qv, asset_quote.precision );

   if(!orders.asks.empty())
   {
       lowest_ask = orders.asks[0].price;
       lowest_ask_base_size = orders.asks[0].base;
       lowest_ask_quote_size = orders.asks[0].quote;
   }

   if(!orders.bids.empty())
   {
       highest_bid = orders.bids[0].price;
       highest_bid_base_size = orders.bids[0].base;
       highest_bid_quote_size = orders.bids[0].quote;
   }

}

market_ticker::market_ticker(const fc::time_point_sec& now,
                             const asset_object& asset_base,
                             const asset_object& asset_quote)
{
   time = now;
   base = asset_base.symbol;
   quote = asset_quote.symbol;
   latest = "0";
   lowest_ask = "0";
   lowest_ask_base_size = "0";
   lowest_ask_quote_size = "0";
   highest_bid = "0";
   highest_bid_base_size = "0";
   highest_bid_quote_size = "0";
   percent_change = "0";
   base_volume = "0";
   quote_volume = "0";
}

maybe_signed_block_header::maybe_signed_block_header( const signed_block_header& bh, bool with_witness_signature )
: block_header( bh ), // Slice intentionally
  witness_signature( with_witness_signature ? bh.witness_signature : optional<signature_type>() )
{ // Nothing else to do
}

} } // graphene::app
