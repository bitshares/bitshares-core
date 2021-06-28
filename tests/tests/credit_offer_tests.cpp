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
#include <graphene/chain/credit_offer_object.hpp>
#include <graphene/chain/proposal_object.hpp>

#include <graphene/app/api.hpp>

#include <boost/test/unit_test.hpp>

using namespace graphene::chain;
using namespace graphene::chain::test;

BOOST_FIXTURE_TEST_SUITE( credit_offer_tests, database_fixture )

BOOST_AUTO_TEST_CASE( credit_offer_hardfork_time_test )
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

      const asset_object& usd = create_user_issued_asset( "MYUSD" );
      asset_id_type usd_id = usd.id;
      issue_uia( sam, usd.amount(init_amount) );

      // Before the hard fork, unable to create a credit offer or transact against a credit offer or a credit deal,
      // or do any of them with proposals
      flat_map<asset_id_type, price> collateral_map;
      collateral_map[usd_id] = price( asset(1), asset(1, usd_id) );
      BOOST_CHECK_THROW( create_credit_offer( sam_id, core.id, 10000, 100, 3600, 0, false,
                                              db.head_block_time() + fc::days(1), collateral_map, {} ),
                         fc::exception );

      credit_offer_id_type tmp_co_id;
      credit_deal_id_type  tmp_cd_id;
      BOOST_CHECK_THROW( delete_credit_offer( sam_id, tmp_co_id ), fc::exception );
      BOOST_CHECK_THROW( update_credit_offer( sam_id, tmp_co_id, core.amount(100), 200, {}, {}, {}, {}, {}, {} ),
                         fc::exception );
      BOOST_CHECK_THROW( borrow_from_credit_offer( sam_id, tmp_co_id, core.amount(100), usd.amount(1000) ),
                         fc::exception );
      BOOST_CHECK_THROW( repay_credit_deal( sam_id, tmp_cd_id, core.amount(100), core.amount(100) ),
                         fc::exception );

      credit_offer_create_operation cop = make_credit_offer_create_op( sam_id, core.id, 10000, 100, 3600, 0, false,
                                              db.head_block_time() + fc::days(1), collateral_map, {} );
      BOOST_CHECK_THROW( propose( cop ), fc::exception );

      credit_offer_delete_operation dop = make_credit_offer_delete_op( sam_id, tmp_co_id );
      BOOST_CHECK_THROW( propose( dop ), fc::exception );

      credit_offer_update_operation uop = make_credit_offer_update_op( sam_id, tmp_co_id, core.amount(100), 200,
                                                                       {}, {}, {}, {}, {}, {} );
      BOOST_CHECK_THROW( propose( uop ), fc::exception );

      credit_offer_accept_operation aop = make_credit_offer_accept_op( sam_id, tmp_co_id, core.amount(100),
                                                                       usd.amount(1000) );
      BOOST_CHECK_THROW( propose( aop ), fc::exception );

      credit_deal_repay_operation rop = make_credit_deal_repay_op( sam_id, tmp_cd_id, core.amount(100),
                                                                   core.amount(100) );
      BOOST_CHECK_THROW( propose( rop ), fc::exception );

      credit_deal_expired_operation eop( tmp_cd_id, tmp_co_id, sam_id, account_id_type(),
                                         core.amount(1), usd.amount(2), 1 );
      BOOST_CHECK_THROW( eop.validate(), fc::exception );
      BOOST_CHECK_THROW( propose( eop ), fc::exception );

      trx.operations.clear();
      trx.operations.push_back( eop );

      for( auto& o : trx.operations ) db.current_fee_schedule().set_fee(o);
      BOOST_CHECK_THROW( trx.validate(), fc::exception );
      set_expiration( db, trx );
      BOOST_CHECK_THROW( PUSH_TX(db, trx, ~0), fc::exception );

   } catch (fc::exception& e) {
      edump((e.to_detail_string()));
      throw;
   }
}

