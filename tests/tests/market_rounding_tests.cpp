/*
 * Copyright (c) 2018 Abit More, and other contributors.
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

#include <graphene/chain/market_object.hpp>

#include "../common/database_fixture.hpp"

using namespace graphene::chain;
using namespace graphene::chain::test;

BOOST_FIXTURE_TEST_SUITE(market_rounding_tests, database_fixture)

/***
 * Reproduces bitshares-core issue #132: something for nothing when maching a limit order with a call order.
 * Also detects the cull_small issue in check_call_orders.
 */
BOOST_AUTO_TEST_CASE( issue_132_limit_and_call_test1 )
{ try { // matching a limit order with call order
   generate_block();

   set_expiration( db, trx );

   ACTORS((buyer)(seller)(borrower)(borrower2)(borrower3)(borrower4)(feedproducer));

   const auto& bitusd = create_bitasset("USDBIT", feedproducer_id);
   const auto& core   = asset_id_type()(db);

   int64_t init_balance(1000000);

   transfer(committee_account, buyer_id, asset(init_balance));
   transfer(committee_account, borrower_id, asset(init_balance));
   transfer(committee_account, borrower2_id, asset(init_balance));
   transfer(committee_account, borrower3_id, asset(init_balance));
   transfer(committee_account, borrower4_id, asset(init_balance));
   update_feed_producers( bitusd, {feedproducer.id} );

   price_feed current_feed;
   current_feed.maintenance_collateral_ratio = 1750;
   current_feed.maximum_short_squeeze_ratio = 1100;
   current_feed.settlement_price = bitusd.amount( 100 ) / core.amount( 5 );
   publish_feed( bitusd, feedproducer, current_feed );
   // start out with 200% collateral, call price is 10/175 CORE/USD = 40/700
   const call_order_object& call = *borrow( borrower, bitusd.amount(10), asset(1));
   call_order_id_type call_id = call.id;
   // create another position with 310% collateral, call price is 15.5/175 CORE/USD = 62/700
   const call_order_object& call2 = *borrow( borrower2, bitusd.amount(100000), asset(15500));
   // create yet another position with 350% collateral, call price is 17.5/175 CORE/USD = 77/700
   const call_order_object& call3 = *borrow( borrower3, bitusd.amount(100000), asset(17500));
   transfer(borrower, seller, bitusd.amount(10));
   transfer(borrower2, seller, bitusd.amount(100000));
   transfer(borrower3, seller, bitusd.amount(100000));

   BOOST_CHECK_EQUAL( 10, call.debt.value );
   BOOST_CHECK_EQUAL( 1, call.collateral.value );
   BOOST_CHECK_EQUAL( 100000, call2.debt.value );
   BOOST_CHECK_EQUAL( 15500, call2.collateral.value );
   BOOST_CHECK_EQUAL( 100000, call3.debt.value );
   BOOST_CHECK_EQUAL( 17500, call3.collateral.value );

   BOOST_CHECK_EQUAL( 200010, get_balance(seller, bitusd) );
   BOOST_CHECK_EQUAL( 0, get_balance(seller, core) );
   BOOST_CHECK_EQUAL( 0, get_balance(borrower, bitusd) );
   BOOST_CHECK_EQUAL( init_balance-1, get_balance(borrower, core) );

   // adjust price feed to get call_order into margin call territory
   current_feed.settlement_price = bitusd.amount( 120 ) / core.amount(10);
   publish_feed( bitusd, feedproducer, current_feed );
   // settlement price = 120 USD / 10 CORE, mssp = 120/11 USD/CORE

   // This would match with call at price 11 USD / 1 CORE, but call only owes 10 USD,
   //   so the seller will pay 10 USD but get nothing.
   // The remaining 1 USD is too little to get any CORE, so the limit order will be cancelled
   BOOST_CHECK( !create_sell_order(seller, bitusd.amount(11), core.amount(1)) );
   BOOST_CHECK( !db.find<call_order_object>( call_id ) ); // the first call order get filled
   BOOST_CHECK_EQUAL( 200000, get_balance(seller, bitusd) ); // the seller paid 10 USD
   BOOST_CHECK_EQUAL( 0, get_balance(seller, core) ); // the seller got nothing
   BOOST_CHECK_EQUAL( 0, get_balance(borrower, bitusd) );
   BOOST_CHECK_EQUAL( init_balance, get_balance(borrower, core) );

   generate_block();

} FC_LOG_AND_RETHROW() }

