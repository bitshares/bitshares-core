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

BOOST_AUTO_TEST_CASE( samet_fund_create_delete_proposal_test )
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

      // Sam is able to delete his own fund
      asset released = delete_samet_fund( sam_id, sf1_id );

      BOOST_REQUIRE( !db.find( sf1_id ) );
      BOOST_REQUIRE( db.find( sf2_id ) );
      BOOST_REQUIRE( db.find( sf3_id ) );

      BOOST_CHECK( released == asset( 10000, core_id ) );

      expected_balance_sam_core += 10000;
      check_balances();

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

      // Sam is still able to delete the fund
      released = delete_samet_fund( sam_id, sf3_id );
      BOOST_REQUIRE( !db.find( sf3_id ) );

      BOOST_CHECK( released == asset( 10, eur_id ) );

      expected_balance_sam_eur += 10;
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

BOOST_AUTO_TEST_SUITE_END()
