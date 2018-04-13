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
#include <graphene/chain/market_object.hpp>

#include <boost/multiprecision/cpp_int.hpp>

using namespace graphene::chain;

/*
target_CR = max( target_CR, MCR )

target_CR = new_collateral / ( new_debt / feed_price )
          = ( collateral - max_amount_to_sell ) * feed_price
            / ( debt - amount_to_get )
          = ( collateral - max_amount_to_sell ) * feed_price
            / ( debt - round_down(max_amount_to_sell * match_price ) )
          = ( collateral - max_amount_to_sell ) * feed_price
            / ( debt - (max_amount_to_sell * match_price - x) )

Note: x is the fraction, 0 <= x < 1

=>

max_amount_to_sell = ( (debt + x) * target_CR - collateral * feed_price )
                     / (target_CR * match_price - feed_price)
                   = ( (debt + x) * tCR / DENOM - collateral * fp_debt_amt / fp_coll_amt + target_CR * x )
                     / ( (tCR / DENOM) * (mp_debt_amt / mp_coll_amt) - fp_debt_amt / fp_coll_amt )
                   = ( (debt + x) * tCR * fp_coll_amt * mp_coll_amt - collateral * fp_debt_amt * DENOM * mp_coll_amt)
                     / ( tCR * mp_debt_amt * fp_coll_amt - fp_debt_amt * DENOM * mp_coll_amt )

max_debt_to_cover = max_amount_to_sell * match_price
                  = max_amount_to_sell * mp_debt_amt / mp_coll_amt
                  = ( (debt + x) * tCR * fp_coll_amt * mp_debt_amt - collateral * fp_debt_amt * DENOM * mp_debt_amt)
                    / (tCR * mp_debt_amt * fp_coll_amt - fp_debt_amt * DENOM * mp_coll_amt)
*/
pair<asset, asset> call_order_object::get_max_sell_receive_pair( const price& match_price,
                                                                 const price& feed_price,
                                                                 const uint16_t maintenance_collateral_ratio )const
{
   if( call_price > feed_price ) // feed protected
      return make_pair( asset( 0, collateral_type() ), asset( 0, debt_type() ) );

   if( !target_collateral_ratio.valid() ) // target cr is not set
      return make_pair( get_collateral(), get_debt() );

   uint16_t tcr = std::max( *target_collateral_ratio, maintenance_collateral_ratio ); // use mcr if target cr is too small

   typedef boost::multiprecision::int256_t i256;
   i256 mp_debt_amt, mp_coll_amt, fp_debt_amt, fp_coll_amt;

   if( match_price.base.asset_id == call_price.base.asset_id )
   {
      mp_debt_amt = match_price.quote.amount.value;
      mp_coll_amt = match_price.base.amount.value;
   }
   else
   {
      mp_debt_amt = match_price.base.amount.value;
      mp_coll_amt = match_price.quote.amount.value;
   }
   if( feed_price.base.asset_id == call_price.base.asset_id )
   {
      fp_debt_amt = feed_price.quote.amount.value;
      fp_coll_amt = feed_price.base.amount.value;
   }
   else
   {
      fp_debt_amt = feed_price.base.amount.value;
      fp_coll_amt = feed_price.quote.amount.value;
   }

   // firstly we calculate without the fraction (x), the result could be a bit too small
   i256 numerator = fp_coll_amt * mp_debt_amt * debt.value * tcr
                  - fp_debt_amt * mp_debt_amt * collateral.value * GRAPHENE_COLLATERAL_RATIO_DENOM;
   if( numerator < 0 ) // black swan
      return make_pair( get_collateral(), get_debt() );

   i256 denominator = fp_coll_amt * mp_debt_amt * tcr - fp_debt_amt * mp_coll_amt * GRAPHENE_COLLATERAL_RATIO_DENOM;
   if( denominator <= 0 ) // black swan
      return make_pair( get_collateral(), get_debt() );

   i256 to_cover_i256 = ( numerator / denominator ) + 1;
   if( to_cover_i256 >= debt.value ) // avoid possible overflow
      return make_pair( get_collateral(), get_debt() );
   share_type to_cover_amt = static_cast< int64_t >( to_cover_i256 );

   // calculate paying collateral (round up), then re-calculate amount of debt would cover (round down)
   asset to_pay = asset( to_cover_amt, debt_type() ) * match_price;
   asset to_cover = to_pay * match_price;
   to_pay = to_cover ^ match_price;

   if( to_cover.amount >= debt || to_pay.amount >= collateral ) // to be safe
      return make_pair( get_collateral(), get_debt() );

   // check collateral ratio after filled, if it's OK, we return
   price fp = asset( fp_coll_amt, collateral_type() ) / asset( fp_debt_amt, debt_type() );
   price new_call_price = price::call_price( get_debt() - to_cover, get_collateral() - to_pay, tcr );
   if( new_call_price > fp )
      return make_pair( std::move(to_pay), std::move(to_cover) );

   // be here, to_cover is too small due to rounding. deal with the fraction
   numerator += fp_coll_amt * mp_debt_amt * tcr; // plus the fraction
   to_cover_i256 = ( numerator / denominator ) + 1;
   if( to_cover_i256 >= debt.value ) // avoid possible overflow
      to_cover_i256 = debt.value;
   to_cover_amt = static_cast< int64_t >( to_cover_i256 );

   asset max_to_pay = ( to_cover_amt == debt.value ) ? get_collateral() : ( asset( to_cover_amt, debt_type() ) ^ match_price );
   asset max_to_cover = ( to_cover_amt == debt.value ) ? get_debt() : ( max_to_pay * match_price );

   asset min_to_pay = to_pay;
   asset min_to_cover = to_cover;

   // try with binary search to find a good value
   share_type delta_to_pay = max_to_pay.amount - min_to_pay.amount;
   share_type delta_to_cover = max_to_cover.amount - min_to_cover.amount;
   bool delta_to_pay_is_smaller = ( delta_to_pay < delta_to_cover );
   bool max_is_ok = false;
   while( true )
   {
      // get the mean
      if( delta_to_pay_is_smaller )
      {
         to_pay.amount = ( min_to_pay.amount + max_to_pay.amount + 1 ) / 2; // round up; should not overflow
         if( to_pay.amount == max_to_pay.amount )
            to_cover.amount = max_to_cover.amount;
         else
            to_cover = to_pay * match_price;
      }
      else
      {
         to_cover.amount = ( min_to_cover.amount + max_to_cover.amount + 1 ) / 2; // round up; should not overflow
         if( to_cover.amount == max_to_cover.amount )
            to_pay.amount = max_to_pay.amount;
         else
            to_pay = to_cover ^ match_price;
      }

      // check the mean
      if( to_pay.amount == max_to_pay.amount && max_is_ok )
         return make_pair( std::move(to_pay), std::move(to_cover) );

      new_call_price = price::call_price( get_debt() - to_cover, get_collateral() - to_pay, tcr );
      if( new_call_price > fp ) // good
      {
         if( to_pay.amount == max_to_pay.amount )
            return make_pair( std::move(to_pay), std::move(to_cover) );
         max_to_pay.amount = to_pay.amount;
         max_to_cover.amount = to_cover.amount;
         max_is_ok = true;
      }
      else // not good
      {
         if( to_pay.amount == max_to_pay.amount )
            return make_pair( get_collateral(), get_debt() );
         min_to_pay.amount = to_pay.amount;
         min_to_cover.amount = to_cover.amount;
      }
   }

}
