/*
 * Copyright (c) 2020 Abit More, and contributors.
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
#include <graphene/chain/liquidity_pool_object.hpp>
#include <graphene/chain/proposal_object.hpp>
#include <graphene/chain/stableswap.hpp>

#include <graphene/app/api.hpp>

#include <boost/test/unit_test.hpp>

#include <limits>
#include <random>

using namespace graphene::chain;
using namespace graphene::chain::test;

BOOST_FIXTURE_TEST_SUITE( liquidity_pool_tests, database_fixture )

BOOST_AUTO_TEST_CASE( liquidity_pool_hardfork_time_test )
{
   try {

      // Proceeds to a recent hard fork
      generate_blocks( HARDFORK_BSIP_86_TIME );
      generate_block();
      set_expiration( db, trx );

      ACTORS((sam));

      auto init_amount = 10000000 * GRAPHENE_BLOCKCHAIN_PRECISION;
      fund( sam, asset(init_amount) );

      const asset_object& core = asset_id_type()(db);
      const asset_object& usd = create_user_issued_asset( "MYUSD" );
      const asset_object& lpa = create_user_issued_asset( "LPATEST", sam, charge_market_fee );

      // Before the hard fork, unable to create a liquidity pool or transact against a liquidity pool,
      // or do any of them with proposals
      BOOST_CHECK_THROW( create_liquidity_pool( sam_id, core.get_id(), usd.get_id(), lpa.get_id(), 0, 0 ),
                         fc::exception );

      liquidity_pool_id_type tmp_lp_id;
      BOOST_CHECK_THROW( delete_liquidity_pool( sam_id, tmp_lp_id ), fc::exception );
      BOOST_CHECK_THROW( deposit_to_liquidity_pool( sam_id, tmp_lp_id, core.amount(100), usd.amount(100) ),
                         fc::exception );
      BOOST_CHECK_THROW( withdraw_from_liquidity_pool( sam_id, tmp_lp_id, lpa.amount(100) ),
                         fc::exception );
      BOOST_CHECK_THROW( exchange_with_liquidity_pool( sam_id, tmp_lp_id, core.amount(100), usd.amount(100) ),
                         fc::exception );

      liquidity_pool_create_operation cop =
                         make_liquidity_pool_create_op( sam_id, core.get_id(), usd.get_id(), lpa.get_id(), 0, 0 );
      BOOST_CHECK_THROW( propose( cop ), fc::exception );

      liquidity_pool_delete_operation delop = make_liquidity_pool_delete_op( sam_id, tmp_lp_id );
      BOOST_CHECK_THROW( propose( delop ), fc::exception );

      liquidity_pool_deposit_operation depop =
                         make_liquidity_pool_deposit_op( sam_id, tmp_lp_id, core.amount(100), usd.amount(100) );
      BOOST_CHECK_THROW( propose( delop ), fc::exception );

      liquidity_pool_withdraw_operation wop =
                         make_liquidity_pool_withdraw_op( sam_id, tmp_lp_id, lpa.amount(100) );
      BOOST_CHECK_THROW( propose( wop ), fc::exception );

      liquidity_pool_exchange_operation exop =
                         make_liquidity_pool_exchange_op( sam_id, tmp_lp_id, core.amount(100), usd.amount(100) );
      BOOST_CHECK_THROW( propose( exop ), fc::exception );

   } catch (fc::exception& e) {
      edump((e.to_detail_string()));
      throw;
   }
}

BOOST_AUTO_TEST_CASE( liquidity_pool_update_hardfork_time_test )
{
   try {

      // Proceeds to a recent hard fork
      generate_blocks( HARDFORK_LIQUIDITY_POOL_TIME );
      set_expiration( db, trx );

      ACTORS((sam));

      auto init_amount = 10000000 * GRAPHENE_BLOCKCHAIN_PRECISION;
      fund( sam, asset(init_amount) );

      const asset_object& core = asset_id_type()(db);
      const asset_object& usd = create_user_issued_asset( "MYUSD" );
      const asset_object& lpa = create_user_issued_asset( "LPATEST", sam, charge_market_fee );

      const liquidity_pool_object& lpo = create_liquidity_pool( sam_id, core.get_id(), usd.get_id(), lpa.get_id(),
                                                                0, 0 );

      // Before the hard fork, unable to update a liquidity pool
      // or update with proposals
      BOOST_CHECK_THROW( update_liquidity_pool( sam_id, lpo.get_id(), 1, 0 ), fc::exception );

      liquidity_pool_update_operation updop = make_liquidity_pool_update_op( sam_id, lpo.get_id(), 1, 0 );
      BOOST_CHECK_THROW( propose( updop ), fc::exception );

   } catch (fc::exception& e) {
      edump((e.to_detail_string()));
      throw;
   }
}

BOOST_AUTO_TEST_CASE( liquidity_pool_update_test )
{
   try {

      // Pass the hard fork time
      generate_blocks( HARDFORK_CORE_2604_TIME );
      set_expiration( db, trx );

      ACTORS((sam)(ted));

      auto init_amount = 10000000 * GRAPHENE_BLOCKCHAIN_PRECISION;
      fund( sam, asset(init_amount) );
      fund( ted, asset(init_amount) );

      const asset_object& core = asset_id_type()(db);
      const asset_object& usd = create_user_issued_asset( "MYUSD" );
      issue_uia( sam, usd.amount(init_amount) );
      issue_uia( ted, usd.amount(init_amount) );

      const asset_object& lpa1 = create_user_issued_asset( "LPATEST1", sam, charge_market_fee );
      const asset_object& lpa2 = create_user_issued_asset( "LPATEST2", ted, charge_market_fee );

      const liquidity_pool_object& lpo1 = create_liquidity_pool( sam_id, core.get_id(), usd.get_id(), lpa1.get_id(),
                                                                0, 0 );

      BOOST_CHECK( lpo1.asset_a == core.id );
      BOOST_CHECK( lpo1.asset_b == usd.id );
      BOOST_CHECK( lpo1.balance_a == 0 );
      BOOST_CHECK( lpo1.balance_b == 0 );
      BOOST_CHECK( lpo1.share_asset == lpa1.id );
      BOOST_CHECK( lpo1.taker_fee_percent == 0 );
      BOOST_CHECK( lpo1.withdrawal_fee_percent == 0 );
      BOOST_CHECK( lpo1.virtual_value == 0 );

      deposit_to_liquidity_pool( sam_id, lpo1.get_id(), asset(10), asset( 20, usd.get_id() ) );

      BOOST_CHECK( lpo1.asset_a == core.id );
      BOOST_CHECK( lpo1.asset_b == usd.id );
      BOOST_CHECK( lpo1.balance_a == 10 );
      BOOST_CHECK( lpo1.balance_b == 20 );
      BOOST_CHECK( lpo1.share_asset == lpa1.id );
      BOOST_CHECK( lpo1.taker_fee_percent == 0 );
      BOOST_CHECK( lpo1.withdrawal_fee_percent == 0 );
      BOOST_CHECK( lpo1.virtual_value == 200 );

      const liquidity_pool_object& lpo2 = create_liquidity_pool( ted_id, core.get_id(), usd.get_id(), lpa2.get_id(),
                                                                1, 2 );

      BOOST_CHECK( lpo2.asset_a == core.id );
      BOOST_CHECK( lpo2.asset_b == usd.id );
      BOOST_CHECK( lpo2.balance_a == 0 );
      BOOST_CHECK( lpo2.balance_b == 0 );
      BOOST_CHECK( lpo2.share_asset == lpa2.id );
      BOOST_CHECK( lpo2.taker_fee_percent == 1 );
      BOOST_CHECK( lpo2.withdrawal_fee_percent == 2 );
      BOOST_CHECK( lpo2.virtual_value == 0 );

      // Able to propose
      {
         liquidity_pool_update_operation updop = make_liquidity_pool_update_op( sam_id, lpo1.get_id(), 1, 0 );
         propose( updop );
      }

      // Unable to update a liquidity pool with invalid data
      // update nothing
      BOOST_CHECK_THROW( update_liquidity_pool( sam_id, lpo1.get_id(), {}, {} ), fc::exception );
      BOOST_CHECK_THROW( propose( make_liquidity_pool_update_op( sam_id, lpo1.get_id(), {}, {} ) ), fc::exception );
      // non-zero withdrawal fee
      BOOST_CHECK_THROW( update_liquidity_pool( sam_id, lpo1.get_id(), {}, 1 ), fc::exception );
      BOOST_CHECK_THROW( propose( make_liquidity_pool_update_op( sam_id, lpo1.get_id(), {}, 1 ) ), fc::exception );
      BOOST_CHECK_THROW( update_liquidity_pool( sam_id, lpo1.get_id(), 0, 1 ), fc::exception );
      BOOST_CHECK_THROW( propose( make_liquidity_pool_update_op( sam_id, lpo1.get_id(), 0, 1 ) ), fc::exception );
      // taker fee exceeds 100%
      BOOST_CHECK_THROW( update_liquidity_pool( sam_id, lpo1.get_id(), 10001, {} ), fc::exception );
      BOOST_CHECK_THROW( update_liquidity_pool( sam_id, lpo1.get_id(), 10001, 0 ), fc::exception );
      BOOST_CHECK_THROW( propose( make_liquidity_pool_update_op( sam_id, lpo1.get_id(), 10001, {} ) ), fc::exception);
      BOOST_CHECK_THROW( propose( make_liquidity_pool_update_op( sam_id, lpo1.get_id(), 10001, 0 ) ), fc::exception );
      // Owner mismatch (able to propose)
      BOOST_CHECK_THROW( update_liquidity_pool( ted_id, lpo1.get_id(), 1, {} ), fc::exception );
      propose( make_liquidity_pool_update_op( ted_id, lpo1.get_id(), 1, {} ) );
      // Updating taker fee when withdrawal fee is non-zero (able to propose)
      BOOST_CHECK_THROW( update_liquidity_pool( ted_id, lpo2.get_id(), 1, {} ), fc::exception );
      propose( make_liquidity_pool_update_op( ted_id, lpo2.get_id(), 1, {} ) );

      // Sam is able to update lpo1
      update_liquidity_pool( sam_id, lpo1.get_id(), 2, 0 );
      BOOST_CHECK( lpo1.asset_a == core.id );
      BOOST_CHECK( lpo1.asset_b == usd.id );
      BOOST_CHECK( lpo1.balance_a == 10 );
      BOOST_CHECK( lpo1.balance_b == 20 );
      BOOST_CHECK( lpo1.share_asset == lpa1.id );
      BOOST_CHECK( lpo1.taker_fee_percent == 2 );
      BOOST_CHECK( lpo1.withdrawal_fee_percent == 0 );
      BOOST_CHECK( lpo1.virtual_value == 200 );

      update_liquidity_pool( sam_id, lpo1.get_id(), 1, {} );
      BOOST_CHECK( lpo1.asset_a == core.id );
      BOOST_CHECK( lpo1.asset_b == usd.id );
      BOOST_CHECK( lpo1.balance_a == 10 );
      BOOST_CHECK( lpo1.balance_b == 20 );
      BOOST_CHECK( lpo1.share_asset == lpa1.id );
      BOOST_CHECK( lpo1.taker_fee_percent == 1 );
      BOOST_CHECK( lpo1.withdrawal_fee_percent == 0 );
      BOOST_CHECK( lpo1.virtual_value == 200 );

      // Ted is able to update lpo2 if to update its withdrawal fee to 0
      update_liquidity_pool( ted_id, lpo2.get_id(), 2, 0 );

      BOOST_CHECK( lpo2.asset_a == core.id );
      BOOST_CHECK( lpo2.asset_b == usd.id );
      BOOST_CHECK( lpo2.balance_a == 0 );
      BOOST_CHECK( lpo2.balance_b == 0 );
      BOOST_CHECK( lpo2.share_asset == lpa2.id );
      BOOST_CHECK( lpo2.taker_fee_percent == 2 );
      BOOST_CHECK( lpo2.withdrawal_fee_percent == 0 );
      BOOST_CHECK( lpo2.virtual_value == 0 );

      generate_block();

   } catch (fc::exception& e) {
      edump((e.to_detail_string()));
      throw;
   }
}

BOOST_AUTO_TEST_CASE( liquidity_pool_create_delete_proposal_test )
{ try {

      // Pass the hard fork time
      generate_blocks( HARDFORK_LIQUIDITY_POOL_TIME );
      set_expiration( db, trx );

      ACTORS((sam)(ted));

      auto init_amount = 10000000 * GRAPHENE_BLOCKCHAIN_PRECISION;
      fund( sam, asset(init_amount) );
      fund( ted, asset(init_amount) );

      const asset_object& core = asset_id_type()(db);

      const asset_object& usd = create_user_issued_asset( "MYUSD" );
      issue_uia( sam, usd.amount(init_amount) );
      issue_uia( ted, usd.amount(init_amount) );

      const asset_object& lpa = create_user_issued_asset( "LPATEST", sam, charge_market_fee );
      const asset_object& lpa1 = create_user_issued_asset( "LPATESTA", sam, charge_market_fee );
      const asset_object& lpa2 = create_user_issued_asset( "LPATESTB", sam, charge_market_fee );
      const asset_object& lpa3 = create_user_issued_asset( "LPATESTC", sam, charge_market_fee );
      const asset_object& ted_lpa = create_user_issued_asset( "LPATED", ted, charge_market_fee );

      const asset_object& mpa = create_bitasset( "MPATEST", sam_id );
      const asset_object& pm = create_prediction_market( "PMTEST", sam_id );

      BOOST_CHECK( !lpa1.is_liquidity_pool_share_asset() );

      asset_id_type no_asset_id1( pm.id + 100 );
      asset_id_type no_asset_id2( pm.id + 200 );
      BOOST_REQUIRE( !db.find( no_asset_id1 ) );
      BOOST_REQUIRE( !db.find( no_asset_id2 ) );

      // Able to propose
      {
         liquidity_pool_create_operation cop =
                         make_liquidity_pool_create_op( sam_id, core.get_id(), usd.get_id(), lpa.get_id(), 0, 0 );
         propose( cop );

         liquidity_pool_id_type tmp_lp_id;

         liquidity_pool_delete_operation delop = make_liquidity_pool_delete_op( sam_id, tmp_lp_id );
         propose( delop );

         liquidity_pool_deposit_operation depop =
                         make_liquidity_pool_deposit_op( sam_id, tmp_lp_id, core.amount(100), usd.amount(100) );
         propose( depop );

         liquidity_pool_withdraw_operation wop =
                         make_liquidity_pool_withdraw_op( sam_id, tmp_lp_id, lpa.amount(100) );
         propose( wop );

         liquidity_pool_exchange_operation exop =
                         make_liquidity_pool_exchange_op( sam_id, tmp_lp_id, core.amount(100), usd.amount(100) );
         propose( exop );
      }

      // Able to create liquidity pools with valid data
      const liquidity_pool_object& lpo1 = create_liquidity_pool( sam_id, core.get_id(), usd.get_id(), lpa1.get_id(),
                                                                 0, 0 );
      BOOST_CHECK( lpo1.asset_a == core.id );
      BOOST_CHECK( lpo1.asset_b == usd.id );
      BOOST_CHECK( lpo1.balance_a == 0 );
      BOOST_CHECK( lpo1.balance_b == 0 );
      BOOST_CHECK( lpo1.share_asset == lpa1.id );
      BOOST_CHECK( lpo1.taker_fee_percent == 0 );
      BOOST_CHECK( lpo1.withdrawal_fee_percent == 0 );
      BOOST_CHECK( lpo1.virtual_value == 0 );

      liquidity_pool_id_type lp_id1 = lpo1.get_id();
      BOOST_CHECK( lpa1.is_liquidity_pool_share_asset() );
      BOOST_CHECK( *lpa1.for_liquidity_pool == lp_id1 );

      const liquidity_pool_object& lpo2 = create_liquidity_pool( sam_id, core.get_id(), usd.get_id(), lpa2.get_id(),
                                                                 200, 300 );
      BOOST_CHECK( lpo2.asset_a == core.id );
      BOOST_CHECK( lpo2.asset_b == usd.id );
      BOOST_CHECK( lpo2.balance_a == 0 );
      BOOST_CHECK( lpo2.balance_b == 0 );
      BOOST_CHECK( lpo2.share_asset == lpa2.id );
      BOOST_CHECK( lpo2.taker_fee_percent == 200 );
      BOOST_CHECK( lpo2.withdrawal_fee_percent == 300 );
      BOOST_CHECK( lpo2.virtual_value == 0 );

      liquidity_pool_id_type lp_id2 = lpo2.get_id();
      BOOST_CHECK( lpa2.is_liquidity_pool_share_asset() );
      BOOST_CHECK( *lpa2.for_liquidity_pool == lp_id2 );

      const liquidity_pool_object& lpo3 = create_liquidity_pool( sam_id, usd.get_id(), mpa.get_id(), lpa3.get_id(),
                                                                 50, 50 );

      BOOST_CHECK( lpo3.asset_a == usd.id );
      BOOST_CHECK( lpo3.asset_b == mpa.id );
      BOOST_CHECK( lpo3.balance_a == 0 );
      BOOST_CHECK( lpo3.balance_b == 0 );
      BOOST_CHECK( lpo3.share_asset == lpa3.id );
      BOOST_CHECK( lpo3.taker_fee_percent == 50 );
      BOOST_CHECK( lpo3.withdrawal_fee_percent == 50 );
      BOOST_CHECK( lpo3.virtual_value == 0 );

      liquidity_pool_id_type lp_id3 = lpo3.get_id();
      BOOST_CHECK( lpa3.is_liquidity_pool_share_asset() );
      BOOST_CHECK( *lpa3.for_liquidity_pool == lp_id3 );

      // Unable to create a liquidity pool with invalid data
      // the same assets in pool
      BOOST_CHECK_THROW( create_liquidity_pool( sam_id, core.get_id(), core.get_id(), lpa.get_id(), 0, 0 ),
                         fc::exception );
      BOOST_CHECK_THROW( create_liquidity_pool( sam_id, usd.get_id(), usd.get_id(), lpa.get_id(), 0, 0 ),
                         fc::exception );
      // ID of the first asset is greater
      BOOST_CHECK_THROW( create_liquidity_pool( sam_id, usd.get_id(), core.get_id(), lpa.get_id(), 0, 0 ),
                         fc::exception );
      // the share asset is one of the assets in pool
      BOOST_CHECK_THROW( create_liquidity_pool( sam_id, usd.get_id(), lpa.get_id(), lpa.get_id(), 0, 0 ),
                         fc::exception );
      BOOST_CHECK_THROW( create_liquidity_pool( sam_id, lpa.get_id(), pm.get_id(), lpa.get_id(), 0, 0 ),
                         fc::exception );
      // percentage too big
      BOOST_CHECK_THROW( create_liquidity_pool( sam_id, core.get_id(), usd.get_id(), lpa.get_id(), 10001, 0 ),
                         fc::exception );
      BOOST_CHECK_THROW( create_liquidity_pool( sam_id, core.get_id(), usd.get_id(), lpa.get_id(), 0, 10001 ),
                         fc::exception );
      BOOST_CHECK_THROW( create_liquidity_pool( sam_id, core.get_id(), usd.get_id(), lpa.get_id(), 10001, 10001 ),
                         fc::exception );
      // asset does not exist
      BOOST_CHECK_THROW( create_liquidity_pool( sam_id, core.get_id(), usd.get_id(), no_asset_id1, 0, 0 ),
                         fc::exception );
      BOOST_CHECK_THROW( create_liquidity_pool( sam_id, core.get_id(), no_asset_id1, lpa.get_id(), 0, 0 ),
                         fc::exception );
      BOOST_CHECK_THROW( create_liquidity_pool( sam_id, no_asset_id1, no_asset_id2, lpa.get_id(), 0, 0 ),
                         fc::exception );
      // the account does not own the share asset
      BOOST_CHECK_THROW( create_liquidity_pool( sam_id, core.get_id(), usd.get_id(), ted_lpa.get_id(), 0, 0 ),
                         fc::exception );
      // the share asset is a MPA or a PM
      BOOST_CHECK_THROW( create_liquidity_pool( sam_id, core.get_id(), usd.get_id(), mpa.get_id(), 0, 0 ),
                         fc::exception );
      BOOST_CHECK_THROW( create_liquidity_pool( sam_id, core.get_id(), usd.get_id(), pm.get_id(), 0, 0 ),
                         fc::exception );
      // the share asset is already bound to a liquidity pool
      BOOST_CHECK_THROW( create_liquidity_pool( sam_id, core.get_id(), usd.get_id(), lpa1.get_id(), 0, 0 ),
                         fc::exception );
      // current supply of the share asset is not zero
      BOOST_CHECK_THROW( create_liquidity_pool( sam_id, core.get_id(), lpa.get_id(), usd.get_id(), 0, 0 ),
                         fc::exception );

      // Unable to issue a liquidity pool share asset
      BOOST_CHECK_THROW( issue_uia( sam, lpa1.amount(1) ), fc::exception );

      // Sam is able to delete an empty pool owned by him
      generic_operation_result result = delete_liquidity_pool( sam_id, lpo1.get_id() );
      BOOST_CHECK( !db.find( lp_id1 ) );
      BOOST_CHECK( !lpa1.is_liquidity_pool_share_asset() );
      BOOST_CHECK_EQUAL( result.new_objects.size(), 0u );
      BOOST_REQUIRE_EQUAL( result.updated_objects.size(), 1u );
      BOOST_CHECK( *result.updated_objects.begin() == lpa1.id );
      BOOST_REQUIRE_EQUAL( result.removed_objects.size(), 1u );
      BOOST_CHECK( *result.removed_objects.begin() == lp_id1 );

      // Other pools are still there
      BOOST_CHECK( db.find( lp_id2 ) );
      BOOST_CHECK( db.find( lp_id3 ) );

      // Ted is not able to delete a pool that does not exist
      BOOST_CHECK_THROW( delete_liquidity_pool( ted_id, lp_id1 ), fc::exception );
      // Ted is not able to delete a pool owned by sam
      BOOST_CHECK_THROW( delete_liquidity_pool( ted_id, lp_id2 ), fc::exception );

      // the asset is now a simple asset, able to issue
      issue_uia( sam, lpa1.amount(1) );

      generate_block();

   } catch (fc::exception& e) {
      edump((e.to_detail_string()));
      throw;
   }
}

BOOST_AUTO_TEST_CASE( liquidity_pool_deposit_withdrawal_test )
{ try {

      // Pass the hard fork time
      generate_blocks( HARDFORK_LIQUIDITY_POOL_TIME );
      set_expiration( db, trx );

      ACTORS((sam)(ted));

      additional_asset_options_t eur_options, usd_options;
      eur_options.value.taker_fee_percent = 50; // 0.5% taker fee
      usd_options.value.taker_fee_percent = 80; // 0.8% taker fee

      const asset_object& eur = create_user_issued_asset( "MYEUR", sam, charge_market_fee,
                                                 price(asset(1, asset_id_type(1)), asset(1)),
                                                 4, 20, eur_options ); // 0.2% maker fee
      const asset_object& usd = create_user_issued_asset( "MYUSD", ted, charge_market_fee,
                                                 price(asset(1, asset_id_type(1)), asset(1)),
                                                 4, 30, usd_options ); // 0.3% maker fee
      const asset_object& lpa = create_user_issued_asset( "LPATEST", sam, charge_market_fee );

      asset_id_type core_id = asset_id_type();
      asset_id_type eur_id = eur.get_id();
      asset_id_type usd_id = usd.get_id();
      asset_id_type lpa_id = lpa.get_id();

      int64_t init_amount = 10000000 * GRAPHENE_BLOCKCHAIN_PRECISION;
      fund( sam, asset(init_amount) );
      fund( ted, asset(init_amount) );
      issue_uia( sam, eur.amount(init_amount) );
      issue_uia( ted, eur.amount(init_amount) );
      issue_uia( sam, usd.amount(init_amount) );
      issue_uia( ted, usd.amount(init_amount) );

      int64_t expected_balance_sam_eur = init_amount;
      int64_t expected_balance_sam_usd = init_amount;
      int64_t expected_balance_sam_lpa = 0;
      int64_t expected_balance_ted_eur = init_amount;
      int64_t expected_balance_ted_usd = init_amount;
      int64_t expected_balance_ted_lpa = 0;

      const auto& check_balances = [&]() {
         BOOST_CHECK_EQUAL( db.get_balance( sam_id, eur_id ).amount.value, expected_balance_sam_eur );
         BOOST_CHECK_EQUAL( db.get_balance( sam_id, usd_id ).amount.value, expected_balance_sam_usd );
         BOOST_CHECK_EQUAL( db.get_balance( sam_id, lpa_id ).amount.value, expected_balance_sam_lpa );
         BOOST_CHECK_EQUAL( db.get_balance( ted_id, eur_id ).amount.value, expected_balance_ted_eur );
         BOOST_CHECK_EQUAL( db.get_balance( ted_id, usd_id ).amount.value, expected_balance_ted_usd );
         BOOST_CHECK_EQUAL( db.get_balance( ted_id, lpa_id ).amount.value, expected_balance_ted_lpa );
      };

      check_balances();

      int64_t expected_pool_balance_a = 0;
      int64_t expected_pool_balance_b = 0;
      int64_t expected_lp_supply = 0;

      // create a liquidity pool
      const liquidity_pool_object& lpo = create_liquidity_pool( sam_id, eur.get_id(), usd.get_id(), lpa.get_id(),
                                                                200, 300 );
      liquidity_pool_id_type lp_id = lpo.get_id();

      BOOST_CHECK( lpo.asset_a == eur_id );
      BOOST_CHECK( lpo.asset_b == usd_id );
      BOOST_CHECK( lpo.share_asset == lpa_id );
      BOOST_CHECK( lpo.taker_fee_percent == 200 );
      BOOST_CHECK( lpo.withdrawal_fee_percent == 300 );

      BOOST_CHECK_EQUAL( lpo.balance_a.value, expected_pool_balance_a);
      BOOST_CHECK_EQUAL( lpo.balance_b.value, expected_pool_balance_b);
      BOOST_CHECK( lpo.virtual_value == fc::uint128_t(expected_pool_balance_a) * expected_pool_balance_b );
      BOOST_CHECK_EQUAL( lpa.dynamic_data(db).current_supply.value, expected_lp_supply );

      BOOST_CHECK( lpa.is_liquidity_pool_share_asset() );
      BOOST_CHECK( *lpa.for_liquidity_pool == lp_id );

      check_balances();

      // Unable to deposit to a liquidity pool with invalid data
      // non-positive amounts
      for( int64_t i = -1; i <= 1; ++i )
      {
         for( int64_t j = -1; j <= 1; ++j )
         {
            if( i > 0 && j > 0 )
               continue;
            BOOST_CHECK_THROW( deposit_to_liquidity_pool( sam_id, lp_id, asset( i, eur_id ), asset( j, usd_id ) ),
                               fc::exception );
         }
      }
      // Insufficient balance
      BOOST_CHECK_THROW( deposit_to_liquidity_pool( sam_id, lp_id,
                            asset( init_amount + 1, eur_id ), asset( 1, usd_id ) ), fc::exception );
      BOOST_CHECK_THROW( deposit_to_liquidity_pool( sam_id, lp_id,
                            asset( 1, eur_id ), asset( init_amount + 1, usd_id ) ), fc::exception );
      // asset ID mismatch
      BOOST_CHECK_THROW( deposit_to_liquidity_pool( sam_id, lp_id, asset( 1, core_id ), asset( 1, usd_id ) ),
                         fc::exception );
      BOOST_CHECK_THROW( deposit_to_liquidity_pool( sam_id, lp_id, asset( 1, eur_id ), asset( 1, lpa_id ) ),
                         fc::exception );
      BOOST_CHECK_THROW( deposit_to_liquidity_pool( sam_id, lp_id, asset( 1, usd_id ), asset( 1, eur_id ) ),
                         fc::exception );
      // non-exist pool
      BOOST_CHECK_THROW( deposit_to_liquidity_pool( sam_id, lp_id+1, asset( 1, eur_id ), asset( 1, usd_id ) ),
                         fc::exception );
      // pool empty but not owner depositting
      BOOST_CHECK_THROW( deposit_to_liquidity_pool( ted_id, lp_id, asset( 1, eur_id ), asset( 1, usd_id ) ),
                         fc::exception );

      // The owner is able to do the initial deposit
      generic_exchange_operation_result result;
      result = deposit_to_liquidity_pool( sam_id, lp_id, asset( 1000, eur_id ), asset( 1200, usd_id ) );

      BOOST_REQUIRE_EQUAL( result.paid.size(), 2u );
      BOOST_CHECK( result.paid.front() == asset( 1000, eur_id ) );
      BOOST_CHECK( result.paid.back() == asset( 1200, usd_id ) );
      BOOST_REQUIRE_EQUAL( result.received.size(), 1u );
      BOOST_CHECK( result.received.front() == asset( 1200, lpa_id ) );
      BOOST_REQUIRE_EQUAL( result.fees.size(), 0u );

      expected_pool_balance_a = 1000;
      expected_pool_balance_b = 1200;
      expected_lp_supply = 1200;
      BOOST_CHECK_EQUAL( lpo.balance_a.value, expected_pool_balance_a);
      BOOST_CHECK_EQUAL( lpo.balance_b.value, expected_pool_balance_b);
      BOOST_CHECK( lpo.virtual_value == fc::uint128_t(expected_pool_balance_a) * expected_pool_balance_b );
      BOOST_CHECK_EQUAL( lpa.dynamic_data(db).current_supply.value, expected_lp_supply );

      expected_balance_sam_eur -= 1000;
      expected_balance_sam_usd -= 1200;
      expected_balance_sam_lpa += 1200;
      check_balances();

      // unable to delete a pool that is not empty
      BOOST_CHECK_THROW( delete_liquidity_pool( sam_id, lp_id ), fc::exception );

      // Sam tries to deposit more
      result = deposit_to_liquidity_pool( sam_id, lp_id, asset( 200, eur_id ), asset( 120, usd_id ) );

      BOOST_REQUIRE_EQUAL( result.paid.size(), 2u );
      BOOST_CHECK( result.paid.front() == asset( 100, eur_id ) );
      BOOST_CHECK( result.paid.back() == asset( 120, usd_id ) );
      BOOST_REQUIRE_EQUAL( result.received.size(), 1u );
      BOOST_CHECK( result.received.front() == asset( 120, lpa_id ) );
      BOOST_REQUIRE_EQUAL( result.fees.size(), 0u );

      expected_pool_balance_a += 100;
      expected_pool_balance_b += 120;
      expected_lp_supply += 120;
      BOOST_CHECK_EQUAL( lpo.balance_a.value, expected_pool_balance_a);
      BOOST_CHECK_EQUAL( lpo.balance_b.value, expected_pool_balance_b);
      BOOST_CHECK( lpo.virtual_value == fc::uint128_t(expected_pool_balance_a) * expected_pool_balance_b );
      BOOST_CHECK_EQUAL( lpa.dynamic_data(db).current_supply.value, expected_lp_supply );

      expected_balance_sam_eur -= 100;
      expected_balance_sam_usd -= 120;
      expected_balance_sam_lpa += 120;
      check_balances();

      // Unable to reserve all the supply of the LP token
      BOOST_CHECK_THROW( reserve_asset( sam_id, asset( expected_balance_sam_lpa, lpa_id ) ), fc::exception );

      // Ted deposits
      result = deposit_to_liquidity_pool( ted_id, lp_id, asset( 12347, eur_id ), asset( 56890, usd_id ) );

      int64_t new_lp_supply = 14816; // 1320 * 12347 / 1100, round down
      int64_t new_a = 12347;
      int64_t new_b = 14816;

      BOOST_REQUIRE_EQUAL( result.paid.size(), 2u );
      BOOST_CHECK( result.paid.front() == asset( new_a, eur_id ) );
      BOOST_CHECK( result.paid.back() == asset( new_b, usd_id ) );
      BOOST_REQUIRE_EQUAL( result.received.size(), 1u );
      BOOST_CHECK( result.received.front() == asset( new_lp_supply, lpa_id ) );
      BOOST_REQUIRE_EQUAL( result.fees.size(), 0u );

      expected_pool_balance_a += new_a; // 1100 + 12347 = 13447
      expected_pool_balance_b += new_b; // 1320 + 14816 = 16136
      expected_lp_supply += new_lp_supply; // 16136
      BOOST_CHECK_EQUAL( lpo.balance_a.value, expected_pool_balance_a);
      BOOST_CHECK_EQUAL( lpo.balance_b.value, expected_pool_balance_b);
      BOOST_CHECK( lpo.virtual_value == fc::uint128_t(expected_pool_balance_a) * expected_pool_balance_b );
      BOOST_CHECK_EQUAL( lpa.dynamic_data(db).current_supply.value, expected_lp_supply );

      expected_balance_ted_eur -= new_a;
      expected_balance_ted_usd -= new_b;
      expected_balance_ted_lpa += new_lp_supply;
      check_balances();

      // Unable to withdraw with invalid data
      // non-positive amount
      BOOST_CHECK_THROW( withdraw_from_liquidity_pool( ted_id, lp_id, asset( -1, lpa_id ) ),
                         fc::exception );
      BOOST_CHECK_THROW( withdraw_from_liquidity_pool( ted_id, lp_id, asset( 0, lpa_id ) ),
                         fc::exception );
      // insufficient balance
      BOOST_CHECK_THROW( withdraw_from_liquidity_pool( ted_id, lp_id, asset( expected_balance_ted_lpa + 1, lpa_id ) ),
                         fc::exception );
      // asset ID mismatch
      BOOST_CHECK_THROW( withdraw_from_liquidity_pool( ted_id, lp_id, asset( 10, core_id ) ),
                         fc::exception );
      BOOST_CHECK_THROW( withdraw_from_liquidity_pool( ted_id, lp_id, asset( 10, usd_id ) ),
                         fc::exception );
      BOOST_CHECK_THROW( withdraw_from_liquidity_pool( ted_id, lp_id, asset( 10, eur_id ) ),
                         fc::exception );
      // non-exist pool
      BOOST_CHECK_THROW( withdraw_from_liquidity_pool( ted_id, lp_id+1, asset( 10, usd_id ) ),
                         fc::exception );

      // Ted reserve some LP token
      reserve_asset( ted_id, asset( 14810, lpa_id ) );

      expected_lp_supply -= 14810; // 16136 - 14810 = 1326
      BOOST_CHECK_EQUAL( lpa.dynamic_data(db).current_supply.value, expected_lp_supply );

      expected_balance_ted_lpa -= 14810; // 6
      check_balances();

      // Ted fails to deposit with too small amounts
      BOOST_CHECK_THROW( deposit_to_liquidity_pool( ted_id, lp_id, asset( 8, eur_id ), asset( 8, usd_id ) ),
                         fc::exception );

      // Ted deposits again
      result = deposit_to_liquidity_pool( ted_id, lp_id, asset( 12347, eur_id ), asset( 56890, usd_id ) );

      new_lp_supply = 1217; // 1326 * 12347 / 13447, round down
      new_a = 12342; // 1217 * 13447 / 1326, round up
      new_b = 14810; // 1217 * 16136 / 1326, round up

      BOOST_REQUIRE_EQUAL( result.paid.size(), 2u );
      BOOST_CHECK( result.paid.front() == asset( new_a, eur_id ) );
      BOOST_CHECK( result.paid.back() == asset( new_b, usd_id ) );
      BOOST_REQUIRE_EQUAL( result.received.size(), 1u );
      BOOST_CHECK( result.received.front() == asset( new_lp_supply, lpa_id ) );
      BOOST_REQUIRE_EQUAL( result.fees.size(), 0u );

      expected_pool_balance_a += new_a; // 13447 + 12342 = 25789
      expected_pool_balance_b += new_b; // 16136 + 14810 = 30946
      expected_lp_supply += new_lp_supply; // 1326 + 1217 = 2543
      BOOST_CHECK_EQUAL( lpo.balance_a.value, expected_pool_balance_a);
      BOOST_CHECK_EQUAL( lpo.balance_b.value, expected_pool_balance_b);
      BOOST_CHECK( lpo.virtual_value == fc::uint128_t(expected_pool_balance_a) * expected_pool_balance_b );
      BOOST_CHECK_EQUAL( lpa.dynamic_data(db).current_supply.value, expected_lp_supply );

      expected_balance_ted_eur -= new_a;
      expected_balance_ted_usd -= new_b;
      expected_balance_ted_lpa += new_lp_supply;
      check_balances();

      // Ted withdraws some LP token
      result = withdraw_from_liquidity_pool( ted_id, lp_id, asset( 7, lpa_id ) );

      new_lp_supply = -7;
      new_a = -68; // - (7 * 25789 / 2543, round down, = 70, deduct withdrawal fee 70 * 3%, round down, = 2)
      new_b = -83; // - (7 * 30946 / 2543, round down, = 85, deduct withdrawal fee 85 * 3%, round down, = 2)

      BOOST_REQUIRE_EQUAL( result.paid.size(), 1u );
      BOOST_CHECK( result.paid.front() == asset( -new_lp_supply, lpa_id ) );
      BOOST_REQUIRE_EQUAL( result.received.size(), 2u );
      BOOST_CHECK( result.received.front() == asset( -new_a, eur_id ) );
      BOOST_CHECK( result.received.back() == asset( -new_b, usd_id ) );
      BOOST_REQUIRE_EQUAL( result.fees.size(), 2u );
      BOOST_CHECK( result.fees.front() == asset( 2, eur_id ) );
      BOOST_CHECK( result.fees.back() == asset( 2, usd_id ) );

      expected_pool_balance_a += new_a; // 25789 - 68 = 25721
      expected_pool_balance_b += new_b; // 30946 - 83 = 30863
      expected_lp_supply += new_lp_supply; // 2543 - 7 = 2536
      BOOST_CHECK_EQUAL( lpo.balance_a.value, expected_pool_balance_a);
      BOOST_CHECK_EQUAL( lpo.balance_b.value, expected_pool_balance_b);
      BOOST_CHECK( lpo.virtual_value == fc::uint128_t(expected_pool_balance_a) * expected_pool_balance_b );
      BOOST_CHECK_EQUAL( lpa.dynamic_data(db).current_supply.value, expected_lp_supply );

      expected_balance_ted_eur -= new_a;
      expected_balance_ted_usd -= new_b;
      expected_balance_ted_lpa += new_lp_supply;
      check_balances();

      // Ted reserve the rest LP token
      reserve_asset( ted_id, asset( expected_balance_ted_lpa, lpa_id ) );

      expected_lp_supply -= expected_balance_ted_lpa; // 1320
      BOOST_CHECK_EQUAL( lpa.dynamic_data(db).current_supply.value, expected_lp_supply );

      expected_balance_ted_lpa = 0;
      check_balances();

      // Sam withdraws all
      result = withdraw_from_liquidity_pool( sam_id, lp_id, asset( 1320, lpa_id ) );

      new_lp_supply = -1320;
      new_a = -25721;
      new_b = -30863;

      BOOST_REQUIRE_EQUAL( result.paid.size(), 1u );
      BOOST_CHECK( result.paid.front() == asset( -new_lp_supply, lpa_id ) );
      BOOST_REQUIRE_EQUAL( result.received.size(), 2u );
      BOOST_CHECK( result.received.front() == asset( -new_a, eur_id ) );
      BOOST_CHECK( result.received.back() == asset( -new_b, usd_id ) );
      BOOST_REQUIRE_EQUAL( result.fees.size(), 2u );
      BOOST_CHECK( result.fees.front() == asset( 0, eur_id ) );
      BOOST_CHECK( result.fees.back() == asset( 0, usd_id ) );

      expected_pool_balance_a = 0;
      expected_pool_balance_b = 0;
      expected_lp_supply = 0;
      BOOST_CHECK_EQUAL( lpo.balance_a.value, expected_pool_balance_a);
      BOOST_CHECK_EQUAL( lpo.balance_b.value, expected_pool_balance_b);
      BOOST_CHECK( lpo.virtual_value == fc::uint128_t(expected_pool_balance_a) * expected_pool_balance_b );
      BOOST_CHECK_EQUAL( lpa.dynamic_data(db).current_supply.value, expected_lp_supply );

      expected_balance_sam_eur -= new_a;
      expected_balance_sam_usd -= new_b;
      expected_balance_sam_lpa += new_lp_supply; // 0
      check_balances();

      // prepare for asset update
      asset_update_operation auop;
      auop.issuer = sam_id;
      auop.asset_to_update = lpa_id;
      auop.new_options = lpa_id(db).options;

      // set max supply to a smaller number
      auop.new_options.max_supply = 2000;
      trx.operations.clear();
      trx.operations.push_back( auop );
      PUSH_TX(db, trx, ~0);

      BOOST_CHECK_EQUAL( lpa_id(db).options.max_supply.value, 2000 );

      // Unable to do initial deposit if to create more than the max supply
      BOOST_CHECK_THROW( deposit_to_liquidity_pool( sam_id, lp_id, asset( 2001, eur_id ), asset( 100, usd_id ) ),
                         fc::exception );
      BOOST_CHECK_THROW( deposit_to_liquidity_pool( sam_id, lp_id, asset( 100, eur_id ), asset( 2001, usd_id ) ),
                         fc::exception );
      BOOST_CHECK_THROW( deposit_to_liquidity_pool( sam_id, lp_id, asset( 2001, eur_id ), asset( 2001, usd_id ) ),
                         fc::exception );

      // Able to deposit less
      result = deposit_to_liquidity_pool( sam_id, lp_id, asset( 1000, eur_id ), asset( 1200, usd_id ) );

      BOOST_REQUIRE_EQUAL( result.paid.size(), 2u );
      BOOST_CHECK( result.paid.front() == asset( 1000, eur_id ) );
      BOOST_CHECK( result.paid.back() == asset( 1200, usd_id ) );
      BOOST_REQUIRE_EQUAL( result.received.size(), 1u );
      BOOST_CHECK( result.received.front() == asset( 1200, lpa_id ) );
      BOOST_REQUIRE_EQUAL( result.fees.size(), 0u );

      expected_pool_balance_a = 1000;
      expected_pool_balance_b = 1200;
      expected_lp_supply = 1200;
      BOOST_CHECK_EQUAL( lpo.balance_a.value, expected_pool_balance_a);
      BOOST_CHECK_EQUAL( lpo.balance_b.value, expected_pool_balance_b);
      BOOST_CHECK( lpo.virtual_value == fc::uint128_t(expected_pool_balance_a) * expected_pool_balance_b );
      BOOST_CHECK_EQUAL( lpa.dynamic_data(db).current_supply.value, expected_lp_supply );

      expected_balance_sam_eur -= 1000;
      expected_balance_sam_usd -= 1200;
      expected_balance_sam_lpa += 1200;
      check_balances();

      // Try to deposit more to create more than max supply, will be capped at max supply
      result = deposit_to_liquidity_pool( sam_id, lp_id, asset( 1000, eur_id ), asset( 1200, usd_id ) );

      new_lp_supply = 800; // 2000 - 1200
      new_a = 667; // 800 * 1000 / 1200, round up
      new_b = 800;

      BOOST_REQUIRE_EQUAL( result.paid.size(), 2u );
      BOOST_CHECK( result.paid.front() == asset( new_a, eur_id ) );
      BOOST_CHECK( result.paid.back() == asset( new_b, usd_id ) );
      BOOST_REQUIRE_EQUAL( result.received.size(), 1u );
      BOOST_CHECK( result.received.front() == asset( new_lp_supply, lpa_id ) );
      BOOST_REQUIRE_EQUAL( result.fees.size(), 0u );

      expected_pool_balance_a += new_a;
      expected_pool_balance_b += new_b;
      expected_lp_supply = 2000;
      BOOST_CHECK_EQUAL( lpo.balance_a.value, expected_pool_balance_a);
      BOOST_CHECK_EQUAL( lpo.balance_b.value, expected_pool_balance_b);
      BOOST_CHECK( lpo.virtual_value == fc::uint128_t(expected_pool_balance_a) * expected_pool_balance_b );
      BOOST_CHECK_EQUAL( lpa.dynamic_data(db).current_supply.value, expected_lp_supply );

      expected_balance_sam_eur -= new_a;
      expected_balance_sam_usd -= new_b;
      expected_balance_sam_lpa += new_lp_supply;
      check_balances();

      // Unable to deposit more
      BOOST_CHECK_THROW( deposit_to_liquidity_pool( sam_id, lp_id, asset( 2, eur_id ), asset( 2, usd_id ) ),
                         fc::exception );

      // set max supply to a bigger number
      auop.new_options.max_supply = 3000;
      trx.operations.clear();
      trx.operations.push_back( auop );
      PUSH_TX(db, trx, ~0);

      BOOST_CHECK_EQUAL( lpa_id(db).options.max_supply.value, 3000 );

      // Able to deposit more
      deposit_to_liquidity_pool( sam_id, lp_id, asset( 2, eur_id ), asset( 2, usd_id ) );

      // update flag to disable creation of new supply
      auop.new_options.flags |= disable_new_supply;
      trx.operations.clear();
      trx.operations.push_back( auop );
      PUSH_TX(db, trx, ~0);

      BOOST_CHECK( !lpa_id(db).can_create_new_supply() );

      // Unable to deposit more
      BOOST_CHECK_THROW( deposit_to_liquidity_pool( sam_id, lp_id, asset( 2, eur_id ), asset( 2, usd_id ) ),
                         fc::exception );

      generate_block();

      graphene::market_history::liquidity_pool_ticker_id_type ticker_id( lp_id.instance );
      const auto& ticker = db.get( ticker_id );
      BOOST_CHECK_EQUAL( ticker._24h_deposit_count, 7u );
      BOOST_CHECK_EQUAL( ticker.total_deposit_count, 7u );
      BOOST_CHECK_EQUAL( ticker._24h_withdrawal_count, 2u );
      BOOST_CHECK_EQUAL( ticker.total_withdrawal_count, 2u );

      generate_blocks( db.head_block_time() + fc::days(2) );

      BOOST_CHECK_EQUAL( ticker._24h_deposit_count, 0u );
      BOOST_CHECK_EQUAL( ticker.total_deposit_count, 7u );
      BOOST_CHECK_EQUAL( ticker._24h_withdrawal_count, 0u );
      BOOST_CHECK_EQUAL( ticker.total_withdrawal_count, 2u );

   } catch (fc::exception& e) {
      edump((e.to_detail_string()));
      throw;
   }
}

BOOST_AUTO_TEST_CASE( liquidity_pool_exchange_test )
{ try {

      // Pass the hard fork time
      generate_blocks( HARDFORK_LIQUIDITY_POOL_TIME );
      set_expiration( db, trx );

      ACTORS((sam)(ted));

      additional_asset_options_t eur_options, usd_options;
      eur_options.value.taker_fee_percent = 50; // 0.5% taker fee
      usd_options.value.taker_fee_percent = 80; // 0.8% taker fee

      const asset_object& eur = create_user_issued_asset( "MYEUR", sam, charge_market_fee,
                                                 price(asset(1, asset_id_type(1)), asset(1)),
                                                 4, 20, eur_options ); // 0.2% maker fee
      const asset_object& usd = create_user_issued_asset( "MYUSD", ted, charge_market_fee,
                                                 price(asset(1, asset_id_type(1)), asset(1)),
                                                 4, 30, usd_options ); // 0.3% maker fee
      const asset_object& lpa = create_user_issued_asset( "LPATEST", sam, charge_market_fee );

      asset_id_type core_id = asset_id_type();
      asset_id_type eur_id = eur.get_id();
      asset_id_type usd_id = usd.get_id();
      asset_id_type lpa_id = lpa.get_id();

      int64_t init_amount = 10000000 * GRAPHENE_BLOCKCHAIN_PRECISION;
      fund( sam, asset(init_amount) );
      fund( ted, asset(init_amount) );
      issue_uia( sam, eur.amount(init_amount) );
      issue_uia( ted, eur.amount(init_amount) );
      issue_uia( sam, usd.amount(init_amount) );
      issue_uia( ted, usd.amount(init_amount) );

      int64_t expected_balance_sam_eur = init_amount;
      int64_t expected_balance_sam_usd = init_amount;
      int64_t expected_balance_sam_lpa = 0;
      int64_t expected_balance_ted_eur = init_amount;
      int64_t expected_balance_ted_usd = init_amount;
      int64_t expected_balance_ted_lpa = 0;

      int64_t expected_accumulated_fees_eur = 0;
      int64_t expected_accumulated_fees_usd = 0;

      const auto& check_balances = [&]() {
         BOOST_CHECK_EQUAL( db.get_balance( sam_id, eur_id ).amount.value, expected_balance_sam_eur );
         BOOST_CHECK_EQUAL( db.get_balance( sam_id, usd_id ).amount.value, expected_balance_sam_usd );
         BOOST_CHECK_EQUAL( db.get_balance( sam_id, lpa_id ).amount.value, expected_balance_sam_lpa );
         BOOST_CHECK_EQUAL( db.get_balance( ted_id, eur_id ).amount.value, expected_balance_ted_eur );
         BOOST_CHECK_EQUAL( db.get_balance( ted_id, usd_id ).amount.value, expected_balance_ted_usd );
         BOOST_CHECK_EQUAL( db.get_balance( ted_id, lpa_id ).amount.value, expected_balance_ted_lpa );
      };

      check_balances();

      int64_t expected_pool_balance_a = 0;
      int64_t expected_pool_balance_b = 0;
      int64_t expected_lp_supply = 0;

      // create a liquidity pool
      const liquidity_pool_object& lpo = create_liquidity_pool( sam_id, eur.get_id(), usd.get_id(), lpa.get_id(),
                                                                200, 300 );
      liquidity_pool_id_type lp_id = lpo.get_id();

      BOOST_CHECK( lpo.asset_a == eur_id );
      BOOST_CHECK( lpo.asset_b == usd_id );
      BOOST_CHECK( lpo.share_asset == lpa_id );
      BOOST_CHECK( lpo.taker_fee_percent == 200 );
      BOOST_CHECK( lpo.withdrawal_fee_percent == 300 );

      BOOST_CHECK_EQUAL( lpo.balance_a.value, expected_pool_balance_a);
      BOOST_CHECK_EQUAL( lpo.balance_b.value, expected_pool_balance_b);
      BOOST_CHECK( lpo.virtual_value == fc::uint128_t(expected_pool_balance_a) * expected_pool_balance_b );
      BOOST_CHECK_EQUAL( lpa.dynamic_data(db).current_supply.value, expected_lp_supply );

      BOOST_CHECK( lpa.is_liquidity_pool_share_asset() );
      BOOST_CHECK( *lpa.for_liquidity_pool == lp_id );

      check_balances();

      // Unable to exchange if the pool is not initialized
      BOOST_CHECK_THROW( exchange_with_liquidity_pool( ted_id, lp_id, asset( 100, eur_id ), asset( 1, usd_id ) ),
                         fc::exception );

      // The owner do the initial deposit
      generic_exchange_operation_result result;
      result = deposit_to_liquidity_pool( sam_id, lp_id, asset( 100, eur_id ), asset( 120, usd_id ) );

      BOOST_REQUIRE_EQUAL( result.paid.size(), 2u );
      BOOST_CHECK( result.paid.front() == asset( 100, eur_id ) );
      BOOST_CHECK( result.paid.back() == asset( 120, usd_id ) );
      BOOST_REQUIRE_EQUAL( result.received.size(), 1u );
      BOOST_CHECK( result.received.front() == asset( 120, lpa_id ) );
      BOOST_REQUIRE_EQUAL( result.fees.size(), 0u );

      expected_pool_balance_a = 100;
      expected_pool_balance_b = 120;
      expected_lp_supply = 120;
      BOOST_CHECK_EQUAL( lp_id(db).balance_a.value, expected_pool_balance_a);
      BOOST_CHECK_EQUAL( lp_id(db).balance_b.value, expected_pool_balance_b);
      BOOST_CHECK( lp_id(db).virtual_value == fc::uint128_t(expected_pool_balance_a) * expected_pool_balance_b );
      BOOST_CHECK_EQUAL( lpa_id(db).dynamic_data(db).current_supply.value, expected_lp_supply );

      expected_balance_sam_eur -= 100;
      expected_balance_sam_usd -= 120;
      expected_balance_sam_lpa += 120;
      check_balances();

      // Generates a block
      generate_block();
      set_expiration( db, trx );

      // Deposit again with 900 EUR and 3000 USD, the pool only takes 900 EUR and 1080 USD
      result = deposit_to_liquidity_pool( sam_id, lp_id, asset( 900, eur_id ), asset( 3000, usd_id ) );

      BOOST_REQUIRE_EQUAL( result.paid.size(), 2u );
      BOOST_CHECK( result.paid.front() == asset( 900, eur_id ) );
      BOOST_CHECK( result.paid.back() == asset( 1080, usd_id ) );
      BOOST_REQUIRE_EQUAL( result.received.size(), 1u );
      BOOST_CHECK( result.received.front() == asset( 1080, lpa_id ) );
      BOOST_REQUIRE_EQUAL( result.fees.size(), 0u );

      expected_pool_balance_a += 900;
      expected_pool_balance_b += 1080;
      expected_lp_supply += 1080;
      BOOST_CHECK_EQUAL( lp_id(db).balance_a.value, expected_pool_balance_a);
      BOOST_CHECK_EQUAL( lp_id(db).balance_b.value, expected_pool_balance_b);
      BOOST_CHECK( lp_id(db).virtual_value == fc::uint128_t(expected_pool_balance_a) * expected_pool_balance_b );
      BOOST_CHECK_EQUAL( lpa_id(db).dynamic_data(db).current_supply.value, expected_lp_supply );

      expected_balance_sam_eur -= 900;
      expected_balance_sam_usd -= 1080;
      expected_balance_sam_lpa += 1080;
      check_balances();


      // Unable to exchange if data is invalid
      // non-positive amounts
      for( int64_t i = -1; i <= 1; ++i )
      {
         for( int64_t j = -1; j <= 1; ++j )
         {
            if( i > 0 && j > 0 )
               continue;
            BOOST_CHECK_THROW( exchange_with_liquidity_pool( ted_id, lp_id, asset( i, eur_id ), asset( j, usd_id ) ),
                               fc::exception );
         }
      }
      // Insufficient balance
      BOOST_CHECK_THROW( exchange_with_liquidity_pool( ted_id, lp_id,
                            asset( init_amount + 1, eur_id ), asset( 1, usd_id ) ), fc::exception );
      BOOST_CHECK_THROW( exchange_with_liquidity_pool( ted_id, lp_id,
                            asset( init_amount + 1, usd_id ), asset( 1, eur_id ) ), fc::exception );
      // asset ID mismatch
      BOOST_CHECK_THROW( exchange_with_liquidity_pool( ted_id, lp_id, asset( 100, core_id ), asset( 1, usd_id ) ),
                         fc::exception );
      BOOST_CHECK_THROW( exchange_with_liquidity_pool( ted_id, lp_id, asset( 100, eur_id ), asset( 1, lpa_id ) ),
                         fc::exception );
      // non-exist pool
      BOOST_CHECK_THROW( exchange_with_liquidity_pool( ted_id, lp_id+1, asset( 100, eur_id ), asset( 1, usd_id ) ),
                         fc::exception );


      // trying to buy an amount that is equal to or more than the balance in the pool
      BOOST_CHECK_THROW( exchange_with_liquidity_pool( ted_id, lp_id, asset( 9000, eur_id ), asset( 1200, usd_id ) ),
                         fc::exception );
      BOOST_CHECK_THROW( exchange_with_liquidity_pool( ted_id, lp_id, asset( 9000, usd_id ), asset( 1000, eur_id ) ),
                         fc::exception );

      // Calculates if Ted sells 1000 EUR to the pool
      int64_t maker_fee = 2; // 1000 * 0.2%, eur
      int64_t delta_a = 998; // 1000 - 2
      // tmp_delta = 1200 - round_up( 1000 * 1200 / (1000+998) ) = 1200 - 601 = 599
      int64_t delta_b = -588; // - ( 599 - round_down(599 * 2%) ) = - ( 599 - 11 ) = -588
      int64_t pool_taker_fee = 11;
      int64_t taker_fee = 4; // 588 * 0.8%, usd
      int64_t ted_receives = 584; // 588 - 4

      // Ted fails to exchange if asks for more
      BOOST_CHECK_THROW( exchange_with_liquidity_pool( ted_id, lp_id, asset( 1000, eur_id ), asset( 585, usd_id ) ),
                         fc::exception );

      // Setup market blacklists and whitelists
      {
         asset_update_operation auop;

         auop.issuer = usd_id(db).issuer;
         auop.asset_to_update = usd_id;
         auop.new_options = usd_id(db).options;
         auop.new_options.whitelist_markets.insert( core_id );
         auop.new_options.blacklist_markets.insert( eur_id );
         auop.new_options.blacklist_markets.insert( usd_id );
         trx.operations.clear();
         trx.operations.push_back( auop );

         auop.issuer = eur_id(db).issuer;
         auop.asset_to_update = eur_id;
         auop.new_options = eur_id(db).options;
         auop.new_options.whitelist_markets.insert( core_id );
         auop.new_options.blacklist_markets.insert( eur_id );
         auop.new_options.blacklist_markets.insert( usd_id );
         trx.operations.push_back( auop );
         PUSH_TX(db, trx, ~0);
      }

      // Ted exchanges with the pool
      // BTW reproduces bitshares-core issue #2350: white/blacklists not in effect
      result = exchange_with_liquidity_pool( ted_id, lp_id, asset( 1000, eur_id ), asset( 584, usd_id ) );

      BOOST_REQUIRE_EQUAL( result.paid.size(), 1u );
      BOOST_CHECK( result.paid.front() == asset( 1000, eur_id ) );
      BOOST_REQUIRE_EQUAL( result.received.size(), 1u );
      BOOST_CHECK( result.received.front() == asset( ted_receives, usd_id ) );
      BOOST_REQUIRE_EQUAL( result.fees.size(), 3u );
      BOOST_CHECK( result.fees.front() == asset( maker_fee, eur_id ) );
      BOOST_CHECK( result.fees.at(1) == asset( taker_fee, usd_id ) );
      BOOST_CHECK( result.fees.back() == asset( pool_taker_fee, usd_id ) );

      expected_pool_balance_a += delta_a; // 1000 + 998 = 1998
      expected_pool_balance_b += delta_b; // 1200 - 588 = 612
      BOOST_CHECK_EQUAL( lp_id(db).balance_a.value, expected_pool_balance_a);
      BOOST_CHECK_EQUAL( lp_id(db).balance_b.value, expected_pool_balance_b);
      BOOST_CHECK( lp_id(db).virtual_value == fc::uint128_t(expected_pool_balance_a) * expected_pool_balance_b );
      BOOST_CHECK_EQUAL( lpa_id(db).dynamic_data(db).current_supply.value, expected_lp_supply );

      expected_accumulated_fees_eur += maker_fee;
      expected_accumulated_fees_usd += taker_fee;
      BOOST_CHECK_EQUAL( eur_id(db).dynamic_data(db).accumulated_fees.value, expected_accumulated_fees_eur );
      BOOST_CHECK_EQUAL( usd_id(db).dynamic_data(db).accumulated_fees.value, expected_accumulated_fees_usd );

      expected_balance_ted_eur -= 1000;
      expected_balance_ted_usd += ted_receives;
      check_balances();

      // Calculates if Ted sells 1000 USD to the pool
      maker_fee = 3; // 1000 * 0.3%, usd
      delta_b = 997; // 1000 - 3
      // tmp_delta = 1998 - round_up( 1998 * 612 / (612+997) ) = 1998 - 760 = 1238
      delta_a = -1214; // - ( 1238 - round_down(1238 * 2%) ) = - ( 1238 - 24 ) = -1214
      pool_taker_fee = 24;
      taker_fee = 6; // 1214 * 0.5%, eur
      ted_receives = 1208; // 1214 - 6

      // Ted fails to exchange if asks for more
      BOOST_CHECK_THROW( exchange_with_liquidity_pool( ted_id, lp_id, asset( 1000, usd_id ), asset( 1209, eur_id ) ),
                         fc::exception );

      // Ted exchanges with the pool
      // BTW reproduces bitshares-core issue #2350: white/blacklists not in effect
      result = exchange_with_liquidity_pool( ted_id, lp_id, asset( 1000, usd_id ), asset( 600, eur_id ) );

      BOOST_REQUIRE_EQUAL( result.paid.size(), 1u );
      BOOST_CHECK( result.paid.front() == asset( 1000, usd_id ) );
      BOOST_REQUIRE_EQUAL( result.received.size(), 1u );
      BOOST_CHECK( result.received.front() == asset( ted_receives, eur_id ) );
      BOOST_REQUIRE_EQUAL( result.fees.size(), 3u );
      BOOST_CHECK( result.fees.front() == asset( maker_fee, usd_id ) );
      BOOST_CHECK( result.fees.at(1) == asset( taker_fee, eur_id ) );
      BOOST_CHECK( result.fees.back() == asset( pool_taker_fee, eur_id ) );

      expected_pool_balance_a += delta_a; // 1998 - 1214 = 784
      expected_pool_balance_b += delta_b; // 612 + 997 = 1609
      BOOST_CHECK_EQUAL( lp_id(db).balance_a.value, expected_pool_balance_a);
      BOOST_CHECK_EQUAL( lp_id(db).balance_b.value, expected_pool_balance_b);
      BOOST_CHECK( lp_id(db).virtual_value == fc::uint128_t(expected_pool_balance_a) * expected_pool_balance_b );
      BOOST_CHECK_EQUAL( lpa_id(db).dynamic_data(db).current_supply.value, expected_lp_supply );

      expected_accumulated_fees_eur += taker_fee;
      expected_accumulated_fees_usd += maker_fee;
      BOOST_CHECK_EQUAL( eur_id(db).dynamic_data(db).accumulated_fees.value, expected_accumulated_fees_eur );
      BOOST_CHECK_EQUAL( usd_id(db).dynamic_data(db).accumulated_fees.value, expected_accumulated_fees_usd );

      expected_balance_ted_eur += ted_receives;
      expected_balance_ted_usd -= 1000;
      check_balances();

      // Withdraw
      result = withdraw_from_liquidity_pool( sam_id, lp_id, asset( 1000, lpa_id) );

      // Generates a block
      generate_block();
      BOOST_CHECK_EQUAL( eur_id(db).dynamic_data(db).accumulated_fees.value, expected_accumulated_fees_eur );

      graphene::market_history::liquidity_pool_ticker_id_type ticker_id( lp_id.instance );
      const auto& ticker = db.get( ticker_id );
      BOOST_CHECK_EQUAL( ticker._24h_exchange_a2b_count, 1u );
      BOOST_CHECK_EQUAL( ticker.total_exchange_a2b_count, 1u );
      BOOST_CHECK_EQUAL( ticker._24h_exchange_b2a_count, 1u );
      BOOST_CHECK_EQUAL( ticker.total_exchange_b2a_count, 1u );
      BOOST_CHECK_EQUAL( ticker._24h_deposit_count, 2u );
      BOOST_CHECK( ticker._24h_deposit_amount_a == 1000u );
      BOOST_CHECK( ticker._24h_deposit_amount_b == 1200u );
      BOOST_CHECK( ticker._24h_deposit_share_amount == 1200u );
      BOOST_CHECK_EQUAL( ticker.total_deposit_count, 2u );
      BOOST_CHECK( ticker.total_deposit_amount_a == 1000u );
      BOOST_CHECK( ticker.total_deposit_amount_b == 1200u );
      BOOST_CHECK( ticker.total_deposit_share_amount == 1200u );
      BOOST_CHECK_EQUAL( ticker._24h_withdrawal_count, 1u );
      BOOST_CHECK_EQUAL( ticker.total_withdrawal_count, 1u );

      // Check database API
      graphene::app::database_api db_api( db, &( app.get_options() ) );

      // get pool without statistics
      auto pools = db_api.get_liquidity_pools( { lp_id } );
      BOOST_REQUIRE_EQUAL( pools.size(), 1u );
      BOOST_REQUIRE( pools.front().valid() );
      BOOST_CHECK( !pools.front()->statistics.valid() );

      // get pool with statistics
      pools = db_api.get_liquidity_pools( { lp_id }, {}, true );
      BOOST_REQUIRE_EQUAL( pools.size(), 1u );
      BOOST_REQUIRE( pools.front().valid() );
      BOOST_REQUIRE( pools.front()->statistics.valid() );
      BOOST_CHECK( pools.front()->statistics->id == ticker_id );
      BOOST_CHECK_EQUAL( pools.front()->statistics->_24h_exchange_a2b_count, 1u );
      BOOST_CHECK_EQUAL( pools.front()->statistics->total_exchange_a2b_count, 1u );
      BOOST_CHECK_EQUAL( pools.front()->statistics->_24h_exchange_b2a_count, 1u );
      BOOST_CHECK_EQUAL( pools.front()->statistics->total_exchange_b2a_count, 1u );
      BOOST_CHECK_EQUAL( pools.front()->statistics->_24h_deposit_count, 2u );
      BOOST_CHECK( pools.front()->statistics->_24h_deposit_amount_a == 1000u );
      BOOST_CHECK( pools.front()->statistics->_24h_deposit_amount_b == 1200u );
      BOOST_CHECK( pools.front()->statistics->_24h_deposit_share_amount == 1200u );
      BOOST_CHECK_EQUAL( pools.front()->statistics->total_deposit_count, 2u );
      BOOST_CHECK( pools.front()->statistics->total_deposit_amount_a == 1000u );
      BOOST_CHECK( pools.front()->statistics->total_deposit_amount_b == 1200u );
      BOOST_CHECK( pools.front()->statistics->total_deposit_share_amount == 1200u );
      BOOST_CHECK_EQUAL( pools.front()->statistics->_24h_withdrawal_count, 1u );
      BOOST_CHECK_EQUAL( pools.front()->statistics->total_withdrawal_count, 1u );

      generate_blocks( db.head_block_time() + fc::days(2) );

      BOOST_CHECK_EQUAL( ticker._24h_exchange_a2b_count, 0u );
      BOOST_CHECK_EQUAL( ticker.total_exchange_a2b_count, 1u );
      BOOST_CHECK_EQUAL( ticker._24h_exchange_b2a_count, 0u );
      BOOST_CHECK_EQUAL( ticker.total_exchange_b2a_count, 1u );
      BOOST_CHECK_EQUAL( ticker._24h_deposit_count, 0u );
      BOOST_CHECK( ticker._24h_deposit_amount_a == 0u );
      BOOST_CHECK( ticker._24h_deposit_amount_b == 0u );
      BOOST_CHECK( ticker._24h_deposit_share_amount == 0u );
      BOOST_CHECK_EQUAL( ticker.total_deposit_count, 2u );
      BOOST_CHECK( ticker.total_deposit_amount_a == 1000u );
      BOOST_CHECK( ticker.total_deposit_amount_b == 1200u );
      BOOST_CHECK( ticker.total_deposit_share_amount == 1200u );
      BOOST_CHECK_EQUAL( ticker._24h_withdrawal_count, 0u );
      BOOST_CHECK_EQUAL( ticker.total_withdrawal_count, 1u );

      // Check history API
      graphene::app::history_api hist_api(app);
      auto head_time = db.head_block_time();

      // all histories : 1:create, 2:deposit, 3:deposit, 4:exchange, 5:exchange, 6:withdrawal
      // The 1st block: {1, 2}, the 2nd block: {3, 4, 5, 6}
      auto histories = hist_api.get_liquidity_pool_history( lp_id );
      BOOST_CHECK_EQUAL( histories.size(), 6u );

      // limit = 3
      histories = hist_api.get_liquidity_pool_history( lp_id, {}, {}, 3 );
      BOOST_CHECK_EQUAL( histories.size(), 3u );

      // only deposits
      histories = hist_api.get_liquidity_pool_history( lp_id, {}, {}, {}, 61 );
      BOOST_REQUIRE_EQUAL( histories.size(), 2u );
      auto second_time = histories[0].time;
      auto first_time = histories[1].time;
      auto late_time = second_time + fc::seconds(1);
      auto early_time = first_time - fc::seconds(1);
      histories = hist_api.get_liquidity_pool_history( lp_id, second_time, {}, {}, 61 );
      BOOST_CHECK_EQUAL( histories.size(), 2u );
      histories = hist_api.get_liquidity_pool_history( lp_id, second_time, {}, 1, 61 );
      BOOST_CHECK_EQUAL( histories.size(), 1u );
      histories = hist_api.get_liquidity_pool_history( lp_id, second_time, first_time, {}, 61 );
      BOOST_CHECK_EQUAL( histories.size(), 1u );
      histories = hist_api.get_liquidity_pool_history( lp_id, first_time, {}, {}, 61 );
      BOOST_CHECK_EQUAL( histories.size(), 1u );
      histories = hist_api.get_liquidity_pool_history( lp_id, {}, first_time, {}, 61 );
      BOOST_CHECK_EQUAL( histories.size(), 1u );
      histories = hist_api.get_liquidity_pool_history( lp_id, {}, early_time, {}, 61 );
      BOOST_CHECK_EQUAL( histories.size(), 2u );
      histories = hist_api.get_liquidity_pool_history( lp_id, {}, early_time, 1, 61 );
      BOOST_CHECK_EQUAL( histories.size(), 1u );
      histories = hist_api.get_liquidity_pool_history( lp_id, second_time, early_time, 5, 61 );
      BOOST_CHECK_EQUAL( histories.size(), 2u );
      histories = hist_api.get_liquidity_pool_history( lp_id, second_time, early_time, 1, 61 );
      BOOST_CHECK_EQUAL( histories.size(), 1u );

      // time is fine
      histories = hist_api.get_liquidity_pool_history( lp_id, second_time, first_time );
      BOOST_CHECK_EQUAL( histories.size(), 4u );
      histories = hist_api.get_liquidity_pool_history( lp_id, {}, first_time );
      BOOST_CHECK_EQUAL( histories.size(), 4u );
      histories = hist_api.get_liquidity_pool_history( lp_id, first_time, early_time );
      BOOST_CHECK_EQUAL( histories.size(), 2u );
      histories = hist_api.get_liquidity_pool_history( lp_id, {}, early_time );
      BOOST_CHECK_EQUAL( histories.size(), 6u );

      // time too early
      histories = hist_api.get_liquidity_pool_history( lp_id, head_time - fc::days(3) );
      BOOST_CHECK_EQUAL( histories.size(), 0u );

      // time too late
      histories = hist_api.get_liquidity_pool_history( lp_id, head_time, head_time - fc::days(1) );
      BOOST_CHECK_EQUAL( histories.size(), 0u );

      // stop and start are the same, or stop is later than start
      histories = hist_api.get_liquidity_pool_history( lp_id, second_time, second_time );
      BOOST_CHECK_EQUAL( histories.size(), 0u );
      histories = hist_api.get_liquidity_pool_history( lp_id, first_time, second_time, 10 );
      BOOST_CHECK_EQUAL( histories.size(), 0u );
      histories = hist_api.get_liquidity_pool_history( lp_id, first_time, late_time, 10 );
      BOOST_CHECK_EQUAL( histories.size(), 0u );

      // time is fine, only exchanges
      histories = hist_api.get_liquidity_pool_history( lp_id, {}, head_time - fc::days(3), {}, 63 );
      BOOST_CHECK_EQUAL( histories.size(), 2u );

      // all histories : 1:create, 2:deposit, 3:deposit, 4:exchange, 5:exchange, 6:withdrawal
      // The 1st block: {1, 2}, the 2nd block: {3, 4, 5, 6}

      // start = 2, limit = 3, so result sequence == {2,1}
      // note: range is (stop, start]
      histories = hist_api.get_liquidity_pool_history_by_sequence( lp_id, 2, {}, 3 );
      BOOST_CHECK_EQUAL( histories.size(), 2u );

      // start = 2, limit = 1, so result sequence == {2}
      histories = hist_api.get_liquidity_pool_history_by_sequence( lp_id, 2, {}, 1 );
      BOOST_CHECK_EQUAL( histories.size(), 1u );

      // start = 2, limit = 50, stop = the 2nd block time or later, result sequence == {}
      histories = hist_api.get_liquidity_pool_history_by_sequence( lp_id, 2, second_time, 50 );
      BOOST_CHECK_EQUAL( histories.size(), 0u );
      histories = hist_api.get_liquidity_pool_history_by_sequence( lp_id, 2, late_time, 50 );
      BOOST_CHECK_EQUAL( histories.size(), 0u );

      // start = 1, limit = 10, stop = the 1st block time, result sequence == {}
      histories = hist_api.get_liquidity_pool_history_by_sequence( lp_id, 1, first_time, 10 );
      BOOST_CHECK_EQUAL( histories.size(), 0u );

      // start = 4, limit is default, stop = the 1st block time, result sequence == {4,3}
      histories = hist_api.get_liquidity_pool_history_by_sequence( lp_id, 4, first_time );
      BOOST_CHECK_EQUAL( histories.size(), 2u );

      // start = 4, limit is default, but exchange only, so result sequence == {4}
      histories = hist_api.get_liquidity_pool_history_by_sequence( lp_id, 4, head_time - fc::days(3), {}, 63 );
      BOOST_CHECK_EQUAL( histories.size(), 1u );

      // start = 4, limit is default, stop = the 2nd block time or later, exchange only, result sequence == {}
      histories = hist_api.get_liquidity_pool_history_by_sequence( lp_id, 4, second_time, {}, 63 );
      BOOST_CHECK_EQUAL( histories.size(), 0u );
      histories = hist_api.get_liquidity_pool_history_by_sequence( lp_id, 4, late_time, {}, 63 );
      BOOST_CHECK_EQUAL( histories.size(), 0u );

      // Proceeds to the hard fork time that added white/blacklist checks for bitshares-core issue #2350
      generate_blocks( HARDFORK_CORE_2350_TIME );

      // Ted now fails to exchange due to the white/blacklists
      BOOST_CHECK_THROW( exchange_with_liquidity_pool( ted_id, lp_id, asset( 1000, eur_id ), asset( 1, usd_id ) ),
                         fc::exception );
      BOOST_CHECK_THROW( exchange_with_liquidity_pool( ted_id, lp_id, asset( 1000, usd_id ), asset( 1, eur_id ) ),
                         fc::exception );

      // Remove market blacklists and whitelists
      {
         asset_update_operation auop;

         auop.issuer = usd_id(db).issuer;
         auop.asset_to_update = usd_id;
         auop.new_options = usd_id(db).options;
         auop.new_options.whitelist_markets.clear();
         auop.new_options.blacklist_markets.clear();
         trx.operations.clear();
         trx.operations.push_back( auop );

         auop.issuer = eur_id(db).issuer;
         auop.asset_to_update = eur_id;
         auop.new_options = eur_id(db).options;
         auop.new_options.whitelist_markets.clear();
         auop.new_options.blacklist_markets.clear();
         trx.operations.push_back( auop );
         PUSH_TX(db, trx, ~0);
      }
      // Able to exchange
      exchange_with_liquidity_pool( ted_id, lp_id, asset( 1000, eur_id ), asset( 1, usd_id ) );
      exchange_with_liquidity_pool( ted_id, lp_id, asset( 1000, usd_id ), asset( 1, eur_id ) );

      // Setup a whitelist without EUR for USD
      {
         asset_update_operation auop;

         auop.issuer = usd_id(db).issuer;
         auop.asset_to_update = usd_id;
         auop.new_options = usd_id(db).options;
         auop.new_options.whitelist_markets.insert( core_id );
         trx.operations.clear();
         trx.operations.push_back( auop );
         PUSH_TX(db, trx, ~0);
      }
      // Now unable to exchange
      BOOST_CHECK_THROW( exchange_with_liquidity_pool( ted_id, lp_id, asset( 1000, eur_id ), asset( 1, usd_id ) ),
                         fc::exception );
      BOOST_CHECK_THROW( exchange_with_liquidity_pool( ted_id, lp_id, asset( 1000, usd_id ), asset( 1, eur_id ) ),
                         fc::exception );

      // Add the USD:EUR market to the whitelist of USD
      {
         asset_update_operation auop;

         auop.issuer = usd_id(db).issuer;
         auop.asset_to_update = usd_id;
         auop.new_options = usd_id(db).options;
         auop.new_options.whitelist_markets.insert( eur_id );
         trx.operations.clear();
         trx.operations.push_back( auop );
         PUSH_TX(db, trx, ~0);
      }
      // Able to exchange
      exchange_with_liquidity_pool( ted_id, lp_id, asset( 1000, eur_id ), asset( 1, usd_id ) );
      exchange_with_liquidity_pool( ted_id, lp_id, asset( 1000, usd_id ), asset( 1, eur_id ) );

      // Setup a blacklist without EUR for USD
      {
         asset_update_operation auop;

         auop.issuer = usd_id(db).issuer;
         auop.asset_to_update = usd_id;
         auop.new_options = usd_id(db).options;
         auop.new_options.blacklist_markets.insert( usd_id );
         trx.operations.clear();
         trx.operations.push_back( auop );
         PUSH_TX(db, trx, ~0);
      }
      // Able to exchange
      exchange_with_liquidity_pool( ted_id, lp_id, asset( 1000, eur_id ), asset( 1, usd_id ) );
      exchange_with_liquidity_pool( ted_id, lp_id, asset( 1000, usd_id ), asset( 1, eur_id ) );

      // Add EUR to blacklist of USD
      {
         asset_update_operation auop;

         auop.issuer = usd_id(db).issuer;
         auop.asset_to_update = usd_id;
         auop.new_options = usd_id(db).options;
         auop.new_options.whitelist_markets.clear();
         auop.new_options.blacklist_markets.insert( eur_id );
         trx.operations.clear();
         trx.operations.push_back( auop );
         PUSH_TX(db, trx, ~0);
      }
      // Now unable to exchange
      BOOST_CHECK_THROW( exchange_with_liquidity_pool( ted_id, lp_id, asset( 1000, eur_id ), asset( 1, usd_id ) ),
                         fc::exception );
      BOOST_CHECK_THROW( exchange_with_liquidity_pool( ted_id, lp_id, asset( 1000, usd_id ), asset( 1, eur_id ) ),
                         fc::exception );

      // Remove the USD:EUR market from the blacklist of USD
      {
         asset_update_operation auop;

         auop.issuer = usd_id(db).issuer;
         auop.asset_to_update = usd_id;
         auop.new_options = usd_id(db).options;
         auop.new_options.blacklist_markets.erase( eur_id );
         trx.operations.clear();
         trx.operations.push_back( auop );
         PUSH_TX(db, trx, ~0);
      }
      // Able to exchange
      exchange_with_liquidity_pool( ted_id, lp_id, asset( 1000, eur_id ), asset( 1, usd_id ) );
      exchange_with_liquidity_pool( ted_id, lp_id, asset( 1000, usd_id ), asset( 1, eur_id ) );

      // Setup a whitelist without USD for EUR
      {
         asset_update_operation auop;

         auop.issuer = eur_id(db).issuer;
         auop.asset_to_update = eur_id;
         auop.new_options = eur_id(db).options;
         auop.new_options.whitelist_markets.insert( core_id );
         trx.operations.clear();
         trx.operations.push_back( auop );
         PUSH_TX(db, trx, ~0);
      }
      // Now unable to exchange
      BOOST_CHECK_THROW( exchange_with_liquidity_pool( ted_id, lp_id, asset( 1000, eur_id ), asset( 1, usd_id ) ),
                         fc::exception );
      BOOST_CHECK_THROW( exchange_with_liquidity_pool( ted_id, lp_id, asset( 1000, usd_id ), asset( 1, eur_id ) ),
                         fc::exception );

      // Add the USD:EUR market to the whitelist of EUR
      {
         asset_update_operation auop;

         auop.issuer = eur_id(db).issuer;
         auop.asset_to_update = eur_id;
         auop.new_options = eur_id(db).options;
         auop.new_options.whitelist_markets.insert( usd_id );
         trx.operations.clear();
         trx.operations.push_back( auop );
         PUSH_TX(db, trx, ~0);
      }
      // Able to exchange
      exchange_with_liquidity_pool( ted_id, lp_id, asset( 1000, eur_id ), asset( 1, usd_id ) );
      exchange_with_liquidity_pool( ted_id, lp_id, asset( 1000, usd_id ), asset( 1, eur_id ) );

      // Setup a blacklist without USD for EUR
      {
         asset_update_operation auop;

         auop.issuer = eur_id(db).issuer;
         auop.asset_to_update = eur_id;
         auop.new_options = eur_id(db).options;
         auop.new_options.blacklist_markets.insert( eur_id );
         trx.operations.clear();
         trx.operations.push_back( auop );
         PUSH_TX(db, trx, ~0);
      }
      // Able to exchange
      exchange_with_liquidity_pool( ted_id, lp_id, asset( 1000, eur_id ), asset( 1, usd_id ) );
      exchange_with_liquidity_pool( ted_id, lp_id, asset( 1000, usd_id ), asset( 1, eur_id ) );

      // Add EUR:USD to the blacklist of EUR
      {
         asset_update_operation auop;

         auop.issuer = eur_id(db).issuer;
         auop.asset_to_update = eur_id;
         auop.new_options = eur_id(db).options;
         auop.new_options.whitelist_markets.clear();
         auop.new_options.blacklist_markets.insert( usd_id );
         trx.operations.clear();
         trx.operations.push_back( auop );
         PUSH_TX(db, trx, ~0);
      }
      // Now unable to exchange
      BOOST_CHECK_THROW( exchange_with_liquidity_pool( ted_id, lp_id, asset( 1000, eur_id ), asset( 1, usd_id ) ),
                         fc::exception );
      BOOST_CHECK_THROW( exchange_with_liquidity_pool( ted_id, lp_id, asset( 1000, usd_id ), asset( 1, eur_id ) ),
                         fc::exception );

      // Remove the USD:EUR market from the blacklist of EUR
      {
         asset_update_operation auop;

         auop.issuer = eur_id(db).issuer;
         auop.asset_to_update = eur_id;
         auop.new_options = eur_id(db).options;
         auop.new_options.blacklist_markets.erase( usd_id );
         trx.operations.clear();
         trx.operations.push_back( auop );
         PUSH_TX(db, trx, ~0);
      }
      // Able to exchange
      exchange_with_liquidity_pool( ted_id, lp_id, asset( 1000, eur_id ), asset( 1, usd_id ) );
      exchange_with_liquidity_pool( ted_id, lp_id, asset( 1000, usd_id ), asset( 1, eur_id ) );

} FC_CAPTURE_LOG_AND_RETHROW( (0) ) }

BOOST_AUTO_TEST_CASE( liquidity_pool_apis_test )
{ try {

      // Pass the hard fork time
      generate_blocks( HARDFORK_LIQUIDITY_POOL_TIME );
      set_expiration( db, trx );

      ACTORS((sam)(ted));

      const asset_object sam_eur = create_user_issued_asset( "SAMEUR", sam, charge_market_fee );
      const asset_object sam_usd = create_user_issued_asset( "SAMUSD", sam, charge_market_fee );
      const asset_object sam_lp1 = create_user_issued_asset( "SAMLP1", sam, charge_market_fee );
      const asset_object sam_lp2 = create_user_issued_asset( "SAMLP2", sam, charge_market_fee );

      const asset_object ted_eur = create_user_issued_asset( "TEDEUR", ted, charge_market_fee );
      const asset_object ted_usd = create_user_issued_asset( "TEDUSD", ted, charge_market_fee );
      const asset_object ted_lp1 = create_user_issued_asset( "TEDLP1", ted, charge_market_fee );
      const asset_object ted_lp2 = create_user_issued_asset( "TEDLP2", ted, charge_market_fee );
      const asset_object ted_lp3 = create_user_issued_asset( "TEDLP3", ted, charge_market_fee );

      // create liquidity pools
      const liquidity_pool_object sam_lpo1 = create_liquidity_pool( sam_id, sam_eur.get_id(), sam_usd.get_id(),
                                                                     sam_lp1.get_id(), 100, 310 );
      const liquidity_pool_object sam_lpo2 = create_liquidity_pool( sam_id, sam_usd.get_id(), ted_usd.get_id(),
                                                                     sam_lp2.get_id(), 200, 320 );
      const liquidity_pool_object ted_lpo1 = create_liquidity_pool( ted_id, sam_usd.get_id(), ted_usd.get_id(),
                                                                     ted_lp1.get_id(), 300, 330 );
      const liquidity_pool_object ted_lpo2 = create_liquidity_pool( ted_id, sam_usd.get_id(), ted_eur.get_id(),
                                                                     ted_lp2.get_id(), 400, 340 );
      const liquidity_pool_object ted_lpo3 = create_liquidity_pool( ted_id, ted_eur.get_id(), ted_usd.get_id(),
                                                                     ted_lp3.get_id(), 500, 350 );
      generate_block();

      // Check database API
      graphene::app::database_api db_api( db, &( app.get_options() ) );

      // list all pools
      auto pools = db_api.list_liquidity_pools();
      BOOST_REQUIRE_EQUAL( pools.size(), 5u );
      BOOST_CHECK( !pools.front().statistics.valid() );
      BOOST_CHECK( pools.front().id == sam_lpo1.get_id() );
      BOOST_CHECK( pools.back().id == ted_lpo3.get_id() );

      // pagination
      pools = db_api.list_liquidity_pools( 5, sam_lpo2.get_id() );
      BOOST_REQUIRE_EQUAL( pools.size(), 4u );
      BOOST_CHECK( !pools.front().statistics.valid() );
      BOOST_CHECK( pools.front().id == sam_lpo2.get_id() );
      BOOST_CHECK( pools.back().id == ted_lpo3.get_id() );

      // with statistics
      pools = db_api.list_liquidity_pools( 2, sam_lpo2.get_id(), true );
      BOOST_REQUIRE_EQUAL( pools.size(), 2u );
      BOOST_CHECK( pools.front().statistics.valid() );
      BOOST_CHECK( pools.front().id == sam_lpo2.get_id() );
      BOOST_CHECK( pools.back().id == ted_lpo1.get_id() );

      // get_liquidity_pools_by_asset_a
      pools = db_api.get_liquidity_pools_by_asset_a( "SAMUSD" );
      BOOST_REQUIRE_EQUAL( pools.size(), 3u );
      BOOST_CHECK( !pools.front().statistics.valid() );
      BOOST_CHECK( pools.front().id == sam_lpo2.get_id() );
      BOOST_CHECK( pools.back().id == ted_lpo2.get_id() );

      // pagination and with statistics
      pools = db_api.get_liquidity_pools_by_asset_a( "SAMUSD", 2, ted_lpo2.get_id(), true );
      BOOST_REQUIRE_EQUAL( pools.size(), 1u );
      BOOST_CHECK( pools.front().statistics.valid() );
      BOOST_CHECK( pools.front().id == ted_lpo2.get_id() );

      // get_liquidity_pools_by_asset_b
      pools = db_api.get_liquidity_pools_by_asset_b( "TEDUSD" );
      BOOST_REQUIRE_EQUAL( pools.size(), 3u );
      BOOST_CHECK( !pools.front().statistics.valid() );
      BOOST_CHECK( pools.front().id == sam_lpo2.get_id() );
      BOOST_CHECK( pools.back().id == ted_lpo3.get_id() );

      // pagination and with statistics
      pools = db_api.get_liquidity_pools_by_asset_b( "TEDUSD", 2, sam_lpo1.get_id(), true );
      BOOST_REQUIRE_EQUAL( pools.size(), 2u );
      BOOST_CHECK( pools.front().statistics.valid() );
      BOOST_CHECK( pools.front().id == sam_lpo2.get_id() );
      BOOST_CHECK( pools.back().id == ted_lpo1.get_id() );

      // get_liquidity_pools_by_one_asset
      pools = db_api.get_liquidity_pools_by_one_asset( "SAMUSD" );
      BOOST_REQUIRE_EQUAL( pools.size(), 4u );
      BOOST_CHECK( !pools.front().statistics.valid() );
      BOOST_CHECK( pools.front().id == sam_lpo1.get_id() );
      BOOST_CHECK( pools.back().id == ted_lpo2.get_id() );

      // pagination and with statistics
      pools = db_api.get_liquidity_pools_by_one_asset( "SAMUSD", 3, liquidity_pool_id_type(), true );
      BOOST_REQUIRE_EQUAL( pools.size(), 3u );
      BOOST_CHECK( pools.front().statistics.valid() );
      BOOST_CHECK( pools.front().id == sam_lpo1.get_id() );
      BOOST_CHECK( pools.back().id == ted_lpo1.get_id() );

      // get_liquidity_pools_by_both_asset
      pools = db_api.get_liquidity_pools_by_both_assets( "SAMUSD", "TEDUSD" );
      BOOST_REQUIRE_EQUAL( pools.size(), 2u );
      BOOST_CHECK( !pools.front().statistics.valid() );
      BOOST_CHECK( pools.front().id == sam_lpo2.get_id() );
      BOOST_CHECK( pools.back().id == ted_lpo1.get_id() );

      // pagination and with statistics
      pools = db_api.get_liquidity_pools_by_both_assets( "SAMUSD", "TEDUSD", 3, ted_lpo2.get_id(), true );
      BOOST_REQUIRE_EQUAL( pools.size(), 0u );

      // get_liquidity_pools_by_share_asset
      auto opools = db_api.get_liquidity_pools_by_share_asset( { "SAMLP1", "SAMEUR" }, true, true );
      BOOST_REQUIRE_EQUAL( opools.size(), 2u );
      BOOST_CHECK( opools.front().valid() );
      BOOST_CHECK( opools.front()->statistics.valid() );
      BOOST_CHECK( opools.front()->id == sam_lpo1.get_id() );
      BOOST_CHECK( !opools.back().valid() );

      // get_liquidity_pools_by_owner
      pools = db_api.get_liquidity_pools_by_owner( "sam" );
      BOOST_REQUIRE_EQUAL( pools.size(), 2u );
      BOOST_CHECK( !pools.front().statistics.valid() );
      BOOST_CHECK( pools.front().id == sam_lpo1.get_id() );
      BOOST_CHECK( pools.back().id == sam_lpo2.get_id() );

      // pagination and with statistics
      pools = db_api.get_liquidity_pools_by_owner( "ted", 5, ted_lp2.get_id(), true );
      BOOST_REQUIRE_EQUAL( pools.size(), 2u );
      BOOST_CHECK( pools.front().statistics.valid() );
      BOOST_CHECK( pools.front().id == ted_lpo2.get_id() );
      BOOST_CHECK( pools.back().id == ted_lpo3.get_id() );

} FC_CAPTURE_LOG_AND_RETHROW( (0) ) }

BOOST_AUTO_TEST_CASE( stableswap_create_test )
{
   try {

      // Before the StableSwap hard fork, creating a stable pool should fail even though
      // ordinary (constant-product) pools already work at this point in time.
      generate_blocks( HARDFORK_CORE_2604_TIME );
      generate_block();
      set_expiration( db, trx );

      ACTORS((sam));

      int64_t init_amount = 10000000 * GRAPHENE_BLOCKCHAIN_PRECISION;
      fund( sam, asset(init_amount) );

      // Two assets that share precision (4), plus a mismatched-precision one (2)
      const asset_object& usd4 = create_user_issued_asset( "STUSD", sam, 0, price(asset(1, asset_id_type(1)), asset(1)), 4 );
      const asset_object& eur4 = create_user_issued_asset( "STEUR", sam, 0, price(asset(1, asset_id_type(1)), asset(1)), 4 );
      const asset_object& cny2 = create_user_issued_asset( "STCNY", sam, 0, price(asset(1, asset_id_type(1)), asset(1)), 2 );
      const asset_object& lpa  = create_user_issued_asset( "STLPA", sam, 0 );

      // Order the pool assets by id as the operation requires asset_a < asset_b
      asset_id_type a = std::min( usd4.get_id(), eur4.get_id() );
      asset_id_type b = std::max( usd4.get_id(), eur4.get_id() );
      // Capture plain ids up front rather than holding the asset_object references across
      // generate_blocks( HARDFORK_STABLESWAP_TIME ) below: that call jumps the chain clock
      // ~15 years forward in one step, crossing a huge number of maintenance intervals, which
      // was observed (via gdb) to leave previously-obtained const asset_object& references
      // pointing at stale/reused storage -- e.g. usd4's reference read back a garbage id, and
      // cny2's read back eur4's symbol. asset_id_type is a plain small value type and stays
      // valid regardless of what happens to the underlying object storage.
      asset_id_type cny2_id = cny2.get_id();
      asset_id_type lpa_id = lpa.get_id();

      // Stable pool not yet allowed
      BOOST_CHECK_THROW( create_stable_liquidity_pool( sam_id, a, b, lpa_id, 0, 0, 100 ),
                         fc::exception );

      // Move past the StableSwap hard fork
      generate_blocks( HARDFORK_STABLESWAP_TIME );
      generate_block();
      set_expiration( db, trx );

      // Mismatched precision is rejected
      asset_id_type ma = std::min( a, cny2_id );
      asset_id_type mb = std::max( a, cny2_id );
      BOOST_CHECK_THROW( create_stable_liquidity_pool( sam_id, ma, mb, lpa_id, 0, 0, 100 ),
                         fc::exception );

      // Amplification out of range is rejected
      BOOST_CHECK_THROW( create_stable_liquidity_pool( sam_id, a, b, lpa_id, 0, 0,
                                                       STABLESWAP_AMP_MAX + 1 ),
                         fc::exception );

      // A valid stable pool is created and carries the new fields
      const liquidity_pool_object& lpo = create_stable_liquidity_pool( sam_id, a, b, lpa_id, 0, 0, 100 );
      BOOST_CHECK( lpo.is_stable() );
      BOOST_CHECK( lpo.pool_type == liquidity_pool_curve_type::stable );
      BOOST_CHECK_EQUAL( lpo.amplification, 100u );
      BOOST_CHECK( lpo.virtual_value == 0 ); // empty pool

   } FC_CAPTURE_LOG_AND_RETHROW( (0) ) }

BOOST_AUTO_TEST_CASE( stableswap_exchange_test )
{
   try {

      generate_blocks( HARDFORK_STABLESWAP_TIME );
      generate_block();
      set_expiration( db, trx );

      ACTORS((sam)(ted));

      int64_t init_amount = 10000000 * GRAPHENE_BLOCKCHAIN_PRECISION;
      fund( sam, asset(init_amount) );
      fund( ted, asset(init_amount) );

      // Two equal-precision, fee-free assets so we can compare curves cleanly
      const asset_object& usd = create_user_issued_asset( "SXUSD", sam, 0, price(asset(1, asset_id_type(1)), asset(1)), 4 );
      const asset_object& eur = create_user_issued_asset( "SXEUR", sam, 0, price(asset(1, asset_id_type(1)), asset(1)), 4 );
      const asset_object& slp = create_user_issued_asset( "SXSLP", sam, 0 ); // stable pool share asset
      const asset_object& clp = create_user_issued_asset( "SXCLP", sam, 0 ); // constant-product share asset

      asset_id_type usd_id = usd.get_id();
      asset_id_type eur_id = eur.get_id();
      asset_id_type a = std::min( usd_id, eur_id );
      asset_id_type b = std::max( usd_id, eur_id );

      issue_uia( sam, usd.amount( init_amount ) );
      issue_uia( sam, eur.amount( init_amount ) );
      issue_uia( ted, usd.amount( init_amount ) );
      issue_uia( ted, eur.amount( init_amount ) );

      const int64_t liq = 1000000; // balanced liquidity on each side

      // Stable pool, A = 100, no fees
      const liquidity_pool_object& s_lpo = create_stable_liquidity_pool( sam_id, a, b, slp.get_id(), 0, 0, 100 );
      liquidity_pool_id_type s_id = s_lpo.get_id();
      deposit_to_liquidity_pool( sam_id, s_id, asset( liq, a ), asset( liq, b ) );

      // The stored invariant is D, not k. At perfect balance D == x + y.
      BOOST_CHECK( s_id(db).virtual_value
                   == stableswap::compute_d( fc::uint128_t(liq), fc::uint128_t(liq), 100 ) );
      BOOST_CHECK( s_id(db).virtual_value == fc::uint128_t( 2 * liq ) );

      // Constant-product pool with identical balances for comparison
      const liquidity_pool_object& c_lpo = create_liquidity_pool( sam_id, a, b, clp.get_id(), 0, 0 );
      liquidity_pool_id_type c_id = c_lpo.get_id();
      deposit_to_liquidity_pool( sam_id, c_id, asset( liq, a ), asset( liq, b ) );
      BOOST_CHECK( c_id(db).virtual_value == fc::uint128_t(liq) * liq );

      // ~10% of the pool: large enough that the curve advantage clearly exceeds the
      // integer rounding the evaluator applies in the pool's favour.
      const int64_t sell = 100000;

      const auto d_before = s_id(db).virtual_value;

      generic_exchange_operation_result s_res =
            exchange_with_liquidity_pool( ted_id, s_id, asset( sell, a ), asset( 1, b ) );
      generic_exchange_operation_result c_res =
            exchange_with_liquidity_pool( ted_id, c_id, asset( sell, a ), asset( 1, b ) );

      int64_t stable_out = s_res.received.front().amount.value;
      int64_t cp_out     = c_res.received.front().amount.value;

      // StableSwap gives materially less slippage than constant-product for a
      // like-valued pair, while still never returning more than the input.
      BOOST_CHECK_GT( stable_out, cp_out );
      BOOST_CHECK_GT( stable_out - cp_out, 1000 ); // a real curve advantage, not rounding
      BOOST_CHECK_LE( stable_out, sell );

      // The invariant D must never decrease across a swap (fees would only raise it).
      BOOST_CHECK( s_id(db).virtual_value >= d_before );

      // A swap in the other direction also executes and preserves the invariant.
      const auto d_before2 = s_id(db).virtual_value;
      generic_exchange_operation_result s_res2 =
            exchange_with_liquidity_pool( ted_id, s_id, asset( sell, b ), asset( 1, a ) );
      BOOST_CHECK_GT( s_res2.received.front().amount.value, 0 );
      BOOST_CHECK( s_id(db).virtual_value >= d_before2 );

   } FC_CAPTURE_LOG_AND_RETHROW( (0) ) }

BOOST_AUTO_TEST_SUITE_END()


BOOST_AUTO_TEST_SUITE( stableswap_differential_tests )

/**
 * The constant-product swap path was REFACTORED when StableSwap was added: the two per-asset
 * branches became one, with the curve chosen by pool type. The arithmetic was meant to be
 * untouched, but "meant to be" is not evidence, and this is live consensus code that prices
 * every existing pool on the chain.
 *
 * A mainnet replay cannot settle it -- liquidity pools postdate the block log available here,
 * and the replay only checks serialisation anyway, never running an evaluator. So this
 * compares the REAL evaluator against the formula as it stood before the refactor, over a
 * wide spread of balances and trade sizes including the awkward corners: one-unit trades,
 * trades that dwarf the pool, and extreme imbalance where the rounding decides the outcome.
 *
 * The reference below is master's expression, transcribed literally:
 *
 *     new_balance_b = ( virtual_value + new_balance_a - 1 ) / new_balance_a     // round up
 *     delta         = balance_b - new_balance_b
 */
