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

#include <boost/test/unit_test.hpp>

#include "../common/database_fixture.hpp"

#include <graphene/app/util.hpp>

using namespace graphene::chain;
using namespace graphene::chain::test;
using namespace graphene::app;

using fc::uint128_t;

BOOST_AUTO_TEST_SUITE(app_util_tests)

BOOST_AUTO_TEST_CASE(uint128_amount_to_string_test) {

   fc::uint128_t max_u64( std::numeric_limits<uint64_t>::max() );
   fc::uint128_t min_gt_u64 = max_u64 + 1;
   fc::uint128_t one_u128 = max_u64 * 10;
   fc::uint128_t max_u128 = std::numeric_limits<fc::uint128_t>::max();

   BOOST_CHECK_EQUAL( uint128_amount_to_string( 0,          0), "0" );
   BOOST_CHECK_EQUAL( uint128_amount_to_string( 1,          0), "1" );
   BOOST_CHECK_EQUAL( uint128_amount_to_string( 100,        0), "100" );
   BOOST_CHECK_EQUAL( uint128_amount_to_string( 1024000,    0), "1024000" );
   BOOST_CHECK_EQUAL( uint128_amount_to_string( 1234567890, 0), "1234567890" );
   BOOST_CHECK_EQUAL( uint128_amount_to_string( max_u64,    0), "18446744073709551615" );
   BOOST_CHECK_EQUAL( uint128_amount_to_string( min_gt_u64, 0), "18446744073709551616" );
   BOOST_CHECK_EQUAL( uint128_amount_to_string( one_u128,   0), "184467440737095516150" );
   BOOST_CHECK_EQUAL( uint128_amount_to_string( max_u128,   0), "340282366920938463463374607431768211455" );

   BOOST_CHECK_EQUAL( uint128_amount_to_string( 0,          1), "0" );
   BOOST_CHECK_EQUAL( uint128_amount_to_string( 1,          1), "0.1" );
   BOOST_CHECK_EQUAL( uint128_amount_to_string( 100,        1), "10" );
   BOOST_CHECK_EQUAL( uint128_amount_to_string( 1024000,    1), "102400" );
   BOOST_CHECK_EQUAL( uint128_amount_to_string( 1234567890, 1), "123456789" );
   BOOST_CHECK_EQUAL( uint128_amount_to_string( max_u64,    1), "1844674407370955161.5" );
   BOOST_CHECK_EQUAL( uint128_amount_to_string( min_gt_u64, 1), "1844674407370955161.6" );
   BOOST_CHECK_EQUAL( uint128_amount_to_string( one_u128,   1), "18446744073709551615" );
   BOOST_CHECK_EQUAL( uint128_amount_to_string( max_u128,   1), "34028236692093846346337460743176821145.5" );

   BOOST_CHECK_EQUAL( uint128_amount_to_string( 0,          2), "0" );
   BOOST_CHECK_EQUAL( uint128_amount_to_string( 1,          2), "0.01" );
   BOOST_CHECK_EQUAL( uint128_amount_to_string( 100,        2), "1" );
   BOOST_CHECK_EQUAL( uint128_amount_to_string( 1024000,    2), "10240" );
   BOOST_CHECK_EQUAL( uint128_amount_to_string( 1234567890, 2), "12345678.9" );
   BOOST_CHECK_EQUAL( uint128_amount_to_string( max_u64,    2), "184467440737095516.15" );
   BOOST_CHECK_EQUAL( uint128_amount_to_string( min_gt_u64, 2), "184467440737095516.16" );
   BOOST_CHECK_EQUAL( uint128_amount_to_string( one_u128,   2), "1844674407370955161.5" );
   BOOST_CHECK_EQUAL( uint128_amount_to_string( max_u128,   2), "3402823669209384634633746074317682114.55" );

   BOOST_CHECK_EQUAL( uint128_amount_to_string( 0,          3), "0" );
   BOOST_CHECK_EQUAL( uint128_amount_to_string( 1,          3), "0.001" );
   BOOST_CHECK_EQUAL( uint128_amount_to_string( 100,        3), "0.1" );
   BOOST_CHECK_EQUAL( uint128_amount_to_string( 1024000,    3), "1024" );
   BOOST_CHECK_EQUAL( uint128_amount_to_string( 1234567890, 3), "1234567.89" );
   BOOST_CHECK_EQUAL( uint128_amount_to_string( max_u64,    3), "18446744073709551.615" );
   BOOST_CHECK_EQUAL( uint128_amount_to_string( min_gt_u64, 3), "18446744073709551.616" );
   BOOST_CHECK_EQUAL( uint128_amount_to_string( one_u128,   3), "184467440737095516.15" );
   BOOST_CHECK_EQUAL( uint128_amount_to_string( max_u128,   3), "340282366920938463463374607431768211.455" );

   BOOST_CHECK_EQUAL( uint128_amount_to_string( 0,          4), "0" );
   BOOST_CHECK_EQUAL( uint128_amount_to_string( 1,          4), "0.0001" );
   BOOST_CHECK_EQUAL( uint128_amount_to_string( 100,        4), "0.01" );
   BOOST_CHECK_EQUAL( uint128_amount_to_string( 1024000,    4), "102.4" );
   BOOST_CHECK_EQUAL( uint128_amount_to_string( 1234567890, 4), "123456.789" );
   BOOST_CHECK_EQUAL( uint128_amount_to_string( max_u64,    4), "1844674407370955.1615" );
   BOOST_CHECK_EQUAL( uint128_amount_to_string( min_gt_u64, 4), "1844674407370955.1616" );
   BOOST_CHECK_EQUAL( uint128_amount_to_string( one_u128,   4), "18446744073709551.615" );
   BOOST_CHECK_EQUAL( uint128_amount_to_string( max_u128,   4), "34028236692093846346337460743176821.1455" );

   BOOST_CHECK_EQUAL( uint128_amount_to_string( 0,          9), "0" );
   BOOST_CHECK_EQUAL( uint128_amount_to_string( 1,          9), "0.000000001" );
   BOOST_CHECK_EQUAL( uint128_amount_to_string( 100,        9), "0.0000001" );
   BOOST_CHECK_EQUAL( uint128_amount_to_string( 1024000,    9), "0.001024" );
   BOOST_CHECK_EQUAL( uint128_amount_to_string( 1234567890, 9), "1.23456789" );
   BOOST_CHECK_EQUAL( uint128_amount_to_string( max_u64,    9), "18446744073.709551615" );
   BOOST_CHECK_EQUAL( uint128_amount_to_string( min_gt_u64, 9), "18446744073.709551616" );
   BOOST_CHECK_EQUAL( uint128_amount_to_string( one_u128,   9), "184467440737.09551615" );
   BOOST_CHECK_EQUAL( uint128_amount_to_string( max_u128,   9), "340282366920938463463374607431.768211455" );

   BOOST_CHECK_EQUAL( uint128_amount_to_string( 0,          10), "0" );
   BOOST_CHECK_EQUAL( uint128_amount_to_string( 1,          10), "0.0000000001" );
   BOOST_CHECK_EQUAL( uint128_amount_to_string( 100,        10), "0.00000001" );
   BOOST_CHECK_EQUAL( uint128_amount_to_string( 1024000,    10), "0.0001024" );
   BOOST_CHECK_EQUAL( uint128_amount_to_string( 1234567890, 10), "0.123456789" );
   BOOST_CHECK_EQUAL( uint128_amount_to_string( max_u64,    10), "1844674407.3709551615" );
   BOOST_CHECK_EQUAL( uint128_amount_to_string( min_gt_u64, 10), "1844674407.3709551616" );
   BOOST_CHECK_EQUAL( uint128_amount_to_string( one_u128,   10), "18446744073.709551615" );
   BOOST_CHECK_EQUAL( uint128_amount_to_string( max_u128,   10), "34028236692093846346337460743.1768211455" );

   BOOST_CHECK_EQUAL( uint128_amount_to_string( 0,          19), "0" );
   BOOST_CHECK_EQUAL( uint128_amount_to_string( 1,          19), "0.0000000000000000001" );
   BOOST_CHECK_EQUAL( uint128_amount_to_string( 100,        19), "0.00000000000000001" );
   BOOST_CHECK_EQUAL( uint128_amount_to_string( 1024000,    19), "0.0000000000001024" );
   BOOST_CHECK_EQUAL( uint128_amount_to_string( 1234567890, 19), "0.000000000123456789" );
   BOOST_CHECK_EQUAL( uint128_amount_to_string( max_u64,    19), "1.8446744073709551615" );
   BOOST_CHECK_EQUAL( uint128_amount_to_string( min_gt_u64, 19), "1.8446744073709551616" );
   BOOST_CHECK_EQUAL( uint128_amount_to_string( one_u128,   19), "18.446744073709551615" );
   BOOST_CHECK_EQUAL( uint128_amount_to_string( max_u128,   19), "34028236692093846346.3374607431768211455" );

   BOOST_CHECK_EQUAL( uint128_amount_to_string( 0,          20), "0" );
   BOOST_CHECK_EQUAL( uint128_amount_to_string( 1,          20), "0.00000000000000000001" );
   BOOST_CHECK_EQUAL( uint128_amount_to_string( 100,        20), "0.000000000000000001" );
   BOOST_CHECK_EQUAL( uint128_amount_to_string( 1024000,    20), "0.00000000000001024" );
   BOOST_CHECK_EQUAL( uint128_amount_to_string( 1234567890, 20), "0.0000000000123456789" );
   BOOST_CHECK_EQUAL( uint128_amount_to_string( max_u64,    20), "0.18446744073709551615" );
   BOOST_CHECK_EQUAL( uint128_amount_to_string( min_gt_u64, 20), "0.18446744073709551616" );
   BOOST_CHECK_EQUAL( uint128_amount_to_string( one_u128,   20), "1.8446744073709551615" );
   BOOST_CHECK_EQUAL( uint128_amount_to_string( max_u128,   20), "3402823669209384634.63374607431768211455" );

   BOOST_CHECK_EQUAL( uint128_amount_to_string( 0,          21), "0" );
   BOOST_CHECK_EQUAL( uint128_amount_to_string( 1,          21), "0.000000000000000000001" );
   BOOST_CHECK_EQUAL( uint128_amount_to_string( 100,        21), "0.0000000000000000001" );
   BOOST_CHECK_EQUAL( uint128_amount_to_string( 1024000,    21), "0.000000000000001024" );
   BOOST_CHECK_EQUAL( uint128_amount_to_string( 1234567890, 21), "0.00000000000123456789" );
   BOOST_CHECK_EQUAL( uint128_amount_to_string( max_u64,    21), "0.018446744073709551615" );
   BOOST_CHECK_EQUAL( uint128_amount_to_string( min_gt_u64, 21), "0.018446744073709551616" );
   BOOST_CHECK_EQUAL( uint128_amount_to_string( one_u128,   21), "0.18446744073709551615" );
   BOOST_CHECK_EQUAL( uint128_amount_to_string( max_u128,   21), "340282366920938463.463374607431768211455" );

   BOOST_CHECK_EQUAL( uint128_amount_to_string( 0,          38), "0" );
   BOOST_CHECK_EQUAL( uint128_amount_to_string( 1,          38), "0.00000000000000000000000000000000000001" );
   BOOST_CHECK_EQUAL( uint128_amount_to_string( 100,        38), "0.000000000000000000000000000000000001" );
   BOOST_CHECK_EQUAL( uint128_amount_to_string( 1024000,    38), "0.00000000000000000000000000000001024" );
   BOOST_CHECK_EQUAL( uint128_amount_to_string( 1234567890, 38), "0.0000000000000000000000000000123456789" );
   BOOST_CHECK_EQUAL( uint128_amount_to_string( max_u64,    38), "0.00000000000000000018446744073709551615" );
   BOOST_CHECK_EQUAL( uint128_amount_to_string( min_gt_u64, 38), "0.00000000000000000018446744073709551616" );
   BOOST_CHECK_EQUAL( uint128_amount_to_string( one_u128,   38), "0.0000000000000000018446744073709551615" );
   BOOST_CHECK_EQUAL( uint128_amount_to_string( max_u128,   38), "3.40282366920938463463374607431768211455" );

   BOOST_CHECK_EQUAL( uint128_amount_to_string( 0,          39), "0" );
   BOOST_CHECK_EQUAL( uint128_amount_to_string( 1,          39), "0.000000000000000000000000000000000000001" );
   BOOST_CHECK_EQUAL( uint128_amount_to_string( 100,        39), "0.0000000000000000000000000000000000001" );
   BOOST_CHECK_EQUAL( uint128_amount_to_string( 1024000,    39), "0.000000000000000000000000000000001024" );
   BOOST_CHECK_EQUAL( uint128_amount_to_string( 1234567890, 39), "0.00000000000000000000000000000123456789" );
   BOOST_CHECK_EQUAL( uint128_amount_to_string( max_u64,    39), "0.000000000000000000018446744073709551615" );
   BOOST_CHECK_EQUAL( uint128_amount_to_string( min_gt_u64, 39), "0.000000000000000000018446744073709551616" );
   BOOST_CHECK_EQUAL( uint128_amount_to_string( one_u128,   39), "0.00000000000000000018446744073709551615" );
   BOOST_CHECK_EQUAL( uint128_amount_to_string( max_u128,   39), "0.340282366920938463463374607431768211455" );

   BOOST_CHECK_EQUAL( uint128_amount_to_string( 0,          40), "0" );
   BOOST_CHECK_EQUAL( uint128_amount_to_string( 1,          40), "0.0000000000000000000000000000000000000001" );
   BOOST_CHECK_EQUAL( uint128_amount_to_string( 100,        40), "0.00000000000000000000000000000000000001" );
   BOOST_CHECK_EQUAL( uint128_amount_to_string( 1024000,    40), "0.0000000000000000000000000000000001024" );
   BOOST_CHECK_EQUAL( uint128_amount_to_string( 1234567890, 40), "0.000000000000000000000000000000123456789" );
   BOOST_CHECK_EQUAL( uint128_amount_to_string( max_u64,    40), "0.0000000000000000000018446744073709551615" );
   BOOST_CHECK_EQUAL( uint128_amount_to_string( min_gt_u64, 40), "0.0000000000000000000018446744073709551616" );
   BOOST_CHECK_EQUAL( uint128_amount_to_string( one_u128,   40), "0.000000000000000000018446744073709551615" );
   BOOST_CHECK_EQUAL( uint128_amount_to_string( max_u128,   40), "0.0340282366920938463463374607431768211455" );

   BOOST_CHECK_EQUAL( uint128_amount_to_string( 0,          127), "0" );
   BOOST_CHECK_EQUAL( uint128_amount_to_string( 1,          127), "0.0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000001" );
   BOOST_CHECK_EQUAL( uint128_amount_to_string( 100,        127), "0.00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000001" );
   BOOST_CHECK_EQUAL( uint128_amount_to_string( max_u128,   127), "0.0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000340282366920938463463374607431768211455" );

}

