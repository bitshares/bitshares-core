/*
 * Copyright (c) 2018 contributors.
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

#include "../common/database_fixture.hpp"

using namespace graphene::chain;
using namespace graphene::chain::test;

BOOST_FIXTURE_TEST_SUITE(api_limit_tests, database_fixture)

BOOST_AUTO_TEST_CASE( api_limit_get_key_references ){
   try{
   const int num_keys = 210;
   const int num_keys1 = 2;
   vector< private_key_type > numbered_private_keys;
   vector< public_key_type >  numbered_key_id;
   numbered_private_keys.reserve( num_keys );

   graphene::app::application_options opt1 = app.get_options();
   opt1.has_api_helper_indexes_plugin = false;
   graphene::app::database_api db_api1( db, &opt1 );
   BOOST_CHECK_THROW( db_api1.get_key_references(numbered_key_id), fc::exception );

   graphene::app::application_options opt = app.get_options();
   opt.has_api_helper_indexes_plugin = true;
   graphene::app::database_api db_api( db, &opt );

   for( int i=0; i<num_keys1; i++ )
   {
      private_key_type privkey = generate_private_key(std::string("key_") + std::to_string(i));
      public_key_type pubkey = privkey.get_public_key();
      numbered_private_keys.push_back( privkey );
      numbered_key_id.push_back( pubkey );
   }
   vector< flat_set<account_id_type> > final_result=db_api.get_key_references(numbered_key_id);
   BOOST_REQUIRE_EQUAL( final_result.size(), 2u );
   numbered_private_keys.reserve( num_keys );
   for( int i=num_keys1; i<num_keys; i++ )
   {
       private_key_type privkey = generate_private_key(std::string("key_") + std::to_string(i));
       public_key_type pubkey = privkey.get_public_key();
       numbered_private_keys.push_back( privkey );
       numbered_key_id.push_back( pubkey );
   }
   GRAPHENE_CHECK_THROW(db_api.get_key_references(numbered_key_id), fc::exception);
   }catch (fc::exception& e) {
   edump((e.to_detail_string()));
   throw;
   }
}

BOOST_AUTO_TEST_CASE( api_limit_get_full_accounts ) {

   try {
      ACTOR(alice);

      graphene::app::application_options opt = app.get_options();
      opt.has_api_helper_indexes_plugin = true;
      graphene::app::database_api db_api( db, &opt );

      vector<string> accounts;

      for( size_t i = 0; i < 50; ++i )
      {
         string account_name = "testaccount" + fc::to_string(i);
         create_account( account_name );
         accounts.push_back( account_name );
      }
      accounts.push_back( "alice" );

      transfer_operation op;
      op.from = alice_id;
      op.amount = asset(1);
      for( size_t i = 0; i < 501; ++i )
      {
         propose( op, alice_id );
      }

      // Too many accounts
      GRAPHENE_CHECK_THROW(db_api.get_full_accounts(accounts, false), fc::exception);

      accounts.erase(accounts.begin());
      auto full_accounts = db_api.get_full_accounts(accounts, false);
      BOOST_CHECK(full_accounts.size() == 50);

      // The default max list size is 500
      BOOST_REQUIRE( full_accounts.find("alice") != full_accounts.end() );
      BOOST_CHECK_EQUAL( full_accounts["alice"].proposals.size(), 500u );
      BOOST_CHECK( full_accounts["alice"].more_data_available.proposals );
      BOOST_REQUIRE( full_accounts.find("testaccount9") != full_accounts.end() );
      BOOST_CHECK_EQUAL( full_accounts["testaccount9"].proposals.size(), 0 );
      BOOST_CHECK( !full_accounts["testaccount9"].more_data_available.proposals );

      // not an account
      accounts.erase(accounts.begin());
      accounts.push_back("nosuchaccount");

      // non existing accounts will be ignored in the results
      full_accounts = db_api.get_full_accounts(accounts, false);
      BOOST_CHECK(full_accounts.size() == 49);

   } catch (fc::exception& e) {
      edump((e.to_detail_string()));
      throw;
   }
}

BOOST_AUTO_TEST_CASE( api_limit_get_limit_orders ){
   try{
   graphene::app::database_api db_api( db, &( app.get_options() ));
   //account_id_type() do 3 ops
   create_bitasset("USD", account_id_type());
   create_account("dan");
   create_account("bob");
   asset_id_type bit_jmj_id = create_bitasset("JMJBIT").id;
   generate_block();
   fc::usleep(fc::milliseconds(100));
   GRAPHENE_CHECK_THROW(db_api.get_limit_orders(std::string(static_cast<object_id_type>(asset_id_type())),
      std::string(static_cast<object_id_type>(bit_jmj_id)), 370), fc::exception);
   vector<limit_order_object>  limit_orders =db_api.get_limit_orders(std::string(
      static_cast<object_id_type>(asset_id_type())),
      std::string(static_cast<object_id_type>(bit_jmj_id)), 340);
   BOOST_REQUIRE_EQUAL( limit_orders.size(), 0u);

   }catch (fc::exception& e) {
   edump((e.to_detail_string()));
   throw;
   }
}

BOOST_AUTO_TEST_CASE( api_limit_get_limit_orders_by_account ){
   try{
   graphene::app::database_api db_api( db, &( app.get_options() ));
   const auto& test = create_user_issued_asset("TESTASSET");
   create_sell_order( account_id_type(), asset(1,asset_id_type()), test.amount(1) );
   GRAPHENE_CHECK_THROW(db_api.get_limit_orders_by_account(
      std::string(static_cast<object_id_type>(account_id_type())), 160), fc::exception);
   vector<limit_order_object>  limit_orders =db_api.get_limit_orders_by_account(
      std::string(static_cast<object_id_type>(account_id_type())), 145);
   BOOST_REQUIRE_EQUAL( limit_orders.size(), 1u);

   }catch (fc::exception& e) {
   edump((e.to_detail_string()));
   throw;
   }
}

BOOST_AUTO_TEST_CASE( api_limit_get_call_orders ){
   try{
   graphene::app::database_api db_api( db, &( app.get_options() ));
   //account_id_type() do 3 ops
   auto nathan_private_key = generate_private_key("nathan");
   account_id_type nathan_id = create_account("nathan", nathan_private_key.get_public_key()).id;
   transfer(account_id_type(), nathan_id, asset(100));
   asset_id_type bitusd_id = create_bitasset(
         "USDBIT", nathan_id, 100, disable_force_settle).id;
   generate_block();
   fc::usleep(fc::milliseconds(100));
   BOOST_CHECK( bitusd_id(db).is_market_issued() );
   GRAPHENE_CHECK_THROW(db_api.get_call_orders(std::string(static_cast<object_id_type>(bitusd_id)),
         370), fc::exception);
   vector< call_order_object>  call_order =db_api.get_call_orders(std::string(
         static_cast<object_id_type>(bitusd_id)), 340);
   BOOST_REQUIRE_EQUAL( call_order.size(), 0u);
   }catch (fc::exception& e) {
   edump((e.to_detail_string()));
   throw;
   }
}
BOOST_AUTO_TEST_CASE( api_limit_get_settle_orders ){
   try{
   graphene::app::database_api db_api( db, &( app.get_options() ));
   //account_id_type() do 3 ops
   auto nathan_private_key = generate_private_key("nathan");
   account_id_type nathan_id = create_account("nathan", nathan_private_key.get_public_key()).id;
   transfer(account_id_type(), nathan_id, asset(100));
   asset_id_type bitusd_id = create_bitasset(
         "USDBIT", nathan_id, 100, disable_force_settle).id;
   generate_block();
   fc::usleep(fc::milliseconds(100));
   GRAPHENE_CHECK_THROW(db_api.get_settle_orders(
         std::string(static_cast<object_id_type>(bitusd_id)), 370), fc::exception);
   vector<force_settlement_object> result =db_api.get_settle_orders(
         std::string(static_cast<object_id_type>(bitusd_id)), 340);
   BOOST_REQUIRE_EQUAL( result.size(), 0u);
   }catch (fc::exception& e) {
   edump((e.to_detail_string()));
   throw;
   }
}
BOOST_AUTO_TEST_CASE( api_limit_get_order_book ){
   try{
   graphene::app::database_api db_api( db, &( app.get_options() ));
   auto nathan_private_key = generate_private_key("nathan");
   auto dan_private_key = generate_private_key("dan");
   account_id_type nathan_id = create_account("nathan", nathan_private_key.get_public_key()).id;
   account_id_type dan_id = create_account("dan", dan_private_key.get_public_key()).id;
   transfer(account_id_type(), nathan_id, asset(100));
   transfer(account_id_type(), dan_id, asset(100));
   asset_id_type bitusd_id = create_bitasset(
         "USDBIT", nathan_id, 100, disable_force_settle).id;
   asset_id_type bitdan_id = create_bitasset(
         "DANBIT", dan_id, 100, disable_force_settle).id;
   generate_block();
   fc::usleep(fc::milliseconds(100));
   GRAPHENE_CHECK_THROW(db_api.get_order_book(std::string(static_cast<object_id_type>(bitusd_id)),
         std::string(static_cast<object_id_type>(bitdan_id)),89), fc::exception);
   graphene::app::order_book result =db_api.get_order_book(std::string(
         static_cast<object_id_type>(bitusd_id)), std::string(static_cast<object_id_type>(bitdan_id)),78);
   BOOST_REQUIRE_EQUAL( result.bids.size(), 0u);
   }catch (fc::exception& e) {
   edump((e.to_detail_string()));
   throw;
   }
}

BOOST_AUTO_TEST_CASE( api_limit_lookup_accounts ) {
   try{
      graphene::app::database_api db_api( db, &( app.get_options() ));
      ACTOR(bob);
      GRAPHENE_CHECK_THROW(db_api.lookup_accounts("bob",220), fc::exception);
      map<string,account_id_type> result =db_api.lookup_accounts("bob",190);
      BOOST_REQUIRE_EQUAL( result.size(), 17u);

   } catch (fc::exception& e) {
      edump((e.to_detail_string()));
      throw;
   }
}

BOOST_AUTO_TEST_CASE( api_limit_lookup_witness_accounts ) {
   try{
      graphene::app::database_api db_api( db, &( app.get_options() ));
      ACTORS((bob)) ;
      GRAPHENE_CHECK_THROW(db_api.lookup_witness_accounts("bob",220), fc::exception);
      map<string, witness_id_type> result =db_api.lookup_witness_accounts("bob",190);
      BOOST_REQUIRE_EQUAL( result.size(), 10u);

   } catch (fc::exception& e) {
      edump((e.to_detail_string()));
      throw;
   }
}
BOOST_AUTO_TEST_CASE( api_limit_get_full_accounts2 ) {

   try {
      ACTOR(alice);

      graphene::app::application_options opt = app.get_options();
      opt.has_api_helper_indexes_plugin = true;
      graphene::app::database_api db_api( db, &opt );

      vector<string> accounts;
      for (int i=0; i<200; i++) {
         std::string acct_name = "mytempacct" + std::to_string(i);
         const account_object& account_name=create_account(acct_name);
         accounts.push_back(account_name.name);
      }
      accounts.push_back( "alice" );

      transfer_operation op;
      op.from = alice_id;
      op.amount = asset(1);
      for( size_t i = 0; i < 501; ++i )
      {
         propose( op, alice_id );
      }

      GRAPHENE_CHECK_THROW(db_api.get_full_accounts(accounts, false), fc::exception);
      accounts.erase(accounts.begin());
      auto full_accounts = db_api.get_full_accounts(accounts, false);
      BOOST_REQUIRE_EQUAL(full_accounts.size(), 200u);

      // The updated max list size is 120
      BOOST_REQUIRE( full_accounts.find("alice") != full_accounts.end() );
      BOOST_CHECK_EQUAL( full_accounts["alice"].proposals.size(), 120u );
      BOOST_CHECK( full_accounts["alice"].more_data_available.proposals );
      BOOST_REQUIRE( full_accounts.find("mytempacct9") != full_accounts.end() );
      BOOST_CHECK_EQUAL( full_accounts["mytempacct9"].proposals.size(), 0 );
      BOOST_CHECK( !full_accounts["mytempacct9"].more_data_available.proposals );

   } catch (fc::exception& e) {
      edump((e.to_detail_string()));
      throw;
   }
}
BOOST_AUTO_TEST_CASE(api_limit_get_withdraw_permissions_by_recipient){
   try{
      graphene::app::database_api db_api( db, &app.get_options());
      ACTORS((bob)) ;
      withdraw_permission_id_type withdraw_permission;
      GRAPHENE_CHECK_THROW(db_api.get_withdraw_permissions_by_recipient(
         "bob",withdraw_permission, 251), fc::exception);
      vector<withdraw_permission_object> result =db_api.get_withdraw_permissions_by_recipient(
         "bob",withdraw_permission,250);
      BOOST_REQUIRE_EQUAL( result.size(), 0u);
   }catch (fc::exception& e) {
      edump((e.to_detail_string()));
      throw;
   }
}
BOOST_AUTO_TEST_CASE(api_limit_get_withdraw_permissions_by_giver){
   try{
      graphene::app::database_api db_api( db, &app.get_options());
      ACTORS((bob)) ;
      withdraw_permission_id_type withdraw_permission;
      GRAPHENE_CHECK_THROW(db_api.get_withdraw_permissions_by_giver(
         "bob",withdraw_permission, 251), fc::exception);
      vector<withdraw_permission_object> result =db_api.get_withdraw_permissions_by_giver(
         "bob",withdraw_permission,250);
      BOOST_REQUIRE_EQUAL( result.size(), 0u);
   }catch (fc::exception& e) {
      edump((e.to_detail_string()));
      throw;
   }
}
BOOST_AUTO_TEST_CASE(api_limit_get_trade_history_by_sequence){
   try{
      app.enable_plugin("market_history");
      graphene::app::application_options opt=app.get_options();
      opt.has_market_history_plugin = true;
      graphene::app::database_api db_api( db, &opt);
      const auto& bitusd = create_bitasset("USDBIT");
      asset_id_type asset_1, asset_2;
      asset_1 = bitusd.id;
      asset_2 = asset_id_type();
      GRAPHENE_CHECK_THROW(db_api.get_trade_history_by_sequence(
         std::string( static_cast<object_id_type>(asset_1)),
         std::string( static_cast<object_id_type>(asset_2)),
         0,fc::time_point_sec(), 251), fc::exception);
      vector<graphene::app::market_trade> result =db_api.get_trade_history_by_sequence(
         std::string( static_cast<object_id_type>(asset_1)),
         std::string( static_cast<object_id_type>(asset_2)),
         0,fc::time_point_sec(),250);
      BOOST_REQUIRE_EQUAL( result.size(), 0u);
   }catch (fc::exception& e) {
      edump((e.to_detail_string()));
      throw;
   }
}

BOOST_AUTO_TEST_CASE(api_limit_get_trade_history){
   try{
      app.enable_plugin("market_history");
      graphene::app::application_options opt=app.get_options();
      opt.has_market_history_plugin = true;
      graphene::app::database_api db_api( db, &opt);
      const auto& bitusd = create_bitasset("USDBIT");
      asset_id_type asset_1, asset_2;
      asset_1 = bitusd.id;
      asset_2 = asset_id_type();
      GRAPHENE_CHECK_THROW(db_api.get_trade_history(
                              std::string( static_cast<object_id_type>(asset_1)),
                              std::string( static_cast<object_id_type>(asset_2)),
                              fc::time_point_sec(),fc::time_point_sec(),
                              251), fc::exception);
      vector<graphene::app::market_trade> result =db_api.get_trade_history(
         std::string( static_cast<object_id_type>(asset_1)),
         std::string( static_cast<object_id_type>(asset_2)),
         fc::time_point_sec(),fc::time_point_sec(),250);
      BOOST_REQUIRE_EQUAL( result.size(), 0u);
   }catch (fc::exception& e) {
      edump((e.to_detail_string()));
      throw;
   }
}
BOOST_AUTO_TEST_CASE(api_limit_get_top_markets){
   try{
      app.enable_plugin("market_history");
      graphene::app::application_options opt=app.get_options();
      opt.has_market_history_plugin = true;
      graphene::app::database_api db_api( db, &opt);
      const auto& bitusd = create_bitasset("USDBIT");
      asset_id_type asset_1, asset_2;
      asset_1 = bitusd.id;
      asset_2 = asset_id_type();
      GRAPHENE_CHECK_THROW(db_api.get_top_markets(251), fc::exception);
      vector<graphene::app::market_ticker> result =db_api.get_top_markets(250);
      BOOST_REQUIRE_EQUAL( result.size(), 0u);
   }catch (fc::exception& e) {
      edump((e.to_detail_string()));
      throw;
   }
}
BOOST_AUTO_TEST_CASE(api_limit_get_collateral_bids) {
   try {
      graphene::app::database_api db_api( db, &( app.get_options() ));

      int64_t init_balance = 10000;
      ///account_id_type borrower, borrower2, feedproducer;
      asset_id_type swan, back;
      ACTORS((borrower) (borrower2) (feedproducer)) ;
      const auto& bitusd = create_bitasset("USDBIT", feedproducer_id);
      swan = bitusd.id;
      back = asset_id_type();
      update_feed_producers(swan(db), {feedproducer_id});
      transfer(committee_account, borrower_id, asset(init_balance));
      transfer(committee_account, borrower2_id, asset(init_balance));

      generate_blocks( HARDFORK_CORE_216_TIME );
      generate_block();

      price_feed feed;
      feed.maintenance_collateral_ratio = 1750; // need to set this explicitly, testnet has a different default
      feed.settlement_price = swan(db).amount(1) / back(db).amount(1);
      publish_feed(swan(db), feedproducer_id(db), feed);
      // start out with 2:1 collateral
      borrow(borrower_id(db), swan(db).amount(10), back(db).amount(2*10));
      borrow(borrower2_id(db), swan(db).amount(10), back(db).amount(4*10));
      //feed 1: 2
      feed.settlement_price = swan(db).amount(1) / back(db).amount(2);
      publish_feed(swan(db), feedproducer_id(db), feed);

      // this sell order is designed to trigger a black swan

      create_sell_order( borrower2_id(db), swan(db).amount(1), back(db).amount(3) );
      BOOST_CHECK( swan(db).bitasset_data(db).has_settlement() );
      //making 3 collateral bids
      for (int i=0; i<3; i++) {

         std::string acct_name = "mytempacct" + std::to_string(i);
         account_id_type account_id=create_account(acct_name).id;
         transfer(committee_account, account_id, asset(init_balance));
         bid_collateral(account_id(db), back(db).amount(10), swan(db).amount(1));
      }
      auto swan_symbol = swan(db).symbol;


      //validating normal case; total_bids =3 ; result_bids=3
      vector<collateral_bid_object> result_bids = db_api.get_collateral_bids(swan_symbol, 250, 0);
      BOOST_CHECK_EQUAL( 3u, result_bids.size() );

      //verify skip /// inefficient code test
      //skip < total_bids; skip=1; total_bids =3 ; result_bids=2
      result_bids = db_api.get_collateral_bids(swan_symbol, 250, 1);
      BOOST_CHECK_EQUAL( 2u, result_bids.size() );
      //skip= total_bids; skip=3; total_bids =3 ; result_bids=0
      result_bids = db_api.get_collateral_bids(swan_symbol, 250, 3);
      BOOST_CHECK_EQUAL( 0u, result_bids.size() );
      //skip> total_bids; skip=4; total_bids =3 ; result_bids=0
      result_bids = db_api.get_collateral_bids(swan_symbol, 250, 4);
      BOOST_CHECK_EQUAL( 0u, result_bids.size() );

      //verify limit // inefficient code test
      //limit= api_limit
      for (int i=3; i<255; i++) {
         std::string acct_name = "mytempacct" + std::to_string(i);
         account_id_type account_id=create_account(acct_name).id;
         transfer(committee_account, account_id, asset(init_balance));
         bid_collateral(account_id(db), back(db).amount(10), swan(db).amount(1));
      }
      result_bids=db_api.get_collateral_bids(swan_symbol, 250, 0);
      BOOST_CHECK_EQUAL( 250u, result_bids.size() );
      //limit> api_limit throw error
      GRAPHENE_CHECK_THROW(db_api.get_collateral_bids(swan_symbol, 253, 3), fc::exception);

   }
   catch (fc::exception& e) {
      edump((e.to_detail_string()));
      throw;
   }
}
BOOST_AUTO_TEST_CASE(api_limit_get_account_limit_orders) {
   try {
      graphene::app::database_api db_api( db, &( app.get_options() ));
      ACTORS((seller));
      const auto &bitcny = create_bitasset("CNY");
      const auto &core = asset_id_type()(db);

      int64_t init_balance(10000000);
      transfer(committee_account, seller_id, asset(init_balance));

      /// Create  versatile orders
      for (size_t i = 0; i < 250; ++i) {
         BOOST_CHECK(create_sell_order(seller, core.amount(100), bitcny.amount(250+i)));
      }


      std::vector<limit_order_object> results=db_api.get_account_limit_orders(seller.name, GRAPHENE_SYMBOL, "CNY",250);
      BOOST_REQUIRE_EQUAL( results.size(), 250u);
      GRAPHENE_CHECK_THROW( db_api.get_account_limit_orders(seller.name, GRAPHENE_SYMBOL, "CNY",251), fc::exception);

   }
   catch (fc::exception& e) {
      edump((e.to_detail_string()));
      throw;
   }
}
BOOST_AUTO_TEST_CASE( api_limit_lookup_vote_ids ) {
   try{
      graphene::app::database_api db_api( db, &( app.get_options() ));
      ACTORS( (connie)(whitney)(wolverine) );
      fund(connie);
      upgrade_to_lifetime_member(connie);
      fund(whitney);
      upgrade_to_lifetime_member(whitney);
      fund(wolverine);
      upgrade_to_lifetime_member(wolverine);
      const auto& committee = create_committee_member( connie );
      const auto& witness = create_witness( whitney );
      const auto& worker = create_worker( wolverine_id );
      std::vector<vote_id_type> votes;
      votes.push_back( committee.vote_id );
      votes.push_back( witness.vote_id );
      const auto results = db_api.lookup_vote_ids( votes );
      BOOST_REQUIRE_EQUAL( results.size(), 2u);
      votes.push_back( worker.vote_for );
      GRAPHENE_CHECK_THROW(db_api.lookup_vote_ids(votes), fc::exception);

   } catch (fc::exception& e) {
      edump((e.to_detail_string()));
      throw;
   }
}

BOOST_AUTO_TEST_CASE( api_limit_lookup_committee_member_accounts ) {
   try{
      graphene::app::database_api db_api( db, &( app.get_options() ));
      ACTORS((bob));
      GRAPHENE_CHECK_THROW(db_api.lookup_committee_member_accounts("bob",220), fc::exception);
      std::map<std::string, committee_member_id_type>  result =db_api.lookup_committee_member_accounts("bob",190);
      BOOST_REQUIRE_EQUAL( result.size(), 10u);

   } catch (fc::exception& e) {
      edump((e.to_detail_string()));
      throw;
   }
}

BOOST_AUTO_TEST_SUITE_END()
