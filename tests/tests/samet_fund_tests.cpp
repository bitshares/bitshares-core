/*
 * Copyright (c) 2021 Abit More, and contributors.
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
#include <graphene/chain/samet_fund_object.hpp>
#include <graphene/chain/proposal_object.hpp>

#include <graphene/app/api.hpp>

#include <boost/test/unit_test.hpp>

using namespace graphene::chain;
using namespace graphene::chain::test;

BOOST_FIXTURE_TEST_SUITE( samet_fund_tests, database_fixture )

BOOST_AUTO_TEST_CASE( samet_fund_hardfork_time_test )
{
   try {

      // Proceeds to a recent hard fork
      generate_blocks( HARDFORK_CORE_2262_TIME );
      generate_block();
      set_expiration( db, trx );

      ACTORS((sam));

      auto init_amount = 10000000 * GRAPHENE_BLOCKCHAIN_PRECISION;
      fund( sam, asset(init_amount) );

      const asset_object& core = asset_id_type()(db);

      // Before the hard fork, unable to create a samet fund or transact against a samet fund,
      // or do any of them with proposals
      BOOST_CHECK_THROW( create_samet_fund( sam_id, core.id, 10000, 100 ), fc::exception );

      samet_fund_id_type tmp_sf_id;
      BOOST_CHECK_THROW( delete_samet_fund( sam_id, tmp_sf_id ), fc::exception );
      BOOST_CHECK_THROW( update_samet_fund( sam_id, tmp_sf_id, core.amount(100), 200 ), fc::exception );
      BOOST_CHECK_THROW( borrow_from_samet_fund( sam_id, tmp_sf_id, core.amount(100) ), fc::exception );
      BOOST_CHECK_THROW( repay_to_samet_fund( sam_id, tmp_sf_id, core.amount(100), core.amount(100) ),
                         fc::exception );

      samet_fund_create_operation cop = make_samet_fund_create_op( sam_id, core.id, 10000, 100 );
      BOOST_CHECK_THROW( propose( cop ), fc::exception );

      samet_fund_delete_operation dop = make_samet_fund_delete_op( sam_id, tmp_sf_id );
      BOOST_CHECK_THROW( propose( dop ), fc::exception );

      samet_fund_update_operation uop = make_samet_fund_update_op( sam_id, tmp_sf_id, core.amount(100), 200 );
      BOOST_CHECK_THROW( propose( uop ), fc::exception );

      samet_fund_borrow_operation bop = make_samet_fund_borrow_op( sam_id, tmp_sf_id, core.amount(100) );
      BOOST_CHECK_THROW( propose( bop ), fc::exception );

      samet_fund_repay_operation rop =
                         make_samet_fund_repay_op( sam_id, tmp_sf_id, core.amount(100), core.amount(100) );
      BOOST_CHECK_THROW( propose( rop ), fc::exception );

   } catch (fc::exception& e) {
      edump((e.to_detail_string()));
      throw;
   }
}

BOOST_AUTO_TEST_CASE( samet_fund_crud_and_proposal_test )
{ try {

      // Pass the hard fork time
      generate_blocks( HARDFORK_CORE_2351_TIME );
      set_expiration( db, trx );

      ACTORS((sam)(ted)(por));

      auto init_amount = 10000000 * GRAPHENE_BLOCKCHAIN_PRECISION;
      fund( sam, asset(init_amount) );
      fund( ted, asset(init_amount) );

      const asset_object& core = asset_id_type()(db);
      asset_id_type core_id;

      const asset_object& usd = create_user_issued_asset( "MYUSD" );
      asset_id_type usd_id = usd.id;
      issue_uia( sam, usd.amount(init_amount) );
      issue_uia( ted, usd.amount(init_amount) );

      const asset_object& eur = create_user_issued_asset( "MYEUR", sam, white_list );
      asset_id_type eur_id = eur.id;
      issue_uia( sam, eur.amount(init_amount) );
      issue_uia( ted, eur.amount(init_amount) );
      // Make a whitelist
      {
         BOOST_TEST_MESSAGE( "Setting up whitelisting" );
         asset_update_operation uop;
         uop.asset_to_update = eur.id;
         uop.issuer = sam_id;
         uop.new_options = eur.options;
         // The whitelist is managed by Sam
         uop.new_options.whitelist_authorities.insert(sam_id);
         trx.operations.clear();
         trx.operations.push_back(uop);
         PUSH_TX( db, trx, ~0 );

         // Upgrade Sam so that he can manage the whitelist
         upgrade_to_lifetime_member( sam_id );

         // Add Sam to the whitelist, but do not add others
         account_whitelist_operation wop;
         wop.authorizing_account = sam_id;
         wop.account_to_list = sam_id;
         wop.new_listing = account_whitelist_operation::white_listed;
         trx.operations.clear();
         trx.operations.push_back(wop);
         PUSH_TX( db, trx, ~0 );
      }

      asset_id_type no_asset_id( core.id + 100 );
      BOOST_REQUIRE( !db.find( no_asset_id ) );

      // Able to propose
      {
         samet_fund_create_operation cop = make_samet_fund_create_op( sam_id, core.id, 10000, 100 );
         propose( cop );

         samet_fund_id_type tmp_sf_id;

         samet_fund_delete_operation dop = make_samet_fund_delete_op( sam_id, tmp_sf_id );
         propose( dop );

         samet_fund_update_operation uop = make_samet_fund_update_op( sam_id, tmp_sf_id, core.amount(100), 200 );
         propose( uop );

         samet_fund_borrow_operation bop = make_samet_fund_borrow_op( sam_id, tmp_sf_id, core.amount(100) );
         propose( bop );

         samet_fund_repay_operation rop =
                         make_samet_fund_repay_op( sam_id, tmp_sf_id, core.amount(100), core.amount(100) );
         propose( rop );
      }

      int64_t expected_balance_sam_core = init_amount;
      int64_t expected_balance_sam_usd = init_amount;
      int64_t expected_balance_sam_eur = init_amount;
      int64_t expected_balance_ted_core = init_amount;
      int64_t expected_balance_ted_usd = init_amount;
      int64_t expected_balance_ted_eur = init_amount;

      const auto& check_balances = [&]() {
         BOOST_CHECK_EQUAL( db.get_balance( sam_id, core_id ).amount.value, expected_balance_sam_core );
         BOOST_CHECK_EQUAL( db.get_balance( sam_id, usd_id ).amount.value, expected_balance_sam_usd );
         BOOST_CHECK_EQUAL( db.get_balance( sam_id, eur_id ).amount.value, expected_balance_sam_eur );
         BOOST_CHECK_EQUAL( db.get_balance( ted_id, core_id ).amount.value, expected_balance_ted_core );
         BOOST_CHECK_EQUAL( db.get_balance( ted_id, usd_id ).amount.value, expected_balance_ted_usd );
         BOOST_CHECK_EQUAL( db.get_balance( ted_id, eur_id ).amount.value, expected_balance_ted_eur );
      };

      check_balances();

      // Able to create samet funds with valid data
      const samet_fund_object& sfo1 = create_samet_fund( sam_id, core.id, 10000, 100u );
      samet_fund_id_type sf1_id = sfo1.id;
      BOOST_CHECK( sfo1.owner_account == sam_id );
      BOOST_CHECK( sfo1.asset_type == core.id );
      BOOST_CHECK( sfo1.balance == 10000 );
      BOOST_CHECK( sfo1.fee_rate == 100u );
      BOOST_CHECK( sfo1.unpaid_amount == 0 );

      expected_balance_sam_core -= 10000;
      check_balances();

      const samet_fund_object& sfo2 = create_samet_fund( ted_id, usd.id, 1, 10000000u );
      samet_fund_id_type sf2_id = sfo2.id;
      BOOST_CHECK( sfo2.owner_account == ted_id );
      BOOST_CHECK( sfo2.asset_type == usd.id );
      BOOST_CHECK( sfo2.balance == 1 );
      BOOST_CHECK( sfo2.fee_rate == 10000000u );
      BOOST_CHECK( sfo2.unpaid_amount == 0 );

      expected_balance_ted_usd -= 1;
      check_balances();

      const samet_fund_object& sfo3 = create_samet_fund( sam_id, eur.id, 10, 1u ); // Account is whitelisted
      samet_fund_id_type sf3_id = sfo3.id;
      BOOST_CHECK( sfo3.owner_account == sam_id );
      BOOST_CHECK( sfo3.asset_type == eur_id );
      BOOST_CHECK( sfo3.balance == 10 );
      BOOST_CHECK( sfo3.fee_rate == 1u );
      BOOST_CHECK( sfo3.unpaid_amount == 0 );

      expected_balance_sam_eur -= 10;
      check_balances();

      // Unable to create a samet fund with invalid data
      // Non-positive balance
      BOOST_CHECK_THROW( create_samet_fund( sam_id, core.id, -1, 100u ), fc::exception );
      BOOST_CHECK_THROW( create_samet_fund( ted_id, usd.id, 0, 10000000u ), fc::exception );
      // Insufficient account balance
      BOOST_CHECK_THROW( create_samet_fund( por_id, usd.id, 1, 100u ), fc::exception );
      // Nonexistent asset type
      BOOST_CHECK_THROW( create_samet_fund( sam_id, no_asset_id, 1, 100u ), fc::exception );
      // Account is not whitelisted
      BOOST_CHECK_THROW( create_samet_fund( ted_id, eur.id, 10, 1u ), fc::exception );

      check_balances();

      // Uable to update a fund with invalid data
      // Changes nothing
      BOOST_CHECK_THROW( update_samet_fund( sam_id, sf1_id, {}, {} ), fc::exception );
      // Zero delta
      BOOST_CHECK_THROW( update_samet_fund( sam_id, sf1_id, asset(0), 10u ), fc::exception );
      // Specified new fee rate but no change
      BOOST_CHECK_THROW( update_samet_fund( sam_id, sf1_id, asset(1), sf1_id(db).fee_rate ), fc::exception );
      // Fund owner mismatch
      BOOST_CHECK_THROW( update_samet_fund( ted_id, sf1_id, asset(1), {} ), fc::exception );
      // Asset type mismatch
      BOOST_CHECK_THROW( update_samet_fund( sam_id, sf1_id, asset(1, usd_id), {} ), fc::exception );
      // Trying to withdraw too much
      BOOST_CHECK_THROW( update_samet_fund( sam_id, sf1_id, asset(-10000), {} ), fc::exception );
      // Insufficient account balance
      BOOST_CHECK_THROW( update_samet_fund( sam_id, sf1_id, asset(init_amount), {} ), fc::exception );

      check_balances();

      // Able to update a fund with valid data
      // Only deposit
      update_samet_fund( sam_id, sf1_id, asset(1), {} );

      BOOST_CHECK( sf1_id(db).owner_account == sam_id );
      BOOST_CHECK( sf1_id(db).asset_type == core.id );
      BOOST_CHECK( sf1_id(db).balance == 10001 );
      BOOST_CHECK( sf1_id(db).fee_rate == 100u );
      BOOST_CHECK( sf1_id(db).unpaid_amount == 0 );

      expected_balance_sam_core -= 1;
      check_balances();

      // Only update fee rate
      update_samet_fund( sam_id, sf1_id, {}, 101u );

      BOOST_CHECK( sf1_id(db).owner_account == sam_id );
      BOOST_CHECK( sf1_id(db).asset_type == core.id );
      BOOST_CHECK( sf1_id(db).balance == 10001 );
      BOOST_CHECK( sf1_id(db).fee_rate == 101u );
      BOOST_CHECK( sf1_id(db).unpaid_amount == 0 );

      check_balances();

      // Withdraw and update fee rate
      update_samet_fund( sam_id, sf1_id, asset(-9999), 10u );

      BOOST_CHECK( sf1_id(db).owner_account == sam_id );
      BOOST_CHECK( sf1_id(db).asset_type == core.id );
      BOOST_CHECK( sf1_id(db).balance == 2 );
      BOOST_CHECK( sf1_id(db).fee_rate == 10u );
      BOOST_CHECK( sf1_id(db).unpaid_amount == 0 );

      expected_balance_sam_core += 9999;
      check_balances();

      // Sam is able to delete his own fund
      asset released = delete_samet_fund( sam_id, sf1_id );

      BOOST_REQUIRE( !db.find( sf1_id ) );
      BOOST_REQUIRE( db.find( sf2_id ) );
      BOOST_REQUIRE( db.find( sf3_id ) );

      BOOST_CHECK( released == asset( 2, core_id ) );

      expected_balance_sam_core += 2;
      check_balances();

      // Unable to update a fund that does not exist
      BOOST_CHECK_THROW( update_samet_fund( sam_id, sf1_id, asset(1), {} ), fc::exception );
      // Unable to delete a fund that does not exist
      BOOST_CHECK_THROW( delete_samet_fund( sam_id, sf1_id ), fc::exception );
      // Unable to delete a fund that is not owned by him
      BOOST_CHECK_THROW( delete_samet_fund( sam_id, sfo2.id ), fc::exception );

      BOOST_REQUIRE( !db.find( sf1_id ) );
      BOOST_REQUIRE( db.find( sf2_id ) );
      BOOST_REQUIRE( db.find( sf3_id ) );

      check_balances();

      {
         // Add Ted to the whitelist and remove Sam
         account_whitelist_operation wop;
         wop.authorizing_account = sam_id;
         wop.account_to_list = ted_id;
         wop.new_listing = account_whitelist_operation::white_listed;
         trx.operations.clear();
         trx.operations.push_back(wop);
         wop.account_to_list = sam_id;
         wop.new_listing = account_whitelist_operation::no_listing;
         trx.operations.push_back(wop);
         PUSH_TX( db, trx, ~0 );
      }

      // Sam is now unable to deposit to the fund
      BOOST_CHECK_THROW( update_samet_fund( sam_id, sf3_id, asset(1, eur_id), {} ), fc::exception );

      BOOST_CHECK( sf3_id(db).owner_account == sam_id );
      BOOST_CHECK( sf3_id(db).asset_type == eur_id );
      BOOST_CHECK( sf3_id(db).balance == 10 );
      BOOST_CHECK( sf3_id(db).fee_rate == 1u );
      BOOST_CHECK( sf3_id(db).unpaid_amount == 0 );

      check_balances();

      // Sam is still able to withdraw from the fund
      update_samet_fund( sam_id, sf3_id, asset(-1, eur_id), {} );
      BOOST_CHECK( sf3_id(db).owner_account == sam_id );
      BOOST_CHECK( sf3_id(db).asset_type == eur_id );
      BOOST_CHECK( sf3_id(db).balance == 9 );
      BOOST_CHECK( sf3_id(db).fee_rate == 1u );
      BOOST_CHECK( sf3_id(db).unpaid_amount == 0 );

      expected_balance_sam_eur += 1;
      check_balances();

      // Sam is still able to update fee rate
      update_samet_fund( sam_id, sf3_id, {}, 2u );
      BOOST_CHECK( sf3_id(db).owner_account == sam_id );
      BOOST_CHECK( sf3_id(db).asset_type == eur_id );
      BOOST_CHECK( sf3_id(db).balance == 9 );
      BOOST_CHECK( sf3_id(db).fee_rate == 2u );
      BOOST_CHECK( sf3_id(db).unpaid_amount == 0 );

      check_balances();

      // Sam is still able to delete the fund
      released = delete_samet_fund( sam_id, sf3_id );
      BOOST_REQUIRE( !db.find( sf3_id ) );

      BOOST_CHECK( released == asset( 9, eur_id ) );

      expected_balance_sam_eur += 9;
      check_balances();

      // Same is unable to recreate the fund
      BOOST_CHECK_THROW( create_samet_fund( sam_id, eur.id, 10, 1u ), fc::exception );
      check_balances();

      generate_block();

   } catch (fc::exception& e) {
      edump((e.to_detail_string()));
      throw;
   }
}

BOOST_AUTO_TEST_CASE( samet_fund_borrow_repay_test )
{ try {

      // Pass the hard fork time
      generate_blocks( HARDFORK_CORE_2351_TIME );
      set_expiration( db, trx );

      ACTORS((sam)(ted)(por));

      auto init_amount = 10000000 * GRAPHENE_BLOCKCHAIN_PRECISION;
      fund( sam, asset(init_amount) );
      fund( ted, asset(init_amount) );

      const asset_object& core = asset_id_type()(db);
      asset_id_type core_id;

      const asset_object& usd = create_user_issued_asset( "MYUSD" );
      asset_id_type usd_id = usd.id;
      issue_uia( sam, usd.amount(init_amount) );
      issue_uia( ted, usd.amount(init_amount) );

      const asset_object& eur = create_user_issued_asset( "MYEUR", sam, white_list );
      asset_id_type eur_id = eur.id;
      issue_uia( sam, eur.amount(init_amount) );
      issue_uia( ted, eur.amount(init_amount) );
      // Make a whitelist
      {
         BOOST_TEST_MESSAGE( "Setting up whitelisting" );
         asset_update_operation uop;
         uop.asset_to_update = eur.id;
         uop.issuer = sam_id;
         uop.new_options = eur.options;
         // The whitelist is managed by Sam
         uop.new_options.whitelist_authorities.insert(sam_id);
         trx.operations.clear();
         trx.operations.push_back(uop);
         PUSH_TX( db, trx, ~0 );

         // Upgrade Sam so that he can manage the whitelist
         upgrade_to_lifetime_member( sam_id );

         // Add Sam to the whitelist, but do not add others
         account_whitelist_operation wop;
         wop.authorizing_account = sam_id;
         wop.account_to_list = sam_id;
         wop.new_listing = account_whitelist_operation::white_listed;
         trx.operations.clear();
         trx.operations.push_back(wop);
         PUSH_TX( db, trx, ~0 );
      }

      asset_id_type no_asset_id( core.id + 100 );
      BOOST_REQUIRE( !db.find( no_asset_id ) );

      int64_t expected_balance_sam_core = init_amount;
      int64_t expected_balance_sam_usd = init_amount;
      int64_t expected_balance_sam_eur = init_amount;
      int64_t expected_balance_ted_core = init_amount;
      int64_t expected_balance_ted_usd = init_amount;
      int64_t expected_balance_ted_eur = init_amount;

      const auto& check_balances = [&]() {
         BOOST_CHECK_EQUAL( db.get_balance( sam_id, core_id ).amount.value, expected_balance_sam_core );
         BOOST_CHECK_EQUAL( db.get_balance( sam_id, usd_id ).amount.value, expected_balance_sam_usd );
         BOOST_CHECK_EQUAL( db.get_balance( sam_id, eur_id ).amount.value, expected_balance_sam_eur );
         BOOST_CHECK_EQUAL( db.get_balance( ted_id, core_id ).amount.value, expected_balance_ted_core );
         BOOST_CHECK_EQUAL( db.get_balance( ted_id, usd_id ).amount.value, expected_balance_ted_usd );
         BOOST_CHECK_EQUAL( db.get_balance( ted_id, eur_id ).amount.value, expected_balance_ted_eur );
      };

      check_balances();

      // create samet funds
      const samet_fund_object& sfo1 = create_samet_fund( sam_id, core.id, 10000, 10000u ); // fee rate is 1%
      samet_fund_id_type sf1_id = sfo1.id;

      expected_balance_sam_core -= 10000;
      check_balances();

      const samet_fund_object& sfo2 = create_samet_fund( ted_id, usd.id, 1, 10000000u ); // fee rate is 1000%
      samet_fund_id_type sf2_id = sfo2.id;

      expected_balance_ted_usd -= 1;
      check_balances();

      const samet_fund_object& sfo3 = create_samet_fund( sam_id, eur.id, 10, 1u ); // Account is whitelisted
      samet_fund_id_type sf3_id = sfo3.id;

      expected_balance_sam_eur -= 10;
      check_balances();

      // Unable to borrow without repayment
      BOOST_CHECK_THROW( borrow_from_samet_fund( sam_id, sf1_id, asset(1) ), fc::exception );
      // Unable to repay without borrowing
      BOOST_CHECK_THROW( repay_to_samet_fund( sam_id, sf1_id, asset(1), asset(100) ), fc::exception );

      // Valid : borrow and repay
      {
         auto bop1 = make_samet_fund_borrow_op( sam_id, sf1_id, asset(1) );
         auto rop1 = make_samet_fund_repay_op( sam_id, sf1_id, asset(1), asset(1) );
         trx.operations.clear();
         trx.operations.push_back(bop1);
         trx.operations.push_back(rop1);
         PUSH_TX( db, trx, ~0 );
      }

      BOOST_CHECK( sf1_id(db).owner_account == sam_id );
      BOOST_CHECK( sf1_id(db).asset_type == core.id );
      BOOST_CHECK( sf1_id(db).balance == 10001 );
      BOOST_CHECK( sf1_id(db).fee_rate == 10000u );
      BOOST_CHECK( sf1_id(db).unpaid_amount == 0 );

      expected_balance_sam_core -= 1;
      check_balances();

      // Valid : borrow multiple times and repay at once
      {
         auto bop1 = make_samet_fund_borrow_op( sam_id, sf1_id, asset(1) );
         auto bop2 = make_samet_fund_borrow_op( sam_id, sf1_id, asset(2) );
         auto rop1 = make_samet_fund_repay_op( sam_id, sf1_id, asset(3), asset(1) );
         trx.operations.clear();
         trx.operations.push_back(bop1);
         trx.operations.push_back(bop2);
         trx.operations.push_back(rop1);
         PUSH_TX( db, trx, ~0 );
      }

      BOOST_CHECK( sf1_id(db).owner_account == sam_id );
      BOOST_CHECK( sf1_id(db).asset_type == core.id );
      BOOST_CHECK( sf1_id(db).balance == 10002 );
      BOOST_CHECK( sf1_id(db).fee_rate == 10000u );
      BOOST_CHECK( sf1_id(db).unpaid_amount == 0 );

      expected_balance_sam_core -= 1;
      check_balances();

      // Valid : borrow with one account and repay with another account
      {
         auto bop1 = make_samet_fund_borrow_op( ted_id, sf1_id, asset(5) );
         auto rop1 = make_samet_fund_repay_op( sam_id, sf1_id, asset(5), asset(1) );
         trx.operations.clear();
         trx.operations.push_back(bop1);
         trx.operations.push_back(rop1);
         PUSH_TX( db, trx, ~0 );
      }

      BOOST_CHECK( sf1_id(db).owner_account == sam_id );
      BOOST_CHECK( sf1_id(db).asset_type == core.id );
      BOOST_CHECK( sf1_id(db).balance == 10003 );
      BOOST_CHECK( sf1_id(db).fee_rate == 10000u );
      BOOST_CHECK( sf1_id(db).unpaid_amount == 0 );

      expected_balance_ted_core += 5;
      expected_balance_sam_core -= 6;
      check_balances();

      // Valid : borrow at once, repay via multiple times
      {
         auto bop1 = make_samet_fund_borrow_op( sam_id, sf1_id, asset(7) );
         auto rop1 = make_samet_fund_repay_op( sam_id, sf1_id, asset(3), asset(1) );
         auto rop2 = make_samet_fund_repay_op( ted_id, sf1_id, asset(4), asset(1) );
         trx.operations.clear();
         trx.operations.push_back(bop1);
         trx.operations.push_back(rop1);
         trx.operations.push_back(rop2);
         PUSH_TX( db, trx, ~0 );
      }

      BOOST_CHECK( sf1_id(db).owner_account == sam_id );
      BOOST_CHECK( sf1_id(db).asset_type == core.id );
      BOOST_CHECK( sf1_id(db).balance == 10005 );
      BOOST_CHECK( sf1_id(db).fee_rate == 10000u );
      BOOST_CHECK( sf1_id(db).unpaid_amount == 0 );

      expected_balance_sam_core += 3;
      expected_balance_ted_core -= 5;
      check_balances();

      // Valid : borrow from multiple funds and repay
      {
         auto bop1 = make_samet_fund_borrow_op( sam_id, sf1_id, asset(7) );
         auto rop1 = make_samet_fund_repay_op( sam_id, sf1_id, asset(3), asset(1) );
         auto bop2 = make_samet_fund_borrow_op( ted_id, sf2_id, asset(1, usd_id) );
         auto rop2 = make_samet_fund_repay_op( ted_id, sf1_id, asset(4), asset(1) );
         auto rop3 = make_samet_fund_repay_op( sam_id, sf2_id, asset(1, usd_id), asset(10, usd_id) );
         trx.operations.clear();
         trx.operations.push_back(bop1);
         trx.operations.push_back(rop1);
         trx.operations.push_back(bop2);
         trx.operations.push_back(rop2);
         trx.operations.push_back(rop3);
         PUSH_TX( db, trx, ~0 );
      }

      BOOST_CHECK( sf1_id(db).owner_account == sam_id );
      BOOST_CHECK( sf1_id(db).asset_type == core.id );
      BOOST_CHECK( sf1_id(db).balance == 10007 );
      BOOST_CHECK( sf1_id(db).fee_rate == 10000u );
      BOOST_CHECK( sf1_id(db).unpaid_amount == 0 );

      BOOST_CHECK( sf2_id(db).owner_account == ted_id );
      BOOST_CHECK( sf2_id(db).asset_type == usd_id );
      BOOST_CHECK( sf2_id(db).balance == 11 );
      BOOST_CHECK( sf2_id(db).fee_rate == 10000000u );
      BOOST_CHECK( sf2_id(db).unpaid_amount == 0 );

      expected_balance_sam_core += 3;
      expected_balance_ted_core -= 5;
      expected_balance_sam_usd -= 11;
      expected_balance_ted_usd += 1;
      check_balances();

      // Valid : borrow and repay with more fee than enough
      {
         auto bop1 = make_samet_fund_borrow_op( sam_id, sf1_id, asset(1) );
         auto rop1 = make_samet_fund_repay_op( sam_id, sf1_id, asset(1), asset(2) );
         trx.operations.clear();
         trx.operations.push_back(bop1);
         trx.operations.push_back(rop1);
         PUSH_TX( db, trx, ~0 );
      }

      BOOST_CHECK( sf1_id(db).owner_account == sam_id );
      BOOST_CHECK( sf1_id(db).asset_type == core.id );
      BOOST_CHECK( sf1_id(db).balance == 10009 );
      BOOST_CHECK( sf1_id(db).fee_rate == 10000u );
      BOOST_CHECK( sf1_id(db).unpaid_amount == 0 );

      expected_balance_sam_core -= 2;
      check_balances();

      // Valid: account whitelisted by asset
      {
         auto bop1 = make_samet_fund_borrow_op( sam_id, sf3_id, asset(1, eur_id) );
         auto rop1 = make_samet_fund_repay_op( sam_id, sf3_id, asset(1, eur_id), asset(1, eur_id) );
         trx.operations.clear();
         trx.operations.push_back(bop1);
         trx.operations.push_back(rop1);
         PUSH_TX( db, trx, ~0 );
      }

      BOOST_CHECK( sf3_id(db).owner_account == sam_id );
      BOOST_CHECK( sf3_id(db).asset_type == eur_id );
      BOOST_CHECK( sf3_id(db).balance == 11 );
      BOOST_CHECK( sf3_id(db).fee_rate == 1u );
      BOOST_CHECK( sf3_id(db).unpaid_amount == 0 );

      expected_balance_sam_eur -= 1;
      check_balances();

      // Invalid operations
      {
         // Borrow 0
         auto bop = make_samet_fund_borrow_op( sam_id, sf1_id, asset(0) );
         BOOST_CHECK_THROW( bop.validate(), fc::exception );
         BOOST_CHECK_THROW( propose( bop ), fc::exception );

         // Borrow a negative amount
         bop = make_samet_fund_borrow_op( sam_id, sf1_id, asset(-1) );
         BOOST_CHECK_THROW( bop.validate(), fc::exception );
         BOOST_CHECK_THROW( propose( bop ), fc::exception );

         // Repay 0
         auto rop = make_samet_fund_repay_op( sam_id, sf1_id, asset(0), asset(1) );
         BOOST_CHECK_THROW( rop.validate(), fc::exception );
         BOOST_CHECK_THROW( propose( rop ), fc::exception );

         // Repay a negative amount
         rop = make_samet_fund_repay_op( sam_id, sf1_id, asset(-1), asset(1) );
         BOOST_CHECK_THROW( rop.validate(), fc::exception );
         BOOST_CHECK_THROW( propose( rop ), fc::exception );

         // Repay with a negative fee
         rop = make_samet_fund_repay_op( sam_id, sf1_id, asset(1), asset(-1) );
         BOOST_CHECK_THROW( rop.validate(), fc::exception );
         BOOST_CHECK_THROW( propose( rop ), fc::exception );

         // Repay amount and fee in different assets
         rop = make_samet_fund_repay_op( sam_id, sf1_id, asset(1), asset(1, usd_id) );
         BOOST_CHECK_THROW( rop.validate(), fc::exception );
         BOOST_CHECK_THROW( propose( rop ), fc::exception );
      }

      // Valid : borrow all from a fund
      auto expected_sf1_balance = sf1_id(db).balance;
      {
         auto balance = sf1_id(db).balance;
         auto to_borrow = balance;
         auto fund_fee = to_borrow / 100 + 1;
         auto bop1 = make_samet_fund_borrow_op( sam_id, sf1_id, asset(to_borrow) );
         auto rop1 = make_samet_fund_repay_op( sam_id, sf1_id, asset(to_borrow), asset(fund_fee) );
         trx.operations.clear();
         trx.operations.push_back(bop1);
         trx.operations.push_back(rop1);
         PUSH_TX( db, trx, ~0 );

         expected_sf1_balance += fund_fee;

         BOOST_CHECK( sf1_id(db).owner_account == sam_id );
         BOOST_CHECK( sf1_id(db).asset_type == core.id );
         BOOST_CHECK( sf1_id(db).balance == expected_sf1_balance );
         BOOST_CHECK( sf1_id(db).fee_rate == 10000u );
         BOOST_CHECK( sf1_id(db).unpaid_amount == 0 );

         expected_balance_sam_core -= fund_fee.value;
         check_balances();
      }

      // Valid : update fund fee rate after borrowed
      {
         auto balance = sf1_id(db).balance;
         auto to_borrow = balance;
         auto fund_fee = to_borrow / 100 + 1;
         auto bop1 = make_samet_fund_borrow_op( sam_id, sf1_id, asset(to_borrow) );
         auto uop1 = make_samet_fund_update_op( sam_id, sf1_id, {}, 9999u ); // new fee rate is 0.9999%
         auto rop1 = make_samet_fund_repay_op( sam_id, sf1_id, asset(to_borrow), asset(fund_fee) );
         trx.operations.clear();
         trx.operations.push_back(bop1);
         trx.operations.push_back(uop1);
         trx.operations.push_back(rop1);
         PUSH_TX( db, trx, ~0 );

         expected_sf1_balance += fund_fee;

         BOOST_CHECK( sf1_id(db).owner_account == sam_id );
         BOOST_CHECK( sf1_id(db).asset_type == core.id );
         BOOST_CHECK( sf1_id(db).balance == expected_sf1_balance );
         BOOST_CHECK( sf1_id(db).fee_rate == 9999u );
         BOOST_CHECK( sf1_id(db).unpaid_amount == 0 );

         expected_balance_sam_core -= fund_fee.value;
         check_balances();

         // Able to do the same via a proposal
         auto cop = make_proposal_create_op( bop1, sam_id, 300, {} );
         auto uop2 = make_samet_fund_update_op( sam_id, sf1_id, {}, 9998u ); // new fee rate is 0.9998%
         cop.proposed_ops.emplace_back(uop2);
         cop.proposed_ops.emplace_back(rop1);
         trx.operations.clear();
         trx.operations.push_back( cop );
         processed_transaction ptx = PUSH_TX(db, trx, ~0);
         const operation_result& op_result = ptx.operation_results.front();
         proposal_id_type pid = op_result.get<object_id_type>();

         proposal_update_operation puo;
         puo.proposal = pid;
         puo.fee_paying_account = sam_id;
         puo.active_approvals_to_add.emplace( sam_id );
         trx.operations.clear();
         trx.operations.push_back(puo);
         PUSH_TX(db, trx, ~0);

         BOOST_CHECK( !db.find(pid) );

         expected_sf1_balance += fund_fee;

         BOOST_CHECK( sf1_id(db).owner_account == sam_id );
         BOOST_CHECK( sf1_id(db).asset_type == core.id );
         BOOST_CHECK( sf1_id(db).balance == expected_sf1_balance );
         BOOST_CHECK( sf1_id(db).fee_rate == 9998u );
         BOOST_CHECK( sf1_id(db).unpaid_amount == 0 );

         expected_balance_sam_core -= fund_fee.value;
         check_balances();
      }

      vector<proposal_id_type> proposals;
      auto make_proposal_from_trx = [&]() {
         proposal_create_operation cop;
         cop.fee_paying_account = sam_id;
         cop.expiration_time = db.head_block_time() + 30;
         cop.review_period_seconds = {};
         for( auto& op : trx.operations )
         {
            cop.proposed_ops.emplace_back( op );
         }
         for( auto& o : cop.proposed_ops ) db.current_fee_schedule().set_fee(o.op);

         trx.operations.clear();
         trx.operations.push_back( cop );
         processed_transaction ptx = PUSH_TX(db, trx, ~0);
         const operation_result& op_result = ptx.operation_results.front();
         proposal_id_type pid = op_result.get<object_id_type>();
         proposals.push_back( pid );
      };

      // Invalid : borrow more amount than available
      {
         auto balance = sf1_id(db).balance;
         auto to_borrow = balance + 1;
         auto fund_fee = to_borrow / 100 + 1;
         auto bop1 = make_samet_fund_borrow_op( sam_id, sf1_id, asset(to_borrow) );
         auto rop1 = make_samet_fund_repay_op( sam_id, sf1_id, asset(to_borrow), asset(fund_fee) );
         trx.operations.clear();
         trx.operations.push_back(bop1);
         trx.operations.push_back(rop1);
         BOOST_CHECK_THROW( PUSH_TX( db, trx, ~0 ), fc::exception );
         make_proposal_from_trx();
      }

      // Invalid : borrow more amount than available
      {
         auto balance = sf1_id(db).balance;
         auto to_borrow = balance + 1;
         auto fund_fee = to_borrow / 100 + 1;
         auto bop1 = make_samet_fund_borrow_op( sam_id, sf1_id, asset(to_borrow - 2) );
         auto bop2 = make_samet_fund_borrow_op( sam_id, sf1_id, asset(2) );
         auto rop1 = make_samet_fund_repay_op( sam_id, sf1_id, asset(to_borrow), asset(fund_fee) );
         trx.operations.clear();
         trx.operations.push_back(bop1);
         trx.operations.push_back(bop2);
         trx.operations.push_back(rop1);
         BOOST_CHECK_THROW( PUSH_TX( db, trx, ~0 ), fc::exception );
         make_proposal_from_trx();
      }

      // Invalid : borrow asset type mismatch
      {
         auto balance = sf1_id(db).balance;
         auto to_borrow = balance;
         auto fund_fee = to_borrow / 100 + 1;
         auto bop1 = make_samet_fund_borrow_op( sam_id, sf1_id, asset(to_borrow, usd_id) );
         auto rop1 = make_samet_fund_repay_op( sam_id, sf1_id, asset(to_borrow), asset(fund_fee) );
         trx.operations.clear();
         trx.operations.push_back(bop1);
         trx.operations.push_back(rop1);
         BOOST_CHECK_THROW( PUSH_TX( db, trx, ~0 ), fc::exception );
         make_proposal_from_trx();

         rop1 = make_samet_fund_repay_op( sam_id, sf1_id, asset(to_borrow, usd_id), asset(fund_fee, usd_id) );
         trx.operations.clear();
         trx.operations.push_back(bop1);
         trx.operations.push_back(rop1);
         BOOST_CHECK_THROW( PUSH_TX( db, trx, ~0 ), fc::exception );
         make_proposal_from_trx();
      }

      // Invalid : repay asset type mismatch
      {
         auto balance = sf1_id(db).balance;
         auto to_borrow = balance;
         auto fund_fee = to_borrow / 100 + 1;
         auto bop1 = make_samet_fund_borrow_op( sam_id, sf1_id, asset(to_borrow) );
         auto rop1 = make_samet_fund_repay_op( sam_id, sf1_id, asset(to_borrow, usd_id), asset(fund_fee, usd_id) );
         trx.operations.clear();
         trx.operations.push_back(bop1);
         trx.operations.push_back(rop1);
         BOOST_CHECK_THROW( PUSH_TX( db, trx, ~0 ), fc::exception );
         make_proposal_from_trx();
      }

      // Invalid : repay less than borrowed
      {
         auto balance = sf1_id(db).balance;
         auto to_borrow = balance;
         auto fund_fee = to_borrow / 100 + 1;
         auto bop1 = make_samet_fund_borrow_op( sam_id, sf1_id, asset(to_borrow) );
         auto rop1 = make_samet_fund_repay_op( sam_id, sf1_id, asset(to_borrow - 1), asset(fund_fee) );
         trx.operations.clear();
         trx.operations.push_back(bop1);
         trx.operations.push_back(rop1);
         BOOST_CHECK_THROW( PUSH_TX( db, trx, ~0 ), fc::exception );
         make_proposal_from_trx();
      }

      // Invalid : repay more than borrowed
      {
         auto balance = sf1_id(db).balance;
         auto to_borrow = balance;
         auto fund_fee = (to_borrow + 1) / 100 + 1;
         auto bop1 = make_samet_fund_borrow_op( sam_id, sf1_id, asset(to_borrow) );
         auto rop1 = make_samet_fund_repay_op( sam_id, sf1_id, asset(to_borrow + 1), asset(fund_fee) );
         trx.operations.clear();
         trx.operations.push_back(bop1);
         trx.operations.push_back(rop1);
         BOOST_CHECK_THROW( PUSH_TX( db, trx, ~0 ), fc::exception );
         make_proposal_from_trx();

         // Invalid too if repaid more and borrow again
         auto bop2 = make_samet_fund_borrow_op( sam_id, sf1_id, asset(1) );
         trx.operations.clear();
         trx.operations.push_back(bop1);
         trx.operations.push_back(rop1);
         trx.operations.push_back(bop2);
         BOOST_CHECK_THROW( PUSH_TX( db, trx, ~0 ), fc::exception );
         make_proposal_from_trx();
      }

      // Invalid : insufficient fund fee paid
      {
         auto balance = sf1_id(db).balance;
         auto to_borrow = balance;
         auto fund_fee = ( to_borrow - 1 ) / 100;
         auto bop1 = make_samet_fund_borrow_op( sam_id, sf1_id, asset(to_borrow) );
         auto rop1 = make_samet_fund_repay_op( sam_id, sf1_id, asset(to_borrow), asset(fund_fee) );
         trx.operations.clear();
         trx.operations.push_back(bop1);
         trx.operations.push_back(rop1);
         BOOST_CHECK_THROW( PUSH_TX( db, trx, ~0 ), fc::exception );
         make_proposal_from_trx();
      }

      // Invalid : insufficient account balance to repay the debt
      {
         auto balance = sf1_id(db).balance;
         auto to_borrow = balance;
         auto fund_fee = to_borrow / 100 + 1;
         auto bop1 = make_samet_fund_borrow_op( por_id, sf1_id, asset(to_borrow) );
         auto rop1 = make_samet_fund_repay_op( por_id, sf1_id, asset(to_borrow), asset(fund_fee) );
         trx.operations.clear();
         trx.operations.push_back(bop1);
         trx.operations.push_back(rop1);
         BOOST_CHECK_THROW( PUSH_TX( db, trx, ~0 ), fc::exception );
         make_proposal_from_trx();
      }

      // Invalid : update fund balance after borrowed
      {
         auto balance = sf1_id(db).balance;
         auto to_borrow = balance;
         auto fund_fee = to_borrow / 100 + 1;
         auto bop1 = make_samet_fund_borrow_op( sam_id, sf1_id, asset(to_borrow) );
         auto uop1 = make_samet_fund_update_op( sam_id, sf1_id, asset(1), {} );
         auto rop1 = make_samet_fund_repay_op( sam_id, sf1_id, asset(to_borrow), asset(fund_fee) );
         trx.operations.clear();
         trx.operations.push_back(bop1);
         trx.operations.push_back(uop1);
         trx.operations.push_back(rop1);
         BOOST_CHECK_THROW( PUSH_TX( db, trx, ~0 ), fc::exception );
         make_proposal_from_trx();

         auto uop2 = make_samet_fund_update_op( sam_id, sf1_id, asset(-1), {} );
         trx.operations.clear();
         trx.operations.push_back(bop1);
         trx.operations.push_back(uop2);
         trx.operations.push_back(rop1);
         BOOST_CHECK_THROW( PUSH_TX( db, trx, ~0 ), fc::exception );
         make_proposal_from_trx();
      }

      // Invalid : delete fund after borrowed
      {
         auto balance = sf1_id(db).balance;
         auto to_borrow = balance;
         auto fund_fee = to_borrow / 100 + 1;
         auto bop1 = make_samet_fund_borrow_op( sam_id, sf1_id, asset(to_borrow) );
         auto dop1 = make_samet_fund_delete_op( sam_id, sf1_id );
         trx.operations.clear();
         trx.operations.push_back(bop1);
         trx.operations.push_back(dop1);
         BOOST_CHECK_THROW( PUSH_TX( db, trx, ~0 ), fc::exception );
         make_proposal_from_trx();

         auto rop1 = make_samet_fund_repay_op( sam_id, sf1_id, asset(to_borrow), asset(fund_fee) );
         trx.operations.clear();
         trx.operations.push_back(bop1);
         trx.operations.push_back(dop1);
         trx.operations.push_back(rop1);
         BOOST_CHECK_THROW( PUSH_TX( db, trx, ~0 ), fc::exception );
         make_proposal_from_trx();
      }

      // Invalid: borrow account is not whitelisted by asset
      {
         auto bop1 = make_samet_fund_borrow_op( ted_id, sf3_id, asset(1, eur_id) );
         auto rop1 = make_samet_fund_repay_op( sam_id, sf3_id, asset(1, eur_id), asset(1, eur_id) );
         trx.operations.clear();
         trx.operations.push_back(bop1);
         trx.operations.push_back(rop1);
         BOOST_CHECK_THROW( PUSH_TX( db, trx, ~0 ), fc::exception );
         make_proposal_from_trx();
      }

      // Invalid: repay account is not whitelisted by asset
      {
         auto bop1 = make_samet_fund_borrow_op( sam_id, sf3_id, asset(1, eur_id) );
         auto rop1 = make_samet_fund_repay_op( ted_id, sf3_id, asset(1, eur_id), asset(1, eur_id) );
         trx.operations.clear();
         trx.operations.push_back(bop1);
         trx.operations.push_back(rop1);
         BOOST_CHECK_THROW( PUSH_TX( db, trx, ~0 ), fc::exception );
         make_proposal_from_trx();
      }

      generate_block();

      // Nothing changed
      BOOST_CHECK( sf1_id(db).owner_account == sam_id );
      BOOST_CHECK( sf1_id(db).asset_type == core.id );
      BOOST_CHECK( sf1_id(db).balance == expected_sf1_balance );
      BOOST_CHECK( sf1_id(db).fee_rate == 9998u );
      BOOST_CHECK( sf1_id(db).unpaid_amount == 0 );

      check_balances();

      // Approve the proposals
      for( auto& pid : proposals )
      {
         auto& p = pid(db);
         proposal_update_operation puo;
         puo.proposal = pid;
         puo.fee_paying_account = sam_id;
         for(auto& req : p.required_active_approvals)
            puo.active_approvals_to_add.emplace( req );
         trx.operations.clear();
         trx.operations.push_back(puo);
         PUSH_TX(db, trx, ~0);

         // Approved but failed to execute
         BOOST_CHECK( pid(db).is_authorized_to_execute(db) );
         BOOST_CHECK( !pid(db).fail_reason.empty() );

         // Nothing changed
         BOOST_CHECK( sf1_id(db).owner_account == sam_id );
         BOOST_CHECK( sf1_id(db).asset_type == core.id );
         BOOST_CHECK( sf1_id(db).balance == expected_sf1_balance );
         BOOST_CHECK( sf1_id(db).fee_rate == 9998u );
         BOOST_CHECK( sf1_id(db).unpaid_amount == 0 );
         check_balances();
      }

      // Nothing changed
      BOOST_CHECK( sf1_id(db).owner_account == sam_id );
      BOOST_CHECK( sf1_id(db).asset_type == core.id );
      BOOST_CHECK( sf1_id(db).balance == expected_sf1_balance );
      BOOST_CHECK( sf1_id(db).fee_rate == 9998u );
      BOOST_CHECK( sf1_id(db).unpaid_amount == 0 );

      check_balances();

      // Time goes by
      generate_blocks( db.head_block_time() + fc::seconds(300) );

      // proposals expired
      for( auto& pid : proposals )
      {
         BOOST_CHECK( !db.find(pid) );
      }

      // Nothing changed
      BOOST_CHECK( sf1_id(db).owner_account == sam_id );
      BOOST_CHECK( sf1_id(db).asset_type == core.id );
      BOOST_CHECK( sf1_id(db).balance == expected_sf1_balance );
      BOOST_CHECK( sf1_id(db).fee_rate == 9998u );
      BOOST_CHECK( sf1_id(db).unpaid_amount == 0 );

      check_balances();

   } catch (fc::exception& e) {
      edump((e.to_detail_string()));
      throw;
   }
}

BOOST_AUTO_TEST_CASE( samet_fund_apis_test )
{ try {

      // Pass the hard fork time
      generate_blocks( HARDFORK_CORE_2351_TIME );
      set_expiration( db, trx );

      ACTORS((sam)(ted));

      auto init_amount = 10000000 * GRAPHENE_BLOCKCHAIN_PRECISION;
      fund( sam, asset(init_amount) );
      fund( ted, asset(init_amount) );

      asset_id_type core_id;

      const asset_object& usd = create_user_issued_asset( "MYUSD" );
      asset_id_type usd_id = usd.id;
      issue_uia( sam, usd.amount(init_amount) );
      issue_uia( ted, usd.amount(init_amount) );

      const asset_object& eur = create_user_issued_asset( "MYEUR", sam, white_list );
      asset_id_type eur_id = eur.id;
      issue_uia( sam, eur.amount(init_amount) );
      issue_uia( ted, eur.amount(init_amount) );

      // create samet funds
      const samet_fund_object& sfo1 = create_samet_fund( sam_id, core_id, 10000, 10000u ); // fee rate is 1%
      samet_fund_id_type sf1_id = sfo1.id;

      const samet_fund_object& sfo2 = create_samet_fund( ted_id, usd_id, 1, 10000000u ); // fee rate is 1000%
      samet_fund_id_type sf2_id = sfo2.id;

      const samet_fund_object& sfo3 = create_samet_fund( sam_id, eur_id, 10, 1u );
      samet_fund_id_type sf3_id = sfo3.id;

      const samet_fund_object& sfo4 = create_samet_fund( sam_id, eur_id, 10, 2u );
      samet_fund_id_type sf4_id = sfo4.id;

      const samet_fund_object& sfo5 = create_samet_fund( sam_id, usd_id, 100, 20u );
      samet_fund_id_type sf5_id = sfo5.id;

      const samet_fund_object& sfo6 = create_samet_fund( ted_id, usd_id, 1000, 200u );
      samet_fund_id_type sf6_id = sfo6.id;

      generate_block();

      // Check database API
      graphene::app::database_api db_api( db, &( app.get_options() ) );

      // List all SameT Funds
      auto funds = db_api.list_samet_funds();
      BOOST_REQUIRE_EQUAL( funds.size(), 6u );
      BOOST_CHECK( funds.front().id == sf1_id );
      BOOST_CHECK( funds.back().id == sf6_id );

      // Pagination : the first page
      funds = db_api.list_samet_funds( 5 );
      BOOST_REQUIRE_EQUAL( funds.size(), 5u );
      BOOST_CHECK( funds.front().id == sf1_id );
      BOOST_CHECK( funds.back().id == sf5_id );

      // Pagination : the last page
      funds = db_api.list_samet_funds( 5, sf3_id );
      BOOST_REQUIRE_EQUAL( funds.size(), 4u );
      BOOST_CHECK( funds.front().id == sf3_id );
      BOOST_CHECK( funds.back().id == sf6_id );

      // Limit too large
      BOOST_CHECK_THROW( db_api.list_samet_funds( 102 ), fc::exception );

      // Get all SameT Funds owned by Sam
      funds = db_api.get_samet_funds_by_owner( "sam" );
      BOOST_REQUIRE_EQUAL( funds.size(), 4u );
      BOOST_CHECK( funds.front().id == sf1_id );
      BOOST_CHECK( funds.back().id == sf5_id );

      // Pagination : the first page
      funds = db_api.get_samet_funds_by_owner( "sam", 3, {} );
      BOOST_REQUIRE_EQUAL( funds.size(), 3u );
      BOOST_CHECK( funds.front().id == sf1_id );
      BOOST_CHECK( funds.back().id == sf4_id );

      // Pagination : another page
      funds = db_api.get_samet_funds_by_owner( "sam", 3, sf2_id );
      BOOST_REQUIRE_EQUAL( funds.size(), 3u );
      BOOST_CHECK( funds.front().id == sf3_id );
      BOOST_CHECK( funds.back().id == sf5_id );

      // Pagination : the first page of SameT Funds owned by Ted
      funds = db_api.get_samet_funds_by_owner( string("1.2.")+fc::to_string(ted_id.instance.value), 3 );
      BOOST_REQUIRE_EQUAL( funds.size(), 2u );
      BOOST_CHECK( funds.front().id == sf2_id );
      BOOST_CHECK( funds.back().id == sf6_id );

      // Nonexistent account
      BOOST_CHECK_THROW( db_api.get_samet_funds_by_owner( "nonexistent-account" ), fc::exception );

      // Limit too large
      BOOST_CHECK_THROW( db_api.get_samet_funds_by_owner( "ted", 102 ), fc::exception );

      // Get all SameT Funds whose asset type is USD
      funds = db_api.get_samet_funds_by_asset( "MYUSD" );
      BOOST_REQUIRE_EQUAL( funds.size(), 3u );
      BOOST_CHECK( funds.front().id == sf2_id );
      BOOST_CHECK( funds.back().id == sf6_id );

      // Pagination : the first page
      funds = db_api.get_samet_funds_by_asset( "MYUSD", 2 );
      BOOST_REQUIRE_EQUAL( funds.size(), 2u );
      BOOST_CHECK( funds.front().id == sf2_id );
      BOOST_CHECK( funds.back().id == sf5_id );

      // Pagination : another page
      funds = db_api.get_samet_funds_by_asset( "MYUSD", 2, sf4_id );
      BOOST_REQUIRE_EQUAL( funds.size(), 2u );
      BOOST_CHECK( funds.front().id == sf5_id );
      BOOST_CHECK( funds.back().id == sf6_id );

      // Pagination : the first page of SameT Funds whose asset type is CORE
      funds = db_api.get_samet_funds_by_asset( "1.3.0", 2, {} );
      BOOST_REQUIRE_EQUAL( funds.size(), 1u );
      BOOST_CHECK( funds.front().id == sf1_id );
      BOOST_CHECK( funds.back().id == sf1_id );

      // Nonexistent asset
      BOOST_CHECK_THROW( db_api.get_samet_funds_by_asset( "NOSUCHASSET" ), fc::exception );

      // Limit too large
      BOOST_CHECK_THROW( db_api.get_samet_funds_by_asset( "MYUSD", 102 ), fc::exception );

   } catch (fc::exception& e) {
      edump((e.to_detail_string()));
      throw;
   }
}

BOOST_AUTO_TEST_CASE( samet_fund_account_history_test )
{ try {

      // Pass the hard fork time
      generate_blocks( HARDFORK_CORE_2351_TIME );
      set_expiration( db, trx );

      ACTORS((sam)(ted));

      auto init_amount = 10000000 * GRAPHENE_BLOCKCHAIN_PRECISION;
      fund( sam, asset(init_amount) );
      fund( ted, asset(init_amount) );

      asset_id_type core_id;

      // create samet funds
      const samet_fund_object& sfo1 = create_samet_fund( sam_id, core_id, 10000, 10000u ); // fee rate is 1%
      samet_fund_id_type sf1_id = sfo1.id;

      generate_block();

      // Check history API
      graphene::app::history_api hist_api(app);

      // Sam's last operation is fund creation
      auto histories = hist_api.get_relative_account_history( "sam", 0, 1, 0 );
      BOOST_REQUIRE_EQUAL( histories.size(), 1u );
      BOOST_CHECK( histories[0].op.is_type<samet_fund_create_operation>() );

      // Ted's last operation is transfer
      histories = hist_api.get_relative_account_history( "ted", 0, 1, 0 );
      BOOST_REQUIRE_EQUAL( histories.size(), 1u );
      BOOST_CHECK( histories[0].op.is_type<transfer_operation>() );

      // Ted borrow and repay
      {
         auto bop1 = make_samet_fund_borrow_op( ted_id, sf1_id, asset(1) );
         auto rop1 = make_samet_fund_repay_op( ted_id, sf1_id, asset(1), asset(1) );
         trx.operations.clear();
         trx.operations.push_back(bop1);
         trx.operations.push_back(rop1);
         PUSH_TX( db, trx, ~0 );
      }

      generate_block();

      // Sam's last 2 operations are Ted's borrowing and repayment
      histories = hist_api.get_relative_account_history( "sam", 0, 2, 0 );
      BOOST_REQUIRE_EQUAL( histories.size(), 2u );
      BOOST_CHECK( histories[0].op.is_type<samet_fund_repay_operation>() );
      BOOST_CHECK( histories[1].op.is_type<samet_fund_borrow_operation>() );

      // Ted's last 2 operations are the same
      auto histories_ted = hist_api.get_relative_account_history( "ted", 0, 2, 0 );
      BOOST_REQUIRE_EQUAL( histories_ted.size(), 2u );
      BOOST_CHECK( histories[0].id == histories_ted[0].id );
      BOOST_CHECK( histories[1].id == histories_ted[1].id );

   } catch (fc::exception& e) {
      edump((e.to_detail_string()));
      throw;
   }
}

BOOST_AUTO_TEST_SUITE_END()
