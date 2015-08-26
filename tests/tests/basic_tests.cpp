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
   BOOST_CHECK( !is_valid_name( "a" ) );
   BOOST_CHECK( !is_valid_name( "A" ) );
   BOOST_CHECK( !is_valid_name( "0" ) );
   BOOST_CHECK( !is_valid_name( "." ) );
   BOOST_CHECK( !is_valid_name( "-" ) );

   BOOST_CHECK( !is_valid_name( "aa" ) );
   BOOST_CHECK( !is_valid_name( "aA" ) );
   BOOST_CHECK( !is_valid_name( "a0" ) );
   BOOST_CHECK( !is_valid_name( "a." ) );
   BOOST_CHECK( !is_valid_name( "a-" ) );

   BOOST_CHECK( is_valid_name( "aaa" ) );
   BOOST_CHECK( !is_valid_name( "aAa" ) );
   BOOST_CHECK( is_valid_name( "a0a" ) );
   BOOST_CHECK( !is_valid_name( "a.a" ) );
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
   BOOST_CHECK( !is_valid_name( "xn--sandmnnchen-p8a.de" ) );
   BOOST_CHECK(  is_valid_name( "xn--sandmnnchen-p8a.dex" ) );
   BOOST_CHECK( !is_valid_name( "xn-sandmnnchen-p8a.de" ) );
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
   BOOST_CHECK( !is_valid_symbol( "A0A" ) );
   BOOST_CHECK( is_valid_symbol( "A.A" ) );

   BOOST_CHECK( !is_valid_symbol( "A..A" ) );
   BOOST_CHECK( !is_valid_symbol( "A.A." ) );
   BOOST_CHECK( !is_valid_symbol( "A.A.A" ) );

   BOOST_CHECK( is_valid_symbol( "AAAAAAAAAAAAAAAA" ) );
   BOOST_CHECK( !is_valid_symbol( "AAAAAAAAAAAAAAAAA" ) );
   BOOST_CHECK( is_valid_symbol( "A.AAAAAAAAAAAAAA" ) );
   BOOST_CHECK( !is_valid_symbol( "A.AAAAAAAAAAAA.A" ) );
}

BOOST_AUTO_TEST_CASE( price_test )
{
    BOOST_CHECK( price::max(0,1) > price::min(0,1) );
    BOOST_CHECK( price::max(1,0) > price::min(1,0) );
    BOOST_CHECK( price::max(0,1) >= price::min(0,1) );
    BOOST_CHECK( price::max(1,0) >= price::min(1,0) );
    BOOST_CHECK( price::max(0,1) >= price::max(0,1) );
    BOOST_CHECK( price::max(1,0) >= price::max(1,0) );
    BOOST_CHECK( price::min(0,1) < price::max(0,1) );
    BOOST_CHECK( price::min(1,0) < price::max(1,0) );
    BOOST_CHECK( price::min(0,1) <= price::max(0,1) );
    BOOST_CHECK( price::min(1,0) <= price::max(1,0) );
    BOOST_CHECK( price::min(0,1) <= price::min(0,1) );
    BOOST_CHECK( price::min(1,0) <= price::min(1,0) );
    BOOST_CHECK( price::min(1,0) != price::max(1,0) );
    BOOST_CHECK( ~price::max(0,1) != price::min(0,1) );
    BOOST_CHECK( ~price::min(0,1) != price::max(0,1) );
    BOOST_CHECK( ~price::max(0,1) == price::min(1,0) );
    BOOST_CHECK( ~price::min(0,1) == price::max(1,0) );
    BOOST_CHECK( ~price::max(0,1) < ~price::min(0,1) );
    BOOST_CHECK( ~price::max(0,1) <= ~price::min(0,1) );
    price a(asset(1), asset(2,1));
    price b(asset(2), asset(2,1));
    price c(asset(1), asset(2,1));
    BOOST_CHECK(a < b);
    BOOST_CHECK(b > a);
    BOOST_CHECK(a == c);
    BOOST_CHECK(!(b == c));

    price_feed dummy;
    dummy.maintenance_collateral_ratio = 1002;
    dummy.maximum_short_squeeze_ratio = 1234;
    dummy.settlement_price = price(asset(1000), asset(2000, 1));
    price_feed dummy2 = dummy;
    BOOST_CHECK(dummy == dummy2);
}

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

BOOST_AUTO_TEST_SUITE_END()
