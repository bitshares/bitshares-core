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

#include <boost/test/unit_test.hpp>

#include <graphene/chain/database.hpp>
#include <graphene/chain/protocol/protocol.hpp>

#include <graphene/chain/account_object.hpp>
#include <graphene/chain/asset_object.hpp>
#include <graphene/chain/exceptions.hpp>

#include <graphene/db/simple_index.hpp>

#include <fc/crypto/digest.hpp>
#include <fc/crypto/hex.hpp>
#include "../common/database_fixture.hpp"

#include <algorithm>
#include <random>

using namespace graphene::chain;
using namespace graphene::db;

BOOST_FIXTURE_TEST_SUITE( basic_tests, database_fixture )

/**
 * Verify that names are RFC-1035 compliant https://tools.ietf.org/html/rfc1035
 * https://github.com/cryptonomex/graphene/issues/15
 */
BOOST_AUTO_TEST_CASE( valid_name_test )
{
   BOOST_CHECK( is_valid_name( "a" ) );
   BOOST_CHECK( !is_valid_name( "A" ) );
   BOOST_CHECK( !is_valid_name( "0" ) );
   BOOST_CHECK( !is_valid_name( "." ) );
   BOOST_CHECK( !is_valid_name( "-" ) );

   BOOST_CHECK( is_valid_name( "aa" ) );
   BOOST_CHECK( !is_valid_name( "aA" ) );
   BOOST_CHECK( is_valid_name( "a0" ) );
   BOOST_CHECK( !is_valid_name( "a." ) );
   BOOST_CHECK( !is_valid_name( "a-" ) );

   BOOST_CHECK( is_valid_name( "aaa" ) );
   BOOST_CHECK( !is_valid_name( "aAa" ) );
   BOOST_CHECK( is_valid_name( "a0a" ) );
   BOOST_CHECK( is_valid_name( "a.a" ) );
   BOOST_CHECK( is_valid_name( "a-a" ) );

   BOOST_CHECK( is_valid_name( "aa0" ) );
   BOOST_CHECK( !is_valid_name( "aA0" ) );
   BOOST_CHECK( is_valid_name( "a00" ) );
   BOOST_CHECK( !is_valid_name( "a.0" ) );
   BOOST_CHECK( is_valid_name( "a-0" ) );

   BOOST_CHECK(  is_valid_name( "aaa-bbb-ccc" ) );
   BOOST_CHECK(  is_valid_name( "aaa-bbb.ccc" ) );

   BOOST_CHECK( !is_valid_name( "aaa,bbb-ccc" ) );
   BOOST_CHECK( !is_valid_name( "aaa_bbb-ccc" ) );
   BOOST_CHECK( !is_valid_name( "aaa-BBB-ccc" ) );

   BOOST_CHECK( !is_valid_name( "1aaa-bbb" ) );
   BOOST_CHECK( !is_valid_name( "-aaa-bbb-ccc" ) );
   BOOST_CHECK( !is_valid_name( ".aaa-bbb-ccc" ) );
   BOOST_CHECK( !is_valid_name( "/aaa-bbb-ccc" ) );

   BOOST_CHECK( !is_valid_name( "aaa-bbb-ccc-" ) );
   BOOST_CHECK( !is_valid_name( "aaa-bbb-ccc." ) );
   BOOST_CHECK( !is_valid_name( "aaa-bbb-ccc.." ) );
   BOOST_CHECK( !is_valid_name( "aaa-bbb-ccc/" ) );

   BOOST_CHECK( !is_valid_name( "aaa..bbb-ccc" ) );
   BOOST_CHECK( is_valid_name( "aaa.bbb-ccc" ) );
   BOOST_CHECK( is_valid_name( "aaa.bbb.ccc" ) );

   BOOST_CHECK(  is_valid_name( "aaa--bbb--ccc" ) );
   BOOST_CHECK(  is_valid_name( "xn--sandmnnchen-p8a.de" ) );
   BOOST_CHECK(  is_valid_name( "xn--sandmnnchen-p8a.dex" ) );
   BOOST_CHECK(  is_valid_name( "xn-sandmnnchen-p8a.de" ) );
   BOOST_CHECK(  is_valid_name( "xn-sandmnnchen-p8a.dex" ) );

   BOOST_CHECK(  is_valid_name( "this-label-has-less-than-64-char.acters-63-to-be-really-precise" ) );
   BOOST_CHECK( !is_valid_name( "this-label-has-more-than-63-char.act.ers-64-to-be-really-precise" ) );
   BOOST_CHECK( !is_valid_name( "none.of.these.labels.has.more.than-63.chars--but.still.not.valid" ) );
}