BOOST_AUTO_TEST_CASE(price_to_string_throws) {

   int64_t m = std::numeric_limits<int64_t>::max();
   int64_t n = -1;
   int64_t x = m / 10000;
   int64_t y = m / 2;
   int64_t z = m - 1;

   int64_t a[11] = {n,0,1,2,3,10,200,x,y,z,m};
   price p[11][11];
   for( int i = 0; i < 11; ++i )
      for( int j = 0; j < 11; ++j )
         p[i][j] = price( asset( a[i] ), asset( a[j] ) );

   for( int i = 0; i < 11; ++i )
   {
      for( int j = 0; j < 11; ++j )
      {
         price pr = p[i][j];
         if( i == 0 )
         {
            GRAPHENE_REQUIRE_THROW( price_to_string( p[i][j], 0, 0 ), fc::exception );
         }
         else if( i == 1 )
         {
            BOOST_CHECK_EQUAL( price_to_string( p[i][j],  0,  0 ), "0" );
            BOOST_CHECK_EQUAL( price_to_string( p[i][j],  0, 19 ), "0" );
            BOOST_CHECK_EQUAL( price_to_string( p[i][j], 19,  0 ), "0" );
            BOOST_CHECK_EQUAL( price_to_string( p[i][j], 19, 19 ), "0" );
            BOOST_CHECK_EQUAL( price_to_string( p[i][j], 20, 20 ), "0" );
         }
         else
         {
            GRAPHENE_REQUIRE_THROW( price_to_string( p[i][j], 20, 0 ), fc::exception );
            GRAPHENE_REQUIRE_THROW( price_to_string( p[i][j], 0, 20 ), fc::exception );
         }
         try {
            if ( pr.base.amount == 0 || (pr.base.amount > 0 && pr.quote.amount >= 0 ) )
            {
               // idump( (i) (j) (pr) ); // for debugging
               // These should not throw
               // TODO: Verify results
               BOOST_CHECK( !price_to_string( pr ,0,0).empty() );
               BOOST_CHECK( !price_to_string( pr ,0,1).empty() );
               BOOST_CHECK( !price_to_string( pr ,0,2).empty() );
               BOOST_CHECK( !price_to_string( pr ,0,8).empty() );
               BOOST_CHECK( !price_to_string( pr ,0,19).empty() );
               BOOST_CHECK( !price_to_string( pr ,1,0).empty() );
               BOOST_CHECK( !price_to_string( pr ,1,15).empty() );
               BOOST_CHECK( !price_to_string( pr ,2,6).empty() );
               BOOST_CHECK( !price_to_string( pr ,2,10).empty() );
               BOOST_CHECK( !price_to_string( pr ,5,0).empty() );
               BOOST_CHECK( !price_to_string( pr ,9,1).empty() );
               BOOST_CHECK( !price_to_string( pr ,9,9).empty() );
               BOOST_CHECK( !price_to_string( pr ,9,19).empty() );
               BOOST_CHECK( !price_to_string( pr ,18,10).empty() );
               BOOST_CHECK( !price_to_string( pr ,18,13).empty() );
               BOOST_CHECK( !price_to_string( pr ,18,19).empty() );
               BOOST_CHECK( !price_to_string( pr ,19,0).empty() );
               BOOST_CHECK( !price_to_string( pr ,19,7).empty() );
               BOOST_CHECK( !price_to_string( pr ,19,19).empty() );
               price new_price = p[j][i];
               if (pr.quote.amount >= 0)
                  BOOST_CHECK( !price_diff_percent_string( pr, new_price ).empty() );
               else
                  GRAPHENE_REQUIRE_THROW( price_diff_percent_string( pr, new_price ), fc::exception );
            } else {
               GRAPHENE_REQUIRE_THROW( price_to_string( pr, 0, 0 ), fc::exception );
            }
         } catch(fc::exception& fcx) {
            BOOST_FAIL( "FC Exception logging price_to_string: " + fcx.to_detail_string() );
         } catch(...) {
            BOOST_FAIL( "Uncaught exception in price_to_string. i=" + std::to_string(i) + " j=" + std::to_string(j));
         }
      }
   }
}

/**
 * Verify that price_to_string comes back with the correct results. Put edge cases here.
 */
BOOST_AUTO_TEST_CASE(price_to_string_verify)
{
   try
   {
      BOOST_CHECK_EQUAL( price_to_string( price{ asset(1), asset(1) }, 0, 0 ), "1" );
      BOOST_CHECK_EQUAL( price_to_string( price{ asset(10), asset(10) }, 0, 0), "1" );
      int64_t mx = std::numeric_limits<int64_t>::max();
      BOOST_CHECK_EQUAL( price_to_string( price{ asset(mx), asset(mx) }, 0, 0), "1" );
      BOOST_CHECK_EQUAL( price_to_string( price{ asset(1), asset(mx) }, 0, 0), "0.0000000000000000001" );
      BOOST_CHECK_EQUAL( price_to_string( price{ asset(mx), asset(1) }, 0, 0), "9223372036854775807" );
   } 
   catch (fc::exception& fx)
   {
      BOOST_FAIL( "FC Exception: " + fx.to_detail_string() );
   }
}

BOOST_AUTO_TEST_SUITE_END()
