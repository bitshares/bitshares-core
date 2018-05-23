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

/*****
 * @brief helper method to change a backing asset to a new one
 * @param fixture the database_fixture
 * @param signing_key the signer
 * @param asset_id_to_update asset to update
 * @param new_backing_asset_id the new backing asset
 */
void change_backing_asset(database_fixture& fixture, const fc::ecc::private_key& signing_key,
      asset_id_type asset_id_to_update, asset_id_type new_backing_asset_id)
{
   asset_update_bitasset_operation ba_op;
   const asset_object& asset_to_update = asset_id_to_update(fixture.db);
   ba_op.asset_to_update = asset_id_to_update;
   ba_op.issuer = asset_to_update.issuer;
   ba_op.new_options.short_backing_asset = new_backing_asset_id;
   fixture.trx.operations.push_back(ba_op);
   fixture.sign(fixture.trx, signing_key);
   PUSH_TX(fixture.db, fixture.trx, ~0);
   fixture.generate_block();
   fixture.trx.clear();
}

/******
 * @ brief helper method to turn witness_fed_asset on and off
 * @param fixture the database_fixture
 * @param new_issuer optionally change the issuer
 * @param signing_key signer
 * @param asset_id asset we want to change
 * @param witness_fed true if you want this to be a witness fed asset
 */
void change_asset_options(database_fixture& fixture, const optional<account_id_type>& new_issuer,
      const fc::ecc::private_key& signing_key,
      asset_id_type asset_id, bool witness_fed)
{
   asset_update_operation op;
   const asset_object& obj = asset_id(fixture.db);
   op.asset_to_update = asset_id;
   op.issuer = obj.issuer;
   if (new_issuer)
      op.new_issuer = new_issuer;
   op.new_options = obj.options;
   if (witness_fed)
   {
      op.new_options.flags |= witness_fed_asset;
      op.new_options.flags &= ~committee_fed_asset;
   }
   else
   {
      op.new_options.flags &= ~witness_fed_asset; // we don't care about the committee flag here
   }
   fixture.trx.operations.push_back(op);
   fixture.sign( fixture.trx, signing_key );
   PUSH_TX( fixture.db, fixture.trx, ~0 );
   fixture.generate_block();
   fixture.trx.clear();

}

/*********
 * @brief make sure feeds still work after changing backing asset on a witness-fed asset
 */
