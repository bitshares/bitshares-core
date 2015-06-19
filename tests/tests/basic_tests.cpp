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
#include <graphene/chain/operations.hpp>

#include <graphene/chain/account_object.hpp>
#include <graphene/chain/asset_object.hpp>
#include <graphene/chain/key_object.hpp>
#include <graphene/chain/delegate_object.hpp>
#include <graphene/chain/witness_scheduler_rng.hpp>

#include <graphene/db/simple_index.hpp>

#include <fc/crypto/digest.hpp>
#include <fc/crypto/hex.hpp>
#include "../common/database_fixture.hpp"

#include <algorithm>
#include <random>

using namespace graphene::chain;
using namespace graphene::db;

BOOST_FIXTURE_TEST_SUITE( basic_tests, database_fixture )

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

BOOST_AUTO_TEST_CASE( serialization_tests )
{
   key_object k;
   k.id = object_id<protocol_ids, key_object_type>(unsigned_int(2));
   BOOST_CHECK(fc::json::from_string(fc::json::to_string(k.id)).as<key_id_type>() == k.id);
   BOOST_CHECK(fc::json::from_string(fc::json::to_string(k.id)).as<object_id_type>() == k.id);
   BOOST_CHECK((fc::json::from_string(fc::json::to_string(k.id)).as<object_id<protocol_ids, key_object_type>>() == k.id));
   public_key_type public_key = fc::ecc::private_key::generate().get_public_key();
   k.key_data = address(public_key);
   BOOST_CHECK(k.key_address() == address(public_key));
}