BOOST_AUTO_TEST_CASE( valid_symbol_test )
{
   BOOST_CHECK( !is_valid_symbol( "A" ) );
   BOOST_CHECK( !is_valid_symbol( "a" ) );
   BOOST_CHECK( !is_valid_symbol( "0" ) );
   BOOST_CHECK( !is_valid_symbol( "." ) );

   BOOST_CHECK( !is_valid_symbol( "AA" ) );
   BOOST_CHECK( !is_valid_symbol( "Aa" ) );
   BOOST_CHECK( !is_valid_symbol( "A0" ) );
   BOOST_CHECK( !is_valid_symbol( "A." ) );

   BOOST_CHECK( is_valid_symbol( "AAA" ) );
   BOOST_CHECK( !is_valid_symbol( "AaA" ) );
   BOOST_CHECK( is_valid_symbol( "A0A" ) );
   BOOST_CHECK( is_valid_symbol( "A.A" ) );

   BOOST_CHECK( !is_valid_symbol( "A..A" ) );
   BOOST_CHECK( !is_valid_symbol( "A.A." ) );
   BOOST_CHECK( !is_valid_symbol( "A.A.A" ) );

   BOOST_CHECK( is_valid_symbol( "AAAAAAAAAAAAAAAA" ) );
   BOOST_CHECK( !is_valid_symbol( "AAAAAAAAAAAAAAAAA" ) );
   BOOST_CHECK( is_valid_symbol( "A.AAAAAAAAAAAAAA" ) );
   BOOST_CHECK( !is_valid_symbol( "A.AAAAAAAAAAAA.A" ) );

   BOOST_CHECK( is_valid_symbol( "AAA000AAA" ) );
}