BOOST_AUTO_TEST_CASE( reset_backing_asset_on_witness_asset )
{
   ACTORS((nathan));

   /*
       // do a maintenance block
      generate_blocks(db.get_dynamic_global_properties().next_maintenance_time);
      // generate blocks until close to hard fork
      generate_blocks( HARDFORK_CORE_868_890_TIME - fc::hours(1) );
    */

   BOOST_TEST_MESSAGE("Advance to near hard fork");
   auto maint_interval = db.get_global_properties().parameters.maintenance_interval;
   generate_blocks( HARDFORK_CORE_868_890_TIME - maint_interval);
   trx.set_expiration(HARDFORK_CORE_868_890_TIME - fc::seconds(1));

   BOOST_TEST_MESSAGE("Create USDBIT");
   asset_id_type bit_usd_id = create_bitasset("USDBIT").id;
   asset_id_type core_id = bit_usd_id(db).bitasset_data(db).options.short_backing_asset;

   {
      BOOST_TEST_MESSAGE("Update the USDBIT asset options");
      change_asset_options(*this, nathan_id, nathan_private_key, bit_usd_id, false );
   }

   BOOST_TEST_MESSAGE("Create JMJBIT based on USDBIT.");
   asset_id_type bit_jmj_id = create_bitasset("JMJBIT").id;
   {
      BOOST_TEST_MESSAGE("Update the JMJBIT asset options");
      change_asset_options(*this, nathan_id, nathan_private_key, bit_jmj_id, true );
   }
   {
      BOOST_TEST_MESSAGE("Update the JMJBIT bitasset options");
      asset_update_bitasset_operation ba_op;
      const asset_object& obj = bit_jmj_id(db);
      ba_op.asset_to_update = obj.get_id();
      ba_op.issuer = obj.issuer;
      ba_op.new_options.short_backing_asset = bit_usd_id;
      ba_op.new_options.minimum_feeds = 1;
      trx.operations.push_back(ba_op);
      sign(trx, nathan_private_key);
      PUSH_TX(db, trx, ~0);
      generate_block();
      trx.clear();
   }

   BOOST_TEST_MESSAGE("Grab active witnesses");
   auto& global_props = db.get_global_properties();
   std::vector<account_id_type> active_witnesses;
   for(const witness_id_type& wit_id : global_props.active_witnesses)
      active_witnesses.push_back(wit_id(db).witness_account);
   BOOST_REQUIRE_EQUAL(active_witnesses.size(), 10lu);

   {
      BOOST_TEST_MESSAGE("Adding price feed 1");
      publish_feed(active_witnesses[0], bit_usd_id, 1, bit_jmj_id, 300, core_id);

      const asset_bitasset_data_object& bitasset = bit_jmj_id(db).bitasset_data(db);
      BOOST_CHECK_EQUAL(bitasset.current_feed.settlement_price.to_real(), 300.0);
      BOOST_CHECK(bitasset.current_feed.maintenance_collateral_ratio == GRAPHENE_DEFAULT_MAINTENANCE_COLLATERAL_RATIO);
   }
   {
      BOOST_TEST_MESSAGE("Adding price feed 2");
      publish_feed(active_witnesses[1], bit_usd_id, 1, bit_jmj_id, 100, core_id);

      const asset_bitasset_data_object& bitasset = bit_jmj_id(db).bitasset_data(db);
      BOOST_CHECK_EQUAL(bitasset.current_feed.settlement_price.to_real(), 300.0);
      BOOST_CHECK(bitasset.current_feed.maintenance_collateral_ratio == GRAPHENE_DEFAULT_MAINTENANCE_COLLATERAL_RATIO);
   }
   {
      BOOST_TEST_MESSAGE("Adding price feed 3");
      publish_feed(active_witnesses[2], bit_usd_id, 1, bit_jmj_id, 1, core_id);

      const asset_bitasset_data_object& bitasset = bit_jmj_id(db).bitasset_data(db);
      BOOST_CHECK_EQUAL(bitasset.current_feed.settlement_price.to_real(), 100.0);
      BOOST_CHECK(bitasset.current_feed.maintenance_collateral_ratio == GRAPHENE_DEFAULT_MAINTENANCE_COLLATERAL_RATIO);
   }
   {
      BOOST_TEST_MESSAGE("Change underlying asset of bit_jmj from bit_usd to core");
      change_backing_asset(*this, nathan_private_key, bit_jmj_id, core_id);

      BOOST_TEST_MESSAGE("Verify feed producers have not been reset");
      const asset_bitasset_data_object& jmj_obj = bit_jmj_id(db).bitasset_data(db);
      BOOST_CHECK_EQUAL(jmj_obj.feeds.size(), 3ul);
   }
   {
      BOOST_TEST_MESSAGE("With underlying bitasset changed from one to another, price feeds should still be publish-able");
      BOOST_TEST_MESSAGE("Re-Adding Witness 1 price feed");
      publish_feed(active_witnesses[0], core_id, 1, bit_jmj_id, 30, core_id);

      const asset_bitasset_data_object& bitasset = bit_jmj_id(db).bitasset_data(db);
      BOOST_CHECK_EQUAL(bitasset.current_feed.settlement_price.to_real(), 1);
      BOOST_CHECK(bitasset.current_feed.maintenance_collateral_ratio == GRAPHENE_DEFAULT_MAINTENANCE_COLLATERAL_RATIO);

      BOOST_CHECK(bitasset.current_feed.core_exchange_rate.base.asset_id != bitasset.current_feed.core_exchange_rate.quote.asset_id);
   }
   {
      BOOST_TEST_MESSAGE("Re-Adding Witness 2 price feed");
      publish_feed(active_witnesses[1], core_id, 1, bit_jmj_id, 100, core_id);

      const asset_bitasset_data_object& bitasset = bit_jmj_id(db).bitasset_data(db);
      BOOST_CHECK_EQUAL(bitasset.current_feed.settlement_price.to_real(), 100);
      BOOST_CHECK(bitasset.current_feed.maintenance_collateral_ratio == GRAPHENE_DEFAULT_MAINTENANCE_COLLATERAL_RATIO);
   }
   {
      BOOST_TEST_MESSAGE("Advance to after hard fork");
      generate_blocks( HARDFORK_CORE_868_890_TIME + fc::seconds(1));
      trx.set_expiration(HARDFORK_CORE_868_890_TIME + fc::hours(2));

      BOOST_TEST_MESSAGE("After hardfork, 1 feed should have been erased");
      const asset_bitasset_data_object& jmj_obj = bit_jmj_id(db).bitasset_data(db);
      BOOST_CHECK_EQUAL(jmj_obj.feeds.size(), 2ul);
   }
   {
      BOOST_TEST_MESSAGE("After hardfork, change underlying asset of bit_jmj from core to bit_usd");
      change_backing_asset(*this, nathan_private_key, bit_jmj_id, bit_usd_id);

      BOOST_TEST_MESSAGE("Verify feed producers have been reset");
      const asset_bitasset_data_object& jmj_obj = bit_jmj_id(db).bitasset_data(db);
      BOOST_CHECK_EQUAL(jmj_obj.feeds.size(), 0ul);
   }
   {
      BOOST_TEST_MESSAGE("With underlying bitasset changed from one to another, price feeds should still be publish-able");
      BOOST_TEST_MESSAGE("Re-Adding Witness 1 price feed");
      publish_feed(active_witnesses[0], bit_usd_id, 1, bit_jmj_id, 30, core_id);

      const asset_bitasset_data_object& bitasset = bit_jmj_id(db).bitasset_data(db);
      BOOST_CHECK_EQUAL(bitasset.current_feed.settlement_price.to_real(), 30);
      BOOST_CHECK(bitasset.current_feed.maintenance_collateral_ratio == GRAPHENE_DEFAULT_MAINTENANCE_COLLATERAL_RATIO);

      BOOST_CHECK(bitasset.current_feed.core_exchange_rate.base.asset_id != bitasset.current_feed.core_exchange_rate.quote.asset_id);
   }
}

