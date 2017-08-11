/*
 * Copyright (c) 2015 Cryptonomex, Inc., and contributors.
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

#define BOOST_TEST_MODULE Test Application
#include <boost/test/included/unit_test.hpp>

#include <graphene/chain/database.hpp>
#include <graphene/chain/exceptions.hpp>
#include <graphene/chain/hardfork.hpp>

#include <graphene/chain/account_object.hpp>
#include <graphene/chain/asset_object.hpp>
#include <graphene/chain/committee_member_object.hpp>
#include <graphene/chain/market_object.hpp>
#include <graphene/chain/vesting_balance_object.hpp>
#include <graphene/chain/withdraw_permission_object.hpp>
#include <graphene/chain/witness_object.hpp>

#include <fc/crypto/digest.hpp>

#include "../common/database_fixture.hpp"

using namespace graphene::chain;
using namespace graphene::chain::test;

BOOST_FIXTURE_TEST_SUITE( operation_tests, database_fixture )

//exec: /home/vm/bitshares-core/tests/optest
//workdir: /home/vm/bitshares-core/tests/


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
	return;
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

   //print_market( "", "" );
   auto unmatched = create_sell_order( buyer_account, test_asset.amount(100), core_asset.amount(100) );
   //print_market( "", "" );
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

   //print_market( "", "" );
   auto unmatched = create_sell_order_with_flag( buyer_account, test_asset.amount(11), core_asset.amount(5), false );
   //print_market( "", "" );
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

   //print_market( "", "" );
   auto unmatched = create_sell_order_with_flag( seller_account, core_asset.amount(15), test_asset.amount(11), true );
   //print_market( "", "" );
   BOOST_CHECK( db.find( first_id ) );
   if( unmatched ) wdump((*unmatched));
   BOOST_CHECK( !unmatched );

   //APM
   //sell_asset nathan 100 TEST 80 BTS 100000 false true    <-- buyer SELL 100 TEST @ 0.80 (bts)
   //sell_asset nathan 15 CORE 11 TEST 100000 false true    <-- seller BUY 11 TEST @ 1.363636
   //expected result: 11 TEST filled @0.80
   // buyer is selling TEST buying CORE
   // seller is buying TEST selling CORE
   BOOST_CHECK_EQUAL( get_balance( seller_account, test_asset ), 11 );
   BOOST_CHECK_EQUAL( get_balance( buyer_account, core_asset ), 9 );
   BOOST_CHECK_EQUAL( core_asset.dynamic_asset_data_id(db).accumulated_fees.value , 0 );
   BOOST_CHECK_EQUAL( get_balance( seller_account, core_asset ), 9991 );
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

   //print_market( "", "" );
   auto unmatched = create_sell_order_with_flag( buyer_account, test_asset.amount(300), core_asset.amount(600), true );
   //print_market( "", "" );
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

   //print_market( "", "" );
   auto unmatched = create_sell_order_with_flag( buyer_account, test_asset.amount(300), core_asset.amount(50), false );
   //print_market( "", "" );
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

   //print_market( "", "" );
   auto unmatched = create_sell_order_with_flag( seller_account, core_asset.amount(275), test_asset.amount(300), false );
   //print_market( "", "" );
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

   //print_market( "", "" );
   auto unmatched = create_sell_order_with_flag( seller_account, core_asset.amount(1500), test_asset.amount(300), true );
   //print_market( "", "" );
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

   //print_market( "", "" );
   auto unmatched = create_sell_order_with_flag( seller_account, core_asset.amount(300), test_asset.amount(150), true );
   //print_market( "", "" );
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


BOOST_AUTO_TEST_CASE( whalehole_test_1 )
{ try {
      ACTORS((buyer)(seller)(borrower)(borrower2)(feedproducer));

      int64_t init_balance(1000000);
      transfer(committee_account, buyer_id, asset(init_balance));

      transfer(committee_account, seller_id, asset(init_balance));

      asset_id_type core_id = db.get_index<asset_object>().get_next_id();
      asset_create_operation creator;
      creator.issuer = account_id_type();  //buyer.get_id();
      creator.fee = asset();
      creator.symbol = "CORE";
      creator.common_options.max_supply = 100000000;
      creator.precision = 8;
      creator.common_options.market_fee_percent = GRAPHENE_MAX_MARKET_FEE_PERCENT/100; /*1%*/
      //creator.common_options.issuer_permissions = charge_market_fee|white_list|override_authority|transfer_restricted|disable_confidential;
      //creator.common_options.flags = charge_market_fee|white_list|override_authority|disable_confidential;
      creator.common_options.core_exchange_rate = price({asset(2),asset(1,asset_id_type(1))});
      //creator.common_options.whitelist_authorities.insert(buyer);
	  //creator.common_options.whitelist_authorities.insert(seller);
	  //creator.common_options.whitelist_authorities = creator.common_options.blacklist_authorities = {account_id_type()};
      trx.operations.push_back(std::move(creator));
      PUSH_TX( db, trx, ~0 );
	  trx.clear();

	  const asset_object& core = core_id(db);

      asset_id_type whalehole_id = db.get_index<asset_object>().get_next_id();
      //asset_create_operation creator;
	  creator.issuer = account_id_type();  //buyer.get_id();
      creator.fee = asset();
      creator.symbol = "WHALEHOLE";
      creator.common_options.max_supply = 100000000;
      creator.precision = 0;
      creator.common_options.market_fee_percent = GRAPHENE_MAX_MARKET_FEE_PERCENT/100; /*1%*/
      //creator1.common_options.issuer_permissions = charge_market_fee|white_list|override_authority|transfer_restricted|disable_confidential;
      //creator1.common_options.flags = charge_market_fee|white_list|override_authority|disable_confidential;
      creator.common_options.core_exchange_rate = price({asset(2),asset(1,asset_id_type(1))});
      //creator1.common_options.whitelist_authorities.insert(buyer);
	  //creator1.common_options.whitelist_authorities.insert(seller);
	  //creator1.common_options.whitelist_authorities = creator1.common_options.blacklist_authorities = {account_id_type()};
	  trx.operations.push_back(std::move(creator));
      PUSH_TX( db, trx, ~0 );
	  trx.clear();
	  
	  const asset_object& whalehole = whalehole_id(db);

      asset_id_type mole_id = db.get_index<asset_object>().get_next_id();
      //asset_create_operation creator;
	  creator.issuer = account_id_type();  //buyer.get_id();
      creator.fee = asset();
      creator.symbol = "MOLE";
      creator.common_options.max_supply = 100000000;
      creator.precision = 4;
      creator.common_options.market_fee_percent = GRAPHENE_MAX_MARKET_FEE_PERCENT/100; /*1%*/
      //creator1.common_options.issuer_permissions = charge_market_fee|white_list|override_authority|transfer_restricted|disable_confidential;
      //creator1.common_options.flags = charge_market_fee|white_list|override_authority|disable_confidential;
      creator.common_options.core_exchange_rate = price({asset(2),asset(1,asset_id_type(1))});
      //creator1.common_options.whitelist_authorities.insert(buyer);
	  //creator1.common_options.whitelist_authorities.insert(seller);
	  //creator1.common_options.whitelist_authorities = creator1.common_options.blacklist_authorities = {account_id_type()};
	  trx.operations.push_back(std::move(creator));
      PUSH_TX( db, trx, ~0 );
	  trx.clear();

	  const asset_object& mole = mole_id(db);

      //const auto& bitusd = create_bitasset("USDBIT", feedproducer_id);
      //const auto& test  = asset_id_type()(db);



      asset usd_pays, usd_receives, core_pays, core_receives;
	  price match_price;
	  //price ask_core = price(asset(80,whalehole_id), asset(480,core_id));
	  //price bid_usd = price(asset(21,core_id), asset(2,whalehole_id));	
	
	// bid-taker
	limit_order_object ask_core = limit_order_object();
      ask_core.seller = seller.get_id();
	  ask_core.sell_price = price(asset(80,whalehole_id), asset(480,core_id));
	  ask_core.for_sale = 80;	
	limit_order_object bid_usd = limit_order_object();
	  bid_usd.seller = buyer.get_id();
	  bid_usd.sell_price = price(asset(21,core_id), asset(2,whalehole_id));
	  bid_usd.for_sale = 21;
	match_price = ask_core.sell_price;

	// ask-taker
	/*limit_order_object bid_usd = limit_order_object();
	  bid_usd.seller = seller.get_id();
	  bid_usd.sell_price = price(asset(80,whalehole_id), asset(480,core_id));
	  bid_usd.for_sale = 80;
	limit_order_object ask_core = limit_order_object();
	  ask_core.seller = buyer.get_id();
	  ask_core.sell_price = price(asset(21,core_id), asset(2,whalehole_id));
	  ask_core.for_sale = 21;
	match_price = bid_usd.sell_price;*/


     auto usd_for_sale = bid_usd.amount_for_sale();
     auto core_for_sale = ask_core.amount_for_sale();

	double real_match_price = match_price.to_real();
	double real_order_price = bid_usd.sell_price.to_real();
	double real_book_price = ask_core.sell_price.to_real();
	int64_t usd_max_counter_size = round(bid_usd.amount_to_receive().amount.value / real_book_price);
	if (usd_max_counter_size < usd_for_sale.amount) usd_for_sale.amount = usd_max_counter_size;
	int64_t core_max_counter_size = round(ask_core.amount_to_receive().amount.value * real_book_price);
	if (core_max_counter_size < core_for_sale.amount) core_for_sale.amount = core_max_counter_size;
	
	idump((usd_max_counter_size));
	idump((usd_for_sale));

    idump((core_max_counter_size));
	idump((core_for_sale));

	idump((ask_core));
	idump((bid_usd));
	idump((match_price));

   if( usd_for_sale <= core_for_sale * match_price )
   {
      core_receives = usd_for_sale;
      usd_receives  = usd_for_sale * match_price;
   }
   else
   {
      //This line once read: assert( core_for_sale < usd_for_sale * match_price );
      //This assert is not always true -- see trade_amount_equals_zero in operation_tests.cpp
      //Although usd_for_sale is greater than core_for_sale * match_price, core_for_sale == usd_for_sale * match_price
      //Removing the assert seems to be safe -- apparently no asset is created or destroyed.
      usd_receives = core_for_sale;
      core_receives = core_for_sale * match_price;
   }
   core_pays = usd_receives;
   usd_pays  = core_receives;
	  
   idump((core_receives));
   idump((usd_receives));
   idump((core_pays));
   idump((usd_pays));
  
   bool result = false;

   //step 1 is in match(), step 2 is in fill_order()

   double real_taker_price = bid_usd.sell_price.to_real();
   int64_t real_taker_over = round(usd_receives.amount.value * real_taker_price) - usd_pays.amount.value;
   bid_usd.for_sale -= usd_pays.amount;
   ask_core.for_sale -= core_pays.amount;
   if (real_taker_over > 0) bid_usd.for_sale -= real_taker_over;
   
   idump((bid_usd));
   idump((ask_core));

   //try { result |= db.fill_order( bid_usd, usd_pays, usd_receives, false ); } catch(...) {}
   //try { result |= db.fill_order( ask_core, core_pays, core_receives, true ) << 1; } catch(...) {}

	  
	  issue_uia(buyer, core.amount(10000000));
	  issue_uia(buyer, whalehole.amount(10000));
	  issue_uia(buyer, mole.amount(10000000));
	  issue_uia(seller, core.amount(10000000));
	  issue_uia(seller, whalehole.amount(10000));
	  issue_uia(seller, mole.amount(10000000));


      auto order = create_sell_order( buyer, asset(925,core_id), asset(1,mole_id));
      order = create_sell_order( seller, asset(950,mole_id), asset(1,core_id));
	  order = create_sell_order( seller, asset(9393000,mole_id), asset(101,core_id));

      //auto order = create_sell_order( seller, asset(480,core_id), asset(80,whalehole_id));
      //order = create_sell_order( buyer, asset(4,whalehole_id), asset(20,core_id));
	  //order = create_sell_order( buyer, asset(2,whalehole_id), asset(17,core_id));
      order = create_sell_order( seller, asset(80,whalehole_id), asset(480,core_id));
      order = create_sell_order( buyer, asset(20,core_id), asset(4,whalehole_id));
	  order = create_sell_order( buyer, asset(17,core_id), asset(2,whalehole_id));
	  order = create_sell_order( buyer, asset(21,core_id), asset(2,whalehole_id));
      
	  order = create_sell_order( seller, asset(2,whalehole_id), asset(20,core_id));
	  order = create_sell_order( seller, asset(5,whalehole_id), asset(20,core_id));


	  ilog("done!");

   } catch( const fc::exception& e) {
      edump((e.to_detail_string()));
      throw;
   }
}



// TODO:  Write linear VBO tests

BOOST_AUTO_TEST_SUITE_END()