BOOST_AUTO_TEST_CASE( credit_offer_crud_and_proposal_test )
{ try {

      // Pass the hard fork time
      generate_blocks( HARDFORK_CORE_2362_TIME );
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

      account_id_type no_account_id( sam.id + 1000 );
      BOOST_REQUIRE( !db.find( no_account_id ) );

      // Able to propose
      credit_offer_id_type tmp_co_id;
      credit_deal_id_type  tmp_cd_id;
      {
         flat_map<asset_id_type, price> collateral_map;
         collateral_map[usd_id] = price( asset(1), asset(1, usd_id) );

         credit_offer_create_operation cop = make_credit_offer_create_op( sam_id, core.id, 10000, 100, 3600, 0, false,
                                              db.head_block_time() + fc::days(1), collateral_map, {} );
         propose( cop );

         credit_offer_delete_operation dop = make_credit_offer_delete_op( sam_id, tmp_co_id );
         propose( dop );

         credit_offer_update_operation uop = make_credit_offer_update_op( sam_id, tmp_co_id, core.amount(100), 200,
                                                                       {}, {}, {}, {}, {}, {} );
         propose( uop );

         credit_offer_accept_operation aop = make_credit_offer_accept_op( sam_id, tmp_co_id, core.amount(100),
                                                                       usd.amount(1000) );
         propose( aop );

         credit_deal_repay_operation rop = make_credit_deal_repay_op( sam_id, tmp_cd_id, core.amount(100),
                                                                   core.amount(100) );
         propose( rop );
      }

      // Test virtual operation
      {
         credit_deal_expired_operation eop( tmp_cd_id, tmp_co_id, sam_id, account_id_type(),
                                         core.amount(1), usd.amount(2), 1 );
         BOOST_CHECK_THROW( eop.validate(), fc::exception );
         BOOST_CHECK_THROW( propose( eop ), fc::exception );

         trx.operations.clear();
         trx.operations.push_back( eop );

         for( auto& o : trx.operations ) db.current_fee_schedule().set_fee(o);
         BOOST_CHECK_THROW( trx.validate(), fc::exception );
         set_expiration( db, trx );
         BOOST_CHECK_THROW( PUSH_TX(db, trx, ~0), fc::exception );
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
         BOOST_CHECK_EQUAL( db.get_balance( por_id, core_id ).amount.value, 0 );
         BOOST_CHECK_EQUAL( db.get_balance( por_id, usd_id ).amount.value, 0 );
         BOOST_CHECK_EQUAL( db.get_balance( por_id, eur_id ).amount.value, 0 );
      };

      check_balances();

      // Able to create credit offers with valid data
      // 1.
      auto disable_time1 = db.head_block_time() - fc::minutes(1); // a time in the past

      flat_map<asset_id_type, price> collateral_map1;
      collateral_map1[usd_id] = price( asset(1), asset(2, usd_id) );

      const credit_offer_object& coo1 = create_credit_offer( sam_id, core.id, 10000, 100, 3600, 0, false,
                                              disable_time1, collateral_map1, {} );
      credit_offer_id_type co1_id = coo1.id;
      BOOST_CHECK( coo1.owner_account == sam_id );
      BOOST_CHECK( coo1.asset_type == core.id );
      BOOST_CHECK( coo1.total_balance == 10000 );
      BOOST_CHECK( coo1.current_balance == 10000 );
      BOOST_CHECK( coo1.fee_rate == 100u );
      BOOST_CHECK( coo1.max_duration_seconds == 3600u );
      BOOST_CHECK( coo1.min_deal_amount == 0 );
      BOOST_CHECK( coo1.enabled == false );
      BOOST_CHECK( coo1.auto_disable_time == disable_time1 );
      BOOST_CHECK( coo1.acceptable_collateral == collateral_map1 );
      BOOST_CHECK( coo1.acceptable_borrowers.empty() );

      expected_balance_sam_core -= 10000;
      check_balances();

      // 2.
      auto duration2 = GRAPHENE_MAX_CREDIT_DEAL_SECS;
      auto disable_time2 = db.head_block_time() + fc::days(GRAPHENE_MAX_CREDIT_OFFER_DAYS);

      flat_map<asset_id_type, price> collateral_map2;
      collateral_map2[core_id] = price( asset(2, usd_id), asset(3) );
      collateral_map2[eur_id] = price( asset(3, usd_id), asset(4, eur_id) );

      flat_map<account_id_type, share_type> borrower_map2;
      borrower_map2[account_id_type()] = 0;
      borrower_map2[sam_id] = 1;
      borrower_map2[ted_id] = GRAPHENE_MAX_SHARE_SUPPLY;

      const credit_offer_object& coo2 = create_credit_offer( ted_id, usd_id, 1, 10000000u, duration2, 10000, true,
                                              disable_time2, collateral_map2, borrower_map2 );
      credit_offer_id_type co2_id = coo2.id;
      BOOST_CHECK( coo2.owner_account == ted_id );
      BOOST_CHECK( coo2.asset_type == usd_id );
      BOOST_CHECK( coo2.total_balance == 1 );
      BOOST_CHECK( coo2.current_balance == 1 );
      BOOST_CHECK( coo2.fee_rate == 10000000u );
      BOOST_CHECK( coo2.max_duration_seconds == duration2 );
      BOOST_CHECK( coo2.min_deal_amount == 10000 );
      BOOST_CHECK( coo2.enabled == true );
      BOOST_CHECK( coo2.auto_disable_time == disable_time2 );
      BOOST_CHECK( coo2.acceptable_collateral == collateral_map2 );
      BOOST_CHECK( coo2.acceptable_borrowers == borrower_map2 );

      expected_balance_ted_usd -= 1;
      check_balances();

      // 3.
      // A time far in the future
      auto disable_time3 = db.head_block_time() + fc::seconds(GRAPHENE_MAX_CREDIT_OFFER_SECS + 1);

      flat_map<asset_id_type, price> collateral_map3;
      collateral_map3[usd_id] = price( asset(1, eur_id), asset(2, usd_id) );

      const credit_offer_object& coo3 = create_credit_offer( sam_id, eur_id, 10, 1, 30, 1, false,
                                              disable_time3, collateral_map3, {} ); // Account is whitelisted
      credit_offer_id_type co3_id = coo3.id;
      BOOST_CHECK( coo3.owner_account == sam_id );
      BOOST_CHECK( coo3.asset_type == eur_id );
      BOOST_CHECK( coo3.total_balance == 10 );
      BOOST_CHECK( coo3.current_balance == 10 );
      BOOST_CHECK( coo3.fee_rate == 1u );
      BOOST_CHECK( coo3.max_duration_seconds == 30u );
      BOOST_CHECK( coo3.min_deal_amount == 1 );
      BOOST_CHECK( coo3.enabled == false );
      BOOST_CHECK( coo3.auto_disable_time == disable_time3 );
      BOOST_CHECK( coo3.acceptable_collateral == collateral_map3 );
      BOOST_CHECK( coo3.acceptable_borrowers.empty() );

      expected_balance_sam_eur -= 10;
      check_balances();

      // Unable to create a credit offer with invalid data
      auto too_big_duration = GRAPHENE_MAX_CREDIT_DEAL_SECS + 1;
      auto too_late_disable_time = db.head_block_time() + fc::seconds(GRAPHENE_MAX_CREDIT_OFFER_SECS + 1);

      flat_map<asset_id_type, price> empty_collateral_map;

      flat_map<asset_id_type, price> invalid_collateral_map1_1;
      invalid_collateral_map1_1[usd_id] = price( asset(1), asset(0, usd_id) ); // zero amount

      flat_map<asset_id_type, price> invalid_collateral_map1_2;
      invalid_collateral_map1_2[usd_id] = price( asset(1), asset(2, eur_id) ); // quote asset type mismatch

      flat_map<asset_id_type, price> invalid_collateral_map1_3;
      invalid_collateral_map1_3[usd_id] = price( asset(1), asset(2, usd_id) );
      invalid_collateral_map1_3[eur_id] = price( asset(1), asset(2, usd_id) ); // quote asset type mismatch

      flat_map<asset_id_type, price> invalid_collateral_map1_4; // amount too big
      invalid_collateral_map1_4[usd_id] = price( asset(GRAPHENE_MAX_SHARE_SUPPLY + 1), asset(1, usd_id) );

      flat_map<asset_id_type, price> invalid_collateral_map1_5;
      invalid_collateral_map1_5[usd_id] = price( asset(2, usd_id), asset(1) ); // base asset type mismatch

      flat_map<asset_id_type, price> invalid_collateral_map1_6;
      invalid_collateral_map1_6[usd_id] = price( asset(1), asset(2, usd_id) );
      invalid_collateral_map1_6[eur_id] = price( asset(1, usd_id), asset(2, eur_id) ); // base asset type mismatch

      flat_map<asset_id_type, price> invalid_collateral_map1_7;
      invalid_collateral_map1_7[no_asset_id] = price( asset(1), asset(2, no_asset_id) ); // asset does not exist

      auto invalid_borrower_map2_1 = borrower_map2;
      invalid_borrower_map2_1[sam_id] = -1; // negative amount

      auto invalid_borrower_map2_2 = borrower_map2;
      invalid_borrower_map2_2[ted_id] = GRAPHENE_MAX_SHARE_SUPPLY + 1; // amount too big

      auto invalid_borrower_map2_3 = borrower_map2;
      invalid_borrower_map2_3[no_account_id] = 1; // account does not exist

      // Non-positive balance
      BOOST_CHECK_THROW( create_credit_offer( sam_id, core.id, 0, 100, 3600, 0, false,
                                              disable_time1, collateral_map1, {} ),
                         fc::exception );
      BOOST_CHECK_THROW( create_credit_offer( ted_id, usd_id, -1, 10000000u, duration2, 10000, true,
                                              disable_time2, collateral_map2, borrower_map2 ),
                         fc::exception );
      // Insufficient account balance
      BOOST_CHECK_THROW( create_credit_offer( por_id, usd_id, 1, 10000000u, duration2, 10000, true,
                                              disable_time2, collateral_map2, borrower_map2 ),
                         fc::exception );
      // Nonexistent asset type
      BOOST_CHECK_THROW( create_credit_offer( sam_id, no_asset_id, 10000, 100, 3600, 0, false,
                                              disable_time1, collateral_map1, {} ),
                         fc::exception );
      // Duration too big
      BOOST_CHECK_THROW( create_credit_offer( ted_id, usd_id, 1, 10000000u, too_big_duration, 10000, true,
                                              disable_time2, collateral_map2, borrower_map2 ),
                         fc::exception );
      // Negative minimum deal amount
      BOOST_CHECK_THROW( create_credit_offer( sam_id, core.id, 10000, 100, 3600, -1, false,
                                              disable_time1, collateral_map1, {} ),
                         fc::exception );
      // Too big minimum deal amount
      BOOST_CHECK_THROW( create_credit_offer( sam_id, core.id, 10000, 100, 3600, GRAPHENE_MAX_SHARE_SUPPLY + 1, false,
                                              disable_time1, collateral_map1, {} ),
                         fc::exception );
      // Auto-disable time in the past and the offer is enabled
      BOOST_CHECK_THROW( create_credit_offer( sam_id, core.id, 10000, 100, 3600, 0, true,
                                              disable_time1, collateral_map1, {} ),
                         fc::exception );
      // Auto-disable time too late
      BOOST_CHECK_THROW( create_credit_offer( ted_id, usd_id, 1, 10000000u, duration2, 10000, true,
                                              too_late_disable_time, collateral_map2, borrower_map2 ),
                         fc::exception );
      // Empty allowed collateral map
      BOOST_CHECK_THROW( create_credit_offer( sam_id, core.id, 10000, 100, 3600, 0, false,
                                              disable_time1, empty_collateral_map, {} ),
                         fc::exception );
      // Invalid allowed collateral map
      BOOST_CHECK_THROW( create_credit_offer( sam_id, core.id, 10000, 100, 3600, 0, false,
                                              disable_time1, invalid_collateral_map1_1, {} ),
                         fc::exception );
      BOOST_CHECK_THROW( create_credit_offer( sam_id, core.id, 10000, 100, 3600, 0, false,
                                              disable_time1, invalid_collateral_map1_2, {} ),
                         fc::exception );
      BOOST_CHECK_THROW( create_credit_offer( sam_id, core.id, 10000, 100, 3600, 0, false,
                                              disable_time1, invalid_collateral_map1_3, {} ),
                         fc::exception );
      BOOST_CHECK_THROW( create_credit_offer( sam_id, core.id, 10000, 100, 3600, 0, false,
                                              disable_time1, invalid_collateral_map1_4, {} ),
                         fc::exception );
      BOOST_CHECK_THROW( create_credit_offer( sam_id, core.id, 10000, 100, 3600, 0, false,
                                              disable_time1, invalid_collateral_map1_5, {} ),
                         fc::exception );
      BOOST_CHECK_THROW( create_credit_offer( sam_id, core.id, 10000, 100, 3600, 0, false,
                                              disable_time1, invalid_collateral_map1_6, {} ),
                         fc::exception );
      BOOST_CHECK_THROW( create_credit_offer( sam_id, core.id, 10000, 100, 3600, 0, false,
                                              disable_time1, invalid_collateral_map1_7, {} ),
                         fc::exception );
      // Invalid acceptable borrowers map
      BOOST_CHECK_THROW( create_credit_offer( ted_id, usd_id, 1, 10000000u, duration2, 10000, true,
                                              disable_time2, collateral_map2, invalid_borrower_map2_1 ),
                         fc::exception );
      BOOST_CHECK_THROW( create_credit_offer( ted_id, usd_id, 1, 10000000u, duration2, 10000, true,
                                              disable_time2, collateral_map2, invalid_borrower_map2_2 ),
                         fc::exception );
      BOOST_CHECK_THROW( create_credit_offer( ted_id, usd_id, 1, 10000000u, duration2, 10000, true,
                                              disable_time2, collateral_map2, invalid_borrower_map2_3 ),
                         fc::exception );
      // Account is not whitelisted
      BOOST_CHECK_THROW( create_credit_offer( ted_id, eur_id, 10, 1, 30, 1, false,
                                              disable_time3, collateral_map3, {} ),
                         fc::exception );

      check_balances();

      // Uable to update a credit offer with invalid data
      // Changes nothing
      BOOST_CHECK_THROW( update_credit_offer( sam_id, co1_id, {}, {}, {}, {}, {}, {}, {}, {} ),
                         fc::exception );
      // Object owner mismatch
      BOOST_CHECK_THROW( update_credit_offer( ted_id, co1_id, asset(1), {}, {}, {}, {}, {}, {}, {} ),
                         fc::exception );
      // Zero delta
      BOOST_CHECK_THROW( update_credit_offer( sam_id, co1_id, asset(0), {}, {}, {}, {}, {}, {}, {} ),
                         fc::exception );
      // Asset type mismatch
      BOOST_CHECK_THROW( update_credit_offer( sam_id, co1_id, asset(1, usd_id), {}, {}, {}, {}, {}, {}, {} ),
                         fc::exception );
      // Trying to withdraw too much
      BOOST_CHECK_THROW( update_credit_offer( sam_id, co1_id, asset(-10000), {}, {}, {}, {}, {}, {}, {} ),
                         fc::exception );
      // Insufficient account balance
      BOOST_CHECK_THROW( update_credit_offer( sam_id, co1_id, asset(init_amount), {}, {}, {}, {}, {}, {}, {} ),
                         fc::exception );
      // Duration too big
      BOOST_CHECK_THROW( update_credit_offer( sam_id, co1_id, {}, {}, too_big_duration, {}, {}, {}, {}, {} ),
                         fc::exception );
      // Invalid minimum deal amount
      BOOST_CHECK_THROW( update_credit_offer( sam_id, co1_id, {}, {}, {}, -1, {}, {}, {}, {} ),
                         fc::exception );
      BOOST_CHECK_THROW( update_credit_offer( sam_id, co1_id, {}, {}, {},
                                              GRAPHENE_MAX_SHARE_SUPPLY + 1, {}, {}, {}, {} ),
                         fc::exception );
      // Enabled but auto-disable time in the past
      BOOST_CHECK_THROW( update_credit_offer( sam_id, co1_id, {}, {}, {}, {}, true, {}, {}, {} ),
                         fc::exception );
      BOOST_CHECK_THROW( update_credit_offer( ted_id, co2_id, {}, {}, {}, {}, {}, disable_time1, {}, {} ),
                         fc::exception );
      // Enabled but auto-disable time too late
      BOOST_CHECK_THROW( update_credit_offer( sam_id, co3_id, {}, {}, {}, {}, true, {}, {}, {} ),
                         fc::exception );
      BOOST_CHECK_THROW( update_credit_offer( ted_id, co2_id, {}, {}, {}, {}, {}, disable_time3, {}, {} ),
                         fc::exception );
      // Invalid collateral map
      BOOST_CHECK_THROW( update_credit_offer( sam_id, co1_id, {}, {}, {}, {}, {}, {}, empty_collateral_map, {} ),
                         fc::exception );
      BOOST_CHECK_THROW( update_credit_offer( sam_id, co1_id, {}, {}, {}, {}, {}, {}, invalid_collateral_map1_1, {} ),
                         fc::exception );
      BOOST_CHECK_THROW( update_credit_offer( sam_id, co1_id, {}, {}, {}, {}, {}, {}, invalid_collateral_map1_2, {} ),
                         fc::exception );
      BOOST_CHECK_THROW( update_credit_offer( sam_id, co1_id, {}, {}, {}, {}, {}, {}, invalid_collateral_map1_3, {} ),
                         fc::exception );
      BOOST_CHECK_THROW( update_credit_offer( sam_id, co1_id, {}, {}, {}, {}, {}, {}, invalid_collateral_map1_4, {} ),
                         fc::exception );
      BOOST_CHECK_THROW( update_credit_offer( sam_id, co1_id, {}, {}, {}, {}, {}, {}, invalid_collateral_map1_5, {} ),
                         fc::exception );
      BOOST_CHECK_THROW( update_credit_offer( sam_id, co1_id, {}, {}, {}, {}, {}, {}, invalid_collateral_map1_6, {} ),
                         fc::exception );
      BOOST_CHECK_THROW( update_credit_offer( sam_id, co1_id, {}, {}, {}, {}, {}, {}, invalid_collateral_map1_7, {} ),
                         fc::exception );
      // Invalid borrowers map
      BOOST_CHECK_THROW( update_credit_offer( sam_id, co1_id, {}, {}, {}, {}, {}, {}, {}, invalid_borrower_map2_1 ),
                         fc::exception );
      BOOST_CHECK_THROW( update_credit_offer( sam_id, co1_id, {}, {}, {}, {}, {}, {}, {}, invalid_borrower_map2_2 ),
                         fc::exception );
      BOOST_CHECK_THROW( update_credit_offer( sam_id, co1_id, {}, {}, {}, {}, {}, {}, {}, invalid_borrower_map2_3 ),
                         fc::exception );

      check_balances();

      // Able to update a credit offer with valid data
      // Only deposit
      update_credit_offer( sam_id, co1_id, asset(1), {}, {}, {}, {}, {}, {}, {} );

      BOOST_CHECK( co1_id(db).owner_account == sam_id );
      BOOST_CHECK( co1_id(db).asset_type == core_id );
      BOOST_CHECK( co1_id(db).total_balance == 10001 );
      BOOST_CHECK( co1_id(db).current_balance == 10001 );
      BOOST_CHECK( co1_id(db).fee_rate == 100u );
      BOOST_CHECK( co1_id(db).max_duration_seconds == 3600u );
      BOOST_CHECK( co1_id(db).min_deal_amount == 0 );
      BOOST_CHECK( co1_id(db).enabled == false );
      BOOST_CHECK( co1_id(db).auto_disable_time == disable_time1 );
      BOOST_CHECK( co1_id(db).acceptable_collateral == collateral_map1 );
      BOOST_CHECK( co1_id(db).acceptable_borrowers.empty() );

      expected_balance_sam_core -= 1;
      check_balances();

      // Only update fee rate
      update_credit_offer( sam_id, co1_id, {}, 101u, {}, {}, {}, {}, {}, {} );

      BOOST_CHECK( co1_id(db).owner_account == sam_id );
      BOOST_CHECK( co1_id(db).asset_type == core_id );
      BOOST_CHECK( co1_id(db).total_balance == 10001 );
      BOOST_CHECK( co1_id(db).current_balance == 10001 );
      BOOST_CHECK( co1_id(db).fee_rate == 101u );
      BOOST_CHECK( co1_id(db).max_duration_seconds == 3600u );
      BOOST_CHECK( co1_id(db).min_deal_amount == 0 );
      BOOST_CHECK( co1_id(db).enabled == false );
      BOOST_CHECK( co1_id(db).auto_disable_time == disable_time1 );
      BOOST_CHECK( co1_id(db).acceptable_collateral == collateral_map1 );
      BOOST_CHECK( co1_id(db).acceptable_borrowers.empty() );

      check_balances();

      // Withdraw, update fee rate and other data
      flat_map<asset_id_type, price> collateral_map1_1;
      collateral_map1_1[usd_id] = price( asset(1), asset(2, usd_id) );
      collateral_map1_1[eur_id] = price( asset(1), asset(3, eur_id) );

      update_credit_offer( sam_id, co1_id, asset(-9999), 10u, 600u, 100, true,
                           db.head_block_time() + fc::days(10), collateral_map1_1, borrower_map2 );

      BOOST_CHECK( co1_id(db).owner_account == sam_id );
      BOOST_CHECK( co1_id(db).asset_type == core_id );
      BOOST_CHECK( co1_id(db).total_balance == 2 );
      BOOST_CHECK( co1_id(db).current_balance == 2 );
      BOOST_CHECK( co1_id(db).fee_rate == 10u );
      BOOST_CHECK( co1_id(db).max_duration_seconds == 600u );
      BOOST_CHECK( co1_id(db).min_deal_amount == 100 );
      BOOST_CHECK( co1_id(db).enabled == true );
      BOOST_CHECK( co1_id(db).auto_disable_time == db.head_block_time() + fc::days(10) );
      BOOST_CHECK( co1_id(db).acceptable_collateral == collateral_map1_1 );
      BOOST_CHECK( co1_id(db).acceptable_borrowers == borrower_map2 );

      expected_balance_sam_core += 9999;
      check_balances();

      // Sam is able to delete his own credit offer
      asset released = delete_credit_offer( sam_id, co1_id );

      BOOST_REQUIRE( !db.find( co1_id ) );
      BOOST_REQUIRE( db.find( co2_id ) );
      BOOST_REQUIRE( db.find( co3_id ) );

      BOOST_CHECK( released == asset( 2, core_id ) );

      expected_balance_sam_core += 2;
      check_balances();

      // Unable to update a credit offer that does not exist
      BOOST_CHECK_THROW( update_credit_offer( sam_id, co1_id, asset(1), {}, {}, {}, {}, {}, {}, {} ),
                         fc::exception );
      // Unable to delete a credit offer that does not exist
      BOOST_CHECK_THROW( delete_credit_offer( sam_id, co1_id ), fc::exception );
      // Unable to delete a credit offer that is not owned by him
      BOOST_CHECK_THROW( delete_credit_offer( sam_id, co2_id ), fc::exception );

      BOOST_REQUIRE( !db.find( co1_id ) );
      BOOST_REQUIRE( db.find( co2_id ) );
      BOOST_REQUIRE( db.find( co3_id ) );

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

      // Sam is now unable to deposit to the credit offer
      BOOST_CHECK_THROW( update_credit_offer( sam_id, co3_id, asset(1, eur_id), {}, {}, {}, {}, {}, {}, {} ),
                         fc::exception );

      BOOST_CHECK( co3_id(db).owner_account == sam_id );
      BOOST_CHECK( co3_id(db).asset_type == eur_id );
      BOOST_CHECK( co3_id(db).total_balance == 10 );
      BOOST_CHECK( co3_id(db).current_balance == 10 );
      BOOST_CHECK( co3_id(db).fee_rate == 1u );
      BOOST_CHECK( co3_id(db).max_duration_seconds == 30u );
      BOOST_CHECK( co3_id(db).min_deal_amount == 1 );
      BOOST_CHECK( co3_id(db).enabled == false );
      BOOST_CHECK( co3_id(db).auto_disable_time == disable_time3 );
      BOOST_CHECK( co3_id(db).acceptable_collateral == collateral_map3 );
      BOOST_CHECK( co3_id(db).acceptable_borrowers.empty() );

      check_balances();

      // Sam is still able to withdraw from the credit offer
      update_credit_offer( sam_id, co3_id, asset(-1, eur_id), {}, {}, {}, {}, {}, {}, {} );

      BOOST_CHECK( co3_id(db).owner_account == sam_id );
      BOOST_CHECK( co3_id(db).asset_type == eur_id );
      BOOST_CHECK( co3_id(db).total_balance == 9 );
      BOOST_CHECK( co3_id(db).current_balance == 9 );
      BOOST_CHECK( co3_id(db).fee_rate == 1u );
      BOOST_CHECK( co3_id(db).max_duration_seconds == 30u );
      BOOST_CHECK( co3_id(db).min_deal_amount == 1 );
      BOOST_CHECK( co3_id(db).enabled == false );
      BOOST_CHECK( co3_id(db).auto_disable_time == disable_time3 );
      BOOST_CHECK( co3_id(db).acceptable_collateral == collateral_map3 );
      BOOST_CHECK( co3_id(db).acceptable_borrowers.empty() );

      expected_balance_sam_eur += 1;
      check_balances();

      // Sam is still able to update other data
      flat_map<asset_id_type, price> collateral_map3_1;
      collateral_map3_1[core_id] = price( asset(2, eur_id), asset(5, core_id) );

      update_credit_offer( sam_id, co3_id, {}, 10u, 600u, 100, true,
                           disable_time2, collateral_map3_1, borrower_map2 );

      BOOST_CHECK( co3_id(db).owner_account == sam_id );
      BOOST_CHECK( co3_id(db).asset_type == eur_id );
      BOOST_CHECK( co3_id(db).total_balance == 9 );
      BOOST_CHECK( co3_id(db).current_balance == 9 );
      BOOST_CHECK( co3_id(db).fee_rate == 10u );
      BOOST_CHECK( co3_id(db).max_duration_seconds == 600u );
      BOOST_CHECK( co3_id(db).min_deal_amount == 100 );
      BOOST_CHECK( co3_id(db).enabled == true );
      BOOST_CHECK( co3_id(db).auto_disable_time == disable_time2 );
      BOOST_CHECK( co3_id(db).acceptable_collateral == collateral_map3_1 );
      BOOST_CHECK( co3_id(db).acceptable_borrowers == borrower_map2 );

      check_balances();

      // Sam is still able to delete the credit offer
      released = delete_credit_offer( sam_id, co3_id );
      BOOST_REQUIRE( !db.find( co3_id ) );

      BOOST_CHECK( released == asset( 9, eur_id ) );

      expected_balance_sam_eur += 9;
      check_balances();

      // Sam is unable to recreate the credit offer
      BOOST_CHECK_THROW( create_credit_offer( sam_id, eur_id, 10, 1, 30, 1, false,
                                              disable_time3, collateral_map3, {} ),
                         fc::exception );
      check_balances();

      generate_block();

   } catch (fc::exception& e) {
      edump((e.to_detail_string()));
      throw;
   }
}

