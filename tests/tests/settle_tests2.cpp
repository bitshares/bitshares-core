/*
 * Copyright (c) 2020 Michel Santos, and contributors.
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

#include <graphene/chain/hardfork.hpp>

#include <graphene/protocol/market.hpp>
#include <graphene/chain/market_object.hpp>

#include "../common/database_fixture.hpp"

using namespace graphene::chain;
using namespace graphene::chain::test;

struct force_settle_database_fixture : database_fixture {
   force_settle_database_fixture()
           : database_fixture() {
   }

   /**
    * Create a smart asset
    * @param name Asset name
    * @param issuer Issuer ID
    * @param force_settlement_offset_percent Force-settlement offset percent
    * @param force_settlement_fee_percent Force-settlement fee percent (BSIP87)
    * @return Asset object
    */
   const asset_object &create_smart_asset(
           const string &name,
           account_id_type issuer /* = GRAPHENE_WITNESS_ACCOUNT */,
           uint16_t force_settlement_offset_percent /* = 100 */ /* 1% */,
           uint16_t force_settlement_fee_percent /* = 100 */ /* 1% */
   ) {
      try {
         uint16_t market_fee_percent = 100; /*1%*/
         uint16_t flags = charge_market_fee;
         uint16_t precision = 2;
         asset_id_type backing_asset = {};
         share_type max_supply = GRAPHENE_MAX_SHARE_SUPPLY;

         asset_create_operation creator;
         creator.issuer = issuer;
         creator.fee = asset();
         creator.symbol = name;
         creator.precision = precision;

         creator.common_options.max_supply = max_supply;
         creator.common_options.market_fee_percent = market_fee_percent;
         if (issuer == GRAPHENE_WITNESS_ACCOUNT)
            flags |= witness_fed_asset;
         creator.common_options.issuer_permissions = flags;
         creator.common_options.flags = flags & ~global_settle;
         creator.common_options.core_exchange_rate = price(asset(1, asset_id_type(1)), asset(1));

         creator.common_options.extensions.value.force_settle_fee_percent = force_settlement_fee_percent;

         creator.bitasset_opts = bitasset_options();
         creator.bitasset_opts->force_settlement_offset_percent = force_settlement_offset_percent;
         creator.bitasset_opts->short_backing_asset = backing_asset;

         trx.operations.push_back(std::move(creator));
         trx.validate();
         processed_transaction ptx = PUSH_TX(db, trx, ~0);
         trx.operations.clear();
         return db.get<asset_object>(ptx.operation_results[0].get<object_id_type>());
      } FC_CAPTURE_AND_RETHROW((name)(issuer))
   }
};


/**
 * Test the effects of the new force settlement fee from BSIP87
 */
