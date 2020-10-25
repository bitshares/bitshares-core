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

#include <boost/test/unit_test.hpp>

using namespace graphene::chain;
using namespace graphene::chain::test;

BOOST_FIXTURE_TEST_SUITE( liquidity_pool_tests, database_fixture )

BOOST_AUTO_TEST_CASE( hardfork_time_test )
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
      BOOST_CHECK_THROW( create_liquidity_pool( sam_id, core.id, usd.id, lpa.id, 0, 0 ), fc::exception );

      liquidity_pool_id_type tmp_lp_id;
      BOOST_CHECK_THROW( delete_liquidity_pool( sam_id, tmp_lp_id ), fc::exception );
      BOOST_CHECK_THROW( deposit_to_liquidity_pool( sam_id, tmp_lp_id, core.amount(100), usd.amount(100) ),
                         fc::exception );
      BOOST_CHECK_THROW( withdraw_from_liquidity_pool( sam_id, tmp_lp_id, lpa.amount(100) ),
                         fc::exception );
      BOOST_CHECK_THROW( exchange_with_liquidity_pool( sam_id, tmp_lp_id, core.amount(100), usd.amount(100) ),
                         fc::exception );

      liquidity_pool_create_operation cop =
                         make_liquidity_pool_create_op( sam_id, core.id, usd.id, lpa.id, 0, 0 );
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

BOOST_AUTO_TEST_CASE( create_delete_proposal_test )
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
                         make_liquidity_pool_create_op( sam_id, core.id, usd.id, lpa.id, 0, 0 );
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
      const liquidity_pool_object& lpo1 = create_liquidity_pool( sam_id, core.id, usd.id, lpa1.id, 0, 0 );
      BOOST_CHECK( lpo1.asset_a == core.id );
      BOOST_CHECK( lpo1.asset_b == usd.id );
      BOOST_CHECK( lpo1.balance_a == 0 );
      BOOST_CHECK( lpo1.balance_b == 0 );
      BOOST_CHECK( lpo1.share_asset == lpa1.id );
      BOOST_CHECK( lpo1.taker_fee_percent == 0 );
      BOOST_CHECK( lpo1.withdrawal_fee_percent == 0 );
      BOOST_CHECK( lpo1.virtual_value == 0 );

      liquidity_pool_id_type lp_id1 = lpo1.id;
      BOOST_CHECK( lpa1.is_liquidity_pool_share_asset() );
      BOOST_CHECK( *lpa1.for_liquidity_pool == lp_id1 );

      const liquidity_pool_object& lpo2 = create_liquidity_pool( sam_id, core.id, usd.id, lpa2.id, 200, 300 );
      BOOST_CHECK( lpo2.asset_a == core.id );
      BOOST_CHECK( lpo2.asset_b == usd.id );
      BOOST_CHECK( lpo2.balance_a == 0 );
      BOOST_CHECK( lpo2.balance_b == 0 );
      BOOST_CHECK( lpo2.share_asset == lpa2.id );
      BOOST_CHECK( lpo2.taker_fee_percent == 200 );
      BOOST_CHECK( lpo2.withdrawal_fee_percent == 300 );
      BOOST_CHECK( lpo2.virtual_value == 0 );

      liquidity_pool_id_type lp_id2 = lpo2.id;
      BOOST_CHECK( lpa2.is_liquidity_pool_share_asset() );
      BOOST_CHECK( *lpa2.for_liquidity_pool == lp_id2 );

      const liquidity_pool_object& lpo3 = create_liquidity_pool( sam_id, usd.id, mpa.id, lpa3.id, 50, 50 );

      BOOST_CHECK( lpo3.asset_a == usd.id );
      BOOST_CHECK( lpo3.asset_b == mpa.id );
      BOOST_CHECK( lpo3.balance_a == 0 );
      BOOST_CHECK( lpo3.balance_b == 0 );
      BOOST_CHECK( lpo3.share_asset == lpa3.id );
      BOOST_CHECK( lpo3.taker_fee_percent == 50 );
      BOOST_CHECK( lpo3.withdrawal_fee_percent == 50 );
      BOOST_CHECK( lpo3.virtual_value == 0 );

      liquidity_pool_id_type lp_id3 = lpo3.id;
      BOOST_CHECK( lpa3.is_liquidity_pool_share_asset() );
      BOOST_CHECK( *lpa3.for_liquidity_pool == lp_id3 );

      // Unable to create a liquidity pool with invalid data
      // the same assets in pool
      BOOST_CHECK_THROW( create_liquidity_pool( sam_id, core.id, core.id, lpa.id, 0, 0 ), fc::exception );
      BOOST_CHECK_THROW( create_liquidity_pool( sam_id, usd.id, usd.id, lpa.id, 0, 0 ), fc::exception );
      // ID of the first asset is greater
      BOOST_CHECK_THROW( create_liquidity_pool( sam_id, usd.id, core.id, lpa.id, 0, 0 ), fc::exception );
      // the share asset is one of the assets in pool
      BOOST_CHECK_THROW( create_liquidity_pool( sam_id, usd.id, lpa.id, lpa.id, 0, 0 ), fc::exception );
      BOOST_CHECK_THROW( create_liquidity_pool( sam_id, lpa.id, pm.id, lpa.id, 0, 0 ), fc::exception );
      // percentage too big
      BOOST_CHECK_THROW( create_liquidity_pool( sam_id, core.id, usd.id, lpa.id, 10001, 0 ), fc::exception );
      BOOST_CHECK_THROW( create_liquidity_pool( sam_id, core.id, usd.id, lpa.id, 0, 10001 ), fc::exception );
      BOOST_CHECK_THROW( create_liquidity_pool( sam_id, core.id, usd.id, lpa.id, 10001, 10001 ), fc::exception );
      // asset does not exist
      BOOST_CHECK_THROW( create_liquidity_pool( sam_id, core.id, usd.id, no_asset_id1, 0, 0 ), fc::exception );
      BOOST_CHECK_THROW( create_liquidity_pool( sam_id, core.id, no_asset_id1, lpa.id, 0, 0 ), fc::exception );
      BOOST_CHECK_THROW( create_liquidity_pool( sam_id, no_asset_id1, no_asset_id2, lpa.id, 0, 0 ), fc::exception );
      // the account does not own the share asset
      BOOST_CHECK_THROW( create_liquidity_pool( sam_id, core.id, usd.id, ted_lpa.id, 0, 0 ), fc::exception );
      // the share asset is a MPA or a PM
      BOOST_CHECK_THROW( create_liquidity_pool( sam_id, core.id, usd.id, mpa.id, 0, 0 ), fc::exception );
      BOOST_CHECK_THROW( create_liquidity_pool( sam_id, core.id, usd.id, pm.id, 0, 0 ), fc::exception );
      // the share asset is already bound to a liquidity pool
      BOOST_CHECK_THROW( create_liquidity_pool( sam_id, core.id, usd.id, lpa1.id, 0, 0 ), fc::exception );
      // current supply of the share asset is not zero
      BOOST_CHECK_THROW( create_liquidity_pool( sam_id, core.id, lpa.id, usd.id, 0, 0 ), fc::exception );

      // Unable to issue a liquidity pool share asset
      BOOST_CHECK_THROW( issue_uia( sam, lpa1.amount(1) ), fc::exception );

      // Sam is able to delete an empty pool owned by him
      generic_operation_result result = delete_liquidity_pool( sam_id, lpo1.id );
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