/****
 * @brief make sure feeds work correctly after changing the backing asset on a non-witness-fed asset
 */
BOOST_AUTO_TEST_CASE( reset_backing_asset_on_non_witness_asset )
{
   ACTORS((nathan)(dan)(ben)(vikram));

   BOOST_TEST_MESSAGE("Advance to near hard fork");
   auto maint_interval = db.get_global_properties().parameters.maintenance_interval;
   generate_blocks( HARDFORK_CORE_868_890_TIME - maint_interval);
   trx.set_expiration(HARDFORK_CORE_868_890_TIME - fc::seconds(1));


   BOOST_TEST_MESSAGE("Create USDBIT");
   asset_id_type bit_usd_id = create_bitasset("USDBIT").id;
   asset_id_type core_id = bit_usd_id(db).bitasset_data(db).options.short_backing_asset;

   {
      BOOST_TEST_MESSAGE("Update the USDBIT asset options");
      change_asset_options(*this, nathan_id, nathan_private_key, bit_usd_id, false );
   }

   BOOST_TEST_MESSAGE("Create JMJBIT based on USDBIT.");
   asset_id_type bit_jmj_id = create_bitasset("JMJBIT").id;
   {
      BOOST_TEST_MESSAGE("Update the JMJBIT asset options");
      change_asset_options(*this, nathan_id, nathan_private_key, bit_jmj_id, false );
   }
   {
      BOOST_TEST_MESSAGE("Update the JMJBIT bitasset options");
      asset_update_bitasset_operation ba_op;
      const asset_object& obj = bit_jmj_id(db);
      ba_op.asset_to_update = obj.get_id();
      ba_op.issuer = obj.issuer;
      ba_op.new_options.short_backing_asset = bit_usd_id;
      ba_op.new_options.minimum_feeds = 1;
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
      BOOST_CHECK_EQUAL(obj.feeds.size(), 3ul);
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
      publish_feed(vikram_id, bit_usd_id, 1, bit_jmj_id, 300, core_id);

      const asset_bitasset_data_object& bitasset = bit_jmj_id(db).bitasset_data(db);
      BOOST_CHECK_EQUAL(bitasset.current_feed.settlement_price.to_real(), 300.0);
      BOOST_CHECK(bitasset.current_feed.maintenance_collateral_ratio == GRAPHENE_DEFAULT_MAINTENANCE_COLLATERAL_RATIO);
   }
   {
      BOOST_TEST_MESSAGE("Adding Ben's pricing to JMJBIT");
      publish_feed(ben_id, bit_usd_id, 1, bit_jmj_id, 100, core_id);

      const asset_bitasset_data_object& bitasset = bit_jmj_id(db).bitasset_data(db);
      BOOST_CHECK_EQUAL(bitasset.current_feed.settlement_price.to_real(), 300);
      BOOST_CHECK(bitasset.current_feed.maintenance_collateral_ratio == GRAPHENE_DEFAULT_MAINTENANCE_COLLATERAL_RATIO);
   }
   {
      BOOST_TEST_MESSAGE("Adding Dan's pricing to JMJBIT");
      publish_feed(dan_id, bit_usd_id, 1, bit_jmj_id, 1, core_id);

      const asset_bitasset_data_object& bitasset = bit_jmj_id(db).bitasset_data(db);
      BOOST_CHECK_EQUAL(bitasset.current_feed.settlement_price.to_real(), 100);
      BOOST_CHECK(bitasset.current_feed.maintenance_collateral_ratio == GRAPHENE_DEFAULT_MAINTENANCE_COLLATERAL_RATIO);
      generate_block();
      trx.clear();

      BOOST_CHECK(bitasset.current_feed.core_exchange_rate.base.asset_id != bitasset.current_feed.core_exchange_rate.quote.asset_id);
   }
   {
      BOOST_TEST_MESSAGE("Change underlying asset of bit_jmj from bit_usd to core");
      change_backing_asset(*this, nathan_private_key, bit_jmj_id, core_id);

      BOOST_TEST_MESSAGE("Verify feed producers have not been reset");
      const asset_bitasset_data_object& jmj_obj = bit_jmj_id(db).bitasset_data(db);
      BOOST_CHECK_EQUAL(jmj_obj.feeds.size(), 3ul);
      for(const auto& feed : jmj_obj.feeds) {
         BOOST_CHECK(!feed.second.second.settlement_price.is_null());
      }
   }
   {
      BOOST_TEST_MESSAGE("Add a new (and correct) feed price for 1 feed producer");
      publish_feed(vikram_id, core_id, 1, bit_jmj_id, 300, core_id);
   }
   {
      BOOST_TEST_MESSAGE("Advance to past hard fork");
      generate_blocks( HARDFORK_CORE_868_890_TIME + maint_interval);
      trx.set_expiration(HARDFORK_CORE_868_890_TIME + fc::hours(48));

      BOOST_TEST_MESSAGE("Verify that the incorrect feeds have been corrected");
      const asset_bitasset_data_object& jmj_obj = bit_jmj_id(db).bitasset_data(db);
      BOOST_CHECK_EQUAL(jmj_obj.feeds.size(), 3ul);
      int nan_count = 0;
      for(const auto& feed : jmj_obj.feeds)
      {
         if (feed.second.second.settlement_price.is_null())
            nan_count++;
      }
      BOOST_CHECK_EQUAL(nan_count, 2);
      // the settlement price will be NaN until 50% of price feeds are valid
      //BOOST_CHECK_EQUAL(jmj_obj.current_feed.settlement_price.to_real(), 300);
   }
   {
      BOOST_TEST_MESSAGE("After hardfork, change underlying asset of bit_jmj from core to bit_usd");
      change_backing_asset(*this, nathan_private_key, bit_jmj_id, bit_usd_id);

      BOOST_TEST_MESSAGE("Verify feed producers have been reset");
      const asset_bitasset_data_object& jmj_obj = bit_jmj_id(db).bitasset_data(db);
      BOOST_CHECK_EQUAL(jmj_obj.feeds.size(), 3ul);
      for(const auto& feed : jmj_obj.feeds)
      {
         BOOST_CHECK(feed.second.second.settlement_price.is_null());
      }
   }
   {
      BOOST_TEST_MESSAGE("With underlying bitasset changed from one to another, price feeds should still be publish-able");
      BOOST_TEST_MESSAGE("Adding Vikram's price feed");
      publish_feed(vikram_id, bit_usd_id, 1, bit_jmj_id, 30, core_id);

      const asset_bitasset_data_object& bitasset = bit_jmj_id(db).bitasset_data(db);
      BOOST_CHECK_EQUAL(bitasset.current_feed.settlement_price.to_real(), 30);
      BOOST_CHECK(bitasset.current_feed.maintenance_collateral_ratio == GRAPHENE_DEFAULT_MAINTENANCE_COLLATERAL_RATIO);

      BOOST_TEST_MESSAGE("Adding Ben's pricing to JMJBIT");
      publish_feed(ben_id, bit_usd_id, 1, bit_jmj_id, 25, core_id);

      BOOST_CHECK_EQUAL(bitasset.current_feed.settlement_price.to_real(), 30);
      BOOST_CHECK(bitasset.current_feed.maintenance_collateral_ratio == GRAPHENE_DEFAULT_MAINTENANCE_COLLATERAL_RATIO);

      BOOST_TEST_MESSAGE("Adding Dan's pricing to JMJBIT");
      publish_feed(dan_id, bit_usd_id, 1, bit_jmj_id, 10, core_id);

      BOOST_CHECK_EQUAL(bitasset.current_feed.settlement_price.to_real(), 25);
      BOOST_CHECK(bitasset.current_feed.maintenance_collateral_ratio == GRAPHENE_DEFAULT_MAINTENANCE_COLLATERAL_RATIO);
      generate_block();
      trx.clear();

      BOOST_CHECK(bitasset.current_feed.core_exchange_rate.base.asset_id != bitasset.current_feed.core_exchange_rate.quote.asset_id);
   }
}

