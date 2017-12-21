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

BOOST_AUTO_TEST_CASE( create_uia )
{
   try {
      asset_id_type test_asset_id = db.get_index<asset_object>().get_next_id();
      asset_create_operation creator;
      creator.issuer = account_id_type();
      creator.fee = asset();
      creator.symbol = "TEST";
      creator.common_options.max_supply = 100000000;
      creator.precision = 2;
      creator.common_options.market_fee_percent = GRAPHENE_MAX_MARKET_FEE_PERCENT/100; /*1%*/
      creator.common_options.issuer_permissions = UIA_ASSET_ISSUER_PERMISSION_MASK;
      creator.common_options.flags = charge_market_fee;
      creator.common_options.core_exchange_rate = price({asset(2),asset(1,asset_id_type(1))});
      trx.operations.push_back(std::move(creator));
      PUSH_TX( db, trx, ~0 );

      const asset_object& test_asset = test_asset_id(db);
      BOOST_CHECK(test_asset.symbol == "TEST");
      BOOST_CHECK(asset(1, test_asset_id) * test_asset.options.core_exchange_rate == asset(2));
      BOOST_CHECK((test_asset.options.flags & white_list) == 0);
      BOOST_CHECK(test_asset.options.max_supply == 100000000);
      BOOST_CHECK(!test_asset.bitasset_data_id.valid());
      BOOST_CHECK(test_asset.options.market_fee_percent == GRAPHENE_MAX_MARKET_FEE_PERCENT/100);
      GRAPHENE_REQUIRE_THROW(PUSH_TX( db, trx, ~0 ), fc::exception);

      const asset_dynamic_data_object& test_asset_dynamic_data = test_asset.dynamic_asset_data_id(db);
      BOOST_CHECK(test_asset_dynamic_data.current_supply == 0);
      BOOST_CHECK(test_asset_dynamic_data.accumulated_fees == 0);
      BOOST_CHECK(test_asset_dynamic_data.fee_pool == 0);

      auto op = trx.operations.back().get<asset_create_operation>();
      op.symbol = "TESTFAIL";
      REQUIRE_THROW_WITH_VALUE(op, issuer, account_id_type(99999999));
      REQUIRE_THROW_WITH_VALUE(op, common_options.max_supply, -1);
      REQUIRE_THROW_WITH_VALUE(op, common_options.max_supply, 0);
      REQUIRE_THROW_WITH_VALUE(op, symbol, "A");
      REQUIRE_THROW_WITH_VALUE(op, symbol, "qqq");
      REQUIRE_THROW_WITH_VALUE(op, symbol, "11");
      REQUIRE_THROW_WITH_VALUE(op, symbol, ".AAA");
      REQUIRE_THROW_WITH_VALUE(op, symbol, "AAA.");
      REQUIRE_THROW_WITH_VALUE(op, symbol, "AB CD");
      REQUIRE_THROW_WITH_VALUE(op, symbol, "ABCDEFGHIJKLMNOPQRSTUVWXYZ");
      REQUIRE_THROW_WITH_VALUE(op, common_options.core_exchange_rate, price({asset(-100), asset(1)}));
      REQUIRE_THROW_WITH_VALUE(op, common_options.core_exchange_rate, price({asset(100),asset(-1)}));
   } catch(fc::exception& e) {
      edump((e.to_detail_string()));
      throw;
   }
}