BOOST_AUTO_TEST_CASE( deposit_withdrawal_test )
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
      asset_id_type eur_id = eur.id;
      asset_id_type usd_id = usd.id;
      asset_id_type lpa_id = lpa.id;

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
      const liquidity_pool_object& lpo = create_liquidity_pool( sam_id, eur.id, usd.id, lpa.id, 200, 300 );
      liquidity_pool_id_type lp_id = lpo.id;

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

   } catch (fc::exception& e) {
      edump((e.to_detail_string()));
      throw;
   }
}

BOOST_AUTO_TEST_CASE( exchange_test )
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
      asset_id_type eur_id = eur.id;
      asset_id_type usd_id = usd.id;
      asset_id_type lpa_id = lpa.id;

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
      const liquidity_pool_object& lpo = create_liquidity_pool( sam_id, eur.id, usd.id, lpa.id, 200, 300 );
      liquidity_pool_id_type lp_id = lpo.id;

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

      // Ted exchanges with the pool
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
      BOOST_CHECK_EQUAL( lpo.balance_a.value, expected_pool_balance_a);
      BOOST_CHECK_EQUAL( lpo.balance_b.value, expected_pool_balance_b);
      BOOST_CHECK( lpo.virtual_value == fc::uint128_t(expected_pool_balance_a) * expected_pool_balance_b );
      BOOST_CHECK_EQUAL( lpa.dynamic_data(db).current_supply.value, expected_lp_supply );

      expected_accumulated_fees_eur += maker_fee;
      expected_accumulated_fees_usd += taker_fee;
      BOOST_CHECK_EQUAL( eur.dynamic_data(db).accumulated_fees.value, expected_accumulated_fees_eur );
      BOOST_CHECK_EQUAL( usd.dynamic_data(db).accumulated_fees.value, expected_accumulated_fees_usd );

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
      BOOST_CHECK_EQUAL( lpo.balance_a.value, expected_pool_balance_a);
      BOOST_CHECK_EQUAL( lpo.balance_b.value, expected_pool_balance_b);
      BOOST_CHECK( lpo.virtual_value == fc::uint128_t(expected_pool_balance_a) * expected_pool_balance_b );
      BOOST_CHECK_EQUAL( lpa.dynamic_data(db).current_supply.value, expected_lp_supply );

      expected_accumulated_fees_eur += taker_fee;
      expected_accumulated_fees_usd += maker_fee;
      BOOST_CHECK_EQUAL( eur.dynamic_data(db).accumulated_fees.value, expected_accumulated_fees_eur );
      BOOST_CHECK_EQUAL( usd.dynamic_data(db).accumulated_fees.value, expected_accumulated_fees_usd );

      expected_balance_ted_eur += ted_receives;
      expected_balance_ted_usd -= 1000;
      check_balances();

      // Generates a block
      generate_block();

   } catch (fc::exception& e) {
      edump((e.to_detail_string()));
      throw;
   }
}

BOOST_AUTO_TEST_SUITE_END()
