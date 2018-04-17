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
 * Reproduce bitshares-core issue #338
 */
BOOST_AUTO_TEST_CASE(issue_338)
{ try {
   generate_blocks(HARDFORK_436_TIME);
   generate_block();

   set_expiration( db, trx );

   ACTORS((buyer)(seller)(borrower)(feedproducer));

   const auto& bitusd = create_bitasset("USDBIT", feedproducer_id);
   const auto& core   = asset_id_type()(db);

   int64_t init_balance(1000000);

   transfer(committee_account, buyer_id, asset(init_balance));
   transfer(committee_account, borrower_id, asset(init_balance));
   update_feed_producers( bitusd, {feedproducer.id} );

   price_feed current_feed;
   current_feed.maintenance_collateral_ratio = 1750;
   current_feed.maximum_short_squeeze_ratio = 1100;
   current_feed.settlement_price = bitusd.amount( 1 ) / core.amount(5);
   publish_feed( bitusd, feedproducer, current_feed );
   // start out with 300% collateral, call price is 15/1.75 CORE/USD = 60/7
   const call_order_object& call = *borrow( borrower, bitusd.amount(1000), asset(15000));
   transfer(borrower, seller, bitusd.amount(1000));

   BOOST_CHECK_EQUAL( 1000, call.debt.value );
   BOOST_CHECK_EQUAL( 15000, call.collateral.value );
   BOOST_CHECK_EQUAL( 1000, get_balance(seller, bitusd) );
   BOOST_CHECK_EQUAL( 0, get_balance(seller, core) );

   // adjust price feed to get call_order into margin call territory
   current_feed.settlement_price = bitusd.amount( 1 ) / core.amount(10);
   publish_feed( bitusd, feedproducer, current_feed );
   // settlement price = 1/10, mssp = 1/11

   // This order slightly below the call price will not be matched
   limit_order_id_type sell_low = create_sell_order(seller, bitusd.amount(7), core.amount(59))->id;
   // This order above the MSSP will not be matched
   limit_order_id_type sell_high = create_sell_order(seller, bitusd.amount(7), core.amount(78))->id;
   // This would match but is blocked by sell_low?!
   limit_order_id_type sell_med = create_sell_order(seller, bitusd.amount(7), core.amount(60))->id;

   cancel_limit_order( sell_med(db) );
   cancel_limit_order( sell_high(db) );
   cancel_limit_order( sell_low(db) );

   // current implementation: an incoming limit order will be filled at the
   // requested price
   BOOST_CHECK( !create_sell_order(seller, bitusd.amount(7), core.amount(60)) );
   BOOST_CHECK_EQUAL( 993, get_balance(seller, bitusd) );
   BOOST_CHECK_EQUAL( 60, get_balance(seller, core) );
   BOOST_CHECK_EQUAL( 993, call.debt.value );
   BOOST_CHECK_EQUAL( 14940, call.collateral.value );

   auto buy_low = create_sell_order(buyer, asset(90), bitusd.amount(10))->id;
   // margin call takes precedence
   BOOST_CHECK( !create_sell_order(seller, bitusd.amount(7), core.amount(60)) );
   BOOST_CHECK_EQUAL( 986, get_balance(seller, bitusd) );
   BOOST_CHECK_EQUAL( 120, get_balance(seller, core) );
   BOOST_CHECK_EQUAL( 986, call.debt.value );
   BOOST_CHECK_EQUAL( 14880, call.collateral.value );

   auto buy_med = create_sell_order(buyer, asset(105), bitusd.amount(10))->id;
   // margin call takes precedence
   BOOST_CHECK( !create_sell_order(seller, bitusd.amount(7), core.amount(70)) );
   BOOST_CHECK_EQUAL( 979, get_balance(seller, bitusd) );
   BOOST_CHECK_EQUAL( 190, get_balance(seller, core) );
   BOOST_CHECK_EQUAL( 979, call.debt.value );
   BOOST_CHECK_EQUAL( 14810, call.collateral.value );

   auto buy_high = create_sell_order(buyer, asset(115), bitusd.amount(10))->id;
   // margin call still has precedence (!))
   BOOST_CHECK( !create_sell_order(seller, bitusd.amount(7), core.amount(77)) );
   BOOST_CHECK_EQUAL( 972, get_balance(seller, bitusd) );
   BOOST_CHECK_EQUAL( 267, get_balance(seller, core) );
   BOOST_CHECK_EQUAL( 972, call.debt.value );
   BOOST_CHECK_EQUAL( 14733, call.collateral.value );
} FC_LOG_AND_RETHROW() }

/***
 * reproduce check_call_orders cull_small issue
 */
BOOST_AUTO_TEST_CASE( check_call_order_cull_small_test )
{ try { // matching a limit order with call order
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

BOOST_AUTO_TEST_SUITE_END()