// No FC_LOG_AND_RETHROW here on purpose: its catch(...) turns a Boost REQUIRE failure into
// "unknown type" with no indication of which case failed.
BOOST_FIXTURE_TEST_CASE( constant_product_matches_the_pre_refactor_formula, database_fixture )
{
   // Liquidity pools do not exist before their hardfork; without this the very first
   // create_liquidity_pool throws and the whole comparison never runs.
   generate_blocks( HARDFORK_LIQUIDITY_POOL_TIME );
   generate_block();
   set_expiration( db, trx );

   ACTORS( (sam)(ted) );

   const int64_t huge = 1000000000000LL;
   fund( sam, asset(huge) );
   fund( ted, asset(huge) );

   // the historical expression, kept verbatim so a future edit to the evaluator has something
   // independent to disagree with
   auto reference_out = []( int64_t balance_in, int64_t balance_out, int64_t sold ) -> int64_t
   {
      const fc::uint128_t virtual_value = fc::uint128_t( balance_in ) * balance_out;
      const int64_t new_in = balance_in + sold;
      const fc::uint128_t new_out = ( virtual_value + new_in - 1 ) / new_in;   // round up
      return static_cast<int64_t>( fc::uint128_t( balance_out ) - new_out );
   };

   struct { int64_t a, b, sell; } cases[] = {
      {      1000,      1000,       1 },   // one unit in
      {      1000,      1000,      10 },
      {      1000,      1000,     999 },
      {      1000,      1000,    5000 },   // trade dwarfs the pool
      {         2,         2,       1 },   // smallest meaningful pool
      {   1000000,         3,       1 },   // extreme imbalance, rounding decides
      {         3,   1000000,       1 },
      {   1000000,   1000000,       7 },
      {  99999999,      1234,   45678 },   // nothing round about any of it
      { 123456789, 987654321,       1 },
      { 123456789, 987654321, 1000000 },
   };

   int checked = 0;
   for( const auto& c : cases )
   {
      BOOST_TEST_MESSAGE( "case " + std::to_string( checked ) + ": a=" + std::to_string( c.a )
                          + " b=" + std::to_string( c.b ) + " sell=" + std::to_string( c.sell ) );
      // a fresh pool per case, so one case cannot contaminate the next
      const std::string suffix = std::to_string( checked );
      const asset_object& coin_a = create_user_issued_asset( "COINA" + suffix );
      const asset_object& coin_b = create_user_issued_asset( "COINB" + suffix );
      // the share asset must belong to the pool's creator, or create_liquidity_pool refuses it
      const asset_object& share  = create_user_issued_asset( "SHARE" + suffix, sam,
                                                             charge_market_fee );
      const auto a_id = coin_a.get_id();
      const auto b_id = coin_b.get_id();

      issue_uia( sam, coin_a.amount( c.a ) );
      issue_uia( sam, coin_b.amount( c.b ) );
      issue_uia( ted, coin_a.amount( c.sell ) );

      const auto& pool = create_liquidity_pool( sam_id, a_id, b_id, share.get_id(), 0, 0 );
      const auto pool_id = pool.get_id();
      deposit_to_liquidity_pool( sam_id, pool_id, coin_a.amount( c.a ), coin_b.amount( c.b ) );

      // a taker fee of zero keeps this about the curve and nothing else
      BOOST_REQUIRE_EQUAL( pool_id(db).balance_a.value, c.a );
      BOOST_REQUIRE_EQUAL( pool_id(db).balance_b.value, c.b );

      const int64_t expected = reference_out( c.a, c.b, c.sell );

      if( expected <= 0 || expected >= c.b )
      {
         // the pre-refactor formula would drain or return nothing; the evaluator refuses such
         // a trade, and agreeing that it is refused is itself the comparison
         GRAPHENE_REQUIRE_THROW(
            exchange_with_liquidity_pool( ted_id, pool_id, coin_a.amount( c.sell ),
                                          coin_b.amount( 1 ) ), fc::exception );
      }
      else
      {
         auto result = exchange_with_liquidity_pool( ted_id, pool_id, coin_a.amount( c.sell ),
                                                     coin_b.amount( 1 ) );
         const int64_t got = ( c.b - pool_id(db).balance_b.value );
         BOOST_CHECK_MESSAGE( got == expected,
            "balances (" + std::to_string(c.a) + "," + std::to_string(c.b) + ") selling "
            + std::to_string(c.sell) + ": evaluator paid " + std::to_string(got)
            + ", pre-refactor formula says " + std::to_string(expected) );

         // and the invariant may only ever grow, never shrink: that is what stops a sequence
         // of small trades extracting value the curve did not intend to give
         const fc::uint128_t before = fc::uint128_t( c.a ) * c.b;
         const fc::uint128_t after  = fc::uint128_t( pool_id(db).balance_a.value )
                                    * pool_id(db).balance_b.value;
         BOOST_CHECK_MESSAGE( after >= before,
            "invariant shrank for balances (" + std::to_string(c.a) + ","
            + std::to_string(c.b) + ")" );
      }
      ++checked;
   }
   BOOST_CHECK_EQUAL( checked, 11 );
}