BOOST_AUTO_TEST_CASE( price_test )
{
    auto price_max = []( uint32_t a, uint32_t b )
    {   return price::max( asset_id_type(a), asset_id_type(b) );   };
    auto price_min = []( uint32_t a, uint32_t b )
    {   return price::min( asset_id_type(a), asset_id_type(b) );   };

    BOOST_CHECK( price_max(0,1) > price_min(0,1) );
    BOOST_CHECK( price_max(1,0) > price_min(1,0) );
    BOOST_CHECK( price_max(0,1) >= price_min(0,1) );
    BOOST_CHECK( price_max(1,0) >= price_min(1,0) );
    BOOST_CHECK( price_max(0,1) >= price_max(0,1) );
    BOOST_CHECK( price_max(1,0) >= price_max(1,0) );
    BOOST_CHECK( price_min(0,1) < price_max(0,1) );
    BOOST_CHECK( price_min(1,0) < price_max(1,0) );
    BOOST_CHECK( price_min(0,1) <= price_max(0,1) );
    BOOST_CHECK( price_min(1,0) <= price_max(1,0) );
    BOOST_CHECK( price_min(0,1) <= price_min(0,1) );
    BOOST_CHECK( price_min(1,0) <= price_min(1,0) );
    BOOST_CHECK( price_min(1,0) != price_max(1,0) );
    BOOST_CHECK( ~price_max(0,1) != price_min(0,1) );
    BOOST_CHECK( ~price_min(0,1) != price_max(0,1) );
    BOOST_CHECK( ~price_max(0,1) == price_min(1,0) );
    BOOST_CHECK( ~price_min(0,1) == price_max(1,0) );
    BOOST_CHECK( ~price_max(0,1) < ~price_min(0,1) );
    BOOST_CHECK( ~price_max(0,1) <= ~price_min(0,1) );
    price a(asset(1), asset(2,asset_id_type(1)));
    price b(asset(2), asset(2,asset_id_type(1)));
    price c(asset(1), asset(2,asset_id_type(1)));
    BOOST_CHECK(a < b);
    BOOST_CHECK(b > a);
    BOOST_CHECK(a == c);
    BOOST_CHECK(!(b == c));

    GRAPHENE_REQUIRE_THROW( price(asset(1),  asset(1)) * ratio_type(1,1), fc::exception );
    GRAPHENE_REQUIRE_THROW( price(asset(0),  asset(1, asset_id_type(1))) * ratio_type(1,1), fc::exception );
    GRAPHENE_REQUIRE_THROW( price(asset(-1), asset(1, asset_id_type(1))) * ratio_type(1,1), fc::exception );
    GRAPHENE_REQUIRE_THROW( price(asset(1),  asset(0, asset_id_type(1))) * ratio_type(1,1), fc::exception );
    GRAPHENE_REQUIRE_THROW( price(asset(1),  asset(-1, asset_id_type(1))) * ratio_type(1,1), fc::exception );
    GRAPHENE_REQUIRE_THROW( price(asset(1),  asset(1, asset_id_type(1))) * ratio_type(0,1), fc::exception );
    GRAPHENE_REQUIRE_THROW( price(asset(1),  asset(1, asset_id_type(1))) * ratio_type(-1,1), fc::exception );
    GRAPHENE_REQUIRE_THROW( price(asset(1),  asset(1, asset_id_type(1))) * ratio_type(1,0), std::domain_error ); // zero denominator
    GRAPHENE_REQUIRE_THROW( price(asset(1),  asset(1, asset_id_type(1))) * ratio_type(1,-1), fc::exception );

    GRAPHENE_REQUIRE_THROW( price(asset(0),  asset(1, asset_id_type(1))) / ratio_type(1,1), fc::exception );
    GRAPHENE_REQUIRE_THROW( price(asset(-1), asset(1, asset_id_type(1))) / ratio_type(1,1), fc::exception );
    GRAPHENE_REQUIRE_THROW( price(asset(1),  asset(0, asset_id_type(1))) / ratio_type(1,1), fc::exception );
    GRAPHENE_REQUIRE_THROW( price(asset(1),  asset(-1, asset_id_type(1))) / ratio_type(1,1), fc::exception );
    GRAPHENE_REQUIRE_THROW( price(asset(1),  asset(1, asset_id_type(1))) / ratio_type(0,1), fc::exception );
    GRAPHENE_REQUIRE_THROW( price(asset(1),  asset(1, asset_id_type(1))) / ratio_type(-1,1), fc::exception );
    GRAPHENE_REQUIRE_THROW( price(asset(1),  asset(1, asset_id_type(1))) / ratio_type(1,0), std::domain_error ); // zero denominator
    GRAPHENE_REQUIRE_THROW( price(asset(1),  asset(1, asset_id_type(1))) / ratio_type(1,-1), fc::exception );

    BOOST_CHECK( price(asset(1), asset(1, asset_id_type(1))) * ratio_type(1,1) == price(asset(1), asset(1, asset_id_type(1))) );
    BOOST_CHECK( price(asset(3), asset(2, asset_id_type(1))) * ratio_type(80,100) == price(asset(12), asset(10, asset_id_type(1))) );
    BOOST_CHECK( price(asset(3), asset(2, asset_id_type(1))) * ratio_type(120,100) == price(asset(9), asset(5, asset_id_type(1))) );

    BOOST_CHECK( price(asset(1), asset(1, asset_id_type(1))) / ratio_type(1,1) == price(asset(1), asset(1, asset_id_type(1))) );
    BOOST_CHECK( price(asset(3), asset(2, asset_id_type(1))) / ratio_type(80,100) == price(asset(15), asset(8, asset_id_type(1))) );
    BOOST_CHECK( price(asset(3), asset(2, asset_id_type(1))) / ratio_type(120,100) == price(asset(30), asset(24, asset_id_type(1))) );

    BOOST_CHECK( price_max(0,1) * ratio_type(2,1) == price_max(0,1) );
    BOOST_CHECK( price_max(0,1) * ratio_type(125317293,125317292) == price_max(0,1) );
    BOOST_CHECK( price_max(0,1) * ratio_type(125317293,105317292) == price_max(0,1) );
    BOOST_CHECK( price_max(0,1) * ratio_type(125317293,25317292) == price_max(0,1) );
    BOOST_CHECK( price_min(0,1) * ratio_type(1,2) == price_min(0,1) );
    BOOST_CHECK( price_min(0,1) * ratio_type(98752395,98752396) == price_min(0,1) );
    BOOST_CHECK( price_min(0,1) * ratio_type(70000000,99999999) == price_min(0,1) );
    BOOST_CHECK( price_min(0,1) * ratio_type(30000000,99999999) == price_min(0,1) );

    price more_than_max = price_max(0,1);
    more_than_max.base.amount *= 5;
    more_than_max.quote.amount *= 3;
    BOOST_CHECK( more_than_max * ratio_type(125317293,125317292) == more_than_max );
    BOOST_CHECK( more_than_max * ratio_type(125317293,125317293) == more_than_max );
    BOOST_CHECK( more_than_max * ratio_type(125317293,125317294) == price_max(0,1) );

    price less_than_min = price_min(0,1);
    less_than_min.base.amount *= 19;
    less_than_min.quote.amount *= 47;
    BOOST_CHECK( less_than_min * ratio_type(125317293,125317292) == price_min(0,1) );
    BOOST_CHECK( less_than_min * ratio_type(125317293,125317293) == less_than_min );
    BOOST_CHECK( less_than_min * ratio_type(125317293,125317294) == less_than_min );

    price less_than_max = price_max(0,1);
    less_than_max.quote.amount = 11;
    BOOST_CHECK( less_than_max * ratio_type(7,1) == price(asset(less_than_max.base.amount*7/11),asset(1,asset_id_type(1))) );
    less_than_max.quote.amount = 92131419;
    BOOST_CHECK( less_than_max * ratio_type(7,1) == price(asset(less_than_max.base.amount*7/92131419),asset(1,asset_id_type(1))) );
    less_than_max.quote.amount = 192131419;
    BOOST_CHECK( less_than_max * ratio_type(7,1) == price(asset(less_than_max.base.amount.value*7>>3),asset(192131419>>3,asset_id_type(1))) );

    price more_than_min = price_min(0,1);
    more_than_min.base.amount = 11;
    BOOST_CHECK( more_than_min * ratio_type(1,7) == price(asset(1),asset(more_than_min.quote.amount*7/11,asset_id_type(1))) );
    more_than_min.base.amount = 64823;
    BOOST_CHECK( more_than_min * ratio_type(31672,102472047) == price(asset(1),asset((fc::uint128(more_than_min.quote.amount.value)*102472047/(64823*31672)).to_uint64(),asset_id_type(1))) );
    more_than_min.base.amount = 13;
    BOOST_CHECK( more_than_min * ratio_type(202472059,3) == price(asset((int64_t(13)*202472059)>>1),asset((more_than_min.quote.amount.value*3)>>1,asset_id_type(1))) ); // after >>1, quote = max*1.5, but gcd = 3, so quote/=3 = max/2, less than max

    price less_than_max2 = price_max(0,1);
    less_than_max2.base.amount *= 2;
    less_than_max2.quote.amount *= 7;
    BOOST_CHECK( less_than_max2 * ratio_type(1,1) == less_than_max2 );
    BOOST_CHECK( less_than_max2 * ratio_type(5,2) == price(asset(less_than_max2.base.amount*5/2/7),asset(1,asset_id_type(1))) );

    BOOST_CHECK( ( asset(1) * price( asset(1), asset(1, asset_id_type(1)) ) ) == asset(1, asset_id_type(1)) );
    BOOST_CHECK( ( asset(1) * price( asset(1, asset_id_type(1)), asset(1) ) ) == asset(1, asset_id_type(1)) );
    BOOST_CHECK( ( asset(1, asset_id_type(1)) * price( asset(1), asset(1, asset_id_type(1)) ) ) == asset(1) );
    BOOST_CHECK( ( asset(1, asset_id_type(1)) * price( asset(1, asset_id_type(1)), asset(1) ) ) == asset(1) );

    BOOST_CHECK( ( asset(3) * price( asset(3), asset(5, asset_id_type(1)) ) ) == asset(5, asset_id_type(1)) ); // round_down(3*5/3)
    BOOST_CHECK( ( asset(5) * price( asset(2, asset_id_type(1)), asset(7) ) ) == asset(1, asset_id_type(1)) ); // round_down(5*2/7)
    BOOST_CHECK( ( asset(7, asset_id_type(1)) * price( asset(2), asset(3, asset_id_type(1)) ) ) == asset(4) ); // round_down(7*2/3)
    BOOST_CHECK( ( asset(9, asset_id_type(1)) * price( asset(8, asset_id_type(1)), asset(7) ) ) == asset(7) ); // round_down(9*7/8)

    // asset and price doesn't match
    BOOST_CHECK_THROW( asset(1) * price( asset(1, asset_id_type(2)), asset(1, asset_id_type(1)) ), fc::assert_exception );
    // divide by zero
    BOOST_CHECK_THROW( asset(1) * price( asset(0), asset(1, asset_id_type(1)) ), fc::assert_exception );
    BOOST_CHECK_THROW( asset(1) * price( asset(1, asset_id_type(1)), asset(0) ), fc::assert_exception );
    // overflow
    BOOST_CHECK_THROW( asset(GRAPHENE_MAX_SHARE_SUPPLY/2+1) * price( asset(1), asset(2, asset_id_type(1)) ), fc::assert_exception );
    BOOST_CHECK_THROW( asset(2) * price( asset(GRAPHENE_MAX_SHARE_SUPPLY/2+1, asset_id_type(1)), asset(1) ), fc::assert_exception );

    BOOST_CHECK( asset(1).multiply_and_round_up( price( asset(1), asset(1, asset_id_type(1)) ) ) == asset(1, asset_id_type(1)) );
    BOOST_CHECK( asset(1).multiply_and_round_up( price( asset(1, asset_id_type(1)), asset(1) ) ) == asset(1, asset_id_type(1)) );
    BOOST_CHECK( asset(1, asset_id_type(1)).multiply_and_round_up( price( asset(1), asset(1, asset_id_type(1)) ) ) == asset(1) );
    BOOST_CHECK( asset(1, asset_id_type(1)).multiply_and_round_up( price( asset(1, asset_id_type(1)), asset(1) ) ) == asset(1) );

    // round_up(3*5/3)
    BOOST_CHECK( asset(3).multiply_and_round_up( price( asset(3), asset(5, asset_id_type(1)) ) ) == asset(5, asset_id_type(1)) );
    // round_up(5*2/7)
    BOOST_CHECK( asset(5).multiply_and_round_up( price( asset(2, asset_id_type(1)), asset(7) ) ) == asset(2, asset_id_type(1)) );
    // round_up(7*2/3)
    BOOST_CHECK( asset(7, asset_id_type(1)).multiply_and_round_up( price( asset(2), asset(3, asset_id_type(1)) ) ) == asset(5) );
    // round_up(9*7/8)
    BOOST_CHECK( asset(9, asset_id_type(1)).multiply_and_round_up( price( asset(8, asset_id_type(1)), asset(7) ) ) == asset(8) );

    // asset and price doesn't match
    BOOST_CHECK_THROW( asset(1, asset_id_type(3)).multiply_and_round_up( price( asset(1, asset_id_type(2)), asset(1) ) ),
                       fc::assert_exception );
    // divide by zero
    BOOST_CHECK_THROW( asset(1).multiply_and_round_up( price( asset(0), asset(1, asset_id_type(1)) ) ), fc::assert_exception );
    BOOST_CHECK_THROW( asset(1).multiply_and_round_up( price( asset(1, asset_id_type(1)), asset(0) ) ), fc::assert_exception );
    // overflow
    BOOST_CHECK_THROW( asset(GRAPHENE_MAX_SHARE_SUPPLY/2+1).multiply_and_round_up( price( asset(1), asset(2, asset_id_type(1)) ) ),
                       fc::assert_exception );
    BOOST_CHECK_THROW( asset(2).multiply_and_round_up( price( asset(GRAPHENE_MAX_SHARE_SUPPLY/2+1, asset_id_type(1)), asset(1) ) ),
                       fc::assert_exception );

    price_feed dummy;
    dummy.maintenance_collateral_ratio = 1002;
    dummy.maximum_short_squeeze_ratio = 1234;
    dummy.settlement_price = price(asset(1000), asset(2000, asset_id_type(1)));
    price_feed dummy2 = dummy;
    BOOST_CHECK(dummy == dummy2);
}