/***
 * Another test case
 * reproduces bitshares-core issue #132: something for nothing when maching a limit order with a call order.
 * Also detects the cull_small issue in check_call_orders.
 */
BOOST_AUTO_TEST_CASE( issue_132_limit_and_call_test2 )
{ try {
   generate_block();

   set_expiration( db, trx );

   ACTORS((buyer)(seller)(borrower)(borrower2)(borrower3)(borrower4)(feedproducer));

   const auto& bitusd = create_bitasset("USDBIT", feedproducer_id);
   const auto& core   = asset_id_type()(db);
   asset_id_type usd_id = bitusd.id;
   asset_id_type core_id = core.id;

   int64_t init_balance(1000000);

   transfer(committee_account, buyer_id, asset(init_balance));
   transfer(committee_account, borrower_id, asset(init_balance));
   transfer(committee_account, borrower2_id, asset(init_balance));
   transfer(committee_account, borrower3_id, asset(init_balance));
   transfer(committee_account, borrower4_id, asset(init_balance));
   update_feed_producers( bitusd, {feedproducer.id} );

   price_feed current_feed;
   current_feed.maintenance_collateral_ratio = 1750;
   current_feed.maximum_short_squeeze_ratio = 1100;
   current_feed.settlement_price = bitusd.amount( 100 ) / core.amount( 5 );
   publish_feed( bitusd, feedproducer, current_feed );
   // start out with 200% collateral, call price is 10/175 CORE/USD = 40/700
   const call_order_object& call = *borrow( borrower, bitusd.amount(10), asset(1));
   call_order_id_type call_id = call.id;
   // create yet another position with 350% collateral, call price is 17.5/175 CORE/USD = 77/700
   const call_order_object& call3 = *borrow( borrower3, bitusd.amount(100000), asset(17500));
   call_order_id_type call3_id = call3.id;
   transfer(borrower, seller, bitusd.amount(10));
   transfer(borrower3, seller, bitusd.amount(100000));

   BOOST_CHECK_EQUAL( 10, call.debt.value );
   BOOST_CHECK_EQUAL( 1, call.collateral.value );
   BOOST_CHECK_EQUAL( 100000, call3.debt.value );
   BOOST_CHECK_EQUAL( 17500, call3.collateral.value );

   BOOST_CHECK_EQUAL( 100010, get_balance(seller, bitusd) );
   BOOST_CHECK_EQUAL( 0, get_balance(seller, core) );
   BOOST_CHECK_EQUAL( 0, get_balance(borrower, bitusd) );
   BOOST_CHECK_EQUAL( init_balance-1, get_balance(borrower, core) );

   // adjust price feed to get call_order into margin call territory
   current_feed.settlement_price = bitusd.amount( 120 ) / core.amount(10);
   publish_feed( bitusd, feedproducer, current_feed );
   // settlement price = 120 USD / 10 CORE, mssp = 120/11 USD/CORE

   // This would match with call at price 33 USD / 3 CORE, but call only owes 10 USD,
   //   so the seller will pay 10 USD but get nothing.
   // The remaining USD will be left in the order on the market
   limit_order_id_type sell_id = create_sell_order(seller, bitusd.amount(33), core.amount(3))->id;
   BOOST_CHECK( !db.find<call_order_object>( call_id ) ); // the first call order get filled
   BOOST_CHECK_EQUAL( 100010-33, get_balance(seller, bitusd) ); // the seller paid 33 USD
   BOOST_CHECK_EQUAL( 0, get_balance(seller, core) ); // the seller got nothing
   BOOST_CHECK_EQUAL( 33-10, sell_id(db).for_sale.value ); // the sell order has some USD left
   BOOST_CHECK_EQUAL( 0, get_balance(borrower, bitusd) );
   BOOST_CHECK_EQUAL( init_balance, get_balance(borrower, core) );

   generate_block();

} FC_LOG_AND_RETHROW() }

/***
 * Yet another test case
 * reproduces bitshares-core issue #132: something for nothing when maching a limit order with a call order.
 * Also detects the cull_small issue in check_call_orders.
 */
