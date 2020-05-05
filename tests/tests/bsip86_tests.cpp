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
#include <graphene/chain/asset_object.hpp>
#include <graphene/chain/proposal_object.hpp>

#include <boost/test/unit_test.hpp>

using namespace graphene::chain;
using namespace graphene::chain::test;

BOOST_FIXTURE_TEST_SUITE( bsip86_tests, database_fixture )

BOOST_AUTO_TEST_CASE( hardfork_time_test )
{ try {

   {
      // The network fee percent is 0 by default
      BOOST_CHECK_EQUAL( db.get_global_properties().parameters.get_market_fee_network_percent(), 0 );

      // Try to set new committee parameter before hardfork
      proposal_create_operation cop = proposal_create_operation::committee_proposal(
            db.get_global_properties().parameters, db.head_block_time() );
      cop.fee_paying_account = GRAPHENE_TEMP_ACCOUNT;
      cop.expiration_time = db.head_block_time() + *cop.review_period_seconds + 10;
      committee_member_update_global_parameters_operation cmuop;
      cmuop.new_parameters.extensions.value.market_fee_network_percent = 1;
      cop.proposed_ops.emplace_back( cmuop );
      trx.operations.push_back( cop );

      // It should fail
      GRAPHENE_CHECK_THROW( PUSH_TX(db, trx, ~0), fc::exception );
      trx.clear();

      // The percent should still be 0
      BOOST_CHECK_EQUAL( db.get_global_properties().parameters.get_market_fee_network_percent(), 0 );
   }

   // Pass the hardfork
   generate_blocks( HARDFORK_BSIP_86_TIME );
   set_expiration( db, trx );

   {
      // The network fee percent is still 0
      BOOST_CHECK_EQUAL( db.get_global_properties().parameters.get_market_fee_network_percent(), 0 );

      // Try to set new committee parameter after hardfork
      proposal_create_operation cop = proposal_create_operation::committee_proposal(
            db.get_global_properties().parameters, db.head_block_time() );
      cop.fee_paying_account = GRAPHENE_TEMP_ACCOUNT;
      cop.expiration_time = db.head_block_time() + *cop.review_period_seconds + 10;
      committee_member_update_global_parameters_operation cmuop;
      cmuop.new_parameters.extensions.value.market_fee_network_percent = 3001; // 30.01%
      cop.proposed_ops.emplace_back(cmuop);
      trx.operations.push_back(cop);

      // Should fail since the value is too big
      GRAPHENE_CHECK_THROW( PUSH_TX(db, trx, ~0), fc::exception );
      // The network fee percent is still 0
      BOOST_CHECK_EQUAL( db.get_global_properties().parameters.get_market_fee_network_percent(), 0 );

      trx.operations.clear();
      cop.proposed_ops.clear();
      cmuop.new_parameters.extensions.value.market_fee_network_percent = 1123; // 11.23%
      cop.proposed_ops.emplace_back(cmuop);
      trx.operations.push_back(cop);

      // Should succeed
      processed_transaction ptx = PUSH_TX(db, trx, ~0);
      trx.operations.clear();
      proposal_id_type prop_id = ptx.operation_results[0].get<object_id_type>();

      // The network fee percent is still 0
      BOOST_CHECK_EQUAL( db.get_global_properties().parameters.get_market_fee_network_percent(), 0 );

      // Approve the proposal
      proposal_update_operation uop;
      uop.fee_paying_account = GRAPHENE_TEMP_ACCOUNT;
      uop.active_approvals_to_add = { get_account("init0").get_id(), get_account("init1").get_id(),
                                      get_account("init2").get_id(), get_account("init3").get_id(),
                                      get_account("init4").get_id(), get_account("init5").get_id(),
                                      get_account("init6").get_id(), get_account("init7").get_id() };
      trx.operations.push_back(uop);
      PUSH_TX(db, trx, ~0);

      // The network fee percent is still 0
      BOOST_CHECK_EQUAL( db.get_global_properties().parameters.get_market_fee_network_percent(), 0 );

      generate_blocks( prop_id( db ).expiration_time + 5 );
      generate_blocks( db.get_dynamic_global_properties().next_maintenance_time );
      generate_block();

      // The network fee percent should have changed
      BOOST_CHECK_EQUAL( db.get_global_properties().parameters.get_market_fee_network_percent(), 1123 );

   }
} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE( fee_sharing_test )
{ try {
   ACTORS((alice)(bob));

   uint16_t market_fee_percent = 100; // 1%
   price cer(asset(1, asset_id_type(1)), asset(1));

   const asset_object& alicecoin = create_user_issued_asset( "ALICECOIN", alice_id(db), charge_market_fee,
                                                             cer, 4, market_fee_percent );
   const asset_object& aliceusd  = create_user_issued_asset( "ALICEUSD",  alice_id(db), 0 );

   asset_id_type alicecoin_id = alicecoin.id;
   asset_id_type aliceusd_id = aliceusd.id;

   // prepare users' balance
   issue_uia( alice, aliceusd.amount( 20000000 ) );
   issue_uia( bob, alicecoin.amount( 10000000 ) );
   transfer( account_id_type(), alice_id, asset(10000000) );
   transfer( account_id_type(), bob_id, asset(10000000) );

   // match and fill orders
   create_sell_order( alice_id, aliceusd_id(db).amount(200000), alicecoin_id(db).amount(100000) );
   create_sell_order( bob_id, alicecoin_id(db).amount(100000), aliceusd_id(db).amount(200000) );

   // check fee sharing
   BOOST_CHECK_EQUAL( get_market_fee_reward( account_id_type(), alicecoin_id ), 0 );
   BOOST_CHECK_EQUAL( get_market_fee_reward( account_id_type(), aliceusd_id ), 0 );

   // check issuer fees
   BOOST_CHECK_EQUAL( alicecoin_id(db).dynamic_data(db).accumulated_fees.value, 1000 );
   BOOST_CHECK_EQUAL( aliceusd_id(db).dynamic_data(db).accumulated_fees.value, 0 );

   // pass the hard fork
   INVOKE( hardfork_time_test );
   set_expiration( db, trx );

   // match and fill orders again
   create_sell_order( alice_id, aliceusd_id(db).amount(200000), alicecoin_id(db).amount(100000) );
   create_sell_order( bob_id, alicecoin_id(db).amount(100000), aliceusd_id(db).amount(200000) );

   // check fee sharing
   BOOST_CHECK_EQUAL( get_market_fee_reward( account_id_type(), alicecoin_id ), 112 ); // 1000*11.23%
   BOOST_CHECK_EQUAL( get_market_fee_reward( account_id_type(), aliceusd_id ), 0 );

   // check issuer fees
   BOOST_CHECK_EQUAL( alicecoin_id(db).dynamic_data(db).accumulated_fees.value, 1888 ); // 1000+1000-112
   BOOST_CHECK_EQUAL( aliceusd_id(db).dynamic_data(db).accumulated_fees.value, 0 );

} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_SUITE_END()