BOOST_AUTO_TEST_CASE( memo_test )
{ try {
   memo_data m;
   auto sender = generate_private_key("1");
   auto receiver = generate_private_key("2");
   m.from = 1;
   m.to = 2;
   m.set_message(sender, receiver.get_public_key(), "Hello, world!");
   BOOST_CHECK_EQUAL(m.get_message(receiver, sender.get_public_key()), "Hello, world!");
} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE( witness_rng_test_bits )
{
   try
   {
      const uint64_t COUNT = 131072;
      const uint64_t HASH_SIZE = 32;
      string ref_bits = "";
      ref_bits.reserve( COUNT * HASH_SIZE );
      static const char seed_data[] = "\xe3\xb0\xc4\x42\x98\xfc\x1c\x14\x9a\xfb\xf4\xc8\x99\x6f\xb9\x24\x27\xae\x41\xe4\x64\x9b\x93\x4c\xa4\x95\x99\x1b\x78\x52\xb8\x55";

      for( uint64_t i=0; i<COUNT; i++ )
      {
         // grab the bits
         fc::sha256::encoder enc;
         enc.write( seed_data, HASH_SIZE );
         enc.put( char((i        ) & 0xFF) );
         enc.put( char((i >> 0x08) & 0xFF) );
         enc.put( char((i >> 0x10) & 0xFF) );
         enc.put( char((i >> 0x18) & 0xFF) );
         enc.put( char((i >> 0x20) & 0xFF) );
         enc.put( char((i >> 0x28) & 0xFF) );
         enc.put( char((i >> 0x30) & 0xFF) );
         enc.put( char((i >> 0x38) & 0xFF) );

         fc::sha256 result = enc.result();
         auto result_data = result.data();
         std::copy( result_data, result_data+HASH_SIZE, std::back_inserter( ref_bits ) );
      }

      fc::sha256 seed = fc::sha256::hash( string("") );
      // seed = sha256("") = e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855
      BOOST_CHECK( memcmp( seed.data(), seed_data, HASH_SIZE ) == 0 );

      hash_ctr_rng< fc::sha256, 32 > test_rng(seed.data(), 0);
      // python2 -c 'import hashlib; import struct; h = lambda x : hashlib.sha256(x).digest(); i = lambda x : struct.pack("<Q", x); print( h( h("") + i(0) ) )' | hd
      string ref_bits_hex =
          "5c5d42dcf39f71c0226ca720d8d518db615b5773f038e5e491963f6f47621bbd"   // h( h("") + i(0) )
          "43fd6dae047c400060be262e6d443200eacd1fafcb77828638085c2e2341fd8d"   // h( h("") + i(1) )
          "d666330a7441dc7279b786e65aba32817275989cfc691b3901f000fb0f14cd05"   // h( h("") + i(2) )
          "34bd93f83d7bac4a667d62fee39bd5eb1991fbadc29a5f216ea746772ca31544"   // h( h("") + i(3) )
          "d3b41a093eab01cd25f987a909b2f4812b0f38475e0fe40f6f42a12c6e018aa7"   // ...
          "c8db17b946c5a6bceaa7b903c93e6ccb8cc6c09b0cfd2108d930de1a79c3a68e"
          "cc1945b36c82e356b6d127057d036a150cb03b760e9c9e706c560f32a749e80d"
          "872b28fe97e289d4f6f361f3427d454113e3b513892d129398dac4daf8a0e43e"
          "8d7a5a2f3cbb245fa471e87e30a38d9c775c985c28db6e521e34cf1e88507c26"
          "c662f230eed0f10899c3a74a2d1bfb88d732909b206a2aed3ae0bda728fac8fe"
          "38eface8b1d473e45cbb40603bcef8bf2219e55669c7a2cfb5f8d52610689f14"
          "3b1d1734273b069a7de7cc6dd2e80db09d1feff200c9bdaf033cd553ea40e05d"
          "16653ca7aa7f790a95c6a8d41e5694b0c6bff806c3ce3e0e320253d408fb6f27"
          "b55df71d265de0b86a1cdf45d1d9c53da8ebf0ceec136affa12228d0d372e698"
          "37e9305ce57d386d587039b49b67104fd4d8467e87546237afc9a90cf8c677f9"
          "fc26784c94f754cf7aeacb6189e705e2f1873ea112940560f11dbbebb22a8922"
          ;
      char* ref_bits_chars = new char[ ref_bits_hex.length() / 2 ];
      fc::from_hex( ref_bits_hex, ref_bits_chars, ref_bits_hex.length() / 2 );
      string ref_bits_str( ref_bits_chars, ref_bits_hex.length() / 2 );
      delete[] ref_bits_chars;
      ref_bits_chars = nullptr;

      BOOST_CHECK( ref_bits_str.length() < ref_bits.length() );
      BOOST_CHECK( ref_bits_str == ref_bits.substr( 0, ref_bits_str.length() ) );
      //std::cout << "ref_bits_str: " << fc::to_hex( ref_bits_str.c_str(), std::min( ref_bits_str.length(), size_t(256) ) ) << "\n";
      //std::cout << "ref_bits    : " << fc::to_hex( ref_bits    .c_str(), std::min( ref_bits.length(), size_t(256) ) ) << "\n";

      // when we get to this point, our code to generate the RNG byte output is working.
      // now let's test get_bits() as follows:

      uint64_t ref_get_bits_offset = 0;

      auto ref_get_bits = [&]( uint8_t count ) -> uint64_t
      {
         uint64_t result = 0;
         uint64_t i = ref_get_bits_offset;
         uint64_t mask = 1;
         while( count > 0 )
         {
            if( ref_bits[ i >> 3 ] & (1 << (i & 7)) )
                result |= mask;
            mask += mask;
            i++;
            count--;
         }
         ref_get_bits_offset = i;
         return result;
      };

      // use PRNG to decide between 0-64 bits
      std::minstd_rand rng;
      rng.seed( 9999 );
      std::uniform_int_distribution< uint16_t > bit_dist( 0, 64 );
      for( int i=0; i<10000; i++ )
      {
         uint8_t bit_count = bit_dist( rng );
         uint64_t ref_bits = ref_get_bits( bit_count );
         uint64_t test_bits = test_rng.get_bits( bit_count );
         //std::cout << i << ": get(" << int(bit_count) << ") -> " << test_bits << " (expect " << ref_bits << ")\n";
         if( bit_count < 64 )
         {
            BOOST_CHECK( ref_bits  < (uint64_t( 1 ) << bit_count ) );
            BOOST_CHECK( test_bits < (uint64_t( 1 ) << bit_count ) );
         }
         BOOST_CHECK( ref_bits == test_bits );
         if( ref_bits != test_bits )
            break;
      }

      std::uniform_int_distribution< uint64_t > whole_dist(
         0, std::numeric_limits<uint64_t>::max() );
      for( int i=0; i<10000; i++ )
      {
         uint8_t bit_count = bit_dist( rng );
         uint64_t bound = whole_dist( rng ) & ((uint64_t(1) << bit_count) - 1);
         //std::cout << "bound:" << bound << "\n";
         uint64_t rnum = test_rng( bound );
         //std::cout << "rnum:" << rnum << "\n";
         if( bound > 1 )
         {
            BOOST_CHECK( rnum < bound );
         }
         else
         {
            BOOST_CHECK( rnum == 0 );
         }
      }

   } FC_LOG_AND_RETHROW()
}

BOOST_AUTO_TEST_SUITE_END()