BOOST_AUTO_TEST_CASE( issue_132_limit_and_call_test3 )
{ try {
   generate_block();

   set_expiration( db, trx );

   ACTORS((buyer)(seller)(borrower)(borrower2)(borrower3)(borrower4)(feedproducer));

   const auto& bitusd = create_bitasset("USDBIT", feedproducer_id);
   const auto& core   = asset_id_type()(db);
   asset_id_type usd_id = bitusd.id;
   asset_id_type core_id = core.id;

   int64_t init_balance(1000000);

   transfer(committee_account, buyer_id, asset(init_balance));
   transfer(committee_account, borrower_id, asset(init_balance));
   transfer(committee_account, borrower2_id, asset(init_balance));
   transfer(committee_account, borrower3_id, asset(init_balance));
   transfer(committee_account, borrower4_id, asset(init_balance));
   update_feed_producers( bitusd, {feedproducer.id} );

   price_feed current_feed;
   current_feed.maintenance_collateral_ratio = 1750;
   current_feed.maximum_short_squeeze_ratio = 1100;
   current_feed.settlement_price = bitusd.amount( 100 ) / core.amount( 5 );
   publish_feed( bitusd, feedproducer, current_feed );
   // start out with 200% collateral, call price is 10/175 CORE/USD = 40/700
   const call_order_object& call = *borrow( borrower, bitusd.amount(10), asset(1));
   call_order_id_type call_id = call.id;
   // create yet another position with 350% collateral, call price is 17.5/175 CORE/USD = 77/700
   const call_order_object& call3 = *borrow( borrower3, bitusd.amount(100000), asset(17500));
   call_order_id_type call3_id = call3.id;
   transfer(borrower, seller, bitusd.amount(10));
   transfer(borrower3, seller, bitusd.amount(100000));

   BOOST_CHECK_EQUAL( 10, call.debt.value );
   BOOST_CHECK_EQUAL( 1, call.collateral.value );
   BOOST_CHECK_EQUAL( 100000, call3.debt.value );
   BOOST_CHECK_EQUAL( 17500, call3.collateral.value );

   BOOST_CHECK_EQUAL( 100010, get_balance(seller, bitusd) );
   BOOST_CHECK_EQUAL( 0, get_balance(seller, core) );
   BOOST_CHECK_EQUAL( 0, get_balance(borrower, bitusd) );
   BOOST_CHECK_EQUAL( init_balance-1, get_balance(borrower, core) );

   // create a limit order which will be matched later
   limit_order_id_type sell_id = create_sell_order(seller, bitusd.amount(33), core.amount(3))->id;
   BOOST_CHECK_EQUAL( 33, sell_id(db).for_sale.value );
   BOOST_CHECK_EQUAL( 100010-33, get_balance(seller, bitusd) );
   BOOST_CHECK_EQUAL( 0, get_balance(seller, core) );

   // adjust price feed to get call_order into margin call territory
   current_feed.settlement_price = bitusd.amount( 120 ) / core.amount(10);
   publish_feed( bitusd, feedproducer, current_feed );
   // settlement price = 120 USD / 10 CORE, mssp = 120/11 USD/CORE

   // the limit order will match with call at price 33 USD / 3 CORE, but call only owes 10 USD,
   //   so the seller will pay 10 USD but get nothing.
   // The remaining USD will be in the order on the market
   BOOST_CHECK( !db.find<call_order_object>( call_id ) ); // the first call order get filled
   BOOST_CHECK_EQUAL( 100010-33, get_balance(seller, bitusd) ); // the seller paid 33 USD
   BOOST_CHECK_EQUAL( 0, get_balance(seller, core) ); // the seller got nothing
   BOOST_CHECK_EQUAL( 33-10, sell_id(db).for_sale.value ); // the sell order has some USD left
   BOOST_CHECK_EQUAL( 0, get_balance(borrower, bitusd) );
   BOOST_CHECK_EQUAL( init_balance, get_balance(borrower, core) );

   generate_block();

} FC_LOG_AND_RETHROW() }

/***
 * Fixed bitshares-core issue #132: something for nothing when maching a limit order with a call order.
 */