BOOST_AUTO_TEST_CASE( credit_offer_borrow_repay_test )
{ try {

      // Pass the hard fork time
      generate_blocks( HARDFORK_CORE_2362_TIME );
      set_expiration( db, trx );

      ACTORS((ray)(sam)(ted)(por));

      auto init_amount = 10000000 * GRAPHENE_BLOCKCHAIN_PRECISION;
      fund( ray, asset(init_amount) );
      fund( sam, asset(init_amount) );
      fund( ted, asset(init_amount) );

      const asset_object& core = asset_id_type()(db);
      asset_id_type core_id;

      const asset_object& usd = create_user_issued_asset( "MYUSD", ted, white_list );
      asset_id_type usd_id = usd.id;
      issue_uia( ray, usd.amount(init_amount) );
      issue_uia( sam, usd.amount(init_amount) );
      issue_uia( ted, usd.amount(init_amount) );

      const asset_object& eur = create_user_issued_asset( "MYEUR", sam, white_list );
      asset_id_type eur_id = eur.id;
      issue_uia( ray, eur.amount(init_amount) );
      issue_uia( sam, eur.amount(init_amount) );
      issue_uia( ted, eur.amount(init_amount) );

      const asset_object& cny = create_user_issued_asset( "MYCNY" );
      asset_id_type cny_id = cny.id;
      issue_uia( ray, cny.amount(init_amount) );
      issue_uia( sam, cny.amount(init_amount) );
      issue_uia( ted, cny.amount(init_amount) );

      // Make a whitelist USD managed by Ted
      {
         BOOST_TEST_MESSAGE( "Setting up whitelisting" );
         asset_update_operation uop;
         uop.asset_to_update = usd.id;
         uop.issuer = ted_id;
         uop.new_options = usd.options;
         // The whitelist is managed by Ted
         uop.new_options.whitelist_authorities.insert(ted_id);
         trx.operations.clear();
         trx.operations.push_back(uop);
         PUSH_TX( db, trx, ~0 );

         // Upgrade Ted so that he can manage the whitelist
         upgrade_to_lifetime_member( ted_id );

         // Add Sam and Ray to the whitelist
         account_whitelist_operation wop;
         wop.authorizing_account = ted_id;
         wop.account_to_list = sam_id;
         wop.new_listing = account_whitelist_operation::white_listed;
         trx.operations.clear();
         trx.operations.push_back(wop);
         wop.account_to_list = ray_id;
         trx.operations.push_back(wop);
         PUSH_TX( db, trx, ~0 );
      }

      // Make a whitelist : EUR managed by Sam
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

         // Add Ted to the whitelist
         account_whitelist_operation wop;
         wop.authorizing_account = sam_id;
         wop.account_to_list = ted_id;
         wop.new_listing = account_whitelist_operation::white_listed;
         trx.operations.clear();
         trx.operations.push_back(wop);
         PUSH_TX( db, trx, ~0 );
      }

      asset_id_type no_asset_id( core.id + 100 );
      BOOST_REQUIRE( !db.find( no_asset_id ) );

      int64_t expected_balance_ray_core = init_amount;
      int64_t expected_balance_ray_usd = init_amount;
      int64_t expected_balance_ray_eur = init_amount;
      int64_t expected_balance_ray_cny = init_amount;
      int64_t expected_balance_sam_core = init_amount;
      int64_t expected_balance_sam_usd = init_amount;
      int64_t expected_balance_sam_eur = init_amount;
      int64_t expected_balance_sam_cny = init_amount;
      int64_t expected_balance_ted_core = init_amount;
      int64_t expected_balance_ted_usd = init_amount;
      int64_t expected_balance_ted_eur = init_amount;
      int64_t expected_balance_ted_cny = init_amount;

      const auto& check_balances = [&]() {
         BOOST_CHECK_EQUAL( db.get_balance( ray_id, core_id ).amount.value, expected_balance_ray_core );
         BOOST_CHECK_EQUAL( db.get_balance( ray_id, usd_id ).amount.value, expected_balance_ray_usd );
         BOOST_CHECK_EQUAL( db.get_balance( ray_id, eur_id ).amount.value, expected_balance_ray_eur );
         BOOST_CHECK_EQUAL( db.get_balance( ray_id, cny_id ).amount.value, expected_balance_ray_cny );
         BOOST_CHECK_EQUAL( db.get_balance( sam_id, core_id ).amount.value, expected_balance_sam_core );
         BOOST_CHECK_EQUAL( db.get_balance( sam_id, usd_id ).amount.value, expected_balance_sam_usd );
         BOOST_CHECK_EQUAL( db.get_balance( sam_id, eur_id ).amount.value, expected_balance_sam_eur );
         BOOST_CHECK_EQUAL( db.get_balance( sam_id, cny_id ).amount.value, expected_balance_sam_cny );
         BOOST_CHECK_EQUAL( db.get_balance( ted_id, core_id ).amount.value, expected_balance_ted_core );
         BOOST_CHECK_EQUAL( db.get_balance( ted_id, usd_id ).amount.value, expected_balance_ted_usd );
         BOOST_CHECK_EQUAL( db.get_balance( ted_id, eur_id ).amount.value, expected_balance_ted_eur );
         BOOST_CHECK_EQUAL( db.get_balance( ted_id, cny_id ).amount.value, expected_balance_ted_cny );
         BOOST_CHECK_EQUAL( db.get_balance( por_id, core_id ).amount.value, 0 );
         BOOST_CHECK_EQUAL( db.get_balance( por_id, usd_id ).amount.value, 0 );
         BOOST_CHECK_EQUAL( db.get_balance( por_id, eur_id ).amount.value, 0 );
         BOOST_CHECK_EQUAL( db.get_balance( por_id, cny_id ).amount.value, 0 );
      };

      check_balances();

      // Unable to borrow : the credit offer is disabled
      credit_offer_id_type tmp_co_id;
      BOOST_CHECK_THROW( borrow_from_credit_offer( ray_id, tmp_co_id, asset(100), asset(200, usd_id) ),
                         fc::exception );

      // create a credit offers
      auto disable_time1 = db.head_block_time() + fc::minutes(20); // 20 minutes after init

      flat_map<asset_id_type, price> collateral_map1;
      collateral_map1[usd_id] = price( asset(1), asset(2, usd_id) );
      collateral_map1[eur_id] = price( asset(1), asset(1, eur_id) );

      const credit_offer_object& coo1 = create_credit_offer( sam_id, core.id, 10000, 30000, 3600, 0, false,
                                              disable_time1, collateral_map1, {} );
      credit_offer_id_type co1_id = coo1.id;
      BOOST_CHECK( co1_id(db).owner_account == sam_id );
      BOOST_CHECK( co1_id(db).asset_type == core.id );
      BOOST_CHECK( co1_id(db).total_balance == 10000 );
      BOOST_CHECK( co1_id(db).current_balance == 10000 );
      BOOST_CHECK( co1_id(db).fee_rate == 30000u );
      BOOST_CHECK( co1_id(db).max_duration_seconds == 3600u );
      BOOST_CHECK( co1_id(db).min_deal_amount == 0 );
      BOOST_CHECK( co1_id(db).enabled == false );
      BOOST_CHECK( co1_id(db).auto_disable_time == disable_time1 );
      BOOST_CHECK( co1_id(db).acceptable_collateral == collateral_map1 );
      BOOST_CHECK( co1_id(db).acceptable_borrowers.empty() );

      expected_balance_sam_core -= 10000;
      check_balances();

      // Unable to borrow : the credit offer is disabled
      BOOST_CHECK_THROW( borrow_from_credit_offer( ray_id, co1_id, asset(100), asset(200, usd_id) ), fc::exception );

      // Enable the offer
      update_credit_offer( sam_id, co1_id, {}, {}, {}, {}, true, {}, {}, {} );

      BOOST_CHECK( co1_id(db).enabled == true );

      // Now able to borrow
      BOOST_TEST_MESSAGE( "Ray borrows" );
      const credit_deal_object& cdo11 = borrow_from_credit_offer( ray_id, co1_id, asset(100), asset(200, usd_id) );
      credit_deal_id_type cd11_id = cdo11.id;
      time_point_sec expected_repay_time11 = db.head_block_time() + fc::seconds(3600); // 60 minutes after init

      BOOST_CHECK( cd11_id(db).borrower == ray_id );
      BOOST_CHECK( cd11_id(db).offer_id == co1_id );
      BOOST_CHECK( cd11_id(db).offer_owner == sam_id );
      BOOST_CHECK( cd11_id(db).debt_asset == core_id );
      BOOST_CHECK( cd11_id(db).debt_amount == 100 );
      BOOST_CHECK( cd11_id(db).collateral_asset == usd_id );
      BOOST_CHECK( cd11_id(db).collateral_amount == 200 );
      BOOST_CHECK( cd11_id(db).fee_rate == 30000u );
      BOOST_CHECK( cd11_id(db).latest_repay_time == expected_repay_time11 );

      BOOST_CHECK( co1_id(db).total_balance == 10000 );
      BOOST_CHECK( co1_id(db).current_balance == 9900 );

      expected_balance_ray_core += 100;
      expected_balance_ray_usd -= 200;
      check_balances();

      // Unable to delete the credit offer : there exists unpaid debt
      BOOST_CHECK_THROW( delete_credit_offer( sam_id, co1_id ), fc::exception );
      // Unable to withdraw more than balance available
      BOOST_CHECK_THROW( update_credit_offer( sam_id, co1_id, asset(-9901), {}, {}, {}, {}, {}, {}, {} ),
                         fc::exception );

      // Unable to borrow : asset type mismatch
      BOOST_CHECK_THROW( borrow_from_credit_offer( ray_id, co1_id, asset(100, cny_id), asset(200, usd_id) ),
                         fc::exception );
      // Unable to borrow : zero or negative amount
      BOOST_CHECK_THROW( borrow_from_credit_offer( ray_id, co1_id, asset(0), asset(200, usd_id) ), fc::exception );
      BOOST_CHECK_THROW( borrow_from_credit_offer( ray_id, co1_id, asset(-1), asset(200, usd_id) ), fc::exception );
      BOOST_CHECK_THROW( borrow_from_credit_offer( ray_id, co1_id, asset(1), asset(0, usd_id) ), fc::exception );
      BOOST_CHECK_THROW( borrow_from_credit_offer( ray_id, co1_id, asset(1), asset(-1, usd_id) ), fc::exception );

      // Set a minimum deal amount
      update_credit_offer( sam_id, co1_id, {}, {}, {}, 100, {}, {}, {}, {} );

      BOOST_CHECK( co1_id(db).min_deal_amount == 100 );

      // Unable to borrow : amount too small
      BOOST_CHECK_THROW( borrow_from_credit_offer( ray_id, co1_id, asset(99), asset(200, usd_id) ), fc::exception );
      // Unable to borrow : collateral amount too small
      BOOST_CHECK_THROW( borrow_from_credit_offer( ray_id, co1_id, asset(100), asset(199, usd_id) ), fc::exception );
      // Unable to borrow : collateral not acceptable
      BOOST_CHECK_THROW( borrow_from_credit_offer( ray_id, co1_id, asset(100), asset(200, cny_id) ), fc::exception );
      // Unable to borrow : account not authorized by debt asset
      BOOST_CHECK_THROW( borrow_from_credit_offer( ted_id, co1_id, asset(100), asset(200, usd_id) ), fc::exception );
      // Unable to borrow : account not authorized by collateral asset
      BOOST_CHECK_THROW( borrow_from_credit_offer( ray_id, co1_id, asset(100), asset(200, eur_id) ), fc::exception );
      // Unable to borrow : insufficient balance in credit offer
      BOOST_CHECK_THROW( borrow_from_credit_offer( ray_id, co1_id, asset(9901), asset(20000, usd_id) ),
                         fc::exception );
      // Unable to borrow : insufficient account balance
      BOOST_CHECK_THROW( borrow_from_credit_offer( ray_id, co1_id, asset(100), asset(init_amount, usd_id) ),
                         fc::exception );

      // Able to borrow the same amount with the same collateral
      BOOST_TEST_MESSAGE( "Ray borrows more" );
      const credit_deal_object& cdo12 = borrow_from_credit_offer( ray_id, co1_id, asset(100), asset(200, usd_id) );
      credit_deal_id_type cd12_id = cdo12.id;
      time_point_sec expected_repay_time12 = db.head_block_time() + fc::seconds(3600);  // 60 minutes after init

      BOOST_CHECK( cd12_id(db).borrower == ray_id );
      BOOST_CHECK( cd12_id(db).offer_id == co1_id );
      BOOST_CHECK( cd12_id(db).offer_owner == sam_id );
      BOOST_CHECK( cd12_id(db).debt_asset == core_id );
      BOOST_CHECK( cd12_id(db).debt_amount == 100 );
      BOOST_CHECK( cd12_id(db).collateral_asset == usd_id );
      BOOST_CHECK( cd12_id(db).collateral_amount == 200 );
      BOOST_CHECK( cd12_id(db).fee_rate == 30000u );
      BOOST_CHECK( cd12_id(db).latest_repay_time == expected_repay_time12 );

      BOOST_CHECK( co1_id(db).total_balance == 10000 );
      BOOST_CHECK( co1_id(db).current_balance == 9800 );

      expected_balance_ray_core += 100;
      expected_balance_ray_usd -= 200;
      check_balances();

      // Time goes by
      generate_blocks( db.head_block_time() + fc::minutes(5) ); // now is 5 minutes after init
      set_expiration( db, trx );

      // Able to borrow the same amount with more collateral
      BOOST_TEST_MESSAGE( "Ray borrows even more" );
      const credit_deal_object& cdo13 = borrow_from_credit_offer( ray_id, co1_id, asset(100), asset(499, usd_id) );
      credit_deal_id_type cd13_id = cdo13.id;
      time_point_sec expected_repay_time13 = db.head_block_time() + fc::seconds(3600); // 65 minutes after init

      BOOST_CHECK( cd13_id(db).borrower == ray_id );
      BOOST_CHECK( cd13_id(db).offer_id == co1_id );
      BOOST_CHECK( cd13_id(db).offer_owner == sam_id );
      BOOST_CHECK( cd13_id(db).debt_asset == core_id );
      BOOST_CHECK( cd13_id(db).debt_amount == 100 );
      BOOST_CHECK( cd13_id(db).collateral_asset == usd_id );
      BOOST_CHECK( cd13_id(db).collateral_amount == 499 );
      BOOST_CHECK( cd13_id(db).fee_rate == 30000u );
      BOOST_CHECK( cd13_id(db).latest_repay_time == expected_repay_time13 );

      BOOST_CHECK( co1_id(db).total_balance == 10000 );
      BOOST_CHECK( co1_id(db).current_balance == 9700 );

      expected_balance_ray_core += 100;
      expected_balance_ray_usd -= 499;
      check_balances();

      // The offer changes
      auto collateral_map1_new = collateral_map1;
      collateral_map1_new[cny_id] = price( asset(1), asset(1, cny_id) );
      BOOST_CHECK( collateral_map1 != collateral_map1_new );

      flat_map<account_id_type, share_type> borrower_map1;
      borrower_map1[ted_id] = 300;

      update_credit_offer( sam_id, co1_id, {}, 500u, 600u, 0, {}, {}, collateral_map1_new, borrower_map1 );

      BOOST_CHECK( co1_id(db).owner_account == sam_id );
      BOOST_CHECK( co1_id(db).asset_type == core_id );
      BOOST_CHECK( co1_id(db).total_balance == 10000 );
      BOOST_CHECK( co1_id(db).current_balance == 9700 );
      BOOST_CHECK( co1_id(db).fee_rate == 500u );
      BOOST_CHECK( co1_id(db).max_duration_seconds == 600u );
      BOOST_CHECK( co1_id(db).min_deal_amount == 0 );
      BOOST_CHECK( co1_id(db).enabled == true );
      BOOST_CHECK( co1_id(db).auto_disable_time == disable_time1 );
      BOOST_CHECK( co1_id(db).acceptable_collateral == collateral_map1_new );
      BOOST_CHECK( co1_id(db).acceptable_borrowers == borrower_map1 );

      // Existing credit deals are unchanged
      BOOST_CHECK( cd11_id(db).borrower == ray_id );
      BOOST_CHECK( cd11_id(db).offer_id == co1_id );
      BOOST_CHECK( cd11_id(db).offer_owner == sam_id );
      BOOST_CHECK( cd11_id(db).debt_asset == core_id );
      BOOST_CHECK( cd11_id(db).debt_amount == 100 );
      BOOST_CHECK( cd11_id(db).collateral_asset == usd_id );
      BOOST_CHECK( cd11_id(db).collateral_amount == 200 );
      BOOST_CHECK( cd11_id(db).fee_rate == 30000u );
      BOOST_CHECK( cd11_id(db).latest_repay_time == expected_repay_time11 );

      // Ted is now able to borrow with CNY
      const credit_deal_object& cdo14 = borrow_from_credit_offer( ted_id, co1_id, asset(200), asset(200, cny_id) );
      credit_deal_id_type cd14_id = cdo14.id;
      time_point_sec expected_repay_time14 = db.head_block_time() + fc::seconds(600); // 15 minutes after init

      BOOST_CHECK( cd14_id(db).borrower == ted_id );
      BOOST_CHECK( cd14_id(db).offer_id == co1_id );
      BOOST_CHECK( cd14_id(db).offer_owner == sam_id );
      BOOST_CHECK( cd14_id(db).debt_asset == core_id );
      BOOST_CHECK( cd14_id(db).debt_amount == 200 );
      BOOST_CHECK( cd14_id(db).collateral_asset == cny_id );
      BOOST_CHECK( cd14_id(db).collateral_amount == 200 );
      BOOST_CHECK( cd14_id(db).fee_rate == 500u );
      BOOST_CHECK( cd14_id(db).latest_repay_time == expected_repay_time14 );

      BOOST_CHECK( co1_id(db).total_balance == 10000 );
      BOOST_CHECK( co1_id(db).current_balance == 9500 );

      expected_balance_ted_core += 200;
      expected_balance_ted_cny -= 200;
      check_balances();

      // Ray is now unable to borrow
      BOOST_CHECK_THROW( borrow_from_credit_offer( ray_id, co1_id, asset(200), asset(200, cny_id) ),
                         fc::exception );
      // Ted is now unable to borrow same amount again because it would exceed the limit
      BOOST_CHECK_THROW( borrow_from_credit_offer( ted_id, co1_id, asset(200), asset(200, cny_id) ),
                         fc::exception );

      // Ted is able to borrow less with CNY
      const credit_deal_object& cdo15 = borrow_from_credit_offer( ted_id, co1_id, asset(50), asset(100, cny_id) );
      credit_deal_id_type cd15_id = cdo15.id;
      time_point_sec expected_repay_time15 = db.head_block_time() + fc::seconds(600); // 15 minutes after init

      BOOST_CHECK( cd15_id(db).borrower == ted_id );
      BOOST_CHECK( cd15_id(db).offer_id == co1_id );
      BOOST_CHECK( cd15_id(db).offer_owner == sam_id );
      BOOST_CHECK( cd15_id(db).debt_asset == core_id );
      BOOST_CHECK( cd15_id(db).debt_amount == 50 );
      BOOST_CHECK( cd15_id(db).collateral_asset == cny_id );
      BOOST_CHECK( cd15_id(db).collateral_amount == 100 );
      BOOST_CHECK( cd15_id(db).fee_rate == 500u );
      BOOST_CHECK( cd15_id(db).latest_repay_time == expected_repay_time15 );

      BOOST_CHECK( co1_id(db).total_balance == 10000 );
      BOOST_CHECK( co1_id(db).current_balance == 9450 );

      expected_balance_ted_core += 50;
      expected_balance_ted_cny -= 100;
      check_balances();

      // Time goes by
      generate_blocks( db.head_block_time() + fc::minutes(3) ); // now is 8 minutes after init
      set_expiration( db, trx );

      // Sam withdraw most of funds from the credit offer
      update_credit_offer( sam_id, co1_id, asset(-9410), {}, {}, {}, {}, {}, {}, {} );
      BOOST_CHECK( co1_id(db).total_balance == 590 );
      BOOST_CHECK( co1_id(db).current_balance == 40 );

      expected_balance_sam_core += 9410;
      check_balances();

      // Ted is unable to borrow with EUR because Sam is not authorized by EUR asset
      BOOST_CHECK_THROW( borrow_from_credit_offer( ted_id, co1_id, asset(40), asset(499, eur_id) ),
                         fc::exception );

      {
         // Add Sam to the whitelist of EUR
         account_whitelist_operation wop;
         wop.authorizing_account = sam_id;
         wop.account_to_list = sam_id;
         wop.new_listing = account_whitelist_operation::white_listed;
         trx.operations.clear();
         trx.operations.push_back(wop);
         PUSH_TX( db, trx, ~0 );
      }

      // Now Ted is able to borrow 40 CORE with EUR
      const credit_deal_object& cdo16 = borrow_from_credit_offer( ted_id, co1_id, asset(40), asset(499, eur_id) );
      credit_deal_id_type cd16_id = cdo16.id;
      time_point_sec expected_repay_time16 = db.head_block_time() + fc::seconds(600); // 18 minutes after init

      BOOST_CHECK( cd16_id(db).borrower == ted_id );
      BOOST_CHECK( cd16_id(db).offer_id == co1_id );
      BOOST_CHECK( cd16_id(db).offer_owner == sam_id );
      BOOST_CHECK( cd16_id(db).debt_asset == core_id );
      BOOST_CHECK( cd16_id(db).debt_amount == 40 );
      BOOST_CHECK( cd16_id(db).collateral_asset == eur_id );
      BOOST_CHECK( cd16_id(db).collateral_amount == 499 );
      BOOST_CHECK( cd16_id(db).fee_rate == 500u );
      BOOST_CHECK( cd16_id(db).latest_repay_time == expected_repay_time16 );

      BOOST_CHECK( co1_id(db).total_balance == 590 );
      BOOST_CHECK( co1_id(db).current_balance == 0 );

      expected_balance_ted_core += 40;
      expected_balance_ted_eur -= 499;
      check_balances();

      // Ted is unable to borrow 1 more CORE with EUR
      BOOST_CHECK_THROW( borrow_from_credit_offer( ted_id, co1_id, asset(1), asset(500, eur_id) ),
                         fc::exception );

      // Time goes by
      generate_blocks( db.head_block_time() + fc::minutes(4) ); // now is 12 minutes after init
      set_expiration( db, trx );

      // Unable to repay : zero or negative amount
      BOOST_CHECK_THROW( repay_credit_deal( ray_id, cd13_id, asset(0), asset(1) ),
                         fc::exception );
      BOOST_CHECK_THROW( repay_credit_deal( ray_id, cd13_id, asset(-1), asset(1) ),
                         fc::exception );
      // Note: credit fee is allowed to be zero
      BOOST_CHECK_THROW( repay_credit_deal( ray_id, cd13_id, asset(1), asset(-1) ),
                         fc::exception );

      // Unable to repay : asset type mismatch
      BOOST_CHECK_THROW( repay_credit_deal( ray_id, cd13_id, asset(1), asset(1, usd_id) ),
                         fc::exception );
      BOOST_CHECK_THROW( repay_credit_deal( ray_id, cd13_id, asset(1, usd_id), asset(1, usd_id) ),
                         fc::exception );
      BOOST_CHECK_THROW( repay_credit_deal( ray_id, cd13_id, asset(1, usd_id), asset(1) ),
                         fc::exception );

      // Unable to repay : credit deal does not belong to the account
      BOOST_CHECK_THROW( repay_credit_deal( ted_id, cd13_id, asset(1), asset(1) ),
                         fc::exception );

      // Ray partially repays
      auto result = repay_credit_deal( ray_id, cd13_id, asset(1), asset(1) );
      BOOST_REQUIRE( result.received.valid() );
      BOOST_REQUIRE( result.received->size() == 1 );
      asset collateral_released = result.received->front();

      BOOST_CHECK( collateral_released == asset(4, usd_id) ); // round_down(499/100)

      BOOST_REQUIRE( result.updated_objects.valid() );
      BOOST_CHECK( result.updated_objects->size() == 2 );
      BOOST_CHECK( *result.updated_objects == flat_set<object_id_type>({ co1_id, cd13_id }) );

      BOOST_CHECK( !result.removed_objects.valid() );

      BOOST_REQUIRE( result.impacted_accounts.valid() );
      BOOST_CHECK( result.impacted_accounts->size() == 1 );
      BOOST_CHECK( *result.impacted_accounts == flat_set<account_id_type>({ sam_id }) );

      BOOST_CHECK( cd13_id(db).borrower == ray_id );
      BOOST_CHECK( cd13_id(db).offer_id == co1_id );
      BOOST_CHECK( cd13_id(db).offer_owner == sam_id );
      BOOST_CHECK( cd13_id(db).debt_asset == core_id );
      BOOST_CHECK( cd13_id(db).debt_amount == 99 );
      BOOST_CHECK( cd13_id(db).collateral_asset == usd_id );
      BOOST_CHECK( cd13_id(db).collateral_amount == 495 );
      BOOST_CHECK( cd13_id(db).fee_rate == 30000u );
      BOOST_CHECK( cd13_id(db).latest_repay_time == expected_repay_time13 );

      BOOST_CHECK( co1_id(db).total_balance == 591 );
      BOOST_CHECK( co1_id(db).current_balance == 2 );

      expected_balance_ray_core -= 2;
      expected_balance_ray_usd += 4;
      check_balances();

      // Ted is able to borrow 2 CORE with EUR
      const credit_deal_object& cdo17 = borrow_from_credit_offer( ted_id, co1_id, asset(2), asset(49, eur_id) );
      credit_deal_id_type cd17_id = cdo17.id;
      time_point_sec expected_repay_time17 = db.head_block_time() + fc::seconds(600); // 22 minutes after init

      BOOST_CHECK( cd17_id(db).borrower == ted_id );
      BOOST_CHECK( cd17_id(db).offer_id == co1_id );
      BOOST_CHECK( cd17_id(db).offer_owner == sam_id );
      BOOST_CHECK( cd17_id(db).debt_asset == core_id );
      BOOST_CHECK( cd17_id(db).debt_amount == 2 );
      BOOST_CHECK( cd17_id(db).collateral_asset == eur_id );
      BOOST_CHECK( cd17_id(db).collateral_amount == 49 );
      BOOST_CHECK( cd17_id(db).fee_rate == 500u );
      BOOST_CHECK( cd17_id(db).latest_repay_time == expected_repay_time17 );

      BOOST_CHECK( co1_id(db).total_balance == 591 );
      BOOST_CHECK( co1_id(db).current_balance == 0 );

      expected_balance_ted_core += 2;
      expected_balance_ted_eur -= 49;
      check_balances();

      // Ray partially repays with more fee than required
      result = repay_credit_deal( ray_id, cd13_id, asset(1), asset(2) );
      BOOST_REQUIRE( result.received.valid() );
      BOOST_REQUIRE( result.received->size() == 1 );
      collateral_released = result.received->front();

      BOOST_CHECK( collateral_released == asset(5, usd_id) ); // round_down(495/99)

      BOOST_REQUIRE( result.updated_objects.valid() );
      BOOST_CHECK( result.updated_objects->size() == 2 );
      BOOST_CHECK( *result.updated_objects == flat_set<object_id_type>({ co1_id, cd13_id }) );

      BOOST_CHECK( !result.removed_objects.valid() );

      BOOST_REQUIRE( result.impacted_accounts.valid() );
      BOOST_CHECK( result.impacted_accounts->size() == 1 );
      BOOST_CHECK( *result.impacted_accounts == flat_set<account_id_type>({ sam_id }) );

      BOOST_CHECK( cd13_id(db).borrower == ray_id );
      BOOST_CHECK( cd13_id(db).offer_id == co1_id );
      BOOST_CHECK( cd13_id(db).offer_owner == sam_id );
      BOOST_CHECK( cd13_id(db).debt_asset == core_id );
      BOOST_CHECK( cd13_id(db).debt_amount == 98 );
      BOOST_CHECK( cd13_id(db).collateral_asset == usd_id );
      BOOST_CHECK( cd13_id(db).collateral_amount == 490 );
      BOOST_CHECK( cd13_id(db).fee_rate == 30000u );
      BOOST_CHECK( cd13_id(db).latest_repay_time == expected_repay_time13 );

      BOOST_CHECK( co1_id(db).total_balance == 593 );
      BOOST_CHECK( co1_id(db).current_balance == 3 );

      expected_balance_ray_core -= 3;
      expected_balance_ray_usd += 5;
      check_balances();

      // Unable to repay : amount too big
      BOOST_CHECK_THROW( repay_credit_deal( ray_id, cd13_id, asset(99), asset(5) ),
                         fc::exception );
      BOOST_CHECK_THROW( repay_credit_deal( ray_id, cd12_id, asset(101), asset(5) ),
                         fc::exception );
      // Unable to repay : insufficient credit fee : fee rate = 3%
      BOOST_CHECK_THROW( repay_credit_deal( ray_id, cd13_id, asset(98), asset(2) ),
                         fc::exception );
      BOOST_CHECK_THROW( repay_credit_deal( ray_id, cd12_id, asset(100), asset(2) ),
                         fc::exception );

      // Fully repays
      result = repay_credit_deal( ray_id, cd12_id, asset(100), asset(3) );
      BOOST_REQUIRE( result.received.valid() );
      BOOST_REQUIRE( result.received->size() == 1 );
      collateral_released = result.received->front();

      BOOST_CHECK( collateral_released == asset(200, usd_id) );

      BOOST_REQUIRE( result.updated_objects.valid() );
      BOOST_REQUIRE( result.updated_objects->size() == 1 );
      BOOST_CHECK( *(result.updated_objects->begin()) == co1_id );

      BOOST_REQUIRE( result.removed_objects.valid() );
      BOOST_REQUIRE( result.removed_objects->size() == 1 );
      BOOST_CHECK( *(result.removed_objects->begin()) == cd12_id );

      BOOST_REQUIRE( result.impacted_accounts.valid() );
      BOOST_CHECK( result.impacted_accounts->size() == 1 );
      BOOST_CHECK( *result.impacted_accounts == flat_set<account_id_type>({ sam_id }) );

      BOOST_CHECK( !db.find( cd12_id ) );

      BOOST_CHECK( co1_id(db).total_balance == 596 );
      BOOST_CHECK( co1_id(db).current_balance == 106 );

      expected_balance_ray_core -= 103;
      expected_balance_ray_usd += 200;
      check_balances();

      // Unable to repay : credit deal does not exist
      BOOST_CHECK_THROW( repay_credit_deal( ray_id, cd12_id, asset(100), asset(3) ),
                         fc::exception );

      // Create another credit offer
      auto disable_time2 = db.head_block_time() + fc::minutes(20); // 32 minites after init

      flat_map<asset_id_type, price> collateral_map2;
      collateral_map2[cny_id] = price( asset(10, usd_id), asset(12, cny_id) );
      collateral_map2[eur_id] = price( asset(10, usd_id), asset(10, eur_id) );
      const credit_offer_object& coo2 = create_credit_offer( sam_id, usd_id, 10000, 70000, 1800, 0, true,
                                              disable_time2, collateral_map2, {} );
      credit_offer_id_type co2_id = coo2.id;
      BOOST_CHECK( co2_id(db).owner_account == sam_id );
      BOOST_CHECK( co2_id(db).asset_type == usd_id );
      BOOST_CHECK( co2_id(db).total_balance == 10000 );
      BOOST_CHECK( co2_id(db).current_balance == 10000 );
      BOOST_CHECK( co2_id(db).fee_rate == 70000u );
      BOOST_CHECK( co2_id(db).max_duration_seconds == 1800u );
      BOOST_CHECK( co2_id(db).min_deal_amount == 0 );
      BOOST_CHECK( co2_id(db).enabled == true );
      BOOST_CHECK( co2_id(db).auto_disable_time == disable_time2 );
      BOOST_CHECK( co2_id(db).acceptable_collateral == collateral_map2 );
      BOOST_CHECK( co2_id(db).acceptable_borrowers.empty() );

      expected_balance_sam_usd -= 10000;
      check_balances();

      // Ray borrows from the new credit offer
      const auto& cdo21 = borrow_from_credit_offer( ray_id, co2_id, asset(1000, usd_id), asset(1200, cny_id) );
      credit_deal_id_type cd21_id = cdo21.id;
      time_point_sec expected_repay_time21 = db.head_block_time() + fc::seconds(1800); // 42 minutes after init

      BOOST_CHECK( cd21_id(db).borrower == ray_id );
      BOOST_CHECK( cd21_id(db).offer_id == co2_id );
      BOOST_CHECK( cd21_id(db).offer_owner == sam_id );
      BOOST_CHECK( cd21_id(db).debt_asset == usd_id );
      BOOST_CHECK( cd21_id(db).debt_amount == 1000 );
      BOOST_CHECK( cd21_id(db).collateral_asset == cny_id );
      BOOST_CHECK( cd21_id(db).collateral_amount == 1200 );
      BOOST_CHECK( cd21_id(db).fee_rate == 70000u );
      BOOST_CHECK( cd21_id(db).latest_repay_time == expected_repay_time21 );

      BOOST_CHECK( co2_id(db).total_balance == 10000 );
      BOOST_CHECK( co2_id(db).current_balance == 9000 );

      expected_balance_ray_usd += 1000;
      expected_balance_ray_cny -= 1200;
      check_balances();

      // Ray repays
      result = repay_credit_deal( ray_id, cd21_id, asset(100, usd_id), asset(7, usd_id) );
      BOOST_REQUIRE( result.received.valid() );
      BOOST_REQUIRE( result.received->size() == 1 );
      collateral_released = result.received->front();

      BOOST_CHECK( collateral_released == asset(120, cny_id) );

      BOOST_REQUIRE( result.updated_objects.valid() );
      BOOST_CHECK( result.updated_objects->size() == 2 );
      BOOST_CHECK( *result.updated_objects == flat_set<object_id_type>({ co2_id, cd21_id }) );

      BOOST_CHECK( !result.removed_objects.valid() );

      BOOST_REQUIRE( result.impacted_accounts.valid() );
      BOOST_CHECK( result.impacted_accounts->size() == 1 );
      BOOST_CHECK( *result.impacted_accounts == flat_set<account_id_type>({ sam_id }) );

      BOOST_CHECK( cd21_id(db).borrower == ray_id );
      BOOST_CHECK( cd21_id(db).offer_id == co2_id );
      BOOST_CHECK( cd21_id(db).offer_owner == sam_id );
      BOOST_CHECK( cd21_id(db).debt_asset == usd_id );
      BOOST_CHECK( cd21_id(db).debt_amount == 900 );
      BOOST_CHECK( cd21_id(db).collateral_asset == cny_id );
      BOOST_CHECK( cd21_id(db).collateral_amount == 1080 );
      BOOST_CHECK( cd21_id(db).fee_rate == 70000u );
      BOOST_CHECK( cd21_id(db).latest_repay_time == expected_repay_time21 );

      BOOST_CHECK( co2_id(db).total_balance == 10007 );
      BOOST_CHECK( co2_id(db).current_balance == 9107 );

      expected_balance_ray_usd -= 107;
      expected_balance_ray_cny += 120;
      check_balances();

      {
         // Remove Ray from the whitelist of USD
         account_whitelist_operation wop;
         wop.authorizing_account = ted_id;
         wop.account_to_list = ray_id;
         wop.new_listing = account_whitelist_operation::no_listing;
         trx.operations.clear();
         trx.operations.push_back(wop);
         PUSH_TX( db, trx, ~0 );
      }

      // Ray is no longer able to borrow from co2
      BOOST_CHECK_THROW( borrow_from_credit_offer( ray_id, co2_id, asset(1000, usd_id), asset(1200, cny_id) ),
                         fc::exception );

      // Ray is unable to repay the deal with USD
      BOOST_CHECK_THROW( repay_credit_deal( ray_id, cd21_id, asset(100, usd_id), asset(7, usd_id) ),
                         fc::exception );

      // Ray is still able to repay another deal with CORE to get USD
      result = repay_credit_deal( ray_id, cd13_id, asset(1), asset(1) );
      BOOST_REQUIRE( result.received.valid() );
      BOOST_REQUIRE( result.received->size() == 1 );
      collateral_released = result.received->front();

      BOOST_CHECK( collateral_released == asset(5, usd_id) ); // round_down(490/98)

      BOOST_REQUIRE( result.updated_objects.valid() );
      BOOST_CHECK( result.updated_objects->size() == 2 );
      BOOST_CHECK( *result.updated_objects == flat_set<object_id_type>({ co1_id, cd13_id }) );

      BOOST_CHECK( !result.removed_objects.valid() );

      BOOST_REQUIRE( result.impacted_accounts.valid() );
      BOOST_CHECK( result.impacted_accounts->size() == 1 );
      BOOST_CHECK( *result.impacted_accounts == flat_set<account_id_type>({ sam_id }) );

      BOOST_CHECK( cd13_id(db).borrower == ray_id );
      BOOST_CHECK( cd13_id(db).offer_id == co1_id );
      BOOST_CHECK( cd13_id(db).offer_owner == sam_id );
      BOOST_CHECK( cd13_id(db).debt_asset == core_id );
      BOOST_CHECK( cd13_id(db).debt_amount == 97 );
      BOOST_CHECK( cd13_id(db).collateral_asset == usd_id );
      BOOST_CHECK( cd13_id(db).collateral_amount == 485 );
      BOOST_CHECK( cd13_id(db).fee_rate == 30000u );
      BOOST_CHECK( cd13_id(db).latest_repay_time == expected_repay_time13 );

      BOOST_CHECK( co1_id(db).total_balance == 597 );
      BOOST_CHECK( co1_id(db).current_balance == 108 );

      expected_balance_ray_core -= 2;
      expected_balance_ray_usd += 5;
      check_balances();

      // Ray transfer most of CORE to Sam
      transfer( ray_id, sam_id, asset(expected_balance_ray_core - 10) );

      expected_balance_sam_core += (expected_balance_ray_core - 10);
      expected_balance_ray_core = 10;
      check_balances();

      // Unable to repay : insufficient account balance
      BOOST_CHECK_THROW( repay_credit_deal( ray_id, cd13_id, asset(10), asset(1) ),
                         fc::exception );

      // Time goes by
      generate_blocks( db.head_block_time() + fc::minutes(1) ); // now is 13 minutes after init
      set_expiration( db, trx );

      // Ted is unable to borrow from co2 : Ted is not authorized by USD
      BOOST_CHECK_THROW( borrow_from_credit_offer( ted_id, co2_id, asset(1000, usd_id), asset(1100, eur_id) ),
                         fc::exception );

      {
         // Add Ted to the whitelist of USD
         account_whitelist_operation wop;
         wop.authorizing_account = ted_id;
         wop.account_to_list = ted_id;
         wop.new_listing = account_whitelist_operation::white_listed;
         trx.operations.clear();
         trx.operations.push_back(wop);
         PUSH_TX( db, trx, ~0 );
      }

      // Ted borrows from the new credit offer
      const auto& cdo22 = borrow_from_credit_offer( ted_id, co2_id, asset(1000, usd_id), asset(1100, eur_id) );
      credit_deal_id_type cd22_id = cdo22.id;
      time_point_sec expected_repay_time22 = db.head_block_time() + fc::seconds(1800); // 43 minutes after init

      BOOST_CHECK( cd22_id(db).borrower == ted_id );
      BOOST_CHECK( cd22_id(db).offer_id == co2_id );
      BOOST_CHECK( cd22_id(db).offer_owner == sam_id );
      BOOST_CHECK( cd22_id(db).debt_asset == usd_id );
      BOOST_CHECK( cd22_id(db).debt_amount == 1000 );
      BOOST_CHECK( cd22_id(db).collateral_asset == eur_id );
      BOOST_CHECK( cd22_id(db).collateral_amount == 1100 );
      BOOST_CHECK( cd22_id(db).fee_rate == 70000u );
      BOOST_CHECK( cd22_id(db).latest_repay_time == expected_repay_time22 );

      BOOST_CHECK( co2_id(db).total_balance == 10007 );
      BOOST_CHECK( co2_id(db).current_balance == 8107 );

      expected_balance_ted_usd += 1000;
      expected_balance_ted_eur -= 1100;
      check_balances();

      // Ted repays
      result = repay_credit_deal( ted_id, cd22_id, asset(200, usd_id), asset(15, usd_id) );
      BOOST_REQUIRE( result.received.valid() );
      BOOST_REQUIRE( result.received->size() == 1 );
      collateral_released = result.received->front();

      BOOST_CHECK( collateral_released == asset(220, eur_id) );

      BOOST_REQUIRE( result.updated_objects.valid() );
      BOOST_CHECK( result.updated_objects->size() == 2 );
      BOOST_CHECK( *result.updated_objects == flat_set<object_id_type>({ co2_id, cd22_id }) );

      BOOST_CHECK( !result.removed_objects.valid() );

      BOOST_REQUIRE( result.impacted_accounts.valid() );
      BOOST_CHECK( result.impacted_accounts->size() == 1 );
      BOOST_CHECK( *result.impacted_accounts == flat_set<account_id_type>({ sam_id }) );

      BOOST_CHECK( cd22_id(db).borrower == ted_id );
      BOOST_CHECK( cd22_id(db).offer_id == co2_id );
      BOOST_CHECK( cd22_id(db).offer_owner == sam_id );
      BOOST_CHECK( cd22_id(db).debt_asset == usd_id );
      BOOST_CHECK( cd22_id(db).debt_amount == 800 );
      BOOST_CHECK( cd22_id(db).collateral_asset == eur_id );
      BOOST_CHECK( cd22_id(db).collateral_amount == 880 );
      BOOST_CHECK( cd22_id(db).fee_rate == 70000u );
      BOOST_CHECK( cd22_id(db).latest_repay_time == expected_repay_time22 );

      BOOST_CHECK( co2_id(db).total_balance == 10022 );
      BOOST_CHECK( co2_id(db).current_balance == 8322 );

      expected_balance_ted_usd -= 215;
      expected_balance_ted_eur += 220;
      check_balances();

      {
         // Remove Sam from the whitelist of USD
         account_whitelist_operation wop;
         wop.authorizing_account = ted_id;
         wop.account_to_list = sam_id;
         wop.new_listing = account_whitelist_operation::no_listing;
         trx.operations.clear();
         trx.operations.push_back(wop);
         PUSH_TX( db, trx, ~0 );
      }

      // Ted is unable to borrow from co2 : credit offer owner Sam is now not authorized by USD
      BOOST_CHECK_THROW( borrow_from_credit_offer( ted_id, co2_id, asset(1000, usd_id), asset(1100, eur_id) ),
                         fc::exception );

      // Ted is unable to repay the co2 deal : credit offer owner Sam is now not authorized by USD
      BOOST_CHECK_THROW( repay_credit_deal( ted_id, cd22_id, asset(200, usd_id), asset(15, usd_id) ),
                         fc::exception );

      // ===== Time table =========
      // now: 13
      // expected_repay_time14 : 15
      // expected_repay_time15 : 15
      // expected_repay_time16 : 18
      // disable_time1 : 20
      // expected_repay_time17 : 22
      // disable_time2 : 32
      // expected_repay_time21 : 42
      // expected_repay_time22 : 43
      // expected_repay_time11 : 60
      // expected_repay_time12 : 60 // fully repaid already
      // expected_repay_time13 : 65

      // Time goes by
      generate_blocks( expected_repay_time14 ); // now is 15 minutes after init
      set_expiration( db, trx );

      // Expiration
      BOOST_REQUIRE( !db.find( cd14_id ) );
      BOOST_REQUIRE( !db.find( cd15_id ) );

      BOOST_CHECK( co1_id(db).total_balance == 347 ); // 597 - 200 - 50
      BOOST_CHECK( co1_id(db).current_balance == 108 ); // unchanged
      BOOST_CHECK( co1_id(db).enabled == true );
      BOOST_CHECK( co1_id(db).auto_disable_time == disable_time1 );

      expected_balance_sam_cny += 200; // cd14
      expected_balance_sam_cny += 100; // cd15
      check_balances();

      BOOST_REQUIRE( db.find( cd16_id ) );
      BOOST_CHECK( cd16_id(db).borrower == ted_id );
      BOOST_CHECK( cd16_id(db).offer_id == co1_id );
      BOOST_CHECK( cd16_id(db).offer_owner == sam_id );
      BOOST_CHECK( cd16_id(db).debt_asset == core_id );
      BOOST_CHECK( cd16_id(db).debt_amount == 40 );
      BOOST_CHECK( cd16_id(db).collateral_asset == eur_id );
      BOOST_CHECK( cd16_id(db).collateral_amount == 499 );
      BOOST_CHECK( cd16_id(db).fee_rate == 500u );
      BOOST_CHECK( cd16_id(db).latest_repay_time == expected_repay_time16 );

      // Time goes by
      generate_blocks( expected_repay_time16 ); // now is 18 minutes after init
      set_expiration( db, trx );

      // Expiration
      BOOST_REQUIRE( !db.find( cd16_id ) );

      BOOST_CHECK( co1_id(db).total_balance == 307 ); // 347  - 40
      BOOST_CHECK( co1_id(db).current_balance == 108 ); // unchanged
      BOOST_CHECK( co1_id(db).enabled == true );
      BOOST_CHECK( co1_id(db).auto_disable_time == disable_time1 );

      expected_balance_sam_eur += 499; // cd16
      check_balances();

      BOOST_REQUIRE( db.find( cd17_id ) );
      BOOST_CHECK( cd17_id(db).borrower == ted_id );
      BOOST_CHECK( cd17_id(db).offer_id == co1_id );
      BOOST_CHECK( cd17_id(db).offer_owner == sam_id );
      BOOST_CHECK( cd17_id(db).debt_asset == core_id );
      BOOST_CHECK( cd17_id(db).debt_amount == 2 );
      BOOST_CHECK( cd17_id(db).collateral_asset == eur_id );
      BOOST_CHECK( cd17_id(db).collateral_amount == 49 );
      BOOST_CHECK( cd17_id(db).fee_rate == 500u );
      BOOST_CHECK( cd17_id(db).latest_repay_time == expected_repay_time17 );

      // Ted borrows more
      const credit_deal_object& cdo18 = borrow_from_credit_offer( ted_id, co1_id, asset(10), asset(30, eur_id) );
      credit_deal_id_type cd18_id = cdo18.id;
      time_point_sec expected_repay_time18 = db.head_block_time() + fc::seconds(600); // 28 minutes after init

      BOOST_CHECK( cd18_id(db).borrower == ted_id );
      BOOST_CHECK( cd18_id(db).offer_id == co1_id );
      BOOST_CHECK( cd18_id(db).offer_owner == sam_id );
      BOOST_CHECK( cd18_id(db).debt_asset == core_id );
      BOOST_CHECK( cd18_id(db).debt_amount == 10 );
      BOOST_CHECK( cd18_id(db).collateral_asset == eur_id );
      BOOST_CHECK( cd18_id(db).collateral_amount == 30 );
      BOOST_CHECK( cd18_id(db).fee_rate == 500u );
      BOOST_CHECK( cd18_id(db).latest_repay_time == expected_repay_time18 );

      BOOST_CHECK( co1_id(db).total_balance == 307 );
      BOOST_CHECK( co1_id(db).current_balance == 98 );

      expected_balance_ted_core += 10;
      expected_balance_ted_eur -= 30;
      check_balances();

      // ===== Time table =========
      // now: 18
      // expected_repay_time14 : 15 // expired
      // expected_repay_time15 : 15 // expired
      // expected_repay_time16 : 18 // expired
      // disable_time1 : 20
      // expected_repay_time17 : 22
      // expected_repay_time18 : 28
      // disable_time2 : 32
      // expected_repay_time21 : 42
      // expected_repay_time22 : 43
      // expected_repay_time11 : 60
      // expected_repay_time12 : 60 // fully repaid already
      // expected_repay_time13 : 65

      // Time goes by
      generate_blocks( disable_time1 ); // now is 20 minutes after init
      set_expiration( db, trx );

      // Expiration
      BOOST_CHECK( co1_id(db).total_balance == 307 );
      BOOST_CHECK( co1_id(db).current_balance == 98 );
      BOOST_CHECK( co1_id(db).enabled == false );
      BOOST_CHECK( co1_id(db).auto_disable_time == disable_time1 );

      // Unable to borrow from co1
      BOOST_CHECK_THROW( borrow_from_credit_offer( ted_id, co1_id, asset(10), asset(30, eur_id) ),
                         fc::exception );

      BOOST_REQUIRE( db.find( cd17_id ) );
      BOOST_CHECK( cd17_id(db).borrower == ted_id );
      BOOST_CHECK( cd17_id(db).offer_id == co1_id );
      BOOST_CHECK( cd17_id(db).offer_owner == sam_id );
      BOOST_CHECK( cd17_id(db).debt_asset == core_id );
      BOOST_CHECK( cd17_id(db).debt_amount == 2 );
      BOOST_CHECK( cd17_id(db).collateral_asset == eur_id );
      BOOST_CHECK( cd17_id(db).collateral_amount == 49 );
      BOOST_CHECK( cd17_id(db).fee_rate == 500u );
      BOOST_CHECK( cd17_id(db).latest_repay_time == expected_repay_time17 );

      // Time goes by
      generate_blocks( expected_repay_time17 ); // now is 22 minutes after init
      set_expiration( db, trx );

      // Expiration
      BOOST_REQUIRE( !db.find( cd17_id ) );

      BOOST_CHECK( co1_id(db).total_balance == 305 ); // 307  - 2
      BOOST_CHECK( co1_id(db).current_balance == 98 ); // unchanged
      BOOST_CHECK( co1_id(db).enabled == false );
      BOOST_CHECK( co1_id(db).auto_disable_time == disable_time1 );

      expected_balance_sam_eur += 49; // cd17
      check_balances();

      BOOST_REQUIRE( db.find( cd18_id ) );
      BOOST_CHECK( cd18_id(db).borrower == ted_id );
      BOOST_CHECK( cd18_id(db).offer_id == co1_id );
      BOOST_CHECK( cd18_id(db).offer_owner == sam_id );
      BOOST_CHECK( cd18_id(db).debt_asset == core_id );
      BOOST_CHECK( cd18_id(db).debt_amount == 10 );
      BOOST_CHECK( cd18_id(db).collateral_asset == eur_id );
      BOOST_CHECK( cd18_id(db).collateral_amount == 30 );
      BOOST_CHECK( cd18_id(db).fee_rate == 500u );
      BOOST_CHECK( cd18_id(db).latest_repay_time == expected_repay_time18 );

      // Time goes by
      generate_blocks( expected_repay_time18 ); // now is 28 minutes after init
      set_expiration( db, trx );

      // Expiration
      BOOST_REQUIRE( !db.find( cd18_id ) );

      BOOST_CHECK( co1_id(db).total_balance == 295 ); // 305  - 10
      BOOST_CHECK( co1_id(db).current_balance == 98 ); // unchanged

      expected_balance_sam_eur += 30; // cd18
      check_balances();

      BOOST_CHECK( co2_id(db).enabled == true );
      BOOST_CHECK( co2_id(db).auto_disable_time == disable_time2 );

      // ===== Time table =========
      // now: 28
      // expected_repay_time14 : 15 // expired
      // expected_repay_time15 : 15 // expired
      // expected_repay_time16 : 18 // expired
      // disable_time1 : 20         // expired
      // expected_repay_time17 : 22 // expired
      // expected_repay_time18 : 28 // expired
      // disable_time2 : 32
      // expected_repay_time21 : 42
      // expected_repay_time22 : 43
      // expected_repay_time11 : 60
      // expected_repay_time12 : 60 // fully repaid already
      // expected_repay_time13 : 65

      // Time goes by
      generate_blocks( disable_time2 ); // now is 32 minutes after init
      set_expiration( db, trx );

      BOOST_CHECK( co2_id(db).enabled == false );
      BOOST_CHECK( co2_id(db).auto_disable_time == disable_time2 );

      BOOST_REQUIRE( db.find( cd21_id ) );
      BOOST_CHECK( cd21_id(db).borrower == ray_id );
      BOOST_CHECK( cd21_id(db).offer_id == co2_id );
      BOOST_CHECK( cd21_id(db).offer_owner == sam_id );
      BOOST_CHECK( cd21_id(db).debt_asset == usd_id );
      BOOST_CHECK( cd21_id(db).debt_amount == 900 );
      BOOST_CHECK( cd21_id(db).collateral_asset == cny_id );
      BOOST_CHECK( cd21_id(db).collateral_amount == 1080 );
      BOOST_CHECK( cd21_id(db).fee_rate == 70000u );
      BOOST_CHECK( cd21_id(db).latest_repay_time == expected_repay_time21 );

      // Time goes by
      generate_blocks( expected_repay_time21 ); // now is 42 minutes after init
      set_expiration( db, trx );

      // Expiration
      BOOST_REQUIRE( !db.find( cd21_id ) );

      BOOST_CHECK( co2_id(db).total_balance == 9122 ); // 10022 - 900
      BOOST_CHECK( co2_id(db).current_balance == 8322 ); // unchanged

      expected_balance_sam_cny += 1080; // cd21
      check_balances();

      BOOST_CHECK( cd22_id(db).borrower == ted_id );
      BOOST_CHECK( cd22_id(db).offer_id == co2_id );
      BOOST_CHECK( cd22_id(db).offer_owner == sam_id );
      BOOST_CHECK( cd22_id(db).debt_asset == usd_id );
      BOOST_CHECK( cd22_id(db).debt_amount == 800 );
      BOOST_CHECK( cd22_id(db).collateral_asset == eur_id );
      BOOST_CHECK( cd22_id(db).collateral_amount == 880 );
      BOOST_CHECK( cd22_id(db).fee_rate == 70000u );
      BOOST_CHECK( cd22_id(db).latest_repay_time == expected_repay_time22 );

      {
         // Remove Sam from the whitelist of EUR
         account_whitelist_operation wop;
         wop.authorizing_account = sam_id;
         wop.account_to_list = sam_id;
         wop.new_listing = account_whitelist_operation::no_listing;
         trx.operations.clear();
         trx.operations.push_back(wop);
         PUSH_TX( db, trx, ~0 );
      }

      // Time goes by
      generate_blocks( expected_repay_time22 ); // now is 43 minutes after init
      set_expiration( db, trx );

      // Expiration
      BOOST_REQUIRE( !db.find( cd22_id ) );

      BOOST_CHECK( co2_id(db).total_balance == 8322 ); // 9122 - 800
      BOOST_CHECK( co2_id(db).current_balance == 8322 ); // unchanged

      // Funds go to account balance ignoring asset authorization
      expected_balance_sam_eur += 880; // cd22
      check_balances();

      // Sam delete credit offer
      delete_credit_offer( sam_id, co2_id );

      BOOST_REQUIRE( db.find( co1_id ) );
      BOOST_REQUIRE( !db.find( co2_id ) );

      expected_balance_sam_usd += 8322;
      check_balances();

      generate_block();

   } catch (fc::exception& e) {
      edump((e.to_detail_string()));
      throw;
   }
}