BOOST_AUTO_TEST_SUITE_END()

BOOST_AUTO_TEST_SUITE( stableswap_fuzz_tests )

/**
 * The StableSwap curve is solved by Newton's method over integers, and integer Newton has two
 * ways to hurt a chain that the hand-picked cases elsewhere in this file cannot rule out:
 *
 *   1. it fails to converge for some balance/amplification combination, permanently bricking
 *      a pool that was legal to create; and
 *   2. it converges, but rounds the wrong way often enough that a trader can grind value out
 *      of the pool one unit at a time.
 *
 * Both are properties of the whole input space, not of any one case, so these tests sweep it
 * pseudo-randomly with a FIXED seed -- a failure here reproduces exactly rather than being a
 * story about a build that once went red.
 *
 * These operate on the header's pure functions, mirroring the evaluator's arithmetic, so they
 * can run hundreds of thousands of cases. The evaluator-level behaviour is covered by
 * stableswap_exchange_test; what is under test here is the maths beneath it.
 */
namespace {

/// The evaluator's stable-pool swap, fees removed so that only rounding decides the result.
/// Kept deliberately parallel to liquidity_pool_evaluator.cpp -- if that rounding changes,
/// this must change with it, and the tests below say what the change is allowed to cost.
/// Returns the amount paid out, or -1 for a trade the evaluator would have rejected.
int64_t ss_swap_out( int64_t y, uint64_t amp, int64_t new_x, const fc::uint128_t& d )
{
   const fc::uint128_t new_x128( new_x );
   fc::uint128_t new_y = stableswap::compute_new_y( new_x128, d, amp );
   new_y += 1;                                     // unconditionally in the pool's favour
   if( new_y > fc::uint128_t( y ) )
      new_y = fc::uint128_t( y );
   return static_cast<int64_t>( fc::uint128_t( y ) - new_y );
}

} // namespace