BOOST_AUTO_TEST_CASE( issue_132_limit_and_call_test1_after_hardfork )
{ try {
   auto mi = db.get_global_properties().parameters.maintenance_interval;
   generate_blocks(HARDFORK_CORE_184_TIME - mi);
   generate_blocks(db.get_dynamic_global_properties().next_maintenance_time);

   set_expiration( db, trx );

   ACTORS((buyer)(seller)(borrower)(borrower2)(borrower3)(borrower4)(feedproducer));

   const auto& bitusd = create_bitasset("USDBIT", feedproducer_id);
   const auto& core   = asset_id_type()(db);
   asset_id_type usd_id = bitusd.id;
   asset_id_type core_id = core.id;

   int64_t init_balance(1000000);

   transfer(committee_account, buyer_id, asset(init_balance));
   transfer(committee_account, borrower_id, asset(init_balance));
   transfer(committee_account, borrower2_id, asset(init_balance));
   transfer(committee_account, borrower3_id, asset(init_balance));
   transfer(committee_account, borrower4_id, asset(init_balance));
   update_feed_producers( bitusd, {feedproducer.id} );

   price_feed current_feed;
   current_feed.maintenance_collateral_ratio = 1750;
   current_feed.maximum_short_squeeze_ratio = 1100;
   current_feed.settlement_price = bitusd.amount( 100 ) / core.amount( 5 );
   publish_feed( bitusd, feedproducer, current_feed );
   // start out with 200% collateral, call price is 10/175 CORE/USD = 40/700
   const call_order_object& call = *borrow( borrower, bitusd.amount(10), asset(1));
   call_order_id_type call_id = call.id;
   // create another position with 310% collateral, call price is 15.5/175 CORE/USD = 62/700
   const call_order_object& call2 = *borrow( borrower2, bitusd.amount(100000), asset(15500));
   call_order_id_type call2_id = call2.id;
   // create yet another position with 350% collateral, call price is 17.5/175 CORE/USD = 77/700
   const call_order_object& call3 = *borrow( borrower3, bitusd.amount(100000), asset(17500));
   call_order_id_type call3_id = call3.id;
   transfer(borrower, seller, bitusd.amount(10));
   transfer(borrower2, seller, bitusd.amount(100000));
   transfer(borrower3, seller, bitusd.amount(100000));

   BOOST_CHECK_EQUAL( 10, call.debt.value );
   BOOST_CHECK_EQUAL( 1, call.collateral.value );
   BOOST_CHECK_EQUAL( 100000, call2.debt.value );
   BOOST_CHECK_EQUAL( 15500, call2.collateral.value );
   BOOST_CHECK_EQUAL( 100000, call3.debt.value );
   BOOST_CHECK_EQUAL( 17500, call3.collateral.value );

   BOOST_CHECK_EQUAL( 200010, get_balance(seller, bitusd) );
   BOOST_CHECK_EQUAL( 0, get_balance(seller, core) );
   BOOST_CHECK_EQUAL( 0, get_balance(borrower, bitusd) );
   BOOST_CHECK_EQUAL( init_balance-1, get_balance(borrower, core) );

   // adjust price feed to get call_order into margin call territory
   current_feed.settlement_price = bitusd.amount( 120 ) / core.amount(10);
   publish_feed( bitusd, feedproducer, current_feed );
   // settlement price = 120 USD / 10 CORE, mssp = 120/11 USD/CORE

   // This would match with call at price 11 USD / 1 CORE, but call only owes 10 USD,
   // Since the call would pay off all debt, let it pay 1 CORE from collateral
   // The remaining 1 USD is too little to get any CORE, so the limit order will be cancelled
   BOOST_CHECK( !create_sell_order(seller, bitusd.amount(11), core.amount(1)) );
   BOOST_CHECK( !db.find<call_order_object>( call_id ) ); // the first call order get filled
   BOOST_CHECK_EQUAL( 200000, get_balance(seller, bitusd) ); // the seller paid 10 USD
   BOOST_CHECK_EQUAL( 1, get_balance(seller, core) ); // the seller got 1 CORE
   BOOST_CHECK_EQUAL( 0, get_balance(borrower, bitusd) );
   BOOST_CHECK_EQUAL( init_balance-1, get_balance(borrower, core) );

   generate_block();

} FC_LOG_AND_RETHROW() }

