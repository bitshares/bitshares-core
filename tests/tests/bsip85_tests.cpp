/*
 * Copyright (c) 2020 contributors.
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
#include <graphene/chain/market_object.hpp>
#include <graphene/chain/proposal_object.hpp>

#include <boost/test/unit_test.hpp>

using namespace graphene::chain;
using namespace graphene::chain::test;

BOOST_FIXTURE_TEST_SUITE( bsip85_tests, database_fixture )

BOOST_AUTO_TEST_CASE( hardfork_time_test )
{ try {

   {
      // The maker fee discount percent is 0 by default
      BOOST_CHECK_EQUAL( db.get_global_properties().parameters.get_maker_fee_discount_percent(), 0 );

      // Try to set new committee parameter before hardfork
      proposal_create_operation cop = proposal_create_operation::committee_proposal(
            db.get_global_properties().parameters, db.head_block_time() );
      cop.fee_paying_account = GRAPHENE_TEMP_ACCOUNT;
      cop.expiration_time = db.head_block_time() + *cop.review_period_seconds + 10;
      committee_member_update_global_parameters_operation cmuop;
      cmuop.new_parameters.extensions.value.maker_fee_discount_percent = 1;
      cop.proposed_ops.emplace_back( cmuop );
      trx.operations.push_back( cop );

      // It should fail
      GRAPHENE_CHECK_THROW( PUSH_TX(db, trx, ~0), fc::exception );
      trx.clear();

      // The percent should still be 0
      BOOST_CHECK_EQUAL( db.get_global_properties().parameters.get_maker_fee_discount_percent(), 0 );
   }

   // Pass the hardfork
   generate_blocks( HARDFORK_BSIP_85_TIME );
   set_expiration( db, trx );

   {
      // The maker fee discount percent is still 0
      BOOST_CHECK_EQUAL( db.get_global_properties().parameters.get_maker_fee_discount_percent(), 0 );

      // Try to set new committee parameter after hardfork
      proposal_create_operation cop = proposal_create_operation::committee_proposal(
            db.get_global_properties().parameters, db.head_block_time() );
      cop.fee_paying_account = GRAPHENE_TEMP_ACCOUNT;
      cop.expiration_time = db.head_block_time() + *cop.review_period_seconds + 10;
      committee_member_update_global_parameters_operation cmuop;
      cmuop.new_parameters.extensions.value.maker_fee_discount_percent = 10001; // 100.01%
      cop.proposed_ops.emplace_back(cmuop);
      trx.operations.push_back(cop);

      // Should fail since the value is too big
      GRAPHENE_CHECK_THROW( PUSH_TX(db, trx, ~0), fc::exception );
      // The maker fee discount percent is still 0
      BOOST_CHECK_EQUAL( db.get_global_properties().parameters.get_maker_fee_discount_percent(), 0 );

      trx.operations.clear();
      cop.proposed_ops.clear();
      cmuop.new_parameters.extensions.value.maker_fee_discount_percent = 1123; // 11.23%
      cop.proposed_ops.emplace_back(cmuop);
      trx.operations.push_back(cop);

      // Should succeed
      processed_transaction ptx = PUSH_TX(db, trx, ~0);
      trx.operations.clear();
      proposal_id_type prop_id = ptx.operation_results[0].get<object_id_type>();

      // The maker fee discount percent is still 0
      BOOST_CHECK_EQUAL( db.get_global_properties().parameters.get_maker_fee_discount_percent(), 0 );

      // Approve the proposal
      proposal_update_operation uop;
      uop.fee_paying_account = GRAPHENE_TEMP_ACCOUNT;
      uop.active_approvals_to_add = { get_account("init0").get_id(), get_account("init1").get_id(),
                                      get_account("init2").get_id(), get_account("init3").get_id(),
                                      get_account("init4").get_id(), get_account("init5").get_id(),
                                      get_account("init6").get_id(), get_account("init7").get_id() };
      trx.operations.push_back(uop);
      PUSH_TX(db, trx, ~0);

      // The maker fee discount percent is still 0
      BOOST_CHECK_EQUAL( db.get_global_properties().parameters.get_maker_fee_discount_percent(), 0 );

      generate_blocks( prop_id( db ).expiration_time + 5 );
      generate_blocks( db.get_dynamic_global_properties().next_maintenance_time );
      generate_block();

      // The maker fee discount percent should have changed
      BOOST_CHECK_EQUAL( db.get_global_properties().parameters.get_maker_fee_discount_percent(), 1123 );

   }
} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE( bsip85_maker_fee_discount_test )
{
   try
   {
      ACTORS((alice)(bob)(izzy));

      int64_t alice_b0 = 1000000, bob_b0 = 1000000;
      int64_t pool_0 = 1000000, accum_0 = 0;

      transfer( account_id_type(), alice_id, asset(alice_b0) );
      transfer( account_id_type(), bob_id, asset(bob_b0) );

      asset_id_type core_id = asset_id_type();
      int64_t cer_core_amount = 1801;
      int64_t cer_usd_amount = 31;
      price tmp_cer( asset( cer_core_amount ), asset( cer_usd_amount, asset_id_type(1) ) );
      const auto& usd_obj = create_user_issued_asset( "IZZYUSD", izzy_id(db), charge_market_fee, tmp_cer );
      asset_id_type usd_id = usd_obj.id;
      issue_uia( alice_id, asset( alice_b0, usd_id ) );
      issue_uia( bob_id, asset( bob_b0, usd_id ) );

      fund_fee_pool( committee_account( db ), usd_obj, pool_0 );

      // If pay fee in CORE
      int64_t order_create_fee = 547;
      int64_t order_maker_refund = 61; // 547 * 11.23% = 61.4281

      // If pay fee in USD
      int64_t usd_create_fee = order_create_fee * cer_usd_amount / cer_core_amount;
      if( usd_create_fee * cer_core_amount != order_create_fee * cer_usd_amount ) usd_create_fee += 1;
      int64_t usd_maker_refund = usd_create_fee * 1123 / 10000;
      // amount paid by fee pool
      int64_t core_create_fee = usd_create_fee * cer_core_amount / cer_usd_amount;
      int64_t core_maker_refund = usd_maker_refund == 0 ? 0 : core_create_fee * 1123 / 10000;

      fee_parameters::flat_set_type new_fees;
      limit_order_create_operation::fee_parameters_type create_fee_params;
      create_fee_params.fee = order_create_fee;
      new_fees.insert( create_fee_params );

      // Pass BSIP 85 HF time
      // Note: no test case for the behavior before the HF since it's covered by other test cases
      INVOKE( hardfork_time_test );
      set_expiration( db, trx );

      // enable_fees() and change_fees() modifies DB directly, and results will be overwritten by block generation
      // so we have to do it every time we stop generating/popping blocks and start doing tx's
      enable_fees();
      change_fees( new_fees );

      {
         // prepare params
         time_point_sec max_exp = time_point_sec::maximum();
         price cer = usd_id( db ).options.core_exchange_rate;
         const auto* usd_stat = &usd_id( db ).dynamic_asset_data_id( db );

         // balance data
         int64_t alice_bc = alice_b0, bob_bc = bob_b0; // core balance
         int64_t alice_bu = alice_b0, bob_bu = bob_b0; // usd balance
         int64_t pool_b = pool_0, accum_b = accum_0;

         // Check order fill
         BOOST_TEST_MESSAGE( "Creating ao1, then be filled by bo1" );
         // pays fee in core
         const limit_order_object* ao1 = create_sell_order( alice_id, asset(1000), asset(200, usd_id) );
         const limit_order_id_type ao1id = ao1->id;
         // pays fee in usd
         const limit_order_object* bo1 = create_sell_order(   bob_id, asset(200, usd_id), asset(1000), max_exp, cer );

         BOOST_CHECK( db.find<limit_order_object>( ao1id ) == nullptr );
         BOOST_CHECK( bo1 == nullptr );

         // data after order created
         alice_bc -= 1000; // amount for sale
         alice_bc -= order_create_fee; // fee
         bob_bu -= 200; // amount for sale
         bob_bu -= usd_create_fee; // fee
         pool_b -= core_create_fee; // fee pool
         accum_b += 0;

         // data after order filled
         alice_bu += 200; // bob pays
         alice_bc += order_maker_refund; // maker fee refund
         bob_bc += 1000; // alice pays
         accum_b += usd_create_fee; // bo1 paid fee, was taker, no refund
         pool_b += 0; // no change

         BOOST_CHECK_EQUAL( get_balance( alice_id, core_id ), alice_bc );
         BOOST_CHECK_EQUAL( get_balance( alice_id,  usd_id ), alice_bu );
         BOOST_CHECK_EQUAL( get_balance(   bob_id, core_id ), bob_bc );
         BOOST_CHECK_EQUAL( get_balance(   bob_id,  usd_id ), bob_bu );
         BOOST_CHECK_EQUAL( usd_stat->fee_pool.value,          pool_b );
         BOOST_CHECK_EQUAL( usd_stat->accumulated_fees.value,  accum_b );

         // Check partial fill
         BOOST_TEST_MESSAGE( "Creating ao2, then be partially filled by bo2" );
         // pays fee in usd
         const limit_order_object* ao2 = create_sell_order( alice_id, asset(1000), asset(200, usd_id), max_exp, cer );
         const limit_order_id_type ao2id = ao2->id;
         // pays fee in core
         const limit_order_object* bo2 = create_sell_order(   bob_id, asset(100, usd_id), asset(500) );

         BOOST_CHECK( db.find<limit_order_object>( ao2id ) != nullptr );
         BOOST_CHECK( bo2 == nullptr );

         // data after order created
         alice_bc -= 1000; // amount to sell
         alice_bu -= usd_create_fee; // fee
         pool_b -= core_create_fee; // fee pool
         accum_b += 0;
         bob_bc -= order_create_fee; // fee
         bob_bu -= 100; // amount to sell

         // data after order filled
         alice_bu += 100; // bob pays
         alice_bu += usd_maker_refund; // maker fee refund
         bob_bc += 500;
         accum_b += usd_create_fee - usd_maker_refund; // ao2 paid fee deduct maker refund
         pool_b += core_maker_refund; // ao2 maker refund

         BOOST_CHECK_EQUAL( get_balance( alice_id, core_id ), alice_bc );
         BOOST_CHECK_EQUAL( get_balance( alice_id,  usd_id ), alice_bu );
         BOOST_CHECK_EQUAL( get_balance(   bob_id, core_id ), bob_bc );
         BOOST_CHECK_EQUAL( get_balance(   bob_id,  usd_id ), bob_bu );
         BOOST_CHECK_EQUAL( usd_stat->fee_pool.value,          pool_b );
         BOOST_CHECK_EQUAL( usd_stat->accumulated_fees.value,  accum_b );

      }
   }
   FC_LOG_AND_RETHROW()
}

BOOST_AUTO_TEST_SUITE_END()

