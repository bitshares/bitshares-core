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
                   = ( (debt + x) * tCR / DENOM - collateral * fp_debt_amt / fp_coll_amt )
                     / ( (tCR / DENOM) * (mp_debt_amt / mp_coll_amt) - fp_debt_amt / fp_coll_amt )
                   = ( (debt + x) * tCR * fp_coll_amt * mp_coll_amt - collateral * fp_debt_amt * DENOM * mp_coll_amt)
                     / ( tCR * mp_debt_amt * fp_coll_amt - fp_debt_amt * DENOM * mp_coll_amt )

max_debt_to_cover = max_amount_to_sell * match_price
                  = max_amount_to_sell * mp_debt_amt / mp_coll_amt
                  = ( (debt + x) * tCR * fp_coll_amt * mp_debt_amt - collateral * fp_debt_amt * DENOM * mp_debt_amt)
                    / (tCR * mp_debt_amt * fp_coll_amt - fp_debt_amt * DENOM * mp_coll_amt)
*/
share_type call_order_object::get_max_debt_to_cover( price match_price,
                                                     price feed_price,
                                                     const uint16_t maintenance_collateral_ratio )const
{ try {
   // be defensive here, make sure feed_price is in collateral / debt format
   if( feed_price.base.asset_id != call_price.base.asset_id )
      feed_price = ~feed_price;

   FC_ASSERT( feed_price.base.asset_id == call_price.base.asset_id
              && feed_price.quote.asset_id == call_price.quote.asset_id );

   if( call_price > feed_price ) // feed protected. be defensive here, although this should be guaranteed by caller
      return 0;

   if( !target_collateral_ratio.valid() ) // target cr is not set
      return debt;

   uint16_t tcr = std::max( *target_collateral_ratio, maintenance_collateral_ratio ); // use mcr if target cr is too small

   // be defensive here, make sure match_price is in collateral / debt format
   if( match_price.base.asset_id != call_price.base.asset_id )
      match_price = ~match_price;

   FC_ASSERT( match_price.base.asset_id == call_price.base.asset_id
              && match_price.quote.asset_id == call_price.quote.asset_id );

   typedef boost::multiprecision::int256_t i256;
   i256 mp_debt_amt = match_price.quote.amount.value;
   i256 mp_coll_amt = match_price.base.amount.value;
   i256 fp_debt_amt = feed_price.quote.amount.value;
   i256 fp_coll_amt = feed_price.base.amount.value;

   // firstly we calculate without the fraction (x), the result could be a bit too small
   i256 numerator = fp_coll_amt * mp_debt_amt * debt.value * tcr
                  - fp_debt_amt * mp_debt_amt * collateral.value * GRAPHENE_COLLATERAL_RATIO_DENOM;
   if( numerator < 0 ) // feed protected, actually should not be true here, just check to be safe
      return 0;

   i256 denominator = fp_coll_amt * mp_debt_amt * tcr - fp_debt_amt * mp_coll_amt * GRAPHENE_COLLATERAL_RATIO_DENOM;
   if( denominator <= 0 ) // black swan
      return debt;

   // note: if add 1 here, will result in 1.5x imperfection rate;
   //       however, due to rounding, the result could still be a bit too big, thus imperfect.
   i256 to_cover_i256 = ( numerator / denominator );
   if( to_cover_i256 >= debt.value ) // avoid possible overflow
      return debt;
   share_type to_cover_amt = static_cast< int64_t >( to_cover_i256 );

   // stabilize
   // note: rounding up-down results in 3x imperfection rate in comparison to down-down-up
   asset to_pay = asset( to_cover_amt, debt_type() ) * match_price;
   asset to_cover = to_pay * match_price;
   to_pay = to_cover.multiply_and_round_up( match_price );

   if( to_cover.amount >= debt || to_pay.amount >= collateral ) // to be safe
      return debt;
   FC_ASSERT( to_pay.amount < collateral && to_cover.amount < debt );

   // check collateral ratio after filled, if it's OK, we return
   price new_call_price = price::call_price( get_debt() - to_cover, get_collateral() - to_pay, tcr );
   if( new_call_price > feed_price )
      return to_cover.amount;

   // be here, to_cover is too small due to rounding. deal with the fraction
   numerator += fp_coll_amt * mp_debt_amt * tcr; // plus the fraction
   to_cover_i256 = ( numerator / denominator ) + 1;
   if( to_cover_i256 >= debt.value ) // avoid possible overflow
      to_cover_i256 = debt.value;
   to_cover_amt = static_cast< int64_t >( to_cover_i256 );

   asset max_to_pay = ( ( to_cover_amt == debt.value ) ? get_collateral()
                        : asset( to_cover_amt, debt_type() ).multiply_and_round_up( match_price ) );
   if( max_to_pay.amount > collateral )
      max_to_pay.amount = collateral;

   asset max_to_cover = ( ( max_to_pay.amount == collateral ) ? get_debt() : ( max_to_pay * match_price ) );
   if( max_to_cover.amount >= debt ) // to be safe
   {
      max_to_pay.amount = collateral;
      max_to_cover.amount = debt;
   }

   if( max_to_pay <= to_pay || max_to_cover <= to_cover ) // strange data. should skip binary search and go on, but doesn't help much
      return debt;
   FC_ASSERT( max_to_pay > to_pay && max_to_cover > to_cover );

   asset min_to_pay = to_pay;
   asset min_to_cover = to_cover;

   // try with binary search to find a good value
   // note: actually binary search can not always provide perfect result here,
   //       due to rounding, collateral ratio is not always increasing while to_pay or to_cover is increasing
   bool max_is_ok = false;
   while( true )
   {
      // get the mean
      if( match_price.base.amount < match_price.quote.amount ) // step of collateral is smaller
      {
         to_pay.amount = ( min_to_pay.amount + max_to_pay.amount + 1 ) / 2; // should not overflow. round up here
         if( to_pay.amount == max_to_pay.amount )
            to_cover.amount = max_to_cover.amount;
         else
         {
            to_cover = to_pay * match_price;
            if( to_cover.amount >= max_to_cover.amount ) // can be true when max_is_ok is false
            {
               to_pay.amount = max_to_pay.amount;
               to_cover.amount = max_to_cover.amount;
            }
            else
            {
               to_pay = to_cover.multiply_and_round_up( match_price ); // stabilization, no change or become smaller
               FC_ASSERT( to_pay.amount < max_to_pay.amount );
            }
         }
      }
      else // step of debt is smaller or equal
      {
         to_cover.amount = ( min_to_cover.amount + max_to_cover.amount ) / 2; // should not overflow. round down here
         if( to_cover.amount == max_to_cover.amount )
            to_pay.amount = max_to_pay.amount;
         else
         {
            to_pay = to_cover.multiply_and_round_up( match_price );
            if( to_pay.amount >= max_to_pay.amount ) // can be true when max_is_ok is false
            {
               to_pay.amount = max_to_pay.amount;
               to_cover.amount = max_to_cover.amount;
            }
            else
            {
               to_cover = to_pay * match_price; // stabilization, to_cover should have increased
               if( to_cover.amount >= max_to_cover.amount ) // to be safe
               {
                  to_pay.amount = max_to_pay.amount;
                  to_cover.amount = max_to_cover.amount;
               }
            }
         }
      }

      // check again to see if we've moved away from the minimums, if not, use the maximums directly
      if( to_pay.amount <= min_to_pay.amount || to_cover.amount <= min_to_cover.amount
            || to_pay.amount > max_to_pay.amount || to_cover.amount > max_to_cover.amount )
      {
         to_pay.amount = max_to_pay.amount;
         to_cover.amount = max_to_cover.amount;
      }

      // check the mean
      if( to_pay.amount == max_to_pay.amount && ( max_is_ok || to_pay.amount == collateral ) )
         return to_cover.amount;
      FC_ASSERT( to_pay.amount < collateral && to_cover.amount < debt );

      new_call_price = price::call_price( get_debt() - to_cover, get_collateral() - to_pay, tcr );
      if( new_call_price > feed_price ) // good
      {
         if( to_pay.amount == max_to_pay.amount )
            return to_cover.amount;
         max_to_pay.amount = to_pay.amount;
         max_to_cover.amount = to_cover.amount;
         max_is_ok = true;
      }
      else // not good
      {
         if( to_pay.amount == max_to_pay.amount )
            break;
         min_to_pay.amount = to_pay.amount;
         min_to_cover.amount = to_cover.amount;
      }
   }

   // be here, max_to_cover is too small due to rounding. search forward
   for( uint64_t d1 = 0, d2 = 1, d3 = 1; ; d1 = d2, d2 = d3, d3 = d1 + d2 ) // 1,1,2,3,5,8,...
   {
      if( match_price.base.amount > match_price.quote.amount ) // step of debt is smaller
      {
         to_pay.amount += d2;
         if( to_pay.amount >= collateral )
            return debt;
         to_cover = to_pay * match_price;
         if( to_cover.amount >= debt )
            return debt;
         to_pay = to_cover.multiply_and_round_up( match_price ); // stabilization
         if( to_pay.amount >= collateral )
            return debt;
      }
      else // step of collateral is smaller or equal
      {
         to_cover.amount += d2;
         if( to_cover.amount >= debt )
            return debt;
         to_pay = to_cover.multiply_and_round_up( match_price );
         if( to_pay.amount >= collateral )
            return debt;
         to_cover = to_pay * match_price; // stabilization
         if( to_cover.amount >= debt )
            return debt;
      }

      // check
      FC_ASSERT( to_pay.amount < collateral && to_cover.amount < debt );

      new_call_price = price::call_price( get_debt() - to_cover, get_collateral() - to_pay, tcr );
      if( new_call_price > feed_price ) // good
         return to_cover.amount;
   }

} FC_CAPTURE_AND_RETHROW( (*this)(feed_price)(match_price)(maintenance_collateral_ratio) ) }