/***
 * Another test case
 * for fixed bitshares-core issue #132: something for nothing when maching a limit order with a call order.
 */
BOOST_AUTO_TEST_CASE( issue_132_limit_and_call_test2_after_hardfork )
{ try {
   auto mi = db.get_global_properties().parameters.maintenance_interval;
   generate_blocks(HARDFORK_CORE_184_TIME - mi);
   generate_blocks(db.get_dynamic_global_properties().next_maintenance_time);

   set_expiration( db, trx );

   ACTORS((buyer)(seller)(borrower)(borrower2)(borrower3)(borrower4)(feedproducer));

   const auto& bitusd = create_bitasset("USDBIT", feedproducer_id);
   const auto& core   = asset_id_type()(db);
   asset_id_type usd_id = bitusd.id;
   asset_id_type core_id = core.id;

   int64_t init_balance(1000000);

   transfer(committee_account, buyer_id, asset(init_balance));
   transfer(committee_account, borrower_id, asset(init_balance));
   transfer(committee_account, borrower2_id, asset(init_balance));
   transfer(committee_account, borrower3_id, asset(init_balance));
   transfer(committee_account, borrower4_id, asset(init_balance));
   update_feed_producers( bitusd, {feedproducer.id} );

   price_feed current_feed;
   current_feed.maintenance_collateral_ratio = 1750;
   current_feed.maximum_short_squeeze_ratio = 1100;
   current_feed.settlement_price = bitusd.amount( 100 ) / core.amount( 5 );
   publish_feed( bitusd, feedproducer, current_feed );
   // start out with 200% collateral, call price is 10/175 CORE/USD = 40/700
   const call_order_object& call = *borrow( borrower, bitusd.amount(10), asset(1));
   call_order_id_type call_id = call.id;
   // create yet another position with 350% collateral, call price is 17.5/175 CORE/USD = 77/700
   const call_order_object& call3 = *borrow( borrower3, bitusd.amount(100000), asset(17500));
   call_order_id_type call3_id = call3.id;
   transfer(borrower, seller, bitusd.amount(10));
   transfer(borrower3, seller, bitusd.amount(100000));

   BOOST_CHECK_EQUAL( 10, call.debt.value );
   BOOST_CHECK_EQUAL( 1, call.collateral.value );
   BOOST_CHECK_EQUAL( 100000, call3.debt.value );
   BOOST_CHECK_EQUAL( 17500, call3.collateral.value );

   BOOST_CHECK_EQUAL( 100010, get_balance(seller, bitusd) );
   BOOST_CHECK_EQUAL( 0, get_balance(seller, core) );
   BOOST_CHECK_EQUAL( 0, get_balance(borrower, bitusd) );
   BOOST_CHECK_EQUAL( init_balance-1, get_balance(borrower, core) );

   // adjust price feed to get call_order into margin call territory
   current_feed.settlement_price = bitusd.amount( 120 ) / core.amount(10);
   publish_feed( bitusd, feedproducer, current_feed );
   // settlement price = 120 USD / 10 CORE, mssp = 120/11 USD/CORE

   // This would match with call at price 33 USD / 3 CORE, but call only owes 10 USD,
   // Since the call would pay off all debt, let it pay 1 CORE from collateral
   // The remaining USD will be left in the order on the market
   limit_order_id_type sell_id = create_sell_order(seller, bitusd.amount(33), core.amount(3))->id;
   BOOST_CHECK( !db.find<call_order_object>( call_id ) ); // the first call order get filled
   BOOST_CHECK_EQUAL( 100010-33, get_balance(seller, bitusd) ); // the seller paid 33 USD
   BOOST_CHECK_EQUAL( 1, get_balance(seller, core) ); // the seller got 1 CORE
   BOOST_CHECK_EQUAL( 33-10, sell_id(db).for_sale.value ); // the sell order has some USD left
   BOOST_CHECK_EQUAL( 0, get_balance(borrower, bitusd) );
   BOOST_CHECK_EQUAL( init_balance-1, get_balance(borrower, core) );

   generate_block();

} FC_LOG_AND_RETHROW() }

/***
 * Yet another test case
 * for fixed bitshares-core issue #132: something for nothing when maching a limit order with a call order.
 * Also detects the cull_small issue in check_call_orders.
 */