BOOST_AUTO_TEST_CASE( credit_offer_apis_test )
{ try {

      // Pass the hard fork time
      generate_blocks( HARDFORK_CORE_2362_TIME );
      set_expiration( db, trx );

      ACTORS((bob)(ray)(sam)(ted));

      auto init_amount = 10000000 * GRAPHENE_BLOCKCHAIN_PRECISION;
      fund( bob, asset(init_amount) );
      fund( ray, asset(init_amount) );
      fund( sam, asset(init_amount) );
      fund( ted, asset(init_amount) );

      asset_id_type core_id;

      const asset_object& usd = create_user_issued_asset( "MYUSD" );
      asset_id_type usd_id = usd.id;
      issue_uia( bob, usd.amount(init_amount) );
      issue_uia( ray, usd.amount(init_amount) );
      issue_uia( sam, usd.amount(init_amount) );
      issue_uia( ted, usd.amount(init_amount) );

      const asset_object& eur = create_user_issued_asset( "MYEUR", sam, white_list );
      asset_id_type eur_id = eur.id;
      issue_uia( bob, eur.amount(init_amount) );
      issue_uia( ray, eur.amount(init_amount) );
      issue_uia( sam, eur.amount(init_amount) );
      issue_uia( ted, eur.amount(init_amount) );

      // create credit offers
      flat_map<asset_id_type, price> collateral_map_core;
      collateral_map_core[usd_id] = price( asset(1), asset(2, usd_id) );
      collateral_map_core[eur_id] = price( asset(1), asset(1, eur_id) );

      flat_map<asset_id_type, price> collateral_map_usd;
      collateral_map_usd[eur_id] = price( asset(1, usd_id), asset(1, eur_id) );

      flat_map<asset_id_type, price> collateral_map_eur;
      collateral_map_eur[core_id] = price( asset(1, eur_id), asset(3) );

      const credit_offer_object& coo1 = create_credit_offer( sam_id, core_id, 10000, 30000, 3600, 0, true,
                                              db.head_block_time() + fc::days(1), collateral_map_core, {} );
      credit_offer_id_type co1_id = coo1.id;

      const credit_offer_object& coo2 = create_credit_offer( ted_id, usd_id, 10000, 30000, 3600, 0, true,
                                              db.head_block_time() + fc::days(1), collateral_map_usd, {} );
      credit_offer_id_type co2_id = coo2.id;

      const credit_offer_object& coo3 = create_credit_offer( sam_id, eur_id, 10000, 30000, 3600, 0, true,
                                              db.head_block_time() + fc::days(1), collateral_map_eur, {} );
      credit_offer_id_type co3_id = coo3.id;

      const credit_offer_object& coo4 = create_credit_offer( sam_id, eur_id, 10000, 30000, 3600, 0, true,
                                              db.head_block_time() + fc::days(1), collateral_map_eur, {} );
      credit_offer_id_type co4_id = coo4.id;

      const credit_offer_object& coo5 = create_credit_offer( sam_id, usd_id, 10000, 30000, 3600, 0, true,
                                              db.head_block_time() + fc::days(1), collateral_map_usd, {} );
      credit_offer_id_type co5_id = coo5.id;

      const credit_offer_object& coo6 = create_credit_offer( ted_id, usd_id, 10000, 30000, 3600, 0, true,
                                              db.head_block_time() + fc::days(1), collateral_map_usd, {} );
      credit_offer_id_type co6_id = coo6.id;

      generate_block();

      // Check database API
      graphene::app::database_api db_api( db, &( app.get_options() ) );

      // List all credit offers
      auto offers = db_api.list_credit_offers();
      BOOST_REQUIRE_EQUAL( offers.size(), 6u );
      BOOST_CHECK( offers.front().id == co1_id );
      BOOST_CHECK( offers.back().id == co6_id );

      // Pagination : the first page
      offers = db_api.list_credit_offers( 5 );
      BOOST_REQUIRE_EQUAL( offers.size(), 5u );
      BOOST_CHECK( offers.front().id == co1_id );
      BOOST_CHECK( offers.back().id == co5_id );

      // Pagination : the last page
      offers = db_api.list_credit_offers( 5, co3_id );
      BOOST_REQUIRE_EQUAL( offers.size(), 4u );
      BOOST_CHECK( offers.front().id == co3_id );
      BOOST_CHECK( offers.back().id == co6_id );

      // Limit too large
      BOOST_CHECK_THROW( db_api.list_credit_offers( 102 ), fc::exception );

      // Get all credit offers owned by Sam
      offers = db_api.get_credit_offers_by_owner( "sam" );
      BOOST_REQUIRE_EQUAL( offers.size(), 4u );
      BOOST_CHECK( offers.front().id == co1_id );
      BOOST_CHECK( offers.back().id == co5_id );

      // Pagination : the first page
      offers = db_api.get_credit_offers_by_owner( "sam", 3, {} );
      BOOST_REQUIRE_EQUAL( offers.size(), 3u );
      BOOST_CHECK( offers.front().id == co1_id );
      BOOST_CHECK( offers.back().id == co4_id );

      // Pagination : another page
      offers = db_api.get_credit_offers_by_owner( "sam", 3, co2_id );
      BOOST_REQUIRE_EQUAL( offers.size(), 3u );
      BOOST_CHECK( offers.front().id == co3_id );
      BOOST_CHECK( offers.back().id == co5_id );

      // Pagination : the first page of credit offers owned by Ted
      offers = db_api.get_credit_offers_by_owner( string("1.2.")+fc::to_string(ted_id.instance.value), 3 );
      BOOST_REQUIRE_EQUAL( offers.size(), 2u );
      BOOST_CHECK( offers.front().id == co2_id );
      BOOST_CHECK( offers.back().id == co6_id );

      // Nonexistent account
      BOOST_CHECK_THROW( db_api.get_credit_offers_by_owner( "nonexistent-account" ), fc::exception );

      // Limit too large
      BOOST_CHECK_THROW( db_api.get_credit_offers_by_owner( "ted", 102 ), fc::exception );

      // Get all credit offers whose asset type is USD
      offers = db_api.get_credit_offers_by_asset( "MYUSD" );
      BOOST_REQUIRE_EQUAL( offers.size(), 3u );
      BOOST_CHECK( offers.front().id == co2_id );
      BOOST_CHECK( offers.back().id == co6_id );

      // Pagination : the first page
      offers = db_api.get_credit_offers_by_asset( "MYUSD", 2 );
      BOOST_REQUIRE_EQUAL( offers.size(), 2u );
      BOOST_CHECK( offers.front().id == co2_id );
      BOOST_CHECK( offers.back().id == co5_id );

      // Pagination : another page
      offers = db_api.get_credit_offers_by_asset( "MYUSD", 2, co4_id );
      BOOST_REQUIRE_EQUAL( offers.size(), 2u );
      BOOST_CHECK( offers.front().id == co5_id );
      BOOST_CHECK( offers.back().id == co6_id );

      // Pagination : the first page of credit offers whose asset type is CORE
      offers = db_api.get_credit_offers_by_asset( "1.3.0", 2, {} );
      BOOST_REQUIRE_EQUAL( offers.size(), 1u );
      BOOST_CHECK( offers.front().id == co1_id );
      BOOST_CHECK( offers.back().id == co1_id );

      // Nonexistent asset
      BOOST_CHECK_THROW( db_api.get_credit_offers_by_asset( "NOSUCHASSET" ), fc::exception );

      // Limit too large
      BOOST_CHECK_THROW( db_api.get_credit_offers_by_asset( "MYUSD", 102 ), fc::exception );

      // Create credit deals
      // Offer owner : sam
      const credit_deal_object& cdo11 = borrow_from_credit_offer( ray_id, co1_id, asset(100), asset(200, usd_id) );
      credit_deal_id_type cd11_id = cdo11.id;

      // Offer owner : sam
      const credit_deal_object& cdo12 = borrow_from_credit_offer( ray_id, co1_id, asset(150), asset(400, eur_id) );
      credit_deal_id_type cd12_id = cdo12.id;

      // Offer owner : sam
      const credit_deal_object& cdo13 = borrow_from_credit_offer( bob_id, co1_id, asset(200), asset(600, eur_id) );
      credit_deal_id_type cd13_id = cdo13.id;

      // Offer owner : ted
      const credit_deal_object& cdo21 = borrow_from_credit_offer( bob_id, co2_id, asset(500, usd_id),
                                                                  asset(500, eur_id) );
      credit_deal_id_type cd21_id = cdo21.id;

      // Offer owner : sam
      const credit_deal_object& cdo31 = borrow_from_credit_offer( bob_id, co3_id, asset(500, eur_id), asset(5000) );
      credit_deal_id_type cd31_id = cdo31.id;

      // Offer owner : sam
      const credit_deal_object& cdo51 = borrow_from_credit_offer( ray_id, co5_id, asset(400, usd_id),
                                                                  asset(800, eur_id) );
      credit_deal_id_type cd51_id = cdo51.id;

      generate_block();

      // Since all APIs are now calling the same template function,
      // no need to to test with many cases only for pagination.
      auto deals = db_api.list_credit_deals();
      BOOST_REQUIRE_EQUAL( deals.size(), 6u );
      BOOST_CHECK( deals.front().id == cd11_id );
      BOOST_CHECK( deals.back().id == cd51_id );

      deals = db_api.get_credit_deals_by_offer_id( co1_id );
      BOOST_REQUIRE_EQUAL( deals.size(), 3u );
      BOOST_CHECK( deals[0].id == cd11_id );
      BOOST_CHECK( deals[1].id == cd12_id );
      BOOST_CHECK( deals[2].id == cd13_id );

      deals = db_api.get_credit_deals_by_offer_owner( "sam" );
      BOOST_REQUIRE_EQUAL( deals.size(), 5u );
      BOOST_CHECK( deals[0].id == cd11_id );
      BOOST_CHECK( deals[1].id == cd12_id );
      BOOST_CHECK( deals[2].id == cd13_id );
      BOOST_CHECK( deals[3].id == cd31_id );
      BOOST_CHECK( deals[4].id == cd51_id );

      deals = db_api.get_credit_deals_by_borrower( "bob" );
      BOOST_REQUIRE_EQUAL( deals.size(), 3u );
      BOOST_CHECK( deals[0].id == cd13_id );
      BOOST_CHECK( deals[1].id == cd21_id );
      BOOST_CHECK( deals[2].id == cd31_id );

      deals = db_api.get_credit_deals_by_debt_asset( "MYUSD" );
      BOOST_REQUIRE_EQUAL( deals.size(), 2u );
      BOOST_CHECK( deals[0].id == cd21_id );
      BOOST_CHECK( deals[1].id == cd51_id );

      deals = db_api.get_credit_deals_by_collateral_asset( "MYEUR" );
      BOOST_REQUIRE_EQUAL( deals.size(), 4u );
      BOOST_CHECK( deals[0].id == cd12_id );
      BOOST_CHECK( deals[1].id == cd13_id );
      BOOST_CHECK( deals[2].id == cd21_id );
      BOOST_CHECK( deals[3].id == cd51_id );

   } catch (fc::exception& e) {
      edump((e.to_detail_string()));
      throw;
   }
}

BOOST_AUTO_TEST_SUITE_END()
