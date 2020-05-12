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
    * Create an operation to create a smart asset
    * @param name Asset name
    * @param issuer Issuer ID
    * @param force_settlement_offset_percent Force-settlement offset percent
    * @param force_settlement_fee_percent Force-settlement fee percent (BSIP87)
    * @return An asset_create_operation
    */
   const asset_create_operation create_smart_asset_op(
           const string &name,
           account_id_type issuer /* = GRAPHENE_WITNESS_ACCOUNT */,
           uint16_t force_settlement_offset_percent /* 100 = 1% */,
           optional<uint16_t> force_settlement_fee_percent /* 100 = 1% */
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

         creator.bitasset_opts = bitasset_options();
         creator.bitasset_opts->force_settlement_offset_percent = force_settlement_offset_percent;
         creator.bitasset_opts->short_backing_asset = backing_asset;
         creator.bitasset_opts->extensions.value.force_settle_fee_percent = force_settlement_fee_percent;

         return creator;

      } FC_CAPTURE_AND_RETHROW((name)(issuer))
   }


   /**
    * Create a smart asset without a force settlement fee percent
    * @param name Asset name
    * @param issuer Issuer ID
    * @param force_settlement_offset_percent Force-settlement offset percent
    * @return Asset object
    */
   const asset_object &create_smart_asset(
           const string &name,
           account_id_type issuer /* = GRAPHENE_WITNESS_ACCOUNT */,
           uint16_t force_settlement_offset_percent /* = 100 */ /* 1% */
   ) {
      try {
         optional<uint16_t> force_settlement_fee_percent; // Not specified
         return create_smart_asset(name, issuer, force_settlement_offset_percent, force_settlement_fee_percent);
      } FC_CAPTURE_AND_RETHROW((name)(issuer)(force_settlement_offset_percent))
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
           uint16_t force_settlement_offset_percent /* 100 = 1% */,
           optional<uint16_t> force_settlement_fee_percent /* 100 = 1% */
   ) {
      try {
         asset_create_operation creator = create_smart_asset_op(name, issuer, force_settlement_offset_percent,
                                                                force_settlement_fee_percent);

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
         const int64_t bitusd_unit = asset::scaled_precision(bitusd.precision).value; // 100 satoshi USDBIT in 1 USDBIT

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
         int64_t paul_initial_usd = 1000 * bitusd_unit; // 100000
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
         int64_t rachel_initial_usd = 200 * bitusd_unit;
         transfer(paul.id, rachel.id, asset(rachel_initial_usd, bitusd.id));

         BOOST_CHECK_EQUAL(get_balance(rachel, core), 0);
         BOOST_CHECK_EQUAL(get_balance(rachel, bitusd), rachel_initial_usd);

         BOOST_CHECK_EQUAL(get_balance(paul, bitusd), paul_initial_usd - rachel_initial_usd);
         BOOST_CHECK_EQUAL(get_balance(paul, core), initial_balance_core - paul_initial_core);


         ///////
         // 6. Rachel force settles 20 bitUSD
         ///////
         const int64_t rachel_settle_amount = 20 * bitusd_unit;
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


   /**
    * This test evaluates:
    *
    * - collecting collateral-denominated fees before and after BSIP87,
    * - applying different force-settlement fee percentages,
    * - accumulating fees from multiple force-settlements,
    * - changing the backing asset of a smart asset is prohibited when there are unclaimed collateral-denominated fees.
    *
    * There are five actors: asset owner, paul, rachel, michael, yanna, vikram
    *
    * Before HARDFORK_CORE_BSIP87_TIME
    *
    * 1. Asset owner creates the smart coin called bitUSD
    *
    * NOTE: To avoid rounding issues in the test, 1 satoshi of the smart asset will be worth more than 1 satoshi
    * of the backing asset.  This allows force settlements of the smart asset to yield more satoshis of the backing asset
    * with controllable truncation and rounding that will not affect the tests.
    * 2. The feed price is 1 satoshi bitUSD for 20 satoshi Core = 0.01 bitUSD for 0.00020 Core = 50 bitUSD for 1 Core
    *
    * 3. Paul borrows 100 bitUSD (10000 satoshis of bitUSD) from the blockchain
    * 4. Paul gives Rachel 20 bitUSD and retains 80 bitUSD
    * 5. Rachel force-settles 2 bitUSD which should be collected from Paul's debt position
    * 6. Asset owner attempts and fails to claim the collateral fees
    *
    *
    * 7. Activate HARDFORK_CORE_BSIP87_TIME
    *
    *
    * After HARDFORK_CORE_BSIP87_TIME
    *
    * 8. Paul gives Michael 30 bitUSD and retains 50 bitUSD
    * 9. Michael force-settles 5 bitUSD which should be collected from Paul's debt position
    *
    * 10. Asset owner sets the force-fee percentage to 3%
    * 11. Paul gives Yanna 40 bitUSD and retains 10 bitUSD
    * 12. Yanna force-settles 10 bitUSD which should be collected from Paul's debt position
    *
    * 13. Asset owner updates the force-settlement fee to 4%
    * 14. Paul gives Vikram 10 bitUSD and retains 0 bitUSD
    * 15. Vikram force-settles 10 bitUSD which should be collected from Paul's debt position
    *
    * 16. Asset owner attempts and fails to change the backing of the smart asset because of its outstanding supply
    * 17. All current holders of bitUSD close their bitUSD positions
    * 18. Asset owner attempts and fails to change the backing of the smart asset because of unclaimed collateral fees
    * 19. Asset owner claims all of the unclaimed collateral fees
    * 20. Asset owner attempts and succeeds in changing the backing of the smart asset
    */
   BOOST_AUTO_TEST_CASE(force_settle_fee_2_test) {
      try {
         ///////
         // Initialize the scenario
         ///////
         // Get around Graphene issue #615 feed expiration bug
         generate_blocks(HARDFORK_615_TIME);
         generate_blocks(db.get_dynamic_global_properties().next_maintenance_time);
         trx.clear();
         set_expiration(db, trx);

         // Create actors
         ACTORS((assetowner)(feedproducer)(paul)(rachel)(michael)(yanna)(vikram));

         // Fund actors
         uint64_t initial_balance_core = 10000000;
         transfer(committee_account, assetowner.id, asset(initial_balance_core));
         transfer(committee_account, feedproducer.id, asset(initial_balance_core));
         transfer(committee_account, michael_id, asset(initial_balance_core));
         transfer(committee_account, paul.id, asset(initial_balance_core));

         ///////
         // 1. Create assets
         ///////
         const uint16_t usd_fso_percent = 5 * GRAPHENE_1_PERCENT; // 5% Force-settlement offset fee %
         const uint16_t usd_fsf_percent_0 = 0 * GRAPHENE_1_PERCENT; // 0% Force-settlement fee %

         // Attempt and fail to create the smart asset with a force-settlement fee % before HARDFORK_CORE_BSIP87_TIME
         trx.clear();
         REQUIRE_EXCEPTION_WITH_TEXT(create_smart_asset("USDBIT", assetowner.id, usd_fso_percent, usd_fsf_percent_0),
                                     "cannot be set before Hardfork BSIP87");


         // Create the smart asset without a force-settlement fee %
         trx.clear();
         create_smart_asset("USDBIT", assetowner.id, usd_fso_percent);

         generate_block();
         set_expiration(db, trx);
         trx.clear();

         const auto &bitusd = get_asset("USDBIT");
         const auto &core = asset_id_type()(db);
         const int64_t core_unit = asset::scaled_precision(core.precision).value; // 100000 satoshi CORE in 1 CORE
         const int64_t bitusd_unit = asset::scaled_precision(bitusd.precision).value; // 100 satoshi USDBIT in 1 USDBIT


         ///////
         // 2. Publish a feed for the smart asset
         ///////
         update_feed_producers(bitusd.id, {feedproducer_id});
         price_feed current_feed;
         current_feed.maintenance_collateral_ratio = 1750;
         current_feed.maximum_short_squeeze_ratio = 1100;
         // Requirement of 20x collateral in satoshis: 1 satoshi bitUSD for 20 satoshi Core
         // -> 0.01 bitUSD for 0.00020 Core = 100 bitUSD for 2 Core = 50 bitUSD for 1 Core
         current_feed.settlement_price = bitusd.amount(1) / core.amount(20);
         publish_feed(bitusd, feedproducer, current_feed);


         ///////
         // 3. Paul borrows 100 bitUSD
         ///////
         // Paul will borrow bitUSD by providing 2x collateral required: 2 * 20 = 40
         int64_t paul_initial_usd = 100 * bitusd_unit; // 10000
         int64_t paul_initial_core = paul_initial_usd * 2 * 20; // 400000
         const call_order_object &call_paul = *borrow(paul, bitusd.amount(paul_initial_usd),
                                                      core.amount(paul_initial_core));
         call_order_id_type call_paul_id = call_paul.id;
         BOOST_REQUIRE_EQUAL(get_balance(paul, bitusd), paul_initial_usd);

         BOOST_CHECK_EQUAL(get_balance(paul, bitusd), paul_initial_usd);
         BOOST_CHECK_EQUAL(get_balance(paul, core), initial_balance_core - paul_initial_core);


         ///////
         // 4. Paul gives Rachel 20 bitUSD and retains 80 bitUSD
         ///////
         int64_t rachel_initial_usd = 20 * bitusd_unit;
         transfer(paul.id, rachel.id, asset(rachel_initial_usd, bitusd.id));

         BOOST_CHECK_EQUAL(get_balance(rachel, bitusd), rachel_initial_usd);
         BOOST_CHECK_EQUAL(get_balance(rachel, core), 0);

         BOOST_CHECK_EQUAL(get_balance(paul, bitusd), paul_initial_usd - rachel_initial_usd);
         BOOST_CHECK_EQUAL(get_balance(paul, core), initial_balance_core - paul_initial_core);


         ///////
         // 5. Rachel force-settles 2 bitUSD which should be collected from Paul's debt position
         ///////
         const int64_t rachel_settle_amount = 2 * bitusd_unit;
         operation_result result = force_settle(rachel, bitusd.amount(rachel_settle_amount));

         force_settlement_id_type rachel_settle_id = result.get<object_id_type>();
         BOOST_CHECK_EQUAL(rachel_settle_id(db).balance.amount.value, rachel_settle_amount);

         // Advance time to complete the force settlement and to update the price feed
         generate_blocks(db.head_block_time() + fc::hours(26));
         set_expiration(db, trx);
         trx.clear();
         publish_feed(bitusd, feedproducer_id(db), current_feed);
         trx.clear();

         // Rachel's settlement should have completed and should no longer be present
         BOOST_CHECK(!db.find(rachel_settle_id));

         // Check Rachel's balance
         // Rachel redeemed some smart asset and should get the equivalent collateral amount (according to the feed price)
         // minus the force_settlement_offset_fee - force_settlement_fee
         // Rachel redeemed 2 bitUSD and should get 4000 satoshi Core - 200 satoshi Core - 0 satoshi  Core
         uint64_t rachel_settle_core = rachel_settle_amount * 20; // Settle amount * feed price
         uint64_t rachel_fso_fee_core = rachel_settle_core * usd_fso_percent / GRAPHENE_100_PERCENT;
         uint64_t rachel_fso_remainder_core = rachel_settle_core - rachel_fso_fee_core;
         uint64_t rachel_fsf_fee_core = (rachel_fso_remainder_core) * 0 / GRAPHENE_100_PERCENT;
         uint64_t expected_rachel_core = rachel_settle_core - rachel_fso_fee_core - rachel_fsf_fee_core;
         BOOST_CHECK_EQUAL(get_balance(rachel, bitusd), rachel_initial_usd - rachel_settle_amount);
         BOOST_CHECK_EQUAL(get_balance(rachel, core), expected_rachel_core);

         // Check Paul's balance
         BOOST_CHECK_EQUAL(get_balance(paul, bitusd), paul_initial_usd - rachel_initial_usd);
         BOOST_CHECK_EQUAL(get_balance(paul, core), initial_balance_core - paul_initial_core);

         // Check Paul's debt to the blockchain
         // Rachel redeemed 2 bitUSD from the blockchain, and the blockchain closed this amount from Paul's debt to it
         BOOST_CHECK_EQUAL(paul_initial_usd - rachel_settle_amount, call_paul_id(db).debt.value);
         // The call order has the original amount of collateral less what was redeemed by Rachel
         BOOST_CHECK_EQUAL(paul_initial_core - rachel_fso_remainder_core, call_paul_id(db).collateral.value);


         ///////
         // 6. Asset owner attempts to claim the collateral fees.
         // Although no collateral-denominated fees should be present, the error should indicate the
         // that claiming such fees are not yet active.
         ///////
         BOOST_CHECK(bitusd.dynamic_asset_data_id(db).accumulated_collateral_fees == 0); // There should be no fees
         trx.clear();
         asset_claim_fees_operation claim_op;
         claim_op.issuer = assetowner.id;
         claim_op.extensions.value.claim_from_asset_id = bitusd.id;
         claim_op.amount_to_claim = core.amount(5 * core_unit);
         trx.operations.push_back(claim_op);
         sign(trx, assetowner_private_key);
         REQUIRE_EXCEPTION_WITH_TEXT(PUSH_TX(db, trx), "Collateral-denominated fees are not yet active");

         // Early proposals to claim should also fail
         proposal_create_operation cop;
         cop.review_period_seconds = 86400;
         uint32_t buffer_seconds = 60 * 60;
         cop.expiration_time = db.head_block_time() + *cop.review_period_seconds + buffer_seconds;
         cop.fee_paying_account = GRAPHENE_TEMP_ACCOUNT;
         cop.proposed_ops.emplace_back(claim_op);

         trx.clear();
         trx.operations.push_back(cop);
         // sign(trx, smartissuer_private_key);
         REQUIRE_EXCEPTION_WITH_TEXT(PUSH_TX(db, trx), "Collateral-denominated fees are not yet active");


         ///////
         // 7. Activate HARDFORK_CORE_BSIP87_TIME
         ///////
         generate_blocks(HARDFORK_CORE_BSIP87_TIME);
         generate_block();
         set_expiration(db, trx);
         trx.clear();

         // Update the price feed
         publish_feed(bitusd, feedproducer_id(db), current_feed);
         trx.clear();


         ///////
         // 8. Paul gives Michael 30 bitUSD and retains 50 bitUSD
         ///////
         int64_t michael_initial_usd = 30 * bitusd_unit;
         transfer(paul.id, michael.id, asset(michael_initial_usd, bitusd.id));

         // Check Michael's balance
         BOOST_CHECK_EQUAL(get_balance(michael, bitusd), michael_initial_usd);
         BOOST_CHECK_EQUAL(get_balance(michael, core), initial_balance_core);

         // Check Paul's balance
         BOOST_CHECK_EQUAL(get_balance(paul, bitusd), paul_initial_usd - rachel_initial_usd - michael_initial_usd);
         BOOST_CHECK_EQUAL(get_balance(paul, core), initial_balance_core - paul_initial_core);


         ///////
         // 9. Michael force-settles 5 bitUSD which should be collected from Paul's debt position
         ///////
         const int64_t michael_settle_amount = 5 * bitusd_unit;
         result = force_settle(michael, bitusd.amount(michael_settle_amount));

         force_settlement_id_type michael_settle_id = result.get<object_id_type>();
         BOOST_CHECK_EQUAL(michael_settle_id(db).balance.amount.value, michael_settle_amount);

         // Advance time to complete the force settlement and to update the price feed
         generate_blocks(db.head_block_time() + fc::hours(26));
         set_expiration(db, trx);
         trx.clear();
         publish_feed(bitusd, feedproducer_id(db), current_feed);

         // Michael's settlement should have completed and should no longer be present
         BOOST_CHECK(!db.find(michael_settle_id));

         // Check Michael's balance
         // Michael redeemed some smart asset and should get the equivalent collateral amount (according to the feed price)
         // minus the force_settlement_offset_fee - force_settlement_fee
         // Michael redeemed 5 bitUSD and should get 10000 satoshi Core - 500 satoshi Core - 0 satoshi  Core
         uint64_t michael_settle_core = michael_settle_amount * 20; // Settle amount * feed price
         uint64_t michael_fso_fee_core = michael_settle_core * usd_fso_percent / GRAPHENE_100_PERCENT;
         uint64_t michael_fso_remainder_core = michael_settle_core - michael_fso_fee_core;
         uint64_t michael_fsf_fee_core = (michael_fso_remainder_core) * usd_fsf_percent_0 / GRAPHENE_100_PERCENT;
         uint64_t expected_michael_core = michael_settle_core - michael_fso_fee_core - michael_fsf_fee_core;
         BOOST_CHECK_EQUAL(get_balance(michael, bitusd), michael_initial_usd - michael_settle_amount);
         BOOST_CHECK_EQUAL(get_balance(michael, core), initial_balance_core + expected_michael_core);

         // Check Paul's balance
         BOOST_CHECK_EQUAL(get_balance(paul, bitusd), paul_initial_usd - rachel_initial_usd - michael_initial_usd);
         BOOST_CHECK_EQUAL(get_balance(paul, core), initial_balance_core - paul_initial_core);

         // Check Paul's debt to the blockchain
         // Michael redeemed 5 bitUSD from the blockchain, and the blockchain closed this amount from Paul's debt to it
         BOOST_CHECK_EQUAL(paul_initial_usd - rachel_settle_amount - michael_settle_amount,
                           call_paul_id(db).debt.value);
         // The call order has the original amount of collateral less what was redeemed by Rachel, Michael
         BOOST_CHECK_EQUAL(paul_initial_core - rachel_fso_remainder_core - michael_fso_remainder_core,
                           call_paul_id(db).collateral.value);

         // The asset's force settlement fee % should still not be set
         BOOST_CHECK(!bitusd.bitasset_data(db).options.extensions.value.force_settle_fee_percent.valid());
         // There should be no accumulated asset fees
         BOOST_CHECK(bitusd.dynamic_asset_data_id(db).accumulated_fees == 0);
         BOOST_CHECK(bitusd.dynamic_asset_data_id(db).accumulated_collateral_fees == 0);


         ///////
         // 10. Asset owner sets the force-fee percentage to 3%
         ///////
         const uint16_t usd_fsf_percent_3 = 3 * GRAPHENE_1_PERCENT; // 3% Force-settlement fee % (BSIP87)
         asset_update_bitasset_operation uop;
         uop.issuer = assetowner.id;
         uop.asset_to_update = bitusd.get_id();
         uop.new_options = bitusd.bitasset_data(db).options;
         uop.new_options.extensions.value.force_settle_fee_percent = usd_fsf_percent_3;

         trx.clear();
         trx.operations.push_back(uop);
         db.current_fee_schedule().set_fee(trx.operations.back());
         sign(trx, assetowner_private_key);
         PUSH_TX(db, trx);

         // The force settlement fee % should be set
         BOOST_CHECK(bitusd.bitasset_data(db).options.extensions.value.force_settle_fee_percent.valid());
         BOOST_CHECK_EQUAL( usd_fsf_percent_3,
                            *bitusd.bitasset_data(db).options.extensions.value.force_settle_fee_percent
                          );

         ///////
         // 11. Paul gives Yanna 40 bitUSD and retains 10 bitUSD
         ///////
         int64_t yanna_initial_usd = 40 * bitusd_unit;
         transfer(paul.id, yanna.id, asset(yanna_initial_usd, bitusd.id));

         // Check Yanna's balance
         BOOST_CHECK_EQUAL(get_balance(yanna, bitusd), yanna_initial_usd);
         BOOST_CHECK_EQUAL(get_balance(yanna, core), 0);

         // Check Paul's balance
         BOOST_CHECK_EQUAL(get_balance(paul, bitusd),
                           paul_initial_usd - rachel_initial_usd - michael_initial_usd - yanna_initial_usd);
         BOOST_CHECK_EQUAL(get_balance(paul, core), initial_balance_core - paul_initial_core);


         ///////
         // 12. Yanna force-settles 10 bitUSD which should be collected from Paul's debt position
         ///////
         const int64_t yanna_settle_amount = 10 * bitusd_unit;
         result = force_settle(yanna, bitusd.amount(yanna_settle_amount));

         force_settlement_id_type yanna_settle_id = result.get<object_id_type>();
         BOOST_CHECK_EQUAL(yanna_settle_id(db).balance.amount.value, yanna_settle_amount);

         // Advance time to complete the force settlement and to update the price feed
         generate_blocks(db.head_block_time() + fc::hours(26));
         set_expiration(db, trx);
         trx.clear();
         publish_feed(bitusd, feedproducer_id(db), current_feed);

         // Yanna's settlement should have completed and should no longer be present
         BOOST_CHECK(!db.find(yanna_settle_id));

         // Check Yanna's balance
         // Yanna redeemed some smart asset and should get the equivalent collateral amount (according to the feed price)
         // minus the force_settlement_offset_fee - force_settlement_fee
         // Yanna redeemed 10 bitUSD and should get 20000 satoshi Core - 1000 satoshi Core - 570 satoshi  Core; (20000 - 1000) * 3% = 570
         uint64_t yanna_settle_core = yanna_settle_amount * 20; // Settle amount * feed price
         uint64_t yanna_fso_fee_core = yanna_settle_core * usd_fso_percent / GRAPHENE_100_PERCENT;
         uint64_t yanna_fso_remainder_core = yanna_settle_core - yanna_fso_fee_core;
         uint64_t yanna_fsf_fee_core = (yanna_fso_remainder_core) * usd_fsf_percent_3 / GRAPHENE_100_PERCENT;
         uint64_t expected_yanna_core = yanna_settle_core - yanna_fso_fee_core - yanna_fsf_fee_core;
         BOOST_CHECK_EQUAL(get_balance(yanna, bitusd), yanna_initial_usd - yanna_settle_amount);
         BOOST_CHECK_EQUAL(get_balance(yanna, core), 0 + expected_yanna_core);

         // Check Paul's balance
         BOOST_CHECK_EQUAL(get_balance(paul, bitusd),
                           paul_initial_usd - rachel_initial_usd - michael_initial_usd - yanna_initial_usd);
         BOOST_CHECK_EQUAL(get_balance(paul, core), initial_balance_core - paul_initial_core);

         // Check Paul's debt to the blockchain
         // Yanna redeemed 10 bitUSD from the blockchain, and the blockchain closed this amount from Paul's debt to it
         BOOST_CHECK_EQUAL(paul_initial_usd - rachel_settle_amount - michael_settle_amount - yanna_settle_amount,
                           call_paul_id(db).debt.value);
         // The call order has the original amount of collateral less what was redeemed by Rachel, Michael, Yanna
         BOOST_CHECK_EQUAL(paul_initial_core - rachel_fso_remainder_core - michael_fso_remainder_core - yanna_fso_remainder_core,
                           call_paul_id(db).collateral.value);

         // The asset's force settlement fee % should be valid
         BOOST_CHECK(bitusd.bitasset_data(db).options.extensions.value.force_settle_fee_percent.valid());
         // There should be some accumulated collateral-deonominated fees
         BOOST_CHECK(bitusd.dynamic_asset_data_id(db).accumulated_fees == 0);
         BOOST_CHECK(bitusd.dynamic_asset_data_id(db).accumulated_collateral_fees == yanna_fsf_fee_core);


         ///////
         // 13. Asset owner updates the force-settlement fee to 4%
         ///////
         const uint16_t usd_fsf_percent_4 = 4 * GRAPHENE_1_PERCENT; // 4% Force-settlement fee % (BSIP87)
         uop = asset_update_bitasset_operation();
         uop.issuer = assetowner.id;
         uop.asset_to_update = bitusd.get_id();
         uop.new_options = bitusd.bitasset_data(db).options;
         uop.new_options.extensions.value.force_settle_fee_percent = usd_fsf_percent_4;

         trx.clear();
         trx.operations.push_back(uop);
         db.current_fee_schedule().set_fee(trx.operations.back());
         sign(trx, assetowner_private_key);
         PUSH_TX(db, trx);

         // The force settlement fee % should be set
         BOOST_CHECK(bitusd.bitasset_data(db).options.extensions.value.force_settle_fee_percent.valid());
         BOOST_CHECK_EQUAL( usd_fsf_percent_4,
                            *bitusd.bitasset_data(db).options.extensions.value.force_settle_fee_percent
                          );

         ///////
         // 14. Paul gives Vikram 10 bitUSD and retains 0 bitUSD
         ///////
         int64_t vikram_initial_usd = 10 * bitusd_unit;
         transfer(paul.id, vikram.id, asset(vikram_initial_usd, bitusd.id));

         // Check Yanna's balance
         BOOST_CHECK_EQUAL(get_balance(vikram, bitusd), vikram_initial_usd);
         BOOST_CHECK_EQUAL(get_balance(vikram, core), 0);

         // Check Paul's balance
         BOOST_CHECK_EQUAL(get_balance(paul, bitusd),
                           paul_initial_usd - rachel_initial_usd - michael_initial_usd - yanna_initial_usd -
                           vikram_initial_usd);
         BOOST_CHECK_EQUAL(get_balance(paul, core), initial_balance_core - paul_initial_core);


         ///////
         // 15. Vikram force-settles 10 bitUSD which should be collected from Paul's debt position
         ///////
         const int64_t vikram_settle_amount = 10 * bitusd_unit;
         result = force_settle(vikram, bitusd.amount(vikram_settle_amount));

         force_settlement_id_type vikram_settle_id = result.get<object_id_type>();
         BOOST_CHECK_EQUAL(vikram_settle_id(db).balance.amount.value, vikram_settle_amount);

         // Advance time to complete the force settlement and to update the price feed
         generate_blocks(db.head_block_time() + fc::hours(26));
         set_expiration(db, trx);
         trx.clear();
         publish_feed(bitusd, feedproducer_id(db), current_feed);

         // Vikrams's settlement should have completed and should no longer be present
         BOOST_CHECK(!db.find(vikram_settle_id));

         // Check Vikrams's balance
         // Vikram redeemed some smart asset and should get the equivalent collateral amount (according to the feed price)
         // minus the force_settlement_offset_fee - force_settlement_fee
         // Vikram redeemed 10 bitUSD and should get 20000 satoshi Core - 1000 satoshi Core - 760 satoshi  Core; (20000 - 1000) * 4% = 760
         uint64_t vikram_settle_core = vikram_settle_amount * 20; // Settle amount * feed price
         uint64_t vikram_fso_fee_core = vikram_settle_core * usd_fso_percent / GRAPHENE_100_PERCENT;
         uint64_t vikram_fso_remainder_core = vikram_settle_core - vikram_fso_fee_core;
         uint64_t vikram_fsf_fee_core = (vikram_fso_remainder_core) * usd_fsf_percent_4 / GRAPHENE_100_PERCENT;
         uint64_t expected_vikram_core = vikram_settle_core - vikram_fso_fee_core - vikram_fsf_fee_core;
         BOOST_CHECK_EQUAL(get_balance(vikram, bitusd), vikram_initial_usd - vikram_settle_amount);
         BOOST_CHECK_EQUAL(get_balance(vikram, core), 0 + expected_vikram_core);

         // Check Paul's balance
         BOOST_CHECK_EQUAL(get_balance(paul, bitusd),
                           paul_initial_usd - rachel_initial_usd - michael_initial_usd - yanna_initial_usd -
                           vikram_initial_usd);
         BOOST_CHECK_EQUAL(get_balance(paul, core), initial_balance_core - paul_initial_core);

         // Check Paul's debt to the blockchain
         // Vikram redeemed 10 bitUSD from the blockchain, and the blockchain closed this amount from Paul's debt to it
         BOOST_CHECK_EQUAL(paul_initial_usd - rachel_settle_amount - michael_settle_amount - yanna_settle_amount -
                           vikram_settle_amount,
                           call_paul_id(db).debt.value);
         // The call order has the original amount of collateral less what was redeemed by Rachel, Michael, Yanna, Vikram
         BOOST_CHECK_EQUAL(
                 paul_initial_core - rachel_fso_remainder_core - michael_fso_remainder_core - yanna_fso_remainder_core -
                 vikram_fso_remainder_core,
                 call_paul_id(db).collateral.value);

         // The asset's force settlement fee % should still not be set
         BOOST_CHECK(bitusd.bitasset_data(db).options.extensions.value.force_settle_fee_percent.valid());
         // There should be some accumulated collateral-deonominated fees
         BOOST_CHECK(bitusd.dynamic_asset_data_id(db).accumulated_fees == 0);
         const uint64_t expected_accumulation_fsf_core_amount = yanna_fsf_fee_core + vikram_fsf_fee_core;
         BOOST_CHECK(
                 bitusd.dynamic_asset_data_id(db).accumulated_collateral_fees == expected_accumulation_fsf_core_amount);


         ///////
         // 16. Asset owner attempts and fails to change the backing of the smart asset
         // because of its outstanding supply
         ///////
         // Create a new user-issued asset
         trx.clear();
         ACTOR(jill);
         trx.clear();
         price core_exchange_rate(asset(1, asset_id_type(1)), asset(1));
         uint16_t market_fee_percent = 20 * GRAPHENE_1_PERCENT;
         create_user_issued_asset("JCOIN", jill, charge_market_fee, core_exchange_rate, 2, market_fee_percent);
         generate_block();
         trx.clear();
         set_expiration(db, trx);
         const asset_object& jillcoin = get_asset("JCOIN");


         // Attempt to change the backing of the smart asset to the new user-issued asset
         trx.clear();
         asset_update_bitasset_operation change_backing_asset_op;
         change_backing_asset_op.asset_to_update = bitusd.id;
         change_backing_asset_op.issuer = assetowner.id;
         change_backing_asset_op.new_options.short_backing_asset = jillcoin.id;
         trx.operations.push_back(change_backing_asset_op);
         sign(trx, assetowner_private_key);
         REQUIRE_EXCEPTION_WITH_TEXT(PUSH_TX(db, trx), "there is already a current supply");


         ///////
         // 17. All current holdings of bitUSD are removed
         ///////
         // Rachel, Michael, and Yanna return their remaining bitUSD to Paul
         trx.clear();
         transfer(rachel.id, paul.id, bitusd.amount(get_balance(rachel, bitusd)));
         transfer(michael.id, paul.id, bitusd.amount(get_balance(michael, bitusd)));
         transfer(yanna.id, paul.id, bitusd.amount(get_balance(yanna, bitusd)));

         // Vikram has no bitUSD to transfer
         BOOST_CHECK_EQUAL(get_balance(vikram, bitusd), 0);

         // Paul closes his debt to the blockchain
         cover(paul, bitusd.amount(call_paul_id(db).debt), core.amount(call_paul_id(db).collateral.value));

         // Check the bitUSD holdings of the actors
         BOOST_CHECK_EQUAL(get_balance(assetowner, bitusd), 0);
         BOOST_CHECK_EQUAL(get_balance(paul, bitusd), 0);
         BOOST_CHECK_EQUAL(get_balance(rachel, bitusd), 0);
         BOOST_CHECK_EQUAL(get_balance(michael, bitusd), 0);
         BOOST_CHECK_EQUAL(get_balance(yanna, bitusd), 0);
         BOOST_CHECK_EQUAL(get_balance(vikram, bitusd), 0);


         ///////
         // 18. Asset owner attempts and fails to change the backing of the smart asset
         // because of unclaimed collateral fees
         ///////
         // Repeat check of the accumulated fees
         BOOST_CHECK(bitusd.dynamic_asset_data_id(db).accumulated_fees == 0);
         BOOST_CHECK(
                 bitusd.dynamic_asset_data_id(db).accumulated_collateral_fees == expected_accumulation_fsf_core_amount);

         trx.clear();
         trx.operations.push_back(change_backing_asset_op);
         sign(trx, assetowner_private_key);
         REQUIRE_EXCEPTION_WITH_TEXT(PUSH_TX(db, trx), "Must claim collateral-denominated fees");


         ///////
         // 19. Asset owner claims all of the unclaimed collateral fees
         ///////
         trx.clear();
         claim_op = asset_claim_fees_operation();
         claim_op.issuer = assetowner.id;
         claim_op.extensions.value.claim_from_asset_id = bitusd.id;
         claim_op.amount_to_claim = core.amount(expected_accumulation_fsf_core_amount);
         trx.operations.push_back(claim_op);
         sign(trx, assetowner_private_key);
         PUSH_TX(db, trx);

         BOOST_CHECK(bitusd.dynamic_asset_data_id(db).accumulated_fees == 0);
         BOOST_CHECK(bitusd.dynamic_asset_data_id(db).accumulated_collateral_fees == 0);


         ///////
         // 20. Asset owner attempts and succeeds in changing the backing of the smart asset
         ///////
         // Confirm that the asset is backed by CORE
         const asset_bitasset_data_object& bitusd_bitasset_data = (*bitusd.bitasset_data_id)(db);
         BOOST_CHECK(bitusd_bitasset_data.options.short_backing_asset == core.id);

         trx.clear();
         trx.operations.push_back(change_backing_asset_op);
         sign(trx, assetowner_private_key);
         PUSH_TX(db, trx);

         // Confirm the change to the backing asset
         BOOST_CHECK(bitusd_bitasset_data.options.short_backing_asset == jillcoin.id);

      }
      FC_LOG_AND_RETHROW()
   }


   /**
    * Attempt to claim invalid fees
    */
   BOOST_AUTO_TEST_CASE(force_settle_fee_invalid_claims_test) {
      try {
         INVOKE(force_settle_fee_1_test);

         GET_ACTOR(assetowner);

         // Check the asset owner's accumulated asset fees
         const auto &core = asset_id_type()(db);
         const int64_t core_unit = asset::scaled_precision(core.precision).value; // 100000 satoshi CORE in 1 CORE
         const asset_object& bitusd = get_asset("USDBIT");
         BOOST_CHECK(bitusd.dynamic_asset_data_id(db).accumulated_fees == 0);
         BOOST_CHECK(bitusd.dynamic_asset_data_id(db).accumulated_collateral_fees > 0);
         share_type rachel_fsf_fee_core = bitusd.dynamic_asset_data_id(db).accumulated_collateral_fees;

         // Attempt to claim negative fees
         trx.clear();
         asset_claim_fees_operation claim_op;
         claim_op.issuer = assetowner.id;
         claim_op.extensions.value.claim_from_asset_id = bitusd.id;
         claim_op.amount_to_claim = core.amount(-5 * core_unit);
         trx.operations.push_back(claim_op);
         sign(trx, assetowner_private_key);
         REQUIRE_EXCEPTION_WITH_TEXT(PUSH_TX(db, trx), "amount_to_claim.amount > 0");

         // Attempt to claim 0 fees
         trx.clear();
         claim_op = asset_claim_fees_operation();
         claim_op.issuer = assetowner.id;
         claim_op.extensions.value.claim_from_asset_id = bitusd.id;
         claim_op.amount_to_claim = core.amount(0 * core_unit);
         trx.operations.push_back(claim_op);
         set_expiration(db, trx);
         sign(trx, assetowner_private_key);
         REQUIRE_EXCEPTION_WITH_TEXT(PUSH_TX(db, trx), "amount_to_claim.amount > 0");

         // Attempt to claim excessive claim fee
         trx.clear();
         claim_op = asset_claim_fees_operation();
         claim_op.issuer = assetowner.id;
         claim_op.extensions.value.claim_from_asset_id = bitusd.id;
         claim_op.amount_to_claim = rachel_fsf_fee_core + 1;
         trx.operations.push_back(claim_op);
         sign(trx, assetowner_private_key);
         REQUIRE_EXCEPTION_WITH_TEXT(PUSH_TX(db, trx), "Attempt to claim more backing-asset fees");

         // Attempt to claim with an invalid asset asset type
         trx.clear();
         ACTOR(jill);
         price price(asset(1, asset_id_type(1)), asset(1));
         uint16_t market_fee_percent = 20 * GRAPHENE_1_PERCENT;
         create_user_issued_asset("JCOIN", jill, charge_market_fee, price, 2, market_fee_percent);
         generate_block(); trx.clear(); set_expiration(db, trx);
         const asset_object& jillcoin = get_asset("JCOIN");

         trx.clear();
         claim_op = asset_claim_fees_operation();
         claim_op.issuer = assetowner.id;
         claim_op.extensions.value.claim_from_asset_id = bitusd.id;
         claim_op.amount_to_claim = jillcoin.amount(rachel_fsf_fee_core.value);
         trx.operations.push_back(claim_op);
         sign(trx, assetowner_private_key);
         REQUIRE_EXCEPTION_WITH_TEXT(PUSH_TX(db, trx), "is not backed by asset");

         // Attempt to claim part of all that can be claimed
         share_type partial_claim_core = 1; // 1 satoshi
         share_type expected_remainder_core = rachel_fsf_fee_core - partial_claim_core;
         FC_ASSERT(expected_remainder_core.value > 0); // Remainder should be positive
         trx.clear();
         claim_op = asset_claim_fees_operation();
         claim_op.issuer = assetowner.id;
         claim_op.extensions.value.claim_from_asset_id = bitusd.id;
         claim_op.amount_to_claim = partial_claim_core;
         trx.operations.push_back(claim_op);
         set_expiration(db, trx);
         sign(trx, assetowner_private_key);
         PUSH_TX(db, trx);
         BOOST_CHECK(bitusd.dynamic_asset_data_id(db).accumulated_collateral_fees == expected_remainder_core);

         // Attempt to claim all that can be claimed
         generate_block();
         trx.clear();
         claim_op = asset_claim_fees_operation();
         claim_op.issuer = assetowner.id;
         claim_op.extensions.value.claim_from_asset_id = bitusd.id;
         claim_op.amount_to_claim = expected_remainder_core;
         trx.operations.push_back(claim_op);
         sign(trx, assetowner_private_key);
         PUSH_TX(db, trx);
         BOOST_CHECK(bitusd.dynamic_asset_data_id(db).accumulated_collateral_fees == 0);

      }
      FC_LOG_AND_RETHROW()
   }


   /**
    * Test 100% force settlement fee.
    *
    * There are two primary actors: paul, rachel
    *
    * 1. Asset owner creates the smart coin called bitUSD
    * 2. The feed price is 1 satoshi bitUSD for 20 satoshi Core = 0.01 bitUSD for 0.00020 Core = 50 bitUSD for 1 Core
    * 3. Paul borrows 100 bitUSD (10000 satoshis of bitUSD) from the blockchain with a low amount of collateral
    * 4. Paul gives Rachel 20 bitUSD
    * 5. Rachel force-settles 2 bitUSD which should be collected from Paul's debt position
    * because of its relatively lower collateral ratio
    *
    * The force-settlement by Rachel should account for both the force-settlement offset fee,
    * and the new force settlement fee from BSIP87.
    */
   BOOST_AUTO_TEST_CASE(force_settle_fee_extreme_1_test) {
      try {
         ///////
         // Initialize the scenario
         ///////
         // Advance to the when the force-settlement fee activates
         generate_blocks(HARDFORK_CORE_BSIP87_TIME);
         generate_block();
         set_expiration(db, trx);
         trx.clear();

         // Create actors
         ACTORS((assetowner)(feedproducer)(paul)(rachel));

         // Fund actors
         uint64_t initial_balance_core = 10000000;
         transfer(committee_account, assetowner.id, asset(initial_balance_core));
         transfer(committee_account, feedproducer.id, asset(initial_balance_core));
         transfer(committee_account, paul.id, asset(initial_balance_core));

         // 1. Create assets
         const uint16_t usd_fso_percent = 5 * GRAPHENE_1_PERCENT; // 5% Force-settlement offset fee %
         const uint16_t usd_fsf_percent = 100 * GRAPHENE_1_PERCENT; // 100% Force-settlement fee % (BSIP87)
         create_smart_asset("USDBIT", assetowner.id, usd_fso_percent, usd_fsf_percent);

         generate_block();
         set_expiration(db, trx);
         trx.clear();

         const auto &bitusd = get_asset("USDBIT");
         const int64_t bitusd_unit = asset::scaled_precision(bitusd.precision).value; // 100 satoshi USDBIT in 1 USDBIT
         const auto &core = asset_id_type()(db);


         ///////
         // 2. Publish a feed for the smart asset
         ///////
         update_feed_producers(bitusd.id, {feedproducer_id});
         price_feed current_feed;
         current_feed.maintenance_collateral_ratio = 1750;
         current_feed.maximum_short_squeeze_ratio = 1100;
         // Requirement of 20x collateral in satoshis: 1 satoshi bitUSD for 20 satoshi Core
         // -> 0.01 bitUSD for 0.00020 Core = 100 bitUSD for 2 Core = 50 bitUSD for 1 Core
         current_feed.settlement_price = bitusd.amount(1) / core.amount(20);
         publish_feed(bitusd, feedproducer, current_feed);


         ///////
         // 3. Paul borrows 100 bitUSD
         ///////
         // Paul will borrow bitUSD by providing 2x collateral required: 2 * 20 = 40
         int64_t paul_initial_usd = 100 * bitusd_unit; // 10000
         int64_t paul_initial_core = paul_initial_usd * 2 * 20; // 400000
         const call_order_object &call_paul = *borrow(paul, bitusd.amount(paul_initial_usd),
                                                      core.amount(paul_initial_core));
         call_order_id_type call_paul_id = call_paul.id;
         BOOST_REQUIRE_EQUAL(get_balance(paul, bitusd), paul_initial_usd);

         BOOST_CHECK_EQUAL(get_balance(paul, bitusd), paul_initial_usd);
         BOOST_CHECK_EQUAL(get_balance(paul, core), initial_balance_core - paul_initial_core);


         ///////
         // 4. Paul gives Rachel 20 bitUSD and retains 80 bitUSD
         ///////
         int64_t rachel_initial_usd = 20 * bitusd_unit;
         transfer(paul.id, rachel.id, asset(rachel_initial_usd, bitusd.id));

         BOOST_CHECK_EQUAL(get_balance(rachel, bitusd), rachel_initial_usd);
         BOOST_CHECK_EQUAL(get_balance(rachel, core), 0);

         BOOST_CHECK_EQUAL(get_balance(paul, bitusd), paul_initial_usd - rachel_initial_usd);
         BOOST_CHECK_EQUAL(get_balance(paul, core), initial_balance_core - paul_initial_core);


         ///////
         // 5. Rachel force-settles 2 bitUSD which should be collected from Paul's debt position
         ///////
         const int64_t rachel_settle_amount = 2 * bitusd_unit; // 200 satoshi bitusd
         operation_result result = force_settle(rachel, bitusd.amount(rachel_settle_amount));

         force_settlement_id_type rachel_settle_id = result.get<object_id_type>();
         BOOST_CHECK_EQUAL(rachel_settle_id(db).balance.amount.value, rachel_settle_amount);

         // Advance time to complete the force settlement and to update the price feed
         generate_blocks(db.head_block_time() + fc::hours(26));
         set_expiration(db, trx);
         trx.clear();
         publish_feed(bitusd, feedproducer_id(db), current_feed);
         trx.clear();

         // Rachel's settlement should have completed and should no longer be present
         BOOST_CHECK(!db.find(rachel_settle_id));

         // Check Rachel's balance
         // Rachel redeemed some smart asset and should get the equivalent collateral amount (according to the feed price)
         // minus the force_settlement_offset_fee - force_settlement_fee
         // uint64_t rachel_settle_core = 4000; // rachel_settle_amount * 20
         // uint64_t rachel_fso_fee_core = 200; // rachel_settle_core * usd_fso_percent / GRAPHENE_100_PERCENT
         uint64_t rachel_fso_remainder_core = 3800; // rachel_settle_core - rachel_fso_fee_core
         uint64_t rachel_fsf_fee_core = 3800; // (rachel_fso_remainder_core) * usd_fsf_percent / GRAPHENE_100_PERCENT
         // Rachel redeemed 2 bitUSD and should get 4000 satoshi Core - 200 satoshi Core - 3800 satoshi  Core
         uint64_t expected_rachel_core = 0; // rachel_settle_core - rachel_fso_fee_core - rachel_fsf_fee_core
         BOOST_CHECK_EQUAL(get_balance(rachel, bitusd), rachel_initial_usd - rachel_settle_amount);
         BOOST_CHECK_EQUAL(get_balance(rachel, core), expected_rachel_core);

         // Check Paul's balance
         BOOST_CHECK_EQUAL(get_balance(paul, bitusd), paul_initial_usd - rachel_initial_usd);
         BOOST_CHECK_EQUAL(get_balance(paul, core), initial_balance_core - paul_initial_core);

         // Check Paul's debt to the blockchain
         // Rachel redeemed 2 bitUSD from the blockchain, and the blockchain closed this amount from Paul's debt to it
         BOOST_CHECK_EQUAL(paul_initial_usd - rachel_settle_amount, call_paul_id(db).debt.value);
         // The call order has the original amount of collateral less what was redeemed by Rachel
         BOOST_CHECK_EQUAL(paul_initial_core - rachel_fso_remainder_core, call_paul_id(db).collateral.value);

         // Check the asset owner's accumulated asset fees
         BOOST_CHECK(bitusd.dynamic_asset_data_id(db).accumulated_fees == 0);
         BOOST_CHECK(bitusd.dynamic_asset_data_id(db).accumulated_collateral_fees == rachel_fsf_fee_core);

      }
      FC_LOG_AND_RETHROW()
   }


   /**
    * Test the ability to create and update assets with force-settlement fee % before HARDFORK_CORE_BSIP87_TIME
    *
    *
    * Before HARDFORK_CORE_BSIP87_TIME
    *
    * 1. Asset owner fails to create the smart coin called USDBIT with a force-settlement fee %
    * 2. Asset owner fails to create the smart coin called USDBIT with a force-settlement fee % in a proposal
    * 3. Asset owner succeeds to create the smart coin called USDBIT without a force-settlement fee %
    *
    * 4. Asset owner fails to update the smart coin with a force-settlement fee %
    * 5. Asset owner fails to update the smart coin with a force-settlement fee % in a proposal
    *
    * 6. Asset owner fails to claim collateral-denominated fees
    * 7. Asset owner fails to claim collateral-denominated fees in a proposal
    *
    *
    * 8. Activate HARDFORK_CORE_BSIP87_TIME
    *
    *
    * After HARDFORK_CORE_BSIP87_TIME
    *
    * 9. Asset owner succeeds to create the smart coin called CNYBIT with a force-settlement fee %
    * 10. Asset owner succeeds to create the smart coin called RUBBIT with a force-settlement fee % in a proposal
    *
    * 11. Asset owner succeeds to update the smart coin called CNYBIT with a force-settlement fee %
    * 12. Asset owner succeeds to update the smart coin called RUBBIT with a force-settlement fee % in a proposal
    */
   BOOST_AUTO_TEST_CASE(prevention_before_hardfork_test) {
      try {
         ///////
         // Initialize the scenario
         ///////
         // Get around Graphene issue #615 feed expiration bug
         generate_blocks(HARDFORK_615_TIME);
         generate_blocks(db.get_dynamic_global_properties().next_maintenance_time);
         trx.clear();
         set_expiration(db, trx);

         // Create actors
         ACTORS((assetowner));

         // Fund actors
         uint64_t initial_balance_core = 10000000;
         transfer(committee_account, assetowner.id, asset(initial_balance_core));

         // Confirm before hardfork activation
         BOOST_CHECK(db.head_block_time() < HARDFORK_CORE_BSIP87_TIME);


         ///////
         // 1. Asset owner fails to create the smart coin called bitUSD with a force-settlement fee %
         ///////
         const uint16_t usd_fso_percent = 5 * GRAPHENE_1_PERCENT; // 5% Force-settlement offset fee %
         const uint16_t usd_fsf_percent_0 = 0 * GRAPHENE_1_PERCENT; // 0% Force-settlement fee %

         // Attempt to create the smart asset with a force-settlement fee %
         // The attempt should fail because it is before HARDFORK_CORE_BSIP87_TIME
         trx.clear();
         REQUIRE_EXCEPTION_WITH_TEXT(create_smart_asset("USDBIT", assetowner.id, usd_fso_percent, usd_fsf_percent_0),
                                     "cannot be set before Hardfork BSIP87");


         ///////
         // 2. Asset owner fails to create the smart coin called bitUSD with a force-settlement fee % in a proposal
         ///////
         {
            asset_create_operation create_op = create_smart_asset_op("USDBIT", assetowner.id, usd_fso_percent,
                                                                     usd_fsf_percent_0);
            proposal_create_operation cop;
            cop.review_period_seconds = 86400;
            uint32_t buffer_seconds = 60 * 60;
            cop.expiration_time = db.head_block_time() + *cop.review_period_seconds + buffer_seconds;
            cop.fee_paying_account = GRAPHENE_TEMP_ACCOUNT;
            cop.proposed_ops.emplace_back(create_op);

            trx.clear();
            trx.operations.push_back(cop);
            // sign(trx, assetowner_private_key);
            REQUIRE_EXCEPTION_WITH_TEXT(PUSH_TX(db, trx), "cannot be set before Hardfork BSIP87");
         }


         ///////
         // 3. Asset owner succeeds to create the smart coin called bitUSD without a force-settlement fee %
         ///////
         trx.clear();
         create_smart_asset("USDBIT", assetowner.id, usd_fso_percent);

         generate_block();
         set_expiration(db, trx);
         trx.clear();

         const auto &bitusd = get_asset("USDBIT");
         const auto &core = asset_id_type()(db);


         ///////
         // 4. Asset owner fails to update the smart coin with a force-settlement fee %
         ///////
         const uint16_t usd_fsf_percent_3 = 3 * GRAPHENE_1_PERCENT; // 3% Force-settlement fee % (BSIP87)
         asset_update_bitasset_operation uop;
         uop.issuer = assetowner.id;
         uop.asset_to_update = bitusd.get_id();
         uop.new_options = bitusd.bitasset_data(db).options;
         uop.new_options.extensions.value.force_settle_fee_percent = usd_fsf_percent_3;

         trx.clear();
         trx.operations.push_back(uop);
         db.current_fee_schedule().set_fee(trx.operations.back());
         sign(trx, assetowner_private_key);
         REQUIRE_EXCEPTION_WITH_TEXT(PUSH_TX(db, trx), "cannot be set before Hardfork BSIP87");

         // The force settlement fee % should not be set
         BOOST_CHECK(!bitusd.bitasset_data(db).options.extensions.value.force_settle_fee_percent.valid());


         ///////
         // 5. Asset owner fails to update the smart coin with a force-settlement fee % in a proposal
         ///////
         {
            proposal_create_operation cop;
            cop.review_period_seconds = 86400;
            uint32_t buffer_seconds = 60 * 60;
            cop.expiration_time = db.head_block_time() + *cop.review_period_seconds + buffer_seconds;
            cop.fee_paying_account = GRAPHENE_TEMP_ACCOUNT;
            cop.proposed_ops.emplace_back(uop);

            trx.clear();
            trx.operations.push_back(cop);
            // sign(trx, assetowner_private_key);
            REQUIRE_EXCEPTION_WITH_TEXT(PUSH_TX(db, trx), "cannot be set before Hardfork BSIP87");

            // The force settlement fee % should not be set
            BOOST_CHECK(!bitusd.bitasset_data(db).options.extensions.value.force_settle_fee_percent.valid());
         }


         ///////
         // 6. Asset owner fails to claim collateral-denominated fees
         ///////
         // Although no collateral-denominated fees should be present, the error should indicate the
         // that claiming such fees are not yet active.
         BOOST_CHECK(bitusd.dynamic_asset_data_id(db).accumulated_collateral_fees == 0); // There should be no fees
         trx.clear();
         asset_claim_fees_operation claim_op;
         claim_op.issuer = assetowner.id;
         claim_op.extensions.value.claim_from_asset_id = bitusd.id;
         claim_op.amount_to_claim = core.amount(5);
         trx.operations.push_back(claim_op);
         sign(trx, assetowner_private_key);
         REQUIRE_EXCEPTION_WITH_TEXT(PUSH_TX(db, trx), "Collateral-denominated fees are not yet active");


         ///////
         // 7. Asset owner fails to claim collateral-denominated fees in a proposal
         ///////
         proposal_create_operation cop;
         cop.review_period_seconds = 86400;
         uint32_t buffer_seconds = 60 * 60;
         cop.expiration_time = db.head_block_time() + *cop.review_period_seconds + buffer_seconds;
         cop.fee_paying_account = GRAPHENE_TEMP_ACCOUNT;
         cop.proposed_ops.emplace_back(claim_op);

         trx.clear();
         trx.operations.push_back(cop);
         // sign(trx, assetowner_private_key);
         REQUIRE_EXCEPTION_WITH_TEXT(PUSH_TX(db, trx), "Collateral-denominated fees are not yet active");



         ///////
         // 8. Activate HARDFORK_CORE_BSIP87_TIME
         ///////
         BOOST_CHECK(db.head_block_time() < HARDFORK_CORE_BSIP87_TIME); // Confirm still before hardfork activation
         generate_blocks(HARDFORK_CORE_BSIP87_TIME);
         generate_block();
         set_expiration(db, trx);
         trx.clear();


         ///////
         // 9. Asset owner succeeds to create the smart coin called CNYBIT with a force-settlement fee %
         ///////
         const uint16_t fsf_percent_1 = 1 * GRAPHENE_1_PERCENT; // 1% Force-settlement fee % (BSIP87)
         const uint16_t fsf_percent_5 = 1 * GRAPHENE_1_PERCENT; // 5% Force-settlement fee % (BSIP87)
         trx.clear();
         create_smart_asset("CNYBIT", assetowner.id, usd_fso_percent, fsf_percent_1);

         generate_block();
         set_expiration(db, trx);
         trx.clear();

         const auto &bitcny = get_asset("CNYBIT");

         // The force settlement fee % should be set
         BOOST_CHECK(bitcny.bitasset_data(db).options.extensions.value.force_settle_fee_percent.valid());
         BOOST_CHECK_EQUAL(fsf_percent_1, *bitcny.bitasset_data(db).options.extensions.value.force_settle_fee_percent);


         ///////
         // 10. Asset owner succeeds to create the smart coin called RUBBIT with a force-settlement fee % in a proposal
         ///////
         {
            // Create the proposal
            asset_create_operation create_op = create_smart_asset_op("RUBBIT", assetowner.id, usd_fso_percent,
                                                                     fsf_percent_1);
            proposal_create_operation cop;
            cop.review_period_seconds = 86400;
            uint32_t buffer_seconds = 60 * 60;
            cop.expiration_time = db.head_block_time() + *cop.review_period_seconds + buffer_seconds;
            cop.fee_paying_account = GRAPHENE_TEMP_ACCOUNT;
            cop.proposed_ops.emplace_back(create_op);

            trx.clear();
            trx.operations.push_back(cop);
            // sign(trx, assetowner_private_key);
            processed_transaction processed = PUSH_TX(db, trx);


            // Approve the proposal
            proposal_id_type pid = processed.operation_results[0].get<object_id_type>();

            proposal_update_operation pup;
            pup.fee_paying_account = assetowner_id;
            pup.proposal = pid;
            pup.active_approvals_to_add.insert(assetowner_id);
            trx.clear();
            trx.operations.push_back(pup);
            set_expiration(db, trx);
            sign(trx, assetowner_private_key);

            PUSH_TX(db, trx); // No exception should be thrown


            // Advance to the activation of the proposal
            generate_blocks(cop.expiration_time);
            set_expiration(db, trx);
         }
         const auto &bitrub = get_asset("RUBBIT");

         // The force settlement fee % should be set
         BOOST_CHECK(bitrub.bitasset_data(db).options.extensions.value.force_settle_fee_percent.valid());
         BOOST_CHECK_EQUAL(fsf_percent_1, *bitrub.bitasset_data(db).options.extensions.value.force_settle_fee_percent);


         ///////
         // 11. Asset owner succeeds to update the smart coin called CNYBIT with a force-settlement fee %
         ///////
         uop = asset_update_bitasset_operation();
         uop.issuer = assetowner.id;
         uop.asset_to_update = bitcny.get_id();
         uop.new_options = bitcny.bitasset_data(db).options;
         uop.new_options.extensions.value.force_settle_fee_percent = fsf_percent_5;

         trx.clear();
         trx.operations.push_back(uop);
         db.current_fee_schedule().set_fee(trx.operations.back());
         sign(trx, assetowner_private_key);
         PUSH_TX(db, trx);

         // The force settlement fee % should be set
         BOOST_CHECK(bitcny.bitasset_data(db).options.extensions.value.force_settle_fee_percent.valid());
         BOOST_CHECK_EQUAL(fsf_percent_5, *bitcny.bitasset_data(db).options.extensions.value.force_settle_fee_percent);


         ///////
         // 12. Asset owner succeeds to update the smart coin called RUBBIT with a force-settlement fee % in a proposal
         ///////
         {
            // Create the proposal
            uop = asset_update_bitasset_operation();
            uop.issuer = assetowner.id;
            uop.asset_to_update = bitrub.get_id();
            uop.new_options = bitrub.bitasset_data(db).options;
            uop.new_options.extensions.value.force_settle_fee_percent = fsf_percent_5;

            proposal_create_operation cop;
            cop.review_period_seconds = 86400;
            uint32_t buffer_seconds = 60 * 60;
            cop.expiration_time = db.head_block_time() + *cop.review_period_seconds + buffer_seconds;
            cop.fee_paying_account = GRAPHENE_TEMP_ACCOUNT;
            cop.proposed_ops.emplace_back(uop);

            trx.clear();
            trx.operations.push_back(cop);
            // sign(trx, assetowner_private_key);
            processed_transaction processed = PUSH_TX(db, trx);


            // Approve the proposal
            proposal_id_type pid = processed.operation_results[0].get<object_id_type>();

            proposal_update_operation pup;
            pup.fee_paying_account = assetowner_id;
            pup.proposal = pid;
            pup.active_approvals_to_add.insert(assetowner_id);
            trx.clear();
            trx.operations.push_back(pup);
            set_expiration(db, trx);
            sign(trx, assetowner_private_key);

            PUSH_TX(db, trx); // No exception should be thrown

            // Advance to the activation of the proposal
            generate_blocks(cop.expiration_time);
            set_expiration(db, trx);
         }

         // The force settlement fee % should be set
         BOOST_CHECK(bitrub.bitasset_data(db).options.extensions.value.force_settle_fee_percent.valid());
         BOOST_CHECK_EQUAL(fsf_percent_5, *bitrub.bitasset_data(db).options.extensions.value.force_settle_fee_percent);

      }
      FC_LOG_AND_RETHROW()
   }

BOOST_AUTO_TEST_SUITE_END()