/*********
 * @brief Update median feeds after feed_lifetime_sec changed
 */
BOOST_AUTO_TEST_CASE( hf_890_test )
{
   uint32_t skip = database::skip_witness_signature
                 | database::skip_transaction_signatures
                 | database::skip_transaction_dupe_check
                 | database::skip_block_size_check
                 | database::skip_tapos_check
                 | database::skip_authority_check
                 | database::skip_merkle_check
                 ;
   generate_blocks(HARDFORK_615_TIME, true, skip); // get around Graphene issue #615 feed expiration bug
   generate_blocks(db.get_dynamic_global_properties().next_maintenance_time, true, skip);

   for( int i=0; i<2; ++i )
   {
      int blocks = 0;
      auto mi = db.get_global_properties().parameters.maintenance_interval;

      if( i == 1 ) // go beyond hard fork
      {
         blocks += generate_blocks(HARDFORK_CORE_868_890_TIME - mi, true, skip);
         blocks += generate_blocks(db.get_dynamic_global_properties().next_maintenance_time, true, skip);
      }
      set_expiration( db, trx );

      ACTORS((buyer)(seller)(borrower)(feedproducer));

      int64_t init_balance(1000000);

      transfer(committee_account, buyer_id, asset(init_balance));
      transfer(committee_account, borrower_id, asset(init_balance));

      const auto& bitusd = create_bitasset("USDBIT", feedproducer_id);
      asset_id_type usd_id = bitusd.id;

      {
         // change feed lifetime
         const asset_object& asset_to_update = usd_id(db);
         asset_update_bitasset_operation ba_op;
         ba_op.asset_to_update = usd_id;
         ba_op.issuer = asset_to_update.issuer;
         ba_op.new_options = asset_to_update.bitasset_data(db).options;
         ba_op.new_options.feed_lifetime_sec = 600;
         trx.operations.push_back(ba_op);
         PUSH_TX(db, trx, ~0);
         trx.clear();
      }

      // prepare feed data
      price_feed current_feed;
      current_feed.maintenance_collateral_ratio = 1750;
      current_feed.maximum_short_squeeze_ratio = 1100;

      // set price feed
      update_feed_producers( usd_id(db), {feedproducer_id} );
      current_feed.settlement_price = asset(100, usd_id) / asset(5);
      publish_feed( usd_id, feedproducer_id, current_feed );

      // Place some collateralized orders
      // start out with 300% collateral, call price is 15/175 CORE/USD = 60/700
      borrow( borrower_id, asset(100, usd_id), asset(15) );

      transfer( borrower_id, seller_id, asset(100, usd_id) );

      // Adjust price feed to get call order into margin call territory
      current_feed.settlement_price = asset(100, usd_id) / asset(10);
      publish_feed( usd_id, feedproducer_id, current_feed );
      // settlement price = 100 USD / 10 CORE, mssp = 100/11 USD/CORE

      // let the feed expire
      blocks += generate_blocks( db.head_block_time() + 1200, true, skip );
      set_expiration( db, trx );

      // check: median feed should be null
      BOOST_CHECK( usd_id(db).bitasset_data(db).current_feed.settlement_price.is_null() );

      // place a sell order, it won't be matched with the call order
      limit_order_id_type sell_id = create_sell_order(seller_id, asset(10, usd_id), asset(1))->id;

      {
         // change feed lifetime to longer
         const asset_object& asset_to_update = usd_id(db);
         asset_update_bitasset_operation ba_op;
         ba_op.asset_to_update = usd_id;
         ba_op.issuer = asset_to_update.issuer;
         ba_op.new_options = asset_to_update.bitasset_data(db).options;
         ba_op.new_options.feed_lifetime_sec = HARDFORK_CORE_868_890_TIME.sec_since_epoch()
                                             - db.head_block_time().sec_since_epoch()
                                             + mi
                                             + 1800;
         trx.operations.push_back(ba_op);
         PUSH_TX(db, trx, ~0);
         trx.clear();
      }

      // check
      if( i == 0 ) // before hard fork, median feed is still null, and limit order is still there
      {
         BOOST_CHECK( usd_id(db).bitasset_data(db).current_feed.settlement_price.is_null() );
         BOOST_CHECK( db.find<limit_order_object>( sell_id ) );

         // go beyond hard fork
         blocks += generate_blocks(HARDFORK_CORE_868_890_TIME - mi, true, skip);
         blocks += generate_blocks(db.get_dynamic_global_properties().next_maintenance_time, true, skip);
      }

      // after hard fork, median feed should become valid, and the limit order should be filled
      {
         BOOST_CHECK( usd_id(db).bitasset_data(db).current_feed.settlement_price == current_feed.settlement_price );
         BOOST_CHECK( !db.find<limit_order_object>( sell_id ) );
      }

      // undo above tx's and reset
      generate_block( skip );
      ++blocks;
      while( blocks > 0 )
      {
         db.pop_block();
         --blocks;
      }
   }
}

