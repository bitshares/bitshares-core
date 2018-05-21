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

#include <random>
#include <boost/test/unit_test.hpp>
#include <boost/multiprecision/cpp_int.hpp>

#include <graphene/chain/market_object.hpp>

#include "../common/database_fixture.hpp"

using namespace graphene::chain;
using namespace graphene::chain::test;

BOOST_FIXTURE_TEST_SUITE( call_order_tests, database_fixture )

BOOST_AUTO_TEST_CASE( call_order_object_test )
{ try {
   // assume GRAPHENE_COLLATERAL_RATIO_DENOM is 1000 in this test case
   BOOST_REQUIRE_EQUAL( 1000, GRAPHENE_COLLATERAL_RATIO_DENOM );

   // function to create a new call_order_object
   auto new_call_obj = []( const share_type c, const share_type d, int16_t mcr, optional<uint16_t> tcr = {} ) {
      call_order_object o;
      o.collateral = c;
      o.debt = d;
      o.call_price = price::call_price( asset( d, asset_id_type(1)), asset(c) , mcr );
      o.target_collateral_ratio = tcr;
      return o;
   };

   // function to validate result of call_order_object::get_max_debt_to_cover(...)
   auto validate_result = []( const call_order_object& o, const price& match_price, const price& feed_price,
                              int16_t mcr, const share_type result, bool print_log = true ) {
      if( result == 0 )
         return 1;

      BOOST_REQUIRE_GT( result.value, 0 );
      BOOST_REQUIRE_LE( result.value, o.debt.value );

      BOOST_REQUIRE( match_price.base.asset_id == o.collateral_type() );
      BOOST_REQUIRE( match_price.quote.asset_id == o.debt_type() );
      BOOST_REQUIRE( feed_price.base.asset_id == o.collateral_type() );
      BOOST_REQUIRE( feed_price.quote.asset_id == o.debt_type() );

      // should be in margin call territory
      price call_price = price::call_price( o.get_debt(), o.get_collateral(), mcr );
      BOOST_CHECK( call_price <= feed_price );

      if( !o.target_collateral_ratio.valid() )
      {
         BOOST_CHECK_EQUAL( result.value, o.debt.value );
         return 2;
      }

      auto tcr = *o.target_collateral_ratio;
      if( tcr == 0 )
         tcr = 1;

      asset to_cover( result, o.debt_type() );
      asset to_pay = o.get_collateral();
      if( result < o.debt )
      {
         to_pay = to_cover.multiply_and_round_up( match_price );
         BOOST_CHECK_LT( to_pay.amount.value, o.collateral.value ); // should cover more on black swan event
         BOOST_CHECK_EQUAL( result.value, (to_pay * match_price).amount.value ); // should not change after rounded down debt to cover

         // should have target_cr set
         // after sold some collateral, the collateral ratio will be higher than expected
         price new_tcr_call_price = price::call_price( o.get_debt() - to_cover, o.get_collateral() - to_pay, tcr );
         price new_mcr_call_price = price::call_price( o.get_debt() - to_cover, o.get_collateral() - to_pay, mcr );
         BOOST_CHECK( new_tcr_call_price > feed_price );
         BOOST_CHECK( new_mcr_call_price > feed_price );
      }

      // if sell less than calculated, the collateral ratio will not be higher than expected
      int j = 3;
      for( int i = 100000; i >= 10; i /= 10, ++j )
      {
         int total_passes = 3;
         for( int k = 1; k <= total_passes; ++k )
         {
            bool last_check = (k == total_passes);
            asset sell_less = to_pay;
            asset cover_less;
            for( int m = 0; m < k; ++m )
            {
               if( i == 100000 )
                  sell_less.amount -= 1;
               else
                  sell_less.amount -= ( ( sell_less.amount + i - 1 ) / i );
               cover_less = sell_less * match_price; // round down debt to cover
               if( cover_less >= to_cover )
               {
                  cover_less.amount = to_cover.amount - 1;
                  sell_less = cover_less * match_price; // round down collateral
                  cover_less = sell_less * match_price; // round down debt to cover
               }
               sell_less = cover_less.multiply_and_round_up( match_price ); // round up to get collateral to sell
               if( sell_less.amount <= 0 || cover_less.amount <= 0 ) // unable to sell or cover less, we return
               {
                  if( to_pay.amount == o.collateral )
                     return j;
                  return (j + 10);
               }
            }
            BOOST_REQUIRE_LT( cover_less.amount.value, o.debt.value );
            BOOST_REQUIRE_LT( sell_less.amount.value, o.collateral.value );
            price tmp_tcr_call_price = price::call_price( o.get_debt() - cover_less, o.get_collateral() - sell_less, tcr );
            price tmp_mcr_call_price = price::call_price( o.get_debt() - cover_less, o.get_collateral() - sell_less, mcr );
            bool cover_less_is_enough = ( tmp_tcr_call_price > feed_price && tmp_mcr_call_price > feed_price );
            if( !cover_less_is_enough )
            {
               if( !last_check )
                  continue;
               if( to_pay.amount == o.collateral )
                  return j;
               return (j + 10);
            }
            if( print_log )
            {
               print_log = false;
               wlog( "Impefect result >= 1 / ${i}", ("i",i) );
               wdump( (o)(match_price)(feed_price)(mcr)(result)(sell_less)(cover_less)(tmp_mcr_call_price)(tmp_tcr_call_price) );
            }
            break;
         }
      }
      if( to_pay.amount == o.collateral )
         return j;
      return (j + 10);
   };

   // init
   int16_t mcr = 1750;
   price mp, fp;
   call_order_object obj;
   int64_t expected;
   share_type result;

   mp = price( asset(1100), asset(1000, asset_id_type(1)) ); // match_price
   fp = price( asset(1000), asset(1000, asset_id_type(1)) ); // feed_price

   // fixed tests
   obj = new_call_obj( 1751, 1000, mcr ); // order is not in margin call territory
   expected = 0;
   result = obj.get_max_debt_to_cover( mp, fp, mcr );
   BOOST_CHECK_EQUAL( result.value, expected );
   validate_result( obj, mp, fp, mcr, result );

   obj = new_call_obj( 1751, 1000, mcr, 10000 ); // order is not in margin call territory
   expected = 0;
   result = obj.get_max_debt_to_cover( mp, fp, mcr );
   BOOST_CHECK_EQUAL( result.value, expected );
   validate_result( obj, mp, fp, mcr, result );

   obj = new_call_obj( 160, 100, mcr ); // target_cr is not set
   expected = 100;
   result = obj.get_max_debt_to_cover( mp, fp, mcr );
   BOOST_CHECK_EQUAL( result.value, expected );
   validate_result( obj, mp, fp, mcr, result );

   obj = new_call_obj( 1009, 1000, mcr, 200 ); // target_cr set, but order is in black swan territory
   expected = 1000;
   result = obj.get_max_debt_to_cover( mp, fp, mcr );
   BOOST_CHECK_EQUAL( result.value, expected );
   validate_result( obj, mp, fp, mcr, result );

   obj = new_call_obj( 1499,  999, mcr, 1600 ); // target_cr is 160%, less than 175%, so use 175%
   expected = 385;
   result = obj.get_max_debt_to_cover( mp, fp, mcr );
   BOOST_CHECK_EQUAL( result.value, expected );
   validate_result( obj, mp, fp, mcr, result );

   obj = new_call_obj( 1500, 1000, mcr, 1800 ); // target_cr is 180%
   expected = 429;
   result = obj.get_max_debt_to_cover( mp, fp, mcr );
   BOOST_CHECK_EQUAL( result.value, expected );
   validate_result( obj, mp, fp, mcr, result );

   obj = new_call_obj( 1501, 1001, mcr, 2000 ); // target_cr is 200%
   expected = 558;
   result = obj.get_max_debt_to_cover( mp, fp, mcr );
   BOOST_CHECK_EQUAL( result.value, expected );
   validate_result( obj, mp, fp, mcr, result );

   obj = new_call_obj( 1502, 1002, mcr, 3000 ); // target_cr is 300%
   expected = 793;
   result = obj.get_max_debt_to_cover( mp, fp, mcr );
   BOOST_CHECK_EQUAL( result.value, expected );
   validate_result( obj, mp, fp, mcr, result );

   mcr = 1750;
   mp = price( asset(40009), asset(79070, asset_id_type(1)) ); // match_price
   fp = price( asset(40009), asset(86977, asset_id_type(1)) ); // feed_price

   obj = new_call_obj( 557197, 701502, mcr, 1700 ); // target_cr is less than mcr
   result = obj.get_max_debt_to_cover( mp, fp, mcr );
   validate_result( obj, mp, fp, mcr, result );

   mcr = 1455;
   mp = price( asset(1150171), asset(985450, asset_id_type(1)) ); // match_price
   fp = price( asset(418244), asset(394180, asset_id_type(1)) ); // feed_price

   obj = new_call_obj( 423536, 302688, mcr, 200 ); // target_cr is less than mcr
   result = obj.get_max_debt_to_cover( mp, fp, mcr );
   validate_result( obj, mp, fp, mcr, result );

   // random tests
   std::mt19937_64 gen( time(NULL) );
   std::uniform_int_distribution<int64_t> amt_uid(1, GRAPHENE_MAX_SHARE_SUPPLY);
   std::uniform_int_distribution<int64_t> amt_uid2(1, 1000*1000*1000);
   std::uniform_int_distribution<int64_t> amt_uid3(1, 1000*1000);
   std::uniform_int_distribution<int64_t> amt_uid4(1, 300);
   std::uniform_int_distribution<int64_t> mp_num_uid(800, 1100);
   std::uniform_int_distribution<int16_t> mcr_uid(1001, 32767);
   std::uniform_int_distribution<int16_t> mcr_uid2(1001, 3000);
   std::uniform_int_distribution<uint16_t> tcr_uid(0, 65535);
   std::uniform_int_distribution<uint16_t> tcr_uid2(0, 3000);

   vector<int> count(20,0);
   int total = 500*1000;
   for( int i = total; i > 0; --i )
   {
      if( i % 9 == 0 )
         mcr = 1002;
      else if( i % 3 == 0 )
         mcr = 1750;
      else if( i % 3 == 1 )
         mcr = mcr_uid(gen);
      else // if( i % 3 == 2 )
         mcr = mcr_uid2(gen);

      // call_object
      if( i % 17 <= 0 )
         obj = new_call_obj( amt_uid(gen), amt_uid(gen), mcr, tcr_uid(gen) );
      else if( i % 17 <= 2 )
         obj = new_call_obj( amt_uid2(gen), amt_uid2(gen), mcr, tcr_uid(gen) );
      else if( i % 17 <= 3 )
         obj = new_call_obj( amt_uid3(gen), amt_uid3(gen), mcr, tcr_uid(gen) );
      else if( i % 17 <= 4 )
         obj = new_call_obj( amt_uid4(gen), amt_uid4(gen), mcr, tcr_uid(gen) );
      else if( i % 17 <= 5 )
         obj = new_call_obj( amt_uid(gen), amt_uid(gen), mcr, tcr_uid2(gen) );
      else if( i % 17 <= 7 )
         obj = new_call_obj( amt_uid2(gen), amt_uid2(gen), mcr, tcr_uid2(gen) );
      else if( i % 17 <= 8 )
         obj = new_call_obj( amt_uid3(gen), amt_uid3(gen), mcr, tcr_uid2(gen) );
      else if( i % 17 <= 9 )
         obj = new_call_obj( amt_uid4(gen), amt_uid4(gen), mcr, tcr_uid2(gen) );
      else if( i % 17 <= 11 )
         obj = new_call_obj( amt_uid3(gen), amt_uid2(gen), mcr, tcr_uid2(gen) );
      else if( i % 17 <= 12 )
         obj = new_call_obj( amt_uid2(gen), amt_uid3(gen), mcr, tcr_uid2(gen) );
      else if( i % 17 <= 13 )
         obj = new_call_obj( amt_uid4(gen), amt_uid2(gen), mcr, tcr_uid2(gen) );
      else if( i % 17 <= 14 )
         obj = new_call_obj( amt_uid2(gen), amt_uid4(gen), mcr, tcr_uid2(gen) );
      else if( i % 17 <= 15 )
         obj = new_call_obj( amt_uid3(gen), amt_uid4(gen), mcr, tcr_uid2(gen) );
      else // if( i % 17 <= 16 )
         obj = new_call_obj( amt_uid4(gen), amt_uid3(gen), mcr, tcr_uid2(gen) );

      // call_price
      price cp = price::call_price( obj.get_debt(), obj.get_collateral(), mcr );

      // get feed_price, and make sure we have sufficient good samples
      int retry = 20;
      do {
         if( i % 5 == 0 )
            fp = price( asset(amt_uid(gen)), asset(amt_uid(gen), asset_id_type(1)) );
         else if( i % 5 == 1 )
            fp = price( asset(amt_uid2(gen)), asset(amt_uid2(gen), asset_id_type(1)) );
         else if( i % 5 == 2 )
            fp = price( asset(amt_uid3(gen)), asset(amt_uid3(gen), asset_id_type(1)) );
         else if( i % 25 <= 18 )
            fp = price( asset(amt_uid4(gen)), asset(amt_uid4(gen), asset_id_type(1)) );
         else if( i % 25 == 19 )
            fp = price( asset(amt_uid2(gen)), asset(amt_uid3(gen), asset_id_type(1)) );
         else if( i % 25 == 20 )
            fp = price( asset(amt_uid3(gen)), asset(amt_uid2(gen), asset_id_type(1)) );
         else if( i % 25 == 21 )
            fp = price( asset(amt_uid3(gen)), asset(amt_uid4(gen), asset_id_type(1)) );
         else if( i % 25 == 22 )
            fp = price( asset(amt_uid4(gen)), asset(amt_uid3(gen), asset_id_type(1)) );
         else if( i % 25 == 23 )
            fp = price( asset(amt_uid4(gen)), asset(amt_uid2(gen), asset_id_type(1)) );
         else // if( i % 25 == 24 )
            fp = price( asset(amt_uid2(gen)), asset(amt_uid4(gen), asset_id_type(1)) );
         --retry;
      } while( retry > 0 && ( cp > fp || cp < ( fp / ratio_type( mcr, 1000 ) ) ) );

      // match_price
      if( i % 16 == 0 )
         mp = fp * ratio_type( 1001, 1000 );
      else if( i % 4 == 0 )
         mp = fp * ratio_type( 1100, 1000 );
      else if( i % 4 == 1 )
         mp = fp * ratio_type( mp_num_uid(gen) , 1000 );
      else if( i % 8 == 4 )
         mp = price( asset(amt_uid2(gen)), asset(amt_uid3(gen), asset_id_type(1)) );
      else if( i % 8 == 5 )
         mp = price( asset(amt_uid3(gen)), asset(amt_uid2(gen), asset_id_type(1)) );
      else if( i % 8 == 6 )
         mp = price( asset(amt_uid2(gen)), asset(amt_uid2(gen), asset_id_type(1)) );
      else // if( i % 8 == 7 )
         mp = price( asset(amt_uid(gen)), asset(amt_uid(gen), asset_id_type(1)) );

      try {
         result = obj.get_max_debt_to_cover( mp, fp, mcr );
         auto vr = validate_result( obj, mp, fp, mcr, result, false );
         ++count[vr];
      }
      catch( fc::assert_exception& e )
      {
         BOOST_CHECK( e.to_detail_string().find( "result <= GRAPHENE_MAX_SHARE_SUPPLY" ) != string::npos );
         ++count[0];
      }
   }
   ilog( "count: [bad_input,sell zero,not set,"
         " sell full (perfect), sell full (<0.01%), sell full (<0.1%),sell full (<1%), sell full (other), ...,"
         " sell some (perfect), sell some (<0.01%), sell some (<0.1%),sell some (<1%), sell some (other), ... ]" );
   idump( (total)(count) );

} FC_CAPTURE_LOG_AND_RETHROW( (0) ) }

BOOST_AUTO_TEST_SUITE_END()