/**
 * D must exist for every pool the protocol allows, and must sit between the two curves it
 * interpolates: the constant-product invariant 2*sqrt(x*y) at A -> 0, and the constant-sum
 * invariant x+y at A -> infinity. A D outside that band is not a rounding wobble, it means
 * the solver converged on the wrong root.
 */
BOOST_AUTO_TEST_CASE( d_converges_and_stays_between_the_two_curves )
{
   std::mt19937_64 rng( 20260821 );               // fixed seed: failures must reproduce
   const int64_t max_balance = GRAPHENE_MAX_SHARE_SUPPLY;

   int checked = 0;
   for( int i = 0; i < 200000; ++i )
   {
      // Log-uniform over the whole legal range, so tiny pools are sampled as densely as
      // huge ones; uniform sampling would spend every case in the top decade.
      auto log_uniform = [&]( int64_t hi ) -> int64_t
      {
         const int bits = 1 + static_cast<int>( rng() % 62 );
         int64_t v = static_cast<int64_t>( rng() % ( 1ULL << bits ) ) + 1;
         return v > hi ? hi : v;
      };

      const int64_t x = log_uniform( max_balance );
      const int64_t y = log_uniform( max_balance );
      const uint64_t amp = 1 + rng() % 1000000;

      fc::uint128_t d;
      BOOST_REQUIRE_NO_THROW( d = stableswap::compute_d( fc::uint128_t( x ), fc::uint128_t( y ), amp ) );

      const boost::multiprecision::uint256_t prod =
            boost::multiprecision::uint256_t( x ) * boost::multiprecision::uint256_t( y );
      const boost::multiprecision::uint256_t lower = 2 * boost::multiprecision::sqrt( prod );
      const boost::multiprecision::uint256_t upper =
            boost::multiprecision::uint256_t( x ) + boost::multiprecision::uint256_t( y );
      const boost::multiprecision::uint256_t d256( d );

      // One unit of slack at each end: Newton stops at a difference of <= 1.
      BOOST_REQUIRE_MESSAGE( d256 + 1 >= lower && d256 <= upper + 1,
                             "D out of band for x=" << x << " y=" << y << " amp=" << amp
                             << ": D=" << d256 << " not in [" << lower << ", " << upper << "]" );
      ++checked;
   }
   BOOST_CHECK_EQUAL( checked, 200000 );
}