BOOST_FIXTURE_TEST_SUITE(force_settle_tests, force_settle_database_fixture)

   /**
    * Test when one holder of a smart asset force settles (FS) their holding when there are two debtors
    *
    * There are three primary actors: michael, paul, rachel
    *
    * 1. Asset owner creates the smart coin called bitUSD
    * 2. The feed price is 20 satoshi bitUSD for 1 satoshi Core -> 0.2 bitUSD for 0.00001 Core = 20000 bitUSD for 1 Core
    * 3. Michael borrows 0.06 bitUSD (6 satoshis of bitUSD) from the blockchain with a high amount of collateral
    * 4. Paul borrows 1000 bitUSD (100000 satoshis of bitUSD) from the blockchain with a low amount of collateral
    * 5. Paul gives Rachel 200 bitUSD
    * 6. Rachel force-settles 20 bitUSD which should be collected from Paul's debt position
    * because of its relatively lower collateral ratio
    *
    * The force-settlement by Rachel should account for both the force-settlement offset fee,
    * and the new force settlement fee from BSIP87.
    *
    * Michael's debt and balances should be unaffected by the activities of Paul and Rachel
    */
   BOOST_AUTO_TEST_CASE(force_settle_fee_1_test) {
      try {
         ///////
         // Initialize the scenario
         ///////
         // Get around Graphene issue #615 feed expiration bug
         generate_blocks(HARDFORK_615_TIME);
         generate_blocks(db.get_dynamic_global_properties().next_maintenance_time);

         // Advance to the when the force-settlement fee activates
         generate_blocks(HARDFORK_CORE_BSIP87_TIME);
         generate_block();
         set_expiration(db, trx);
         trx.clear();

         // Create actors
         ACTORS((assetowner)(feedproducer)(paul)(michael)(rachel));

         // Fund actors
         uint64_t initial_balance_core = 10000000;
         transfer(committee_account, assetowner.id, asset(initial_balance_core));
         transfer(committee_account, feedproducer.id, asset(initial_balance_core));
         transfer(committee_account, michael_id, asset(initial_balance_core));
         transfer(committee_account, paul.id, asset(initial_balance_core));

         // 1. Create assets
         const uint16_t usd_fso_percent = 5 * GRAPHENE_1_PERCENT; // 5% Force-settlement offset fee %
         const uint16_t usd_fsf_percent = 3 * GRAPHENE_1_PERCENT; // 3% Force-settlement fee % (BSIP87)
         create_smart_asset("USDBIT", assetowner.id, usd_fso_percent, usd_fsf_percent);

         generate_block();
         set_expiration(db, trx);
         trx.clear();

         const auto &bitusd = get_asset("USDBIT");
         const auto &core = asset_id_type()(db);
         asset_id_type bitusd_id = bitusd.id;
         asset_id_type core_id = core.id;

         // 2. Publish a feed for the smart asset
         update_feed_producers(bitusd_id(db), {feedproducer_id});
         price_feed current_feed;
         current_feed.maintenance_collateral_ratio = 1750;
         current_feed.maximum_short_squeeze_ratio = 1100;
         // Requirement of 20 satoshi bitUSD for 1 satoshi Core -> 0.2 bitUSD for 0.00001 Core = 20000 bitUSD for 1 Core
         current_feed.settlement_price = bitusd.amount(20) / core.amount(1);
         publish_feed(bitusd, feedproducer, current_feed);


         ///////
         // 3. Michael borrows 0.06 bitUSD
         ///////
         int64_t michael_initial_usd = 6; // 0.06 USD
         int64_t michael_initial_core = 8;
         const call_order_object &call_michael = *borrow(michael, bitusd.amount(michael_initial_usd),
                                                         core.amount(michael_initial_core));
         call_order_id_type call_michael_id = call_michael.id;

         BOOST_CHECK_EQUAL(get_balance(michael, bitusd), michael_initial_usd);
         BOOST_CHECK_EQUAL(get_balance(michael, core), initial_balance_core - michael_initial_core);


         ///////
         // 4. Paul borrows 1000 bitUSD
         ///////
         // Paul will borrow bitUSD by providing 2x collateral required: 2 * 1/20 = 1/10
         int64_t paul_initial_usd = 1000 * std::pow(10, bitusd.precision); // 100000
         int64_t paul_initial_core = paul_initial_usd * 2 / 20; // 10000
         const call_order_object &call_paul = *borrow(paul, bitusd.amount(paul_initial_usd),
                                                      core.amount(paul_initial_core));
         call_order_id_type call_paul_id = call_paul.id;
         BOOST_REQUIRE_EQUAL(get_balance(paul, bitusd), paul_initial_usd);

         BOOST_CHECK_EQUAL(get_balance(paul, bitusd), paul_initial_usd);
         BOOST_CHECK_EQUAL(get_balance(paul, core), initial_balance_core - paul_initial_core);


         ///////
         // 5. Paul transfers 200 bitUSD to Rachel
         ///////
         int64_t rachel_initial_usd = 200 * std::pow(10, bitusd.precision);
         transfer(paul.id, rachel.id, asset(rachel_initial_usd, bitusd.id));

         BOOST_CHECK_EQUAL(get_balance(rachel, core), 0);
         BOOST_CHECK_EQUAL(get_balance(rachel, bitusd), rachel_initial_usd);

         BOOST_CHECK_EQUAL(get_balance(paul, bitusd), paul_initial_usd - rachel_initial_usd);
         BOOST_CHECK_EQUAL(get_balance(paul, core), initial_balance_core - paul_initial_core);


         ///////
         // 6. Rachel force settles 20 bitUSD
         ///////
         const int64_t rachel_settle_amount = 20 * std::pow(10, bitusd.precision);
         operation_result result = force_settle(rachel, bitusd.amount(rachel_settle_amount));

         force_settlement_id_type rachel_settle_id = result.get<object_id_type>();
         BOOST_CHECK_EQUAL(rachel_settle_id(db).balance.amount.value, rachel_settle_amount);

         // Check Rachel's balance
         BOOST_CHECK_EQUAL(get_balance(rachel, bitusd), rachel_initial_usd - rachel_settle_amount);
         BOOST_CHECK_EQUAL(get_balance(rachel, core), 0);

         // Check Paul's balance
         BOOST_CHECK_EQUAL(get_balance(paul, bitusd), paul_initial_usd - rachel_initial_usd);
         BOOST_CHECK_EQUAL(get_balance(paul, core), initial_balance_core - paul_initial_core);

         // Check Paul's debt to the blockchain
         BOOST_CHECK_EQUAL(paul_initial_usd, call_paul.debt.value);
         BOOST_CHECK_EQUAL(paul_initial_core, call_paul.collateral.value);

         // Check Michael's balance
         BOOST_CHECK_EQUAL(get_balance(michael, bitusd), michael_initial_usd);
         BOOST_CHECK_EQUAL(get_balance(michael, core), initial_balance_core - michael_initial_core);

         // Check Michael's debt to the blockchain
         BOOST_CHECK_EQUAL(michael_initial_usd, call_michael.debt.value);
         BOOST_CHECK_EQUAL(michael_initial_core, call_michael.collateral.value);


         ///////
         // Advance time and update the price feed
         ///////
         generate_blocks(db.head_block_time() + fc::hours(20));
         set_expiration(db, trx);
         trx.clear();

         // The default feed and settlement expires at the same time
         // Publish another feed to have a valid price to exit
         publish_feed(bitusd_id(db), feedproducer_id(db), current_feed);


         ///////
         // Advance time to trigger the conclusion of the force settlement
         ///////
         generate_blocks(db.head_block_time() + fc::hours(6));
         set_expiration(db, trx);
         trx.clear();


         //////
         // Check
         //////
         // Rachel's settlement should have completed and should no longer be present
         BOOST_CHECK(!db.find(rachel_settle_id));

         // Check Rachel's balance
         // Rachel redeemed some smart asset and should get the equivalent collateral amount (according to the feed price)
         // minus the force_settlement_offset_fee - force_settlement_fee
         // Rachel redeemed  20 USD (2000 satoshi bitUSD) and should get
         // 100 satoshi Core - 5 satoshi Core - 2 satoshi Core; 3% * (100 - 5) = 2.85 trunacted to 2 satoshi Core
         uint64_t rachel_settle_core = rachel_settle_amount * 1 / 20; // Settle amount * feed price
         uint64_t rachel_fso_fee_core = rachel_settle_core * usd_fso_percent / GRAPHENE_100_PERCENT; // 5 satoshi Core
         uint64_t rachel_fso_remainder_core = rachel_settle_core - rachel_fso_fee_core; // 95 satoshi Core
         uint64_t rachel_fsf_fee_core =
                 (rachel_fso_remainder_core) * usd_fsf_percent / GRAPHENE_100_PERCENT; // 2 satoshi Core
         uint64_t expected_rachel_core =
                 rachel_settle_core - rachel_fso_fee_core - rachel_fsf_fee_core; // 93 satoshi Core
         BOOST_CHECK_EQUAL(get_balance(rachel, bitusd), rachel_initial_usd - rachel_settle_amount);
         BOOST_CHECK_EQUAL(get_balance(rachel_id(db), core_id(db)), expected_rachel_core);

         // Check Paul's balance
         BOOST_CHECK_EQUAL(get_balance(paul_id(db), bitusd_id(db)), paul_initial_usd - rachel_initial_usd);
         BOOST_CHECK_EQUAL(get_balance(paul_id(db), core_id(db)), initial_balance_core - paul_initial_core);

         // Check Paul's debt to the blockchain
         // Rachel redeemed 20 usd from the blockchain, and the blockchain closed this amount from Paul's debt to it
         BOOST_CHECK_EQUAL(paul_initial_usd - rachel_settle_amount, call_paul_id(db).debt.value);
         // The call order has the original amount of collateral less what was redeemed by Rachel
         BOOST_CHECK_EQUAL(paul_initial_core - rachel_fso_remainder_core, call_paul_id(db).collateral.value);

         // Check Michael's balance
         // Rachel's redemption should not have affected Michael's balance
         BOOST_CHECK_EQUAL(get_balance(michael, bitusd), michael_initial_usd);
         BOOST_CHECK_EQUAL(get_balance(michael, core), initial_balance_core - michael_initial_core);

         // Check Michael's debt to the blockchain
         // Rachel's redemption should not have affected Michael's debt to the blockchain
         BOOST_CHECK_EQUAL(michael_initial_usd, call_michael_id(db).debt.value);
         BOOST_CHECK_EQUAL(michael_initial_core, call_michael_id(db).collateral.value);

         // The supply of USD equals the amount borrowed/created by Paul and Michael
         // minus the amount redeemed/destroyed by Rachel
         BOOST_CHECK_EQUAL(bitusd_id(db).dynamic_data(db).current_supply.value,
                           paul_initial_usd + michael_initial_usd - rachel_settle_amount);

         // Check the asset owner's vesting fees
         // The market fee reward should be zero because the market fee reward % is 0
         const auto assetowner_fs_fees_usd = get_market_fee_reward(assetowner, bitusd);
         BOOST_CHECK_EQUAL(assetowner_fs_fees_usd, 0);

         // Check the asset owner's accumulated asset fees
         BOOST_CHECK(bitusd.dynamic_asset_data_id(db).accumulated_fees == 0);
         BOOST_CHECK(bitusd.dynamic_asset_data_id(db).accumulated_collateral_fees == rachel_fsf_fee_core);

      }
      FC_LOG_AND_RETHROW()
   }

BOOST_AUTO_TEST_SUITE_END()
