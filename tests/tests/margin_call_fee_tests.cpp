/*
 * Copyright (c) 2020 Contributors
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

#include "../common/database_fixture.hpp"

// For account history
#include <vector>
#include <graphene/app/api.hpp>


using namespace graphene::chain;
using namespace graphene::chain::test;

struct bitasset_database_fixture : database_fixture {
   bitasset_database_fixture()
           : database_fixture() {
   }

   const limit_order_create_operation
   create_sell_operation(account_id_type user, const asset &amount, const asset &recv) {
      const time_point_sec order_expiration = time_point_sec::maximum();
      const price &fee_core_exchange_rate = price::unit_price();
      limit_order_create_operation op = create_sell_operation(user, amount, recv, order_expiration,
                                                              fee_core_exchange_rate);
      return op;
   }

   const limit_order_create_operation
   create_sell_operation(account_id_type user, const asset &amount, const asset &recv,
                         const time_point_sec order_expiration,
                         const price &fee_core_exchange_rate) {
      limit_order_create_operation op = create_sell_operation(user(db), amount, recv, order_expiration,
                                                              fee_core_exchange_rate);
      return op;
   }

   const limit_order_create_operation
   create_sell_operation(const account_object &user, const asset &amount, const asset &recv,
                         const time_point_sec order_expiration,
                         const price &fee_core_exchange_rate) {
      limit_order_create_operation sell_order;
      sell_order.seller = user.id;
      sell_order.amount_to_sell = amount;
      sell_order.min_to_receive = recv;
      sell_order.expiration = order_expiration;

      return sell_order;
   }

   const asset_create_operation create_user_issued_asset_operation(const string &name, const account_object &issuer,
                                                                   uint16_t flags, const price &core_exchange_rate,
                                                                   uint8_t precision, uint16_t maker_fee_percent,
                                                                   uint16_t taker_fee_percent) {
      asset_create_operation creator;
      creator.issuer = issuer.id;
      creator.fee = asset();
      creator.symbol = name;
      creator.common_options.max_supply = 0;
      creator.precision = precision;

      creator.common_options.core_exchange_rate = core_exchange_rate;
      creator.common_options.max_supply = GRAPHENE_MAX_SHARE_SUPPLY;
      creator.common_options.flags = flags;
      creator.common_options.issuer_permissions = flags;
      creator.common_options.market_fee_percent = maker_fee_percent;
      creator.common_options.extensions.value.taker_fee_percent = taker_fee_percent;

      return creator;

   }
};


BOOST_FIXTURE_TEST_SUITE(margin_call_fee_tests, bitasset_database_fixture)

   /**
    * Test the effects of different MCFRs on derived prices and ratios
    */
   BOOST_AUTO_TEST_CASE(mcfr_tests) {
      try {
         ACTORS((charlie))
         const asset_id_type core_id;

         BOOST_TEST_MESSAGE("Advancing past Hardfork BSIP74");
         generate_blocks(HARDFORK_CORE_BSIP74_TIME);
         generate_block();
         set_expiration(db, trx);

         //////
         // Initialize
         //////
         fc::optional<uint16_t> mcfr;
         price expected_offer_price; // The price offered by the margin call
         price expected_paid_price; // The effective price paid by the margin call
         ratio_type expected_margin_call_pays_ratio;

         const asset_object core = core_id(db);
         const uint16_t smartbit_market_fee_percent = 2 * GRAPHENE_1_PERCENT;
         create_bitasset("SMARTBIT2", charlie_id, smartbit_market_fee_percent, charge_market_fee, 2);
         generate_block();
         const asset_object smartbit2 = get_asset("SMARTBIT2");
         BOOST_CHECK_EQUAL(2, smartbit2.precision);

         // Construct a price feed
         // Initial price of 1 satoshi SMARTBIT2 for 20 satoshi CORE
         // = 0.0001 SMARTBIT2 for 0.00020 CORE = 1 SMARTBIT2 for 2 CORE
         const price initial_price =
                 smartbit2.amount(1) / core.amount(20); // 1 satoshi SMARTBIT2 for 20 satoshi CORE

         price_feed feed;
         feed.settlement_price = initial_price;
         feed.maintenance_collateral_ratio = 1750; // MCR of 1.75x
         feed.maximum_short_squeeze_ratio = 1500; // MSSR of 1.50x

         //////
         // Check prices and ratios when MSSR = 150% and MCFR is not set
         //////
         mcfr = {};

         // Expected paid price = price / MSSR
         // = (1 satoshi SMARTBIT2 / 20 satoshi Core) / (1500 / 1000)
         // = (1 satoshi SMARTBIT2 / 20 satoshi Core) / (3 / 2)
         // = (1 satoshi SMARTBIT2 / 30 satoshi Core)
         expected_paid_price = price(smartbit2.amount(1), core.amount(30));
         BOOST_CHECK(expected_paid_price == feed.max_short_squeeze_price());

         // Expected offer price = price / (MSSR-MCFR)
         // = (1 satoshi SMARTBIT2 / 20 satoshi Core) / ([1500 - 0] / 1000)
         // = (1 satoshi SMARTBIT2 / 20 satoshi Core) / (3 / 2)
         // = (1 satoshi SMARTBIT2 / 30 satoshi Core)
         expected_paid_price = price(smartbit2.amount(1), core.amount(30));
         BOOST_CHECK(expected_paid_price == feed.margin_call_order_price(mcfr));

         // Expected margin call pays ratio = (MSSR-MCFR) / MSSR
         // = (1500 - 0) / 1500
         // = 1
         expected_margin_call_pays_ratio = ratio_type(1, 1);
         BOOST_CHECK(expected_margin_call_pays_ratio == feed.margin_call_pays_ratio(mcfr));


         //////
         // Check prices and ratios when MSSR = 150% and MCFR = 0
         //////
         mcfr = 0;

         // Expected paid price = price / MSSR
         // = (1 satoshi SMARTBIT2 / 20 satoshi Core) / (1500 / 1000)
         // = (1 satoshi SMARTBIT2 / 20 satoshi Core) / (3 / 2)
         // = (1 satoshi SMARTBIT2 / 30 satoshi Core)
         expected_paid_price = price(smartbit2.amount(1), core.amount(30));
         BOOST_CHECK(expected_paid_price == feed.max_short_squeeze_price());

         // Expected offer price = price / (MSSR-MCFR)
         // = (1 satoshi SMARTBIT2 / 20 satoshi Core) / ([1500 - 0] / 1000)
         // = (1 satoshi SMARTBIT2 / 20 satoshi Core) / (3 / 2)
         // = (1 satoshi SMARTBIT2 / 30 satoshi Core)
         expected_paid_price = price(smartbit2.amount(1), core.amount(30));
         BOOST_CHECK(expected_paid_price == feed.margin_call_order_price(mcfr));

         // Expected margin call pays ratio = (MSSR-MCFR) / MSSR
         // = (1500 - 0) / 1500
         // = 1
         expected_margin_call_pays_ratio = ratio_type(1, 1);
         BOOST_CHECK(expected_margin_call_pays_ratio == feed.margin_call_pays_ratio(mcfr));


         //////
         // Check prices and ratios when MSSR = 150% and MCFR = 5%
         //////
         mcfr = 50; // 5% expressed in terms of GRAPHENE_COLLATERAL_RATIO_DENOM

         // Expected paid price = price / MSSR
         // = (1 satoshi SMARTBIT2 / 20 satoshi Core) / (1500 / 1000)
         // = (1 satoshi SMARTBIT2 / 20 satoshi Core) / (3 / 2)
         // = (1 satoshi SMARTBIT2 / 30 satoshi Core)
         expected_paid_price = price(smartbit2.amount(1), core.amount(30));
         BOOST_CHECK(expected_paid_price == feed.max_short_squeeze_price());

         // Expected offer price = price / (MSSR-MCFR)
         // = (1 satoshi SMARTBIT2 / 20 satoshi Core) / ([1500 - 50] / 1000)
         // = (1 satoshi SMARTBIT2 / 20 satoshi Core) / (1450 / 1000)
         // = (1 satoshi SMARTBIT2 / 20 satoshi Core) / (145 / 100)
         // = (1 satoshi SMARTBIT2 / 2 satoshi Core) / (145 / 10)
         // = (10 satoshi SMARTBIT2 / 290 satoshi Core)
         // = (1 satoshi SMARTBIT2 / 29 satoshi Core)
         expected_paid_price = price(smartbit2.amount(1), core.amount(29));
         BOOST_CHECK(expected_paid_price == feed.margin_call_order_price(mcfr));

         // Expected margin call pays ratio = (MSSR-MCFR) / MSSR
         // = (1500 - 50) / 1500
         // = 1450 / 1500 = 145 / 150 = 29 / 30
         expected_margin_call_pays_ratio = ratio_type(29, 30);
         BOOST_CHECK(expected_margin_call_pays_ratio == feed.margin_call_pays_ratio(mcfr));


         //////
         // Check prices and ratios when MSSR = 150% and MCFR = 30%
         //////
         mcfr = 300; // 30% expressed in terms of GRAPHENE_COLLATERAL_RATIO_DENOM

         // Expected paid price = price / MSSR
         // = (1 satoshi SMARTBIT2 / 20 satoshi Core) / (1500 / 1000)
         // = (1 satoshi SMARTBIT2 / 20 satoshi Core) / (3 / 2)
         // = (1 satoshi SMARTBIT2 / 30 satoshi Core)
         expected_paid_price = price(smartbit2.amount(1), core.amount(30));
         BOOST_CHECK(expected_paid_price == feed.max_short_squeeze_price());

         // Expected offer price = price / (MSSR-MCFR)
         // = (1 satoshi SMARTBIT2 / 20 satoshi Core) / ([1500 - 300] / 1000)
         // = (1 satoshi SMARTBIT2 / 20 satoshi Core) / (1200 / 1000)
         // = (1 satoshi SMARTBIT2 / 20 satoshi Core) / (6 / 5)
         // = (5 satoshi SMARTBIT2 / 120 satoshi Core)
         // = (1 satoshi SMARTBIT2 / 24 satoshi Core)
         expected_paid_price = price(smartbit2.amount(1), core.amount(24));
         BOOST_CHECK(expected_paid_price == feed.margin_call_order_price(mcfr));

         // Expected margin call pays ratio = (MSSR-MCFR) / MSSR
         // = (1500 - 300) / 1500
         // = 1200 / 1500 = 4 / 5
         expected_margin_call_pays_ratio = ratio_type(4, 5);
         BOOST_CHECK(expected_margin_call_pays_ratio == feed.margin_call_pays_ratio(mcfr));


         //////
         // Check prices and ratios when MSSR = 150% and MCFR = 60%
         //////
         mcfr = 600; // 60% expressed in terms of GRAPHENE_COLLATERAL_RATIO_DENOM

         // Expected paid price = price / MSSR
         // = (1 satoshi SMARTBIT2 / 20 satoshi Core) / (1500 / 1000)
         // = (1 satoshi SMARTBIT2 / 20 satoshi Core) / (3 / 2)
         // = (1 satoshi SMARTBIT2 / 30 satoshi Core)
         expected_paid_price = price(smartbit2.amount(1), core.amount(30));
         BOOST_CHECK(expected_paid_price == feed.max_short_squeeze_price());

         // Expected offer price = price / (MSSR-MCFR)
         // but (MSSR-MCFR) has a floor 1
         // Therefore = price / 1 = price
         // = (1 satoshi SMARTBIT2 / 20 satoshi Core)
         expected_paid_price = price(smartbit2.amount(1), core.amount(20));
         BOOST_CHECK(expected_paid_price == feed.margin_call_order_price(mcfr));

         // Expected margin call pays ratio = (MSSR-MCFR) / MSSR
         // but (MSSR-MCFR) has a floor 1
         // Therefore = 1 / MSSR
         // = 1000 / 1500 = 2 / 3
         expected_margin_call_pays_ratio = ratio_type(2, 3);
         BOOST_CHECK(expected_margin_call_pays_ratio == feed.margin_call_pays_ratio(mcfr));

      }
      FC_LOG_AND_RETHROW()
   }


   /**
    * Test a simple scenario of a Complete Fill of a Call Order as a Maker after HF
    *
    * 0. Advance to HF
    * 1. Initialize actors and a smart asset called SMARTBIT
    * 2. Publish feed
    * 3. (Order 1: Call order) Bob borrows a **"small"** amount of SMARTBIT into existence.
    *     Bob retains the asset in his own balances, or transfers it, or sells it is not critical
    *     because his debt position is what will be tracked.
    * 4. The feed price is updated to indicate that the collateral drops enough to trigger a margin call
    *    **but not enough** to trigger a global settlement.
    *    Bob's activated margin call cannot be matched against any existing limit order's price.
    * 5. (Order 2: Limit order) Alice places a **"large"** limit order to sell SMARTBIT at a price
    *    that will overlap with Bob's "activated" call order / margin call.
    *    **Bob should be charged as a maker, and Alice as a taker.**
    *    Alice's limit order should be (partially or completely) filled, but Bob's order should be completely filled,
    *    and the debt position should be closed.
    */
   BOOST_AUTO_TEST_CASE(complete_fill_of_call_order_as_maker) {
      try {
         //////
         // 0. Advance to activate hardfork
         //////
         BOOST_TEST_MESSAGE("Advancing past Hardfork BSIP74");
         generate_blocks(HARDFORK_CORE_BSIP74_TIME);
         generate_block();
         set_expiration(db, trx);


         //////
         // 1. Initialize actors and a smart asset called SMARTBIT
         //////
         // Initialize for the current time
         trx.clear();
         set_expiration(db, trx);

         // Initialize actors
         ACTORS((alice)(bob));
         ACTORS((smartissuer)(feedproducer));

         // Initialize tokens
         // CORE asset exists by default
         const asset_object &core = asset_id_type()(db);
         const asset_id_type core_id = core.id;
         const int64_t CORE_UNIT = asset::scaled_precision(core.precision).value; // 100000 satoshi CORE in 1 CORE

         // Create the SMARTBIT asset
         const int16_t SMARTBIT_UNIT = 10000; // 10000 satoshi SMARTBIT in 1 SMARTBIT
         const uint16_t smartbit_market_fee_percent = 2 * GRAPHENE_1_PERCENT;
         const uint16_t smartbit_margin_call_fee_ratio = 50; // 5% expressed in terms of GRAPHENE_COLLATERAL_RATIO_DENOM
         // Define the margin call fee ratio
         create_bitasset("SMARTBIT", smartissuer_id, smartbit_market_fee_percent, charge_market_fee, 4, core_id,
                         GRAPHENE_MAX_SHARE_SUPPLY, {}, smartbit_margin_call_fee_ratio);
         // Obtain asset object after a block is generated to obtain the final object that is commited to the database
         generate_block();
         const asset_object smartbit = get_asset("SMARTBIT");
         const asset_id_type smartbit_id = smartbit.id;
         update_feed_producers(smartbit, {feedproducer_id});

         // Initialize token balance of actors
         // Alice should start with 5,000,000 CORE
         const asset alice_initial_core = asset(5000000 * CORE_UNIT);
         transfer(committee_account, alice_id, alice_initial_core);
         BOOST_REQUIRE_EQUAL(get_balance(alice_id, core_id), alice_initial_core.amount.value);

         // Bob should start with enough CORE to back 200 SMARTBIT subject to
         // (a) to an initial price feed of 1 satoshi SMARTBIT for 20 satoshi CORE
         // = 0.0001 SMARTBIT for 0.00020 CORE = 1 SMARTBIT for 2 CORE
         // (b) an initial collateral ratio of 2x
         const price initial_feed_price =
                 smartbit.amount(1) / core.amount(20); // 1 satoshi SMARTBIT for 20 satoshi CORE
         const asset bob_initial_smart = smartbit.amount(200 * SMARTBIT_UNIT); // 2,000,000 satoshi SMARTBIT
         const asset bob_initial_core = core.amount(
                 2 * (bob_initial_smart * initial_feed_price).amount); // 80,000,000 satoshi CORE
         transfer(committee_account, bob_id, bob_initial_core);
         BOOST_REQUIRE_EQUAL(get_balance(bob, core), 80000000);


         //////
         // 2. Publish feed
         //////
         price_feed current_feed;
         current_feed.settlement_price = initial_feed_price;
         current_feed.maintenance_collateral_ratio = 1750; // MCR of 1.75x
         current_feed.maximum_short_squeeze_ratio = 1500; // MSSR of 1.50x
         publish_feed(smartbit, feedproducer_id(db), current_feed);
         FC_ASSERT(smartbit.bitasset_data(db).current_feed.settlement_price == current_feed.settlement_price);


         //////
         // 3. (Order 1: Call order) Bob borrows a **"small"** amount of SMARTBIT into existence.
         //    Bob retains the asset in his own balances, or transfers it, or sells it is not critical
         //    because his debt position is what will be tracked.
         //////
         call_order_id_type bob_call_id = (*borrow(bob, bob_initial_smart, bob_initial_core)).id;
         BOOST_REQUIRE_EQUAL(get_balance(bob, smartbit), 200 * SMARTBIT_UNIT);
         BOOST_CHECK(!smartbit.bitasset_data(db).has_settlement()); // No global settlement
         const price bob_initial_cr = bob_call_id(db).collateralization(); // Units of collateral / debt
         BOOST_CHECK_EQUAL(bob_initial_cr.base.amount.value, 80000000); // Collateral of 80,000,000 satoshi CORE
         BOOST_CHECK_EQUAL(bob_initial_cr.quote.amount.value, 2000000); // Debt of 2,000,000 satoshi SMARTBIT


         //////
         // 4. The feed price is updated to indicate that the collateral drops enough to trigger a margin call
         //    **but not enough** to trigger a global settlement.
         //    Bob's activated margin call cannot be matched against any existing limit order's price.
         //////
         // Adjust the price such that the initial CR of Bob's position (CR_0) drops to 1.7x = (17/10)x
         // Want new price = 1.7 / CR_0 = (17/10) / CR_0
         //
         // Collateral ratios are defined as collateral / debt
         // BitShares prices are conventionally defined as debt / collateral
         // The new price can be expressed with the available codebase as
         // = (17/10) * ~CR_0 = ~CR_0 * (17/10)
         const price intermediate_feed_price = ~bob_initial_cr * ratio_type(17, 10); // Units of debt / collateral
         // Reduces to (2000000 * 17) / (80000000 * 10) = (17) / (40 * 10) = 17 / 400
         BOOST_CHECK(intermediate_feed_price < initial_feed_price);
         BOOST_CHECK_EQUAL(intermediate_feed_price.base.amount.value, 17); // satoshi SMARTBIT
         BOOST_CHECK_EQUAL(intermediate_feed_price.quote.amount.value, 400); // satoshi CORE

         current_feed.settlement_price = intermediate_feed_price;
         publish_feed(smartbit, feedproducer_id(db), current_feed);

         BOOST_CHECK(smartbit.bitasset_data(db).current_feed.settlement_price == current_feed.settlement_price);
         BOOST_CHECK(!smartbit.bitasset_data(db).has_settlement()); // No global settlement

         // Check Bob's debt to the blockchain
         BOOST_CHECK_EQUAL(bob_call_id(db).debt.value, bob_initial_smart.amount.value);
         BOOST_CHECK_EQUAL(bob_call_id(db).collateral.value, bob_initial_core.amount.value);

         // Check Bob's balances
         BOOST_CHECK_EQUAL(get_balance(bob_id(db), smartbit_id(db)), bob_initial_smart.amount.value);
         BOOST_CHECK_EQUAL(get_balance(bob_id(db), core_id(db)), 0);



         //////
         // 5. (Order 2: Limit order) Alice places a **"large"** limit order to sell SMARTBIT at a price
         //    that will overlap with Bob's "activated" call order / margin call.
         //    **Bob should be charged as a maker, and Alice as a taker.**
         //    Alice's limit order should be (partially or completely) filled, but Bob's order should be completely filled,
         //    and the debt position should be closed.
         //////
         // Alice obtains her SMARTBIT from Bob
         transfer(bob_id, alice_id, bob_initial_smart);
         BOOST_CHECK_EQUAL(get_balance(bob_id(db), smartbit_id(db)), 0);
         BOOST_CHECK_EQUAL(get_balance(alice_id(db), smartbit_id(db)), bob_initial_smart.amount.value);

         // The margin call should be priced at settlement_price / (MSSR-MCFR)
         // where settlement_price is expressed as debt / collateral
         // Create a "large" sell order at a "high" price of settlement_price * 1.1 = settlement_price * (11/10)
         const price alice_order_price_implied = intermediate_feed_price * ratio_type(11, 10);


         const asset alice_debt_to_sell = smartbit.amount(get_balance(alice_id(db), smartbit_id(db)));
         // multiply_and_round_up() handles inverting the price so that the output is in correct collateral units
         const asset alice_collateral_to_buy = alice_debt_to_sell.multiply_and_round_up(alice_order_price_implied);
         limit_order_create_operation alice_sell_op = create_sell_operation(alice_id, alice_debt_to_sell,
                                                                            alice_collateral_to_buy);
         trx.clear();
         trx.operations.push_back(alice_sell_op);
         // asset alice_sell_fee = db.current_fee_schedule().set_fee(trx.operations.back());
         sign(trx, alice_private_key);
         processed_transaction ptx = PUSH_TX(db, trx); // No exception should be thrown
         limit_order_id_type alice_order_id = ptx.operation_results[0].get<object_id_type>();

         // Margin call should exchange all of the available debt (X) for X*(MSSR-MCFR)/settlement_price
         // The match price should be the settlement_price/(MSSR-MCFR) = settlement_price/(MSSR-MCFR)
         const uint16_t ratio_numerator = current_feed.maximum_short_squeeze_ratio - smartbit_margin_call_fee_ratio;
         BOOST_REQUIRE_EQUAL(ratio_numerator,
                             1450); // GRAPHENE_DEFAULT_MAX_SHORT_SQUEEZE_RATIO - smartbit_margin_call_fee_ratio
         const price expected_match_price = intermediate_feed_price * ratio_type(GRAPHENE_COLLATERAL_RATIO_DENOM,
                                                                           ratio_numerator);
         // Reduces to (17 satoshi SMARTBIT / 400 satoshi CORE) * (1000 / 1450)
         // = (17 satoshi SMARTBIT / 400 satoshi CORE) * (100 / 145)
         // = (17 satoshi SMARTBIT / 4 satoshi CORE) * (1 / 145)
         // = 17 satoshi SMARTBIT / 580 satoshi CORE
         BOOST_CHECK_EQUAL(expected_match_price.base.amount.value, 17); // satoshi SMARTBIT
         BOOST_CHECK_EQUAL(expected_match_price.quote.amount.value, 580); // satoshi CORE

         // Payment to limit order = X*(MSSR-MCFR)/settlement_price
         // = 2000000 satoshi SMARTBIT * (580 satoshi CORE / 17 satoshi SMARTBIT)
         // = 68235294.1176 satoshi CORE rounded up to 68235295 satoshi CORE = 682.35295 CORE
         const asset expected_payment_to_alice_core = core.amount(68235295);

         // Expected payment by call order: filled_debt * (MSSR / settlement_price) = filled_debt * (MSSR / settlement_price)
         //
         // (MSSR / settlement_price) = (1500 / 1000) / (17 satoshi SMARTBIT / 400 satoshi CORE)
         // = (15 / 10) / (17 satoshi SMARTBIT / 400 satoshi CORE)
         // = (15 / 1) / (17 satoshi SMARTBIT / 40 satoshi CORE)
         // = (15 * 40 satoshi CORE) / (17 satoshi SMARTBIT)
         // = (15 * 40 satoshi CORE) / (17 satoshi SMARTBIT)
         // = 600 satoshi CORE / 17 satoshi SMARTBIT
         //
         // Expected payment by call order = 2000000 satoshi SMARTBIT * (600 satoshi CORE / 17 satoshi SMARTBIT)
         // = 2000000 * 600 satoshi CORE / 17
         // = 70588235.2941 satoshi CORE rounding up to 70588236 satoshi CORE = 705.88236 CORE
         const asset expected_payment_from_bob_core = core.amount(70588236);

         // Expected fee = payment by call order - payment to limit order
         // fee = (70588236 - 68235295) satoshi CORE = 2352941 satoshi CORE = 23.52941 CORE
         const asset expected_margin_call_fee =
                 expected_payment_from_bob_core - expected_payment_to_alice_core; // core.amount(2352941);

         // Check Alice's balances
         BOOST_CHECK_EQUAL(get_balance(alice, smartbit), 0);
         BOOST_CHECK_EQUAL(get_balance(alice, core),
                           alice_initial_core.amount.value + expected_payment_to_alice_core.amount.value);

         // Check Alice's limit order is closed
         BOOST_CHECK(!db.find(alice_order_id));

         // Check Bob's debt position is closed
         BOOST_CHECK(!db.find(bob_call_id));

         // Check Bob's balances
         // Bob should have no debt asset
         BOOST_CHECK_EQUAL(get_balance(bob_id(db), smartbit_id(db)), 0);
         // Bob should have collected the balance of his collateral after the margin call
         BOOST_CHECK_EQUAL(get_balance(bob_id(db), core_id(db)),
                           bob_initial_core.amount.value - expected_payment_from_bob_core.amount.value);

         // Check the asset owner's accumulated asset fees
         BOOST_CHECK(smartbit.dynamic_asset_data_id(db).accumulated_fees == 0);
         BOOST_CHECK_EQUAL(smartbit.dynamic_asset_data_id(db).accumulated_collateral_fees.value,
                           expected_margin_call_fee.amount.value);

         // Check the fee of the fill operations for Alice and Bob
         generate_block(); // To trigger db_notify() and record pending operations into histories
         graphene::app::history_api hist_api(app);
         vector <operation_history_object> histories;
         const int fill_order_op_id = operation::tag<fill_order_operation>::value;

         // Check Alice's history
         histories = hist_api.get_account_history_operations(
                 "alice", fill_order_op_id, operation_history_id_type(), operation_history_id_type(), 100);
         // There should be one fill order operation
         BOOST_CHECK_EQUAL(histories.size(), 1);
         // Alice's fill order for her limit order should have zero fee
         fill_order_operation alice_fill_op = histories.front().op.get<fill_order_operation>();
         BOOST_CHECK(alice_fill_op.fee == asset(0));
         // Alice's fill order's fill price should equal the expected match price
         BOOST_CHECK(~alice_fill_op.fill_price == expected_match_price);

         // Check Bob's history
         histories = hist_api.get_account_history_operations(
                 "bob", fill_order_op_id, operation_history_id_type(), operation_history_id_type(), 100);
         // There should be one fill order operation
         BOOST_CHECK_EQUAL(histories.size(), 1);
         // Bob's fill order for his margin call should have a fee equal to the margin call fee
         fill_order_operation bob_fill_op = histories.front().op.get<fill_order_operation>();
         BOOST_CHECK(bob_fill_op.fee == expected_margin_call_fee);
         // Bob's fill order's fill price should equal the expected match price
         BOOST_CHECK(~bob_fill_op.fill_price == expected_match_price);

      } FC_LOG_AND_RETHROW()
   }


   /**
    * Test a scenario of a Complete Fill of a Call Order as a Maker after HF
    * that evaluates the price ranges of matchable limit orders.
    * Before BSIP74, taker limit orders must be priced >= settlement_price/MSSR
    * After BSIP74, taker limit orders must priced >= settlement_price/(MSSR-MCFR)
    *
    * 0. Advance to HF
    * 1. Initialize actors and a smart asset called SMARTBIT
    * 2. Publish feed
    * 3. (Order 1: Call order) Bob borrows a **"small"** amount of SMARTBIT into existence.
    *     Bob retains the asset in his own balances, or transfers it, or sells it is not critical
    *     because his debt position is what will be tracked.
    * 4. The feed price is updated to indicate that the collateral drops enough to trigger a margin call
    *    **but not enough** to trigger a global settlement.
    *    Bob's activated margin call cannot be matched against any existing limit order's price.
    * 5. (Order 2: Limit order) Charlie places a **"large"** limit order to sell SMARTBIT at a price
    *    that should NOT overlap with Bob's "activated" call order / margin call but would have before BSIP74.
    *    **Bob's margin call should not be affected.
    * 6. (Order 3: Limit order) Alice places a **"large"** limit order to sell SMARTBIT at a price
    *    that will overlap with Bob's "activated" call order / margin call.
    *    **Bob should be charged as a maker, and Alice as a taker.**
    *    Alice's limit order should be (partially or completely) filled, but Bob's order should be completely filled,
    *    and the debt position should be closed.
    *
    * Summary: The offer price of the taker limit order affects whether it matches the margin call order.
    *          The offer price of the taker limit order DOES NOT affect the filling.
    *          Filling of a maker margin call / taker limit order is based on the the call order's match price.
    */
   BOOST_AUTO_TEST_CASE(complete_fill_of_call_order_as_maker_2) {
      try {
         //////
         // 0. Advance to activate hardfork
         //////
         BOOST_TEST_MESSAGE("Advancing past Hardfork BSIP74");
         generate_blocks(HARDFORK_CORE_BSIP74_TIME);
         generate_block();
         set_expiration(db, trx);


         //////
         // 1. Initialize actors and a smart asset called SMARTBIT
         //////
         // Initialize for the current time
         trx.clear();
         set_expiration(db, trx);

         // Initialize actors
         ACTORS((alice)(bob)(charlie));
         ACTORS((smartissuer)(feedproducer));

         // Initialize tokens
         // CORE asset exists by default
         const asset_object &core = asset_id_type()(db);
         const asset_id_type core_id = core.id;
         const int64_t CORE_UNIT = asset::scaled_precision(core.precision).value; // 100000 satoshi CORE in 1 CORE

         // Create the SMARTBIT asset
         const int16_t SMARTBIT_UNIT = 10000; // 10000 satoshi SMARTBIT in 1 SMARTBIT
         const uint16_t smartbit_market_fee_percent = 2 * GRAPHENE_1_PERCENT;
         const uint16_t smartbit_margin_call_fee_ratio = 50; // 5% expressed in terms of GRAPHENE_COLLATERAL_RATIO_DENOM
         // Define the margin call fee ratio
         create_bitasset("SMARTBIT", smartissuer_id, smartbit_market_fee_percent, charge_market_fee, 4, core_id,
                         GRAPHENE_MAX_SHARE_SUPPLY, {}, smartbit_margin_call_fee_ratio);
         // Obtain asset object after a block is generated to obtain the final object that is commited to the database
         generate_block();
         const asset_object smartbit = get_asset("SMARTBIT");
         const asset_id_type smartbit_id = smartbit.id;
         update_feed_producers(smartbit, {feedproducer_id});

         // Initialize token balance of actors
         // Alice should start with 5,000,000 CORE
         const asset alice_initial_core = asset(5000000 * CORE_UNIT);
         transfer(committee_account, alice_id, alice_initial_core);
         BOOST_REQUIRE_EQUAL(get_balance(alice_id, core_id), alice_initial_core.amount.value);

         // Bob should start with enough CORE to back 200 SMARTBIT subject to
         // (a) to an initial price feed of 1 satoshi SMARTBIT for 20 satoshi CORE
         // = 0.0001 SMARTBIT for 0.00020 CORE = 1 SMARTBIT for 2 CORE
         // (b) an initial collateral ratio of 2x
         const price initial_feed_price =
                 smartbit.amount(1) / core.amount(20); // 1 satoshi SMARTBIT for 20 satoshi CORE
         const asset bob_initial_smart = smartbit.amount(200 * SMARTBIT_UNIT); // 2,000,000 satoshi SMARTBIT
         const asset bob_initial_core = core.amount(
                 2 * (bob_initial_smart * initial_feed_price).amount); // 80,000,000 satoshi CORE
         transfer(committee_account, bob_id, bob_initial_core);
         BOOST_REQUIRE_EQUAL(get_balance(bob, core), 80000000);

         // Charlie should start with enough CORE to back 200 SMARTBIT subject to
         // (a) to an initial price feed of 1 satoshi SMARTBIT for 20 satoshi CORE
         // = 0.0001 SMARTBIT for 0.00020 CORE = 1 SMARTBIT for 2 CORE
         // (b) an initial collateral ratio of 3x
         const asset charlie_initial_smart = smartbit.amount(200 * SMARTBIT_UNIT); // 2,000,000 satoshi SMARTBIT
         const asset charlie_initial_core = core.amount(
                 3 * (bob_initial_smart * initial_feed_price).amount); // 120,000,000 satoshi CORE
         transfer(committee_account, charlie_id, charlie_initial_core);
         BOOST_REQUIRE_EQUAL(get_balance(charlie, core), 120000000);


         //////
         // 2. Publish feed
         //////
         price_feed current_feed;
         current_feed.settlement_price = initial_feed_price;
         current_feed.maintenance_collateral_ratio = 1750; // MCR of 1.75x
         current_feed.maximum_short_squeeze_ratio = 1500; // MSSR of 1.50x
         publish_feed(smartbit, feedproducer_id(db), current_feed);
         FC_ASSERT(smartbit.bitasset_data(db).current_feed.settlement_price == current_feed.settlement_price);


         //////
         // 3. (Order 1: Call order) Bob borrows a **"small"** amount of SMARTBIT into existence.
         //    Bob retains the asset in his own balances, or transfers it, or sells it is not critical
         //    because his debt position is what will be tracked.
         //////
         call_order_id_type bob_call_id = (*borrow(bob, bob_initial_smart, bob_initial_core)).id;
         BOOST_REQUIRE_EQUAL(get_balance(bob, smartbit), 200 * SMARTBIT_UNIT);
         BOOST_CHECK(!smartbit.bitasset_data(db).has_settlement()); // No global settlement
         const price bob_initial_cr = bob_call_id(db).collateralization(); // Units of collateral / debt
         BOOST_CHECK_EQUAL(bob_initial_cr.base.amount.value, 80000000); // Collateral of 80,000,000 satoshi CORE
         BOOST_CHECK_EQUAL(bob_initial_cr.quote.amount.value, 2000000); // Debt of 2,000,000 satoshi SMARTBIT


         //////
         // 4. The feed price is updated to indicate that the collateral drops enough to trigger a margin call
         //    **but not enough** to trigger a global settlement.
         //    Bob's activated margin call cannot be matched against any existing limit order's price.
         //////
         // Adjust the price such that the initial CR of Bob's position (CR_0) drops to 1.7x = (17/10)x
         // Want new price = 1.7 / CR_0 = (17/10) / CR_0
         //
         // Collateral ratios are defined as collateral / debt
         // BitShares prices are conventionally defined as debt / collateral
         // The new price can be expressed with the available codebase as
         // = (17/10) * ~CR_0 = ~CR_0 * (17/10)
         const price intermediate_feed_price = ~bob_initial_cr * ratio_type(17, 10); // Units of debt / collateral
         // Reduces to (2000000 * 17) / (80000000 * 10) = (17) / (40 * 10) = 17 / 400
         BOOST_CHECK(intermediate_feed_price < initial_feed_price);
         BOOST_CHECK_EQUAL(intermediate_feed_price.base.amount.value, 17); // satoshi SMARTBIT
         BOOST_CHECK_EQUAL(intermediate_feed_price.quote.amount.value, 400); // satoshi CORE

         current_feed.settlement_price = intermediate_feed_price;
         publish_feed(smartbit, feedproducer_id(db), current_feed);

         BOOST_CHECK(smartbit.bitasset_data(db).current_feed.settlement_price == current_feed.settlement_price);
         BOOST_CHECK(!smartbit.bitasset_data(db).has_settlement()); // No global settlement

         // Check Bob's debt to the blockchain
         BOOST_CHECK_EQUAL(bob_call_id(db).debt.value, bob_initial_smart.amount.value);
         BOOST_CHECK_EQUAL(bob_call_id(db).collateral.value, bob_initial_core.amount.value);

         // Check Bob's balances
         BOOST_CHECK_EQUAL(get_balance(bob_id(db), smartbit_id(db)), bob_initial_smart.amount.value);
         BOOST_CHECK_EQUAL(get_balance(bob_id(db), core_id(db)), 0);


         //////
         // 5. (Order 2: Limit order) Charlie places a **"large"** limit order to sell SMARTBIT at a price
         //    that SHOULD NOT overlap with Bob's "activated" call order / margin call but would have before BSIP74.
         //    **Bob's margin call SHOULD NOT be affected.**
         //////
         // Charlie obtains his SMARTBIT by borrowing it from the blockchain
         call_order_id_type charlie_call_id = (*borrow(charlie, charlie_initial_smart, charlie_initial_core)).id;
         BOOST_REQUIRE_EQUAL(get_balance(charlie, smartbit), 200 * SMARTBIT_UNIT);
         BOOST_CHECK(!smartbit.bitasset_data(db).has_settlement()); // No global settlement
         const price charlie_initial_cr = charlie_call_id(db).collateralization(); // Units of collateral / debt
         BOOST_CHECK_EQUAL(charlie_initial_cr.base.amount.value, 120000000); // Collateral of 120,000,000 satoshi CORE
         BOOST_CHECK_EQUAL(charlie_initial_cr.quote.amount.value, 2000000); // Debt of 2,000,000 satoshi SMARTBIT

         // Check Charlie's liquid balance
         BOOST_CHECK_EQUAL(get_balance(charlie_id(db), core_id(db)), 0);
         BOOST_CHECK_EQUAL(get_balance(charlie_id(db), smartbit_id(db)), charlie_initial_smart.amount.value);

         // The margin call match price should be the settlement_price/(MSSR-MCFR) = settlement_price/(MSSR-MCFR)
         const uint16_t ratio_numerator = current_feed.maximum_short_squeeze_ratio - smartbit_margin_call_fee_ratio;
         BOOST_REQUIRE_EQUAL(ratio_numerator,
                             1450); // GRAPHENE_DEFAULT_MAX_SHORT_SQUEEZE_RATIO - smartbit_margin_call_fee_ratio
         const price expected_match_price = intermediate_feed_price * ratio_type(GRAPHENE_COLLATERAL_RATIO_DENOM,
                                                                                 ratio_numerator);
         // Reduces to (17 satoshi SMARTBIT / 400 satoshi CORE) * (1000 / 1450)
         // = (17 satoshi SMARTBIT / 400 satoshi CORE) * (100 / 145)
         // = (17 satoshi SMARTBIT / 4 satoshi CORE) * (1 / 145)
         // = 17 satoshi SMARTBIT / 580 satoshi CORE
         BOOST_CHECK_EQUAL(expected_match_price.base.amount.value, 17); // satoshi SMARTBIT
         BOOST_CHECK_EQUAL(expected_match_price.quote.amount.value, 580); // satoshi CORE

         // Charlie create a "large" sell order SLIGHTLY BELOW the match_price
         // This price should ensure that the order is NOT matched against Bob's margin call
         // The margin call should be priced at settlement_price / (MSSR-MCFR)
         // where settlement_price is expressed as debt / collateral
         const price charlie_order_price = price(smartbit.amount(17), core.amount(580));
         BOOST_CHECK(charlie_order_price == expected_match_price); // Exactly at the edge

         const asset charlie_debt_to_sell = smartbit.amount(get_balance(charlie_id(db), smartbit_id(db)));
         // multiply_and_round_up() handles inverting the price so that the output is in correct collateral units
         const asset charlie_collateral_to_buy = charlie_debt_to_sell.multiply_and_round_up(charlie_order_price);
         limit_order_create_operation charlie_sell_op = create_sell_operation(charlie_id, charlie_debt_to_sell,
                                                                            charlie_collateral_to_buy);
         // The limit order's price should be slightly below the expected match price
         // due to multiply_and_round_up() which increases the collateral
         // thereby decreasing the ratio of debt / collateral
         BOOST_CHECK(charlie_sell_op.get_price() < expected_match_price);

         trx.clear();
         trx.operations.push_back(charlie_sell_op);
         // asset charlie_sell_fee = db.current_fee_schedule().set_fee(trx.operations.back());
         sign(trx, charlie_private_key);
         processed_transaction ptx = PUSH_TX(db, trx); // No exception should be thrown
         limit_order_id_type charlie_order_id = ptx.operation_results[0].get<object_id_type>();

         // Check Charlies's limit order is still open
         BOOST_CHECK(db.find(charlie_order_id));

         // Check Charlies's limit order is NOT CHANGED
         const limit_order_object charlie_limit_order = charlie_order_id(db);
         BOOST_CHECK(charlie_limit_order.amount_for_sale() == charlie_debt_to_sell);
         BOOST_CHECK(charlie_limit_order.amount_to_receive() == charlie_collateral_to_buy);

         // Check Bob's debt position is still open
         BOOST_CHECK(db.find(bob_call_id));

         // Check Bob's debt to the blockchain is NOT CHANGED
         BOOST_CHECK_EQUAL(bob_call_id(db).debt.value, bob_initial_smart.amount.value);
         BOOST_CHECK_EQUAL(bob_call_id(db).collateral.value, bob_initial_core.amount.value);


         //////
         // 6. (Order 2: Limit order) Alice places a **"large"** limit order to sell SMARTBIT at a price
         //    that will overlap with Bob's "activated" call order / margin call.
         //    **Bob should be charged as a maker, and Alice as a taker.**
         //    Alice's limit order should be (partially or completely) filled,
         //    but Bob's order should be completely filled,
         //    and the debt position should be closed.
         //////
         // Alice obtains her SMARTBIT from Bob
         transfer(bob_id, alice_id, bob_initial_smart);
         BOOST_CHECK_EQUAL(get_balance(bob_id(db), smartbit_id(db)), 0);
         BOOST_CHECK_EQUAL(get_balance(alice_id(db), smartbit_id(db)), bob_initial_smart.amount.value);

         // The margin call should be priced at settlement_price / (MSSR-MCFR)
         // where settlement_price is expressed as debt / collateral
         // Create a "large" sell order at JUST above the expected match price
         const price alice_order_price_implied = intermediate_feed_price * ratio_type(11, 10);
         const price alice_order_price = price(smartbit.amount(17 + 1), core.amount(580)); // Barely matching
         // const price alice_order_price = price(smartbit.amount(17), core.amount(580 - 1)); // Barely matching
         BOOST_CHECK(alice_order_price > expected_match_price);

         const asset alice_debt_to_sell = smartbit.amount(get_balance(alice_id(db), smartbit_id(db)));
         // multiply_and_round_up() handles inverting the price so that the output is in correct collateral units
         const asset alice_collateral_to_buy = alice_debt_to_sell.multiply_and_round_up(alice_order_price_implied);
         limit_order_create_operation alice_sell_op = create_sell_operation(alice_id, alice_debt_to_sell,
                                                                            alice_collateral_to_buy);
         trx.clear();
         trx.operations.push_back(alice_sell_op);
         // asset alice_sell_fee = db.current_fee_schedule().set_fee(trx.operations.back());
         sign(trx, alice_private_key);
         ptx = PUSH_TX(db, trx); // No exception should be thrown
         limit_order_id_type alice_order_id = ptx.operation_results[0].get<object_id_type>();

         // Margin call should exchange all of the available debt (X) for X*(MSSR-MCFR)/settlement_price
         // Payment to limit order = X*(MSSR-MCFR)/settlement_price
         // = 2000000 satoshi SMARTBIT * (580 satoshi CORE / 17 satoshi SMARTBIT)
         // = 68235294.1176 satoshi CORE rounded up to 68235295 satoshi CORE = 682.35295 CORE
         const asset expected_payment_to_alice_core = core.amount(68235295);

         // Expected payment by call order: filled_debt * (MSSR / settlement_price) = filled_debt * (MSSR / settlement_price)
         //
         // (MSSR / settlement_price) = (1500 / 1000) / (17 satoshi SMARTBIT / 400 satoshi CORE)
         // = (15 / 10) / (17 satoshi SMARTBIT / 400 satoshi CORE)
         // = (15 / 1) / (17 satoshi SMARTBIT / 40 satoshi CORE)
         // = (15 * 40 satoshi CORE) / (17 satoshi SMARTBIT)
         // = (15 * 40 satoshi CORE) / (17 satoshi SMARTBIT)
         // = 600 satoshi CORE / 17 satoshi SMARTBIT
         //
         // Expected payment by call order = 2000000 satoshi SMARTBIT * (600 satoshi CORE / 17 satoshi SMARTBIT)
         // = 2000000 * 600 satoshi CORE / 17
         // = 70588235.2941 satoshi CORE rounding up to 70588236 satoshi CORE = 705.88236 CORE
         const asset expected_payment_from_bob_core = core.amount(70588236);

         // Expected fee = payment by call order - payment to limit order
         // fee = (70588236 - 68235295) satoshi CORE = 2352941 satoshi CORE = 23.52941 CORE
         const asset expected_margin_call_fee =
                 expected_payment_from_bob_core - expected_payment_to_alice_core; // core.amount(2352941);

         // Check Alice's balances
         BOOST_CHECK_EQUAL(get_balance(alice, smartbit), 0);
         BOOST_CHECK_EQUAL(get_balance(alice, core),
                           alice_initial_core.amount.value + expected_payment_to_alice_core.amount.value);

         // Check Alice's limit order is close
         BOOST_CHECK(!db.find(alice_order_id));

         // Check Bob's debt position is closed
         BOOST_CHECK(!db.find(bob_call_id));

         // Check Bob's balances
         // Bob should have no debt asset
         BOOST_CHECK_EQUAL(get_balance(bob_id(db), smartbit_id(db)), 0);
         // Bob should have collected the balance of his collateral after the margin call
         BOOST_CHECK_EQUAL(get_balance(bob_id(db), core_id(db)),
                           bob_initial_core.amount.value - expected_payment_from_bob_core.amount.value);

         // Check the asset owner's accumulated asset fees
         BOOST_CHECK(smartbit.dynamic_asset_data_id(db).accumulated_fees == 0);
         BOOST_CHECK_EQUAL(smartbit.dynamic_asset_data_id(db).accumulated_collateral_fees.value,
                           expected_margin_call_fee.amount.value);

         // Check the fee of the fill operations for Alice and Bob
         generate_block(); // To trigger db_notify() and record pending operations into histories
         graphene::app::history_api hist_api(app);
         vector <operation_history_object> histories;
         const int fill_order_op_id = operation::tag<fill_order_operation>::value;

         // Check Alice's history
         histories = hist_api.get_account_history_operations(
                 "alice", fill_order_op_id, operation_history_id_type(), operation_history_id_type(), 100);
         // There should be one fill order operation
         BOOST_CHECK_EQUAL(histories.size(), 1);
         // Alice's fill order for her limit order should have zero fee
         fill_order_operation alice_fill_op = histories.front().op.get<fill_order_operation>();
         BOOST_CHECK(alice_fill_op.fee == asset(0));
         // Alice's fill order's fill price should equal the expected match price
         BOOST_CHECK(~alice_fill_op.fill_price == expected_match_price);

         // Check Bob's history
         histories = hist_api.get_account_history_operations(
                 "bob", fill_order_op_id, operation_history_id_type(), operation_history_id_type(), 100);
         // There should be one fill order operation
         BOOST_CHECK_EQUAL(histories.size(), 1);
         // Bob's fill order for his margin call should have a fee equal to the margin call fee
         fill_order_operation bob_fill_op = histories.front().op.get<fill_order_operation>();
         BOOST_CHECK(bob_fill_op.fee == expected_margin_call_fee);
         // Bob's fill order's fill price should equal the expected match price
         BOOST_CHECK(~bob_fill_op.fill_price == expected_match_price);

      } FC_LOG_AND_RETHROW()
   }


   /**
    * Test a scenario of a partial Filling of a Call Order as a Maker after HF
    * where the partial filling is due to call order defining a target collateral ratio (TCR) (BSIP38)
    *
    * 0. Advance to HF
    * 1. Initialize actors and a smart asset called SMARTBIT
    * 2. Publish feed
    * 3. (Order 1: Call order) Bob borrows a **"small"** amount of SMARTBIT into existence.
    *     Bob retains the asset in his own balances, or transfers it, or sells it is not critical
    *     because his debt position is what will be tracked.
    * 4. The feed price is updated to indicate that the collateral drops enough to trigger a margin call
    *    **but not enough** to trigger a global settlement.
    *    Bob's activated margin call cannot be matched against any existing limit order's price.
    * 5. (Order 2: Limit order) Alice places a **"large"** limit order to sell SMARTBIT at a price
    *    that will overlap with Bob's "activated" call order / margin call.
    *    **Bob should be charged as a maker, and Alice as a taker.**
    *    Alice's limit order should be (partially or completely) filled,
    *    but Bob's order will also only be partially filled because the TCR will sell just enough collateral
    *    so that the remaining CR of the debt position >= TCR.
    *    Bob's debt position should remain open.
    */
   BOOST_AUTO_TEST_CASE(target_cr_partial_fill_of_call_order_as_maker) {
      try {
         //////
         // 0. Advance to activate hardfork
         //////
         BOOST_TEST_MESSAGE("Advancing past Hardfork BSIP74");
         generate_blocks(HARDFORK_CORE_BSIP74_TIME);
         generate_block();
         set_expiration(db, trx);


         //////
         // 1. Initialize actors and a smart asset called SMARTBIT
         //////
         // Initialize for the current time
         trx.clear();
         set_expiration(db, trx);

         // Initialize actors
         ACTORS((alice)(bob));
         ACTORS((smartissuer)(feedproducer));

         // Initialize tokens
         // CORE asset exists by default
         const asset_object &core = asset_id_type()(db);
         const asset_id_type core_id = core.id;
         const int64_t CORE_UNIT = asset::scaled_precision(core.precision).value; // 100000 satoshi CORE in 1 CORE

         // Create the SMARTBIT asset
         const int16_t SMARTBIT_UNIT = 10000; // 10000 satoshi SMARTBIT in 1 SMARTBIT
         const uint16_t smartbit_market_fee_percent = 2 * GRAPHENE_1_PERCENT;
         const uint16_t smartbit_margin_call_fee_ratio = 50; // 5% expressed in terms of GRAPHENE_COLLATERAL_RATIO_DENOM
         // Define the margin call fee ratio
         create_bitasset("SMARTBIT", smartissuer_id, smartbit_market_fee_percent, charge_market_fee, 4, core_id,
                         GRAPHENE_MAX_SHARE_SUPPLY, {}, smartbit_margin_call_fee_ratio);
         // Obtain asset object after a block is generated to obtain the final object that is commited to the database
         generate_block();
         const asset_object smartbit = get_asset("SMARTBIT");
         const asset_id_type smartbit_id = smartbit.id;
         update_feed_producers(smartbit, {feedproducer_id});

         // Initialize token balance of actors
         // Alice should start with 5,000,000 CORE
         const asset alice_initial_core = asset(5000000 * CORE_UNIT);
         transfer(committee_account, alice_id, alice_initial_core);
         BOOST_REQUIRE_EQUAL(get_balance(alice_id, core_id), alice_initial_core.amount.value);

         // Bob should start with enough CORE to back 200 SMARTBIT subject to
         // (a) to an initial price feed of 1 satoshi SMARTBIT for 20 satoshi CORE
         // = 0.0001 SMARTBIT for 0.00020 CORE = 1 SMARTBIT for 2 CORE
         // (b) an initial collateral ratio of 2x
         const price initial_feed_price =
                 smartbit.amount(1) / core.amount(20); // 1 satoshi SMARTBIT for 20 satoshi CORE
         const asset bob_initial_smart = smartbit.amount(200 * SMARTBIT_UNIT); // 2,000,000 satoshi SMARTBIT
         const asset bob_initial_core = core.amount(
                 2 * (bob_initial_smart * initial_feed_price).amount); // 80,000,000 satoshi CORE
         transfer(committee_account, bob_id, bob_initial_core);
         BOOST_REQUIRE_EQUAL(get_balance(bob, core), 80000000);


         //////
         // 2. Publish feed
         //////
         price_feed current_feed;
         current_feed.settlement_price = initial_feed_price;
         current_feed.maintenance_collateral_ratio = 1750; // MCR of 1.75x
         current_feed.maximum_short_squeeze_ratio = 1500; // MSSR of 1.50x
         publish_feed(smartbit, feedproducer_id(db), current_feed);
         FC_ASSERT(smartbit.bitasset_data(db).current_feed.settlement_price == current_feed.settlement_price);


         //////
         // 3. (Order 1: Call order) Bob borrows a **"small"** amount of SMARTBIT into existence.
         //    Bob retains the asset in his own balances, or transfers it, or sells it is not critical
         //    because his debt position is what will be tracked.
         //////
         const uint16_t tcr = 2200; // Bob's target collateral ratio (TCR) 220% expressed in terms of GRAPHENE_COLLATERAL_RATIO_DENOM
         call_order_id_type bob_call_id = (*borrow(bob, bob_initial_smart, bob_initial_core, tcr)).id;
         BOOST_REQUIRE_EQUAL(get_balance(bob, smartbit), 200 * SMARTBIT_UNIT);
         BOOST_CHECK(!smartbit.bitasset_data(db).has_settlement()); // No global settlement
         const price bob_initial_cr = bob_call_id(db).collateralization(); // Units of collateral / debt
         BOOST_CHECK_EQUAL(bob_initial_cr.base.amount.value, 80000000); // Collateral of 80,000,000 satoshi CORE
         BOOST_CHECK_EQUAL(bob_initial_cr.quote.amount.value, 2000000); // Debt of 2,000,000 satoshi SMARTBIT


         //////
         // 4. The feed price is updated to indicate that the collateral drops enough to trigger a margin call
         //    **but not enough** to trigger a global settlement.
         //    Bob's activated margin call cannot be matched against any existing limit order's price.
         //////
         // Adjust the price such that the initial CR of Bob's position (CR_0) drops to 1.7x = (17/10)x
         // Want new price = 1.7 / CR_0 = (17/10) / CR_0
         //
         // Collateral ratios are defined as collateral / debt
         // BitShares prices are conventionally defined as debt / collateral
         // The new price can be expressed with the available codebase as
         // = (17/10) * ~CR_0 = ~CR_0 * (17/10)
         const price intermediate_feed_price = ~bob_initial_cr * ratio_type(17, 10); // Units of debt / collateral
         // Reduces to (2000000 * 17) / (80000000 * 10) = (17) / (40 * 10) = 17 / 400
         BOOST_CHECK(intermediate_feed_price < initial_feed_price);
         BOOST_CHECK_EQUAL(intermediate_feed_price.base.amount.value, 17); // satoshi SMARTBIT
         BOOST_CHECK_EQUAL(intermediate_feed_price.quote.amount.value, 400); // satoshi CORE

         current_feed.settlement_price = intermediate_feed_price;
         publish_feed(smartbit, feedproducer_id(db), current_feed);

         BOOST_CHECK(smartbit.bitasset_data(db).current_feed.settlement_price == current_feed.settlement_price);
         BOOST_CHECK(!smartbit.bitasset_data(db).has_settlement()); // No global settlement

         // Check Bob's debt to the blockchain
         BOOST_CHECK_EQUAL(bob_call_id(db).debt.value, bob_initial_smart.amount.value);
         BOOST_CHECK_EQUAL(bob_call_id(db).collateral.value, bob_initial_core.amount.value);

         // Check Bob's balances
         BOOST_CHECK_EQUAL(get_balance(bob_id(db), smartbit_id(db)), bob_initial_smart.amount.value);
         BOOST_CHECK_EQUAL(get_balance(bob_id(db), core_id(db)), 0);



         //////
         // 5. (Order 2: Limit order) Alice places a **"large"** limit order to sell SMARTBIT at a price
         //    that will overlap with Bob's "activated" call order / margin call.
         //    **Bob should be charged as a maker, and Alice as a taker.**
         //    Alice's limit order should be (partially or completely) filled,
         //    but Bob's order will also only be partially filled because the TCR will sell just enough collateral
         //    so that the remaining CR of the debt position >= TCR.
         //    Bob's debt position should remain open.
         //////
         // Alice obtains her SMARTBIT from Bob
         transfer(bob_id, alice_id, bob_initial_smart);
         BOOST_CHECK_EQUAL(get_balance(bob_id(db), smartbit_id(db)), 0);
         BOOST_CHECK_EQUAL(get_balance(alice_id(db), smartbit_id(db)), bob_initial_smart.amount.value);

         // The margin call should be priced at settlement_price / (MSSR-MCFR)
         // where settlement_price is expressed as debt / collateral
         // Create a "large" sell order at a "high" price of settlement_price * 1.1 = settlement_price * (11/10)
         const price alice_order_price_implied = intermediate_feed_price * ratio_type(11, 10);


         const asset alice_debt_to_sell = smartbit.amount(get_balance(alice_id(db), smartbit_id(db)));
         // multiply_and_round_up() handles inverting the price so that the output is in correct collateral units
         const asset alice_collateral_to_buy = alice_debt_to_sell.multiply_and_round_up(alice_order_price_implied);
         limit_order_create_operation alice_sell_op = create_sell_operation(alice_id, alice_debt_to_sell,
                                                                            alice_collateral_to_buy);
         trx.clear();
         trx.operations.push_back(alice_sell_op);
         // asset alice_sell_fee = db.current_fee_schedule().set_fee(trx.operations.back());
         sign(trx, alice_private_key);
         processed_transaction ptx = PUSH_TX(db, trx); // No exception should be thrown
         limit_order_id_type alice_order_id = ptx.operation_results[0].get<object_id_type>();

         // The match price **as maker** should be the settlement_price/(MSSR-MCFR) = settlement_price/(MSSR-MCFR)
         const uint16_t ratio_numerator = current_feed.maximum_short_squeeze_ratio - smartbit_margin_call_fee_ratio;
         BOOST_REQUIRE_EQUAL(ratio_numerator,
                             1450); // GRAPHENE_DEFAULT_MAX_SHORT_SQUEEZE_RATIO - smartbit_margin_call_fee_ratio
         const price expected_match_price = intermediate_feed_price * ratio_type(GRAPHENE_COLLATERAL_RATIO_DENOM,
                                                                                 ratio_numerator);
         // Reduces to (17 satoshi SMARTBIT / 400 satoshi CORE) * (1000 / 1450)
         // = (17 satoshi SMARTBIT / 400 satoshi CORE) * (100 / 145)
         // = (17 satoshi SMARTBIT / 4 satoshi CORE) * (1 / 145)
         // = 17 satoshi SMARTBIT / 580 satoshi CORE
         BOOST_CHECK_EQUAL(expected_match_price.base.amount.value, 17); // satoshi SMARTBIT
         BOOST_CHECK_EQUAL(expected_match_price.quote.amount.value, 580); // satoshi CORE

         // When a TCR is set for a call order, the ideal is to not sell all of the collateral
         // but only enough collateral so that the remaining collateral and the remaining debt in the debt position
         // has a resulting CR >= TCR.  The specifications are described in BSIP38.
         //
         // Per BSIP38, the expected amount to sell from the call order is
         // max_amount_to_sell = (debt * target_CR - collateral * settlement_price)
         //                    / (target_CR * match_price - settlement_price)
         //
         // HOWEVER, the match price that is used in this calculation
         // NEEDS TO BE ADJUSTED to account for the extra MCFR > 0 that will be paid by the call order.
         //
         // Rather than using a match price of settlement_price/(MSSR-MCFR) **AS A MAKER**,
         // the call_pays_price of settlement_price/(MSSR-MCFR+MCFR) = settlement_price/MSSR should be used
         // when determining the amount of collateral and debt that will removed from the debt position.
         // The limit order will still be compensated based on the normal match price of settlement_price/(MSSR-MCFR)
         // but the calculation from BSIP38 should use the call_pays_price which reflects that the call order
         // will actually pay more collateral
         // (it can be considered as a higher effective price when denominated in collateral / debt,
         // or equivalently a lower effective price when denominated in debt / collateral).

         // Therefore, the call_pays_price, WHEN THE CALL ORDER IS MAKER,
         //                   = feed_price / MSSR reduces to
         // feed_price / MSSR = (17 satoshi SMARTBIT / 400 satoshi CORE) * (1000 / 1500)
         //                   =  (17 satoshi SMARTBIT / 400 satoshi CORE) * (10 / 15)
         //                   =  17 satoshi SMARTBIT / 600 satoshi CORE

         // Returning to the formula for the TCR amount to sell from the call order
         // max_amount_to_sell = (debt * target_CR - collateral * feed_price) / (target_CR * call_pays_price - feed_price)
         //
         // = (2000000 satoshi SMARTBIT * [2200 / 1000] - 80000000 satoshi CORE * [17 satoshi SMARTBIT / 400 satoshi CORE])
         //   / ([2200 / 1000] *  [17 satoshi SMARTBIT / 600 satoshi CORE] - [17 satoshi SMARTBIT / 400 satoshi CORE])
         //
         // = (2000000 satoshi SMARTBIT * [22 / 10] - 80000000 satoshi SMARTBIT * [17 / 400])
         //   / ([22 / 10] *  [17 satoshi SMARTBIT / 600 satoshi CORE] - [17 satoshi SMARTBIT / 400 satoshi CORE])
         //
         // = (200000 satoshi SMARTBIT * [22] - 200000 satoshi SMARTBIT * [17])
         //   / ([22 / 10] *  [17 satoshi SMARTBIT / 600 satoshi CORE] - [17 satoshi SMARTBIT / 400 satoshi CORE])
         //
         // = (200000 satoshi SMARTBIT * [22 - 17])
         //   / ([22 / 10] *  [17 satoshi SMARTBIT / 600 satoshi CORE] - [17 satoshi SMARTBIT / 400 satoshi CORE])
         //
         // = (200000 satoshi CORE * [5]) / ([22 / 10] *  [17 / 600] - [17 / 400])
         //
         // = (1000000 satoshi CORE) / ([22 / 10] *  [17 / 600] - [17 / 400])
         //
         // ~= (1000000 satoshi CORE) / (0.0198333333333) ~= 50420168.0757 satoshi CORE
         //
         // ~= rounded up to 50420169 satoshi CORE = 504.20169 CORE
         // const asset expected_max_amount_to_sell = core.amount(50420169);
         // match() is calculating 50420189 CORE

         // Per BSIP38, the expected amount to cover from the call order
         //
         // max_debt_to_cover = max_amount_to_sell * match_price
         //
         // which is adjusted to
         //
         // max_debt_to_cover = max_amount_to_sell * call_pays_price

         // Therefore the
         //
         // = (1000000 satoshi CORE) / ([22 / 10] *  [17 / 600] - [17 / 400]) * (17 satoshi SMARTBIT / 600 satoshi CORE)
         //
         // ~= 50420168.0757 satoshi CORE * (17 satoshi SMARTBIT / 600 satoshi CORE)
         //
         // ~= 1428571.42881 satoshi SMARTBIT rounded down to 1428571 satoshi SMARTBIT = 142.8571 SMARTBIT
         // ~= 1428571.42881 satoshi SMARTBIT rounded up to 1428572 satoshi SMARTBIT = 142.8572 SMARTBIT
         const asset expected_max_debt_to_cover = smartbit.amount(1428572);

         // WHEN THE CALL ORDER IS MAKER, the match_price is settlement_price/(MSSR-MCFR)
         // Payment to limit order = X/match_price = X*(MSSR-MCFR)/settlement_price
         // = 1428572 satoshi SMARTBIT * (580 satoshi CORE / 17 satoshi SMARTBIT)
         // = 48739515.2941 satoshi CORE rounded up to 48739516 satoshi CORE = 487.39516 CORE
         // Margin call should exchange the filled debt (X) for X*(MSSR-MCFR)/settlement_price
         const asset expected_payment_to_alice_core = core.amount(48739516);

         // Caluclate the expected payment in collateral by the call order
         // to fulfill the (complete or partial) filling of the margin call.
         //
         // The expected payment is not necessarily equal to BSIP38's max_amount_to_sell.
         // It should be calculated base on the amount paid to the limit order (X), the settlement price,
         // and the MSSR.
         //
         // Expected payment by call order = X*MSSR/settlement_price
         // Expected payment by call order = 1428572 satoshi SMARTBIT * (600 satoshi CORE / 17 satoshi SMARTBIT)
         // = 1428572 * 600 satoshi CORE / 17
         // = 50420188.2353 satoshi CORE rounding up to 50420189 satoshi CORE = 504.20189 CORE
         const asset expected_payment_from_bob_core = core.amount(50420189);

         // The call order MUST ALSO pay the margin call fee
         // Expected fee = payment by call order - payment to limit order
         const asset expected_margin_call_fee = expected_payment_from_bob_core - expected_payment_to_alice_core;

         // Check Alice's balances
         BOOST_CHECK_EQUAL(get_balance(alice, smartbit), 0);
         BOOST_CHECK_EQUAL(get_balance(alice, core),
                           alice_initial_core.amount.value + expected_payment_to_alice_core.amount.value);

         // Alice's limit order should be open because of its partial filling
         BOOST_CHECK(db.find(alice_order_id));

         // Check Alice's limit order
         // The amount of smart asset available for sale should be reduced by the amount paid to Bob's margin call
         limit_order_object alice_limit_order = alice_order_id(db);
         asset expected_alice_remaining_smart_for_sale = alice_debt_to_sell - expected_max_debt_to_cover;
         BOOST_CHECK_EQUAL(alice_limit_order.amount_for_sale().amount.value,
                           expected_alice_remaining_smart_for_sale.amount.value);
         // Alice's limit order's price should be unchanged by the margin call
         BOOST_CHECK(alice_limit_order.sell_price == alice_sell_op.get_price());

         // Bob's debt position should be open because of its partial filling
         BOOST_CHECK(db.find(bob_call_id));

         // Check Bob's debt position
         BOOST_CHECK_EQUAL(bob_call_id(db).debt.value,
                           bob_initial_smart.amount.value - expected_max_debt_to_cover.amount.value);
         BOOST_CHECK_EQUAL(bob_call_id(db).collateral.value,
                           bob_initial_core.amount.value - expected_payment_to_alice_core.amount.value -
                           expected_margin_call_fee.amount.value);

         // Check Bob's balances
         // Bob should have no debt asset
         BOOST_CHECK_EQUAL(get_balance(bob_id(db), smartbit_id(db)), 0);
         // Bob should NOT have collected the balance of his collateral after the margin call
         // because the debt position is still open
         BOOST_CHECK_EQUAL(get_balance(bob_id(db), core_id(db)), 0);

         // Check the asset owner's accumulated asset fees
         BOOST_CHECK(smartbit.dynamic_asset_data_id(db).accumulated_fees == 0);
         BOOST_CHECK_EQUAL(smartbit.dynamic_asset_data_id(db).accumulated_collateral_fees.value,
                           expected_margin_call_fee.amount.value);

         // Check the fee of the fill operations for Alice and Bob
         generate_block(); // To trigger db_notify() and record pending operations into histories
         graphene::app::history_api hist_api(app);
         vector <operation_history_object> histories;
         const int fill_order_op_id = operation::tag<fill_order_operation>::value;

         // Check Alice's history
         histories = hist_api.get_account_history_operations(
                 "alice", fill_order_op_id, operation_history_id_type(), operation_history_id_type(), 100);
         // There should be one fill order operation
         BOOST_CHECK_EQUAL(histories.size(), 1);
         // Alice's fill order for her limit order should have zero fee
         fill_order_operation alice_fill_op = histories.front().op.get<fill_order_operation>();
         BOOST_CHECK(alice_fill_op.fee == asset(0));
         // Alice's fill order's fill price should equal the expected match price
         BOOST_CHECK(~alice_fill_op.fill_price == expected_match_price);

         // Check Bob's history
         histories = hist_api.get_account_history_operations(
                 "bob", fill_order_op_id, operation_history_id_type(), operation_history_id_type(), 100);
         // There should be one fill order operation
         BOOST_CHECK_EQUAL(histories.size(), 1);
         // Bob's fill order for his margin call should have a fee equal to the margin call fee
         fill_order_operation bob_fill_op = histories.front().op.get<fill_order_operation>();
         BOOST_CHECK(bob_fill_op.fee == expected_margin_call_fee);
         // Bob's fill order's fill price should equal the expected match price
         BOOST_CHECK(~bob_fill_op.fill_price == expected_match_price);

      } FC_LOG_AND_RETHROW()
   }


   /**
    * Test a simple scenario of a Complete Fill of a Call Order as a Taker after HF
    *
    * 0. Advance to HF
    * 1. Initialize actors and a smart asset called SMARTBIT
    * 2. Publish feed
    * 3. (Order 1: Limit order) Alice places a **"large"** limit order to sell SMARTBIT
    * 4. (Order 2: Call order) Bob borrows a **"small"** amount of SMARTBIT into existence.
    *     Bob retains the asset in his own balances, or transfers it, or sells it is not critical
    *     because his debt position is what will be tracked.
    * 5. The feed price indicates that the collateral drops enough to trigger a margin call
    *    **and** enough to be matched against Alice's limit order.
    *    (Global settlement is not at risk because Bob's small order should be matched
    *    and completely filled by Alice's large order).
    *    Alice's limit order should be matched against Bob's "activated" call order.
    *    **Alice should be charged as a maker, and Bob as a taker.**
    *    Alice's limit order should be partially filled,
    *    but Bob's order should be completely filled and removed from the book.
    */
   BOOST_AUTO_TEST_CASE(complete_fill_of_call_order_as_taker) {
      try {
         //////
         // 0. Advance to activate hardfork
         //////
         BOOST_TEST_MESSAGE("Advancing past Hardfork BSIP74");
         generate_blocks(HARDFORK_CORE_BSIP74_TIME);
         generate_block();
         set_expiration(db, trx);


         //////
         // 1. Initialize actors and a smart asset called SMARTBIT
         //////
         // Initialize for the current time
         trx.clear();
         set_expiration(db, trx);

         // Initialize actors
         ACTORS((alice)(bob)(charlie));
         ACTORS((smartissuer)(feedproducer));

         // Initialize tokens
         // CORE asset exists by default
         const asset_object &core = asset_id_type()(db);
         const asset_id_type core_id = core.id;
         const int64_t CORE_UNIT = asset::scaled_precision(core.precision).value; // 100000 satoshi CORE in 1 CORE

         // Create the SMARTBIT asset
         const int16_t SMARTBIT_UNIT = 10000; // 10000 satoshi SMARTBIT in 1 SMARTBIT
         const uint16_t smartbit_market_fee_percent = 2 * GRAPHENE_1_PERCENT;
         const uint16_t smartbit_margin_call_fee_ratio = 50; // 5% expressed in terms of GRAPHENE_COLLATERAL_RATIO_DENOM
         // Define the margin call fee ratio
         create_bitasset("SMARTBIT", smartissuer_id, smartbit_market_fee_percent, charge_market_fee, 4, core_id,
                         GRAPHENE_MAX_SHARE_SUPPLY, {}, smartbit_margin_call_fee_ratio);
         // Obtain asset object after a block is generated to obtain the final object that is commited to the database
         generate_block();
         const asset_object smartbit = get_asset("SMARTBIT");
         const asset_id_type smartbit_id = smartbit.id;
         update_feed_producers(smartbit, {feedproducer_id});

         // Initialize token balance of actors

         // Alice should start with enough CORE to back 5000 SMARTBIT subject to
         // (a) to an initial price feed of 1 satoshi SMARTBIT for 20 satoshi CORE
         // = 0.0001 SMARTBIT for 0.00020 CORE = 1 SMARTBIT for 2 CORE
         // (b) an initial collateral ratio of 4x
         const price initial_feed_price =
                 smartbit.amount(1) / core.amount(20); // 1 satoshi SMARTBIT for 20 satoshi CORE
         const asset alice_initial_smart = smartbit.amount(500 * SMARTBIT_UNIT); // 5,000,000 satoshi SMARTBIT
         const asset alice_initial_core = core.amount(
                 4 * (alice_initial_smart * initial_feed_price).amount); // 400,000,000 satoshi CORE
         transfer(committee_account, alice_id, alice_initial_core);
         BOOST_CHECK_EQUAL(get_balance(alice_id, core_id), alice_initial_core.amount.value);

         // Bob should start with enough CORE to back 200 SMARTBIT subject to
         // (a) to an initial price feed of 1 satoshi SMARTBIT for 20 satoshi CORE
         // = 0.0001 SMARTBIT for 0.00020 CORE = 1 SMARTBIT for 2 CORE
         // (b) an initial collateral ratio of 2x
         const asset bob_initial_smart = smartbit.amount(200 * SMARTBIT_UNIT); // 2,000,000 satoshi SMARTBIT
         const asset bob_initial_core = core.amount(
                 2 * (bob_initial_smart * initial_feed_price).amount); // 80,000,000 satoshi CORE
         transfer(committee_account, bob_id, bob_initial_core);
         BOOST_REQUIRE_EQUAL(get_balance(bob, core), 80000000);

         // In Step 5, the feed price will be adjusted such that
         // the initial CR of Bob's position (CR_0) drops to 1.7x = (17/10)x
         // Want new price = 1.7 / CR_0 = (17/10) / CR_0
         //
         // Collateral ratios are defined as collateral / debt
         // BitShares prices are conventionally defined as debt / collateral
         // The new price can be expressed with the available codebase as
         // = (17/10) * ~CR_0 = ~CR_0 * (17/10)
         const price expected_bob_initial_cr =
                 core.amount(2 * 20) / smartbit.amount(1); // 1 satoshi SMARTBIT for 40 satoshi CORE
         const price intermediate_feed_price =
                 ~expected_bob_initial_cr * ratio_type(17, 10); // Units of debt / collateral
         // Reduces to (2000000 * 17) / (80000000 * 10) = (17) / (40 * 10) = 17 satoshi SMARTBIT / 400 satoshi CORE
         BOOST_CHECK_EQUAL(intermediate_feed_price.base.amount.value, 17); // satoshi SMARTBIT
         BOOST_CHECK_EQUAL(intermediate_feed_price.quote.amount.value, 400); // satoshi CORE
         BOOST_CHECK(intermediate_feed_price < initial_feed_price);


         //////
         // 2. Publish feed
         //////
         price_feed current_feed;
         current_feed.settlement_price = initial_feed_price;
         current_feed.maintenance_collateral_ratio = 1750; // MCR of 1.75x
         current_feed.maximum_short_squeeze_ratio = 1500; // MSSR of 1.50x
         publish_feed(smartbit, feedproducer_id(db), current_feed);
         FC_ASSERT(smartbit.bitasset_data(db).current_feed.settlement_price == current_feed.settlement_price);


         //////
         // 3. (Order 1: Limit order) Alice places a **"large"** limit order to sell SMARTBIT.
         //////
         // Alice borrows SMARTBIT
         call_order_id_type alice_call_id = (*borrow(alice, alice_initial_smart, alice_initial_core)).id;
         BOOST_CHECK_EQUAL(get_balance(alice_id(db), smartbit_id(db)), 500 * SMARTBIT_UNIT);
         BOOST_CHECK_EQUAL(get_balance(alice_id, core_id), 0 * CORE_UNIT);
         BOOST_CHECK(!smartbit.bitasset_data(db).has_settlement()); // No global settlement

         // Alice offer to sell the SMARTBIT
         // Create a "large" sell order at a "high" price of settlement_price * 1.1 = settlement_price * (11/10)
         const price alice_order_price_implied = intermediate_feed_price * ratio_type(11, 10);
         // = (17 satoshi SMARTBIT / 400 satoshi CORE) * (11/10)
         // = 187 satoshi SMARTBIT / 4000 satoshi CORE
         BOOST_CHECK_EQUAL(alice_order_price_implied.base.amount.value, 187); // satoshi SMARTBIT
         BOOST_CHECK_EQUAL(alice_order_price_implied.quote.amount.value, 4000); // satoshi CORE

         const asset alice_debt_to_sell = smartbit.amount(get_balance(alice_id(db), smartbit_id(db)));
         // multiply_and_round_up() handles inverting the price so that the output is in correct collateral units
         const asset alice_collateral_to_buy = alice_debt_to_sell.multiply_and_round_up(alice_order_price_implied);
         limit_order_create_operation alice_sell_op = create_sell_operation(alice_id, alice_debt_to_sell,
                                                                            alice_collateral_to_buy);
         trx.clear();
         trx.operations.push_back(alice_sell_op);
         // asset alice_sell_fee = db.current_fee_schedule().set_fee(trx.operations.back());
         sign(trx, alice_private_key);
         processed_transaction ptx = PUSH_TX(db, trx); // No exception should be thrown
         limit_order_id_type alice_order_id = ptx.operation_results[0].get<object_id_type>();

         // Alice should have no balance
         BOOST_CHECK_EQUAL(get_balance(alice_id(db), smartbit_id(db)), 0 * SMARTBIT_UNIT);
         BOOST_CHECK_EQUAL(get_balance(alice_id, core_id), 0 * CORE_UNIT);


         //////
         // 4. (Order 2: Call order) Bob borrows a **"small"** amount of SMARTBIT into existence.
         //    Bob retains the asset in his own balances, or transfers it, or sells it is not critical
         //    because his debt position is what will be tracked.
         //////
         const asset bob_initial_debt_smart = bob_initial_smart;
         const asset bob_initial_debt_collateral = bob_initial_core;
         call_order_id_type bob_call_id = (*borrow(bob, bob_initial_debt_smart, bob_initial_debt_collateral)).id;

         // Bobs's balances should reflect that CORE was used to create SMARTBIT
         BOOST_CHECK_EQUAL(get_balance(bob_id, smartbit_id), 200 * SMARTBIT_UNIT);
         BOOST_CHECK_EQUAL(get_balance(bob_id, core_id), 0);
         BOOST_CHECK(!smartbit.bitasset_data(db).has_settlement()); // No global settlement
         const price bob_initial_cr = bob_call_id(db).collateralization(); // Units of collateral / debt
         BOOST_CHECK(bob_initial_cr == expected_bob_initial_cr);
         BOOST_CHECK_EQUAL(bob_initial_cr.base.amount.value, 80000000); // Collateral of 80,000,000 satoshi CORE
         BOOST_CHECK_EQUAL(bob_initial_cr.quote.amount.value, 2000000); // Debt of 2,000,000 satoshi SMARTBIT

         // Alice's balances should not have changed
         BOOST_REQUIRE_EQUAL(get_balance(alice_id(db), smartbit_id(db)), 0 * SMARTBIT_UNIT);
         BOOST_REQUIRE_EQUAL(get_balance(alice_id, core_id), 0 * CORE_UNIT);

         // Alice should not have been margin called
         price alice_initial_cr = alice_call_id(db).collateralization(); // Units of collateral / debt
         BOOST_CHECK_EQUAL(alice_initial_cr.base.amount.value, 400000000); // Collateral of 400,000,000 satoshi CORE
         BOOST_CHECK_EQUAL(alice_initial_cr.quote.amount.value, 5000000); // Debt of 5,000,000 satoshi SMARTBIT

         //////
         // Bob transfers his SMARTBIT to Charlie to clarify the accounting
         //////
         transfer(bob_id, charlie_id, bob_initial_smart);
         BOOST_CHECK_EQUAL(get_balance(bob_id, smartbit_id), 0 * SMARTBIT_UNIT);
         BOOST_CHECK_EQUAL(get_balance(bob_id, core_id), 0 * CORE_UNIT);
         BOOST_CHECK_EQUAL(get_balance(charlie_id, smartbit_id), 200 * SMARTBIT_UNIT);
         BOOST_CHECK_EQUAL(get_balance(charlie_id, core_id), 0 * CORE_UNIT);


         //////
         // 5. The feed price indicates that the collateral drops enough to trigger a margin call
         //    **and** enough to be matched against Alice's limit order.
         //    (Global settlement is not at risk because Bob's small order should be matched
         //    and completely filled by Alice's large order).
         //    Alice's limit order should be matched against Bob's "activated" call order.
         //    **Alice should be charged as a maker, and Bob as a taker.**
         //    Alice's limit order should be partially filled,
         //     but Bob's order should be completely filled and removed from the book.
         //////
         current_feed.settlement_price = intermediate_feed_price;
         publish_feed(smartbit, feedproducer_id(db), current_feed);
         // Confirm the updated feed
         BOOST_CHECK(smartbit.bitasset_data(db).current_feed.settlement_price == current_feed.settlement_price);
         // Confirm no global settlement
         BOOST_CHECK(!smartbit.bitasset_data(db).has_settlement());


         // The margin call of Bob's position should have closed the debt of bob_initial_smart
         // Bob's margin call should been matched against Alice's limit order
         // Bob's debt position should have paid collateral = bob_initial_smart / limit_order_price
         // 200 SMARTBIT / (187 satoshi SMARTBIT / 4000 satoshi CORE)
         // = 2,000,000 satoshi SMARTBIT / (187 satoshi SMARTBIT / 4000 satoshi CORE)
         // = 2,000,000 satoshi SMARTBIT * (4000 satoshi CORE / 187 satoshi SMARTBIT)
         // = 2,000,000 satoshi CORE / (4000 / 187)
         // = 42,780,748.6631 satoshi CORE rounded up to 42,780,749 satoshi CORE
         const asset expected_margin_call_from_bob_debt_core = core.amount(42780749);

         // Bob's margin call fee, which is paid in collateral, should be charged as a taker
         // The margin call fee debt = filled_debt * MCFR/(MSSR-MCFR) / limit_order_price
         // 200 SMARTBIT * (50 / (1500 - 50)) / (187 satoshi SMARTBIT / 4000 satoshi CORE)
         // = 2,000,000 satoshi SMARTBIT * (50 / 1450) / (187 satoshi SMARTBIT / 4000 satoshi CORE)
         // = 2,000,000 satoshi CORE * (1 / 29) * (4000 / 187)
         // = 1475198.22976 satoshi CORE rounded up to 1475199 satoshi CORE
         const asset expected_margin_call_fee_from_bob_debt_core = core.amount(1475199);

         // The balance of Bob's debt position
         const asset expected_return_from_bob_debt_core = bob_initial_core
                                                          - expected_margin_call_from_bob_debt_core
                                                          - expected_margin_call_fee_from_bob_debt_core;

         // Check Bob's debt position is closed
         BOOST_CHECK(!db.find(bob_call_id));

         // Check Bob's balances
         BOOST_CHECK_EQUAL(get_balance(bob_id(db), smartbit_id(db)), 0 * SMARTBIT_UNIT);
         BOOST_CHECK_EQUAL(get_balance(bob_id(db), core_id(db)), expected_return_from_bob_debt_core.amount.value);

         // Charlie's balances should not have changed
         BOOST_CHECK_EQUAL(get_balance(charlie_id, smartbit_id), 200 * SMARTBIT_UNIT);
         BOOST_CHECK_EQUAL(get_balance(charlie_id, core_id), 0 * CORE_UNIT);

         // Alice's balances should have changed because her limit order was partially filled by the margin call
         BOOST_CHECK_EQUAL(get_balance(alice_id(db), smartbit_id(db)), 0 * SMARTBIT_UNIT);
         BOOST_CHECK_EQUAL(get_balance(alice_id, core_id), expected_margin_call_from_bob_debt_core.amount.value);

         // Check Alice's debt
         // Alice's debt position should not be NOT closed
         BOOST_CHECK(db.find(alice_call_id));
         // Alice's debt should NOT have changed because its CR > MCR
         alice_initial_cr = alice_call_id(db).collateralization(); // Units of collateral / debt
         BOOST_CHECK_EQUAL(alice_initial_cr.base.amount.value, 400000000); // Collateral of 400,000,000 satoshi CORE
         BOOST_CHECK_EQUAL(alice_initial_cr.quote.amount.value, 5000000); // Debt of 5,000,000 satoshi SMARTBIT

         // Check Alice's limit order
         // The amount of smart asset available for sale should be reduced by the amount paid to Bob's margin call
         limit_order_object alice_limit_order = alice_order_id(db);
         asset expected_alice_remaining_smart_for_sale = alice_debt_to_sell - bob_initial_debt_smart;
         asset expected_alice_remaining_core_to_receive =
                 alice_collateral_to_buy - expected_margin_call_from_bob_debt_core;
         BOOST_CHECK(alice_limit_order.amount_for_sale() == expected_alice_remaining_smart_for_sale);
         BOOST_CHECK(alice_limit_order.amount_to_receive() == expected_alice_remaining_core_to_receive);

         // Check the asset owner's accumulated asset fees
         BOOST_CHECK_EQUAL(smartbit.dynamic_asset_data_id(db).accumulated_fees.value, 0);
         BOOST_CHECK_EQUAL(smartbit.dynamic_asset_data_id(db).accumulated_collateral_fees.value,
                           expected_margin_call_fee_from_bob_debt_core.amount.value);

         // Check the fee of the fill operations for Alice and Bob
         generate_block(); // To trigger db_notify() and record pending operations into histories
         graphene::app::history_api hist_api(app);
         vector <operation_history_object> histories;
         const int fill_order_op_id = operation::tag<fill_order_operation>::value;

         // Check Alice's history
         histories = hist_api.get_account_history_operations(
                 "alice", fill_order_op_id, operation_history_id_type(), operation_history_id_type(), 100);
         // There should be one fill order operation
         BOOST_CHECK_EQUAL(histories.size(), 1);
         // Alice's fill order for her limit order should have zero fee
         fill_order_operation alice_fill_op = histories.front().op.get<fill_order_operation>();
         BOOST_CHECK(alice_fill_op.fee == asset(0));
         // Alice's fill order's fill price should equal the expected match price
         // Alice's alice_order_price_implied differs slightly from alice_sell_op.get_price()
         // due to rounding in this test while creating the parameters for the limit order
         const price expected_match_price = alice_sell_op.get_price();
         BOOST_CHECK(alice_fill_op.fill_price == expected_match_price);

         // Check Bob's history
         histories = hist_api.get_account_history_operations(
                 "bob", fill_order_op_id, operation_history_id_type(), operation_history_id_type(), 100);
         // There should be one fill order operation
         BOOST_CHECK_EQUAL(histories.size(), 1);
         // Bob's fill order for his margin call should have a fee equal to the margin call fee
         fill_order_operation bob_fill_op = histories.front().op.get<fill_order_operation>();
         BOOST_CHECK(bob_fill_op.fee == expected_margin_call_fee_from_bob_debt_core);
         // Bob's fill order's fill price should equal the expected match price
         BOOST_CHECK(bob_fill_op.fill_price == expected_match_price);

      } FC_LOG_AND_RETHROW()
   }


   /**
    * Test a scenario of a partial Filling of a Call Order as a Taker after HF
    * where the partial filling is due to call order defining a target collateral ratio (TCR) (BSIP38)
    *
    * 0. Advance to HF
    * 1. Initialize actors and a smart asset called SMARTBIT
    * 2. Publish feed
    * 3. (Order 1: Limit order) Alice places a **"large"** limit order to sell SMARTBIT
    * 4. (Order 2: Call order) Bob borrows a **"small"** amount of SMARTBIT into existence.
    *     Bob retains the asset in his own balances, or transfers it, or sells it is not critical
    *     because his debt position is what will be tracked.
    * 5. The feed price indicates that the collateral drops enough to trigger a margin call
    *    **and** enough to be matched against Alice's limit order.
    *    Alice's limit order should be matched against Bob's "activated" call order.
    *    **Alice should be charged as a maker, and Bob as a taker.**
    *    Alice's limit order should be (partially or completely) filled,
    *    but Bob's order will also only be partially filled because the TCR will sell just enough collateral
    *    so that the remaining CR of the debt position >= TCR.
    *    Bob's debt position should remain open.
    */
   BOOST_AUTO_TEST_CASE(target_cr_partial_fill_of_call_order_as_taker) {
      try {
         //////
         // 0. Advance to activate hardfork
         //////
         BOOST_TEST_MESSAGE("Advancing past Hardfork BSIP74");
         generate_blocks(HARDFORK_CORE_BSIP74_TIME);
         generate_block();
         set_expiration(db, trx);


         //////
         // 1. Initialize actors and a smart asset called SMARTBIT
         //////
         // Initialize for the current time
         trx.clear();
         set_expiration(db, trx);

         // Initialize actors
         ACTORS((alice)(bob)(charlie));
         ACTORS((smartissuer)(feedproducer));

         // Initialize tokens
         // CORE asset exists by default
         const asset_object &core = asset_id_type()(db);
         const asset_id_type core_id = core.id;
         const int64_t CORE_UNIT = asset::scaled_precision(core.precision).value; // 100000 satoshi CORE in 1 CORE

         // Create the SMARTBIT asset
         const int16_t SMARTBIT_UNIT = 10000; // 10000 satoshi SMARTBIT in 1 SMARTBIT
         const uint16_t smartbit_market_fee_percent = 2 * GRAPHENE_1_PERCENT;
         const uint16_t smartbit_margin_call_fee_ratio = 50; // 5% expressed in terms of GRAPHENE_COLLATERAL_RATIO_DENOM
         // Define the margin call fee ratio
         create_bitasset("SMARTBIT", smartissuer_id, smartbit_market_fee_percent, charge_market_fee, 4, core_id,
                         GRAPHENE_MAX_SHARE_SUPPLY, {}, smartbit_margin_call_fee_ratio);
         // Obtain asset object after a block is generated to obtain the final object that is commited to the database
         generate_block();
         const asset_object smartbit = get_asset("SMARTBIT");
         const asset_id_type smartbit_id = smartbit.id;
         update_feed_producers(smartbit, {feedproducer_id});

         // Initialize token balance of actors

         // Alice should start with enough CORE to back 5000 SMARTBIT subject to
         // (a) to an initial price feed of 1 satoshi SMARTBIT for 20 satoshi CORE
         // = 0.0001 SMARTBIT for 0.00020 CORE = 1 SMARTBIT for 2 CORE
         // (b) an initial collateral ratio of 4x
         const price initial_feed_price =
                 smartbit.amount(1) / core.amount(20); // 1 satoshi SMARTBIT for 20 satoshi CORE
         const asset alice_initial_smart = smartbit.amount(500 * SMARTBIT_UNIT); // 5,000,000 satoshi SMARTBIT
         const asset alice_initial_core = core.amount(
                 4 * (alice_initial_smart * initial_feed_price).amount); // 400,000,000 satoshi CORE
         transfer(committee_account, alice_id, alice_initial_core);
         BOOST_CHECK_EQUAL(get_balance(alice_id, core_id), alice_initial_core.amount.value);

         // Bob should start with enough CORE to back 200 SMARTBIT subject to
         // (a) to an initial price feed of 1 satoshi SMARTBIT for 20 satoshi CORE
         // = 0.0001 SMARTBIT for 0.00020 CORE = 1 SMARTBIT for 2 CORE
         // (b) an initial collateral ratio of 2x
         const asset bob_initial_smart = smartbit.amount(200 * SMARTBIT_UNIT); // 2,000,000 satoshi SMARTBIT
         const asset bob_initial_core = core.amount(
                 2 * (bob_initial_smart * initial_feed_price).amount); // 80,000,000 satoshi CORE
         transfer(committee_account, bob_id, bob_initial_core);
         BOOST_REQUIRE_EQUAL(get_balance(bob, core), 80000000);

         // In Step 5, the feed price will be adjusted such that
         // the initial CR of Bob's position (CR_0) drops to 1.7x = (17/10)x
         // Want new price = 1.7 / CR_0 = (17/10) / CR_0
         //
         // Collateral ratios are defined as collateral / debt
         // BitShares prices are conventionally defined as debt / collateral
         // The new price can be expressed with the available codebase as
         // = (17/10) * ~CR_0 = ~CR_0 * (17/10)
         const price expected_bob_initial_cr =
                 core.amount(2 * 20) / smartbit.amount(1); // 1 satoshi SMARTBIT for 40 satoshi CORE
         const price intermediate_feed_price =
                 ~expected_bob_initial_cr * ratio_type(17, 10); // Units of debt / collateral
         // Reduces to (2000000 * 17) / (80000000 * 10) = (17) / (40 * 10) = 17 satoshi SMARTBIT / 400 satoshi CORE
         BOOST_CHECK_EQUAL(intermediate_feed_price.base.amount.value, 17); // satoshi SMARTBIT
         BOOST_CHECK_EQUAL(intermediate_feed_price.quote.amount.value, 400); // satoshi CORE
         BOOST_CHECK(intermediate_feed_price < initial_feed_price);


         //////
         // 2. Publish feed
         //////
         price_feed current_feed;
         current_feed.settlement_price = initial_feed_price;
         current_feed.maintenance_collateral_ratio = 1750; // MCR of 1.75x
         current_feed.maximum_short_squeeze_ratio = 1500; // MSSR of 1.50x
         publish_feed(smartbit, feedproducer_id(db), current_feed);
         FC_ASSERT(smartbit.bitasset_data(db).current_feed.settlement_price == current_feed.settlement_price);


         //////
         // 3. (Order 1: Limit order) Alice places a **"large"** limit order to sell SMARTBIT.
         //////
         // Alice borrows SMARTBIT
         borrow(alice, alice_initial_smart, alice_initial_core);
         BOOST_CHECK_EQUAL(get_balance(alice_id(db), smartbit_id(db)), 500 * SMARTBIT_UNIT);
         BOOST_CHECK_EQUAL(get_balance(alice_id, core_id), 0 * CORE_UNIT);
         BOOST_CHECK(!smartbit.bitasset_data(db).has_settlement()); // No global settlement

         // Alice offer to sell the SMARTBIT
         // Create a "large" sell order at a "high" price of settlement_price * 1.1 = settlement_price * (11/10)
         const price alice_order_price_implied = intermediate_feed_price * ratio_type(11, 10);
         // = (17 satoshi SMARTBIT / 400 satoshi CORE) * (11/10)
         // = 187 satoshi SMARTBIT / 4000 satoshi CORE
         BOOST_CHECK_EQUAL(alice_order_price_implied.base.amount.value, 187); // satoshi SMARTBIT
         BOOST_CHECK_EQUAL(alice_order_price_implied.quote.amount.value, 4000); // satoshi CORE

         const asset alice_debt_to_sell = smartbit.amount(get_balance(alice_id(db), smartbit_id(db)));
         // multiply_and_round_up() handles inverting the price so that the output is in correct collateral units
         const asset alice_collateral_to_buy = alice_debt_to_sell.multiply_and_round_up(alice_order_price_implied);
         //
         // NOTE: The calculated limit order price is 5000000 satoshi SMARTBIT / 106951872 satoshi CORE
         //
         limit_order_create_operation alice_sell_op = create_sell_operation(alice_id, alice_debt_to_sell,
                                                                            alice_collateral_to_buy);
         trx.clear();
         trx.operations.push_back(alice_sell_op);
         // asset alice_sell_fee = db.current_fee_schedule().set_fee(trx.operations.back());
         sign(trx, alice_private_key);
         processed_transaction ptx = PUSH_TX(db, trx); // No exception should be thrown
         limit_order_id_type alice_order_id = ptx.operation_results[0].get<object_id_type>();

         // Alice should have no balance
         BOOST_CHECK_EQUAL(get_balance(alice_id(db), smartbit_id(db)), 0 * SMARTBIT_UNIT);
         BOOST_CHECK_EQUAL(get_balance(alice_id, core_id), 0 * CORE_UNIT);


         //////
         // 4. (Order 1: Call order) Bob borrows a **"small"** amount of SMARTBIT into existence.
         //    Bob retains the asset in his own balances, or transfers it, or sells it is not critical
         //    because his debt position is what will be tracked.
         //////
         const uint16_t tcr = 2200; // Bob's target collateral ratio (TCR) 220% expressed in terms of GRAPHENE_COLLATERAL_RATIO_DENOM
         call_order_id_type bob_call_id = (*borrow(bob, bob_initial_smart, bob_initial_core, tcr)).id;
         BOOST_REQUIRE_EQUAL(get_balance(bob, smartbit), 200 * SMARTBIT_UNIT);
         BOOST_CHECK(!smartbit.bitasset_data(db).has_settlement()); // No global settlement
         const price bob_initial_cr = bob_call_id(db).collateralization(); // Units of collateral / debt
         BOOST_CHECK_EQUAL(bob_initial_cr.base.amount.value, 80000000); // Collateral of 80,000,000 satoshi CORE
         BOOST_CHECK_EQUAL(bob_initial_cr.quote.amount.value, 2000000); // Debt of 2,000,000 satoshi SMARTBIT

         //////
         // Bob transfers his SMARTBIT to Charlie to clarify the accounting
         //////
         transfer(bob_id, charlie_id, bob_initial_smart);
         BOOST_CHECK_EQUAL(get_balance(bob_id, smartbit_id), 0 * SMARTBIT_UNIT);
         BOOST_CHECK_EQUAL(get_balance(bob_id, core_id), 0 * CORE_UNIT);
         BOOST_CHECK_EQUAL(get_balance(charlie_id, smartbit_id), 200 * SMARTBIT_UNIT);
         BOOST_CHECK_EQUAL(get_balance(charlie_id, core_id), 0 * CORE_UNIT);


         //////
         // 5. The feed price indicates that the collateral drops enough to trigger a margin call
         //    **and** enough to be matched against Alice's limit order.
         //    Alice's limit order should be matched against Bob's "activated" call order.
         //    **Alice should be charged as a maker, and Bob as a taker.**
         //    Alice's limit order should be (partially or completely) filled,
         //    but Bob's order will also only be partially filled because the TCR will sell just enough collateral
         //    so that the remaining CR of the debt position >= TCR.
         //    Bob's debt position should remain open.
         //////
         current_feed.settlement_price = intermediate_feed_price;
         publish_feed(smartbit, feedproducer_id(db), current_feed);
         // Confirm the updated feed
         BOOST_CHECK(smartbit.bitasset_data(db).current_feed.settlement_price == current_feed.settlement_price);
         // Confirm no global settlement
         BOOST_CHECK(!smartbit.bitasset_data(db).has_settlement());

         // When a TCR is set for a call order, the ideal is to not sell all of the collateral
         // but only enough collateral so that the remaining collateral and the remaining debt in the debt position
         // has a resulting CR >= TCR.  The specifications are described in BSIP38.
         //
         // Per BSIP38, the expected amount to sell from the call order is
         // max_amount_to_sell = (debt * target_CR - collateral * feed_price) / (target_CR * match_price - feed_price)
         //
         // HOWEVER, the match price that is used in this calculation
         // NEEDS TO BE ADJUSTED to account for the extra MCFR > 0 that will be paid by the call order.
         //
         // Rather than using a match price of limit_order_price **AS A TAKER**,
         // the call_pays_price of limit_order_price * (MSSR-MCFR) / MSSR should be used
         // when determining the amount of collateral and debt that will removed from the debt position.
         //
         // The limit order will still be compensated based on the quoted match price of limit_order_price
         // but the calculation from BSIP38 should use the call_pays_price which reflects that the call order
         // will actually pay more collateral
         // (it can be considered as a higher effective price when denominated in collateral / debt,
         // or equivalently a lower effective price when denominated in debt / collateral).

         // Therefore, the call_pays_price, WHEN THE CALL ORDER IS TAKER,
         //                 = limit_order_price*(MSSR-MCFR)/MSSR reduces to
         //
         // call_pays_price = (5000000 satoshi SMARTBIT / 106951872 satoshi CORE) * ([1500-50] / 1500)
         //                 = (5000000 satoshi SMARTBIT / 106951872 satoshi CORE) * (1450 / 1500)
         //                 = (5000000 satoshi SMARTBIT / 106951872 satoshi CORE) * (29 / 30)
         //                 = (500000 satoshi SMARTBIT / 106951872 satoshi CORE) * (29 / 3)
         //                 = (14500000 satoshi SMARTBIT / 320855616 satoshi CORE)
         //                 = (453125 satoshi SMARTBIT / 10026738 satoshi CORE)

         // Returning to the formula for the TCR amount to sell from the call order
         // max_amount_to_sell = (debt * target_CR - collateral * feed_price) / (target_CR * call_pays_price - feed_price)
         //
         // = (2000000 satoshi SMARTBIT * [2200 / 1000] - 80000000 satoshi CORE * [17 satoshi SMARTBIT / 400 satoshi CORE])
         //   / ([2200 / 1000] *  [453125 satoshi SMARTBIT / 10026738 satoshi CORE] - [17 satoshi SMARTBIT / 400 satoshi CORE])
         //
         // = (2000000 satoshi SMARTBIT * [22 / 10] - 80000000 satoshi SMARTBIT * [17 / 400])
         //   / ([22 / 10] *  [453125 satoshi SMARTBIT / 10026738 satoshi CORE] - [17 satoshi SMARTBIT / 400 satoshi CORE])
         //
         // = (200000 satoshi SMARTBIT * [22] - 200000 satoshi SMARTBIT * [17])
         //   / ([22 / 10] *  [453125 satoshi SMARTBIT / 10026738 satoshi CORE] - [17 satoshi SMARTBIT / 400 satoshi CORE])
         //
         // = (200000 satoshi SMARTBIT * [22 - 17])
         //   / ([22 / 10] *  [453125 satoshi SMARTBIT / 10026738 satoshi CORE] - [17 satoshi SMARTBIT / 400 satoshi CORE])
         //
         // = (200000 satoshi CORE * [5]) / ([22 / 10] *  [453125 / 10026738] - [17 / 400])
         //
         // = (1000000 satoshi CORE) / ([22 / 10] *  [453125 / 10026738] - [17 / 400])
         //
         // ~= (1000000 satoshi CORE) / (0.0569216663485) ~= 17568002.9117 satoshi CORE
         //
         // ~= rounded up to 17568003 satoshi CORE = 175.68003 CORE
         // const asset expected_max_amount_to_sell = core.amount(17568003);
         // match() is calculating ???? CORE

         // Per BSIP38, the expected amount to cover from the call order
         //
         // max_debt_to_cover = max_amount_to_sell * match_price
         //
         // which is adjusted to
         //
         // max_debt_to_cover = max_amount_to_sell * call_pays_price

         // Therefore the
         //
         // = (1000000 satoshi CORE) / ([22 / 10] *  [17 / 600] - [17 / 400])
         //   * (453125 satoshi SMARTBIT / 10026738 satoshi CORE)
         //
         // ~= 17568002.9117 satoshi CORE * (453125 satoshi SMARTBIT / 10026738 satoshi CORE)
         //
         // ~= 793927.329044 satoshi SMARTBIT rounded down to 793927 satoshi SMARTBIT = 79.3927 SMARTBIT
         // ~= 793927.329044 satoshi SMARTBIT rounded up to 793928 satoshi SMARTBIT = 79.3928 SMARTBIT
         const asset expected_max_debt_to_cover = smartbit.amount(793928);


         // WHEN THE CALL ORDER IS TAKER, the match_price is the limit_order price
         // Payment to limit order = X/match_price = X/limit_order_price
         // = 793928 satoshi SMARTBIT * (106951872 satoshi CORE / 5000000 satoshi SMARTBIT)
         // = 16982417.1666 satoshi CORE rounded up to 16982418 satoshi CORE = 169.82418 CORE
         // Margin call should exchange the filled debt (X) for X/limit_order_price
         const asset expected_payment_to_alice_core = core.amount(16982418);

         // Caluclate the expected payment in collateral by the call order
         // to fulfill the (complete or partial) filling of the margin call.
         //
         // The expected payment is not necessarily equal to BSIP38's max_amount_to_sell.
         // It should be calculated base on the amount paid to the limit order (X), the settlement price,
         // and the MSSR.
         //
         // Expected payment by call order = X/fill_price
         // Expected payment by call order = X/[settlement_price*(MSSR-MCFR)/MSSR]
         // Expected payment by call order
         // = 793928 satoshi SMARTBIT / (453125 satoshi SMARTBIT / 10026738 satoshi CORE)
         // = 17568017.7586 satoshi CORE rounding up to 17568018 satoshi CORE = 175.68018 CORE
         const asset expected_payment_from_bob_core = core.amount(17568018);

         // The call order MUST ALSO pay the margin call fee
         // Expected fee = payment by call order - payment to limit order
         const asset expected_margin_call_fee = expected_payment_from_bob_core - expected_payment_to_alice_core;

         // Check Alice's balances
         BOOST_CHECK_EQUAL(get_balance(alice, smartbit), 0);
         BOOST_CHECK_EQUAL(get_balance(alice, core), 0 + expected_payment_to_alice_core.amount.value);

         // Alice's limit order should be open because of its partial filling
         BOOST_CHECK(db.find(alice_order_id));

         // Check Alice's limit order
         // The amount of smart asset available for sale should be reduced by the amount paid to Bob's margin call
         limit_order_object alice_limit_order = alice_order_id(db);
         asset expected_alice_remaining_smart_for_sale = alice_debt_to_sell - expected_max_debt_to_cover;
         BOOST_CHECK_EQUAL(alice_limit_order.amount_for_sale().amount.value,
                           expected_alice_remaining_smart_for_sale.amount.value);
         // Alice's limit order's price should be unchanged by the margin call
         BOOST_CHECK(alice_limit_order.sell_price == alice_sell_op.get_price());

         // Bob's debt position should be open because of its partial filling
         BOOST_CHECK(db.find(bob_call_id));

         // Check Bob's debt position
         BOOST_CHECK_EQUAL(bob_call_id(db).debt.value,
                           bob_initial_smart.amount.value - expected_max_debt_to_cover.amount.value);
         BOOST_CHECK_EQUAL(bob_call_id(db).collateral.value,
                           bob_initial_core.amount.value - expected_payment_to_alice_core.amount.value -
                           expected_margin_call_fee.amount.value);

         // Bob's balances should not have changed because his debt position should remain open
         // because the debt position is still open
         BOOST_CHECK_EQUAL(get_balance(bob_id(db), smartbit_id(db)), 0 * SMARTBIT_UNIT);
         BOOST_CHECK_EQUAL(get_balance(bob_id(db), core_id(db)), 0 * CORE_UNIT);

         // Charlie's balances should not have changed
         BOOST_CHECK_EQUAL(get_balance(charlie_id, smartbit_id), 200 * SMARTBIT_UNIT);
         BOOST_CHECK_EQUAL(get_balance(charlie_id, core_id), 0 * CORE_UNIT);

         // Check the asset owner's accumulated asset fees
         BOOST_CHECK(smartbit.dynamic_asset_data_id(db).accumulated_fees == 0);
         BOOST_CHECK_EQUAL(smartbit.dynamic_asset_data_id(db).accumulated_collateral_fees.value,
                           expected_margin_call_fee.amount.value);

         // Check the fee of the fill operations for Alice and Bob
         generate_block(); // To trigger db_notify() and record pending operations into histories
         graphene::app::history_api hist_api(app);
         vector <operation_history_object> histories;
         const int fill_order_op_id = operation::tag<fill_order_operation>::value;

         // Check Alice's history
         histories = hist_api.get_account_history_operations(
                 "alice", fill_order_op_id, operation_history_id_type(), operation_history_id_type(), 100);
         // There should be one fill order operation
         BOOST_CHECK_EQUAL(histories.size(), 1);
         // Alice's fill order for her limit order should have zero fee
         fill_order_operation alice_fill_op = histories.front().op.get<fill_order_operation>();
         BOOST_CHECK(alice_fill_op.fee == asset(0));
         // Alice's fill order's fill price should equal the expected match price
         // Alice's alice_order_price_implied differs slightly from alice_sell_op.get_price()
         // due to rounding in this test while creating the parameters for the limit order
         const price expected_match_price = alice_sell_op.get_price();
         BOOST_CHECK(alice_fill_op.fill_price == expected_match_price);

         // Check Bob's history
         histories = hist_api.get_account_history_operations(
                 "bob", fill_order_op_id, operation_history_id_type(), operation_history_id_type(), 100);
         // There should be one fill order operation
         BOOST_CHECK_EQUAL(histories.size(), 1);
         // Bob's fill order for his margin call should have a fee equal to the margin call fee
         fill_order_operation bob_fill_op = histories.front().op.get<fill_order_operation>();
         BOOST_CHECK(bob_fill_op.fee == expected_margin_call_fee);
         // Bob's fill order's fill price should equal the expected match price
         BOOST_CHECK(bob_fill_op.fill_price == expected_match_price);

      } FC_LOG_AND_RETHROW()
   }


   /**
    * Test a scenario of a Complete Fill of a Call Order as a Taker after HF
    * where the matching to an existing limit order becomes possible
    * after the MCFR is reduced and without any change to the feed price.
    * This is made possible by the reduction of the MCFR changing the margin call order price.
    *
    * 0. Advance to HF
    * 1. Initialize actors and a smart asset called SMARTBIT
    * 2. Publish feed
    * 3. (Order 1: Limit order) Alice places a **"large"** limit order to sell SMARTBIT
    * 4. (Order 2: Call order) Bob borrows a **"small"** amount of SMARTBIT into existence.
    *     Bob retains the asset in his own balances, or transfers it, or sells it is not critical
    *     because his debt position is what will be tracked.
    * 5. The feed price indicates that the collateral drops enough to trigger a margin call
    *    **but** the margin call order price (denominated in debt/collateral) is less than
    *    than Alice's limit order price, resulting in no match.
    * 6. The asset owner reduces the MCFR enough such that Alice's offer price SHOULD overlap
    *    with the margin call order price.
    *    Alice's limit order should be matched against Bob's "activated" call order.
    *    **Alice should be charged as a maker, and Bob as a taker.**
    *    Alice's limit order should be partially filled,
    *    but Bob's order should be completely filled and removed from the book.
    */
   BOOST_AUTO_TEST_CASE(mcfr_reduction_triggers_matching_of_margin_call_order) {
      try {
         //////
         // 0. Advance to activate hardfork
         //////
         BOOST_TEST_MESSAGE("Advancing past Hardfork BSIP74");
         generate_blocks(HARDFORK_CORE_BSIP74_TIME);
         generate_block();
         set_expiration(db, trx);


         //////
         // 1. Initialize actors and a smart asset called SMARTBIT
         //////
         // Initialize for the current time
         trx.clear();
         set_expiration(db, trx);

         // Initialize actors
         ACTORS((alice)(bob)(charlie));
         ACTORS((smartissuer)(feedproducer));

         // Initialize tokens
         // CORE asset exists by default
         const asset_object &core = asset_id_type()(db);
         const asset_id_type core_id = core.id;
         const int64_t CORE_UNIT = asset::scaled_precision(core.precision).value; // 100000 satoshi CORE in 1 CORE

         // Create the SMARTBIT asset
         const int16_t SMARTBIT_UNIT = 10000; // 10000 satoshi SMARTBIT in 1 SMARTBIT
         const uint16_t smartbit_market_fee_percent = 2 * GRAPHENE_1_PERCENT;
         const uint16_t initial_mcfr = 400; // 40% expressed in terms of GRAPHENE_COLLATERAL_RATIO_DENOM
         const uint16_t final_mcfr = 50; // 5% expressed in terms of GRAPHENE_COLLATERAL_RATIO_DENOM
         // Define the margin call fee ratio
         create_bitasset("SMARTBIT", smartissuer_id, smartbit_market_fee_percent, charge_market_fee, 4, core_id,
                         GRAPHENE_MAX_SHARE_SUPPLY, {}, initial_mcfr);
         // Obtain asset object after a block is generated to obtain the final object that is commited to the database
         generate_block();
         const asset_object smartbit = get_asset("SMARTBIT");
         const asset_id_type smartbit_id = smartbit.id;
         update_feed_producers(smartbit, {feedproducer_id});

         // Initialize token balance of actors

         // Alice should start with enough CORE to back 5000 SMARTBIT subject to
         // (a) to an initial price feed of 1 satoshi SMARTBIT for 20 satoshi CORE
         // = 0.0001 SMARTBIT for 0.00020 CORE = 1 SMARTBIT for 2 CORE
         // (b) an initial collateral ratio of 4x
         const price initial_feed_price =
                 smartbit.amount(1) / core.amount(20); // 1 satoshi SMARTBIT for 20 satoshi CORE
         const asset alice_initial_smart = smartbit.amount(500 * SMARTBIT_UNIT); // 5,000,000 satoshi SMARTBIT
         const asset alice_initial_core = core.amount(
                 4 * (alice_initial_smart * initial_feed_price).amount); // 400,000,000 satoshi CORE
         transfer(committee_account, alice_id, alice_initial_core);
         BOOST_CHECK_EQUAL(get_balance(alice_id, core_id), alice_initial_core.amount.value);

         // Bob should start with enough CORE to back 200 SMARTBIT subject to
         // (a) to an initial price feed of 1 satoshi SMARTBIT for 20 satoshi CORE
         // = 0.0001 SMARTBIT for 0.00020 CORE = 1 SMARTBIT for 2 CORE
         // (b) an initial collateral ratio of 2x
         const asset bob_initial_smart = smartbit.amount(200 * SMARTBIT_UNIT); // 2,000,000 satoshi SMARTBIT
         const asset bob_initial_core = core.amount(
                 2 * (bob_initial_smart * initial_feed_price).amount); // 80,000,000 satoshi CORE
         transfer(committee_account, bob_id, bob_initial_core);
         BOOST_REQUIRE_EQUAL(get_balance(bob, core), 80000000);

         // In Step 5, the feed price will be adjusted such that
         // the initial CR of Bob's position (CR_0) drops to 1.7x = (17/10)x
         // Want new price = 1.7 / CR_0 = (17/10) / CR_0
         //
         // Collateral ratios are defined as collateral / debt
         // BitShares prices are conventionally defined as debt / collateral
         // The new price can be expressed with the available codebase as
         // = (17/10) * ~CR_0 = ~CR_0 * (17/10)
         const price expected_bob_initial_cr =
                 core.amount(2 * 20) / smartbit.amount(1); // 1 satoshi SMARTBIT for 40 satoshi CORE
         const price intermediate_feed_price =
                 ~expected_bob_initial_cr * ratio_type(17, 10); // Units of debt / collateral
         // Reduces to (2000000 * 17) / (80000000 * 10) = (17) / (40 * 10) = 17 satoshi SMARTBIT / 400 satoshi CORE
         BOOST_CHECK_EQUAL(intermediate_feed_price.base.amount.value, 17); // satoshi SMARTBIT
         BOOST_CHECK_EQUAL(intermediate_feed_price.quote.amount.value, 400); // satoshi CORE
         BOOST_CHECK(intermediate_feed_price < initial_feed_price);

         // Pre-calculate the planned initial margin call order price (MCOP)
         const uint16_t mssr = 1500;
         const uint16_t initial_ratio_numerator = mssr - initial_mcfr;
         BOOST_REQUIRE_EQUAL(initial_ratio_numerator, 1100);
         const price planned_initial_mcop = intermediate_feed_price * ratio_type(GRAPHENE_COLLATERAL_RATIO_DENOM,
                                                                                 initial_ratio_numerator);
         // The initial MCOP should = 17 satoshi SMARTBIT / 400 satoshi CORE / (1100 / 1000)
         //                         = 17 satoshi SMARTBIT / 400 satoshi CORE * (1000 / 1100)
         //                         = 17 satoshi SMARTBIT / 4 satoshi CORE * (10 / 1100)
         //                         = 17 satoshi SMARTBIT / 4 satoshi CORE * (1 / 110)
         //                         = 17 satoshi SMARTBIT / 440 satoshi CORE
         //                        ~= 0.0386 satoshi SMARTBIT / satoshi CORE
         BOOST_CHECK_EQUAL(planned_initial_mcop.base.amount.value, 17); // satoshi SMARTBIT
         BOOST_CHECK_EQUAL(planned_initial_mcop.quote.amount.value, 440); // satoshi CORE

         // Pre-calculate the planned final margin call order price (MCOP)
         const uint16_t final_ratio_numerator = mssr - final_mcfr;
         BOOST_REQUIRE_EQUAL(final_ratio_numerator, 1450);
         const price planned_final_mcop = intermediate_feed_price * ratio_type(GRAPHENE_COLLATERAL_RATIO_DENOM,
                                                                               final_ratio_numerator);
         // The final MCOP should   = 17 satoshi SMARTBIT / 400 satoshi CORE / (1450 / 1000)
         //                         = 17 satoshi SMARTBIT / 400 satoshi CORE * (1000 / 1450)
         //                         = 17 satoshi SMARTBIT / 4 satoshi CORE * (10 / 1450)
         //                         = 17 satoshi SMARTBIT / 4 satoshi CORE * (1 / 145)
         //                         = 17 satoshi SMARTBIT / 580 satoshi CORE
         //                        ~= 0.0293 satoshi SMARTBIT / satoshi CORE
         BOOST_CHECK_EQUAL(planned_final_mcop.base.amount.value, 17); // satoshi SMARTBIT
         BOOST_CHECK_EQUAL(planned_final_mcop.quote.amount.value, 580); // satoshi CORE


         //////
         // 2. Publish feed
         //////
         price_feed current_feed;
         current_feed.settlement_price = initial_feed_price;
         current_feed.maintenance_collateral_ratio = 1750; // MCR of 1.75x
         current_feed.maximum_short_squeeze_ratio = mssr; // MSSR of 1.50x
         publish_feed(smartbit, feedproducer_id(db), current_feed);
         FC_ASSERT(smartbit.bitasset_data(db).current_feed.settlement_price == current_feed.settlement_price);


         //////
         // 3. (Order 1: Limit order) Alice places a **"large"** limit order to sell SMARTBIT.
         //////
         // Alice borrows SMARTBIT
         call_order_id_type alice_call_id = (*borrow(alice, alice_initial_smart, alice_initial_core)).id;
         BOOST_CHECK_EQUAL(get_balance(alice_id(db), smartbit_id(db)), 500 * SMARTBIT_UNIT);
         BOOST_CHECK_EQUAL(get_balance(alice_id, core_id), 0 * CORE_UNIT);
         BOOST_CHECK(!smartbit.bitasset_data(db).has_settlement()); // No global settlement

         // Alice offer to sell the SMARTBIT
         const asset alice_debt_to_sell = smartbit.amount(500 * SMARTBIT_UNIT);
         const asset alice_collateral_to_buy = core.amount(1500 * CORE_UNIT); // 150,000,000 satoshi CORE
         limit_order_create_operation alice_sell_op = create_sell_operation(alice_id, alice_debt_to_sell,
                                                                            alice_collateral_to_buy);

         // Check the new price relative to the planned initial and final MCOP
         // The implied resulting price = 5,000,000 satoshi SMARTBIT / 150,000,000 satoshi CORE
         //                             = 1 satoshi SMARTBIT / 30 satoshi CORE
         //                            ~= 0.033 satoshi SMARTBIT / satoshi CORE
         const price alice_order_price_implied = price(smartbit.amount(1), core.amount(30));
         BOOST_REQUIRE(alice_sell_op.get_price() == alice_order_price_implied);
         // Alice's offer price should be less than the intermediate MCOP
         BOOST_REQUIRE(alice_sell_op.get_price() < planned_initial_mcop);
         // Alice's offer price should be more than the final MCOP
         BOOST_REQUIRE(alice_sell_op.get_price() > planned_final_mcop);

         // Submit the limit order
         trx.clear();
         trx.operations.push_back(alice_sell_op);
         // asset alice_sell_fee = db.current_fee_schedule().set_fee(trx.operations.back());
         sign(trx, alice_private_key);
         processed_transaction ptx = PUSH_TX(db, trx); // No exception should be thrown
         limit_order_id_type alice_order_id = ptx.operation_results[0].get<object_id_type>();

         // Alice should have no balance
         BOOST_CHECK_EQUAL(get_balance(alice_id(db), smartbit_id(db)), 0 * SMARTBIT_UNIT);
         BOOST_CHECK_EQUAL(get_balance(alice_id, core_id), 0 * CORE_UNIT);



         //////
         // 4. (Order 2: Call order) Bob borrows a **"small"** amount of SMARTBIT into existence.
         //    Bob retains the asset in his own balances, or transfers it, or sells it is not critical
         //    because his debt position is what will be tracked.
         //////
         const asset bob_initial_debt_smart = bob_initial_smart;
         const asset bob_initial_debt_collateral = bob_initial_core;
         call_order_id_type bob_call_id = (*borrow(bob, bob_initial_debt_smart, bob_initial_debt_collateral)).id;

         // Bobs's balances should reflect that CORE was used to create SMARTBIT
         BOOST_CHECK_EQUAL(get_balance(bob_id, smartbit_id), 200 * SMARTBIT_UNIT);
         BOOST_CHECK_EQUAL(get_balance(bob_id, core_id), 0);
         BOOST_CHECK(!smartbit.bitasset_data(db).has_settlement()); // No global settlement
         const price bob_initial_cr = bob_call_id(db).collateralization(); // Units of collateral / debt
         BOOST_CHECK(bob_initial_cr == expected_bob_initial_cr);
         BOOST_CHECK_EQUAL(bob_initial_cr.base.amount.value, 80000000); // Collateral of 80,000,000 satoshi CORE
         BOOST_CHECK_EQUAL(bob_initial_cr.quote.amount.value, 2000000); // Debt of 2,000,000 satoshi SMARTBIT

         // Alice's balances should not have changed
         BOOST_REQUIRE_EQUAL(get_balance(alice_id(db), smartbit_id(db)), 0 * SMARTBIT_UNIT);
         BOOST_REQUIRE_EQUAL(get_balance(alice_id, core_id), 0 * CORE_UNIT);

         // Alice should not have been margin called
         price alice_initial_cr = alice_call_id(db).collateralization(); // Units of collateral / debt
         BOOST_CHECK_EQUAL(alice_initial_cr.base.amount.value, 400000000); // Collateral of 400,000,000 satoshi CORE
         BOOST_CHECK_EQUAL(alice_initial_cr.quote.amount.value, 5000000); // Debt of 5,000,000 satoshi SMARTBIT

         //////
         // Bob transfers his SMARTBIT to Charlie to clarify the accounting
         //////
         transfer(bob_id, charlie_id, bob_initial_smart);
         BOOST_CHECK_EQUAL(get_balance(bob_id, smartbit_id), 0 * SMARTBIT_UNIT);
         BOOST_CHECK_EQUAL(get_balance(bob_id, core_id), 0 * CORE_UNIT);
         BOOST_CHECK_EQUAL(get_balance(charlie_id, smartbit_id), 200 * SMARTBIT_UNIT);
         BOOST_CHECK_EQUAL(get_balance(charlie_id, core_id), 0 * CORE_UNIT);


         //////
         // 5. The feed price indicates that the collateral drops enough to trigger a margin call
         //    **but** the margin call order price (denominated in debt/collateral) is less than
         //    than Alice's limit order price, resulting in no match.
         //////
         current_feed.settlement_price = intermediate_feed_price;
         publish_feed(smartbit, feedproducer_id(db), current_feed);
         // Confirm the updated feed
         BOOST_CHECK(smartbit.bitasset_data(db).current_feed.settlement_price == current_feed.settlement_price);
         // Confirm no global settlement
         BOOST_CHECK(!smartbit.bitasset_data(db).has_settlement());
         // Verify the margin call order price is as planned
         BOOST_CHECK(smartbit_id(db).bitasset_data(db).current_feed.margin_call_order_price(initial_mcfr)
                     == planned_initial_mcop);

         // Alice's limit order should be open
         BOOST_CHECK(db.find(alice_order_id));

         // Alice's limit order should not be affected
         BOOST_CHECK_EQUAL(alice_order_id(db).amount_for_sale().amount.value,
                           alice_debt_to_sell.amount.value);

         // Bob's debt position should be open
         BOOST_CHECK(db.find(bob_call_id));

         // Bob's debt to the blockchain should not have changed
         BOOST_CHECK_EQUAL(bob_call_id(db).debt.value, bob_initial_smart.amount.value);
         BOOST_CHECK_EQUAL(bob_call_id(db).collateral.value, bob_initial_core.amount.value);

         // Bob's balances should not have changed
         BOOST_CHECK_EQUAL(get_balance(bob_id, smartbit_id), 0 * SMARTBIT_UNIT);
         BOOST_CHECK_EQUAL(get_balance(bob_id, core_id), 0 * CORE_UNIT);


         //////
         // 6. The asset owner reduces the MCFR enough such that Alice's offer price SHOULD overlap
         //    with the margin call order price.
         //    Alice's limit order should be matched against Bob's "activated" call order.
         //    **Alice should be charged as a maker, and Bob as a taker.**
         //    Alice's limit order should be partially filled,
         //     but Bob's order should be completely filled and removed from the book.
         //////
         asset_update_bitasset_operation uop;
         uop.issuer = smartissuer_id;
         uop.asset_to_update = smartbit_id;
         uop.new_options = smartbit_id(db).bitasset_data(db).options;
         uop.new_options.extensions.value.margin_call_fee_ratio = final_mcfr;

         trx.clear();
         trx.operations.push_back(uop);
         db.current_fee_schedule().set_fee(trx.operations.back());
         sign(trx, smartissuer_private_key);
         PUSH_TX(db, trx);

         // Check MCFR is updated
         BOOST_CHECK(smartbit_id(db).bitasset_data(db).options.extensions.value.margin_call_fee_ratio.valid());
         BOOST_CHECK_EQUAL(*smartbit_id(db).bitasset_data(db).options.extensions.value.margin_call_fee_ratio,
                           final_mcfr);

         // Verify the margin call order price is as planned
         BOOST_CHECK(smartbit_id(db).bitasset_data(db).current_feed.margin_call_order_price(final_mcfr)
                     == planned_final_mcop);

         //////
         // Bob's margin call should have been matched with Alice's limit order
         //////

         // The margin call of Bob's position should have closed the debt of bob_initial_smart
         // Bob's margin call should been matched against Alice's limit order
         // Bob's debt position should have paid collateral = bob_initial_smart / limit_order_price
         // 200 SMARTBIT / (1 satoshi SMARTBIT / 30 satoshi CORE)
         // = 2,000,000 satoshi SMARTBIT / (1 satoshi SMARTBIT / 30 satoshi CORE)
         // = 2,000,000 satoshi CORE / (1 / 30)
         // = 60,000,000 satoshi CORE
         const asset expected_margin_call_from_bob_debt_core = core.amount(60000000);

         // Bob's margin call fee, which is paid in collateral, should be charged as a taker
         // The margin call fee debt = filled_debt * MCFR/(MSSR-MCFR) / limit_order_price
         // 200 SMARTBIT * (50 / (1500 - 50)) / (1 satoshi SMARTBIT / 30 satoshi CORE)
         // = 2,000,000 satoshi SMARTBIT * (50 / 1450) / (1 satoshi SMARTBIT / 30 satoshi CORE)
         // = 2,000,000 satoshi CORE * (1 / 29) * (30 / 1)
         // = 2068965.51724 satoshi CORE rounded up to 2068966 satoshi CORE
         const asset expected_margin_call_fee_from_bob_debt_core = core.amount(2068966);

         // The balance of Bob's debt position
         const asset expected_return_from_bob_debt_core = bob_initial_core
                                                          - expected_margin_call_from_bob_debt_core
                                                          - expected_margin_call_fee_from_bob_debt_core;

         // Check Bob's debt position is closed
         BOOST_CHECK(!db.find(bob_call_id));

         // Check Bob's balances
         BOOST_CHECK_EQUAL(get_balance(bob_id(db), smartbit_id(db)), 0 * SMARTBIT_UNIT);
         BOOST_CHECK_EQUAL(get_balance(bob_id(db), core_id(db)), expected_return_from_bob_debt_core.amount.value);

         // Charlie's balances should not have changed
         BOOST_CHECK_EQUAL(get_balance(charlie_id, smartbit_id), 200 * SMARTBIT_UNIT);
         BOOST_CHECK_EQUAL(get_balance(charlie_id, core_id), 0 * CORE_UNIT);

         // Alice's balances should have changed because her limit order was partially filled by the margin call
         BOOST_CHECK_EQUAL(get_balance(alice_id(db), smartbit_id(db)), 0 * SMARTBIT_UNIT);
         BOOST_CHECK_EQUAL(get_balance(alice_id, core_id), expected_margin_call_from_bob_debt_core.amount.value);

         // Check Alice's debt
         // Alice's debt position should not be NOT closed
         BOOST_CHECK(db.find(alice_call_id));
         // Alice's debt should NOT have changed because its CR > MCR
         alice_initial_cr = alice_call_id(db).collateralization(); // Units of collateral / debt
         BOOST_CHECK_EQUAL(alice_initial_cr.base.amount.value, 400000000); // Collateral of 400,000,000 satoshi CORE
         BOOST_CHECK_EQUAL(alice_initial_cr.quote.amount.value, 5000000); // Debt of 5,000,000 satoshi SMARTBIT

         // Check Alice's limit order
         // The amount of smart asset available for sale should be reduced by the amount paid to Bob's margin call
         limit_order_object alice_limit_order = alice_order_id(db);
         asset expected_alice_remaining_smart_for_sale = alice_debt_to_sell - bob_initial_debt_smart;
         asset expected_alice_remaining_core_to_receive =
                 alice_collateral_to_buy - expected_margin_call_from_bob_debt_core;
         BOOST_CHECK(alice_limit_order.amount_for_sale() == expected_alice_remaining_smart_for_sale);
         BOOST_CHECK(alice_limit_order.amount_to_receive() == expected_alice_remaining_core_to_receive);

         // Check the asset owner's accumulated asset fees
         BOOST_CHECK_EQUAL(smartbit.dynamic_asset_data_id(db).accumulated_fees.value, 0);
         BOOST_CHECK_EQUAL(smartbit.dynamic_asset_data_id(db).accumulated_collateral_fees.value,
                           expected_margin_call_fee_from_bob_debt_core.amount.value);

         // Check the fee of the fill operations for Alice and Bob
         generate_block(); // To trigger db_notify() and record pending operations into histories
         graphene::app::history_api hist_api(app);
         vector<operation_history_object> histories;
         const int fill_order_op_id = operation::tag<fill_order_operation>::value;

         // Check Alice's history
         histories = hist_api.get_account_history_operations(
                 "alice", fill_order_op_id, operation_history_id_type(), operation_history_id_type(), 100);
         // There should be one fill order operation
         BOOST_CHECK_EQUAL(histories.size(), 1);
         // Alice's fill order for her limit order should have zero fee
         fill_order_operation alice_fill_op = histories.front().op.get<fill_order_operation>();
         BOOST_CHECK(alice_fill_op.fee == asset(0));
         // Alice's fill order's fill price should equal the expected match price
         // Alice's alice_order_price_implied differs slightly from alice_sell_op.get_price()
         // due to rounding in this test while creating the parameters for the limit order
         const price expected_match_price = alice_sell_op.get_price();
         BOOST_CHECK(alice_fill_op.fill_price == expected_match_price);

         // Check Bob's history
         histories = hist_api.get_account_history_operations(
                 "bob", fill_order_op_id, operation_history_id_type(), operation_history_id_type(), 100);
         // There should be one fill order operation
         BOOST_CHECK_EQUAL(histories.size(), 1);
         // Bob's fill order for his margin call should have a fee equal to the margin call fee
         fill_order_operation bob_fill_op = histories.front().op.get<fill_order_operation>();
         BOOST_CHECK(bob_fill_op.fee == expected_margin_call_fee_from_bob_debt_core);
         // Bob's fill order's fill price should equal the expected match price
         BOOST_CHECK(bob_fill_op.fill_price == expected_match_price);

      } FC_LOG_AND_RETHROW()
   }


   /**
    * Test the ability to create and update assets with a margin call fee ratio (MCFR) before HARDFORK_CORE_BSIP74_TIME
    *
    *
    * Before HARDFORK_CORE_BSIP74_TIME
    *
    * 1. Asset owner fails to create the smart coin called USDBIT with a MCFR
    * 2. Asset owner fails to create the smart coin called USDBIT with a MCFR in a proposal
    * 3. Asset owner succeeds to create the smart coin called USDBIT without a MCFR
    *
    * 4. Asset owner fails to update the smart coin with a MCFR
    * 5. Asset owner fails to update the smart coin with a MCFR in a proposal
    *
    *
    * 6. Activate HARDFORK_CORE_BSIP74_TIME
    *
    *
    * After HARDFORK_CORE_BSIP74_TIME
    *
    * 7. Asset owner succeeds to create the smart coin called CNYBIT with a MCFR
    * 8. Asset owner succeeds to create the smart coin called RUBBIT with a MCFR in a proposal
    *
    * 9. Asset owner succeeds to update the smart coin called CNYBIT with a MCFR
    * 10. Asset owner succeeds to update the smart coin called RUBBIT with a MCFR in a proposal
    *
    * 11. Asset owner succeeds to create the smart coin called YENBIT without a MCFR
    * 12. Asset owner succeeds to update the smart coin called RUBBIT without a MCFR in a proposal
    */
   BOOST_AUTO_TEST_CASE(prevention_before_hardfork_test) {
      try {
         ///////
         // Initialize the scenario
         ///////
         generate_blocks(db.get_dynamic_global_properties().next_maintenance_time);
         trx.clear();
         set_expiration(db, trx);

         // Create actors
         ACTORS((assetowner));

         // CORE asset exists by default
         asset_object core = asset_id_type()(db);
         const asset_id_type core_id = core.id;

         // Fund actors
         uint64_t initial_balance_core = 10000000;
         transfer(committee_account, assetowner_id, asset(initial_balance_core));

         // Confirm before hardfork activation
         BOOST_CHECK(db.head_block_time() < HARDFORK_CORE_BSIP74_TIME);


         ///////
         // 1. Asset owner fails to create the smart coin called bitUSD with a MCFR
         ///////
         const uint16_t market_fee_percent = 2 * GRAPHENE_1_PERCENT;
         const optional<uint16_t> icr_opt = {}; // Initial collateral ratio
         const uint16_t mcfr_5 = 50; // 5% expressed in terms of GRAPHENE_COLLATERAL_RATIO_DENOM
         optional<uint16_t> mcfr_opt = mcfr_5;

         // Attempt to create the smart asset with a MCFR
         // The attempt should fail because it is before HARDFORK_CORE_BSIP74_TIME
         {
            const asset_create_operation create_op = make_bitasset("USDBIT", assetowner_id, market_fee_percent,
                                                                   charge_market_fee, 4, core_id,
                                                                   GRAPHENE_MAX_SHARE_SUPPLY, icr_opt, mcfr_opt);
            trx.clear();
            trx.operations.push_back(create_op);
            sign(trx, assetowner_private_key);
            REQUIRE_EXCEPTION_WITH_TEXT(PUSH_TX(db, trx), "cannot be set before Hardfork BSIP74");
         }

         ///////
         // 2. Asset owner fails to create the smart coin called bitUSD with a MCFR in a proposal
         ///////
         {
            const asset_create_operation create_op = make_bitasset("USDBIT", assetowner_id, market_fee_percent,
                                                                   charge_market_fee, 4, core_id,
                                                                   GRAPHENE_MAX_SHARE_SUPPLY, icr_opt, mcfr_opt);
            proposal_create_operation cop;
            cop.review_period_seconds = 86400;
            uint32_t buffer_seconds = 60 * 60;
            cop.expiration_time = db.head_block_time() + *cop.review_period_seconds + buffer_seconds;
            cop.fee_paying_account = GRAPHENE_TEMP_ACCOUNT;
            cop.proposed_ops.emplace_back(create_op);

            trx.clear();
            trx.operations.push_back(cop);
            // sign(trx, assetowner_private_key);
            REQUIRE_EXCEPTION_WITH_TEXT(PUSH_TX(db, trx), "cannot be set before Hardfork BSIP74");
         }


         ///////
         // 3. Asset owner succeeds to create the smart coin called bitUSD without a MCFR
         ///////
         const optional<uint16_t> mcfr_null_opt = {};
         {
            const asset_create_operation create_op = make_bitasset("USDBIT", assetowner_id, market_fee_percent,
                                                                   charge_market_fee, 4, core_id,
                                                                   GRAPHENE_MAX_SHARE_SUPPLY, icr_opt, mcfr_null_opt);

            trx.clear();
            trx.operations.push_back(create_op);
            sign(trx, assetowner_private_key);
            PUSH_TX(db, trx); // No exception should be thrown
         }

         generate_block();
         set_expiration(db, trx);
         trx.clear();

         const asset_object &bitusd = get_asset("USDBIT");
         core = core_id(db);

         // The force MCFR should not be set
         BOOST_CHECK(!bitusd.bitasset_data(db).options.extensions.value.margin_call_fee_ratio.valid());


         ///////
         // 4. Asset owner fails to update the smart coin with a MCFR
         ///////
         const uint16_t mcfr_3 = 30; // 3% MCFR (BSIP74)
         asset_update_bitasset_operation uop;
         uop.issuer = assetowner_id;
         uop.asset_to_update = bitusd.get_id();
         uop.new_options = bitusd.bitasset_data(db).options;
         uop.new_options.extensions.value.margin_call_fee_ratio = mcfr_3;

         trx.clear();
         trx.operations.push_back(uop);
         db.current_fee_schedule().set_fee(trx.operations.back());
         sign(trx, assetowner_private_key);
         REQUIRE_EXCEPTION_WITH_TEXT(PUSH_TX(db, trx), "cannot be set before Hardfork BSIP74");

         // The MCFR should not be set
         BOOST_CHECK(!bitusd.bitasset_data(db).options.extensions.value.margin_call_fee_ratio.valid());


         ///////
         // 5. Asset owner fails to update the smart coin with a MCFR in a proposal
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
            REQUIRE_EXCEPTION_WITH_TEXT(PUSH_TX(db, trx), "cannot be set before Hardfork BSIP74");

            // The MCFR should not be set
            BOOST_CHECK(!bitusd.bitasset_data(db).options.extensions.value.margin_call_fee_ratio.valid());
         }


         ///////
         // 6. Activate HARDFORK_CORE_BSIP74_TIME
         ///////
         BOOST_CHECK(db.head_block_time() < HARDFORK_CORE_BSIP74_TIME); // Confirm still before hardfork activation
         BOOST_TEST_MESSAGE("Advancing past Hardfork BSIP74");
         generate_blocks(HARDFORK_CORE_BSIP74_TIME);
         generate_block();
         set_expiration(db, trx);
         trx.clear();


         ///////
         // 7. Asset owner succeeds to create the smart coin called CNYBIT with a MCFR
         ///////
         {
            mcfr_opt = mcfr_3;
            const asset_create_operation create_op = make_bitasset("CNYBIT", assetowner_id, market_fee_percent,
                                                                   charge_market_fee, 4, core_id,
                                                                   GRAPHENE_MAX_SHARE_SUPPLY, icr_opt, mcfr_opt);

            trx.clear();
            trx.operations.push_back(create_op);
            sign(trx, assetowner_private_key);
            PUSH_TX(db, trx); // No exception should be thrown
         }

         generate_block();
         set_expiration(db, trx);
         trx.clear();

         const auto &bitcny = get_asset("CNYBIT");

         // The MCFR should be set
         BOOST_CHECK(bitcny.bitasset_data(db).options.extensions.value.margin_call_fee_ratio.valid());
         BOOST_CHECK_EQUAL(*bitcny.bitasset_data(db).options.extensions.value.margin_call_fee_ratio, mcfr_3);


         ///////
         // 8. Asset owner succeeds to create the smart coin called RUBBIT with a MCFR in a proposal
         ///////
         const uint16_t mcfr_1 = 10; // 1% expressed in terms of GRAPHENE_COLLATERAL_RATIO_DENOM
         {
            // Create the proposal
            mcfr_opt = mcfr_1;
            const asset_create_operation create_op = make_bitasset("RUBBIT", assetowner_id, market_fee_percent,
                                                                   charge_market_fee, 4, core_id,
                                                                   GRAPHENE_MAX_SHARE_SUPPLY, icr_opt, mcfr_opt);

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

         // The MCFR should be set
         BOOST_CHECK(bitrub.bitasset_data(db).options.extensions.value.margin_call_fee_ratio.valid());
         BOOST_CHECK_EQUAL(*bitrub.bitasset_data(db).options.extensions.value.margin_call_fee_ratio, mcfr_1);


         ///////
         // 9. Asset owner succeeds to update the smart coin called CNYBIT with a MCFR
         ///////
         uop = asset_update_bitasset_operation();
         uop.issuer = assetowner_id;
         uop.asset_to_update = bitcny.get_id();
         uop.new_options = bitcny.bitasset_data(db).options;
         uop.new_options.extensions.value.margin_call_fee_ratio = mcfr_5;

         trx.clear();
         trx.operations.push_back(uop);
         db.current_fee_schedule().set_fee(trx.operations.back());
         sign(trx, assetowner_private_key);
         PUSH_TX(db, trx);

         // The MCFR should be set
         BOOST_CHECK(bitcny.bitasset_data(db).options.extensions.value.margin_call_fee_ratio.valid());
         BOOST_CHECK_EQUAL(*bitcny.bitasset_data(db).options.extensions.value.margin_call_fee_ratio, mcfr_5);


         ///////
         // 10. Asset owner succeeds to update the smart coin called RUBBIT with a MCFR in a proposal
         ///////
         {
            // Create the proposal
            uop = asset_update_bitasset_operation();
            uop.issuer = assetowner_id;
            uop.asset_to_update = bitrub.get_id();
            uop.new_options = bitrub.bitasset_data(db).options;
            uop.new_options.extensions.value.margin_call_fee_ratio = mcfr_5;

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

         // The MCFR should be set
         BOOST_CHECK(bitrub.bitasset_data(db).options.extensions.value.margin_call_fee_ratio.valid());
         BOOST_CHECK_EQUAL(*bitrub.bitasset_data(db).options.extensions.value.margin_call_fee_ratio, mcfr_5);


         ///////
         // 11. Asset owner succeeds to create the smart coin called YENBIT without a MCFR
         ///////
         {
            const asset_create_operation create_op = make_bitasset("YENBIT", assetowner_id, market_fee_percent,
                                                                   charge_market_fee, 4, core_id,
                                                                   GRAPHENE_MAX_SHARE_SUPPLY, icr_opt, mcfr_null_opt);

            trx.clear();
            trx.operations.push_back(create_op);
            sign(trx, assetowner_private_key);
            PUSH_TX(db, trx); // No exception should be thrown
         }

         generate_block();
         set_expiration(db, trx);
         trx.clear();

         const auto &bityen = get_asset("YENBIT");

         // The MCFR should be set
         BOOST_CHECK(!bityen.bitasset_data(db).options.extensions.value.margin_call_fee_ratio.valid());


         ///////
         // 12. Asset owner succeeds to update the smart coin called RUBBIT without a MCFR in a proposal
         ///////
         {
            // Create the proposal
            uop = asset_update_bitasset_operation();
            uop.issuer = assetowner_id;
            uop.asset_to_update = bitrub.get_id();
            uop.new_options = bitrub.bitasset_data(db).options;
            uop.new_options.extensions.value.margin_call_fee_ratio = mcfr_null_opt;

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

         // The MCFR should NOT be set
         BOOST_CHECK(!bitrub.bitasset_data(db).options.extensions.value.margin_call_fee_ratio.valid());


      } FC_LOG_AND_RETHROW()
   }


BOOST_AUTO_TEST_SUITE_END()