BOOST_AUTO_TEST_CASE( create_account_test )
{
   try {
      trx.operations.push_back(make_account());
      account_create_operation op = trx.operations.back().get<account_create_operation>();

      REQUIRE_THROW_WITH_VALUE(op, registrar, account_id_type(9999999));
      REQUIRE_THROW_WITH_VALUE(op, fee, asset(-1));
      REQUIRE_THROW_WITH_VALUE(op, name, "!");
      REQUIRE_THROW_WITH_VALUE(op, name, "Sam");
      REQUIRE_THROW_WITH_VALUE(op, name, "saM");
      REQUIRE_THROW_WITH_VALUE(op, name, "sAm");
      REQUIRE_THROW_WITH_VALUE(op, name, "6j");
      REQUIRE_THROW_WITH_VALUE(op, name, "j-");
      REQUIRE_THROW_WITH_VALUE(op, name, "-j");
      REQUIRE_THROW_WITH_VALUE(op, name, "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
      REQUIRE_THROW_WITH_VALUE(op, name, "aaaa.");
      REQUIRE_THROW_WITH_VALUE(op, name, ".aaaa");
      REQUIRE_THROW_WITH_VALUE(op, options.voting_account, account_id_type(999999999));

      auto auth_bak = op.owner;
      op.owner.add_authority(account_id_type(9999999999), 10);
      trx.operations.back() = op;
      op.owner = auth_bak;
      GRAPHENE_REQUIRE_THROW(PUSH_TX( db, trx, ~0 ), fc::exception);
      op.owner = auth_bak;

      trx.operations.back() = op;
      sign( trx,  init_account_priv_key );
      trx.validate();
      PUSH_TX( db, trx, ~0 );

      const account_object& nathan_account = *db.get_index_type<account_index>().indices().get<by_name>().find("nathan");
      BOOST_CHECK(nathan_account.id.space() == protocol_ids);
      BOOST_CHECK(nathan_account.id.type() == account_object_type);
      BOOST_CHECK(nathan_account.name == "nathan");

      BOOST_REQUIRE(nathan_account.owner.num_auths() == 1);
      BOOST_CHECK(nathan_account.owner.key_auths.at(committee_key) == 123);
      BOOST_REQUIRE(nathan_account.active.num_auths() == 1);
      BOOST_CHECK(nathan_account.active.key_auths.at(committee_key) == 321);
      BOOST_CHECK(nathan_account.options.voting_account == GRAPHENE_PROXY_TO_SELF_ACCOUNT);
      BOOST_CHECK(nathan_account.options.memo_key == committee_key);

      const account_statistics_object& statistics = nathan_account.statistics(db);
      BOOST_CHECK(statistics.id.space() == implementation_ids);
      BOOST_CHECK(statistics.id.type() == impl_account_statistics_object_type);
   } catch (fc::exception& e) {
      edump((e.to_detail_string()));
      throw;
   }
}

BOOST_AUTO_TEST_CASE( issue_uia )
{
   try {
      INVOKE(create_uia);
      INVOKE(create_account_test);

      const asset_object& test_asset = *db.get_index_type<asset_index>().indices().get<by_symbol>().find("TEST");
      const account_object& nathan_account = *db.get_index_type<account_index>().indices().get<by_name>().find("nathan");

      asset_issue_operation op;
      op.issuer = test_asset.issuer;
      op.asset_to_issue =  test_asset.amount(5000000);
      op.issue_to_account = nathan_account.id;
      trx.operations.push_back(op);

      REQUIRE_THROW_WITH_VALUE(op, asset_to_issue, asset(200));
      REQUIRE_THROW_WITH_VALUE(op, fee, asset(-1));
      REQUIRE_THROW_WITH_VALUE(op, issue_to_account, account_id_type(999999999));

      trx.operations.back() = op;
      PUSH_TX( db, trx, ~0 );

      const asset_dynamic_data_object& test_dynamic_data = test_asset.dynamic_asset_data_id(db);
      BOOST_CHECK_EQUAL(get_balance(nathan_account, test_asset), 5000000);
      BOOST_CHECK(test_dynamic_data.current_supply == 5000000);
      BOOST_CHECK(test_dynamic_data.accumulated_fees == 0);
      BOOST_CHECK(test_dynamic_data.fee_pool == 0);

      PUSH_TX( db, trx, ~0 );

      BOOST_CHECK_EQUAL(get_balance(nathan_account, test_asset), 10000000);
      BOOST_CHECK(test_dynamic_data.current_supply == 10000000);
      BOOST_CHECK(test_dynamic_data.accumulated_fees == 0);
      BOOST_CHECK(test_dynamic_data.fee_pool == 0);
   } catch(fc::exception& e) {
      edump((e.to_detail_string()));
      throw;
   }
}

BOOST_AUTO_TEST_CASE( taker_sells_1to1 )
{ try {
   INVOKE( issue_uia );
   const asset_object&   core_asset     = get_asset( "TEST" );
   const asset_object&   test_asset     = get_asset( GRAPHENE_SYMBOL );
   const account_object& nathan_account = get_account( "nathan" );
   const account_object& buyer_account  = create_account( "buyer" );
   const account_object& seller_account = create_account( "seller" );

   transfer( committee_account(db), buyer_account, test_asset.amount( 10000 ) );
   transfer( nathan_account, seller_account, core_asset.amount(10000) );

   BOOST_CHECK_EQUAL( get_balance( buyer_account, test_asset ), 10000 );

   limit_order_id_type first_id  = create_sell_order( seller_account, core_asset.amount(50), test_asset.amount(100) )->id;
   limit_order_id_type second_id = create_sell_order( seller_account, core_asset.amount(100), test_asset.amount(100) )->id;

   BOOST_CHECK_EQUAL( get_balance( seller_account, core_asset ), 9850 );

   auto unmatched = create_sell_order( buyer_account, test_asset.amount(100), core_asset.amount(100) );
   BOOST_CHECK( !db.find( first_id ) );
   BOOST_CHECK( !db.find( second_id ) );
   if( unmatched ) wdump((*unmatched));
   BOOST_CHECK( !unmatched );

   //APM
   //sell_asset nathan 400 BTS 100 TEST 100000 false true    <-- seller BUY 100 TEST @ 4 (bts)
   //sell_asset nathan 300 BTS 100 TEST 100000 false true    <-- seller BUY 100 TEST @ 3
   //sell_asset nathan 300 BTS 600 TEST 100000 false true    <-- buyer SELL 300 TEST @ 2
   //expected result: 100 TEST filled @0.50, 100 TEST filled @0.25, remainder: 100 TEST offered @0.16667
   // seller is buying TEST selling CORE
   // buyer is selling TEST buying CORE
   BOOST_CHECK_EQUAL( get_balance( seller_account, test_asset ), 150 /*200*/ );
   BOOST_CHECK_EQUAL( get_balance( buyer_account, core_asset ), 198 /*297*/ );
   BOOST_CHECK_EQUAL( core_asset.dynamic_asset_data_id(db).accumulated_fees.value , 2 /*3*/ );
   BOOST_CHECK_EQUAL( get_balance( seller_account, core_asset ), 9800 );
   BOOST_CHECK_EQUAL( get_balance( buyer_account, test_asset ), 9700 );
 }
 catch ( const fc::exception& e )
 {
    elog( "${e}", ("e", e.to_detail_string() ) );
    throw;
 }
}

BOOST_AUTO_TEST_CASE( taker_sells_small_lot_too_low )
{ try {
   INVOKE( issue_uia );
   const asset_object&   core_asset     = get_asset( "TEST" );
   const asset_object&   test_asset     = get_asset( GRAPHENE_SYMBOL );
   const account_object& nathan_account = get_account( "nathan" );
   const account_object& buyer_account  = create_account( "buyer" );
   const account_object& seller_account = create_account( "seller" );

   transfer( committee_account(db), buyer_account, test_asset.amount( 10000 ) );
   transfer( nathan_account, seller_account, core_asset.amount(10000) );

   BOOST_CHECK_EQUAL( get_balance( buyer_account, test_asset ), 10000 );

   limit_order_id_type first_id  = create_sell_order_with_flag( seller_account, core_asset.amount(150), test_asset.amount(100), true )->id;

   BOOST_CHECK_EQUAL( get_balance( seller_account, core_asset ), 9850 );

   auto unmatched = create_sell_order_with_flag( buyer_account, test_asset.amount(11), core_asset.amount(5), false );
   BOOST_CHECK( db.find( first_id ) );
   if( unmatched ) wdump((*unmatched));
   BOOST_CHECK( !unmatched );

   //APM
   //sell_asset nathan 150 BTS 100 TEST 100000 false true    <-- seller BUY 100 TEST @ 1.50 (bts)
   //sell_asset nathan 11 TEST 5 BTS 100000 false true    <-- buyer SELL 11 TEST @ 0.454545
   //expected result: 11 TEST filled @1.5
   // seller is buying TEST selling CORE
   // buyer is selling TEST buying CORE
   BOOST_CHECK_EQUAL( get_balance( seller_account, test_asset ), 11 );
   BOOST_CHECK_EQUAL( get_balance( buyer_account, core_asset ), 16 );
   BOOST_CHECK_EQUAL( core_asset.dynamic_asset_data_id(db).accumulated_fees.value , 0 );
   BOOST_CHECK_EQUAL( get_balance( seller_account, core_asset ), 9850 );
   BOOST_CHECK_EQUAL( get_balance( buyer_account, test_asset ), 9989 );
 }
 catch ( const fc::exception& e )
 {
    elog( "${e}", ("e", e.to_detail_string() ) );
    throw;
 }
}

BOOST_AUTO_TEST_CASE( taker_buys_small_lot_too_high )
{ try {
   INVOKE( issue_uia );
   const asset_object&   core_asset     = get_asset( "TEST" );
   const asset_object&   test_asset     = get_asset( GRAPHENE_SYMBOL );
   const account_object& nathan_account = get_account( "nathan" );
   const account_object& buyer_account  = create_account( "buyer" );
   const account_object& seller_account = create_account( "seller" );

   transfer( committee_account(db), buyer_account, test_asset.amount( 10000 ) );
   transfer( nathan_account, seller_account, core_asset.amount(10000) );

   BOOST_CHECK_EQUAL( get_balance( seller_account, core_asset ), 10000 );

   limit_order_id_type first_id  = create_sell_order_with_flag( buyer_account, test_asset.amount(100), core_asset.amount(80), false )->id;

   BOOST_CHECK_EQUAL( get_balance( buyer_account, test_asset ), 9900 );

   auto unmatched = create_sell_order_with_flag( seller_account, core_asset.amount(15), test_asset.amount(11), true );
   BOOST_CHECK( db.find( first_id ) );
   if( unmatched ) wdump((*unmatched));
   BOOST_CHECK( unmatched );

   //APM
   //sell_asset nathan 100 TEST 80 BTS 100000 false true    <-- buyer SELL 100 TEST @ 0.80 (bts)
   //sell_asset nathan 15 CORE 11 TEST 100000 false true    <-- seller BUY 11 TEST @ 1.363636
   //expected result: 11 TEST filled @0.80
   // buyer is selling TEST buying CORE
   // seller is buying TEST selling CORE
   BOOST_CHECK_EQUAL( get_balance( seller_account, test_asset ), 10 );
   BOOST_CHECK_EQUAL( get_balance( buyer_account, core_asset ), 8 );
   BOOST_CHECK_EQUAL( core_asset.dynamic_asset_data_id(db).accumulated_fees.value , 0 );
   BOOST_CHECK_EQUAL( get_balance( seller_account, core_asset ), 9990 );
   BOOST_CHECK_EQUAL( get_balance( buyer_account, test_asset ), 9900 );
 }
 catch ( const fc::exception& e )
 {
    elog( "${e}", ("e", e.to_detail_string() ) );
    throw;
 }
}

BOOST_AUTO_TEST_CASE( taker_sells_above_1 )
{ try {
   INVOKE( issue_uia );
   const asset_object&   core_asset     = get_asset( "TEST" );
   const asset_object&   test_asset     = get_asset( GRAPHENE_SYMBOL );
   const account_object& nathan_account = get_account( "nathan" );
   const account_object& buyer_account  = create_account( "buyer" );
   const account_object& seller_account = create_account( "seller" );

   transfer( committee_account(db), buyer_account, test_asset.amount( 10000 ) );
   transfer( nathan_account, seller_account, core_asset.amount(10000) );

   BOOST_CHECK_EQUAL( get_balance( buyer_account, test_asset ), 10000 );

   limit_order_id_type first_id  = create_sell_order_with_flag( seller_account, core_asset.amount(400), test_asset.amount(100), true )->id;
   limit_order_id_type second_id = create_sell_order_with_flag( seller_account, core_asset.amount(300), test_asset.amount(100), true )->id;

   BOOST_CHECK_EQUAL( get_balance( seller_account, core_asset ), 9300 );

   auto unmatched = create_sell_order_with_flag( buyer_account, test_asset.amount(300), core_asset.amount(600), true );
   BOOST_CHECK( !db.find( first_id ) );
   BOOST_CHECK( !db.find( second_id ) );
   if( unmatched ) wdump((*unmatched));
   BOOST_CHECK( unmatched );

   //APM
   //sell_asset nathan 400 BTS 100 TEST 100000 false true    <-- seller BUY 100 TEST @ 4 (bts)
   //sell_asset nathan 300 BTS 100 TEST 100000 false true    <-- seller BUY 100 TEST @ 3
   //sell_asset nathan 300 TEST 600 BTS 100000 false true    <-- buyer SELL 300 TEST @ 2
   //expected result: 100 TEST filled @0.50, 100 TEST filled @0.25, remainder: 100 TEST offered @0.16667
   // seller is buying TEST selling CORE
   // buyer is selling TEST buying CORE
   BOOST_CHECK_EQUAL( get_balance( seller_account, test_asset ), 200 );
   BOOST_CHECK_EQUAL( get_balance( buyer_account, core_asset ), 693 );
   BOOST_CHECK_EQUAL( core_asset.dynamic_asset_data_id(db).accumulated_fees.value , 7 );
   BOOST_CHECK_EQUAL( get_balance( seller_account, core_asset ), 9300 );
   BOOST_CHECK_EQUAL( get_balance( buyer_account, test_asset ), 9700 );
 }
 catch ( const fc::exception& e )
 {
    elog( "${e}", ("e", e.to_detail_string() ) );
    throw;
 }
}

BOOST_AUTO_TEST_CASE( taker_sells_below_1 )
{ try {
   INVOKE( issue_uia );
   const asset_object&   core_asset     = get_asset( "TEST" );
   const asset_object&   test_asset     = get_asset( GRAPHENE_SYMBOL );
   const account_object& nathan_account = get_account( "nathan" );
   const account_object& buyer_account  = create_account( "buyer" );
   const account_object& seller_account = create_account( "seller" );

   transfer( committee_account(db), buyer_account, test_asset.amount( 10000 ) );
   transfer( nathan_account, seller_account, core_asset.amount(10000) );

   BOOST_CHECK_EQUAL( get_balance( buyer_account, test_asset ), 10000 );

   limit_order_id_type first_id  = create_sell_order_with_flag( seller_account, core_asset.amount(25), test_asset.amount(100), false )->id;
   limit_order_id_type second_id = create_sell_order_with_flag( seller_account, core_asset.amount(50), test_asset.amount(100), false )->id;

   BOOST_CHECK_EQUAL( get_balance( seller_account, core_asset ), 9925 );

   auto unmatched = create_sell_order_with_flag( buyer_account, test_asset.amount(300), core_asset.amount(50), false );
   BOOST_CHECK( !db.find( first_id ) );
   BOOST_CHECK( !db.find( second_id ) );
   if( unmatched ) wdump((*unmatched));
   BOOST_CHECK( unmatched );

   //APM
   //sell_asset nathan 25 BTS 100 TEST 100000 false true    <-- seller BUY 100 TEST @ 0.25 (bts)
   //sell_asset nathan 50 BTS 100 TEST 100000 false true    <-- seller BUY 100 TEST @ 0.50
   //sell_asset nathan 300 BTS 150 TEST 100000 false true    <-- buyer SELL 300 TEST @0.16667
   //expected result: 100 TEST filled @0.50, 100 TEST filled @0.25, remainder: 100 TEST offered @0.16667
   // seller is buying TEST selling CORE
   // buyer is selling TEST buying CORE
   BOOST_CHECK_EQUAL( get_balance( seller_account, test_asset ), 200 );
   BOOST_CHECK_EQUAL( get_balance( buyer_account, core_asset ), 75 );
   BOOST_CHECK_EQUAL( core_asset.dynamic_asset_data_id(db).accumulated_fees.value , 0 );
   BOOST_CHECK_EQUAL( get_balance( seller_account, core_asset ), 9925 );
   BOOST_CHECK_EQUAL( get_balance( buyer_account, test_asset ), 9700 );
 }
 catch ( const fc::exception& e )
 {
    elog( "${e}", ("e", e.to_detail_string() ) );
    throw;
 }
}

BOOST_AUTO_TEST_CASE( taker_buys_below_1 )
{ try {
   INVOKE( issue_uia );
   const asset_object&   core_asset     = get_asset( "TEST" );
   const asset_object&   test_asset     = get_asset( GRAPHENE_SYMBOL );
   const account_object& nathan_account = get_account( "nathan" );
   const account_object& buyer_account  = create_account( "buyer" );
   const account_object& seller_account = create_account( "seller" );

   transfer( committee_account(db), buyer_account, test_asset.amount( 10000 ) );
   transfer( nathan_account, seller_account, core_asset.amount(10000) );

   BOOST_CHECK_EQUAL( get_balance( seller_account, core_asset ), 10000 );

   limit_order_id_type first_id  = create_sell_order_with_flag( buyer_account, test_asset.amount(100), core_asset.amount(25), false )->id;
   limit_order_id_type second_id = create_sell_order_with_flag( buyer_account, test_asset.amount(100), core_asset.amount(50), false )->id;

   BOOST_CHECK_EQUAL( get_balance( buyer_account, test_asset ), 9800 );

   auto unmatched = create_sell_order_with_flag( seller_account, core_asset.amount(275), test_asset.amount(300), false );
   BOOST_CHECK( !db.find( first_id ) );
   BOOST_CHECK( !db.find( second_id ) );
   if( unmatched ) wdump((*unmatched));
   BOOST_CHECK( unmatched );

   //APM
   //sell_asset nathan 100 TEST 25 BTS 100000 false true    <-- buyer SELL 100 TEST @ 0.25 (bts)
   //sell_asset nathan 100 TEST 50 BTS 100000 false true    <-- buyer SELL 100 TEST @ 0.50
   //sell_asset nathan 275 BTS 300 TEST 100000 false true    <-- seller BUY 275 TEST @0.916667
   //expected result: 100 TEST filled @0.25, 100 TEST filled @0.50, remainder: 100 TEST bid @0.916667
   // buyer is selling TEST buying CORE
   // seller is buying TEST selling CORE
   BOOST_CHECK_EQUAL( get_balance( seller_account, test_asset ), 200 );
   BOOST_CHECK_EQUAL( get_balance( buyer_account, core_asset ), 75 );
   BOOST_CHECK_EQUAL( core_asset.dynamic_asset_data_id(db).accumulated_fees.value , 0 );
   BOOST_CHECK_EQUAL( get_balance( seller_account, core_asset ), 9725 );
   BOOST_CHECK_EQUAL( get_balance( buyer_account, test_asset ), 9800 );
 }
 catch ( const fc::exception& e )
 {
    elog( "${e}", ("e", e.to_detail_string() ) );
    throw;
 }
}

BOOST_AUTO_TEST_CASE( taker_buys_above_1 )
{ try {
   INVOKE( issue_uia );
   const asset_object&   core_asset     = get_asset( "TEST" );
   const asset_object&   test_asset     = get_asset( GRAPHENE_SYMBOL );
   const account_object& nathan_account = get_account( "nathan" );
   const account_object& buyer_account  = create_account( "buyer" );
   const account_object& seller_account = create_account( "seller" );

   transfer( committee_account(db), buyer_account, test_asset.amount( 10000 ) );
   transfer( nathan_account, seller_account, core_asset.amount(10000) );

   BOOST_CHECK_EQUAL( get_balance( seller_account, core_asset ), 10000 );

   limit_order_id_type first_id  = create_sell_order_with_flag( buyer_account, test_asset.amount(100), core_asset.amount(400), true )->id;
   limit_order_id_type second_id = create_sell_order_with_flag( buyer_account, test_asset.amount(100), core_asset.amount(300), true )->id;

   BOOST_CHECK_EQUAL( get_balance( buyer_account, test_asset ), 9800 );

   auto unmatched = create_sell_order_with_flag( seller_account, core_asset.amount(1500), test_asset.amount(300), true );
   BOOST_CHECK( !db.find( first_id ) );
   BOOST_CHECK( !db.find( second_id ) );
   if( unmatched ) wdump((*unmatched));
   BOOST_CHECK( unmatched );

   //APM
   //sell_asset nathan 100 TEST 400 BTS 100000 false true    <-- seller SELL 100 TEST @ 4 (bts)
   //sell_asset nathan 100 TEST 300 BTS 100000 false true    <-- seller SELL 100 TEST @ 3
   //sell_asset nathan 1500 BTS 300 TEST 100000 false true    <-- buyer BUY 300 TEST @ 5
   //expected result: 100 TEST filled @3, 100 TEST filled @4, remainder: 100 TEST bid @5
   // seller is selling TEST buying CORE
   // buyer is buying TEST selling CORE
   BOOST_CHECK_EQUAL( get_balance( seller_account, test_asset ), 200 );
   BOOST_CHECK_EQUAL( get_balance( buyer_account, core_asset ), 693 );
   BOOST_CHECK_EQUAL( core_asset.dynamic_asset_data_id(db).accumulated_fees.value , 7 );
   BOOST_CHECK_EQUAL( get_balance( seller_account, core_asset ), 8800 );
   BOOST_CHECK_EQUAL( get_balance( buyer_account, test_asset ), 9800 );
 }
 catch ( const fc::exception& e )
 {
    elog( "${e}", ("e", e.to_detail_string() ) );
    throw;
 }
}

BOOST_AUTO_TEST_CASE( create_buy_uia_multiple_match_new )
{ try {
   INVOKE( issue_uia );
   const asset_object&   core_asset     = get_asset( "TEST" );
   const asset_object&   test_asset     = get_asset( GRAPHENE_SYMBOL );
   const account_object& nathan_account = get_account( "nathan" );
   const account_object& buyer_account  = create_account( "buyer" );
   const account_object& seller_account = create_account( "seller" );

   transfer( committee_account(db), buyer_account, test_asset.amount( 10000 ) );
   transfer( nathan_account, seller_account, core_asset.amount(10000) );

   BOOST_CHECK_EQUAL( get_balance( buyer_account, test_asset ), 10000 );

   limit_order_id_type first_id  = create_sell_order_with_flag( buyer_account, test_asset.amount(100), core_asset.amount(100), true )->id;
   limit_order_id_type second_id = create_sell_order_with_flag( buyer_account, test_asset.amount(100), core_asset.amount(200), true )->id;
   limit_order_id_type third_id  = create_sell_order_with_flag( buyer_account, test_asset.amount(100), core_asset.amount(300), true )->id;

   BOOST_CHECK_EQUAL( get_balance( buyer_account, test_asset ), 9700 );

   auto unmatched = create_sell_order_with_flag( seller_account, core_asset.amount(300), test_asset.amount(150), true );
   BOOST_CHECK( !db.find( first_id ) );
   BOOST_CHECK( db.find( second_id ) );
   BOOST_CHECK( db.find( third_id ) );
   if( unmatched ) wdump((*unmatched));
   BOOST_CHECK( !unmatched );

   //APM
   //sell_asset nathan 100 TEST 100 BTS 100000 false true    <-- buyer SELL 100 TEST @ 1 (bts)
   //sell_asset nathan 100 TEST 200 BTS 100000 false true    <-- buyer SELL 100 TEST @ 2 (bts)
   //sell_asset nathan 100 TEST 300 BTS 100000 false true    <-- buyer SELL 100 TEST @ 3 (bts)
   //sell_asset nathan 300 BTS 150 TEST 100000 false true    <-- seller BUY 150 TEST @ 2 (bts)
   //expected result: 100 TEST filled @1, 50 TEST filled @2
   // buyer is selling TEST buying CORE
   // seller is buying TEST selling CORE
   BOOST_CHECK_EQUAL( get_balance( seller_account, test_asset ), 150 /*200*/ );
   BOOST_CHECK_EQUAL( get_balance( buyer_account, core_asset ), 198 /*297*/ );
   BOOST_CHECK_EQUAL( core_asset.dynamic_asset_data_id(db).accumulated_fees.value , 2 /*3*/ );
   BOOST_CHECK_EQUAL( get_balance( seller_account, core_asset ), 9800 );
   BOOST_CHECK_EQUAL( get_balance( buyer_account, test_asset ), 9700 );
 }
 catch ( const fc::exception& e )
 {
    elog( "${e}", ("e", e.to_detail_string() ) );
    throw;
 }
}
BOOST_AUTO_TEST_SUITE_END()