/**
 * The safety property the whole pool rests on: a swap may never lower D. D is what every
 * liquidity provider's share is denominated in, so a swap that reduces it takes value from
 * the providers and hands it to the trader. Rounding must always fall the pool's way.
 */
BOOST_AUTO_TEST_CASE( a_swap_never_lowers_d )
{
   std::mt19937_64 rng( 20260822 );

   int checked = 0, rejected = 0;
   for( int i = 0; i < 100000; ++i )
   {
      const int bits_x = 4 + static_cast<int>( rng() % 50 );
      const int bits_y = 4 + static_cast<int>( rng() % 50 );
      const int64_t x = static_cast<int64_t>( rng() % ( 1ULL << bits_x ) ) + 2;
      const int64_t y = static_cast<int64_t>( rng() % ( 1ULL << bits_y ) ) + 2;
      const uint64_t amp = 1 + rng() % 100000;

      // Trade sizes from a single unit (where rounding dominates) up to several times the
      // pool (where the curve is at its most extreme).
      const int64_t dx = 1 + static_cast<int64_t>( rng() % static_cast<uint64_t>( 4 * x ) );
      if( x > GRAPHENE_MAX_SHARE_SUPPLY - dx )
         continue;

      const int64_t new_x = x + dx;

      // Every solver call stays inside this guard on purpose: fc::exception does not derive
      // from std::exception, so one escaping here is reported by Boost as a bare "unknown
      // type" with none of the inputs echoed, which is the least useful failure possible.
      fc::uint128_t d, d_after;
      int64_t out;
      try
      {
         d = stableswap::compute_d( fc::uint128_t( x ), fc::uint128_t( y ), amp );
         out = ss_swap_out( y, amp, new_x, d );
         if( out < 0 )
         {
            ++rejected;
            continue;
         }
         BOOST_REQUIRE_MESSAGE( out <= y - 1,
                                "swap drained the pool: x=" << x << " y=" << y << " amp=" << amp
                                << " dx=" << dx << " out=" << out );
         d_after = stableswap::compute_d( fc::uint128_t( new_x ), fc::uint128_t( y - out ), amp );
      }
      catch( const fc::exception& )
      {
         ++rejected;                               // a trade the chain refuses is not a loss
         continue;
      }

      BOOST_REQUIRE_MESSAGE( d_after >= d,
                             "D fell across a swap: x=" << x << " y=" << y << " amp=" << amp
                             << " dx=" << dx << " out=" << out
                             << " D=" << boost::multiprecision::uint256_t( d )
                             << " -> " << boost::multiprecision::uint256_t( d_after ) );
      ++checked;
   }
   BOOST_TEST_MESSAGE( "a_swap_never_lowers_d: " << checked << " swaps checked, "
                       << rejected << " rejected by the evaluator's own guards" );
   BOOST_CHECK_GT( checked, 50000 );
}

