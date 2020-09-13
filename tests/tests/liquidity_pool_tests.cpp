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

      // Sam is able to delete an empty pool owned by him
      generic_operation_result result = delete_liquidity_pool( sam_id, lpo1.id );
      BOOST_CHECK( !db.find( lp_id1 ) );
      BOOST_CHECK( !lpa1.is_liquidity_pool_share_asset() );
      BOOST_CHECK_EQUAL( result.new_objects.size(), 0u );
      BOOST_REQUIRE_EQUAL( result.updated_objects.size(), 1u );
      BOOST_CHECK( *result.updated_objects.begin() == lpa1.id );
      BOOST_CHECK_EQUAL( result.removed_objects.size(), 1u );
      BOOST_CHECK( *result.removed_objects.begin() == lp_id1 );

      // Other pools are still there
      BOOST_CHECK( db.find( lp_id2 ) );
      BOOST_CHECK( db.find( lp_id3 ) );
     
      // Ted is not able to delete a pool that does not exist
      BOOST_CHECK_THROW( delete_liquidity_pool( ted_id, lp_id1 ), fc::exception );
      // Ted is not able to delete a pool owned by sam
      BOOST_CHECK_THROW( delete_liquidity_pool( ted_id, lp_id2 ), fc::exception );

      generate_block();

   } catch (fc::exception& e) {
      edump((e.to_detail_string()));
      throw;
   }
}

BOOST_AUTO_TEST_SUITE_END()
