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
            / ( debt - max_amount_to_sell * match_price )
=>
max_amount_to_sell = (debt * target_CR - collateral * feed_price)
                     / (target_CR * match_price - feed_price)
                   = (debt * tCR / DENOM - collateral * fp_debt_amt / fp_coll_amt)
                     / ( (tCR / DENOM) * (mp_debt_amt / mp_coll_amt) - fp_debt_amt / fp_coll_amt )
                   = (debt * tCR * fp_coll_amt * mp_coll_amt - collateral * fp_debt_amt * DENOM * mp_coll_amt)
                     / ( tCR * mp_debt_amt * fp_coll_amt - fp_debt_amt * DENOM * mp_coll_amt )
max_debt_to_cover = max_amount_to_sell * match_price
                  = max_amount_to_sell * mp_debt_amt / mp_coll_amt
                  = (debt * tCR * fp_coll_amt * mp_debt_amt - collateral * fp_debt_amt * DENOM * mp_debt_amt)
                    / (tCR * mp_debt_amt * fp_coll_amt - fp_debt_amt * DENOM * mp_coll_amt)
*/
pair<asset, asset> call_order_object::get_max_sell_receive_pair( const price& match_price,
                                                                 const price& feed_price,
                                                                 const uint16_t maintenance_collateral_ratio )const
{
   if( !target_collateral_ratio.valid() )
      return make_pair( get_collateral(), get_debt() );

   if( call_price > feed_price ) // feed protected
      return make_pair( asset( 0, collateral_type() ), asset( 0, debt_type() ) );

   uint16_t tcr = std::max( *target_collateral_ratio, maintenance_collateral_ratio );

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

   i256 numerator = fp_coll_amt * mp_debt_amt * debt.value * tcr
                  - fp_debt_amt * mp_debt_amt * collateral.value * GRAPHENE_COLLATERAL_RATIO_DENOM;
   FC_ASSERT( numerator >= 0 );

   i256 denominator = fp_coll_amt * mp_debt_amt * tcr - fp_debt_amt * mp_coll_amt * GRAPHENE_COLLATERAL_RATIO_DENOM;
   FC_ASSERT( denominator > 0 );

   i256 to_cover_i256 = ( numerator / denominator ) + 1;
   if( to_cover_i256 >= debt.value )
      return make_pair( get_collateral(), get_debt() );
   share_type to_cover_amt = static_cast< int64_t >( to_cover_i256 );

   // calculate paying collateral (round up), then re-calculate amount of debt would cover (round down)
   asset to_pay = asset( to_cover_amt, debt_type() ) ^ match_price;
   asset to_cover = to_pay * match_price;

   if( to_cover.amount >= debt || to_pay.amount >= collateral )
      return make_pair( get_collateral(), get_debt() );

   // check collateral ratio after filled
   if( ( get_collateral() / get_debt() ) <= ( to_pay / to_cover ) )
      return make_pair( get_collateral(), get_debt() );

   return make_pair( std::move(to_pay), std::move(to_cover) );
}