BOOST_AUTO_TEST_CASE( price_multiplication_test )
{ try {
   // random test
   std::mt19937_64 gen( time(NULL) );
   std::uniform_int_distribution<int64_t> amt_uid(1, GRAPHENE_MAX_SHARE_SUPPLY);
   std::uniform_int_distribution<int64_t> amt_uid2(1, 1000*1000*1000);
   std::uniform_int_distribution<int64_t> amt_uid3(1, 1000*1000);
   std::uniform_int_distribution<int64_t> amt_uid4(1, 1000);
   asset a;
   price p;
   for( int i = 1*1000*1000; i > 0; --i )
   {
      if( i <= 30 )
         a = asset( 0 );
      else if( i % 4 == 0 )
         a = asset( amt_uid(gen) );
      else if( i % 4 == 1 )
         a = asset( amt_uid2(gen) );
      else if( i % 4 == 2 )
         a = asset( amt_uid3(gen) );
      else // if( i % 4 == 3 )
         a = asset( amt_uid4(gen) );

      if( i % 7 == 0 )
         p = price( asset(amt_uid(gen)), asset(amt_uid(gen), asset_id_type(1)) );
      else if( i % 7 == 1 )
         p = price( asset(amt_uid2(gen)), asset(amt_uid2(gen), asset_id_type(1)) );
      else if( i % 7 == 2 )
         p = price( asset(amt_uid3(gen)), asset(amt_uid3(gen), asset_id_type(1)) );
      else if( i % 7 == 3 )
         p = price( asset(amt_uid4(gen)), asset(amt_uid4(gen), asset_id_type(1)) );
      else if( i % 7 == 4 )
         p = price( asset(amt_uid(gen)), asset(amt_uid(gen), asset_id_type(1)) );
      else if( i % 7 == 5 )
         p = price( asset(amt_uid4(gen)), asset(amt_uid2(gen), asset_id_type(1)) );
      else // if( i % 7 == 6 )
         p = price( asset(amt_uid2(gen)), asset(amt_uid4(gen), asset_id_type(1)) );

      try
      {
         asset b = a * p;
         asset a1 = b.multiply_and_round_up( p );
         BOOST_CHECK( a1 <= a );
         BOOST_CHECK( (a1 * p) == b );

         b = a.multiply_and_round_up( p );
         a1 = b * p;
         BOOST_CHECK( a1 >= a );
         BOOST_CHECK( a1.multiply_and_round_up( p ) == b );
      }
      catch( fc::assert_exception& e )
      {
         BOOST_CHECK( e.to_detail_string().find( "result <= GRAPHENE_MAX_SHARE_SUPPLY" ) != string::npos );
      }
   }
} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE( memo_test )
{ try {
   memo_data m;
   auto sender = generate_private_key("1");
   auto receiver = generate_private_key("2");
   m.from = sender.get_public_key();
   m.to = receiver.get_public_key();
   m.set_message(sender, receiver.get_public_key(), "Hello, world!", 12345);

   decltype(fc::digest(m)) hash("8de72a07d093a589f574460deb19023b4aff354b561eb34590d9f4629f51dbf3");
   if( fc::digest(m) != hash )
   {
      // If this happens, notify the web guys that the memo serialization format changed.
      edump((m)(fc::digest(m)));
      BOOST_FAIL("Memo format has changed. Notify the web guys and update this test.");
   }
   BOOST_CHECK_EQUAL(m.get_message(receiver, sender.get_public_key()), "Hello, world!");
} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE( exceptions )
{
   GRAPHENE_CHECK_THROW(FC_THROW_EXCEPTION(balance_claim_invalid_claim_amount, "Etc"), balance_claim_invalid_claim_amount);
}

BOOST_AUTO_TEST_CASE( scaled_precision )
{
   const int64_t _k = 1000;
   const int64_t _m = _k*_k;
   const int64_t _g = _m*_k;
   const int64_t _t = _g*_k;
   const int64_t _p = _t*_k;
   const int64_t _e = _p*_k;

   BOOST_CHECK( asset::scaled_precision( 0) == share_type(   1   ) );
   BOOST_CHECK( asset::scaled_precision( 1) == share_type(  10   ) );
   BOOST_CHECK( asset::scaled_precision( 2) == share_type( 100   ) );
   BOOST_CHECK( asset::scaled_precision( 3) == share_type(   1*_k) );
   BOOST_CHECK( asset::scaled_precision( 4) == share_type(  10*_k) );
   BOOST_CHECK( asset::scaled_precision( 5) == share_type( 100*_k) );
   BOOST_CHECK( asset::scaled_precision( 6) == share_type(   1*_m) );
   BOOST_CHECK( asset::scaled_precision( 7) == share_type(  10*_m) );
   BOOST_CHECK( asset::scaled_precision( 8) == share_type( 100*_m) );
   BOOST_CHECK( asset::scaled_precision( 9) == share_type(   1*_g) );
   BOOST_CHECK( asset::scaled_precision(10) == share_type(  10*_g) );
   BOOST_CHECK( asset::scaled_precision(11) == share_type( 100*_g) );
   BOOST_CHECK( asset::scaled_precision(12) == share_type(   1*_t) );
   BOOST_CHECK( asset::scaled_precision(13) == share_type(  10*_t) );
   BOOST_CHECK( asset::scaled_precision(14) == share_type( 100*_t) );
   BOOST_CHECK( asset::scaled_precision(15) == share_type(   1*_p) );
   BOOST_CHECK( asset::scaled_precision(16) == share_type(  10*_p) );
   BOOST_CHECK( asset::scaled_precision(17) == share_type( 100*_p) );
   BOOST_CHECK( asset::scaled_precision(18) == share_type(   1*_e) );
   GRAPHENE_CHECK_THROW( asset::scaled_precision(19), fc::exception );
}

BOOST_AUTO_TEST_CASE( merkle_root )
{
   signed_block block;
   vector<processed_transaction> tx;
   vector<digest_type> t;
   const uint32_t num_tx = 10;

   for( uint32_t i=0; i<num_tx; i++ )
   {
      tx.emplace_back();
      tx.back().ref_block_prefix = i;
      t.push_back( tx.back().merkle_digest() );
   }

   auto c = []( const digest_type& digest ) -> checksum_type
   {   return checksum_type::hash( digest );   };
   
   auto d = []( const digest_type& left, const digest_type& right ) -> digest_type
   {   return digest_type::hash( std::make_pair( left, right ) );   };

   BOOST_CHECK( block.calculate_merkle_root() == checksum_type() );

   block.transactions.push_back( tx[0] );
   BOOST_CHECK( block.calculate_merkle_root() ==
      c(t[0])
      );

   digest_type dA, dB, dC, dD, dE, dI, dJ, dK, dM, dN, dO;

   /*
      A=d(0,1)
         / \ 
        0   1
   */

   dA = d(t[0], t[1]);

   block.transactions.push_back( tx[1] );
   BOOST_CHECK( block.calculate_merkle_root() == c(dA) );

   /*
            I=d(A,B)
           /        \
      A=d(0,1)      B=2
         / \        /
        0   1      2
   */

   dB = t[2];
   dI = d(dA, dB);

   block.transactions.push_back( tx[2] );
   BOOST_CHECK( block.calculate_merkle_root() == c(dI) );

   /*
          I=d(A,B)
           /    \
      A=d(0,1)   B=d(2,3)
         / \    /   \
        0   1  2     3
   */

   dB = d(t[2], t[3]);
   dI = d(dA, dB);

   block.transactions.push_back( tx[3] );
   BOOST_CHECK( block.calculate_merkle_root() == c(dI) );

   /*
                     __M=d(I,J)__
                    /            \
            I=d(A,B)              J=C
           /        \            /
      A=d(0,1)   B=d(2,3)      C=4
         / \        / \        /
        0   1      2   3      4
   */

   dC = t[4];
   dJ = dC;
   dM = d(dI, dJ);

   block.transactions.push_back( tx[4] );
   BOOST_CHECK( block.calculate_merkle_root() == c(dM) );

   /*
                     __M=d(I,J)__
                    /            \
            I=d(A,B)              J=C
           /        \            /
      A=d(0,1)   B=d(2,3)   C=d(4,5)
         / \        / \        / \
        0   1      2   3      4   5
   */

   dC = d(t[4], t[5]);
   dJ = dC;
   dM = d(dI, dJ);

   block.transactions.push_back( tx[5] );
   BOOST_CHECK( block.calculate_merkle_root() == c(dM) );

   /*
                     __M=d(I,J)__
                    /            \
            I=d(A,B)              J=d(C,D)
           /        \            /        \
      A=d(0,1)   B=d(2,3)   C=d(4,5)      D=6
         / \        / \        / \        /
        0   1      2   3      4   5      6
   */

   dD = t[6];
   dJ = d(dC, dD);
   dM = d(dI, dJ);

   block.transactions.push_back( tx[6] );
   BOOST_CHECK( block.calculate_merkle_root() == c(dM) );

   /*
                     __M=d(I,J)__
                    /            \
            I=d(A,B)              J=d(C,D)
           /        \            /        \
      A=d(0,1)   B=d(2,3)   C=d(4,5)   D=d(6,7)
         / \        / \        / \        / \
        0   1      2   3      4   5      6   7
   */

   dD = d(t[6], t[7]);
   dJ = d(dC, dD);
   dM = d(dI, dJ);

   block.transactions.push_back( tx[7] );
   BOOST_CHECK( block.calculate_merkle_root() == c(dM) );

   /*
                                _____________O=d(M,N)______________
                               /                                   \   
                     __M=d(I,J)__                                  N=K
                    /            \                              /
            I=d(A,B)              J=d(C,D)                 K=E
           /        \            /        \            /
      A=d(0,1)   B=d(2,3)   C=d(4,5)   D=d(6,7)      E=8
         / \        / \        / \        / \        /
        0   1      2   3      4   5      6   7      8
   */

   dE = t[8];
   dK = dE;
   dN = dK;
   dO = d(dM, dN);

   block.transactions.push_back( tx[8] );
   BOOST_CHECK( block.calculate_merkle_root() == c(dO) );

   /*
                                _____________O=d(M,N)______________
                               /                                   \   
                     __M=d(I,J)__                                  N=K
                    /            \                              /
            I=d(A,B)              J=d(C,D)                 K=E
           /        \            /        \            /
      A=d(0,1)   B=d(2,3)   C=d(4,5)   D=d(6,7)   E=d(8,9)
         / \        / \        / \        / \        / \
        0   1      2   3      4   5      6   7      8   9
   */

   dE = d(t[8], t[9]);
   dK = dE;
   dN = dK;
   dO = d(dM, dN);

   block.transactions.push_back( tx[9] );
   BOOST_CHECK( block.calculate_merkle_root() == c(dO) );
}

/**
 * Reproduces https://github.com/bitshares/bitshares-core/issues/888 and tests fix for it.
 */
BOOST_AUTO_TEST_CASE( bitasset_feed_expiration_test )
{
   time_point_sec now = fc::time_point::now();

   asset_bitasset_data_object o;

   o.current_feed_publication_time = now - fc::hours(1);
   o.options.feed_lifetime_sec = std::numeric_limits<uint32_t>::max() - 1;

   BOOST_CHECK( !o.feed_is_expired( now ) );
}

BOOST_AUTO_TEST_SUITE_END()