/*********
 * @brief Call check_call_orders after current_feed changed but not only settlement_price changed.
 */
BOOST_AUTO_TEST_CASE( hf_935_test )
{ try {
   uint32_t skip = database::skip_witness_signature
                 | database::skip_transaction_signatures
                 | database::skip_transaction_dupe_check
                 | database::skip_block_size_check
                 | database::skip_tapos_check
                 | database::skip_authority_check
                 | database::skip_merkle_check
                 ;
   generate_blocks( HARDFORK_615_TIME, true, skip ); // get around Graphene issue #615 feed expiration bug
   generate_blocks( db.get_dynamic_global_properties().next_maintenance_time, true, skip );
   generate_block( skip );

   for( int i = 0; i < 3; ++i )
   {
      idump( (i) );
      int blocks = 0;
      auto mi = db.get_global_properties().parameters.maintenance_interval;

      if( i == 1 ) // go beyond hard fork 890
      {
         generate_blocks( HARDFORK_CORE_868_890_TIME - mi, true, skip );
         generate_blocks( db.get_dynamic_global_properties().next_maintenance_time, true, skip );
      }
      else if( i == 2 ) // go beyond hard fork 935
      {
         generate_blocks( HARDFORK_CORE_935_TIME - mi, true, skip );
         generate_blocks( db.get_dynamic_global_properties().next_maintenance_time, true, skip );
      }
      set_expiration( db, trx );

      ACTORS( (seller)(borrower)(feedproducer)(feedproducer2)(feedproducer3) );

      int64_t init_balance( 1000000 );

      transfer( committee_account, borrower_id, asset(init_balance) );

      const auto& bitusd = create_bitasset( "USDBIT", feedproducer_id );
      asset_id_type usd_id = bitusd.id;

      {
         // change feed lifetime (2x maintenance interval)
         const asset_object& asset_to_update = usd_id(db);
         asset_update_bitasset_operation ba_op;
         ba_op.asset_to_update = usd_id;
         ba_op.issuer = asset_to_update.issuer;
         ba_op.new_options = asset_to_update.bitasset_data(db).options;
         ba_op.new_options.feed_lifetime_sec = 300;
         trx.operations.push_back(ba_op);
         PUSH_TX(db, trx, ~0);
         trx.clear();
      }

      // set feed producers
      flat_set<account_id_type> producers;
      producers.insert( feedproducer_id );
      producers.insert( feedproducer2_id );
      producers.insert( feedproducer3_id );
      update_feed_producers( usd_id(db), producers );

      // prepare feed data
      price_feed current_feed;
      current_feed.maintenance_collateral_ratio = 3500;
      current_feed.maximum_short_squeeze_ratio = 1100;

      // set 2 price feeds with 350% MCR
      current_feed.settlement_price = asset(100, usd_id) / asset(5);
      publish_feed( usd_id, feedproducer_id, current_feed );
      publish_feed( usd_id, feedproducer2_id, current_feed );

      // check median, MCR should be 350%
      BOOST_CHECK( usd_id(db).bitasset_data(db).current_feed.settlement_price == current_feed.settlement_price );
      BOOST_CHECK_EQUAL( usd_id(db).bitasset_data(db).current_feed.maintenance_collateral_ratio, 3500 );

      // generate some blocks, let the feeds expire
      blocks += generate_blocks( db.head_block_time() + 360, true, skip );
      set_expiration( db, trx );

      // check median, should be null
      BOOST_CHECK( usd_id(db).bitasset_data(db).current_feed.settlement_price.is_null() );

      // publish a new feed with 175% MCR, new median MCR would be 175%
      current_feed.maintenance_collateral_ratio = 1750;
      publish_feed( usd_id, feedproducer3_id, current_feed );

      // check median, MCR should be 175%
      BOOST_CHECK( usd_id(db).bitasset_data(db).current_feed.settlement_price == current_feed.settlement_price );
      BOOST_CHECK_EQUAL( usd_id(db).bitasset_data(db).current_feed.maintenance_collateral_ratio, 1750 );

      // Place some collateralized orders
      // start out with 300% collateral, call price is 15/175 CORE/USD = 60/700
      borrow( borrower_id, asset(100, usd_id), asset(15) );

      transfer( borrower_id, seller_id, asset(100, usd_id) );

      // place a sell order, it won't be matched with the call order now.
      // when median MCR changed to 350%, the call order with 300% collateral will be in margin call territory,
      // then this limit order should be filled
      limit_order_id_type sell_id = create_sell_order( seller_id, asset(20, usd_id), asset(1) )->id;

      {
         // change feed lifetime to longer, let all 3 feeds be valid
         const asset_object& asset_to_update = usd_id(db);
         asset_update_bitasset_operation ba_op;
         ba_op.asset_to_update = usd_id;
         ba_op.issuer = asset_to_update.issuer;
         ba_op.new_options = asset_to_update.bitasset_data(db).options;
         ba_op.new_options.feed_lifetime_sec = HARDFORK_CORE_935_TIME.sec_since_epoch()
                                             - db.head_block_time().sec_since_epoch()
                                             + mi * 3;
         trx.operations.push_back(ba_op);
         PUSH_TX(db, trx, ~0);
         trx.clear();
      }

      // check
      if( i == 0 ) // before hard fork 890
      {
         // median feed won't change (issue 890)
         BOOST_CHECK( usd_id(db).bitasset_data(db).current_feed.settlement_price == current_feed.settlement_price );
         BOOST_CHECK_EQUAL( usd_id(db).bitasset_data(db).current_feed.maintenance_collateral_ratio, 1750 );
         // limit order is still there
         BOOST_CHECK( db.find<limit_order_object>( sell_id ) );

         // go beyond hard fork 890
         blocks += generate_blocks(HARDFORK_CORE_868_890_TIME - mi, true, skip);
         blocks += generate_blocks(db.get_dynamic_global_properties().next_maintenance_time, true, skip);
      }

      // after hard fork 890, if it's before hard fork 935
      if( db.get_dynamic_global_properties().next_maintenance_time <= HARDFORK_CORE_935_TIME )
      {
         // median should have changed
         BOOST_CHECK( usd_id(db).bitasset_data(db).current_feed.settlement_price == current_feed.settlement_price );
         BOOST_CHECK_EQUAL( usd_id(db).bitasset_data(db).current_feed.maintenance_collateral_ratio, 3500 );
         // but the limit order is still there, because `check_call_order` was incorrectly skipped
         BOOST_CHECK( db.find<limit_order_object>( sell_id ) );

         // go beyond hard fork 935
         blocks += generate_blocks(HARDFORK_CORE_935_TIME - mi, true, skip);
         blocks += generate_blocks(db.get_dynamic_global_properties().next_maintenance_time, true, skip);
      }

      // after hard fork 935, the limit order should be filled
      {
         // median MCR should be 350%
         BOOST_CHECK( usd_id(db).bitasset_data(db).current_feed.settlement_price == current_feed.settlement_price );
         BOOST_CHECK_EQUAL( usd_id(db).bitasset_data(db).current_feed.maintenance_collateral_ratio, 3500 );
         // the limit order is still there, because `check_call_order` is skipped
         BOOST_CHECK( !db.find<limit_order_object>( sell_id ) );
         if( db.find<limit_order_object>( sell_id ) )
         {
            idump( (sell_id(db)) );
         }
      }


      // undo above tx's and reset
      generate_block( skip );
      ++blocks;
      while( blocks > 0 )
      {
         db.pop_block();
         --blocks;
      }
   }
} FC_LOG_AND_RETHROW() }