/**
 * The grind attack, stated directly: swap one way, swap straight back, and you must not come
 * out ahead. Rounding errors that individually look like a single unit are only harmless if
 * they cannot be repeated, so this also runs the round trip in a loop against one pool and
 * checks the attacker's balance never rises above where it started.
 */
BOOST_AUTO_TEST_CASE( round_trips_cannot_extract_value )
{
   std::mt19937_64 rng( 20260823 );

   // --- single round trips across a wide spread of pools -------------------------------
   int checked = 0;
   for( int i = 0; i < 50000; ++i )
   {
      const int bits = 8 + static_cast<int>( rng() % 44 );
      const int64_t x = static_cast<int64_t>( rng() % ( 1ULL << bits ) ) + 100;
      const int64_t y = static_cast<int64_t>( rng() % ( 1ULL << bits ) ) + 100;
      const uint64_t amp = 1 + rng() % 100000;
      const int64_t dx = 1 + static_cast<int64_t>( rng() % static_cast<uint64_t>( x ) );

      try
      {
         const fc::uint128_t d = stableswap::compute_d( fc::uint128_t( x ), fc::uint128_t( y ), amp );
         const int64_t out = ss_swap_out( y, amp, x + dx, d );
         if( out <= 0 )
            continue;

         // Sell the proceeds straight back into the pool it just came from.
         const int64_t x2 = x + dx, y2 = y - out;
         const fc::uint128_t d2 = stableswap::compute_d( fc::uint128_t( x2 ), fc::uint128_t( y2 ), amp );
         const int64_t back = ss_swap_out( x2, amp, y2 + out, d2 );
         if( back < 0 )
            continue;

         // Exactly, not approximately: the evaluator rounds every payout in the pool's
         // favour, so a round trip can never return more than it cost. This assertion is
         // the reason that rule exists -- before it, this line caught a trader coming back
         // a unit up on x=159 y=141 amp=72554 dx=103, which repeated into a real drain.
         BOOST_REQUIRE_MESSAGE( back <= dx,
                                "round trip returned more than it cost: x=" << x
                                << " y=" << y << " amp=" << amp << " dx=" << dx
                                << " back=" << back );
         ++checked;
      }
      catch( const fc::exception& )
      {
         continue;
      }
   }
   BOOST_CHECK_GT( checked, 20000 );

   // --- the same pool, ground repeatedly ------------------------------------------------
   // A one-unit-per-trip leak only matters if it can be repeated, so repeat it.
   struct { int64_t x, y; uint64_t amp; int64_t trade; } grinds[] = {
      {      1000000,      1000000,     100,      1 },   // minimum trade, balanced pool
      {      1000000,      1000000,   10000,      1 },   // high amplification, flattest curve
      {      1000000,      1000000,       1,      1 },   // amplification floor
      {      1000000,       999999,     100,      1 },   // barely imbalanced
      {   1000000000,         1000,     100,      1 },   // extreme imbalance
      {      1000000,      1000000,     100,   1000 },
   };

   for( const auto& g : grinds )
   {
      int64_t x = g.x, y = g.y;
      int64_t attacker_a = 1000000000, attacker_b = 0;
      const int64_t start_a = attacker_a;

      for( int round = 0; round < 500; ++round )
      {
         if( g.trade > attacker_a )
            break;
         const fc::uint128_t d = stableswap::compute_d( fc::uint128_t( x ), fc::uint128_t( y ), g.amp );
         const int64_t out = ss_swap_out( y, g.amp, x + g.trade, d );
         if( out <= 0 )
            break;
         attacker_a -= g.trade; attacker_b += out;
         x += g.trade;          y -= out;

         const fc::uint128_t d2 = stableswap::compute_d( fc::uint128_t( x ), fc::uint128_t( y ), g.amp );
         const int64_t back = ss_swap_out( x, g.amp, y + attacker_b, d2 );
         if( back <= 0 )
            break;
         y += attacker_b;       x -= back;
         attacker_a += back;    attacker_b = 0;

         BOOST_REQUIRE_MESSAGE( attacker_a <= start_a,
                                "grind profited after " << round << " rounds: pool began x="
                                << g.x << " y=" << g.y << " amp=" << g.amp << " trade=" << g.trade
                                << "; attacker " << start_a << " -> " << attacker_a );
      }
   }
}

