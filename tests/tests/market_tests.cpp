/*
 * Copyright (c) 2017 Peter Conrad, and other contributors.
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

#include <graphene/app/database_api.hpp>
#include <graphene/chain/hardfork.hpp>
#include <graphene/wallet/wallet.hpp>

#include <iostream>

#include "../common/database_fixture.hpp"

using namespace graphene::chain;
using namespace graphene::chain::test;
using namespace graphene::wallet;

BOOST_FIXTURE_TEST_SUITE(market_tests, database_fixture)

/***
 * Reproduce bitshares-core issue #338 #343 #453 #606 #625 #649
 */
BOOST_AUTO_TEST_CASE(issue_338_etc)
{ try {
   generate_blocks(HARDFORK_615_TIME); // get around Graphene issue #615 feed expiration bug
   generate_block();

   set_expiration( db, trx );

   ACTORS((buyer)(seller)(borrower)(borrower2)(borrower3)(feedproducer));

   const auto& bitusd = create_bitasset("USDBIT", feedproducer_id);
   const auto& core   = asset_id_type()(db);
   asset_id_type usd_id = bitusd.id;
   asset_id_type core_id = core.id;

   int64_t init_balance(1000000);

   transfer(committee_account, buyer_id, asset(init_balance));
   transfer(committee_account, borrower_id, asset(init_balance));
   transfer(committee_account, borrower2_id, asset(init_balance));
   transfer(committee_account, borrower3_id, asset(init_balance));
   update_feed_producers( bitusd, {feedproducer.id} );

   price_feed current_feed;
   current_feed.maintenance_collateral_ratio = 1750;
   current_feed.maximum_short_squeeze_ratio = 1100;
   current_feed.settlement_price = bitusd.amount( 1 ) / core.amount(5);
   publish_feed( bitusd, feedproducer, current_feed );
   // start out with 300% collateral, call price is 15/1.75 CORE/USD = 60/7
   const call_order_object& call = *borrow( borrower, bitusd.amount(1000), asset(15000));
   call_order_id_type call_id = call.id;
   // create another position with 310% collateral, call price is 15.5/1.75 CORE/USD = 63/7
   const call_order_object& call2 = *borrow( borrower2, bitusd.amount(1000), asset(15500));
   call_order_id_type call2_id = call2.id;
   // create yet another position with 320% collateral, call price is 16/1.75 CORE/USD = 64/7
   const call_order_object& call3 = *borrow( borrower3, bitusd.amount(1000), asset(16000));
   call_order_id_type call3_id = call3.id;
   transfer(borrower, seller, bitusd.amount(1000));

   BOOST_CHECK_EQUAL( 1000, call.debt.value );
   BOOST_CHECK_EQUAL( 15000, call.collateral.value );
   BOOST_CHECK_EQUAL( 1000, get_balance(seller, bitusd) );
   BOOST_CHECK_EQUAL( 0, get_balance(seller, core) );

   // adjust price feed to get call_order into margin call territory
   current_feed.settlement_price = bitusd.amount( 1 ) / core.amount(10);
   publish_feed( bitusd, feedproducer, current_feed );
   // settlement price = 1/10, mssp = 1/11

   // This order slightly below the call price will not be matched #606
   limit_order_id_type sell_low = create_sell_order(seller, bitusd.amount(7), core.amount(59))->id;
   // This order above the MSSP will not be matched
   limit_order_id_type sell_high = create_sell_order(seller, bitusd.amount(7), core.amount(78))->id;
   // This would match but is blocked by sell_low?! #606
   limit_order_id_type sell_med = create_sell_order(seller, bitusd.amount(7), core.amount(60))->id;

   cancel_limit_order( sell_med(db) );
   cancel_limit_order( sell_high(db) );
   cancel_limit_order( sell_low(db) );

   // current implementation: an incoming limit order will be filled at the
   // requested price #338
   BOOST_CHECK( !create_sell_order(seller, bitusd.amount(7), core.amount(60)) );
   BOOST_CHECK_EQUAL( 993, get_balance(seller, bitusd) );
   BOOST_CHECK_EQUAL( 60, get_balance(seller, core) );
   BOOST_CHECK_EQUAL( 993, call.debt.value );
   BOOST_CHECK_EQUAL( 14940, call.collateral.value );

   limit_order_id_type buy_low = create_sell_order(buyer, asset(90), bitusd.amount(10))->id;
   // margin call takes precedence
   BOOST_CHECK( !create_sell_order(seller, bitusd.amount(7), core.amount(60)) );
   BOOST_CHECK_EQUAL( 986, get_balance(seller, bitusd) );
   BOOST_CHECK_EQUAL( 120, get_balance(seller, core) );
   BOOST_CHECK_EQUAL( 986, call.debt.value );
   BOOST_CHECK_EQUAL( 14880, call.collateral.value );

   limit_order_id_type buy_med = create_sell_order(buyer, asset(105), bitusd.amount(10))->id;
   // margin call takes precedence
   BOOST_CHECK( !create_sell_order(seller, bitusd.amount(7), core.amount(70)) );
   BOOST_CHECK_EQUAL( 979, get_balance(seller, bitusd) );
   BOOST_CHECK_EQUAL( 190, get_balance(seller, core) );
   BOOST_CHECK_EQUAL( 979, call.debt.value );
   BOOST_CHECK_EQUAL( 14810, call.collateral.value );

   limit_order_id_type buy_high = create_sell_order(buyer, asset(115), bitusd.amount(10))->id;
   // margin call still has precedence (!) #625
   BOOST_CHECK( !create_sell_order(seller, bitusd.amount(7), core.amount(77)) );
   BOOST_CHECK_EQUAL( 972, get_balance(seller, bitusd) );
   BOOST_CHECK_EQUAL( 267, get_balance(seller, core) );
   BOOST_CHECK_EQUAL( 972, call.debt.value );
   BOOST_CHECK_EQUAL( 14733, call.collateral.value );

   cancel_limit_order( buy_high(db) );
   cancel_limit_order( buy_med(db) );
   cancel_limit_order( buy_low(db) );

   // call with more usd
   BOOST_CHECK( !create_sell_order(seller, bitusd.amount(700), core.amount(7700)) );
   BOOST_CHECK_EQUAL( 272, get_balance(seller, bitusd) );
   BOOST_CHECK_EQUAL( 7967, get_balance(seller, core) );
   BOOST_CHECK_EQUAL( 272, call.debt.value );
   BOOST_CHECK_EQUAL( 7033, call.collateral.value );

   // at this moment, collateralization of call is 7033 / 272 = 25.8
   // collateralization of call2 is 15500 / 1000 = 15.5
   // collateralization of call3 is 16000 / 1000 = 16

   // call more, still matches with the first call order #343
   BOOST_CHECK( !create_sell_order(seller, bitusd.amount(10), core.amount(110)) );
   BOOST_CHECK_EQUAL( 262, get_balance(seller, bitusd) );
   BOOST_CHECK_EQUAL( 8077, get_balance(seller, core) );
   BOOST_CHECK_EQUAL( 262, call.debt.value );
   BOOST_CHECK_EQUAL( 6923, call.collateral.value );

   // at this moment, collateralization of call is 6923 / 262 = 26.4
   // collateralization of call2 is 15500 / 1000 = 15.5
   // collateralization of call3 is 16000 / 1000 = 16

   // force settle
   force_settle( seller, bitusd.amount(10) );
   BOOST_CHECK_EQUAL( 252, get_balance(seller, bitusd) );
   BOOST_CHECK_EQUAL( 8077, get_balance(seller, core) );
   BOOST_CHECK_EQUAL( 262, call.debt.value );
   BOOST_CHECK_EQUAL( 6923, call.collateral.value );

   // generate blocks to let the settle order execute (price feed will expire after it)
   generate_blocks( HARDFORK_615_TIME + fc::hours(25) );
   // call2 get settled #343
   BOOST_CHECK_EQUAL( 252, get_balance(seller_id, usd_id) );
   BOOST_CHECK_EQUAL( 8177, get_balance(seller_id, core_id) );
   BOOST_CHECK_EQUAL( 262, call_id(db).debt.value );
   BOOST_CHECK_EQUAL( 6923, call_id(db).collateral.value );
   BOOST_CHECK_EQUAL( 990, call2_id(db).debt.value );
   BOOST_CHECK_EQUAL( 15400, call2_id(db).collateral.value );

   set_expiration( db, trx );
   update_feed_producers( usd_id(db), {feedproducer_id} );

   // at this moment, collateralization of call is 8177 / 252 = 32.4
   // collateralization of call2 is 15400 / 990 = 15.5
   // collateralization of call3 is 16000 / 1000 = 16

   // adjust price feed to get call2 into black swan territory, but not the first call order
   current_feed.settlement_price = asset(1, usd_id) / asset(20, core_id);
   publish_feed( usd_id(db), feedproducer_id(db), current_feed );
   // settlement price = 1/20, mssp = 1/22

   // black swan event doesn't occur #649
   BOOST_CHECK( !usd_id(db).bitasset_data(db).has_settlement() );

   // generate a block
   generate_block();

   set_expiration( db, trx );
   update_feed_producers( usd_id(db), {feedproducer_id} );

   // adjust price feed back
   current_feed.settlement_price = asset(1, usd_id) / asset(10, core_id);
   publish_feed( usd_id(db), feedproducer_id(db), current_feed );
   // settlement price = 1/10, mssp = 1/11

   transfer(borrower2_id, seller_id, asset(1000, usd_id));
   transfer(borrower3_id, seller_id, asset(1000, usd_id));

   // Re-create sell_low, slightly below the call price, will not be matched, will expire soon
   sell_low = create_sell_order(seller_id(db), asset(7, usd_id), asset(59), db.head_block_time()+fc::seconds(300) )->id;
   // This would match but is blocked by sell_low, it has an amount same as call's debt which will be full filled later
   sell_med = create_sell_order(seller_id(db), asset(262, usd_id), asset(2620))->id; // 1/10
   // Another big order above sell_med, blocked
   limit_order_id_type sell_med2 = create_sell_order(seller_id(db), asset(1200, usd_id), asset(12120))->id; // 1/10.1
   // Another small order above sell_med2, blocked
   limit_order_id_type sell_med3 = create_sell_order(seller_id(db), asset(120, usd_id), asset(1224))->id; // 1/10.2

   // generate a block, sell_low will expire
   BOOST_TEST_MESSAGE( "Expire sell_low" );
   generate_blocks( HARDFORK_615_TIME + fc::hours(26) );
   BOOST_CHECK( db.find<limit_order_object>( sell_low ) == nullptr );

   // #453 multiple order matching issue occurs
   BOOST_CHECK( db.find<limit_order_object>( sell_med ) == nullptr ); // sell_med get filled
   BOOST_CHECK( db.find<limit_order_object>( sell_med2 ) != nullptr ); // sell_med2 is still there
   BOOST_CHECK( db.find<limit_order_object>( sell_med3 ) == nullptr ); // sell_med3 get filled
   BOOST_CHECK( db.find<call_order_object>( call_id ) == nullptr ); // the first call order get filled
   BOOST_CHECK( db.find<call_order_object>( call2_id ) == nullptr ); // the second call order get filled
   BOOST_CHECK( db.find<call_order_object>( call3_id ) != nullptr ); // the third call order is still there


} FC_LOG_AND_RETHROW() }


BOOST_AUTO_TEST_SUITE_END()
