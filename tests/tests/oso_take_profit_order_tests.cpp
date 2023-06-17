/*
 * Copyright (c) 2023 Abit More, and contributors.
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

#include "../common/database_fixture.hpp"

#include <graphene/chain/hardfork.hpp>
#include <graphene/chain/market_object.hpp>
#include <graphene/chain/proposal_object.hpp>

#include <graphene/app/api.hpp>

#include <boost/test/unit_test.hpp>

using namespace graphene::chain;
using namespace graphene::chain::test;

BOOST_FIXTURE_TEST_SUITE( oso_tests, database_fixture )

BOOST_AUTO_TEST_CASE( oso_take_profit_order_hardfork_time_test )
{ try {

      // Proceeds to a recent hard fork
      generate_blocks( HARDFORK_CORE_2362_TIME );
      generate_block();
      set_expiration( db, trx );

      ACTORS((sam));

      auto init_amount = 10000000 * GRAPHENE_BLOCKCHAIN_PRECISION;
      fund( sam, asset(init_amount) );

      const asset_object& usd = create_user_issued_asset( "MYUSD" );
      asset_id_type usd_id = usd.get_id();

      // Before the hard fork, unable to create a limit order with the "on_fill" extension
      // or create with proposals,
      // but can create without on_fill
      create_take_profit_order_action tpa1 { asset_id_type(), 5, GRAPHENE_100_PERCENT, 3600, false };
      vector<limit_order_auto_action> on_fill { tpa1 };

      // With on_fill
      BOOST_CHECK_THROW( create_sell_order( sam_id, asset(1), asset(1, usd_id),
                                            time_point_sec::maximum(), price::unit_price(), on_fill ),
                         fc::exception );
      // Without on_fill
      create_sell_order( sam_id, asset(1), asset(1, usd_id),
                                            time_point_sec::maximum(), price::unit_price(), {} );

      // Proposal with on_fill
      limit_order_create_operation cop1 = make_limit_order_create_op( sam_id, asset(1), asset(1, usd_id),
                                            time_point_sec::maximum(), on_fill );
      BOOST_CHECK_THROW( propose( cop1 ), fc::exception );
      // Proposal without on_fill
      limit_order_create_operation cop2 = make_limit_order_create_op( sam_id, asset(1), asset(1, usd_id),
                                            time_point_sec::maximum(), {} );
      propose( cop2 );

} FC_LOG_AND_RETHROW() }

/// Tests setting up oso with limit_order_create_operation
BOOST_AUTO_TEST_CASE( oso_take_profit_order_setup_test )
{ try {

      // Proceeds to the hard fork
      generate_blocks( HARDFORK_CORE_2535_TIME );
      generate_block();
      set_expiration( db, trx );

      ACTORS((sam));

      auto init_amount = 10000000 * GRAPHENE_BLOCKCHAIN_PRECISION;
      fund( sam, asset(init_amount) );

      const asset_object& usd = create_user_issued_asset( "MYUSD" );
      asset_id_type usd_id = usd.get_id();

      // Spread percentage should be positive
      create_take_profit_order_action tpa1 { asset_id_type(), 0, GRAPHENE_100_PERCENT, 3600, false };
      vector<limit_order_auto_action> on_fill { tpa1 };
      BOOST_CHECK_THROW( create_sell_order( sam_id, asset(1), asset(1, usd_id),
                                            time_point_sec::maximum(), price::unit_price(), on_fill ),
                         fc::exception );
      // Cannot propose either
      limit_order_create_operation cop1 = make_limit_order_create_op( sam_id, asset(1), asset(1, usd_id),
                                            time_point_sec::maximum(), on_fill );
      BOOST_CHECK_THROW( propose( cop1 ), fc::exception );

      // Size percentage should be positive
      tpa1 = { asset_id_type(), 1, 0, 3600, false };
      on_fill = { tpa1 };
      BOOST_CHECK_THROW( create_sell_order( sam_id, asset(1), asset(1, usd_id),
                                            time_point_sec::maximum(), price::unit_price(), on_fill ),
                         fc::exception );
      // Cannot propose either
      cop1 = make_limit_order_create_op( sam_id, asset(1), asset(1, usd_id),
                                            time_point_sec::maximum(), on_fill );
      BOOST_CHECK_THROW( propose( cop1 ), fc::exception );

      // Size percentage should not exceed 100%
      tpa1 = { asset_id_type(), 1, GRAPHENE_100_PERCENT + 1, 3600, false };
      on_fill = { tpa1 };
      BOOST_CHECK_THROW( create_sell_order( sam_id, asset(1), asset(1, usd_id),
                                            time_point_sec::maximum(), price::unit_price(), on_fill ),
                         fc::exception );
      // Cannot propose either
      cop1 = make_limit_order_create_op( sam_id, asset(1), asset(1, usd_id),
                                            time_point_sec::maximum(), on_fill );
      BOOST_CHECK_THROW( propose( cop1 ), fc::exception );

      // Expiration should be positive
      tpa1 = { asset_id_type(), 1, GRAPHENE_100_PERCENT, 0, false };
      on_fill = { tpa1 };
      BOOST_CHECK_THROW( create_sell_order( sam_id, asset(1), asset(1, usd_id),
                                            time_point_sec::maximum(), price::unit_price(), on_fill ),
                         fc::exception );
      // Cannot propose either
      cop1 = make_limit_order_create_op( sam_id, asset(1), asset(1, usd_id),
                                            time_point_sec::maximum(), on_fill );
      BOOST_CHECK_THROW( propose( cop1 ), fc::exception );

      // Fee asset should exist
      tpa1 = { usd_id + 1, 1, GRAPHENE_100_PERCENT, 3600, false };
      on_fill = { tpa1 };
      BOOST_CHECK_THROW( create_sell_order( sam_id, asset(1), asset(1, usd_id),
                                            time_point_sec::maximum(), price::unit_price(), on_fill ),
                         fc::exception );
      // Can propose
      cop1 = make_limit_order_create_op( sam_id, asset(1), asset(1, usd_id),
                                            time_point_sec::maximum(), on_fill );
      propose( cop1 );

      // on_fill must contain only 1 action
      tpa1 = { asset_id_type(), 1, GRAPHENE_100_PERCENT, 3600, false };
      // size == 0
      on_fill = {};
      BOOST_CHECK_THROW( create_sell_order( sam_id, asset(1), asset(1, usd_id),
                                            time_point_sec::maximum(), price::unit_price(), on_fill ),
                         fc::exception );
      // Can propose
      cop1 = make_limit_order_create_op( sam_id, asset(1), asset(1, usd_id),
                                            time_point_sec::maximum(), on_fill );
      propose( cop1 );
      // size > 1
      on_fill = { tpa1, tpa1 };
      BOOST_CHECK_THROW( create_sell_order( sam_id, asset(1), asset(1, usd_id),
                                            time_point_sec::maximum(), price::unit_price(), on_fill ),
                         fc::exception );
      // Can propose
      cop1 = make_limit_order_create_op( sam_id, asset(1), asset(1, usd_id),
                                            time_point_sec::maximum(), on_fill );
      propose( cop1 );

      // A valid operation with on_fill
      tpa1 = { asset_id_type(), 1, GRAPHENE_100_PERCENT, 3600, false };
      on_fill = { tpa1 };
      const limit_order_object* order1 = create_sell_order( sam_id, asset(1), asset(1, usd_id),
                                            time_point_sec::maximum(), price::unit_price(), on_fill );
      // Can propose
      cop1 = make_limit_order_create_op( sam_id, asset(1), asset(1, usd_id),
                                            time_point_sec::maximum(), on_fill );
      propose( cop1 );

      BOOST_REQUIRE( order1 );
      limit_order_id_type order1_id = order1->get_id();

      // Another order without on_fill
      const limit_order_object* order2 = create_sell_order( sam_id, asset(1), asset(1, usd_id),
                                            time_point_sec::maximum(), price::unit_price(), {} );
      BOOST_REQUIRE( order2 );
      limit_order_id_type order2_id = order2->get_id();

      // Final check
      const auto& check_result = [&]()
      {
         BOOST_CHECK( order2_id(db).on_fill.empty() );

         BOOST_REQUIRE_EQUAL( order1_id(db).on_fill.size(), 1U );
         BOOST_REQUIRE( order1_id(db).on_fill.front().is_type<create_take_profit_order_action>() );
         const auto& action = order1_id(db).on_fill.front().get<create_take_profit_order_action>();
         BOOST_CHECK( action.fee_asset_id == tpa1.fee_asset_id );
         BOOST_CHECK( action.spread_percent == tpa1.spread_percent );
         BOOST_CHECK( action.size_percent == tpa1.size_percent );
         BOOST_CHECK( action.expiration_seconds == tpa1.expiration_seconds );
         BOOST_CHECK( action.repeat == tpa1.repeat );
      };

      check_result();

      generate_block();

      check_result();

} FC_LOG_AND_RETHROW() }

/// Tests order-sends-take-profit-order and related order cancellation
BOOST_AUTO_TEST_CASE( oso_take_profit_order_trigger_and_cancel_test )
{ try {

      // Proceeds to the hard fork
      generate_blocks( HARDFORK_CORE_2535_TIME );
      generate_block();
      set_expiration( db, trx );

      ACTORS((sam)(ted));

      additional_asset_options_t usd_options;
      usd_options.value.taker_fee_percent = 80; // 0.8% taker fee

      const asset_object& usd = create_user_issued_asset( "MYUSD", ted, charge_market_fee | white_list,
                                                 price(asset(1, asset_id_type(1)), asset(1)),
                                                 4, 30, usd_options ); // 0.3% maker fee
      asset_id_type usd_id = usd.get_id();
      asset_id_type core_id;

      auto init_amount = 10000000 * GRAPHENE_BLOCKCHAIN_PRECISION;
      fund( sam, asset(init_amount) );
      fund( ted, asset(init_amount) );
      issue_uia( ted, asset(init_amount, usd_id) );

      int64_t expected_balance_sam_core = init_amount;
      int64_t expected_balance_ted_core = init_amount;
      int64_t expected_balance_sam_usd = 0;
      int64_t expected_balance_ted_usd = init_amount;

      const auto& check_balances = [&]() {
         BOOST_CHECK_EQUAL( db.get_balance( sam_id, core_id ).amount.value, expected_balance_sam_core );
         BOOST_CHECK_EQUAL( db.get_balance( ted_id, core_id ).amount.value, expected_balance_ted_core );
         BOOST_CHECK_EQUAL( db.get_balance( sam_id, usd_id ).amount.value, expected_balance_sam_usd );
         BOOST_CHECK_EQUAL( db.get_balance( ted_id, usd_id ).amount.value, expected_balance_ted_usd );
      };

      check_balances();

      // Sam sells CORE for USD with on_fill
      //                                   fee_asset, spread,  size,   expiration, repeat
      create_take_profit_order_action tpa1 { core_id,    100, 10000,         3600, false };
      vector<limit_order_auto_action> on_fill_1 { tpa1 };

      const limit_order_object* sell_order1 = create_sell_order( sam_id, asset(10000), asset(12345, usd_id),
                                            time_point_sec::maximum(), price::unit_price(), on_fill_1 );
      BOOST_REQUIRE( sell_order1 );
      limit_order_id_type sell_order1_id = sell_order1->get_id();

      limit_order_id_type last_order_id = sell_order1_id;

      BOOST_CHECK( !sell_order1_id(db).take_profit_order_id );

      BOOST_REQUIRE_EQUAL( sell_order1_id(db).on_fill.size(), 1U );
      BOOST_REQUIRE( sell_order1_id(db).on_fill.front().is_type<create_take_profit_order_action>() );
      const auto& action_s1 = sell_order1_id(db).on_fill.front().get<create_take_profit_order_action>();
      BOOST_CHECK( action_s1.fee_asset_id == tpa1.fee_asset_id );
      BOOST_CHECK( action_s1.spread_percent == tpa1.spread_percent );
      BOOST_CHECK( action_s1.size_percent == tpa1.size_percent );
      BOOST_CHECK( action_s1.expiration_seconds == tpa1.expiration_seconds );
      BOOST_CHECK( action_s1.repeat == tpa1.repeat );

      expected_balance_sam_core -= 10000;
      check_balances();

      // Ted buys CORE with USD without on_fill, partially fills Sam's order
      const limit_order_object* buy_order1 = create_sell_order( ted_id, asset(1235, usd_id), asset(1000) );
      last_order_id = last_order_id + 1;

      // The buy order is smaller, it gets fully filled
      BOOST_CHECK( !buy_order1 );
      expected_balance_ted_core += 1000;
      expected_balance_ted_usd -= 1235;

      // The newly created take profit order is a buy order
      last_order_id = last_order_id + 1;
      limit_order_id_type buy_order2_id = last_order_id;

      auto buy_order2_expiration = db.head_block_time() + 3600;

      const auto& check_result_1 = [&]()
      {
         // The sell order is partially filled
         BOOST_REQUIRE( db.find(sell_order1_id) );
         // The take profit order
         BOOST_REQUIRE( db.find(buy_order2_id) );
         BOOST_CHECK( buy_order2_id(db).seller == sam_id );
         // The sell order gets 1235, market fee = round_down(1235 * 30 / 10000) = 3
         BOOST_CHECK_EQUAL( buy_order2_id(db).for_sale.value, 1232 ); // 1235 - 3
         // price = (12345 / 10000) / 101% = 12345 / 10100
         // min to receive = round_up( 1232 * 10100 / 12345 ) = 1008
         // updated price = 1232 / 1008
         BOOST_CHECK( buy_order2_id(db).sell_price == asset(1232,usd_id) / asset(1008) );
         BOOST_CHECK( buy_order2_id(db).expiration == buy_order2_expiration );
         BOOST_CHECK( buy_order2_id(db).take_profit_order_id == sell_order1_id );
         BOOST_CHECK( buy_order2_id(db).on_fill.empty() );

         // The sell order is partially filled, pays 1000
         BOOST_CHECK_EQUAL( sell_order1_id(db).for_sale.value, 9000 ); // 10000 - 1000
         BOOST_CHECK( sell_order1_id(db).take_profit_order_id == buy_order2_id );

         check_balances();
      };

      check_result_1();

      generate_block();

      check_result_1();

      // Sam sells more CORE for USD with on_fill
      //                                   fee_asset, spread,  size,   expiration, repeat
      create_take_profit_order_action tpa2 {  usd_id,     70,  9700, uint32_t(-1), true };
      vector<limit_order_auto_action> on_fill_2 { tpa2 };

      const limit_order_object* sell_order2 = create_sell_order( sam_id, asset(10000), asset(13000, usd_id),
                                            time_point_sec::maximum(), price::unit_price(), on_fill_2 );
      last_order_id = last_order_id + 1;

      BOOST_REQUIRE( sell_order2 );
      limit_order_id_type sell_order2_id = sell_order2->get_id();

      BOOST_REQUIRE_EQUAL( sell_order2_id(db).on_fill.size(), 1U );
      BOOST_REQUIRE( sell_order2_id(db).on_fill.front().is_type<create_take_profit_order_action>() );
      const auto& action_s2 = sell_order2_id(db).on_fill.front().get<create_take_profit_order_action>();
      BOOST_CHECK( action_s2.fee_asset_id == tpa2.fee_asset_id );
      BOOST_CHECK( action_s2.spread_percent == tpa2.spread_percent );
      BOOST_CHECK( action_s2.size_percent == tpa2.size_percent );
      BOOST_CHECK( action_s2.expiration_seconds == tpa2.expiration_seconds );
      BOOST_CHECK( action_s2.repeat == tpa2.repeat );

      expected_balance_sam_core -= 10000;
      check_balances();

      // Sam sells yet more CORE for USD with on_fill
      //                                   fee_asset, spread,  size,   expiration, repeat
      create_take_profit_order_action tpa3 {  usd_id,     70,  9970,         3600, true };
      vector<limit_order_auto_action> on_fill_3 { tpa3 };

      const limit_order_object* sell_order3 = create_sell_order( sam_id, asset(10000), asset(34000, usd_id),
                                            db.head_block_time() + 7200, price::unit_price(), on_fill_3 );
      last_order_id = last_order_id + 1;

      BOOST_REQUIRE( sell_order3 );
      limit_order_id_type sell_order3_id = sell_order3->get_id();

      // Data backup
      auto sell_order3_expiration = sell_order3->expiration;

      BOOST_REQUIRE_EQUAL( sell_order3_id(db).on_fill.size(), 1U );
      BOOST_REQUIRE( sell_order3_id(db).on_fill.front().is_type<create_take_profit_order_action>() );
      const auto& action_s3 = sell_order3_id(db).on_fill.front().get<create_take_profit_order_action>();
      BOOST_CHECK( action_s3.fee_asset_id == tpa3.fee_asset_id );
      BOOST_CHECK( action_s3.spread_percent == tpa3.spread_percent );
      BOOST_CHECK( action_s3.size_percent == tpa3.size_percent );
      BOOST_CHECK( action_s3.expiration_seconds == tpa3.expiration_seconds );
      BOOST_CHECK( action_s3.repeat == tpa3.repeat );

      expected_balance_sam_core -= 10000;
      check_balances();

      // Ted buys CORE with USD with on_fill, fills Sam's orders
      //                                   fee_asset, spread,  size,   expiration, repeat
      create_take_profit_order_action tpa4 { core_id,      1,  9999, uint32_t(-1), true };
      vector<limit_order_auto_action> on_fill_4 { tpa4 };

      const limit_order_object* buy_order3 = create_sell_order( ted_id, asset(30000, usd_id), asset(7000),
                                            time_point_sec::maximum(), price::unit_price(), on_fill_4 );
      last_order_id = last_order_id + 1;

      // buy_order3 is fully filled
      BOOST_CHECK( !buy_order3 );

      // The take profit order created by sell_order1 is updated
      auto buy_order2_expiration_new = db.head_block_time() + 3600;

      // The take profit order created by buy_order3 is a sell order
      last_order_id = last_order_id + 1;
      limit_order_id_type sell_order4_id = last_order_id;
      auto sell_order4_expiration = time_point_sec::maximum();

      // The take profit order created by sell_order2 is a buy order
      last_order_id = last_order_id + 1;
      limit_order_id_type buy_order4_id = last_order_id;
      auto buy_order4_expiration = time_point_sec::maximum();

      // The take profit order created by sell_order3 is a buy order
      last_order_id = last_order_id + 1;
      limit_order_id_type buy_order5_id = last_order_id;
      auto buy_order5_expiration = db.head_block_time() + 3600;

      expected_balance_ted_core += 1; // see calculation below
      expected_balance_ted_usd -= (30000 - 1); // buy_order3 refund 1, see calculation below
      expected_balance_sam_usd += (388 + 17); // sell_order2 and sell_order3, see calculation below


      const auto& check_result_2 = [&]()
      {
         // sell_order1 gets fully filled
         BOOST_CHECK( !db.find(sell_order1_id) );

         // The take profit order linked to sell_order1 (buy_order2) is updated
         BOOST_REQUIRE( db.find(buy_order2_id) );
         BOOST_CHECK( buy_order2_id(db).seller == sam_id );
         // sell_order1 pays 9000, gets round_down(9000 * 12345 / 10000) = 11110, market fee = 11110 * 30 / 10000 = 33
         BOOST_CHECK_EQUAL( buy_order2_id(db).for_sale.value, 12309 ); // 1232 + 11110 - 33
         // price = (12345 / 10000) / 101% = 12345 / 10100
         // min to receive = round_up( 12309 * 10100 / 12345 ) = 10071
         // updated price = 12309 / 10071
         BOOST_CHECK( buy_order2_id(db).sell_price == asset(12309,usd_id) / asset(10071) );
         BOOST_CHECK( buy_order2_id(db).expiration == buy_order2_expiration_new );
         BOOST_CHECK( buy_order2_id(db).expiration != buy_order2_expiration );
         BOOST_CHECK( !buy_order2_id(db).take_profit_order_id ); // cleared
         BOOST_CHECK( buy_order2_id(db).on_fill.empty() );

         // buy_order3 pays 11110, gets 9000, remaining for sale = 30000 - 11110 = 18890

         // sell_order2 gets fully filled
         BOOST_CHECK( !db.find(sell_order2_id) );

         // The take profit order created by sell_order2
         BOOST_REQUIRE( db.find(buy_order4_id) );
         BOOST_CHECK( buy_order4_id(db).seller == sam_id );
         // sell_order2 gets 13000, market fee = round_down(13000 * 30 / 10000) = 39
         // gets = 13000 - 39 = 12961
         // take profit order size = round_up(12961 * 9700 / 10000) = 12573
         // Sam USD balance change = 12961 - 12573 = 388
         BOOST_CHECK_EQUAL( buy_order4_id(db).for_sale.value, 12573 );
         // price = (13000 / 10000) / 100.7% = 13000 / 10070
         // min to receive = round_up( 12573 * 10070 / 13000 ) = 9740
         // updated price = 12573 / 9740
         BOOST_CHECK( buy_order4_id(db).sell_price == asset(12573,usd_id) / asset(9740) );
         BOOST_CHECK( buy_order4_id(db).expiration == buy_order4_expiration );
         BOOST_CHECK( !buy_order4_id(db).take_profit_order_id );

         BOOST_REQUIRE_EQUAL( buy_order4_id(db).on_fill.size(), 1U );
         BOOST_REQUIRE( buy_order4_id(db).on_fill.front().is_type<create_take_profit_order_action>() );
         const auto& action_b4 = buy_order4_id(db).on_fill.front().get<create_take_profit_order_action>();
         BOOST_CHECK( action_b4.fee_asset_id == tpa2.fee_asset_id );
         BOOST_CHECK( action_b4.spread_percent == tpa2.spread_percent );
         BOOST_CHECK( action_b4.size_percent == tpa2.size_percent );
         BOOST_CHECK( action_b4.expiration_seconds == tpa2.expiration_seconds );
         BOOST_CHECK( action_b4.repeat == tpa2.repeat );

         // buy_order3 pays 13000, gets 10000, remaining for sale = 18890 - 13000 = 5890

         // sell_order3 gets partially filled
         BOOST_REQUIRE( db.find(sell_order3_id) );
         // The take profit order created by sell_order3
         BOOST_REQUIRE( db.find(buy_order5_id) );
         BOOST_CHECK( buy_order5_id(db).seller == sam_id );
         // sell_order3 gets 5890, pays round_down(5890 * 10000 / 34000) = 1732
         // updated gets = round_up(1732 * 34000 / 10000) = 5889, refund = 5890 - 5889 = 1
         // market fee = round_down(5889 * 30 / 10000) = 17
         // gets = 5889 - 17 = 5872
         // take profit order size = round_up(5872 * 9970 / 10000) = 5855
         // Sam USD balance change = 5872 - 5855 = 17
         BOOST_CHECK_EQUAL( buy_order5_id(db).for_sale.value, 5855 );
         // price = (34000 / 10000) / 100.7% = 34000 / 10070
         // min to receive = round_up( 5855 * 10070 / 34000 ) = 1735
         // updated price = 5855 / 1735 = 3.374639769
         BOOST_CHECK( buy_order5_id(db).sell_price == asset(5855,usd_id) / asset(1735) );
         BOOST_CHECK( buy_order5_id(db).expiration == buy_order5_expiration );
         BOOST_CHECK( buy_order5_id(db).take_profit_order_id == sell_order3_id );

         BOOST_REQUIRE_EQUAL( buy_order5_id(db).on_fill.size(), 1U );
         BOOST_REQUIRE( buy_order5_id(db).on_fill.front().is_type<create_take_profit_order_action>() );
         const auto& action_b5 = buy_order5_id(db).on_fill.front().get<create_take_profit_order_action>();
         BOOST_CHECK( action_b5.fee_asset_id == tpa3.fee_asset_id );
         BOOST_CHECK( action_b5.spread_percent == tpa3.spread_percent );
         BOOST_CHECK( action_b5.size_percent == tpa3.size_percent );
         BOOST_CHECK( action_b5.expiration_seconds == tpa3.expiration_seconds );
         BOOST_CHECK( action_b5.repeat == tpa3.repeat );

         // sell_order3 gets partially filled, pays 1732
         BOOST_CHECK_EQUAL( sell_order3_id(db).for_sale.value, 8268 ); // 10000 - 1732
         BOOST_CHECK( sell_order3_id(db).take_profit_order_id == buy_order5_id );

         // buy_order3 gets 1732, pays 5889, refund 1

         // The take profit order created by buy_order3
         BOOST_REQUIRE( db.find(sell_order4_id) );
         BOOST_CHECK( sell_order4_id(db).seller == ted_id );
         // buy_order3 got in total 9000 + 10000 + 1732 = 20732, market fee = 0
         // take profit order size =
         //   round_up(9000 * 9999 / 10000) + round_up(10000 * 9999 / 10000) + round_up(1732 * 9999 / 10000) = 20731
         // Ted CORE balance change = 20732 - 20731 = 1
         BOOST_CHECK_EQUAL( sell_order4_id(db).for_sale.value, 20731 );
         // price = (7000 / 30000) / 100.01% = 7000 / 30003
         // min to receive = round_up( 20731 * 30003 / 7000 ) = 88857
         // updated price = 20731 / 88857
         BOOST_CHECK( sell_order4_id(db).sell_price == asset(20731) / asset(88857,usd_id) );
         BOOST_CHECK( sell_order4_id(db).expiration == sell_order4_expiration );
         BOOST_CHECK( !sell_order4_id(db).take_profit_order_id );

         BOOST_REQUIRE_EQUAL( sell_order4_id(db).on_fill.size(), 1U );
         BOOST_REQUIRE( sell_order4_id(db).on_fill.front().is_type<create_take_profit_order_action>() );
         const auto& action_s4 = sell_order4_id(db).on_fill.front().get<create_take_profit_order_action>();
         BOOST_CHECK( action_s4.fee_asset_id == tpa4.fee_asset_id );
         BOOST_CHECK( action_s4.spread_percent == tpa4.spread_percent );
         BOOST_CHECK( action_s4.size_percent == tpa4.size_percent );
         BOOST_CHECK( action_s4.expiration_seconds == tpa4.expiration_seconds );
         BOOST_CHECK( action_s4.repeat == tpa4.repeat );

         check_balances();
      };

      check_result_2();

      generate_block();

      check_result_2();

      // Ted sells CORE for USD with on_fill
      //                                   fee_asset, spread,  size,   expiration, repeat
      create_take_profit_order_action tpa5 {  usd_id,  65535,     1,         8800, true };
      vector<limit_order_auto_action> on_fill_5 { tpa5 };

      const limit_order_object* sell_order5 = create_sell_order( ted_id, asset(1), asset(1, usd_id),
                                            db.head_block_time() + 9900, price::unit_price(), on_fill_5 );
      last_order_id = last_order_id + 1;

      // sell_order5 is fully filled
      BOOST_CHECK( !sell_order5 );

      // buy_order5 is partially filled
      // The take profit order linked to buy_order5 (sell_order3) is updated
      auto sell_order3_expiration_new = db.head_block_time() + 3600;

      // The take profit order created by sell_order5 is a buy order
      last_order_id = last_order_id + 1;
      limit_order_id_type buy_order6_id = last_order_id;
      auto buy_order6_expiration = db.head_block_time() + 8800;

      expected_balance_ted_core -= 1; // see calculation below
      expected_balance_ted_usd += 2; // see calculation below

      const auto check_result_3 = [&]()
      {
         // buy_order5 is partially filled
         BOOST_REQUIRE( db.find(buy_order5_id) );
         BOOST_CHECK( buy_order5_id(db).seller == sam_id );
         // buy_order5 gets 1, pays round_down(1 * 5855 / 1735) = 3
         BOOST_CHECK_EQUAL( buy_order5_id(db).for_sale.value, 5852 ); // 5855 - 3
         BOOST_CHECK( buy_order5_id(db).sell_price == asset(5855,usd_id) / asset(1735) ); // unchanged
         BOOST_CHECK( buy_order5_id(db).expiration == buy_order5_expiration ); // unchanged
         BOOST_CHECK( buy_order5_id(db).take_profit_order_id == sell_order3_id ); // unchanged

         // All unchanged
         BOOST_REQUIRE_EQUAL( buy_order5_id(db).on_fill.size(), 1U );
         BOOST_REQUIRE( buy_order5_id(db).on_fill.front().is_type<create_take_profit_order_action>() );
         const auto& action_b5 = buy_order5_id(db).on_fill.front().get<create_take_profit_order_action>();
         BOOST_CHECK( action_b5.fee_asset_id == tpa3.fee_asset_id );
         BOOST_CHECK( action_b5.spread_percent == tpa3.spread_percent );
         BOOST_CHECK( action_b5.size_percent == tpa3.size_percent );
         BOOST_CHECK( action_b5.expiration_seconds == tpa3.expiration_seconds );
         BOOST_CHECK( action_b5.repeat == tpa3.repeat );

         // The take profit order linked to buy_order5 (sell_order3) is updated
         BOOST_REQUIRE( db.find(sell_order3_id) );
         BOOST_CHECK( sell_order3_id(db).seller == sam_id );
         // new amount for sale = round_up(1 * 99.7%) = 1, account balances unchanged
         BOOST_CHECK_EQUAL( sell_order3_id(db).for_sale.value, 8269 ); // 8268 + 1
         BOOST_CHECK( sell_order3_id(db).sell_price == asset(10000) / asset(34000,usd_id) ); // unchanged
         BOOST_CHECK( sell_order3_id(db).expiration == sell_order3_expiration_new );
         BOOST_CHECK( sell_order3_id(db).expiration != sell_order3_expiration );
         BOOST_CHECK( sell_order3_id(db).take_profit_order_id == buy_order5_id ); // unchanged

         // All unchanged
         BOOST_REQUIRE_EQUAL( sell_order3_id(db).on_fill.size(), 1U );
         BOOST_REQUIRE( sell_order3_id(db).on_fill.front().is_type<create_take_profit_order_action>() );
         const auto& action_s3 = sell_order3_id(db).on_fill.front().get<create_take_profit_order_action>();
         BOOST_CHECK( action_s3.fee_asset_id == tpa3.fee_asset_id );
         BOOST_CHECK( action_s3.spread_percent == tpa3.spread_percent );
         BOOST_CHECK( action_s3.size_percent == tpa3.size_percent );
         BOOST_CHECK( action_s3.expiration_seconds == tpa3.expiration_seconds );
         BOOST_CHECK( action_s3.repeat == tpa3.repeat );

         // The take profit order created by sell_order5
         BOOST_REQUIRE( db.find(buy_order6_id) );
         BOOST_CHECK( buy_order6_id(db).seller == ted_id );
         // sell_order5 gets 3, market fee = round_down(3 * 30 / 10000) = 0, still gets 3
         // take profit order size = round_up(3 * 1 / 10000) = 1
         // Ted USD balance change = 3 - 1 = 2
         BOOST_CHECK_EQUAL( buy_order6_id(db).for_sale.value, 1 );
         // price = (1 / 1) / (1 + 655.35%) = 10000 / 75535
         // min to receive = round_up( 1 * 75535 / 10000 ) = 8
         // updated price = 1 / 8
         BOOST_CHECK( buy_order6_id(db).sell_price == asset(1,usd_id) / asset(8) );
         BOOST_CHECK( buy_order6_id(db).expiration == buy_order6_expiration );
         BOOST_CHECK( !buy_order6_id(db).take_profit_order_id );

         BOOST_REQUIRE_EQUAL( buy_order6_id(db).on_fill.size(), 1U );
         BOOST_REQUIRE( buy_order6_id(db).on_fill.front().is_type<create_take_profit_order_action>() );
         const auto& action_b6 = buy_order6_id(db).on_fill.front().get<create_take_profit_order_action>();
         BOOST_CHECK( action_b6.fee_asset_id == tpa5.fee_asset_id );
         BOOST_CHECK( action_b6.spread_percent == tpa5.spread_percent );
         BOOST_CHECK( action_b6.size_percent == tpa5.size_percent );
         BOOST_CHECK( action_b6.expiration_seconds == tpa5.expiration_seconds );
         BOOST_CHECK( action_b6.repeat == tpa5.repeat );

         check_balances();
      };

      check_result_3();

      generate_block();

      check_result_3();

      // Sam places an order to buy CORE with USD with on_fill
      //                                   fee_asset, spread,  size,   expiration, repeat
      create_take_profit_order_action tpa6 { core_id,     10, 10000, uint32_t(-1), true };
      vector<limit_order_auto_action> on_fill_6 { tpa6 };

      const limit_order_object* buy_order7 = create_sell_order( sam_id, asset(338, usd_id), asset(100),
                                            time_point_sec::maximum(), price::unit_price(), on_fill_6 );
      last_order_id = last_order_id + 1;

      BOOST_REQUIRE( buy_order7 );
      limit_order_id_type buy_order7_id = buy_order7->get_id();

      BOOST_CHECK( buy_order7_id(db).seller == sam_id );
      BOOST_CHECK_EQUAL( buy_order7_id(db).for_sale.value, 338 );
      BOOST_CHECK( buy_order7_id(db).sell_price == asset(338,usd_id) / asset(100) );
      BOOST_CHECK( buy_order7_id(db).expiration == time_point_sec::maximum() );
      BOOST_CHECK( !buy_order7_id(db).take_profit_order_id );

      BOOST_REQUIRE_EQUAL( buy_order7_id(db).on_fill.size(), 1U );
      BOOST_REQUIRE( buy_order7_id(db).on_fill.front().is_type<create_take_profit_order_action>() );
      const auto& action_b7 = buy_order7_id(db).on_fill.front().get<create_take_profit_order_action>();
      BOOST_CHECK( action_b7.fee_asset_id == tpa6.fee_asset_id );
      BOOST_CHECK( action_b7.spread_percent == tpa6.spread_percent );
      BOOST_CHECK( action_b7.size_percent == tpa6.size_percent );
      BOOST_CHECK( action_b7.expiration_seconds == tpa6.expiration_seconds );
      BOOST_CHECK( action_b7.repeat == tpa6.repeat );

      expected_balance_sam_usd -= 338;

      check_balances();

      // Make a whitelist, Sam is not in
      {
         BOOST_TEST_MESSAGE( "Setting up whitelisting" );
         asset_update_operation uop;
         uop.asset_to_update = usd_id;
         uop.issuer = usd_id(db).issuer;
         uop.new_options = usd_id(db).options;
         // The whitelist is managed by Ted
         uop.new_options.whitelist_authorities.insert(ted_id);
         trx.operations.clear();
         trx.operations.push_back(uop);
         PUSH_TX( db, trx, ~0 );

         // Upgrade Ted so that he can manage the whitelist
         upgrade_to_lifetime_member( ted_id );

         // Add Ted to the whitelist, but do not add others
         account_whitelist_operation wop;
         wop.authorizing_account = ted_id;
         wop.account_to_list = ted_id;
         wop.new_listing = account_whitelist_operation::white_listed;
         trx.operations.clear();
         trx.operations.push_back(wop);
         PUSH_TX( db, trx, ~0 );
      }

      // Ted sells CORE for USD, fully fills buy_order7, partially fills buy_order5
      const limit_order_object* sell_order7 = create_sell_order( ted_id, asset(200), asset(200, usd_id) );
      last_order_id = last_order_id + 1;

      // sell_order7 is fully filled
      BOOST_CHECK( !sell_order7 );

      expected_balance_sam_core += 200; // See calculation below
      expected_balance_ted_core -= 200; // See calculation below
      expected_balance_ted_usd += 671; // 336 + 335, See calculation below

      const auto check_result_4 = [&]()
      {
         // buy_order7 is fully filled
         BOOST_CHECK( !db.find(buy_order7_id) );
         // buy_order7 gets 100, pays = round_down(100 * 3380 / 1000) = 338,
         // updated gets = round_up( 338 * 1000 / 3380 ) = 100

         // fails to create a take profit order due to whitelisting
         BOOST_CHECK( !db.find(last_order_id+1) );

         // Ted gets 338 USD, market fee = round_down(338 * 0.8%) = 2,
         // updated gets = 338 - 2 = 336

         // buy_order5 is partially filled
         BOOST_REQUIRE( db.find(buy_order5_id) );
         BOOST_CHECK( buy_order5_id(db).seller == sam_id );
         // buy_order5 gets 100, pays round_down(100 * 5855 / 1735) = 337
         // updated gets = round_up(337 * 1735 / 5855) = 100
         BOOST_CHECK_EQUAL( buy_order5_id(db).for_sale.value, 5515 ); // 5852 - 337
         BOOST_CHECK( buy_order5_id(db).sell_price == asset(5855,usd_id) / asset(1735) ); // unchanged
         BOOST_CHECK( buy_order5_id(db).expiration == buy_order5_expiration ); // unchanged
         BOOST_CHECK( buy_order5_id(db).take_profit_order_id == sell_order3_id ); // unchanged

         // All unchanged
         BOOST_REQUIRE_EQUAL( buy_order5_id(db).on_fill.size(), 1U );
         BOOST_REQUIRE( buy_order5_id(db).on_fill.front().is_type<create_take_profit_order_action>() );
         const auto& action_b5 = buy_order5_id(db).on_fill.front().get<create_take_profit_order_action>();
         BOOST_CHECK( action_b5.fee_asset_id == tpa3.fee_asset_id );
         BOOST_CHECK( action_b5.spread_percent == tpa3.spread_percent );
         BOOST_CHECK( action_b5.size_percent == tpa3.size_percent );
         BOOST_CHECK( action_b5.expiration_seconds == tpa3.expiration_seconds );
         BOOST_CHECK( action_b5.repeat == tpa3.repeat );

         // Due to whitelisting, the take profit order linked to buy_order5 (sell_order3) is unchanged
         BOOST_REQUIRE( db.find(sell_order3_id) );
         BOOST_CHECK( sell_order3_id(db).seller == sam_id );
         BOOST_CHECK_EQUAL( sell_order3_id(db).for_sale.value, 8269 ); // unchanged
         BOOST_CHECK( sell_order3_id(db).sell_price == asset(10000) / asset(34000,usd_id) ); // unchanged
         BOOST_CHECK( sell_order3_id(db).expiration == sell_order3_expiration_new );
         BOOST_CHECK( sell_order3_id(db).take_profit_order_id == buy_order5_id ); // unchanged

         // All unchanged
         BOOST_REQUIRE_EQUAL( sell_order3_id(db).on_fill.size(), 1U );
         BOOST_REQUIRE( sell_order3_id(db).on_fill.front().is_type<create_take_profit_order_action>() );
         const auto& action_s3 = sell_order3_id(db).on_fill.front().get<create_take_profit_order_action>();
         BOOST_CHECK( action_s3.fee_asset_id == tpa3.fee_asset_id );
         BOOST_CHECK( action_s3.spread_percent == tpa3.spread_percent );
         BOOST_CHECK( action_s3.size_percent == tpa3.size_percent );
         BOOST_CHECK( action_s3.expiration_seconds == tpa3.expiration_seconds );
         BOOST_CHECK( action_s3.repeat == tpa3.repeat );

         // Ted gets 337 USD, market fee = round_down(337 * 0.8%) = 2,
         // updated gets = 337 - 2 = 335

         check_balances();
      };

      check_result_4();

      generate_block();

      check_result_4();

      const asset_object& eur = create_user_issued_asset("MYEUR");
      asset_id_type eur_id = eur.get_id();

      // Ted buys EUR with USD
      const limit_order_object* buy_eur = create_sell_order( ted_id, asset(200, usd_id), asset(200, eur_id) );
      last_order_id = last_order_id + 1;

      limit_order_id_type buy_eur_id = buy_eur->get_id();

      expected_balance_ted_usd -= 200;

      const auto check_result_5 = [&]()
      {
         // Check that the failed OSO operation does not increase the internal next value of limit_order_id
         BOOST_CHECK( last_order_id == buy_eur_id );

         check_balances();
      };

      check_result_5();

      generate_block();

      check_result_5();

      // Sam cancels an order
      cancel_limit_order( sell_order3_id(db) );

      expected_balance_sam_core += 8269;

      const auto check_result_6 = [&]()
      {
         // buy_order5 is canceled
         BOOST_CHECK( !db.find(sell_order3_id) );

         // The take profit order linked to sell_order3 (buy_order5) is updated
         BOOST_REQUIRE( db.find(buy_order5_id) );
         BOOST_CHECK_EQUAL( buy_order5_id(db).for_sale.value, 5515 ); // unchanged
         BOOST_CHECK( buy_order5_id(db).sell_price == asset(5855,usd_id) / asset(1735) ); // unchanged
         BOOST_CHECK( buy_order5_id(db).expiration == buy_order5_expiration ); // unchanged
         BOOST_CHECK( !buy_order5_id(db).take_profit_order_id ); // cleared

         // Others all unchanged
         BOOST_REQUIRE_EQUAL( buy_order5_id(db).on_fill.size(), 1U );
         BOOST_REQUIRE( buy_order5_id(db).on_fill.front().is_type<create_take_profit_order_action>() );
         const auto& action_b5 = buy_order5_id(db).on_fill.front().get<create_take_profit_order_action>();
         BOOST_CHECK( action_b5.fee_asset_id == tpa3.fee_asset_id );
         BOOST_CHECK( action_b5.spread_percent == tpa3.spread_percent );
         BOOST_CHECK( action_b5.size_percent == tpa3.size_percent );
         BOOST_CHECK( action_b5.expiration_seconds == tpa3.expiration_seconds );
         BOOST_CHECK( action_b5.repeat == tpa3.repeat );

         check_balances();
      };

      check_result_6();

      generate_block();

      check_result_6();

} FC_LOG_AND_RETHROW() }

/// Tests a scenario where a take profit order fails to be sent due to extreme order price
BOOST_AUTO_TEST_CASE( oso_take_profit_order_fail_test_1 )
{ try {

      // Proceeds to the hard fork
      generate_blocks( HARDFORK_CORE_2535_TIME );
      generate_block();
      set_expiration( db, trx );

      ACTORS((sam)(ted));

      const asset_object& usd = create_user_issued_asset( "MYUSD" );
      asset_id_type usd_id = usd.get_id();
      asset_id_type core_id;

      auto init_amount = 10000000 * GRAPHENE_BLOCKCHAIN_PRECISION;
      fund( sam, asset(init_amount) );
      fund( ted, asset(init_amount) );

      issue_uia( ted, asset(GRAPHENE_MAX_SHARE_SUPPLY, usd_id) );

      int64_t expected_balance_sam_core = init_amount;
      int64_t expected_balance_ted_core = init_amount;
      int64_t expected_balance_sam_usd = 0;
      int64_t expected_balance_ted_usd = GRAPHENE_MAX_SHARE_SUPPLY;

      const auto& check_balances = [&]() {
         BOOST_CHECK_EQUAL( db.get_balance( sam_id, core_id ).amount.value, expected_balance_sam_core );
         BOOST_CHECK_EQUAL( db.get_balance( ted_id, core_id ).amount.value, expected_balance_ted_core );
         BOOST_CHECK_EQUAL( db.get_balance( sam_id, usd_id ).amount.value, expected_balance_sam_usd );
         BOOST_CHECK_EQUAL( db.get_balance( ted_id, usd_id ).amount.value, expected_balance_ted_usd );
      };

      check_balances();

      // Ted buys CORE with USD with on_fill
      //                                   fee_asset, spread,  size,   expiration, repeat
      create_take_profit_order_action tpa1 { core_id,    500, 10000,         3600, false };
      vector<limit_order_auto_action> on_fill_1 { tpa1 };

      const limit_order_object* sell_order1 = create_sell_order( ted_id, asset(GRAPHENE_MAX_SHARE_SUPPLY, usd_id),
                                                                 asset(100), time_point_sec::maximum(),
                                                                 price::unit_price(), on_fill_1 );
      BOOST_REQUIRE( sell_order1 );
      limit_order_id_type sell_order1_id = sell_order1->get_id();

      limit_order_id_type last_order_id = sell_order1_id;

      BOOST_CHECK( !sell_order1_id(db).take_profit_order_id );

      BOOST_REQUIRE_EQUAL( sell_order1_id(db).on_fill.size(), 1U );
      BOOST_REQUIRE( sell_order1_id(db).on_fill.front().is_type<create_take_profit_order_action>() );
      const auto& action_s1 = sell_order1_id(db).on_fill.front().get<create_take_profit_order_action>();
      BOOST_CHECK( action_s1.fee_asset_id == tpa1.fee_asset_id );
      BOOST_CHECK( action_s1.spread_percent == tpa1.spread_percent );
      BOOST_CHECK( action_s1.size_percent == tpa1.size_percent );
      BOOST_CHECK( action_s1.expiration_seconds == tpa1.expiration_seconds );
      BOOST_CHECK( action_s1.repeat == tpa1.repeat );

      expected_balance_ted_usd -= GRAPHENE_MAX_SHARE_SUPPLY;
      check_balances();

      // Sam sells CORE for USD without on_fill, fully fills Ted's order
      const limit_order_object* buy_order1 = create_sell_order( sam_id, asset(100),
                                                                asset(GRAPHENE_MAX_SHARE_SUPPLY, usd_id) );
      last_order_id = last_order_id + 1;

      // The buy order gets fully filled
      BOOST_CHECK( !buy_order1 );

      expected_balance_sam_core -= 100;
      expected_balance_sam_usd += GRAPHENE_MAX_SHARE_SUPPLY;

      expected_balance_ted_core += 100;

      const auto& check_result_1 = [&]()
      {
         // The sell order is fully filled
         BOOST_CHECK( !db.find(sell_order1_id) );

         // The take profit order is not created due to an exception
         BOOST_CHECK( !db.find(last_order_id+1) );

         check_balances();
      };

      check_result_1();

      generate_block();

      check_result_1();

      // Sam sells more CORE for USD without on_fill
      const limit_order_object* sell_order2 = create_sell_order( sam_id, asset(10000), asset(13000, usd_id) );
      last_order_id = last_order_id + 1;

      BOOST_REQUIRE( sell_order2 );
      limit_order_id_type sell_order2_id = sell_order2->get_id();

      expected_balance_sam_core -= 10000;

      const auto check_result_2 = [&]()
      {
         // Check that the failed OSO operation does not increase the internal next value of limit_order_id
         BOOST_CHECK( last_order_id == sell_order2_id );

         check_balances();
      };

      check_result_2();

      generate_block();

      check_result_2();

} FC_LOG_AND_RETHROW() }

/// Tests OSO-related order updates: basic operation validation and evaluation
BOOST_AUTO_TEST_CASE( oso_take_profit_order_update_basic_test )
{ try {

      // Proceeds to the hard fork
      generate_blocks( HARDFORK_CORE_2535_TIME );
      generate_block();
      set_expiration( db, trx );

      ACTORS((sam));

      const asset_object& usd = create_user_issued_asset( "MYUSD" );
      asset_id_type usd_id = usd.get_id();
      asset_id_type core_id;

      auto init_amount = 10000000 * GRAPHENE_BLOCKCHAIN_PRECISION;
      fund( sam, asset(init_amount) );

      // Sam sells CORE for USD with on_fill
      //                                   fee_asset, spread,  size,   expiration, repeat
      create_take_profit_order_action tpa1 { core_id,    100, 10000,         3600, false };
      vector<limit_order_auto_action> on_fill_1 { tpa1 };

      const limit_order_object* sell_order1 = create_sell_order( sam_id, asset(10000), asset(12345, usd_id),
                                            time_point_sec::maximum(), price::unit_price(), on_fill_1 );
      BOOST_REQUIRE( sell_order1 );
      limit_order_id_type sell_order1_id = sell_order1->get_id();

      // Sam tries to update a limit order

      // Spread percentage should be positive
      //     fee_asset, spread,  size,   expiration, repeat
      tpa1 = { core_id,      0, 10000,         3600, false };
      on_fill_1 = { tpa1 };
      BOOST_CHECK_THROW( update_limit_order( sell_order1_id, {}, {}, {}, price::unit_price(), on_fill_1 ),
                         fc::exception );
      // Cannot propose either
      limit_order_update_operation uop1 = make_limit_order_update_op( sam_id, sell_order1_id, {}, {}, {}, on_fill_1 );
      BOOST_CHECK_THROW( propose( uop1 ), fc::exception );

      // Size percentage should be positive
      //     fee_asset, spread,  size,   expiration, repeat
      tpa1 = { core_id,      1,     0,         3600, false };
      on_fill_1 = { tpa1 };
      BOOST_CHECK_THROW( update_limit_order( sell_order1_id, {}, {}, {}, price::unit_price(), on_fill_1 ),
                         fc::exception );
      // Cannot propose either
      uop1 = make_limit_order_update_op( sam_id, sell_order1_id, {}, {}, {}, on_fill_1 );
      BOOST_CHECK_THROW( propose( uop1 ), fc::exception );

      // Size percentage should not exceed 100%
      //     fee_asset, spread,  size,   expiration, repeat
      tpa1 = { core_id,      1, 10001,         3600, false };
      on_fill_1 = { tpa1 };
      BOOST_CHECK_THROW( update_limit_order( sell_order1_id, {}, {}, {}, price::unit_price(), on_fill_1 ),
                         fc::exception );
      // Cannot propose either
      uop1 = make_limit_order_update_op( sam_id, sell_order1_id, {}, {}, {}, on_fill_1 );
      BOOST_CHECK_THROW( propose( uop1 ), fc::exception );

      // Expiration should be positive
      //     fee_asset, spread,  size,   expiration, repeat
      tpa1 = { core_id,      1, 10000,            0, false };
      on_fill_1 = { tpa1 };
      BOOST_CHECK_THROW( update_limit_order( sell_order1_id, {}, {}, {}, price::unit_price(), on_fill_1 ),
                         fc::exception );
      // Cannot propose either
      uop1 = make_limit_order_update_op( sam_id, sell_order1_id, {}, {}, {}, on_fill_1 );
      BOOST_CHECK_THROW( propose( uop1 ), fc::exception );

      // Fee asset should exist
      tpa1 = { usd_id + 1, 1, GRAPHENE_100_PERCENT, 3600, false };
      on_fill_1 = { tpa1 };
      BOOST_CHECK_THROW( update_limit_order( sell_order1_id, {}, {}, {}, price::unit_price(), on_fill_1 ),
                         fc::exception );
      // Can propose
      uop1 = make_limit_order_update_op( sam_id, sell_order1_id, {}, {}, {}, on_fill_1 );
      propose( uop1 );

      // on_fill must contain 0 or 1 action
      tpa1 = { core_id, 1, GRAPHENE_100_PERCENT, 3600, false };
      on_fill_1 = { tpa1, tpa1 };
      BOOST_CHECK_THROW( update_limit_order( sell_order1_id, {}, {}, {}, price::unit_price(), on_fill_1 ),
                         fc::exception );
      // Can propose
      uop1 = make_limit_order_update_op( sam_id, sell_order1_id, {}, {}, {}, on_fill_1 );
      propose( uop1 );

      generate_block();

} FC_LOG_AND_RETHROW() }

/// Tests OSO-related order updates, scenarios:
/// * update an order which is not linked to another order and has no on_fill
///   * add on_fill
/// * update an order which is not linked to another order and has on_fill
///   * update on_fill
///   * remove on_fill
BOOST_AUTO_TEST_CASE( oso_take_profit_order_update_test_1 )
{ try {

      // Proceeds to the hard fork
      generate_blocks( HARDFORK_CORE_2535_TIME );
      generate_block();
      set_expiration( db, trx );

      ACTORS((sam));

      const asset_object& usd = create_user_issued_asset( "MYUSD" );
      asset_id_type usd_id = usd.get_id();
      asset_id_type core_id;

      auto init_amount = 10000000 * GRAPHENE_BLOCKCHAIN_PRECISION;
      fund( sam, asset(init_amount) );

      int64_t expected_balance_sam_core = init_amount;
      int64_t expected_balance_sam_usd = 0;

      const auto& check_balances = [&]() {
         BOOST_CHECK_EQUAL( db.get_balance( sam_id, core_id ).amount.value, expected_balance_sam_core );
         BOOST_CHECK_EQUAL( db.get_balance( sam_id, usd_id ).amount.value, expected_balance_sam_usd );
      };

      // Sam sells CORE for USD without on_fill
      const limit_order_object* sell_order1 = create_sell_order( sam_id, asset(10000), asset(12345, usd_id) );
      BOOST_REQUIRE( sell_order1 );
      limit_order_id_type sell_order1_id = sell_order1->get_id();

      BOOST_CHECK( sell_order1_id(db).on_fill.empty() );
      BOOST_CHECK( !sell_order1_id(db).take_profit_order_id );

      expected_balance_sam_core -= 10000;
      check_balances();

      // Sam updates order with on_fill
      //                                   fee_asset, spread,  size,   expiration, repeat
      create_take_profit_order_action tpa1 { core_id,    100, 10000,         3600, false };
      vector<limit_order_auto_action> on_fill_1 { tpa1 };
      update_limit_order( sell_order1_id, {}, {}, {}, price::unit_price(), on_fill_1 );

      const auto& check_result_1 = [&]()
      {
         BOOST_CHECK( !sell_order1_id(db).take_profit_order_id );

         BOOST_REQUIRE_EQUAL( sell_order1_id(db).on_fill.size(), 1U );
         BOOST_REQUIRE( sell_order1_id(db).on_fill.front().is_type<create_take_profit_order_action>() );
         const auto& action_s1 = sell_order1_id(db).on_fill.front().get<create_take_profit_order_action>();
         BOOST_CHECK( action_s1.fee_asset_id == tpa1.fee_asset_id );
         BOOST_CHECK( action_s1.spread_percent == tpa1.spread_percent );
         BOOST_CHECK( action_s1.size_percent == tpa1.size_percent );
         BOOST_CHECK( action_s1.expiration_seconds == tpa1.expiration_seconds );
         BOOST_CHECK( action_s1.repeat == tpa1.repeat );

         check_balances();
      };

      check_result_1();

      generate_block();

      check_result_1();

      // Sam updates order with new on_fill
      //                                   fee_asset, spread,  size,   expiration, repeat
      create_take_profit_order_action tpa2 { usd_id,      10,  1000,         3800, true };
      vector<limit_order_auto_action> on_fill_2 { tpa2 };
      update_limit_order( sell_order1_id, {}, {}, {}, price::unit_price(), on_fill_2 );

      const auto& check_result_2 = [&]()
      {
         BOOST_CHECK( !sell_order1_id(db).take_profit_order_id );

         BOOST_REQUIRE_EQUAL( sell_order1_id(db).on_fill.size(), 1U );
         BOOST_REQUIRE( sell_order1_id(db).on_fill.front().is_type<create_take_profit_order_action>() );
         const auto& action_s1 = sell_order1_id(db).on_fill.front().get<create_take_profit_order_action>();
         BOOST_CHECK( action_s1.fee_asset_id == tpa2.fee_asset_id );
         BOOST_CHECK( action_s1.spread_percent == tpa2.spread_percent );
         BOOST_CHECK( action_s1.size_percent == tpa2.size_percent );
         BOOST_CHECK( action_s1.expiration_seconds == tpa2.expiration_seconds );
         BOOST_CHECK( action_s1.repeat == tpa2.repeat );

         check_balances();
      };

      check_result_2();

      generate_block();

      check_result_2();

      // Sam updates order without on_fill
      update_limit_order( sell_order1_id, {}, asset(1) );
      expected_balance_sam_core -= 1;

      const auto& check_result_3 = [&]()
      {
         BOOST_CHECK( !sell_order1_id(db).take_profit_order_id );

         BOOST_REQUIRE_EQUAL( sell_order1_id(db).on_fill.size(), 1U );
         BOOST_REQUIRE( sell_order1_id(db).on_fill.front().is_type<create_take_profit_order_action>() );
         const auto& action_s1 = sell_order1_id(db).on_fill.front().get<create_take_profit_order_action>();
         BOOST_CHECK( action_s1.fee_asset_id == tpa2.fee_asset_id );
         BOOST_CHECK( action_s1.spread_percent == tpa2.spread_percent );
         BOOST_CHECK( action_s1.size_percent == tpa2.size_percent );
         BOOST_CHECK( action_s1.expiration_seconds == tpa2.expiration_seconds );
         BOOST_CHECK( action_s1.repeat == tpa2.repeat );

         check_balances();
      };

      check_result_3();

      generate_block();

      check_result_3();

      // Sam updates order with an empty on_fill
      vector<limit_order_auto_action> on_fill_3;
      update_limit_order( sell_order1_id, {}, {}, {}, price::unit_price(), on_fill_3 );

      const auto& check_result_4 = [&]()
      {
         BOOST_CHECK( !sell_order1_id(db).take_profit_order_id );

         BOOST_CHECK( sell_order1_id(db).on_fill.empty() );

         check_balances();
      };

      check_result_4();

      generate_block();

      check_result_4();

} FC_LOG_AND_RETHROW() }

/// Tests OSO-related order updates, scenarios:
/// * update an order which is linked to another order but has no on_fill
///   * do not add on_fill, do not specify a new price
///   * do not add on_fill, specify a new price but no change
///   * do not add on_fill, update price
///   * add on_fill
/// * update an order which is linked to another order and has on_fill
///   * do not specify new on_fill, do not specify a new price
///   * do not specify new on_fill, specify a new price but no change
///   * do not specify new on_fill, update price
///   * remove on_fill
///   * update on_fill
///     * do not update spread_percent or repeat
///     * update spread_percent
///     * update repeat
BOOST_AUTO_TEST_CASE( oso_take_profit_order_update_test_2 )
{ try {

      // Proceeds to the hard fork
      generate_blocks( HARDFORK_CORE_2535_TIME );
      generate_block();
      set_expiration( db, trx );

      ACTORS((sam)(ted));

      additional_asset_options_t usd_options;
      usd_options.value.taker_fee_percent = 80; // 0.8% taker fee

      const asset_object& usd = create_user_issued_asset( "MYUSD", ted, charge_market_fee | white_list,
                                                 price(asset(1, asset_id_type(1)), asset(1)),
                                                 4, 30, usd_options ); // 0.3% maker fee
      asset_id_type usd_id = usd.get_id();
      asset_id_type core_id;

      auto init_amount = 10000000 * GRAPHENE_BLOCKCHAIN_PRECISION;
      fund( sam, asset(init_amount) );
      fund( ted, asset(init_amount) );
      issue_uia( ted, asset(init_amount, usd_id) );

      int64_t expected_balance_sam_core = init_amount;
      int64_t expected_balance_ted_core = init_amount;
      int64_t expected_balance_sam_usd = 0;
      int64_t expected_balance_ted_usd = init_amount;

      const auto& check_balances = [&]() {
         BOOST_CHECK_EQUAL( db.get_balance( sam_id, core_id ).amount.value, expected_balance_sam_core );
         BOOST_CHECK_EQUAL( db.get_balance( ted_id, core_id ).amount.value, expected_balance_ted_core );
         BOOST_CHECK_EQUAL( db.get_balance( sam_id, usd_id ).amount.value, expected_balance_sam_usd );
         BOOST_CHECK_EQUAL( db.get_balance( ted_id, usd_id ).amount.value, expected_balance_ted_usd );
      };

      check_balances();

      // Sam sells CORE for USD with on_fill
      //                                   fee_asset, spread,  size,   expiration, repeat
      create_take_profit_order_action tpa1 { core_id,    100, 10000,         3600, false };
      vector<limit_order_auto_action> on_fill_1 { tpa1 };

      const limit_order_object* sell_order1 = create_sell_order( sam_id, asset(10000), asset(12345, usd_id),
                                            time_point_sec::maximum(), price::unit_price(), on_fill_1 );
      BOOST_REQUIRE( sell_order1 );
      limit_order_id_type sell_order1_id = sell_order1->get_id();

      limit_order_id_type last_order_id = sell_order1_id;

      BOOST_CHECK_EQUAL( sell_order1_id(db).for_sale.value, 10000 );
      BOOST_CHECK( sell_order1_id(db).sell_price == asset(10000) / asset(12345, usd_id) );
      BOOST_CHECK( !sell_order1_id(db).take_profit_order_id );

      BOOST_REQUIRE_EQUAL( sell_order1_id(db).on_fill.size(), 1U );
      BOOST_REQUIRE( sell_order1_id(db).on_fill.front().is_type<create_take_profit_order_action>() );
      {
         const auto& action_s1 = sell_order1_id(db).on_fill.front().get<create_take_profit_order_action>();
         BOOST_CHECK( action_s1.fee_asset_id == tpa1.fee_asset_id );
         BOOST_CHECK( action_s1.spread_percent == tpa1.spread_percent );
         BOOST_CHECK( action_s1.size_percent == tpa1.size_percent );
         BOOST_CHECK( action_s1.expiration_seconds == tpa1.expiration_seconds );
         BOOST_CHECK( action_s1.repeat == tpa1.repeat );
      }

      expected_balance_sam_core -= 10000;
      check_balances();

      // Ted buys CORE with USD without on_fill, partially fills Sam's order
      const limit_order_object* buy_order1 = create_sell_order( ted_id, asset(1235, usd_id), asset(1000) );
      last_order_id = last_order_id + 1;

      // The buy order is smaller, it gets fully filled
      BOOST_CHECK( !buy_order1 );
      expected_balance_ted_core += 1000;
      expected_balance_ted_usd -= 1235;

      // The newly created take profit order is a buy order
      last_order_id = last_order_id + 1;
      limit_order_id_type buy_order2_id = last_order_id;

      auto buy_order2_expiration = db.head_block_time() + 3600;

      const auto& check_result_1 = [&]()
      {
         // The sell order is partially filled
         BOOST_REQUIRE( db.find(sell_order1_id) );
         // The take profit order
         BOOST_REQUIRE( db.find(buy_order2_id) );
         BOOST_CHECK( buy_order2_id(db).seller == sam_id );
         // The sell order gets 1235, market fee = round_down(1235 * 30 / 10000) = 3
         BOOST_CHECK_EQUAL( buy_order2_id(db).for_sale.value, 1232 ); // 1235 - 3
         // price = (12345 / 10000) / 101% = 12345 / 10100
         // min to receive = round_up( 1232 * 10100 / 12345 ) = 1008
         // updated price = 1232 / 1008
         BOOST_CHECK( buy_order2_id(db).sell_price == asset(1232,usd_id) / asset(1008) );
         BOOST_CHECK( buy_order2_id(db).expiration == buy_order2_expiration );
         BOOST_CHECK( buy_order2_id(db).take_profit_order_id == sell_order1_id );
         BOOST_CHECK( buy_order2_id(db).on_fill.empty() );

         // The sell order is partially filled, pays 1000
         BOOST_CHECK_EQUAL( sell_order1_id(db).for_sale.value, 9000 ); // 10000 - 1000
         BOOST_CHECK( sell_order1_id(db).sell_price == asset(10000) / asset(12345, usd_id) );
         BOOST_CHECK( sell_order1_id(db).take_profit_order_id == buy_order2_id );

         check_balances();
      };

      check_result_1();

      generate_block();

      check_result_1();

      // Several passes to test different scenarios
      auto bak_balance_sam_core = expected_balance_sam_core;
      auto bak_balance_sam_usd = expected_balance_sam_usd;
      for( size_t i = 0; i <= 10; ++i )
      {
         // Sam updates order
         create_take_profit_order_action tpa2 = tpa1;
         if( 0 == i )
         {
            // no on_fill, do not add on_fill, do not specify a new price
            update_limit_order( buy_order2_id, {}, asset(-1, usd_id) );
            expected_balance_sam_usd += 1;
         }
         else if( 1 == i )
         {
            // no on_fill, do not add on_fill, specify a new price but no change
            update_limit_order( buy_order2_id, buy_order2_id(db).sell_price, {}, time_point_sec::maximum() );
         }
         else if( 2 == i )
         {
            // no on_fill, do not add on_fill, update price
            auto new_price = buy_order2_id(db).sell_price;
            new_price.quote.amount += 1;
            update_limit_order( buy_order2_id, new_price );
         }
         else if( 3 == i )
         {
            // no on_fill, add on_fill
            update_limit_order( buy_order2_id, {}, {}, {}, price::unit_price(), on_fill_1 );
         }
         else if( 4 == i )
         {
            // has on_fill, do not specify new on_fill, do not specify a new price
            update_limit_order( sell_order1_id, {}, asset(1) );
            expected_balance_sam_core -= 1;
         }
         else if( 5 == i )
         {
            // has on_fill, do not specify new on_fill, specify a new price but no change
            update_limit_order( sell_order1_id, sell_order1_id(db).sell_price, asset(1) );
            expected_balance_sam_core -= 1;
         }
         else if( 6 == i )
         {
            // has on_fill, do not specify new on_fill, update price
            auto new_price = sell_order1_id(db).sell_price;
            new_price.quote.amount += 1;
            update_limit_order( sell_order1_id, new_price );
         }
         else if( 7 == i )
         {
            // has on_fill, specify an empty new on_fill (to remove it)
            vector<limit_order_auto_action> on_fill_2;
            update_limit_order( sell_order1_id, {}, {}, {}, price::unit_price(), on_fill_2 );
         }
         else if( 8 == i )
         {
            // has on_fill, specify a new on_fill, but no update to spread_percent or repeat
            //     fee_asset, spread,  size,   expiration, repeat
            tpa2 = {  usd_id,    100,  9000,         7200, false };
            vector<limit_order_auto_action> on_fill_2 { tpa2 };
            update_limit_order( sell_order1_id, {}, {}, {}, price::unit_price(), on_fill_2 );
         }
         else if( 9 == i )
         {
            // has on_fill, specify a new on_fill, update spread_percent
            //     fee_asset, spread,  size,   expiration, repeat
            tpa2 = { core_id,    101, 10000,         3600, false };
            vector<limit_order_auto_action> on_fill_2 { tpa2 };
            update_limit_order( sell_order1_id, {}, {}, {}, price::unit_price(), on_fill_2 );
         }
         else if( 10 == i )
         {
            // has on_fill, specify a new on_fill, update repeat
            //     fee_asset, spread,  size,   expiration, repeat
            tpa2 = { core_id,    100, 10000,         3600, true  };
            vector<limit_order_auto_action> on_fill_2 { tpa2 };
            update_limit_order( sell_order1_id, {}, {}, {}, price::unit_price(), on_fill_2 );
         }

         const auto& check_result_2 = [&]()
         {
            if( 0 == i )
            {
               // The sell order
               BOOST_REQUIRE( db.find(sell_order1_id) );
               BOOST_CHECK_EQUAL( sell_order1_id(db).for_sale.value, 9000 );
               BOOST_CHECK( sell_order1_id(db).sell_price == asset(10000) / asset(12345, usd_id) );
               BOOST_CHECK( sell_order1_id(db).take_profit_order_id == buy_order2_id );

               BOOST_REQUIRE_EQUAL( sell_order1_id(db).on_fill.size(), 1U );
               BOOST_REQUIRE( sell_order1_id(db).on_fill.front().is_type<create_take_profit_order_action>() );
               const auto& action_s1 = sell_order1_id(db).on_fill.front().get<create_take_profit_order_action>();
               BOOST_CHECK( action_s1.fee_asset_id == tpa1.fee_asset_id );
               BOOST_CHECK( action_s1.spread_percent == tpa1.spread_percent );
               BOOST_CHECK( action_s1.size_percent == tpa1.size_percent );
               BOOST_CHECK( action_s1.expiration_seconds == tpa1.expiration_seconds );
               BOOST_CHECK( action_s1.repeat == tpa1.repeat );

               // The take profit order
               BOOST_REQUIRE( db.find(buy_order2_id) );
               BOOST_CHECK( buy_order2_id(db).seller == sam_id );
               BOOST_CHECK_EQUAL( buy_order2_id(db).for_sale.value, 1231 ); // updated: 1232 - 1
               BOOST_CHECK( buy_order2_id(db).sell_price == asset(1232,usd_id) / asset(1008) );
               BOOST_CHECK( buy_order2_id(db).expiration == buy_order2_expiration );
               BOOST_CHECK( buy_order2_id(db).take_profit_order_id == sell_order1_id );
               BOOST_CHECK( buy_order2_id(db).on_fill.empty() );
            }
            else if( 1 == i )
            {
               // The sell order
               BOOST_REQUIRE( db.find(sell_order1_id) );
               BOOST_CHECK_EQUAL( sell_order1_id(db).for_sale.value, 9000 );
               BOOST_CHECK( sell_order1_id(db).sell_price == asset(10000) / asset(12345, usd_id) );
               BOOST_CHECK( sell_order1_id(db).take_profit_order_id == buy_order2_id );

               BOOST_REQUIRE_EQUAL( sell_order1_id(db).on_fill.size(), 1U );
               BOOST_REQUIRE( sell_order1_id(db).on_fill.front().is_type<create_take_profit_order_action>() );
               const auto& action_s1 = sell_order1_id(db).on_fill.front().get<create_take_profit_order_action>();
               BOOST_CHECK( action_s1.fee_asset_id == tpa1.fee_asset_id );
               BOOST_CHECK( action_s1.spread_percent == tpa1.spread_percent );
               BOOST_CHECK( action_s1.size_percent == tpa1.size_percent );
               BOOST_CHECK( action_s1.expiration_seconds == tpa1.expiration_seconds );
               BOOST_CHECK( action_s1.repeat == tpa1.repeat );

               // The take profit order
               BOOST_REQUIRE( db.find(buy_order2_id) );
               BOOST_CHECK( buy_order2_id(db).seller == sam_id );
               BOOST_CHECK_EQUAL( buy_order2_id(db).for_sale.value, 1232 );
               BOOST_CHECK( buy_order2_id(db).sell_price == asset(1232,usd_id) / asset(1008) );
               BOOST_CHECK( buy_order2_id(db).expiration == time_point_sec::maximum() ); // updated
               BOOST_CHECK( buy_order2_id(db).take_profit_order_id == sell_order1_id );
               BOOST_CHECK( buy_order2_id(db).on_fill.empty() );
            }
            else if( 2 == i )
            {
               // The sell order
               BOOST_REQUIRE( db.find(sell_order1_id) );
               BOOST_CHECK_EQUAL( sell_order1_id(db).for_sale.value, 9000 );
               BOOST_CHECK( sell_order1_id(db).sell_price == asset(10000) / asset(12345, usd_id) );
               BOOST_CHECK( !sell_order1_id(db).take_profit_order_id ); // cleared

               BOOST_REQUIRE_EQUAL( sell_order1_id(db).on_fill.size(), 1U );
               BOOST_REQUIRE( sell_order1_id(db).on_fill.front().is_type<create_take_profit_order_action>() );
               const auto& action_s1 = sell_order1_id(db).on_fill.front().get<create_take_profit_order_action>();
               BOOST_CHECK( action_s1.fee_asset_id == tpa1.fee_asset_id );
               BOOST_CHECK( action_s1.spread_percent == tpa1.spread_percent );
               BOOST_CHECK( action_s1.size_percent == tpa1.size_percent );
               BOOST_CHECK( action_s1.expiration_seconds == tpa1.expiration_seconds );
               BOOST_CHECK( action_s1.repeat == tpa1.repeat );

               // The take profit order
               BOOST_REQUIRE( db.find(buy_order2_id) );
               BOOST_CHECK( buy_order2_id(db).seller == sam_id );
               BOOST_CHECK_EQUAL( buy_order2_id(db).for_sale.value, 1232 );
               BOOST_CHECK( buy_order2_id(db).sell_price == asset(1232,usd_id) / asset(1009) ); // updated
               BOOST_CHECK( buy_order2_id(db).expiration == buy_order2_expiration );
               BOOST_CHECK( !buy_order2_id(db).take_profit_order_id ); // cleared
               BOOST_CHECK( buy_order2_id(db).on_fill.empty() );
            }
            else if( 3 == i )
            {
               // The sell order
               BOOST_REQUIRE( db.find(sell_order1_id) );
               BOOST_CHECK_EQUAL( sell_order1_id(db).for_sale.value, 9000 );
               BOOST_CHECK( sell_order1_id(db).sell_price == asset(10000) / asset(12345, usd_id) );
               BOOST_CHECK( !sell_order1_id(db).take_profit_order_id ); // cleared

               BOOST_REQUIRE_EQUAL( sell_order1_id(db).on_fill.size(), 1U );
               BOOST_REQUIRE( sell_order1_id(db).on_fill.front().is_type<create_take_profit_order_action>() );
               const auto& action_s1 = sell_order1_id(db).on_fill.front().get<create_take_profit_order_action>();
               BOOST_CHECK( action_s1.fee_asset_id == tpa1.fee_asset_id );
               BOOST_CHECK( action_s1.spread_percent == tpa1.spread_percent );
               BOOST_CHECK( action_s1.size_percent == tpa1.size_percent );
               BOOST_CHECK( action_s1.expiration_seconds == tpa1.expiration_seconds );
               BOOST_CHECK( action_s1.repeat == tpa1.repeat );

               // The take profit order
               BOOST_REQUIRE( db.find(buy_order2_id) );
               BOOST_CHECK( buy_order2_id(db).seller == sam_id );
               BOOST_CHECK_EQUAL( buy_order2_id(db).for_sale.value, 1232 );
               BOOST_CHECK( buy_order2_id(db).sell_price == asset(1232,usd_id) / asset(1008) );
               BOOST_CHECK( buy_order2_id(db).expiration == buy_order2_expiration );
               BOOST_CHECK( !buy_order2_id(db).take_profit_order_id ); // cleared

               BOOST_REQUIRE_EQUAL( buy_order2_id(db).on_fill.size(), 1U ); // updated
               BOOST_REQUIRE( buy_order2_id(db).on_fill.front().is_type<create_take_profit_order_action>() );
               const auto& action_b2 = buy_order2_id(db).on_fill.front().get<create_take_profit_order_action>();
               BOOST_CHECK( action_b2.fee_asset_id == tpa1.fee_asset_id );
               BOOST_CHECK( action_b2.spread_percent == tpa1.spread_percent );
               BOOST_CHECK( action_b2.size_percent == tpa1.size_percent );
               BOOST_CHECK( action_b2.expiration_seconds == tpa1.expiration_seconds );
               BOOST_CHECK( action_b2.repeat == tpa1.repeat );
            }
            else if( 4 == i )
            {
               // The sell order
               BOOST_REQUIRE( db.find(sell_order1_id) );
               BOOST_CHECK_EQUAL( sell_order1_id(db).for_sale.value, 9001 ); // updated: 9000 + 1
               BOOST_CHECK( sell_order1_id(db).sell_price == asset(10000) / asset(12345, usd_id) );
               BOOST_CHECK( sell_order1_id(db).take_profit_order_id == buy_order2_id );

               BOOST_REQUIRE_EQUAL( sell_order1_id(db).on_fill.size(), 1U );
               BOOST_REQUIRE( sell_order1_id(db).on_fill.front().is_type<create_take_profit_order_action>() );
               const auto& action_s1 = sell_order1_id(db).on_fill.front().get<create_take_profit_order_action>();
               BOOST_CHECK( action_s1.fee_asset_id == tpa1.fee_asset_id );
               BOOST_CHECK( action_s1.spread_percent == tpa1.spread_percent );
               BOOST_CHECK( action_s1.size_percent == tpa1.size_percent );
               BOOST_CHECK( action_s1.expiration_seconds == tpa1.expiration_seconds );
               BOOST_CHECK( action_s1.repeat == tpa1.repeat );

               // The take profit order
               BOOST_REQUIRE( db.find(buy_order2_id) );
               BOOST_CHECK( buy_order2_id(db).seller == sam_id );
               BOOST_CHECK_EQUAL( buy_order2_id(db).for_sale.value, 1232 );
               BOOST_CHECK( buy_order2_id(db).sell_price == asset(1232,usd_id) / asset(1008) );
               BOOST_CHECK( buy_order2_id(db).expiration == buy_order2_expiration );
               BOOST_CHECK( buy_order2_id(db).take_profit_order_id == sell_order1_id );
               BOOST_CHECK( buy_order2_id(db).on_fill.empty() );
            }
            else if( 5 == i )
            {
               // The sell order
               BOOST_REQUIRE( db.find(sell_order1_id) );
               BOOST_CHECK_EQUAL( sell_order1_id(db).for_sale.value, 9001 ); // updated: 9000 + 1
               BOOST_CHECK( sell_order1_id(db).sell_price == asset(10000) / asset(12345, usd_id) );
               BOOST_CHECK( sell_order1_id(db).take_profit_order_id == buy_order2_id );

               BOOST_REQUIRE_EQUAL( sell_order1_id(db).on_fill.size(), 1U );
               BOOST_REQUIRE( sell_order1_id(db).on_fill.front().is_type<create_take_profit_order_action>() );
               const auto& action_s1 = sell_order1_id(db).on_fill.front().get<create_take_profit_order_action>();
               BOOST_CHECK( action_s1.fee_asset_id == tpa1.fee_asset_id );
               BOOST_CHECK( action_s1.spread_percent == tpa1.spread_percent );
               BOOST_CHECK( action_s1.size_percent == tpa1.size_percent );
               BOOST_CHECK( action_s1.expiration_seconds == tpa1.expiration_seconds );
               BOOST_CHECK( action_s1.repeat == tpa1.repeat );

               // The take profit order
               BOOST_REQUIRE( db.find(buy_order2_id) );
               BOOST_CHECK( buy_order2_id(db).seller == sam_id );
               BOOST_CHECK_EQUAL( buy_order2_id(db).for_sale.value, 1232 );
               BOOST_CHECK( buy_order2_id(db).sell_price == asset(1232,usd_id) / asset(1008) );
               BOOST_CHECK( buy_order2_id(db).expiration == buy_order2_expiration );
               BOOST_CHECK( buy_order2_id(db).take_profit_order_id == sell_order1_id );
               BOOST_CHECK( buy_order2_id(db).on_fill.empty() );
            }
            else if( 6 == i )
            {
               // The sell order
               BOOST_REQUIRE( db.find(sell_order1_id) );
               BOOST_CHECK_EQUAL( sell_order1_id(db).for_sale.value, 9000 );
               BOOST_CHECK( sell_order1_id(db).sell_price == asset(10000) / asset(12346, usd_id) ); // updated
               BOOST_CHECK( !sell_order1_id(db).take_profit_order_id ); // cleared

               BOOST_REQUIRE_EQUAL( sell_order1_id(db).on_fill.size(), 1U );
               BOOST_REQUIRE( sell_order1_id(db).on_fill.front().is_type<create_take_profit_order_action>() );
               const auto& action_s1 = sell_order1_id(db).on_fill.front().get<create_take_profit_order_action>();
               BOOST_CHECK( action_s1.fee_asset_id == tpa1.fee_asset_id );
               BOOST_CHECK( action_s1.spread_percent == tpa1.spread_percent );
               BOOST_CHECK( action_s1.size_percent == tpa1.size_percent );
               BOOST_CHECK( action_s1.expiration_seconds == tpa1.expiration_seconds );
               BOOST_CHECK( action_s1.repeat == tpa1.repeat );

               // The take profit order
               BOOST_REQUIRE( db.find(buy_order2_id) );
               BOOST_CHECK( buy_order2_id(db).seller == sam_id );
               BOOST_CHECK_EQUAL( buy_order2_id(db).for_sale.value, 1232 );
               BOOST_CHECK( buy_order2_id(db).sell_price == asset(1232,usd_id) / asset(1008) );
               BOOST_CHECK( buy_order2_id(db).expiration == buy_order2_expiration );
               BOOST_CHECK( !buy_order2_id(db).take_profit_order_id ); // cleared
               BOOST_CHECK( buy_order2_id(db).on_fill.empty() );
            }
            else if( 7 == i )
            {
               // The sell order
               BOOST_REQUIRE( db.find(sell_order1_id) );
               BOOST_CHECK_EQUAL( sell_order1_id(db).for_sale.value, 9000 );
               BOOST_CHECK( sell_order1_id(db).sell_price == asset(10000) / asset(12345, usd_id) );
               BOOST_CHECK( !sell_order1_id(db).take_profit_order_id ); // cleared

               BOOST_CHECK( sell_order1_id(db).on_fill.empty() ); // removed

               // The take profit order
               BOOST_REQUIRE( db.find(buy_order2_id) );
               BOOST_CHECK( buy_order2_id(db).seller == sam_id );
               BOOST_CHECK_EQUAL( buy_order2_id(db).for_sale.value, 1232 );
               BOOST_CHECK( buy_order2_id(db).sell_price == asset(1232,usd_id) / asset(1008) );
               BOOST_CHECK( buy_order2_id(db).expiration == buy_order2_expiration );
               BOOST_CHECK( !buy_order2_id(db).take_profit_order_id ); // cleared
               BOOST_CHECK( buy_order2_id(db).on_fill.empty() );
            }
            else if( 8 == i )
            {
               // The sell order
               BOOST_REQUIRE( db.find(sell_order1_id) );
               BOOST_CHECK_EQUAL( sell_order1_id(db).for_sale.value, 9000 );
               BOOST_CHECK( sell_order1_id(db).sell_price == asset(10000) / asset(12345, usd_id) );
               BOOST_CHECK( sell_order1_id(db).take_profit_order_id == buy_order2_id );

               BOOST_REQUIRE_EQUAL( sell_order1_id(db).on_fill.size(), 1U );
               BOOST_REQUIRE( sell_order1_id(db).on_fill.front().is_type<create_take_profit_order_action>() );
               const auto& action_s1 = sell_order1_id(db).on_fill.front().get<create_take_profit_order_action>();
               BOOST_CHECK( action_s1.fee_asset_id == tpa2.fee_asset_id ); // updated
               BOOST_CHECK( action_s1.spread_percent == tpa2.spread_percent );
               BOOST_CHECK( action_s1.size_percent == tpa2.size_percent ); // updated
               BOOST_CHECK( action_s1.expiration_seconds == tpa2.expiration_seconds ); // updated
               BOOST_CHECK( action_s1.repeat == tpa2.repeat );

               // The take profit order
               BOOST_REQUIRE( db.find(buy_order2_id) );
               BOOST_CHECK( buy_order2_id(db).seller == sam_id );
               BOOST_CHECK_EQUAL( buy_order2_id(db).for_sale.value, 1232 );
               BOOST_CHECK( buy_order2_id(db).sell_price == asset(1232,usd_id) / asset(1008) );
               BOOST_CHECK( buy_order2_id(db).expiration == buy_order2_expiration );
               BOOST_CHECK( buy_order2_id(db).take_profit_order_id == sell_order1_id );
               BOOST_CHECK( buy_order2_id(db).on_fill.empty() );
            }
            else if( 9 == i )
            {
               // The sell order
               BOOST_REQUIRE( db.find(sell_order1_id) );
               BOOST_CHECK_EQUAL( sell_order1_id(db).for_sale.value, 9000 );
               BOOST_CHECK( sell_order1_id(db).sell_price == asset(10000) / asset(12345, usd_id) );
               BOOST_CHECK( !sell_order1_id(db).take_profit_order_id ); // cleared

               BOOST_REQUIRE_EQUAL( sell_order1_id(db).on_fill.size(), 1U );
               BOOST_REQUIRE( sell_order1_id(db).on_fill.front().is_type<create_take_profit_order_action>() );
               const auto& action_s1 = sell_order1_id(db).on_fill.front().get<create_take_profit_order_action>();
               BOOST_CHECK( action_s1.fee_asset_id == tpa2.fee_asset_id );
               BOOST_CHECK( action_s1.spread_percent == tpa2.spread_percent ); // updated
               BOOST_CHECK( action_s1.size_percent == tpa2.size_percent );
               BOOST_CHECK( action_s1.expiration_seconds == tpa2.expiration_seconds );
               BOOST_CHECK( action_s1.repeat == tpa2.repeat );

               // The take profit order
               BOOST_REQUIRE( db.find(buy_order2_id) );
               BOOST_CHECK( buy_order2_id(db).seller == sam_id );
               BOOST_CHECK_EQUAL( buy_order2_id(db).for_sale.value, 1232 );
               BOOST_CHECK( buy_order2_id(db).sell_price == asset(1232,usd_id) / asset(1008) );
               BOOST_CHECK( buy_order2_id(db).expiration == buy_order2_expiration );
               BOOST_CHECK( !buy_order2_id(db).take_profit_order_id ); // cleared
               BOOST_CHECK( buy_order2_id(db).on_fill.empty() );
            }
            else if( 10 == i )
            {
               // The sell order
               BOOST_REQUIRE( db.find(sell_order1_id) );
               BOOST_CHECK_EQUAL( sell_order1_id(db).for_sale.value, 9000 );
               BOOST_CHECK( sell_order1_id(db).sell_price == asset(10000) / asset(12345, usd_id) );
               BOOST_CHECK( !sell_order1_id(db).take_profit_order_id ); // cleared

               BOOST_REQUIRE_EQUAL( sell_order1_id(db).on_fill.size(), 1U );
               BOOST_REQUIRE( sell_order1_id(db).on_fill.front().is_type<create_take_profit_order_action>() );
               const auto& action_s1 = sell_order1_id(db).on_fill.front().get<create_take_profit_order_action>();
               BOOST_CHECK( action_s1.fee_asset_id == tpa2.fee_asset_id );
               BOOST_CHECK( action_s1.spread_percent == tpa2.spread_percent );
               BOOST_CHECK( action_s1.size_percent == tpa2.size_percent );
               BOOST_CHECK( action_s1.expiration_seconds == tpa2.expiration_seconds );
               BOOST_CHECK( action_s1.repeat == tpa2.repeat ); // updated

               // The take profit order
               BOOST_REQUIRE( db.find(buy_order2_id) );
               BOOST_CHECK( buy_order2_id(db).seller == sam_id );
               BOOST_CHECK_EQUAL( buy_order2_id(db).for_sale.value, 1232 );
               BOOST_CHECK( buy_order2_id(db).sell_price == asset(1232,usd_id) / asset(1008) );
               BOOST_CHECK( buy_order2_id(db).expiration == buy_order2_expiration );
               BOOST_CHECK( !buy_order2_id(db).take_profit_order_id ); // cleared
               BOOST_CHECK( buy_order2_id(db).on_fill.empty() );
            }

            check_balances();

         };

         check_result_2();

         generate_block();

         check_result_2();

         // reset
         db.pop_block();
         expected_balance_sam_core = bak_balance_sam_core;
         expected_balance_sam_usd = bak_balance_sam_usd;
      }

} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_SUITE_END()
