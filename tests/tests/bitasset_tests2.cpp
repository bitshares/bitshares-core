/*
 * Copyright (c) 2018 Bitshares Foundation, and contributors.
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

#include <graphene/chain/account_object.hpp>

#include <fc/crypto/digest.hpp>

#include "../common/database_fixture.hpp"
#include <string>

BOOST_FIXTURE_TEST_SUITE( bitasset_tests, graphene::chain::database_fixture )

void change_backing_asset(graphene::chain::database_fixture& fixture, const fc::ecc::private_key& signing_key,
      const graphene::chain::asset_object& asset_to_update, graphene::chain::asset_id_type new_backing_asset_id)
{
   graphene::chain::asset_update_bitasset_operation ba_op;
   ba_op.asset_to_update = asset_to_update.get_id();
   ba_op.issuer = asset_to_update.issuer;
   ba_op.new_options.short_backing_asset = new_backing_asset_id;
   fixture.trx.operations.push_back(ba_op);
   fixture.sign(fixture.trx, signing_key);
   PUSH_TX(fixture.db, fixture.trx, ~0);
   fixture.generate_block();
   fixture.trx.clear();
}

const graphene::chain::asset_object& create_bitasset_backed(graphene::chain::database_fixture& fixture,
      int index, graphene::chain::asset_id_type backing, const fc::ecc::private_key& signing_key)
{
   // create the coin
   std::string name = "COIN" + std::to_string(index + 1) + "TEST";
   const graphene::chain::asset_object& obj = fixture.create_bitasset(name);
   // adjust the backing asset
   change_backing_asset(fixture, signing_key, obj, backing);
   fixture.trx.set_expiration(fixture.db.get_dynamic_global_properties().next_maintenance_time);
   return obj;
}

BOOST_AUTO_TEST_CASE( bitasset_secondary_index )
{
   ACTORS( (nathan) );

   graphene::chain::asset_id_type core_id;
   BOOST_TEST_MESSAGE("Create coins");
   try
   {
      // make 5 coins (backed by core)
      for(int i = 0; i < 5; i++)
      {
         create_bitasset_backed(*this, i, core_id, nathan_private_key);
      }
      // make the next 5 (10-14) be backed by COIN1
      graphene::chain::asset_id_type coin1_id = get_asset("COIN1TEST").get_id();
      for(int i = 5; i < 10; i++)
      {
         create_bitasset_backed(*this, i, coin1_id, nathan_private_key);
      }
      // make the next 5 (15-19) be backed by COIN2
      graphene::chain::asset_id_type coin2_id = get_asset("COIN2TEST").get_id();
      for(int i = 10; i < 15; i++)
      {
         create_bitasset_backed(*this, i, coin2_id, nathan_private_key);
      }
      // make the last 5 be backed by core
      for(int i = 15; i < 20; i++)
      {
         create_bitasset_backed(*this, i, core_id, nathan_private_key);
      }

      BOOST_TEST_MESSAGE("Searching for all coins backed by CORE");
      const auto& idx = db.get_index_type<graphene::chain::asset_bitasset_data_index>().indices().get<graphene::chain::by_short_backing_asset>();
      auto core_itr = idx.find( core_id );
      auto core_end = idx.upper_bound(core_id);
      BOOST_TEST_MESSAGE("Searching for all coins backed by COIN1");
      auto coin1_itr = idx.find( coin1_id );
      auto coin1_end = idx.upper_bound( coin1_id );
      BOOST_TEST_MESSAGE("Searching for all coins backed by COIN2");
      auto coin2_itr = idx.find( coin2_id );

      int core_count = 0, coin1_count = 0, coin2_count = 0;

      BOOST_TEST_MESSAGE("Counting coins in each category");

      for( ; core_itr != core_end; ++core_itr)
      {
         BOOST_CHECK(core_itr->options.short_backing_asset == core_id);
         core_count++;
      }
      for( ; coin1_itr != coin1_end; ++coin1_itr )
      {
         BOOST_CHECK(coin1_itr->options.short_backing_asset == coin1_id);
         coin1_count++;
      }
      for( ; coin2_itr != idx.end(); ++coin2_itr )
      {
         BOOST_CHECK(coin2_itr->options.short_backing_asset == coin2_id);
         coin2_count++;
      }

      BOOST_CHECK_EQUAL(core_count, 10);
      BOOST_CHECK_EQUAL(coin1_count, 5);
      BOOST_CHECK_EQUAL(coin2_count, 5);
   }
   catch (fc::exception& ex)
   {
      BOOST_FAIL(ex.to_string(fc::log_level(fc::log_level::all)));
   }
}

BOOST_AUTO_TEST_SUITE_END()