/*****
 * @brief make sure feeds work correctly after changing from non-witness-fed to witness-fed before the 868 fork
 * NOTE: This test case is a different issue than what is currently being worked on, and fails. Hopefully it
 * will help when the fix for that issue is being coded.
 */
/*
BOOST_AUTO_TEST_CASE( reset_backing_asset_switching_to_witness_fed )
{
   ACTORS((nathan)(dan)(ben)(vikram));

   BOOST_TEST_MESSAGE("Advance to near hard fork");
   auto maint_interval = db.get_global_properties().parameters.maintenance_interval;
   generate_blocks( HARDFORK_CORE_868_890_TIME - maint_interval);
   trx.set_expiration(HARDFORK_CORE_868_890_TIME - fc::seconds(1));


   BOOST_TEST_MESSAGE("Create USDBIT");
   asset_id_type bit_usd_id = create_bitasset("USDBIT").id;
   asset_id_type core_id = bit_usd_id(db).bitasset_data(db).options.short_backing_asset;

   {
      BOOST_TEST_MESSAGE("Update the USDBIT asset options");
      change_asset_options(*this, nathan_id, nathan_private_key, bit_usd_id, false );
   }

   BOOST_TEST_MESSAGE("Create JMJBIT based on USDBIT.");
   asset_id_type bit_jmj_id = create_bitasset("JMJBIT").id;
   {
      BOOST_TEST_MESSAGE("Update the JMJBIT asset options");
      change_asset_options(*this, nathan_id, nathan_private_key, bit_jmj_id, false );
   }
   {
      BOOST_TEST_MESSAGE("Update the JMJBIT bitasset options");
      asset_update_bitasset_operation ba_op;
      const asset_object& obj = bit_jmj_id(db);
      ba_op.asset_to_update = obj.get_id();
      ba_op.issuer = obj.issuer;
      ba_op.new_options.short_backing_asset = bit_usd_id;
      ba_op.new_options.minimum_feeds = 1;
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
      BOOST_CHECK_EQUAL(obj.feeds.size(), 3ul);
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
      add_price_feed(*this, vikram_id, bit_usd_id, 1, bit_jmj_id, 300, core_id);

      const asset_bitasset_data_object& bitasset = bit_jmj_id(db).bitasset_data(db);
      BOOST_CHECK_EQUAL(bitasset.current_feed.settlement_price.to_real(), 300.0);
      BOOST_CHECK(bitasset.current_feed.maintenance_collateral_ratio == GRAPHENE_DEFAULT_MAINTENANCE_COLLATERAL_RATIO);
   }
   {
      BOOST_TEST_MESSAGE("Change JMJBIT to be witness_fed");
      optional<account_id_type> noone;
      change_asset_options(*this, noone, nathan_private_key, bit_jmj_id, true );
   }
   {
      BOOST_TEST_MESSAGE("Change underlying asset of bit_jmj from bit_usd to core");
      change_backing_asset(*this, nathan_private_key, bit_jmj_id, core_id);

      BOOST_TEST_MESSAGE("Verify feed producers have not been reset");
      const asset_bitasset_data_object& jmj_obj = bit_jmj_id(db).bitasset_data(db);
      BOOST_CHECK_EQUAL(jmj_obj.feeds.size(), 3ul);
      int nan_count = 0;
      for(const auto& feed : jmj_obj.feeds) {
         if(feed.second.second.settlement_price.is_null())
            ++nan_count;
      }
      BOOST_CHECK_EQUAL(nan_count, 2);
   }
   {
      BOOST_TEST_MESSAGE("Add a new (and correct) feed price from a witness");
      auto& global_props = db.get_global_properties();
      std::vector<account_id_type> active_witnesses;
      const witness_id_type& first_witness_id = (*global_props.active_witnesses.begin());
      const account_id_type witness_account_id = first_witness_id(db).witness_account;
      add_price_feed(*this, witness_account_id, core_id, 1, bit_jmj_id, 300, core_id);

      // we should have 2 feeds nan, 1 old feed with wrong asset, and 1 witness feed
      const asset_bitasset_data_object& jmj_obj = bit_jmj_id(db).bitasset_data(db);
      BOOST_CHECK_EQUAL(jmj_obj.feeds.size(), 4ul);
      int nan_count = 0;
      for(const auto& feed : jmj_obj.feeds) {
         if ( feed.second.second.settlement_price.is_null() )
            ++nan_count;
      }
      BOOST_CHECK_EQUAL(nan_count, 2);
   }
   {
      BOOST_TEST_MESSAGE("Advance to past hard fork");
      generate_blocks( HARDFORK_CORE_868_890_TIME + maint_interval);
      trx.set_expiration(HARDFORK_CORE_868_890_TIME + fc::hours(48));

      BOOST_TEST_MESSAGE("Verify that the incorrect feeds have been removed");
      const asset_bitasset_data_object& jmj_obj = bit_jmj_id(db).bitasset_data(db);
      BOOST_CHECK_EQUAL(jmj_obj.feeds.size(), 1ul);
      BOOST_CHECK( ! (*jmj_obj.feeds.begin()).second.second.settlement_price.is_null() );
      // the settlement price will be NaN until 50% of price feeds are valid
      //BOOST_CHECK_EQUAL(jmj_obj.current_feed.settlement_price.to_real(), 300);
   }
   {
      BOOST_TEST_MESSAGE("After hardfork, change underlying asset of bit_jmj from core to bit_usd");
      change_backing_asset(*this, nathan_private_key, bit_jmj_id, bit_usd_id);

      BOOST_TEST_MESSAGE("Verify feed producers have been reset");
      const asset_bitasset_data_object& jmj_obj = bit_jmj_id(db).bitasset_data(db);
      BOOST_CHECK_EQUAL(jmj_obj.feeds.size(), 0ul);
   }
   {
      BOOST_TEST_MESSAGE("With underlying bitasset changed from one to another, price feeds should still be publish-able");
      auto& global_props = db.get_global_properties();
      std::vector<account_id_type> active_witnesses;
      for(const auto& witness_id : global_props.active_witnesses)
      {
         active_witnesses.push_back(witness_id(db).witness_account);
      }
      BOOST_TEST_MESSAGE("Adding Witness 0's price feed");
      add_price_feed(*this, active_witnesses[0], bit_usd_id, 1, bit_jmj_id, 30, core_id);

      const asset_bitasset_data_object& bitasset = bit_jmj_id(db).bitasset_data(db);
      BOOST_CHECK_EQUAL(bitasset.current_feed.settlement_price.to_real(), 30);
      BOOST_CHECK(bitasset.current_feed.maintenance_collateral_ratio == GRAPHENE_DEFAULT_MAINTENANCE_COLLATERAL_RATIO);

      BOOST_TEST_MESSAGE("Adding Witness 1's pricing to JMJBIT");
      add_price_feed(*this, active_witnesses[0], bit_usd_id, 1, bit_jmj_id, 25, core_id);

      BOOST_CHECK_EQUAL(bitasset.current_feed.settlement_price.to_real(), 30);
      BOOST_CHECK(bitasset.current_feed.maintenance_collateral_ratio == GRAPHENE_DEFAULT_MAINTENANCE_COLLATERAL_RATIO);

      BOOST_TEST_MESSAGE("Adding Witness 2's pricing to JMJBIT");
      add_price_feed(*this, active_witnesses[2], bit_usd_id, 1, bit_jmj_id, 10, core_id);

      BOOST_CHECK_EQUAL(bitasset.current_feed.settlement_price.to_real(), 25);
      BOOST_CHECK(bitasset.current_feed.maintenance_collateral_ratio == GRAPHENE_DEFAULT_MAINTENANCE_COLLATERAL_RATIO);
      generate_block();
      trx.clear();

      BOOST_CHECK(bitasset.current_feed.core_exchange_rate.base.asset_id != bitasset.current_feed.core_exchange_rate.quote.asset_id);
   }
}
*/

BOOST_AUTO_TEST_SUITE_END()
