/*
 * Copyright (c) 2018 Abit More, and contributors.
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

#include <graphene/app/util.hpp>


namespace graphene { namespace app {

u256 to256( const fc::uint128& t )
{
   u256 v(t.hi);
   v <<= 64;
   v += t.lo;
   return v;
}

fc::uint128 to_capped128( const u256& t )
{
   static u256 max128 = to256( fc::uint128::max_value() );
   if( t >= max128 )
      return fc::uint128::max_value();
   fc::uint128 result;
   u256 hi(t);
   hi >>= 64;
   result.hi = static_cast< uint64_t >( hi );
   u256 lo(t);
   hi <<= 64;
   lo -= hi;
   result.lo = static_cast< uint64_t >( lo );
   return result;
}

string uint128_amount_to_string( const fc::uint128& amount, const uint8_t precision )
{ try {
   string s = string( amount );
   if( precision == 0 || amount == fc::uint128() )
      return s;

   std::stringstream ss;
   uint8_t pos = s.find_last_not_of( '0' ); // should be >= 0
   uint8_t len = s.size();
   if( len > precision )
   {
      uint8_t left_len = len - precision;
      ss << s.substr( 0, left_len );
      if( pos >= left_len )
         ss << '.' << s.substr( left_len, pos - left_len + 1 );
   }
   else
   {
      ss << "0.";
      for( uint8_t i = precision - len; i > 0; --i )
         ss << '0';
      ss << s.substr( 0, pos + 1 );
   }
   return ss.str();
} FC_CAPTURE_AND_RETHROW( (amount)(precision) ) }

string price_to_string( const price& _price, const uint8_t base_precision, const uint8_t quote_precision )
{ try {
   if( _price.base.amount == 0 )
      return "0";
   FC_ASSERT( _price.base.amount >= 0 );
   FC_ASSERT( _price.quote.amount >= 0 );
   FC_ASSERT( base_precision <= 19 );
   FC_ASSERT( quote_precision <= 19 );
   price new_price = _price;
   if( new_price.quote.amount == 0 )
   {
      new_price.base.amount = std::numeric_limits<int64_t>::max();
      new_price.quote.amount = 1;
   }

   // times (10**19) so won't overflow but have good accuracy
   fc::uint128 price128 = fc::uint128( new_price.base.amount.value ) * uint64_t(10000000000000000000ULL)
                                                                     / new_price.quote.amount.value;

   return uint128_amount_to_string( price128, 19 + base_precision - quote_precision );
} FC_CAPTURE_AND_RETHROW( (_price)(base_precision)(quote_precision) ) }

string price_diff_percent_string( const price& old_price, const price& new_price )
{ try {
   FC_ASSERT( old_price.base.asset_id == new_price.base.asset_id );
   FC_ASSERT( old_price.quote.asset_id == new_price.quote.asset_id );
   FC_ASSERT( old_price.base.amount >= 0 );
   FC_ASSERT( old_price.quote.amount >= 0 );
   FC_ASSERT( new_price.base.amount >= 0 );
   FC_ASSERT( new_price.quote.amount >= 0 );
   price old_price1 = old_price;
   if( old_price.base.amount == 0 )
   {
      old_price1.base.amount = 1;
      old_price1.quote.amount = std::numeric_limits<int64_t>::max();
   }
   else if( old_price.quote.amount == 0 )
   {
      old_price1.base.amount = std::numeric_limits<int64_t>::max();
      old_price1.quote.amount = 1;
   }
   price new_price1 = new_price;
   if( new_price.base.amount == 0 )
   {
      new_price1.base.amount = 1;
      new_price1.quote.amount = std::numeric_limits<int64_t>::max();
   }
   else if( new_price.quote.amount == 0 )
   {
      new_price1.base.amount = std::numeric_limits<int64_t>::max();
      new_price1.quote.amount = 1;
   }

   // change = new/old - 1 = (new_base/new_quote)/(old_base/old_quote) - 1
   //        = (new_base * old_quote) / (new_quote * old_base) - 1
   //        = (new_base * old_quote - new_quote * old_base) / (new_quote * old_base)
   fc::uint128 new128 = fc::uint128( new_price1.base.amount.value ) * old_price1.quote.amount.value;
   fc::uint128 old128 = fc::uint128( old_price1.base.amount.value ) * new_price1.quote.amount.value;
   bool non_negative = (new128 >= old128);
   fc::uint128 diff128;
   if( non_negative )
      diff128 = new128 - old128;
   else
      diff128 = old128 - new128;
   static fc::uint128 max = fc::uint128::max_value() / 10000;
   if( diff128 <= max )
      diff128 = diff128 * 10000 / old128;
   else
   {
      u256 diff256 = to256( diff128 );
      diff256 *= 10000;
      diff256 /= to256( old128 );
      diff128 = to_capped128( diff256 );
   }
   string diff_str = uint128_amount_to_string( diff128, 2 ); // at most 2 decimal digits
   if( non_negative || diff_str == "0" )
      return diff_str;
   else
      return "-" + diff_str;
} FC_CAPTURE_AND_RETHROW( (old_price)(new_price) ) }

} } // graphene::app
