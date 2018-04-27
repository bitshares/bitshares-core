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

#include <vector>
#include <boost/test/unit_test.hpp>

#include <graphene/chain/database.hpp>
#include <graphene/chain/hardfork.hpp>

#include <graphene/chain/balance_object.hpp>
#include <graphene/chain/budget_record_object.hpp>
#include <graphene/chain/committee_member_object.hpp>
#include <graphene/chain/market_object.hpp>
#include <graphene/chain/withdraw_permission_object.hpp>
#include <graphene/chain/witness_object.hpp>
#include <graphene/chain/worker_object.hpp>

#include <graphene/utilities/tempdir.hpp>

#include <fc/crypto/digest.hpp>

#include "../common/database_fixture.hpp"

using namespace graphene::chain;
using namespace graphene::chain::test;

BOOST_FIXTURE_TEST_SUITE( bitasset_tests, database_fixture )

BOOST_AUTO_TEST_CASE( reset_non_witness_asset )
{
   ACTORS((nathan)(dan)(ben)(vikram));

   BOOST_TEST_MESSAGE("Create USDBIT");
   asset_id_type bit_usd_id = create_bitasset("USDBIT").id;
   asset_id_type core_id = bit_usd_id(db).bitasset_data(db).options.short_backing_asset;
   {
      BOOST_TEST_MESSAGE("Update the USDBIT asset options");
      asset_update_operation op;
      const asset_object& obj = bit_usd_id(db);
      op.asset_to_update = bit_usd_id;
      op.issuer = obj.issuer;
      op.new_issuer = nathan_id;
      op.new_options = obj.options;
      op.new_options.flags &= ~witness_fed_asset;
      trx.operations.push_back(op);
      sign( trx, nathan_private_key );
      PUSH_TX( db, trx, ~0 );
      generate_block();
      trx.clear();
   }

   BOOST_TEST_MESSAGE("Create JMJBIT based on USDBIT.");
   asset_id_type bit_jmj_id = create_bitasset("JMJBIT").id;
   {
      BOOST_TEST_MESSAGE("Update the JMJBIT asset options");
      asset_update_operation op;
      const asset_object& obj = bit_jmj_id(db);
      op.asset_to_update = bit_jmj_id;
      op.issuer = obj.issuer;
      op.new_issuer = nathan_id;
      op.new_options = obj.options;
      op.new_options.flags &= ~(witness_fed_asset | committee_fed_asset);
      trx.operations.push_back(op);
      sign(trx, nathan_private_key);
      PUSH_TX( db, trx, ~0);
      generate_block();
      trx.clear();
   }
   {
      BOOST_TEST_MESSAGE("Update the JMJBIT bitasset options");
      asset_update_bitasset_operation ba_op;
      const asset_object& obj = bit_jmj_id(db);
      ba_op.asset_to_update = obj.get_id();
      ba_op.issuer = obj.issuer;
      ba_op.new_options.short_backing_asset = bit_usd_id;
      trx.operations.push_back(ba_op);
      sign(trx, nathan_private_key);
      PUSH_TX(db, trx, ~0);
      generate_block();
      trx.clear();
   }
   {
      BOOST_TEST_MESSAGE("Set feed producers for JMJBIT");
      asset_update_feed_producers_operation op;
      op.asset_to_update = bit_jmj_id;
      op.issuer = nathan_id;
      op.new_feed_producers = {dan_id, ben_id, vikram_id};
      trx.operations.push_back(op);
      sign( trx, nathan_private_key );
      PUSH_TX( db, trx, ~0 );
      generate_block();
      trx.clear();
   }
   {
      BOOST_TEST_MESSAGE("Verify feed producers are registered for JMJBIT");
      const asset_bitasset_data_object& obj = bit_jmj_id(db).bitasset_data(db);
      BOOST_CHECK_EQUAL(obj.feeds.size(), 3);
      BOOST_CHECK(obj.current_feed == price_feed());


      BOOST_CHECK_EQUAL("1", std::to_string(obj.options.short_backing_asset.space_id));
      BOOST_CHECK_EQUAL("3", std::to_string(obj.options.short_backing_asset.type_id));
      BOOST_CHECK_EQUAL("1", std::to_string(obj.options.short_backing_asset.instance.value));

      BOOST_CHECK_EQUAL("1", std::to_string(bit_jmj_id.space_id));
      BOOST_CHECK_EQUAL("3", std::to_string(bit_jmj_id.type_id));
      BOOST_CHECK_EQUAL("2", std::to_string(bit_jmj_id.instance.value));
   }
   {
      BOOST_TEST_MESSAGE("Adding Vikram's price feed");
      const asset_object& bit_jmj = bit_jmj_id(db);
      const asset_object& bit_usd = bit_usd_id(db);
      asset_publish_feed_operation op;
      op.publisher = vikram_id;
      op.asset_id = bit_jmj_id;
      op.feed.settlement_price = op.feed.core_exchange_rate = ~price(bit_usd.amount(1),bit_jmj.amount(30));
      trx.operations.push_back(op);
      PUSH_TX( db, trx, ~0 );

      BOOST_TEST_MESSAGE("Adding Ben's pricing to JMJBIT");
      const asset_bitasset_data_object& bitasset = bit_jmj_id(db).bitasset_data(db);
      op.publisher = ben_id;
      op.feed.settlement_price = op.feed.core_exchange_rate = ~price(bit_usd.amount(1),bit_jmj.amount(25));
      trx.operations.push_back(op);
      PUSH_TX( db, trx, ~0 );


      BOOST_CHECK_EQUAL(bitasset.current_feed.settlement_price.to_real(), 30.0 / 1.0);
      BOOST_CHECK(bitasset.current_feed.maintenance_collateral_ratio == GRAPHENE_DEFAULT_MAINTENANCE_COLLATERAL_RATIO);

      BOOST_TEST_MESSAGE("Adding Dan's pricing to JMJBIT");
      op.publisher = dan_id;
      op.feed.settlement_price = op.feed.core_exchange_rate = ~price(bit_usd.amount(1),bit_jmj.amount(40));
      op.feed.maximum_short_squeeze_ratio = 1001;
      op.feed.maintenance_collateral_ratio = 1001;
      trx.operations.push_back(op);
      PUSH_TX( db, trx, ~0 );

      BOOST_CHECK_EQUAL(bitasset.current_feed.settlement_price.to_real(), 30.0 / 1.0);
      BOOST_CHECK(bitasset.current_feed.maintenance_collateral_ratio == GRAPHENE_DEFAULT_MAINTENANCE_COLLATERAL_RATIO);
      generate_block();
      trx.clear();

      BOOST_CHECK(bitasset.current_feed.core_exchange_rate.base.asset_id != bitasset.current_feed.core_exchange_rate.quote.asset_id);
   }
   {
      BOOST_TEST_MESSAGE("Change underlying asset of bit_jmj from bit_usd to core");
      asset_update_bitasset_operation ba_op;
      const asset_object& obj = bit_jmj_id(db);
      ba_op.asset_to_update = bit_jmj_id;
      ba_op.issuer = obj.issuer;
      ba_op.new_options.short_backing_asset = core_id;
      trx.operations.push_back(ba_op);
      sign(trx, nathan_private_key);
      PUSH_TX(db, trx, ~0);
      generate_block();
      trx.clear();

      BOOST_TEST_MESSAGE("Verify feed producers have been reset");
      const asset_bitasset_data_object& jmj_obj = bit_jmj_id(db).bitasset_data(db);
      BOOST_CHECK_EQUAL(jmj_obj.feeds.size(), 3);
      for(auto feed : jmj_obj.feeds) {
         BOOST_CHECK(std::isnan(feed.second.second.settlement_price.to_real()));
      }
   }
   {
      BOOST_TEST_MESSAGE("With underlying bitasset changed from one to another, price feeds should still be publish-able");
      BOOST_TEST_MESSAGE("Adding Vikram's price feed");
      const asset_object& bit_jmj = bit_jmj_id(db);
      const asset_object& core = core_id(db);
      asset_publish_feed_operation op;
      op.publisher = vikram_id;
      op.asset_id = bit_jmj_id;
      op.feed.settlement_price = op.feed.core_exchange_rate = ~price(core.amount(1),bit_jmj.amount(30));
      trx.operations.push_back(op);
      PUSH_TX( db, trx, ~0 );

      BOOST_TEST_MESSAGE("Adding Ben's pricing to JMJBIT");
      const asset_bitasset_data_object& bitasset = bit_jmj_id(db).bitasset_data(db);
      op.publisher = ben_id;
      op.feed.settlement_price = op.feed.core_exchange_rate = ~price(core.amount(1),bit_jmj.amount(25));
      trx.operations.push_back(op);
      PUSH_TX( db, trx, ~0 );

      BOOST_CHECK_EQUAL(bitasset.current_feed.settlement_price.to_real(), 25.0 / 1.0);
      BOOST_CHECK(bitasset.current_feed.maintenance_collateral_ratio == GRAPHENE_DEFAULT_MAINTENANCE_COLLATERAL_RATIO);

      BOOST_TEST_MESSAGE("Adding Dan's pricing to JMJBIT");
      op.publisher = dan_id;
      op.feed.settlement_price = op.feed.core_exchange_rate = ~price(core.amount(1),bit_jmj.amount(40));
      op.feed.maximum_short_squeeze_ratio = 1001;
      op.feed.maintenance_collateral_ratio = 1001;
      trx.operations.push_back(op);
      PUSH_TX( db, trx, ~0 );

      BOOST_CHECK_EQUAL(bitasset.current_feed.settlement_price.to_real(), 30.0 / 1.0);
      BOOST_CHECK(bitasset.current_feed.maintenance_collateral_ratio == GRAPHENE_DEFAULT_MAINTENANCE_COLLATERAL_RATIO);
      generate_block();
      trx.clear();

      BOOST_CHECK(bitasset.current_feed.core_exchange_rate.base.asset_id != bitasset.current_feed.core_exchange_rate.quote.asset_id);
   }
}

BOOST_AUTO_TEST_SUITE_END()