BOOST_AUTO_TEST_CASE( issue_132_limit_and_call_test3_after_hardfork )
{ try {
   auto mi = db.get_global_properties().parameters.maintenance_interval;
   generate_blocks(HARDFORK_CORE_184_TIME - mi);
   generate_blocks(db.get_dynamic_global_properties().next_maintenance_time);

   set_expiration( db, trx );

   ACTORS((buyer)(seller)(borrower)(borrower2)(borrower3)(borrower4)(feedproducer));

   const auto& bitusd = create_bitasset("USDBIT", feedproducer_id);
   const auto& core   = asset_id_type()(db);
   asset_id_type usd_id = bitusd.id;
   asset_id_type core_id = core.id;

   int64_t init_balance(1000000);

   transfer(committee_account, buyer_id, asset(init_balance));
   transfer(committee_account, borrower_id, asset(init_balance));
   transfer(committee_account, borrower2_id, asset(init_balance));
   transfer(committee_account, borrower3_id, asset(init_balance));
   transfer(committee_account, borrower4_id, asset(init_balance));
   update_feed_producers( bitusd, {feedproducer.id} );

   price_feed current_feed;
   current_feed.maintenance_collateral_ratio = 1750;
   current_feed.maximum_short_squeeze_ratio = 1100;
   current_feed.settlement_price = bitusd.amount( 100 ) / core.amount( 5 );
   publish_feed( bitusd, feedproducer, current_feed );
   // start out with 200% collateral, call price is 10/175 CORE/USD = 40/700
   const call_order_object& call = *borrow( borrower, bitusd.amount(10), asset(1));
   call_order_id_type call_id = call.id;
   // create yet another position with 350% collateral, call price is 17.5/175 CORE/USD = 77/700
   const call_order_object& call3 = *borrow( borrower3, bitusd.amount(100000), asset(17500));
   call_order_id_type call3_id = call3.id;
   transfer(borrower, seller, bitusd.amount(10));
   transfer(borrower3, seller, bitusd.amount(100000));

   BOOST_CHECK_EQUAL( 10, call.debt.value );
   BOOST_CHECK_EQUAL( 1, call.collateral.value );
   BOOST_CHECK_EQUAL( 100000, call3.debt.value );
   BOOST_CHECK_EQUAL( 17500, call3.collateral.value );

   BOOST_CHECK_EQUAL( 100010, get_balance(seller, bitusd) );
   BOOST_CHECK_EQUAL( 0, get_balance(seller, core) );
   BOOST_CHECK_EQUAL( 0, get_balance(borrower, bitusd) );
   BOOST_CHECK_EQUAL( init_balance-1, get_balance(borrower, core) );

   // create a limit order which will be matched later
   limit_order_id_type sell_id = create_sell_order(seller, bitusd.amount(33), core.amount(3))->id;
   BOOST_CHECK_EQUAL( 33, sell_id(db).for_sale.value );
   BOOST_CHECK_EQUAL( 100010-33, get_balance(seller, bitusd) );
   BOOST_CHECK_EQUAL( 0, get_balance(seller, core) );

   // adjust price feed to get call_order into margin call territory
   current_feed.settlement_price = bitusd.amount( 120 ) / core.amount(10);
   publish_feed( bitusd, feedproducer, current_feed );
   // settlement price = 120 USD / 10 CORE, mssp = 120/11 USD/CORE

   // the limit order will match with call at price 33 USD / 3 CORE, but call only owes 10 USD,
   // Since the call would pay off all debt, let it pay 1 CORE from collateral
   // The remaining USD will be in the order on the market
   BOOST_CHECK( !db.find<call_order_object>( call_id ) ); // the first call order get filled
   BOOST_CHECK_EQUAL( 100010-33, get_balance(seller, bitusd) ); // the seller paid 33 USD
   BOOST_CHECK_EQUAL( 1, get_balance(seller, core) ); // the seller got 1 CORE
   BOOST_CHECK_EQUAL( 33-10, sell_id(db).for_sale.value ); // the sell order has some USD left
   BOOST_CHECK_EQUAL( 0, get_balance(borrower, bitusd) );
   BOOST_CHECK_EQUAL( init_balance-1, get_balance(borrower, core) );

   generate_block();

} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_SUITE_END()