/**
 * Selling more must never pay out less. A non-monotonic curve would let a trader improve
 * their fill by splitting or padding an order, which is both a mispricing and a way to
 * probe for the rounding steps the test above is guarding.
 */
BOOST_AUTO_TEST_CASE( output_is_monotonic_in_input )
{
   std::mt19937_64 rng( 20260824 );

   int checked = 0;
   for( int i = 0; i < 20000; ++i )
   {
      const int bits = 10 + static_cast<int>( rng() % 40 );
      const int64_t x = static_cast<int64_t>( rng() % ( 1ULL << bits ) ) + 1000;
      const int64_t y = static_cast<int64_t>( rng() % ( 1ULL << bits ) ) + 1000;
      const uint64_t amp = 1 + rng() % 100000;

      try
      {
         const fc::uint128_t d = stableswap::compute_d( fc::uint128_t( x ), fc::uint128_t( y ), amp );

         int64_t prev_out = -1;
         for( int step = 0; step < 12; ++step )
         {
            const int64_t dx = ( 1LL << step );
            if( x > GRAPHENE_MAX_SHARE_SUPPLY - dx )
               break;
            const int64_t out = ss_swap_out( y, amp, x + dx, d );
            if( out < 0 )
               break;
            BOOST_REQUIRE_MESSAGE( out >= prev_out,
                                   "output fell as input rose: x=" << x << " y=" << y
                                   << " amp=" << amp << " dx=" << dx
                                   << " out=" << out << " < previous " << prev_out );
            prev_out = out;
         }
         ++checked;
      }
      catch( const fc::exception& )
      {
         continue;
      }
   }
   BOOST_CHECK_GT( checked, 10000 );
}

/**
 * validate() accepts any amplification above zero, so a pool can be created with one far
 * outside the range StableSwap is normally parameterised for. Whatever the solver does with
 * those, it must be a clean rejection and not a hang, a wrap, or a wrong answer -- a pool
 * that cannot be traded is bad, but a pool that prices wrongly is worse.
 */
BOOST_AUTO_TEST_CASE( extreme_amplification_is_handled_cleanly )
{
   const uint64_t amps[] = {
      1, 2, 1000000, 1000000000ULL, 1000000000000ULL,
      uint64_t( 1 ) << 40, uint64_t( 1 ) << 50, uint64_t( 1 ) << 60,
      std::numeric_limits<uint64_t>::max() / 4,
      std::numeric_limits<uint64_t>::max()
   };
   const int64_t balances[][2] = {
      { 1000, 1000 }, { 1000000, 1000000 }, { 1, 1 },
      { GRAPHENE_MAX_SHARE_SUPPLY, GRAPHENE_MAX_SHARE_SUPPLY },
      { GRAPHENE_MAX_SHARE_SUPPLY, 1 }
   };

   int converged = 0, refused = 0;
   for( uint64_t amp : amps )
      for( const auto& b : balances )
      {
         fc::uint128_t d;
         bool ok = true;
         try
         {
            d = stableswap::compute_d( fc::uint128_t( b[0] ), fc::uint128_t( b[1] ), amp );
         }
         catch( const fc::exception& )
         {
            ok = false;                            // refusing is acceptable; wrapping is not
         }
         if( !ok )
         {
            ++refused;
            continue;
         }
         // If it did converge, the answer still has to be in band.
         const boost::multiprecision::uint256_t upper =
               boost::multiprecision::uint256_t( b[0] ) + boost::multiprecision::uint256_t( b[1] );
         BOOST_CHECK_MESSAGE( boost::multiprecision::uint256_t( d ) <= upper + 1,
                              "D exceeded x+y at amp=" << amp
                              << " for x=" << b[0] << " y=" << b[1] );
         ++converged;
      }
   BOOST_TEST_MESSAGE( "extreme_amplification: " << converged << " converged, "
                       << refused << " refused" );
   BOOST_CHECK_GT( converged, 0 );
}

/**
 * Regression: heavily imbalanced pools used to make Newton cycle rather than converge, and
 * compute_d threw "StableSwap D did not converge" for balances that are entirely legal. The
 * iterates in those cycles sit within about one part in 10^10 of each other -- the answer was
 * there, the stop condition just could not see it.
 *
 * Every case below was produced by the fuzzers above and threw before limit-cycle detection
 * was added. They are pinned here by value so the failure cannot come back quietly: the
 * fuzzers would find it again, but only as a random case with no history attached.
 */
BOOST_AUTO_TEST_CASE( imbalanced_pools_that_used_to_cycle_forever )
{
   struct { int64_t x, y; uint64_t amp; } cases[] = {
      {  22081886358008,     6, 145046 },   // cycle length 2
      { 698095958805215,     2, 685479 },   // cycle length 4
      {    276344005464,   130, 903521 },
      {  44459310978557,    21, 329861 },
      {  21069301701155, 93099,  38032 },
      {     45312411853,     6, 868284 },
      {  12906206037633,    30, 955141 },
      {  95887410913181,   184, 211893 },
      {  34322364307006,     3, 976389 },   // cycle length 9
      { 420124979824210,    43, 390570 },   // cycle length 11
      {   3505223116620,     2, 198009 },
      { 120775533911346,   962, 723402 },
      { 256155875671515,    15, 530124 },
   };

   for( const auto& c : cases )
   {
      fc::uint128_t d;
      BOOST_REQUIRE_NO_THROW(
         d = stableswap::compute_d( fc::uint128_t( c.x ), fc::uint128_t( c.y ), c.amp ) );

      const boost::multiprecision::uint256_t prod =
            boost::multiprecision::uint256_t( c.x ) * boost::multiprecision::uint256_t( c.y );
      const boost::multiprecision::uint256_t lower = 2 * boost::multiprecision::sqrt( prod );
      const boost::multiprecision::uint256_t upper =
            boost::multiprecision::uint256_t( c.x ) + boost::multiprecision::uint256_t( c.y );
      const boost::multiprecision::uint256_t d256( d );
      BOOST_REQUIRE_MESSAGE( d256 + 1 >= lower && d256 <= upper + 1,
                             "resolved D out of band for x=" << c.x << " y=" << c.y
                             << " amp=" << c.amp << ": " << d256 );

      // Deterministic: consensus needs every node to pick the same member of the cycle.
      const fc::uint128_t again =
            stableswap::compute_d( fc::uint128_t( c.x ), fc::uint128_t( c.y ), c.amp );
      BOOST_REQUIRE( again == d );
   }
}

/**
 * Pin the evaluator to the header maths, end to end.
 *
 * The existing stableswap_exchange_test asserts only relations -- the stable curve pays more
 * than constant product, D does not fall -- so a change of one unit in the payout slips
 * straight through it. That is not hypothetical: the pool-favouring rounding rule this test
 * exists to pin was introduced precisely because the alternative leaked a unit per round
 * trip, and the relational test could not tell the two rules apart.
 *
 * So compute the expected payout independently, from the header's own solver plus the rule
 * as stated, and require the real exchange operation to match it exactly.
 */
BOOST_FIXTURE_TEST_CASE( evaluator_payout_matches_the_pool_favouring_rule, database_fixture )
{
   generate_blocks( HARDFORK_STABLESWAP_TIME );
   generate_block();
   set_expiration( db, trx );

   ACTORS( (sam)(ted) );

   const int64_t init_amount = 10000000 * GRAPHENE_BLOCKCHAIN_PRECISION;
   fund( sam, asset( init_amount ) );
   fund( ted, asset( init_amount ) );

   const asset_object& usd = create_user_issued_asset( "SXRUSD", sam, 0,
                                   price( asset( 1, asset_id_type( 1 ) ), asset( 1 ) ), 4 );
   const asset_object& eur = create_user_issued_asset( "SXREUR", sam, 0,
                                   price( asset( 1, asset_id_type( 1 ) ), asset( 1 ) ), 4 );
   const asset_object& slp = create_user_issued_asset( "SXRSLP", sam, 0 );

   const asset_id_type a = std::min( usd.get_id(), eur.get_id() );
   const asset_id_type b = std::max( usd.get_id(), eur.get_id() );

   issue_uia( sam, usd.amount( init_amount ) );
   issue_uia( sam, eur.amount( init_amount ) );
   issue_uia( ted, usd.amount( init_amount ) );
   issue_uia( ted, eur.amount( init_amount ) );

   const uint64_t amp = 100;
   const int64_t liq = 1000000;

   const liquidity_pool_object& lpo =
         create_stable_liquidity_pool( sam_id, a, b, slp.get_id(), 0, 0, amp );
   const liquidity_pool_id_type pool = lpo.get_id();
   deposit_to_liquidity_pool( sam_id, pool, asset( liq, a ), asset( liq, b ) );

   // A spread of trade sizes, including the one-unit case where the rounding rule is the
   // entire answer, and sizes that leave the pool progressively more imbalanced.
   const int64_t sells[] = { 1, 2, 7, 100, 4321, 100000, 250000 };

   int exercised = 0;
   for( int64_t sell : sells )
   {
      const share_type bal_a = pool( db ).balance_a;
      const share_type bal_b = pool( db ).balance_b;
      const fc::uint128_t d  = pool( db ).virtual_value;

      // Selling asset a, so a is the in-asset and b is the out-asset.
      const fc::uint128_t new_in( bal_a.value + sell );
      fc::uint128_t expected_out_balance = stableswap::compute_new_y( new_in, d, amp );
      expected_out_balance += 1;                        // the rule under test
      if( expected_out_balance > fc::uint128_t( bal_b.value ) )
         expected_out_balance = fc::uint128_t( bal_b.value );
      const int64_t expected =
            static_cast<int64_t>( fc::uint128_t( bal_b.value ) - expected_out_balance );

      if( expected <= 0 )
         continue;                                      // min_to_receive would reject it

      const generic_exchange_operation_result res =
            exchange_with_liquidity_pool( ted_id, pool, asset( sell, a ), asset( 1, b ) );
      const int64_t got = res.received.front().amount.value;

      BOOST_CHECK_MESSAGE( got == expected,
                           "payout disagrees with the header maths for sell=" << sell
                           << ": evaluator paid " << got << ", rule says " << expected );

      // And the invariant it is all supposed to protect.
      BOOST_CHECK( pool( db ).virtual_value >= d );
      ++exercised;
   }

   // Guard against the whole loop quietly skipping: a test that checks nothing passes too.
   BOOST_CHECK_GE( exercised, 4 );
}


/**
 * A stable pool takes liquidity in any proportion, and charges for the imbalance.
 *
 * This is the property that makes a StableSwap pool one as an LP venue rather than merely one
 * as a pricing curve. The proportional path used for constant-product pools cannot express it:
 * it takes the limiting side and quietly leaves the rest of the deposit with the depositor, so
 * putting in 1,000,000 of one asset and a token amount of the other deposits almost nothing.
 *
 * The fee is what stops it being a free swap. Without it, depositing one side and withdrawing
 * proportionally would trade around the pool's own trading fee -- so the second test here is
 * the one that matters, and it is an economic assertion, not an arithmetic one.
 *
 * Note the one-unit floor on the thin side: liquidity_pool_deposit_operation::validate()
 * requires both amounts positive, and relaxing that would make transactions that every existing
 * node rejects suddenly valid, with no hardfork gate to hide behind. One unit is the smallest
 * amount the operation admits and is indistinguishable from zero for any real deposit.
 */
BOOST_FIXTURE_TEST_CASE( a_stable_pool_accepts_an_imbalanced_deposit, database_fixture )
{
   generate_blocks( HARDFORK_STABLESWAP_TIME );
   generate_block();
   set_expiration( db, trx );

   ACTORS( (sam)(ted) );
   const int64_t huge = 1000000000000LL;
   fund( sam, asset(huge) );
   fund( ted, asset(huge) );

   const asset_object& usd = create_user_issued_asset( "IMBUSD", sam, 0,
                                   price( asset( 1, asset_id_type( 1 ) ), asset( 1 ) ), 4 );
   const asset_object& eur = create_user_issued_asset( "IMBEUR", sam, 0,
                                   price( asset( 1, asset_id_type( 1 ) ), asset( 1 ) ), 4 );
   const asset_object& slp = create_user_issued_asset( "IMBSLP", sam, 0 );
   const asset_object& clp = create_user_issued_asset( "IMBCLP", sam, 0 );

   const asset_id_type a = std::min( usd.get_id(), eur.get_id() );
   const asset_id_type b = std::max( usd.get_id(), eur.get_id() );
   issue_uia( sam, usd.amount( huge ) ); issue_uia( sam, eur.amount( huge ) );
   issue_uia( ted, usd.amount( huge ) ); issue_uia( ted, eur.amount( huge ) );

   const int64_t liq = 1000000;

   // 0.3% trading fee, so the imbalance fee is a real number rather than zero.
   const liquidity_pool_object& s_lpo =
         create_stable_liquidity_pool( sam_id, a, b, slp.get_id(), 30, 0, 100 );
   const auto s_id = s_lpo.get_id();
   deposit_to_liquidity_pool( sam_id, s_id, asset( liq, a ), asset( liq, b ) );

   const liquidity_pool_object& c_lpo = create_liquidity_pool( sam_id, a, b, clp.get_id(), 30, 0 );
   const auto c_id = c_lpo.get_id();
   deposit_to_liquidity_pool( sam_id, c_id, asset( liq, a ), asset( liq, b ) );

   // Heavily one-sided: a lot of A, the smallest admissible amount of B.
   const int64_t lopsided = 100000;

   const auto s_before_a = s_id(db).balance_a;
   const auto s_res = deposit_to_liquidity_pool( ted_id, s_id, asset( lopsided, a ), asset( 1, b ) );
   const int64_t s_shares = s_res.received.front().amount.value;

   // The whole deposit was taken, not the limiting share of it.
   BOOST_CHECK_EQUAL( ( s_id(db).balance_a - s_before_a ).value, lopsided );
   BOOST_CHECK_GT( s_shares, 0 );

   // The constant-product pool takes the limiting side, which for this deposit is almost
   // nothing -- the contrast is the whole point.
   const auto c_before_a = c_id(db).balance_a;
   deposit_to_liquidity_pool( ted_id, c_id, asset( lopsided, a ), asset( 1, b ) );
   BOOST_CHECK_MESSAGE( ( c_id(db).balance_a - c_before_a ).value < lopsided / 100,
                        "the constant-product pool took "
                        << ( c_id(db).balance_a - c_before_a ).value
                        << " of a " << lopsided << " deposit, so there is nothing to contrast" );
}

/// The imbalance fee has to make a round trip cost something. Deposit one-sided, withdraw
/// proportionally, and you must not come out ahead -- otherwise it is a swap that skips the
/// pool's trading fee, which is the whole reason Curve charges for imbalance.
BOOST_FIXTURE_TEST_CASE( an_imbalanced_deposit_is_not_a_free_swap, database_fixture )
{
   generate_blocks( HARDFORK_STABLESWAP_TIME );
   generate_block();
   set_expiration( db, trx );

   ACTORS( (sam)(ted) );
   const int64_t huge = 1000000000000LL;
   fund( sam, asset(huge) );
   fund( ted, asset(huge) );

   const asset_object& usd = create_user_issued_asset( "SWPUSD", sam, 0,
                                   price( asset( 1, asset_id_type( 1 ) ), asset( 1 ) ), 4 );
   const asset_object& eur = create_user_issued_asset( "SWPEUR", sam, 0,
                                   price( asset( 1, asset_id_type( 1 ) ), asset( 1 ) ), 4 );
   const asset_object& slp = create_user_issued_asset( "SWPSLP", sam, 0 );

   const asset_id_type a = std::min( usd.get_id(), eur.get_id() );
   const asset_id_type b = std::max( usd.get_id(), eur.get_id() );
   issue_uia( sam, usd.amount( huge ) ); issue_uia( sam, eur.amount( huge ) );
   issue_uia( ted, usd.amount( huge ) ); issue_uia( ted, eur.amount( huge ) );

   const int64_t liq = 1000000;
   const liquidity_pool_object& lpo =
         create_stable_liquidity_pool( sam_id, a, b, slp.get_id(), 30, 0, 100 );
   const auto pid = lpo.get_id();
   deposit_to_liquidity_pool( sam_id, pid, asset( liq, a ), asset( liq, b ) );

   const auto ted_a_before = get_balance( ted_id, a );
   const auto ted_b_before = get_balance( ted_id, b );

   const int64_t lopsided = 100000;
   const auto res = deposit_to_liquidity_pool( ted_id, pid, asset( lopsided, a ), asset( 1, b ) );
   const auto shares = res.received.front();
   BOOST_REQUIRE_GT( shares.amount.value, 0 );

   withdraw_from_liquidity_pool( ted_id, pid, shares );

   const auto gained_a = get_balance( ted_id, a ) - ted_a_before;
   const auto gained_b = get_balance( ted_id, b ) - ted_b_before;

   // Ted put in `lopsided` of A and 1 of B. Whatever mix he gets back, valuing the two sides
   // one-for-one -- which is what a stable pool asserts they are worth -- he must be down.
   const int64_t net = gained_a + gained_b;
   BOOST_CHECK_MESSAGE( net < 0,
                        "a one-sided deposit and proportional withdrawal netted "
                        << net << ", which is a swap that skipped the trading fee" );
}

BOOST_AUTO_TEST_SUITE_END()
