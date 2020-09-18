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
#include <graphene/chain/ticket_object.hpp>
#include <graphene/chain/proposal_object.hpp>

#include <boost/test/unit_test.hpp>

using namespace graphene::chain;
using namespace graphene::chain::test;

BOOST_FIXTURE_TEST_SUITE( pob_tests, database_fixture )

BOOST_AUTO_TEST_CASE( hardfork_time_test )
{
   try {

      // Proceeds to a recent hard fork
      generate_blocks( HARDFORK_CORE_1270_TIME );
      generate_block();
      set_expiration( db, trx );

      ACTORS((sam));

      auto init_amount = 10000000 * GRAPHENE_BLOCKCHAIN_PRECISION;
      fund( sam, asset(init_amount) );

      // Before the hard fork, unable to create a ticket or update a ticket, or do any of them with proposals
      BOOST_CHECK_THROW( create_ticket( sam_id, lock_180_days, asset(1) ), fc::exception );
      ticket_object tmp_ticket;
      tmp_ticket.account = sam_id;
      BOOST_CHECK_THROW( update_ticket( tmp_ticket, lock_360_days, asset(1) ), fc::exception );

      ticket_create_operation cop = make_ticket_create_op( sam_id, lock_720_days, asset(2) );
      BOOST_CHECK_THROW( propose( cop ), fc::exception );

      ticket_update_operation uop = make_ticket_update_op( tmp_ticket, lock_720_days, {} );
      BOOST_CHECK_THROW( propose( uop ), fc::exception );

   } catch (fc::exception& e) {
      edump((e.to_detail_string()));
      throw;
   }
}

BOOST_AUTO_TEST_CASE( validation_and_basic_logic_test )
{ try {

      // Pass the hard fork time
      generate_blocks( HARDFORK_CORE_2103_TIME );
      set_expiration( db, trx );

      ACTORS((sam)(ted));

      auto fee_amount = 50 * GRAPHENE_BLOCKCHAIN_PRECISION;

      auto init_amount = 10000000 * GRAPHENE_BLOCKCHAIN_PRECISION;
      fund( sam, asset(init_amount) );
      fund( ted, asset(init_amount) );

      const asset_object& usd = create_user_issued_asset( "MYUSD" );
      issue_uia( sam, usd.amount(init_amount) );
      issue_uia( ted, usd.amount(init_amount) );

      // Able to propose
      {
         ticket_create_operation cop = make_ticket_create_op( sam_id, lock_720_days, asset(2) );
         propose( cop );

         ticket_object tmp_ticket;
         tmp_ticket.account = sam_id;
         ticket_update_operation uop = make_ticket_update_op( tmp_ticket, lock_720_days, {} );
         propose( uop );
      }

      // Unable to create a ticket with invalid data
      // zero amount
      BOOST_CHECK_THROW( create_ticket( sam_id, lock_180_days, asset(0) ), fc::exception );
      // negative amount
      BOOST_CHECK_THROW( create_ticket( sam_id, lock_180_days, asset(-1) ), fc::exception );
      // non-core asset
      BOOST_CHECK_THROW( create_ticket( sam_id, lock_180_days, usd.amount(1) ), fc::exception );
      // target type liquid
      BOOST_CHECK_THROW( create_ticket( sam_id, liquid, asset(1) ), fc::exception );
      // target type too big
      BOOST_CHECK_THROW( create_ticket( sam_id, TICKET_TYPE_COUNT, asset(1) ), fc::exception );
      // target type too big
      {
         ticket_create_operation cop = make_ticket_create_op( sam_id, lock_180_days, asset(1) );
         cop.target_type = static_cast<uint8_t>(TICKET_TYPE_COUNT) + 1;
         trx.operations.clear();
         trx.operations.push_back( cop );

         for( auto& o : trx.operations ) db.current_fee_schedule().set_fee(o);
         set_expiration( db, trx );
         BOOST_CHECK_THROW( PUSH_TX(db, trx, ~0), fc::exception );
      }

      // enable and update fee schedule
      enable_fees();
      db.modify(global_property_id_type()(db), [](global_property_object& gpo)
      {
         auto& fee_params = gpo.parameters.get_mutable_fees().parameters;

         auto itr = fee_params.find( ticket_create_operation::fee_parameters_type() );
         itr->get<ticket_create_operation::fee_parameters_type>().fee = 1;

         itr = fee_params.find( ticket_update_operation::fee_parameters_type() );
         itr->get<ticket_update_operation::fee_parameters_type>().fee = 2;
      });

      int64_t expected_balance = init_amount;

      // Able to create a ticket with valid data
      const ticket_object& tick_1 = create_ticket( sam_id, lock_180_days, asset(1) );
      BOOST_CHECK( tick_1.account == sam_id );
      BOOST_CHECK( tick_1.target_type == lock_180_days );
      BOOST_CHECK( tick_1.amount == asset(1) );
      expected_balance -= (1 + fee_amount);
      BOOST_CHECK_EQUAL( db.get_balance( sam_id, asset_id_type() ).amount.value, expected_balance );

      const ticket_object& tick_2 = create_ticket( sam_id, lock_360_days, asset(1000) );
      BOOST_CHECK( tick_2.account == sam_id );
      BOOST_CHECK( tick_2.target_type == lock_360_days );
      BOOST_CHECK( tick_2.amount == asset(1000) );
      expected_balance -= (1000 + fee_amount);
      BOOST_CHECK_EQUAL( db.get_balance( sam_id, asset_id_type() ).amount.value, expected_balance );

      const ticket_object& tick_3 = create_ticket( sam_id, lock_720_days, asset(10) );
      BOOST_CHECK( tick_3.account == sam_id );
      BOOST_CHECK( tick_3.target_type == lock_720_days );
      BOOST_CHECK( tick_3.amount == asset(10) );
      expected_balance -= (10 + fee_amount);
      BOOST_CHECK_EQUAL( db.get_balance( sam_id, asset_id_type() ).amount.value, expected_balance );

      const ticket_object& tick_4 = create_ticket( sam_id, lock_forever, asset(100000) );
      BOOST_CHECK( tick_4.account == sam_id );
      BOOST_CHECK( tick_4.target_type == lock_forever );
      BOOST_CHECK( tick_4.amount == asset(100000) );
      expected_balance -= (100000 + fee_amount);
      BOOST_CHECK_EQUAL( db.get_balance( sam_id, asset_id_type() ).amount.value, expected_balance );

      // Unable to update a ticket with invalid data
      // zero amount
      BOOST_CHECK_THROW( update_ticket( tick_1, liquid, asset(0) ), fc::exception );
      // negative amount
      BOOST_CHECK_THROW( update_ticket( tick_1, liquid, asset(-1) ), fc::exception );
      // non-core asset
      BOOST_CHECK_THROW( update_ticket( tick_1, liquid, asset(1, usd.id) ), fc::exception );
      // too big amount
      BOOST_CHECK_THROW( update_ticket( tick_1, liquid, asset(2) ), fc::exception );
      // target type unchanged
      BOOST_CHECK_THROW( update_ticket( tick_1, lock_180_days, {} ), fc::exception );
      // target type unchanged
      BOOST_CHECK_THROW( update_ticket( tick_1, lock_180_days, asset(1) ), fc::exception );
      // target type too big
      BOOST_CHECK_THROW( update_ticket( tick_1, TICKET_TYPE_COUNT, {} ), fc::exception );
      {
         // target type too big
         ticket_update_operation uop = make_ticket_update_op( tick_1, liquid, asset(1) );
         uop.target_type = static_cast<uint8_t>(TICKET_TYPE_COUNT) + 1;
         trx.operations.clear();
         trx.operations.push_back( uop );

         for( auto& o : trx.operations ) db.current_fee_schedule().set_fee(o);
         set_expiration( db, trx );
         BOOST_CHECK_THROW( PUSH_TX(db, trx, ~0), fc::exception );

         // account mismatch
         uop.target_type = liquid;
         uop.account = ted_id;
         trx.operations.clear();
         trx.operations.push_back( uop );
         BOOST_CHECK_THROW( PUSH_TX(db, trx, ~0), fc::exception );
      }

      ticket_id_type tick_1_id = tick_1.id;
      ticket_id_type tick_2_id = tick_2.id;
      ticket_id_type tick_4_id = tick_4.id;

      // Update ticket 1 to liquid
      generic_operation_result result = update_ticket( tick_1, liquid, asset(1) );
      BOOST_REQUIRE( db.find( tick_1_id ) );
      BOOST_CHECK( tick_1_id(db).account == sam_id );
      BOOST_CHECK( tick_1_id(db).target_type == liquid );
      BOOST_CHECK( tick_1_id(db).amount == asset(1) );
      expected_balance -= fee_amount;
      BOOST_CHECK_EQUAL( db.get_balance( sam_id, asset_id_type() ).amount.value, expected_balance );
      BOOST_CHECK_EQUAL( result.new_objects.size(), 0u );
      BOOST_REQUIRE_EQUAL( result.updated_objects.size(), 1u );
      BOOST_CHECK( *result.updated_objects.begin() == tick_1_id );
      BOOST_CHECK_EQUAL( result.removed_objects.size(), 0u );

      // target type unchanged
      BOOST_CHECK_THROW( update_ticket( tick_1, liquid, {} ), fc::exception );

      // Update ticket 1 to lock_forever
      result = update_ticket( tick_1, lock_forever, {} );
      BOOST_REQUIRE( db.find( tick_1_id ) );
      BOOST_CHECK( tick_1_id(db).account == sam_id );
      BOOST_CHECK( tick_1_id(db).target_type == lock_forever );
      BOOST_CHECK( tick_1_id(db).amount == asset(1) );
      expected_balance -= fee_amount;
      BOOST_CHECK_EQUAL( db.get_balance( sam_id, asset_id_type() ).amount.value, expected_balance );
      BOOST_CHECK_EQUAL( result.new_objects.size(), 0u );
      BOOST_REQUIRE_EQUAL( result.updated_objects.size(), 1u );
      BOOST_CHECK( *result.updated_objects.begin() == tick_1_id );
      BOOST_CHECK_EQUAL( result.removed_objects.size(), 0u );

      // Update 3 CORE in ticket 2 to lock_180_days
      result = update_ticket( tick_2, lock_180_days, asset(3) );
      BOOST_REQUIRE( db.find( tick_2_id ) );
      BOOST_CHECK( tick_2_id(db).account == sam_id );
      BOOST_CHECK( tick_2_id(db).target_type == lock_360_days ); // target type of the remaining ticket is unchanged
      BOOST_CHECK( tick_2_id(db).amount == asset(1000-3) );
      expected_balance -= fee_amount;
      BOOST_CHECK_EQUAL( db.get_balance( sam_id, asset_id_type() ).amount.value, expected_balance );
      BOOST_REQUIRE_EQUAL( result.new_objects.size(), 1u );
      BOOST_REQUIRE_EQUAL( result.updated_objects.size(), 1u );
      BOOST_CHECK( *result.updated_objects.begin() == tick_2_id );
      BOOST_CHECK_EQUAL( result.removed_objects.size(), 0u );

      ticket_id_type new_ticket_id = *result.new_objects.begin();
      BOOST_CHECK( new_ticket_id > tick_4_id );
      BOOST_REQUIRE( db.find( new_ticket_id ) );
      BOOST_CHECK( new_ticket_id(db).account == sam_id );
      BOOST_CHECK( new_ticket_id(db).target_type == lock_180_days ); // target type of the new ticket is set
      BOOST_CHECK( new_ticket_id(db).amount == asset(3) );

      generate_block();

      BOOST_REQUIRE( db.find( tick_1_id ) );
      BOOST_CHECK( tick_1_id(db).account == sam_id );
      BOOST_CHECK( tick_1_id(db).target_type == lock_forever );
      BOOST_CHECK( tick_1_id(db).amount == asset(1) );
      BOOST_CHECK_EQUAL( db.get_balance( sam_id, asset_id_type() ).amount.value, expected_balance );

      BOOST_REQUIRE( db.find( tick_2_id ) );
      BOOST_CHECK( tick_2_id(db).account == sam_id );
      BOOST_CHECK( tick_2_id(db).target_type == lock_360_days );
      BOOST_CHECK( tick_2_id(db).amount == asset(1000-3) );

      BOOST_REQUIRE( db.find( new_ticket_id ) );
      BOOST_CHECK( new_ticket_id(db).account == sam_id );
      BOOST_CHECK( new_ticket_id(db).target_type == lock_180_days );
      BOOST_CHECK( new_ticket_id(db).amount == asset(3) );

   } catch (fc::exception& e) {
      edump((e.to_detail_string()));
      throw;
   }
}

BOOST_AUTO_TEST_CASE( one_lock_180_ticket )
{ try {

      // Pass the hard fork time
      generate_blocks( HARDFORK_CORE_2103_TIME );
      set_expiration( db, trx );

      ACTORS((sam));

      auto init_amount = 10000000 * GRAPHENE_BLOCKCHAIN_PRECISION;
      fund( sam, asset(init_amount) );

      int64_t sam_balance = init_amount;

      // create a ticket
      const ticket_object& tick_1 = create_ticket( sam_id, lock_180_days, asset(100) );
      ticket_id_type tick_1_id = tick_1.id;

      BOOST_CHECK( tick_1_id(db).account == sam_id );
      BOOST_CHECK( tick_1_id(db).target_type == lock_180_days );
      BOOST_CHECK( tick_1_id(db).current_type == liquid );
      BOOST_CHECK( tick_1_id(db).status == charging );
      BOOST_CHECK( tick_1_id(db).amount == asset(100) );
      BOOST_CHECK_EQUAL( tick_1_id(db).value.value, 100 );
      sam_balance -= 100;
      BOOST_CHECK_EQUAL( db.get_balance( sam_id, asset_id_type() ).amount.value, sam_balance );

      // unable to update ticket if not to change target type
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_180_days, {} ), fc::exception );
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_180_days, asset(1) ), fc::exception );
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_180_days, asset(100) ), fc::exception );

      // 14 days passed
      generate_blocks( db.head_block_time() + fc::days(14) );
      set_expiration( db, trx );

      // no change
      BOOST_CHECK( tick_1_id(db).account == sam_id );
      BOOST_CHECK( tick_1_id(db).target_type == lock_180_days );
      BOOST_CHECK( tick_1_id(db).current_type == liquid );
      BOOST_CHECK( tick_1_id(db).status == charging );
      BOOST_CHECK( tick_1_id(db).amount == asset(100) );
      BOOST_CHECK_EQUAL( tick_1_id(db).value.value, 100 );
      BOOST_CHECK_EQUAL( db.get_balance( sam_id, asset_id_type() ).amount.value, sam_balance );

      // unable to update ticket if not to change target type
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_180_days, {} ), fc::exception );
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_180_days, asset(1) ), fc::exception );
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_180_days, asset(100) ), fc::exception );

      // 1 day passed
      generate_blocks( db.head_block_time() + fc::days(1) );
      set_expiration( db, trx );

      // ticket should be stable now
      BOOST_CHECK( tick_1_id(db).account == sam_id );
      BOOST_CHECK( tick_1_id(db).target_type == lock_180_days );
      BOOST_CHECK( tick_1_id(db).current_type == lock_180_days );
      BOOST_CHECK( tick_1_id(db).status == stable );
      BOOST_CHECK( tick_1_id(db).next_auto_update_time == time_point_sec::maximum() );
      BOOST_CHECK( tick_1_id(db).next_type_downgrade_time == time_point_sec::maximum() );
      BOOST_CHECK( tick_1_id(db).amount == asset(100) );
      BOOST_CHECK_EQUAL( tick_1_id(db).value.value, 100 * 2 );
      BOOST_CHECK_EQUAL( db.get_balance( sam_id, asset_id_type() ).amount.value, sam_balance );

      // unable to update ticket if not to change target type
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_180_days, {} ), fc::exception );
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_180_days, asset(1) ), fc::exception );
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_180_days, asset(100) ), fc::exception );

      // 1 day passed
      generate_blocks( db.head_block_time() + fc::days(1) );
      set_expiration( db, trx );

      // no change
      BOOST_CHECK( tick_1_id(db).account == sam_id );
      BOOST_CHECK( tick_1_id(db).target_type == lock_180_days );
      BOOST_CHECK( tick_1_id(db).current_type == lock_180_days );
      BOOST_CHECK( tick_1_id(db).status == stable );
      BOOST_CHECK( tick_1_id(db).next_auto_update_time == time_point_sec::maximum() );
      BOOST_CHECK( tick_1_id(db).next_type_downgrade_time == time_point_sec::maximum() );
      BOOST_CHECK( tick_1_id(db).amount == asset(100) );
      BOOST_CHECK_EQUAL( tick_1_id(db).value.value, 100 * 2 );
      BOOST_CHECK_EQUAL( db.get_balance( sam_id, asset_id_type() ).amount.value, sam_balance );

      // unable to update ticket if not to change target type
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_180_days, {} ), fc::exception );
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_180_days, asset(1) ), fc::exception );
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_180_days, asset(100) ), fc::exception );

      BOOST_REQUIRE( tick_1_id == ticket_id_type() );

   } FC_LOG_AND_RETHROW()
}

BOOST_AUTO_TEST_CASE( one_lock_360_ticket )
{ try {

      // Pass the hard fork time
      generate_blocks( HARDFORK_CORE_2103_TIME );
      set_expiration( db, trx );

      ACTORS((sam));

      auto init_amount = 10000000 * GRAPHENE_BLOCKCHAIN_PRECISION;
      fund( sam, asset(init_amount) );

      int64_t sam_balance = init_amount;

      // create a ticket
      const ticket_object& tick_1 = create_ticket( sam_id, lock_360_days, asset(100) );
      ticket_id_type tick_1_id = tick_1.id;

      BOOST_CHECK( tick_1_id(db).account == sam_id );
      BOOST_CHECK( tick_1_id(db).target_type == lock_360_days );
      BOOST_CHECK( tick_1_id(db).current_type == liquid );
      BOOST_CHECK( tick_1_id(db).status == charging );
      BOOST_CHECK( tick_1_id(db).amount == asset(100) );
      BOOST_CHECK_EQUAL( tick_1_id(db).value.value, 100 );
      sam_balance -= 100;
      BOOST_CHECK_EQUAL( db.get_balance( sam_id, asset_id_type() ).amount.value, sam_balance );

      // unable to update ticket if not to change target type
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_360_days, {} ), fc::exception );
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_360_days, asset(1) ), fc::exception );
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_360_days, asset(100) ), fc::exception );

      // 14 days passed
      generate_blocks( db.head_block_time() + fc::days(14) );
      set_expiration( db, trx );

      // no change
      BOOST_CHECK( tick_1_id(db).account == sam_id );
      BOOST_CHECK( tick_1_id(db).target_type == lock_360_days );
      BOOST_CHECK( tick_1_id(db).current_type == liquid );
      BOOST_CHECK( tick_1_id(db).status == charging );
      BOOST_CHECK( tick_1_id(db).amount == asset(100) );
      BOOST_CHECK_EQUAL( tick_1_id(db).value.value, 100 );
      BOOST_CHECK_EQUAL( db.get_balance( sam_id, asset_id_type() ).amount.value, sam_balance );

      // unable to update ticket if not to change target type
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_360_days, {} ), fc::exception );
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_360_days, asset(1) ), fc::exception );
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_360_days, asset(100) ), fc::exception );

      // 1 day passed
      generate_blocks( db.head_block_time() + fc::days(1) );
      set_expiration( db, trx );

      // ticket should be upgraded now, and still charging
      BOOST_CHECK( tick_1_id(db).account == sam_id );
      BOOST_CHECK( tick_1_id(db).target_type == lock_360_days );
      BOOST_CHECK( tick_1_id(db).current_type == lock_180_days );
      BOOST_CHECK( tick_1_id(db).status == charging );
      BOOST_CHECK( tick_1_id(db).amount == asset(100) );
      BOOST_CHECK_EQUAL( tick_1_id(db).value.value, 100 * 2 );
      BOOST_CHECK_EQUAL( db.get_balance( sam_id, asset_id_type() ).amount.value, sam_balance );

      // unable to update ticket if not to change target type
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_360_days, {} ), fc::exception );
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_360_days, asset(1) ), fc::exception );
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_360_days, asset(100) ), fc::exception );

      // 14 days passed
      generate_blocks( db.head_block_time() + fc::days(14) );
      set_expiration( db, trx );

      // no change
      BOOST_CHECK( tick_1_id(db).account == sam_id );
      BOOST_CHECK( tick_1_id(db).target_type == lock_360_days );
      BOOST_CHECK( tick_1_id(db).current_type == lock_180_days );
      BOOST_CHECK( tick_1_id(db).status == charging );
      BOOST_CHECK( tick_1_id(db).amount == asset(100) );
      BOOST_CHECK_EQUAL( tick_1_id(db).value.value, 100 * 2 );
      BOOST_CHECK_EQUAL( db.get_balance( sam_id, asset_id_type() ).amount.value, sam_balance );

      // unable to update ticket if not to change target type
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_360_days, {} ), fc::exception );
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_360_days, asset(1) ), fc::exception );
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_360_days, asset(100) ), fc::exception );

      // 1 day passed
      generate_blocks( db.head_block_time() + fc::days(1) );
      set_expiration( db, trx );

      // ticket should be stable now
      BOOST_CHECK( tick_1_id(db).account == sam_id );
      BOOST_CHECK( tick_1_id(db).target_type == lock_360_days );
      BOOST_CHECK( tick_1_id(db).current_type == lock_360_days );
      BOOST_CHECK( tick_1_id(db).status == stable );
      BOOST_CHECK( tick_1_id(db).next_auto_update_time == time_point_sec::maximum() );
      BOOST_CHECK( tick_1_id(db).next_type_downgrade_time == time_point_sec::maximum() );
      BOOST_CHECK( tick_1_id(db).amount == asset(100) );
      BOOST_CHECK_EQUAL( tick_1_id(db).value.value, 100 * 4 );
      BOOST_CHECK_EQUAL( db.get_balance( sam_id, asset_id_type() ).amount.value, sam_balance );

      // unable to update ticket if not to change target type
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_360_days, {} ), fc::exception );
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_360_days, asset(1) ), fc::exception );
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_360_days, asset(100) ), fc::exception );

      // 1 day passed
      generate_blocks( db.head_block_time() + fc::days(1) );
      set_expiration( db, trx );

      // no change
      BOOST_CHECK( tick_1_id(db).account == sam_id );
      BOOST_CHECK( tick_1_id(db).target_type == lock_360_days );
      BOOST_CHECK( tick_1_id(db).current_type == lock_360_days );
      BOOST_CHECK( tick_1_id(db).status == stable );
      BOOST_CHECK( tick_1_id(db).next_auto_update_time == time_point_sec::maximum() );
      BOOST_CHECK( tick_1_id(db).next_type_downgrade_time == time_point_sec::maximum() );
      BOOST_CHECK( tick_1_id(db).amount == asset(100) );
      BOOST_CHECK_EQUAL( tick_1_id(db).value.value, 100 * 4 );
      BOOST_CHECK_EQUAL( db.get_balance( sam_id, asset_id_type() ).amount.value, sam_balance );

      // unable to update ticket if not to change target type
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_360_days, {} ), fc::exception );
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_360_days, asset(1) ), fc::exception );
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_360_days, asset(100) ), fc::exception );

      BOOST_REQUIRE( tick_1_id == ticket_id_type() );

   } FC_LOG_AND_RETHROW()
}

BOOST_AUTO_TEST_CASE( one_lock_720_ticket )
{ try {

      // Pass the hard fork time
      generate_blocks( HARDFORK_CORE_2103_TIME );
      set_expiration( db, trx );

      ACTORS((sam));

      auto init_amount = 10000000 * GRAPHENE_BLOCKCHAIN_PRECISION;
      fund( sam, asset(init_amount) );

      int64_t sam_balance = init_amount;

      // create a ticket
      const ticket_object& tick_1 = create_ticket( sam_id, lock_720_days, asset(100) );
      ticket_id_type tick_1_id = tick_1.id;

      BOOST_CHECK( tick_1_id(db).account == sam_id );
      BOOST_CHECK( tick_1_id(db).target_type == lock_720_days );
      BOOST_CHECK( tick_1_id(db).current_type == liquid );
      BOOST_CHECK( tick_1_id(db).status == charging );
      BOOST_CHECK( tick_1_id(db).amount == asset(100) );
      BOOST_CHECK_EQUAL( tick_1_id(db).value.value, 100 );
      sam_balance -= 100;
      BOOST_CHECK_EQUAL( db.get_balance( sam_id, asset_id_type() ).amount.value, sam_balance );

      // unable to update ticket if not to change target type
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_720_days, {} ), fc::exception );
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_720_days, asset(1) ), fc::exception );
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_720_days, asset(100) ), fc::exception );

      // 14 days passed
      generate_blocks( db.head_block_time() + fc::days(14) );
      set_expiration( db, trx );

      // no change
      BOOST_CHECK( tick_1_id(db).account == sam_id );
      BOOST_CHECK( tick_1_id(db).target_type == lock_720_days );
      BOOST_CHECK( tick_1_id(db).current_type == liquid );
      BOOST_CHECK( tick_1_id(db).status == charging );
      BOOST_CHECK( tick_1_id(db).amount == asset(100) );
      BOOST_CHECK_EQUAL( tick_1_id(db).value.value, 100 );
      BOOST_CHECK_EQUAL( db.get_balance( sam_id, asset_id_type() ).amount.value, sam_balance );

      // unable to update ticket if not to change target type
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_720_days, {} ), fc::exception );
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_720_days, asset(1) ), fc::exception );
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_720_days, asset(100) ), fc::exception );

      // 1 day passed
      generate_blocks( db.head_block_time() + fc::days(1) );
      set_expiration( db, trx );

      // ticket should be upgraded now, and still charging
      BOOST_CHECK( tick_1_id(db).account == sam_id );
      BOOST_CHECK( tick_1_id(db).target_type == lock_720_days );
      BOOST_CHECK( tick_1_id(db).current_type == lock_180_days );
      BOOST_CHECK( tick_1_id(db).status == charging );
      BOOST_CHECK( tick_1_id(db).amount == asset(100) );
      BOOST_CHECK_EQUAL( tick_1_id(db).value.value, 100 * 2 );
      BOOST_CHECK_EQUAL( db.get_balance( sam_id, asset_id_type() ).amount.value, sam_balance );

      // unable to update ticket if not to change target type
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_720_days, {} ), fc::exception );
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_720_days, asset(1) ), fc::exception );
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_720_days, asset(100) ), fc::exception );

      // 14 days passed
      generate_blocks( db.head_block_time() + fc::days(14) );
      set_expiration( db, trx );

      // no change
      BOOST_CHECK( tick_1_id(db).account == sam_id );
      BOOST_CHECK( tick_1_id(db).target_type == lock_720_days );
      BOOST_CHECK( tick_1_id(db).current_type == lock_180_days );
      BOOST_CHECK( tick_1_id(db).status == charging );
      BOOST_CHECK( tick_1_id(db).amount == asset(100) );
      BOOST_CHECK_EQUAL( tick_1_id(db).value.value, 100 * 2 );
      BOOST_CHECK_EQUAL( db.get_balance( sam_id, asset_id_type() ).amount.value, sam_balance );

      // unable to update ticket if not to change target type
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_720_days, {} ), fc::exception );
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_720_days, asset(1) ), fc::exception );
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_720_days, asset(100) ), fc::exception );

      // 1 day passed
      generate_blocks( db.head_block_time() + fc::days(1) );
      set_expiration( db, trx );

      // ticket should be upgraded now, and still charging
      BOOST_CHECK( tick_1_id(db).account == sam_id );
      BOOST_CHECK( tick_1_id(db).target_type == lock_720_days );
      BOOST_CHECK( tick_1_id(db).current_type == lock_360_days );
      BOOST_CHECK( tick_1_id(db).status == charging );
      BOOST_CHECK( tick_1_id(db).amount == asset(100) );
      BOOST_CHECK_EQUAL( tick_1_id(db).value.value, 100 * 4 );
      BOOST_CHECK_EQUAL( db.get_balance( sam_id, asset_id_type() ).amount.value, sam_balance );

      // unable to update ticket if not to change target type
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_720_days, {} ), fc::exception );
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_720_days, asset(1) ), fc::exception );
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_720_days, asset(100) ), fc::exception );

      // 14 days passed
      generate_blocks( db.head_block_time() + fc::days(14) );
      set_expiration( db, trx );

      // no change
      BOOST_CHECK( tick_1_id(db).account == sam_id );
      BOOST_CHECK( tick_1_id(db).target_type == lock_720_days );
      BOOST_CHECK( tick_1_id(db).current_type == lock_360_days );
      BOOST_CHECK( tick_1_id(db).status == charging );
      BOOST_CHECK( tick_1_id(db).amount == asset(100) );
      BOOST_CHECK_EQUAL( tick_1_id(db).value.value, 100 * 4 );
      BOOST_CHECK_EQUAL( db.get_balance( sam_id, asset_id_type() ).amount.value, sam_balance );

      // unable to update ticket if not to change target type
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_720_days, {} ), fc::exception );
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_720_days, asset(1) ), fc::exception );
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_720_days, asset(100) ), fc::exception );

      // 1 day passed
      generate_blocks( db.head_block_time() + fc::days(1) );
      set_expiration( db, trx );

      // ticket should be stable now
      BOOST_CHECK( tick_1_id(db).account == sam_id );
      BOOST_CHECK( tick_1_id(db).target_type == lock_720_days );
      BOOST_CHECK( tick_1_id(db).current_type == lock_720_days );
      BOOST_CHECK( tick_1_id(db).status == stable );
      BOOST_CHECK( tick_1_id(db).next_auto_update_time == time_point_sec::maximum() );
      BOOST_CHECK( tick_1_id(db).next_type_downgrade_time == time_point_sec::maximum() );
      BOOST_CHECK( tick_1_id(db).amount == asset(100) );
      BOOST_CHECK_EQUAL( tick_1_id(db).value.value, 100 * 8 );
      BOOST_CHECK_EQUAL( db.get_balance( sam_id, asset_id_type() ).amount.value, sam_balance );

      // unable to update ticket if not to change target type
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_720_days, {} ), fc::exception );
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_720_days, asset(1) ), fc::exception );
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_720_days, asset(100) ), fc::exception );

      // 1 day passed
      generate_blocks( db.head_block_time() + fc::days(1) );
      set_expiration( db, trx );

      // no change
      BOOST_CHECK( tick_1_id(db).account == sam_id );
      BOOST_CHECK( tick_1_id(db).target_type == lock_720_days );
      BOOST_CHECK( tick_1_id(db).current_type == lock_720_days );
      BOOST_CHECK( tick_1_id(db).status == stable );
      BOOST_CHECK( tick_1_id(db).next_auto_update_time == time_point_sec::maximum() );
      BOOST_CHECK( tick_1_id(db).next_type_downgrade_time == time_point_sec::maximum() );
      BOOST_CHECK( tick_1_id(db).amount == asset(100) );
      BOOST_CHECK_EQUAL( tick_1_id(db).value.value, 100 * 8 );
      BOOST_CHECK_EQUAL( db.get_balance( sam_id, asset_id_type() ).amount.value, sam_balance );

      // unable to update ticket if not to change target type
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_720_days, {} ), fc::exception );
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_720_days, asset(1) ), fc::exception );
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_720_days, asset(100) ), fc::exception );

      BOOST_REQUIRE( tick_1_id == ticket_id_type() );

   } FC_LOG_AND_RETHROW()
}

BOOST_AUTO_TEST_CASE( one_lock_720_ticket_if_blocks_missed )
{ try {

      // Pass the hard fork time
      generate_blocks( HARDFORK_CORE_2103_TIME );
      set_expiration( db, trx );

      ACTORS((sam));

      auto init_amount = 10000000 * GRAPHENE_BLOCKCHAIN_PRECISION;
      fund( sam, asset(init_amount) );

      int64_t sam_balance = init_amount;

      // create a ticket
      const ticket_object& tick_1 = create_ticket( sam_id, lock_720_days, asset(100) );
      ticket_id_type tick_1_id = tick_1.id;

      BOOST_CHECK( tick_1_id(db).account == sam_id );
      BOOST_CHECK( tick_1_id(db).target_type == lock_720_days );
      BOOST_CHECK( tick_1_id(db).current_type == liquid );
      BOOST_CHECK( tick_1_id(db).status == charging );
      BOOST_CHECK( tick_1_id(db).amount == asset(100) );
      BOOST_CHECK_EQUAL( tick_1_id(db).value.value, 100 );
      sam_balance -= 100;
      BOOST_CHECK_EQUAL( db.get_balance( sam_id, asset_id_type() ).amount.value, sam_balance );

      // unable to update ticket if not to change target type
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_720_days, {} ), fc::exception );
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_720_days, asset(1) ), fc::exception );
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_720_days, asset(100) ), fc::exception );

      // 800 days passed
      generate_blocks( db.head_block_time() + fc::days(800) );
      set_expiration( db, trx );

      // check the ticket
      BOOST_CHECK( tick_1_id(db).account == sam_id );
      BOOST_CHECK( tick_1_id(db).target_type == lock_720_days );
      BOOST_CHECK( tick_1_id(db).current_type == lock_720_days );
      BOOST_CHECK( tick_1_id(db).status == stable );
      BOOST_CHECK( tick_1_id(db).next_auto_update_time == time_point_sec::maximum() );
      BOOST_CHECK( tick_1_id(db).next_type_downgrade_time == time_point_sec::maximum() );
      BOOST_CHECK( tick_1_id(db).amount == asset(100) );
      BOOST_CHECK_EQUAL( tick_1_id(db).value.value, 100 * 8 );
      BOOST_CHECK_EQUAL( db.get_balance( sam_id, asset_id_type() ).amount.value, sam_balance );

      // unable to update ticket if not to change target type
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_720_days, {} ), fc::exception );
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_720_days, asset(1) ), fc::exception );
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_720_days, asset(100) ), fc::exception );

      BOOST_REQUIRE( tick_1_id == ticket_id_type() );

   } FC_LOG_AND_RETHROW()
}

BOOST_AUTO_TEST_CASE( one_lock_forever_ticket )
{ try {

      // Pass the hard fork time
      generate_blocks( HARDFORK_CORE_2103_TIME );
      set_expiration( db, trx );

      ACTORS((sam));

      auto init_amount = 10000000 * GRAPHENE_BLOCKCHAIN_PRECISION;
      fund( sam, asset(init_amount) );

      int64_t sam_balance = init_amount;

      // create a ticket
      const ticket_object& tick_1 = create_ticket( sam_id, lock_forever, asset(100) );
      ticket_id_type tick_1_id = tick_1.id;

      BOOST_CHECK( tick_1_id(db).account == sam_id );
      BOOST_CHECK( tick_1_id(db).target_type == lock_forever );
      BOOST_CHECK( tick_1_id(db).current_type == liquid );
      BOOST_CHECK( tick_1_id(db).status == charging );
      BOOST_CHECK( tick_1_id(db).amount == asset(100) );
      BOOST_CHECK_EQUAL( tick_1_id(db).value.value, 100 );
      sam_balance -= 100;
      BOOST_CHECK_EQUAL( db.get_balance( sam_id, asset_id_type() ).amount.value, sam_balance );

      // unable to update ticket if not to change target type
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_forever, {} ), fc::exception );
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_forever, asset(1) ), fc::exception );
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_forever, asset(100) ), fc::exception );

      // 14 days passed
      generate_blocks( db.head_block_time() + fc::days(14) );
      set_expiration( db, trx );

      // no change
      BOOST_CHECK( tick_1_id(db).account == sam_id );
      BOOST_CHECK( tick_1_id(db).target_type == lock_forever );
      BOOST_CHECK( tick_1_id(db).current_type == liquid );
      BOOST_CHECK( tick_1_id(db).status == charging );
      BOOST_CHECK( tick_1_id(db).amount == asset(100) );
      BOOST_CHECK_EQUAL( tick_1_id(db).value.value, 100 );
      BOOST_CHECK_EQUAL( db.get_balance( sam_id, asset_id_type() ).amount.value, sam_balance );

      // unable to update ticket if not to change target type
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_forever, {} ), fc::exception );
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_forever, asset(1) ), fc::exception );
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_forever, asset(100) ), fc::exception );

      // 1 day passed
      generate_blocks( db.head_block_time() + fc::days(1) );
      set_expiration( db, trx );

      // ticket should be upgraded now, and still charging
      BOOST_CHECK( tick_1_id(db).account == sam_id );
      BOOST_CHECK( tick_1_id(db).target_type == lock_forever );
      BOOST_CHECK( tick_1_id(db).current_type == lock_180_days );
      BOOST_CHECK( tick_1_id(db).status == charging );
      BOOST_CHECK( tick_1_id(db).amount == asset(100) );
      BOOST_CHECK_EQUAL( tick_1_id(db).value.value, 100 * 2 );
      BOOST_CHECK_EQUAL( db.get_balance( sam_id, asset_id_type() ).amount.value, sam_balance );

      // unable to update ticket if not to change target type
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_forever, {} ), fc::exception );
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_forever, asset(1) ), fc::exception );
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_forever, asset(100) ), fc::exception );

      // 14 days passed
      generate_blocks( db.head_block_time() + fc::days(14) );
      set_expiration( db, trx );

      // no change
      BOOST_CHECK( tick_1_id(db).account == sam_id );
      BOOST_CHECK( tick_1_id(db).target_type == lock_forever );
      BOOST_CHECK( tick_1_id(db).current_type == lock_180_days );
      BOOST_CHECK( tick_1_id(db).status == charging );
      BOOST_CHECK( tick_1_id(db).amount == asset(100) );
      BOOST_CHECK_EQUAL( tick_1_id(db).value.value, 100 * 2 );
      BOOST_CHECK_EQUAL( db.get_balance( sam_id, asset_id_type() ).amount.value, sam_balance );

      // unable to update ticket if not to change target type
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_forever, {} ), fc::exception );
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_forever, asset(1) ), fc::exception );
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_forever, asset(100) ), fc::exception );

      // 1 day passed
      generate_blocks( db.head_block_time() + fc::days(1) );
      set_expiration( db, trx );

      // ticket should be upgraded now, and still charging
      BOOST_CHECK( tick_1_id(db).account == sam_id );
      BOOST_CHECK( tick_1_id(db).target_type == lock_forever );
      BOOST_CHECK( tick_1_id(db).current_type == lock_360_days );
      BOOST_CHECK( tick_1_id(db).status == charging );
      BOOST_CHECK( tick_1_id(db).amount == asset(100) );
      BOOST_CHECK_EQUAL( tick_1_id(db).value.value, 100 * 4 );
      BOOST_CHECK_EQUAL( db.get_balance( sam_id, asset_id_type() ).amount.value, sam_balance );

      // unable to update ticket if not to change target type
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_forever, {} ), fc::exception );
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_forever, asset(1) ), fc::exception );
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_forever, asset(100) ), fc::exception );

      // 14 days passed
      generate_blocks( db.head_block_time() + fc::days(14) );
      set_expiration( db, trx );

      // no change
      BOOST_CHECK( tick_1_id(db).account == sam_id );
      BOOST_CHECK( tick_1_id(db).target_type == lock_forever );
      BOOST_CHECK( tick_1_id(db).current_type == lock_360_days );
      BOOST_CHECK( tick_1_id(db).status == charging );
      BOOST_CHECK( tick_1_id(db).amount == asset(100) );
      BOOST_CHECK_EQUAL( tick_1_id(db).value.value, 100 * 4 );
      BOOST_CHECK_EQUAL( db.get_balance( sam_id, asset_id_type() ).amount.value, sam_balance );

      // unable to update ticket if not to change target type
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_forever, {} ), fc::exception );
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_forever, asset(1) ), fc::exception );
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_forever, asset(100) ), fc::exception );

      // 1 day passed
      generate_blocks( db.head_block_time() + fc::days(1) );
      set_expiration( db, trx );

      // ticket should be upgraded now, and still charging
      BOOST_CHECK( tick_1_id(db).account == sam_id );
      BOOST_CHECK( tick_1_id(db).target_type == lock_forever );
      BOOST_CHECK( tick_1_id(db).current_type == lock_720_days );
      BOOST_CHECK( tick_1_id(db).status == charging );
      BOOST_CHECK( tick_1_id(db).amount == asset(100) );
      BOOST_CHECK_EQUAL( tick_1_id(db).value.value, 100 * 8 );
      BOOST_CHECK_EQUAL( db.get_balance( sam_id, asset_id_type() ).amount.value, sam_balance );

      // unable to update ticket if not to change target type
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_forever, {} ), fc::exception );
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_forever, asset(1) ), fc::exception );
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_forever, asset(100) ), fc::exception );

      // 14 days passed
      generate_blocks( db.head_block_time() + fc::days(14) );
      set_expiration( db, trx );

      // no change
      BOOST_CHECK( tick_1_id(db).account == sam_id );
      BOOST_CHECK( tick_1_id(db).target_type == lock_forever );
      BOOST_CHECK( tick_1_id(db).current_type == lock_720_days );
      BOOST_CHECK( tick_1_id(db).status == charging );
      BOOST_CHECK( tick_1_id(db).amount == asset(100) );
      BOOST_CHECK_EQUAL( tick_1_id(db).value.value, 100 * 8 );
      BOOST_CHECK_EQUAL( db.get_balance( sam_id, asset_id_type() ).amount.value, sam_balance );

      // unable to update ticket if not to change target type
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_forever, {} ), fc::exception );
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_forever, asset(1) ), fc::exception );
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_forever, asset(100) ), fc::exception );

      // 1 day passed
      generate_blocks( db.head_block_time() + fc::days(1) );
      set_expiration( db, trx );

      // ticket should have reached the target
      BOOST_CHECK( tick_1_id(db).account == sam_id );
      BOOST_CHECK( tick_1_id(db).target_type == lock_forever );
      BOOST_CHECK( tick_1_id(db).current_type == lock_forever );
      BOOST_CHECK( tick_1_id(db).status == withdrawing );
      BOOST_CHECK( tick_1_id(db).amount == asset(100) );
      BOOST_CHECK_EQUAL( tick_1_id(db).value.value, 100 * 8 );
      BOOST_CHECK_EQUAL( db.get_balance( sam_id, asset_id_type() ).amount.value, sam_balance );

      // no longer be able to update ticket
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_forever, {} ), fc::exception );
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_forever, asset(1) ), fc::exception );
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_forever, asset(100) ), fc::exception );
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_720_days, {} ), fc::exception );
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_720_days, asset(1) ), fc::exception );
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_360_days, {} ), fc::exception );
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_360_days, asset(1) ), fc::exception );
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_180_days, {} ), fc::exception );
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_180_days, asset(1) ), fc::exception );
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), liquid, {} ), fc::exception );
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), liquid, asset(1) ), fc::exception );

      BOOST_REQUIRE( tick_1_id == ticket_id_type() );

   } FC_LOG_AND_RETHROW()
}

BOOST_AUTO_TEST_CASE( one_lock_forever_ticket_if_blocks_missed )
{ try {

      // Pass the hard fork time
      generate_blocks( HARDFORK_CORE_2103_TIME );
      set_expiration( db, trx );

      ACTORS((sam));

      auto init_amount = 10000000 * GRAPHENE_BLOCKCHAIN_PRECISION;
      fund( sam, asset(init_amount) );

      int64_t sam_balance = init_amount;

      // create a ticket
      const ticket_object& tick_1 = create_ticket( sam_id, lock_forever, asset(100) );
      ticket_id_type tick_1_id = tick_1.id;

      BOOST_CHECK( tick_1_id(db).account == sam_id );
      BOOST_CHECK( tick_1_id(db).target_type == lock_forever );
      BOOST_CHECK( tick_1_id(db).current_type == liquid );
      BOOST_CHECK( tick_1_id(db).status == charging );
      BOOST_CHECK( tick_1_id(db).amount == asset(100) );
      BOOST_CHECK_EQUAL( tick_1_id(db).value.value, 100 );
      sam_balance -= 100;
      BOOST_CHECK_EQUAL( db.get_balance( sam_id, asset_id_type() ).amount.value, sam_balance );

      // unable to update ticket if not to change target type
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_forever, {} ), fc::exception );
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_forever, asset(1) ), fc::exception );
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_forever, asset(100) ), fc::exception );

      // 60 days passed
      generate_blocks( db.head_block_time() + fc::days(60) );
      set_expiration( db, trx );

      // ticket should have reached the target
      BOOST_CHECK( tick_1_id(db).account == sam_id );
      BOOST_CHECK( tick_1_id(db).target_type == lock_forever );
      BOOST_CHECK( tick_1_id(db).current_type == lock_forever );
      BOOST_CHECK( tick_1_id(db).status == withdrawing );
      BOOST_CHECK( tick_1_id(db).amount == asset(100) );
      BOOST_CHECK_EQUAL( tick_1_id(db).value.value, 100 * 8 );
      BOOST_CHECK_EQUAL( db.get_balance( sam_id, asset_id_type() ).amount.value, sam_balance );

      // no longer be able to update ticket
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_forever, {} ), fc::exception );
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_forever, asset(1) ), fc::exception );
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_forever, asset(100) ), fc::exception );
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_720_days, {} ), fc::exception );
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_720_days, asset(1) ), fc::exception );
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_360_days, {} ), fc::exception );
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_360_days, asset(1) ), fc::exception );
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_180_days, {} ), fc::exception );
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_180_days, asset(1) ), fc::exception );
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), liquid, {} ), fc::exception );
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), liquid, asset(1) ), fc::exception );

      BOOST_REQUIRE( tick_1_id == ticket_id_type() );

   } FC_LOG_AND_RETHROW()
}

BOOST_AUTO_TEST_CASE( one_lock_forever_ticket_if_too_many_blocks_missed )
{ try {

      // Pass the hard fork time
      generate_blocks( HARDFORK_CORE_2103_TIME );
      set_expiration( db, trx );

      ACTORS((sam));

      auto init_amount = 10000000 * GRAPHENE_BLOCKCHAIN_PRECISION;
      fund( sam, asset(init_amount) );

      int64_t sam_balance = init_amount;

      // create a ticket
      const ticket_object& tick_1 = create_ticket( sam_id, lock_forever, asset(100) );
      ticket_id_type tick_1_id = tick_1.id;

      BOOST_CHECK( tick_1_id(db).account == sam_id );
      BOOST_CHECK( tick_1_id(db).target_type == lock_forever );
      BOOST_CHECK( tick_1_id(db).current_type == liquid );
      BOOST_CHECK( tick_1_id(db).status == charging );
      BOOST_CHECK( tick_1_id(db).amount == asset(100) );
      BOOST_CHECK_EQUAL( tick_1_id(db).value.value, 100 );
      sam_balance -= 100;
      BOOST_CHECK_EQUAL( db.get_balance( sam_id, asset_id_type() ).amount.value, sam_balance );

      // unable to update ticket if not to change target type
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_forever, {} ), fc::exception );
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_forever, asset(1) ), fc::exception );
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_forever, asset(100) ), fc::exception );

      // 1060 days passed
      generate_blocks( db.head_block_time() + fc::days(1060) );
      set_expiration( db, trx );

      // check ticket
      BOOST_CHECK( tick_1_id(db).account == sam_id );
      BOOST_CHECK( tick_1_id(db).target_type == lock_forever );
      BOOST_CHECK( tick_1_id(db).current_type == lock_forever );
      BOOST_CHECK( tick_1_id(db).status == stable );
      BOOST_CHECK( tick_1_id(db).next_auto_update_time == time_point_sec::maximum() );
      BOOST_CHECK( tick_1_id(db).next_type_downgrade_time == time_point_sec::maximum() );
      BOOST_CHECK( tick_1_id(db).amount == asset(100) );
      BOOST_CHECK_EQUAL( tick_1_id(db).value.value, 0 );
      BOOST_CHECK_EQUAL( db.get_balance( sam_id, asset_id_type() ).amount.value, sam_balance );

      // no longer be able to update ticket
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_forever, {} ), fc::exception );
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_forever, asset(1) ), fc::exception );
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_forever, asset(100) ), fc::exception );
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_720_days, {} ), fc::exception );
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_720_days, asset(1) ), fc::exception );
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_360_days, {} ), fc::exception );
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_360_days, asset(1) ), fc::exception );
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_180_days, {} ), fc::exception );
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_180_days, asset(1) ), fc::exception );
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), liquid, {} ), fc::exception );
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), liquid, asset(1) ), fc::exception );

      BOOST_REQUIRE( tick_1_id == ticket_id_type() );

   } FC_LOG_AND_RETHROW()
}

BOOST_AUTO_TEST_CASE( upgrade_lock_180_ticket_to_360 )
{ try {

      INVOKE( one_lock_180_ticket );

      GET_ACTOR( sam );

      int64_t sam_balance = db.get_balance( sam_id, asset_id_type() ).amount.value;

      ticket_id_type tick_1_id; // default value

      // upgrade the ticket
      auto result = update_ticket( tick_1_id(db), lock_360_days, {} );
      BOOST_CHECK_EQUAL( result.new_objects.size(), 0u );

      BOOST_CHECK( tick_1_id(db).target_type == lock_360_days );
      BOOST_CHECK( tick_1_id(db).current_type == lock_180_days );
      BOOST_CHECK( tick_1_id(db).status == charging );
      BOOST_CHECK( tick_1_id(db).amount == asset(100) );
      BOOST_CHECK_EQUAL( tick_1_id(db).value.value, 100 * 2 );

      BOOST_CHECK_EQUAL( db.get_balance( sam_id, asset_id_type() ).amount.value, sam_balance );

      // 14 days passed
      generate_blocks( db.head_block_time() + fc::days(14) );
      set_expiration( db, trx );

      // no change
      BOOST_CHECK( tick_1_id(db).target_type == lock_360_days );
      BOOST_CHECK( tick_1_id(db).current_type == lock_180_days );
      BOOST_CHECK( tick_1_id(db).status == charging );
      BOOST_CHECK( tick_1_id(db).amount == asset(100) );
      BOOST_CHECK_EQUAL( tick_1_id(db).value.value, 100 * 2 );

      BOOST_CHECK_EQUAL( db.get_balance( sam_id, asset_id_type() ).amount.value, sam_balance );

      // 1 day passed
      generate_blocks( db.head_block_time() + fc::days(1) );
      set_expiration( db, trx );

      // the ticket should be upgraded
      BOOST_CHECK( tick_1_id(db).target_type == lock_360_days );
      BOOST_CHECK( tick_1_id(db).current_type == lock_360_days );
      BOOST_CHECK( tick_1_id(db).status == stable );
      BOOST_CHECK( tick_1_id(db).next_auto_update_time == time_point_sec::maximum() );
      BOOST_CHECK( tick_1_id(db).next_type_downgrade_time == time_point_sec::maximum() );
      BOOST_CHECK( tick_1_id(db).amount == asset(100) );
      BOOST_CHECK_EQUAL( tick_1_id(db).value.value, 100 * 4 );

      BOOST_CHECK_EQUAL( db.get_balance( sam_id, asset_id_type() ).amount.value, sam_balance );

      // 1 day passed
      generate_blocks( db.head_block_time() + fc::days(1) );
      set_expiration( db, trx );

      // no change
      BOOST_CHECK( tick_1_id(db).target_type == lock_360_days );
      BOOST_CHECK( tick_1_id(db).current_type == lock_360_days );
      BOOST_CHECK( tick_1_id(db).status == stable );
      BOOST_CHECK( tick_1_id(db).next_auto_update_time == time_point_sec::maximum() );
      BOOST_CHECK( tick_1_id(db).next_type_downgrade_time == time_point_sec::maximum() );
      BOOST_CHECK( tick_1_id(db).amount == asset(100) );
      BOOST_CHECK_EQUAL( tick_1_id(db).value.value, 100 * 4 );

      BOOST_CHECK_EQUAL( db.get_balance( sam_id, asset_id_type() ).amount.value, sam_balance );

   } FC_LOG_AND_RETHROW()
}

BOOST_AUTO_TEST_CASE( upgrade_lock_180_ticket_to_720 )
{ try {

      INVOKE( one_lock_180_ticket );

      GET_ACTOR( sam );

      int64_t sam_balance = db.get_balance( sam_id, asset_id_type() ).amount.value;

      ticket_id_type tick_1_id; // default value

      // upgrade the ticket
      auto result = update_ticket( tick_1_id(db), lock_720_days, {} );
      BOOST_CHECK_EQUAL( result.new_objects.size(), 0u );

      BOOST_CHECK( tick_1_id(db).target_type == lock_720_days );
      BOOST_CHECK( tick_1_id(db).current_type == lock_180_days );
      BOOST_CHECK( tick_1_id(db).status == charging );
      BOOST_CHECK( tick_1_id(db).amount == asset(100) );
      BOOST_CHECK_EQUAL( tick_1_id(db).value.value, 100 * 2 );
      BOOST_CHECK_EQUAL( db.get_balance( sam_id, asset_id_type() ).amount.value, sam_balance );

      // 14 days passed
      generate_blocks( db.head_block_time() + fc::days(14) );
      set_expiration( db, trx );

      // no change
      BOOST_CHECK( tick_1_id(db).target_type == lock_720_days );
      BOOST_CHECK( tick_1_id(db).current_type == lock_180_days );
      BOOST_CHECK( tick_1_id(db).status == charging );
      BOOST_CHECK( tick_1_id(db).amount == asset(100) );
      BOOST_CHECK_EQUAL( tick_1_id(db).value.value, 100 * 2 );
      BOOST_CHECK_EQUAL( db.get_balance( sam_id, asset_id_type() ).amount.value, sam_balance );

      // 1 day passed
      generate_blocks( db.head_block_time() + fc::days(1) );
      set_expiration( db, trx );

      // the ticket should be upgraded
      BOOST_CHECK( tick_1_id(db).target_type == lock_720_days );
      BOOST_CHECK( tick_1_id(db).current_type == lock_360_days );
      BOOST_CHECK( tick_1_id(db).status == charging );
      BOOST_CHECK( tick_1_id(db).amount == asset(100) );
      BOOST_CHECK_EQUAL( tick_1_id(db).value.value, 100 * 4 );
      BOOST_CHECK_EQUAL( db.get_balance( sam_id, asset_id_type() ).amount.value, sam_balance );

      // 14 days passed
      generate_blocks( db.head_block_time() + fc::days(14) );
      set_expiration( db, trx );

      // no change
      BOOST_CHECK( tick_1_id(db).target_type == lock_720_days );
      BOOST_CHECK( tick_1_id(db).current_type == lock_360_days );
      BOOST_CHECK( tick_1_id(db).status == charging );
      BOOST_CHECK( tick_1_id(db).amount == asset(100) );
      BOOST_CHECK_EQUAL( tick_1_id(db).value.value, 100 * 4 );
      BOOST_CHECK_EQUAL( db.get_balance( sam_id, asset_id_type() ).amount.value, sam_balance );

      // 1 day passed
      generate_blocks( db.head_block_time() + fc::days(1) );
      set_expiration( db, trx );

      // the ticket should be upgraded
      BOOST_CHECK( tick_1_id(db).target_type == lock_720_days );
      BOOST_CHECK( tick_1_id(db).current_type == lock_720_days );
      BOOST_CHECK( tick_1_id(db).status == stable );
      BOOST_CHECK( tick_1_id(db).next_auto_update_time == time_point_sec::maximum() );
      BOOST_CHECK( tick_1_id(db).next_type_downgrade_time == time_point_sec::maximum() );
      BOOST_CHECK( tick_1_id(db).amount == asset(100) );
      BOOST_CHECK_EQUAL( tick_1_id(db).value.value, 100 * 8 );
      BOOST_CHECK_EQUAL( db.get_balance( sam_id, asset_id_type() ).amount.value, sam_balance );

      // 1 day passed
      generate_blocks( db.head_block_time() + fc::days(1) );
      set_expiration( db, trx );

      // no change
      BOOST_CHECK( tick_1_id(db).target_type == lock_720_days );
      BOOST_CHECK( tick_1_id(db).current_type == lock_720_days );
      BOOST_CHECK( tick_1_id(db).status == stable );
      BOOST_CHECK( tick_1_id(db).next_auto_update_time == time_point_sec::maximum() );
      BOOST_CHECK( tick_1_id(db).next_type_downgrade_time == time_point_sec::maximum() );
      BOOST_CHECK( tick_1_id(db).amount == asset(100) );
      BOOST_CHECK_EQUAL( tick_1_id(db).value.value, 100 * 8 );
      BOOST_CHECK_EQUAL( db.get_balance( sam_id, asset_id_type() ).amount.value, sam_balance );

   } FC_LOG_AND_RETHROW()
}

BOOST_AUTO_TEST_CASE( upgrade_lock_180_ticket_to_forever )
{ try {

      INVOKE( one_lock_180_ticket );

      GET_ACTOR( sam );

      int64_t sam_balance = db.get_balance( sam_id, asset_id_type() ).amount.value;

      ticket_id_type tick_1_id; // default value

      // upgrade the ticket
      auto result = update_ticket( tick_1_id(db), lock_forever, {} );
      BOOST_CHECK_EQUAL( result.new_objects.size(), 0u );

      BOOST_CHECK( tick_1_id(db).target_type == lock_forever );
      BOOST_CHECK( tick_1_id(db).current_type == lock_180_days );
      BOOST_CHECK( tick_1_id(db).status == charging );
      BOOST_CHECK( tick_1_id(db).amount == asset(100) );
      BOOST_CHECK_EQUAL( tick_1_id(db).value.value, 100 * 2 );
      BOOST_CHECK_EQUAL( db.get_balance( sam_id, asset_id_type() ).amount.value, sam_balance );

      // 14 days passed
      generate_blocks( db.head_block_time() + fc::days(14) );
      set_expiration( db, trx );

      // no change
      BOOST_CHECK( tick_1_id(db).target_type == lock_forever );
      BOOST_CHECK( tick_1_id(db).current_type == lock_180_days );
      BOOST_CHECK( tick_1_id(db).status == charging );
      BOOST_CHECK( tick_1_id(db).amount == asset(100) );
      BOOST_CHECK_EQUAL( tick_1_id(db).value.value, 100 * 2 );
      BOOST_CHECK_EQUAL( db.get_balance( sam_id, asset_id_type() ).amount.value, sam_balance );

      // 1 day passed
      generate_blocks( db.head_block_time() + fc::days(1) );
      set_expiration( db, trx );

      // the ticket should be upgraded
      BOOST_CHECK( tick_1_id(db).target_type == lock_forever );
      BOOST_CHECK( tick_1_id(db).current_type == lock_360_days );
      BOOST_CHECK( tick_1_id(db).status == charging );
      BOOST_CHECK( tick_1_id(db).amount == asset(100) );
      BOOST_CHECK_EQUAL( tick_1_id(db).value.value, 100 * 4 );
      BOOST_CHECK_EQUAL( db.get_balance( sam_id, asset_id_type() ).amount.value, sam_balance );

      // 14 days passed
      generate_blocks( db.head_block_time() + fc::days(14) );
      set_expiration( db, trx );

      // no change
      BOOST_CHECK( tick_1_id(db).target_type == lock_forever );
      BOOST_CHECK( tick_1_id(db).current_type == lock_360_days );
      BOOST_CHECK( tick_1_id(db).status == charging );
      BOOST_CHECK( tick_1_id(db).amount == asset(100) );
      BOOST_CHECK_EQUAL( tick_1_id(db).value.value, 100 * 4 );
      BOOST_CHECK_EQUAL( db.get_balance( sam_id, asset_id_type() ).amount.value, sam_balance );

      // 1 day passed
      generate_blocks( db.head_block_time() + fc::days(1) );
      set_expiration( db, trx );

      // ticket should be upgraded now, and still charging
      BOOST_CHECK( tick_1_id(db).target_type == lock_forever );
      BOOST_CHECK( tick_1_id(db).current_type == lock_720_days );
      BOOST_CHECK( tick_1_id(db).status == charging );
      BOOST_CHECK( tick_1_id(db).amount == asset(100) );
      BOOST_CHECK_EQUAL( tick_1_id(db).value.value, 100 * 8 );
      BOOST_CHECK_EQUAL( db.get_balance( sam_id, asset_id_type() ).amount.value, sam_balance );

      // 14 days passed
      generate_blocks( db.head_block_time() + fc::days(14) );
      set_expiration( db, trx );

      // no change
      BOOST_CHECK( tick_1_id(db).target_type == lock_forever );
      BOOST_CHECK( tick_1_id(db).current_type == lock_720_days );
      BOOST_CHECK( tick_1_id(db).status == charging );
      BOOST_CHECK( tick_1_id(db).amount == asset(100) );
      BOOST_CHECK_EQUAL( tick_1_id(db).value.value, 100 * 8 );
      BOOST_CHECK_EQUAL( db.get_balance( sam_id, asset_id_type() ).amount.value, sam_balance );

      // 1 day passed
      generate_blocks( db.head_block_time() + fc::days(1) );
      set_expiration( db, trx );

      // ticket should have reached the target
      BOOST_CHECK( tick_1_id(db).target_type == lock_forever );
      BOOST_CHECK( tick_1_id(db).current_type == lock_forever );
      BOOST_CHECK( tick_1_id(db).status == withdrawing );
      BOOST_CHECK( tick_1_id(db).amount == asset(100) );
      BOOST_CHECK_EQUAL( tick_1_id(db).value.value, 100 * 8 );
      BOOST_CHECK_EQUAL( db.get_balance( sam_id, asset_id_type() ).amount.value, sam_balance );

   } FC_LOG_AND_RETHROW()
}

BOOST_AUTO_TEST_CASE( upgrade_lock_360_ticket_to_720 )
{ try {

      INVOKE( one_lock_360_ticket );

      GET_ACTOR( sam );

      int64_t sam_balance = db.get_balance( sam_id, asset_id_type() ).amount.value;

      ticket_id_type tick_1_id; // default value

      // upgrade the ticket
      auto result = update_ticket( tick_1_id(db), lock_720_days, {} );
      BOOST_CHECK_EQUAL( result.new_objects.size(), 0u );

      BOOST_CHECK( tick_1_id(db).target_type == lock_720_days );
      BOOST_CHECK( tick_1_id(db).current_type == lock_360_days );
      BOOST_CHECK( tick_1_id(db).status == charging );
      BOOST_CHECK( tick_1_id(db).amount == asset(100) );
      BOOST_CHECK_EQUAL( tick_1_id(db).value.value, 100 * 4 );
      BOOST_CHECK_EQUAL( db.get_balance( sam_id, asset_id_type() ).amount.value, sam_balance );

      // 14 days passed
      generate_blocks( db.head_block_time() + fc::days(14) );
      set_expiration( db, trx );

      // no change
      BOOST_CHECK( tick_1_id(db).target_type == lock_720_days );
      BOOST_CHECK( tick_1_id(db).current_type == lock_360_days );
      BOOST_CHECK( tick_1_id(db).status == charging );
      BOOST_CHECK( tick_1_id(db).amount == asset(100) );
      BOOST_CHECK_EQUAL( tick_1_id(db).value.value, 100 * 4 );
      BOOST_CHECK_EQUAL( db.get_balance( sam_id, asset_id_type() ).amount.value, sam_balance );

      // 1 day passed
      generate_blocks( db.head_block_time() + fc::days(1) );
      set_expiration( db, trx );

      // the ticket should be upgraded
      BOOST_CHECK( tick_1_id(db).target_type == lock_720_days );
      BOOST_CHECK( tick_1_id(db).current_type == lock_720_days );
      BOOST_CHECK( tick_1_id(db).status == stable );
      BOOST_CHECK( tick_1_id(db).next_auto_update_time == time_point_sec::maximum() );
      BOOST_CHECK( tick_1_id(db).next_type_downgrade_time == time_point_sec::maximum() );
      BOOST_CHECK( tick_1_id(db).amount == asset(100) );
      BOOST_CHECK_EQUAL( tick_1_id(db).value.value, 100 * 8 );
      BOOST_CHECK_EQUAL( db.get_balance( sam_id, asset_id_type() ).amount.value, sam_balance );

      // 1 day passed
      generate_blocks( db.head_block_time() + fc::days(1) );
      set_expiration( db, trx );

      // no change
      BOOST_CHECK( tick_1_id(db).target_type == lock_720_days );
      BOOST_CHECK( tick_1_id(db).current_type == lock_720_days );
      BOOST_CHECK( tick_1_id(db).status == stable );
      BOOST_CHECK( tick_1_id(db).next_auto_update_time == time_point_sec::maximum() );
      BOOST_CHECK( tick_1_id(db).next_type_downgrade_time == time_point_sec::maximum() );
      BOOST_CHECK( tick_1_id(db).amount == asset(100) );
      BOOST_CHECK_EQUAL( tick_1_id(db).value.value, 100 * 8 );
      BOOST_CHECK_EQUAL( db.get_balance( sam_id, asset_id_type() ).amount.value, sam_balance );

   } FC_LOG_AND_RETHROW()
}

BOOST_AUTO_TEST_CASE( upgrade_lock_360_ticket_to_forever )
{ try {

      INVOKE( one_lock_360_ticket );

      GET_ACTOR( sam );

      int64_t sam_balance = db.get_balance( sam_id, asset_id_type() ).amount.value;

      ticket_id_type tick_1_id; // default value

      // upgrade the ticket
      auto result = update_ticket( tick_1_id(db), lock_forever, {} );
      BOOST_CHECK_EQUAL( result.new_objects.size(), 0u );

      BOOST_CHECK( tick_1_id(db).target_type == lock_forever );
      BOOST_CHECK( tick_1_id(db).current_type == lock_360_days );
      BOOST_CHECK( tick_1_id(db).status == charging );
      BOOST_CHECK( tick_1_id(db).amount == asset(100) );
      BOOST_CHECK_EQUAL( tick_1_id(db).value.value, 100 * 4 );
      BOOST_CHECK_EQUAL( db.get_balance( sam_id, asset_id_type() ).amount.value, sam_balance );

      // 14 days passed
      generate_blocks( db.head_block_time() + fc::days(14) );
      set_expiration( db, trx );

      // no change
      BOOST_CHECK( tick_1_id(db).target_type == lock_forever );
      BOOST_CHECK( tick_1_id(db).current_type == lock_360_days );
      BOOST_CHECK( tick_1_id(db).status == charging );
      BOOST_CHECK( tick_1_id(db).amount == asset(100) );
      BOOST_CHECK_EQUAL( tick_1_id(db).value.value, 100 * 4 );
      BOOST_CHECK_EQUAL( db.get_balance( sam_id, asset_id_type() ).amount.value, sam_balance );

      // 1 day passed
      generate_blocks( db.head_block_time() + fc::days(1) );
      set_expiration( db, trx );

      // ticket should be upgraded now, and still charging
      BOOST_CHECK( tick_1_id(db).target_type == lock_forever );
      BOOST_CHECK( tick_1_id(db).current_type == lock_720_days );
      BOOST_CHECK( tick_1_id(db).status == charging );
      BOOST_CHECK( tick_1_id(db).amount == asset(100) );
      BOOST_CHECK_EQUAL( tick_1_id(db).value.value, 100 * 8 );
      BOOST_CHECK_EQUAL( db.get_balance( sam_id, asset_id_type() ).amount.value, sam_balance );

      // 14 days passed
      generate_blocks( db.head_block_time() + fc::days(14) );
      set_expiration( db, trx );

      // no change
      BOOST_CHECK( tick_1_id(db).target_type == lock_forever );
      BOOST_CHECK( tick_1_id(db).current_type == lock_720_days );
      BOOST_CHECK( tick_1_id(db).status == charging );
      BOOST_CHECK( tick_1_id(db).amount == asset(100) );
      BOOST_CHECK_EQUAL( tick_1_id(db).value.value, 100 * 8 );
      BOOST_CHECK_EQUAL( db.get_balance( sam_id, asset_id_type() ).amount.value, sam_balance );

      // 1 day passed
      generate_blocks( db.head_block_time() + fc::days(1) );
      set_expiration( db, trx );

      // ticket should have reached the target
      BOOST_CHECK( tick_1_id(db).target_type == lock_forever );
      BOOST_CHECK( tick_1_id(db).current_type == lock_forever );
      BOOST_CHECK( tick_1_id(db).status == withdrawing );
      BOOST_CHECK( tick_1_id(db).amount == asset(100) );
      BOOST_CHECK_EQUAL( tick_1_id(db).value.value, 100 * 8 );
      BOOST_CHECK_EQUAL( db.get_balance( sam_id, asset_id_type() ).amount.value, sam_balance );

   } FC_LOG_AND_RETHROW()
}

BOOST_AUTO_TEST_CASE( upgrade_lock_720_ticket_to_forever )
{ try {

      INVOKE( one_lock_720_ticket );

      GET_ACTOR( sam );

      int64_t sam_balance = db.get_balance( sam_id, asset_id_type() ).amount.value;

      ticket_id_type tick_1_id; // default value

      // upgrade the ticket
      auto result = update_ticket( tick_1_id(db), lock_forever, {} );
      BOOST_CHECK_EQUAL( result.new_objects.size(), 0u );

      BOOST_CHECK( tick_1_id(db).target_type == lock_forever );
      BOOST_CHECK( tick_1_id(db).current_type == lock_720_days );
      BOOST_CHECK( tick_1_id(db).status == charging );
      BOOST_CHECK( tick_1_id(db).amount == asset(100) );
      BOOST_CHECK_EQUAL( tick_1_id(db).value.value, 100 * 8 );
      BOOST_CHECK_EQUAL( db.get_balance( sam_id, asset_id_type() ).amount.value, sam_balance );

      // 14 days passed
      generate_blocks( db.head_block_time() + fc::days(14) );
      set_expiration( db, trx );

      // no change
      BOOST_CHECK( tick_1_id(db).target_type == lock_forever );
      BOOST_CHECK( tick_1_id(db).current_type == lock_720_days );
      BOOST_CHECK( tick_1_id(db).status == charging );
      BOOST_CHECK( tick_1_id(db).amount == asset(100) );
      BOOST_CHECK_EQUAL( tick_1_id(db).value.value, 100 * 8 );
      BOOST_CHECK_EQUAL( db.get_balance( sam_id, asset_id_type() ).amount.value, sam_balance );

      // 1 day passed
      generate_blocks( db.head_block_time() + fc::days(1) );
      set_expiration( db, trx );

      // ticket should have reached the target
      BOOST_CHECK( tick_1_id(db).target_type == lock_forever );
      BOOST_CHECK( tick_1_id(db).current_type == lock_forever );
      BOOST_CHECK( tick_1_id(db).status == withdrawing );
      BOOST_CHECK( tick_1_id(db).amount == asset(100) );
      BOOST_CHECK_EQUAL( tick_1_id(db).value.value, 100 * 8 );
      BOOST_CHECK_EQUAL( db.get_balance( sam_id, asset_id_type() ).amount.value, sam_balance );

   } FC_LOG_AND_RETHROW()
}

BOOST_AUTO_TEST_CASE( withdraw_lock_180_ticket )
{ try {

      INVOKE( one_lock_180_ticket );

      GET_ACTOR( sam );

      int64_t sam_balance = db.get_balance( sam_id, asset_id_type() ).amount.value;

      ticket_id_type tick_1_id; // default value

      // withdraw the ticket
      auto result = update_ticket( tick_1_id(db), liquid, {} );
      BOOST_CHECK_EQUAL( result.new_objects.size(), 0u );

      BOOST_CHECK( tick_1_id(db).target_type == liquid );
      BOOST_CHECK( tick_1_id(db).current_type == liquid );
      BOOST_CHECK( tick_1_id(db).status == withdrawing );
      BOOST_CHECK( tick_1_id(db).amount == asset(100) );
      BOOST_CHECK_EQUAL( tick_1_id(db).value.value, 100 );
      BOOST_CHECK_EQUAL( db.get_balance( sam_id, asset_id_type() ).amount.value, sam_balance );

      // 179 days passed
      generate_blocks( db.head_block_time() + fc::days(179) );
      set_expiration( db, trx );

      // no change
      bool has_hf_2262 = ( HARDFORK_CORE_2262_PASSED( db.get_dynamic_global_properties().next_maintenance_time ) );
      BOOST_CHECK( tick_1_id(db).target_type == liquid );
      BOOST_CHECK( tick_1_id(db).current_type == liquid );
      BOOST_CHECK( tick_1_id(db).status == withdrawing );
      BOOST_CHECK( tick_1_id(db).amount == asset(100) );
      BOOST_CHECK_EQUAL( tick_1_id(db).value.value, has_hf_2262 ? 0 : 100 );
      BOOST_CHECK_EQUAL( db.get_balance( sam_id, asset_id_type() ).amount.value, sam_balance );

      // 1 day passed
      generate_blocks( db.head_block_time() + fc::days(1) );
      set_expiration( db, trx );

      // the ticket should be freed
      BOOST_CHECK( !db.find( tick_1_id ) );

      BOOST_CHECK_EQUAL( db.get_balance( sam_id, asset_id_type() ).amount.value, sam_balance + 100 );

   } FC_LOG_AND_RETHROW()
}

BOOST_AUTO_TEST_CASE( withdraw_lock_360_ticket )
{ try {

      INVOKE( one_lock_360_ticket );

      GET_ACTOR( sam );

      int64_t sam_balance = db.get_balance( sam_id, asset_id_type() ).amount.value;

      ticket_id_type tick_1_id; // default value

      // withdraw the ticket
      auto result = update_ticket( tick_1_id(db), liquid, {} );
      BOOST_CHECK_EQUAL( result.new_objects.size(), 0u );

      BOOST_CHECK( tick_1_id(db).target_type == liquid );
      BOOST_CHECK( tick_1_id(db).current_type == lock_180_days );
      BOOST_CHECK( tick_1_id(db).status == withdrawing );
      BOOST_CHECK( tick_1_id(db).amount == asset(100) );
      BOOST_CHECK_EQUAL( tick_1_id(db).value.value, 100 * 2 );
      BOOST_CHECK_EQUAL( db.get_balance( sam_id, asset_id_type() ).amount.value, sam_balance );

      // 179 days passed
      generate_blocks( db.head_block_time() + fc::days(179) );
      set_expiration( db, trx );

      // no change
      BOOST_CHECK( tick_1_id(db).target_type == liquid );
      BOOST_CHECK( tick_1_id(db).current_type == lock_180_days );
      BOOST_CHECK( tick_1_id(db).status == withdrawing );
      BOOST_CHECK( tick_1_id(db).amount == asset(100) );
      BOOST_CHECK_EQUAL( tick_1_id(db).value.value, 100  * 2 );
      BOOST_CHECK_EQUAL( db.get_balance( sam_id, asset_id_type() ).amount.value, sam_balance );

      // 1 day passed
      generate_blocks( db.head_block_time() + fc::days(1) );
      set_expiration( db, trx );

      // the ticket should have downgraded
      bool has_hf_2262 = ( HARDFORK_CORE_2262_PASSED( db.get_dynamic_global_properties().next_maintenance_time ) );
      BOOST_CHECK( tick_1_id(db).target_type == liquid );
      BOOST_CHECK( tick_1_id(db).current_type == liquid );
      BOOST_CHECK( tick_1_id(db).status == withdrawing );
      BOOST_CHECK( tick_1_id(db).amount == asset(100) );
      BOOST_CHECK_EQUAL( tick_1_id(db).value.value, has_hf_2262 ? 0 : 100 );
      BOOST_CHECK_EQUAL( db.get_balance( sam_id, asset_id_type() ).amount.value, sam_balance );

      // 179 days passed
      generate_blocks( db.head_block_time() + fc::days(179) );
      set_expiration( db, trx );

      // no change
      has_hf_2262 = ( HARDFORK_CORE_2262_PASSED( db.get_dynamic_global_properties().next_maintenance_time ) );
      BOOST_CHECK( tick_1_id(db).target_type == liquid );
      BOOST_CHECK( tick_1_id(db).current_type == liquid );
      BOOST_CHECK( tick_1_id(db).status == withdrawing );
      BOOST_CHECK( tick_1_id(db).amount == asset(100) );
      BOOST_CHECK_EQUAL( tick_1_id(db).value.value, has_hf_2262 ? 0 : 100 );
      BOOST_CHECK_EQUAL( db.get_balance( sam_id, asset_id_type() ).amount.value, sam_balance );

      // 1 day passed
      generate_blocks( db.head_block_time() + fc::days(1) );
      set_expiration( db, trx );

      // the ticket should be freed
      BOOST_CHECK( !db.find( tick_1_id ) );

      BOOST_CHECK_EQUAL( db.get_balance( sam_id, asset_id_type() ).amount.value, sam_balance + 100 );

   } FC_LOG_AND_RETHROW()
}

BOOST_AUTO_TEST_CASE( withdraw_lock_720_ticket )
{ try {

      INVOKE( one_lock_720_ticket );

      GET_ACTOR( sam );

      int64_t sam_balance = db.get_balance( sam_id, asset_id_type() ).amount.value;

      ticket_id_type tick_1_id; // default value

      // withdraw the ticket
      auto result = update_ticket( tick_1_id(db), liquid, {} );
      BOOST_CHECK_EQUAL( result.new_objects.size(), 0u );

      BOOST_CHECK( tick_1_id(db).target_type == liquid );
      BOOST_CHECK( tick_1_id(db).current_type == lock_360_days );
      BOOST_CHECK( tick_1_id(db).status == withdrawing );
      BOOST_CHECK( tick_1_id(db).amount == asset(100) );
      BOOST_CHECK_EQUAL( tick_1_id(db).value.value, 100 * 4 );
      BOOST_CHECK_EQUAL( db.get_balance( sam_id, asset_id_type() ).amount.value, sam_balance );

      // unable to update ticket if not to change target type
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), liquid, {} ), fc::exception );
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), liquid, asset(1) ), fc::exception );
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), liquid, asset(100) ), fc::exception );

      // 359 days passed
      generate_blocks( db.head_block_time() + fc::days(359) );
      set_expiration( db, trx );

      // no change
      BOOST_CHECK( tick_1_id(db).target_type == liquid );
      BOOST_CHECK( tick_1_id(db).current_type == lock_360_days );
      BOOST_CHECK( tick_1_id(db).status == withdrawing );
      BOOST_CHECK( tick_1_id(db).amount == asset(100) );
      BOOST_CHECK_EQUAL( tick_1_id(db).value.value, 100  * 4 );
      BOOST_CHECK_EQUAL( db.get_balance( sam_id, asset_id_type() ).amount.value, sam_balance );

      // unable to update ticket if not to change target type
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), liquid, {} ), fc::exception );
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), liquid, asset(1) ), fc::exception );
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), liquid, asset(100) ), fc::exception );

      // 1 day passed
      generate_blocks( db.head_block_time() + fc::days(1) );
      set_expiration( db, trx );

      // the ticket should have downgraded
      BOOST_CHECK( tick_1_id(db).target_type == liquid );
      BOOST_CHECK( tick_1_id(db).current_type == lock_180_days );
      BOOST_CHECK( tick_1_id(db).status == withdrawing );
      BOOST_CHECK( tick_1_id(db).amount == asset(100) );
      BOOST_CHECK_EQUAL( tick_1_id(db).value.value, 100 * 2 );
      BOOST_CHECK_EQUAL( db.get_balance( sam_id, asset_id_type() ).amount.value, sam_balance );

      // unable to update ticket if not to change target type
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), liquid, {} ), fc::exception );
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), liquid, asset(1) ), fc::exception );
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), liquid, asset(100) ), fc::exception );

      // 179 days passed
      generate_blocks( db.head_block_time() + fc::days(179) );
      set_expiration( db, trx );

      // no change
      BOOST_CHECK( tick_1_id(db).target_type == liquid );
      BOOST_CHECK( tick_1_id(db).current_type == lock_180_days );
      BOOST_CHECK( tick_1_id(db).status == withdrawing );
      BOOST_CHECK( tick_1_id(db).amount == asset(100) );
      BOOST_CHECK_EQUAL( tick_1_id(db).value.value, 100  * 2 );
      BOOST_CHECK_EQUAL( db.get_balance( sam_id, asset_id_type() ).amount.value, sam_balance );

      // unable to update ticket if not to change target type
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), liquid, {} ), fc::exception );
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), liquid, asset(1) ), fc::exception );
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), liquid, asset(100) ), fc::exception );

      // 1 day passed
      generate_blocks( db.head_block_time() + fc::days(1) );
      set_expiration( db, trx );

      // the ticket should have downgraded
      bool has_hf_2262 = ( HARDFORK_CORE_2262_PASSED( db.get_dynamic_global_properties().next_maintenance_time ) );
      BOOST_CHECK( tick_1_id(db).target_type == liquid );
      BOOST_CHECK( tick_1_id(db).current_type == liquid );
      BOOST_CHECK( tick_1_id(db).status == withdrawing );
      BOOST_CHECK( tick_1_id(db).amount == asset(100) );
      BOOST_CHECK_EQUAL( tick_1_id(db).value.value, has_hf_2262 ? 0 : 100 );
      BOOST_CHECK_EQUAL( db.get_balance( sam_id, asset_id_type() ).amount.value, sam_balance );

      // unable to update ticket if not to change target type
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), liquid, {} ), fc::exception );
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), liquid, asset(1) ), fc::exception );
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), liquid, asset(100) ), fc::exception );

      // 179 days passed
      generate_blocks( db.head_block_time() + fc::days(179) );
      set_expiration( db, trx );

      // no change
      has_hf_2262 = ( HARDFORK_CORE_2262_PASSED( db.get_dynamic_global_properties().next_maintenance_time ) );
      BOOST_CHECK( tick_1_id(db).target_type == liquid );
      BOOST_CHECK( tick_1_id(db).current_type == liquid );
      BOOST_CHECK( tick_1_id(db).status == withdrawing );
      BOOST_CHECK( tick_1_id(db).amount == asset(100) );
      BOOST_CHECK_EQUAL( tick_1_id(db).value.value, has_hf_2262 ? 0 : 100 );
      BOOST_CHECK_EQUAL( db.get_balance( sam_id, asset_id_type() ).amount.value, sam_balance );

      // unable to update ticket if not to change target type
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), liquid, {} ), fc::exception );
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), liquid, asset(1) ), fc::exception );
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), liquid, asset(100) ), fc::exception );

      // 1 day passed
      generate_blocks( db.head_block_time() + fc::days(1) );
      set_expiration( db, trx );

      // the ticket should be freed
      BOOST_CHECK( !db.find( tick_1_id ) );

      BOOST_CHECK_EQUAL( db.get_balance( sam_id, asset_id_type() ).amount.value, sam_balance + 100 );

   } FC_LOG_AND_RETHROW()
}

BOOST_AUTO_TEST_CASE( withdraw_lock_720_ticket_if_blocks_missed )
{ try {

      INVOKE( one_lock_720_ticket );

      GET_ACTOR( sam );

      int64_t sam_balance = db.get_balance( sam_id, asset_id_type() ).amount.value;

      ticket_id_type tick_1_id; // default value

      // withdraw the ticket
      auto result = update_ticket( tick_1_id(db), liquid, {} );
      BOOST_CHECK_EQUAL( result.new_objects.size(), 0u );

      BOOST_CHECK( tick_1_id(db).target_type == liquid );
      BOOST_CHECK( tick_1_id(db).current_type == lock_360_days );
      BOOST_CHECK( tick_1_id(db).status == withdrawing );
      BOOST_CHECK( tick_1_id(db).amount == asset(100) );
      BOOST_CHECK_EQUAL( tick_1_id(db).value.value, 100 * 4 );
      BOOST_CHECK_EQUAL( db.get_balance( sam_id, asset_id_type() ).amount.value, sam_balance );

      // unable to update ticket if not to change target type
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), liquid, {} ), fc::exception );
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), liquid, asset(1) ), fc::exception );
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), liquid, asset(100) ), fc::exception );

      // 900 days passed
      generate_blocks( db.head_block_time() + fc::days(900) );
      set_expiration( db, trx );

      // the ticket should be freed
      BOOST_CHECK( !db.find( tick_1_id ) );

      BOOST_CHECK_EQUAL( db.get_balance( sam_id, asset_id_type() ).amount.value, sam_balance + 100 );

   } FC_LOG_AND_RETHROW()
}

BOOST_AUTO_TEST_CASE( downgrade_lock_720_ticket_to_180 )
{ try {

      INVOKE( one_lock_720_ticket );

      GET_ACTOR( sam );

      int64_t sam_balance = db.get_balance( sam_id, asset_id_type() ).amount.value;

      ticket_id_type tick_1_id; // default value

      // withdraw the ticket
      auto result = update_ticket( tick_1_id(db), lock_180_days, {} );
      BOOST_CHECK_EQUAL( result.new_objects.size(), 0u );

      BOOST_CHECK( tick_1_id(db).target_type == lock_180_days );
      BOOST_CHECK( tick_1_id(db).current_type == lock_360_days );
      BOOST_CHECK( tick_1_id(db).status == withdrawing );
      BOOST_CHECK( tick_1_id(db).amount == asset(100) );
      BOOST_CHECK_EQUAL( tick_1_id(db).value.value, 100 * 4 );
      BOOST_CHECK_EQUAL( db.get_balance( sam_id, asset_id_type() ).amount.value, sam_balance );

      // unable to update ticket if not to change target type
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_180_days, {} ), fc::exception );
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_180_days, asset(1) ), fc::exception );
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_180_days, asset(100) ), fc::exception );

      // 359 days passed
      generate_blocks( db.head_block_time() + fc::days(359) );
      set_expiration( db, trx );

      // no change
      BOOST_CHECK( tick_1_id(db).target_type == lock_180_days );
      BOOST_CHECK( tick_1_id(db).current_type == lock_360_days );
      BOOST_CHECK( tick_1_id(db).status == withdrawing );
      BOOST_CHECK( tick_1_id(db).amount == asset(100) );
      BOOST_CHECK_EQUAL( tick_1_id(db).value.value, 100  * 4 );
      BOOST_CHECK_EQUAL( db.get_balance( sam_id, asset_id_type() ).amount.value, sam_balance );

      // unable to update ticket if not to change target type
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_180_days, {} ), fc::exception );
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_180_days, asset(1) ), fc::exception );
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_180_days, asset(100) ), fc::exception );

      // 1 day passed
      generate_blocks( db.head_block_time() + fc::days(1) );
      set_expiration( db, trx );

      // the ticket should have downgraded
      BOOST_CHECK( tick_1_id(db).target_type == lock_180_days );
      BOOST_CHECK( tick_1_id(db).current_type == lock_180_days );
      BOOST_CHECK( tick_1_id(db).status == withdrawing );
      BOOST_CHECK( tick_1_id(db).amount == asset(100) );
      BOOST_CHECK_EQUAL( tick_1_id(db).value.value, 100 * 2 );
      BOOST_CHECK_EQUAL( db.get_balance( sam_id, asset_id_type() ).amount.value, sam_balance );

      // unable to update ticket if not to change target type
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_180_days, {} ), fc::exception );
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_180_days, asset(1) ), fc::exception );
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_180_days, asset(100) ), fc::exception );

      // 179 days passed
      generate_blocks( db.head_block_time() + fc::days(179) );
      set_expiration( db, trx );

      // no change
      BOOST_CHECK( tick_1_id(db).target_type == lock_180_days );
      BOOST_CHECK( tick_1_id(db).current_type == lock_180_days );
      BOOST_CHECK( tick_1_id(db).status == withdrawing );
      BOOST_CHECK( tick_1_id(db).amount == asset(100) );
      BOOST_CHECK_EQUAL( tick_1_id(db).value.value, 100  * 2 );
      BOOST_CHECK_EQUAL( db.get_balance( sam_id, asset_id_type() ).amount.value, sam_balance );

      // unable to update ticket if not to change target type
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_180_days, {} ), fc::exception );
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_180_days, asset(1) ), fc::exception );
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_180_days, asset(100) ), fc::exception );

      // 1 day passed
      generate_blocks( db.head_block_time() + fc::days(1) );
      set_expiration( db, trx );

      // the ticket should have downgraded, and is stable now
      BOOST_CHECK( tick_1_id(db).target_type == lock_180_days );
      BOOST_CHECK( tick_1_id(db).current_type == lock_180_days );
      BOOST_CHECK( tick_1_id(db).status == stable );
      BOOST_CHECK( tick_1_id(db).next_auto_update_time == time_point_sec::maximum() );
      BOOST_CHECK( tick_1_id(db).next_type_downgrade_time == time_point_sec::maximum() );
      BOOST_CHECK( tick_1_id(db).amount == asset(100) );
      BOOST_CHECK_EQUAL( tick_1_id(db).value.value, 100 * 2 );
      BOOST_CHECK_EQUAL( db.get_balance( sam_id, asset_id_type() ).amount.value, sam_balance );

      // unable to update ticket if not to change target type
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_180_days, {} ), fc::exception );
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_180_days, asset(1) ), fc::exception );
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_180_days, asset(100) ), fc::exception );

      // 1 day passed
      generate_blocks( db.head_block_time() + fc::days(1) );
      set_expiration( db, trx );

      // no change
      BOOST_CHECK( tick_1_id(db).target_type == lock_180_days );
      BOOST_CHECK( tick_1_id(db).current_type == lock_180_days );
      BOOST_CHECK( tick_1_id(db).status == stable );
      BOOST_CHECK( tick_1_id(db).next_auto_update_time == time_point_sec::maximum() );
      BOOST_CHECK( tick_1_id(db).next_type_downgrade_time == time_point_sec::maximum() );
      BOOST_CHECK( tick_1_id(db).amount == asset(100) );
      BOOST_CHECK_EQUAL( tick_1_id(db).value.value, 100 * 2 );
      BOOST_CHECK_EQUAL( db.get_balance( sam_id, asset_id_type() ).amount.value, sam_balance );

      // unable to update ticket if not to change target type
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_180_days, {} ), fc::exception );
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_180_days, asset(1) ), fc::exception );
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_180_days, asset(100) ), fc::exception );

   } FC_LOG_AND_RETHROW()
}

BOOST_AUTO_TEST_CASE( downgrade_lock_720_ticket_to_360 )
{ try {

      INVOKE( one_lock_720_ticket );

      GET_ACTOR( sam );

      int64_t sam_balance = db.get_balance( sam_id, asset_id_type() ).amount.value;

      ticket_id_type tick_1_id; // default value

      // withdraw the ticket
      auto result = update_ticket( tick_1_id(db), lock_360_days, {} );
      BOOST_CHECK_EQUAL( result.new_objects.size(), 0u );

      BOOST_CHECK( tick_1_id(db).target_type == lock_360_days );
      BOOST_CHECK( tick_1_id(db).current_type == lock_360_days );
      BOOST_CHECK( tick_1_id(db).status == withdrawing );
      BOOST_CHECK( tick_1_id(db).amount == asset(100) );
      BOOST_CHECK_EQUAL( tick_1_id(db).value.value, 100 * 4 );
      BOOST_CHECK_EQUAL( db.get_balance( sam_id, asset_id_type() ).amount.value, sam_balance );

      // unable to update ticket if not to change target type
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_360_days, {} ), fc::exception );
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_360_days, asset(1) ), fc::exception );
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_360_days, asset(100) ), fc::exception );

      // 359 days passed
      generate_blocks( db.head_block_time() + fc::days(359) );
      set_expiration( db, trx );

      // no change
      BOOST_CHECK( tick_1_id(db).target_type == lock_360_days );
      BOOST_CHECK( tick_1_id(db).current_type == lock_360_days );
      BOOST_CHECK( tick_1_id(db).status == withdrawing );
      BOOST_CHECK( tick_1_id(db).amount == asset(100) );
      BOOST_CHECK_EQUAL( tick_1_id(db).value.value, 100  * 4 );
      BOOST_CHECK_EQUAL( db.get_balance( sam_id, asset_id_type() ).amount.value, sam_balance );

      // unable to update ticket if not to change target type
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_360_days, {} ), fc::exception );
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_360_days, asset(1) ), fc::exception );
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_360_days, asset(100) ), fc::exception );

      // 1 day passed
      generate_blocks( db.head_block_time() + fc::days(1) );
      set_expiration( db, trx );

      // the ticket should have downgraded, and is stable now
      BOOST_CHECK( tick_1_id(db).target_type == lock_360_days );
      BOOST_CHECK( tick_1_id(db).current_type == lock_360_days );
      BOOST_CHECK( tick_1_id(db).status == stable );
      BOOST_CHECK( tick_1_id(db).next_auto_update_time == time_point_sec::maximum() );
      BOOST_CHECK( tick_1_id(db).next_type_downgrade_time == time_point_sec::maximum() );
      BOOST_CHECK( tick_1_id(db).amount == asset(100) );
      BOOST_CHECK_EQUAL( tick_1_id(db).value.value, 100 * 4 );
      BOOST_CHECK_EQUAL( db.get_balance( sam_id, asset_id_type() ).amount.value, sam_balance );

      // unable to update ticket if not to change target type
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_360_days, {} ), fc::exception );
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_360_days, asset(1) ), fc::exception );
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_360_days, asset(100) ), fc::exception );

      // 1 day passed
      generate_blocks( db.head_block_time() + fc::days(1) );
      set_expiration( db, trx );

      // no change
      BOOST_CHECK( tick_1_id(db).target_type == lock_360_days );
      BOOST_CHECK( tick_1_id(db).current_type == lock_360_days );
      BOOST_CHECK( tick_1_id(db).status == stable );
      BOOST_CHECK( tick_1_id(db).next_auto_update_time == time_point_sec::maximum() );
      BOOST_CHECK( tick_1_id(db).next_type_downgrade_time == time_point_sec::maximum() );
      BOOST_CHECK( tick_1_id(db).amount == asset(100) );
      BOOST_CHECK_EQUAL( tick_1_id(db).value.value, 100 * 4 );
      BOOST_CHECK_EQUAL( db.get_balance( sam_id, asset_id_type() ).amount.value, sam_balance );

      // unable to update ticket if not to change target type
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_360_days, {} ), fc::exception );
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_360_days, asset(1) ), fc::exception );
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_360_days, asset(100) ), fc::exception );

   } FC_LOG_AND_RETHROW()
}

BOOST_AUTO_TEST_CASE( downgrade_lock_360_ticket_to_180 )
{ try {

      INVOKE( one_lock_360_ticket );

      GET_ACTOR( sam );

      int64_t sam_balance = db.get_balance( sam_id, asset_id_type() ).amount.value;

      ticket_id_type tick_1_id; // default value

      // withdraw the ticket
      auto result = update_ticket( tick_1_id(db), lock_180_days, {} );
      BOOST_CHECK_EQUAL( result.new_objects.size(), 0u );

      BOOST_CHECK( tick_1_id(db).target_type == lock_180_days );
      BOOST_CHECK( tick_1_id(db).current_type == lock_180_days );
      BOOST_CHECK( tick_1_id(db).status == withdrawing );
      BOOST_CHECK( tick_1_id(db).amount == asset(100) );
      BOOST_CHECK_EQUAL( tick_1_id(db).value.value, 100 * 2 );
      BOOST_CHECK_EQUAL( db.get_balance( sam_id, asset_id_type() ).amount.value, sam_balance );

      // 179 days passed
      generate_blocks( db.head_block_time() + fc::days(179) );
      set_expiration( db, trx );

      // no change
      BOOST_CHECK( tick_1_id(db).target_type == lock_180_days );
      BOOST_CHECK( tick_1_id(db).current_type == lock_180_days );
      BOOST_CHECK( tick_1_id(db).status == withdrawing );
      BOOST_CHECK( tick_1_id(db).amount == asset(100) );
      BOOST_CHECK_EQUAL( tick_1_id(db).value.value, 100  * 2 );
      BOOST_CHECK_EQUAL( db.get_balance( sam_id, asset_id_type() ).amount.value, sam_balance );

      // 1 day passed
      generate_blocks( db.head_block_time() + fc::days(1) );
      set_expiration( db, trx );

      // the ticket should have downgraded, and is stable now
      BOOST_CHECK( tick_1_id(db).target_type == lock_180_days );
      BOOST_CHECK( tick_1_id(db).current_type == lock_180_days );
      BOOST_CHECK( tick_1_id(db).status == stable );
      BOOST_CHECK( tick_1_id(db).next_auto_update_time == time_point_sec::maximum() );
      BOOST_CHECK( tick_1_id(db).next_type_downgrade_time == time_point_sec::maximum() );
      BOOST_CHECK( tick_1_id(db).amount == asset(100) );
      BOOST_CHECK_EQUAL( tick_1_id(db).value.value, 100 * 2 );
      BOOST_CHECK_EQUAL( db.get_balance( sam_id, asset_id_type() ).amount.value, sam_balance );

      // 1 day passed
      generate_blocks( db.head_block_time() + fc::days(1) );
      set_expiration( db, trx );

      // no change
      BOOST_CHECK( tick_1_id(db).target_type == lock_180_days );
      BOOST_CHECK( tick_1_id(db).current_type == lock_180_days );
      BOOST_CHECK( tick_1_id(db).status == stable );
      BOOST_CHECK( tick_1_id(db).next_auto_update_time == time_point_sec::maximum() );
      BOOST_CHECK( tick_1_id(db).next_type_downgrade_time == time_point_sec::maximum() );
      BOOST_CHECK( tick_1_id(db).amount == asset(100) );
      BOOST_CHECK_EQUAL( tick_1_id(db).value.value, 100 * 2 );
      BOOST_CHECK_EQUAL( db.get_balance( sam_id, asset_id_type() ).amount.value, sam_balance );

   } FC_LOG_AND_RETHROW()
}

BOOST_AUTO_TEST_CASE( forever_ticket_auto_update )
{ try {

      INVOKE( one_lock_forever_ticket );

      GET_ACTOR( sam );

      int64_t sam_balance = db.get_balance( sam_id, asset_id_type() ).amount.value;

      ticket_id_type tick_1_id; // default value

      // can not update ticket
      auto check_no_update = [&]()
      {
         BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_forever, {} ), fc::exception );
         BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_forever, asset(1) ), fc::exception );
         BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_forever, asset(100) ), fc::exception );
         BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_720_days, {} ), fc::exception );
         BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_720_days, asset(1) ), fc::exception );
         BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_360_days, {} ), fc::exception );
         BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_360_days, asset(1) ), fc::exception );
         BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_180_days, {} ), fc::exception );
         BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_180_days, asset(1) ), fc::exception );
         BOOST_CHECK_THROW( update_ticket( tick_1_id(db), liquid, {} ), fc::exception );
         BOOST_CHECK_THROW( update_ticket( tick_1_id(db), liquid, asset(1) ), fc::exception );
      };

      // check the ticket
      BOOST_CHECK( tick_1_id(db).account == sam_id );
      BOOST_CHECK( tick_1_id(db).target_type == lock_forever );
      BOOST_CHECK( tick_1_id(db).current_type == lock_forever );
      BOOST_CHECK( tick_1_id(db).status == withdrawing );
      BOOST_CHECK( tick_1_id(db).amount == asset(100) );
      BOOST_CHECK_EQUAL( tick_1_id(db).value.value, 100 * 8 );
      BOOST_CHECK_EQUAL( db.get_balance( sam_id, asset_id_type() ).amount.value, sam_balance );
      check_no_update();

      for( int8_t i = 0; i < 4; ++i )
      {
         // 179 days passed
         generate_blocks( db.head_block_time() + fc::days(179) );
         set_expiration( db, trx );

         // no change
         BOOST_CHECK( tick_1_id(db).account == sam_id );
         BOOST_CHECK( tick_1_id(db).target_type == lock_forever );
         BOOST_CHECK( tick_1_id(db).current_type == lock_forever );
         BOOST_CHECK( tick_1_id(db).status == withdrawing );
         BOOST_CHECK( tick_1_id(db).amount == asset(100) );
         BOOST_CHECK_EQUAL( tick_1_id(db).value.value, 100 * 2 * (4-i) );
         BOOST_CHECK_EQUAL( db.get_balance( sam_id, asset_id_type() ).amount.value, sam_balance );
         check_no_update();

         // 1 day passed
         generate_blocks( db.head_block_time() + fc::days(1) );
         set_expiration( db, trx );

         // the ticket should have been updated
         BOOST_CHECK( tick_1_id(db).account == sam_id );
         BOOST_CHECK( tick_1_id(db).target_type == lock_forever );
         BOOST_CHECK( tick_1_id(db).current_type == lock_forever );
         if( i < 3 ) {
            BOOST_CHECK( tick_1_id(db).status == withdrawing );
         } else {
            BOOST_CHECK( tick_1_id(db).status == stable );
            BOOST_CHECK( tick_1_id(db).next_auto_update_time == time_point_sec::maximum() );
            BOOST_CHECK( tick_1_id(db).next_type_downgrade_time == time_point_sec::maximum() );
         }
         BOOST_CHECK( tick_1_id(db).amount == asset(100) );
         BOOST_CHECK_EQUAL( tick_1_id(db).value.value, 100 * 2 * (3-i) );
         BOOST_CHECK_EQUAL( db.get_balance( sam_id, asset_id_type() ).amount.value, sam_balance );
         check_no_update();
      }

      // 1 day passed
      generate_blocks( db.head_block_time() + fc::days(1) );
      set_expiration( db, trx );

      // check the ticket
      BOOST_CHECK( tick_1_id(db).account == sam_id );
      BOOST_CHECK( tick_1_id(db).target_type == lock_forever );
      BOOST_CHECK( tick_1_id(db).current_type == lock_forever );
      BOOST_CHECK( tick_1_id(db).status == stable );
      BOOST_CHECK( tick_1_id(db).next_auto_update_time == time_point_sec::maximum() );
      BOOST_CHECK( tick_1_id(db).next_type_downgrade_time == time_point_sec::maximum() );
      BOOST_CHECK( tick_1_id(db).amount == asset(100) );
      BOOST_CHECK_EQUAL( tick_1_id(db).value.value, 0 );
      BOOST_CHECK_EQUAL( db.get_balance( sam_id, asset_id_type() ).amount.value, sam_balance );
      check_no_update();

   } FC_LOG_AND_RETHROW()
}

BOOST_AUTO_TEST_CASE( forever_ticket_auto_update_if_blocks_missed )
{ try {

      INVOKE( one_lock_forever_ticket );

      GET_ACTOR( sam );

      int64_t sam_balance = db.get_balance( sam_id, asset_id_type() ).amount.value;

      ticket_id_type tick_1_id; // default value

      // can not update ticket
      auto check_no_update = [&]()
      {
         BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_forever, {} ), fc::exception );
         BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_forever, asset(1) ), fc::exception );
         BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_forever, asset(100) ), fc::exception );
         BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_720_days, {} ), fc::exception );
         BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_720_days, asset(1) ), fc::exception );
         BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_360_days, {} ), fc::exception );
         BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_360_days, asset(1) ), fc::exception );
         BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_180_days, {} ), fc::exception );
         BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_180_days, asset(1) ), fc::exception );
         BOOST_CHECK_THROW( update_ticket( tick_1_id(db), liquid, {} ), fc::exception );
         BOOST_CHECK_THROW( update_ticket( tick_1_id(db), liquid, asset(1) ), fc::exception );
      };

      // check the ticket
      BOOST_CHECK( tick_1_id(db).account == sam_id );
      BOOST_CHECK( tick_1_id(db).target_type == lock_forever );
      BOOST_CHECK( tick_1_id(db).current_type == lock_forever );
      BOOST_CHECK( tick_1_id(db).status == withdrawing );
      BOOST_CHECK( tick_1_id(db).amount == asset(100) );
      BOOST_CHECK_EQUAL( tick_1_id(db).value.value, 100 * 8 );
      BOOST_CHECK_EQUAL( db.get_balance( sam_id, asset_id_type() ).amount.value, sam_balance );
      check_no_update();

      // 750 days passed
      generate_blocks( db.head_block_time() + fc::days(750) );
      set_expiration( db, trx );

      // check the ticket
      BOOST_CHECK( tick_1_id(db).account == sam_id );
      BOOST_CHECK( tick_1_id(db).target_type == lock_forever );
      BOOST_CHECK( tick_1_id(db).current_type == lock_forever );
      BOOST_CHECK( tick_1_id(db).status == stable );
      BOOST_CHECK( tick_1_id(db).next_auto_update_time == time_point_sec::maximum() );
      BOOST_CHECK( tick_1_id(db).next_type_downgrade_time == time_point_sec::maximum() );
      BOOST_CHECK( tick_1_id(db).amount == asset(100) );
      BOOST_CHECK_EQUAL( tick_1_id(db).value.value, 0 );
      BOOST_CHECK_EQUAL( db.get_balance( sam_id, asset_id_type() ).amount.value, sam_balance );
      check_no_update();

   } FC_LOG_AND_RETHROW()
}

BOOST_AUTO_TEST_CASE( cancel_charging_from_liquid )
{ try {

      // Pass the hard fork time
      generate_blocks( HARDFORK_CORE_2103_TIME );
      set_expiration( db, trx );

      ACTORS((sam));

      auto init_amount = 10000000 * GRAPHENE_BLOCKCHAIN_PRECISION;
      fund( sam, asset(init_amount) );

      int64_t sam_balance = init_amount;

      // create a ticket
      const ticket_object& tick_1 = create_ticket( sam_id, lock_360_days, asset(100) );
      ticket_id_type tick_1_id = tick_1.id;

      BOOST_CHECK( tick_1_id(db).account == sam_id );
      BOOST_CHECK( tick_1_id(db).target_type == lock_360_days );
      BOOST_CHECK( tick_1_id(db).current_type == liquid );
      BOOST_CHECK( tick_1_id(db).status == charging );
      BOOST_CHECK( tick_1_id(db).amount == asset(100) );
      BOOST_CHECK_EQUAL( tick_1_id(db).value.value, 100 );
      sam_balance -= 100;
      BOOST_CHECK_EQUAL( db.get_balance( sam_id, asset_id_type() ).amount.value, sam_balance );

      // unable to update ticket if not to change target type
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_360_days, {} ), fc::exception );
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_360_days, asset(1) ), fc::exception );
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_360_days, asset(100) ), fc::exception );

      // 14 days passed
      generate_blocks( db.head_block_time() + fc::days(14) );
      set_expiration( db, trx );

      // no change
      BOOST_CHECK( tick_1_id(db).account == sam_id );
      BOOST_CHECK( tick_1_id(db).target_type == lock_360_days );
      BOOST_CHECK( tick_1_id(db).current_type == liquid );
      BOOST_CHECK( tick_1_id(db).status == charging );
      BOOST_CHECK( tick_1_id(db).amount == asset(100) );
      BOOST_CHECK_EQUAL( tick_1_id(db).value.value, 100 );
      BOOST_CHECK_EQUAL( db.get_balance( sam_id, asset_id_type() ).amount.value, sam_balance );

      // unable to update ticket if not to change target type
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_360_days, {} ), fc::exception );
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_360_days, asset(1) ), fc::exception );
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_360_days, asset(100) ), fc::exception );

      // cancel charging
      auto result = update_ticket( tick_1_id(db), liquid, {} );
      BOOST_CHECK_EQUAL( result.new_objects.size(), 0u );

      BOOST_CHECK( tick_1_id(db).account == sam_id );
      BOOST_CHECK( tick_1_id(db).target_type == liquid );
      BOOST_CHECK( tick_1_id(db).current_type == liquid );
      BOOST_CHECK( tick_1_id(db).status == withdrawing );
      BOOST_CHECK( tick_1_id(db).amount == asset(100) );
      BOOST_CHECK_EQUAL( tick_1_id(db).value.value, 100 );
      BOOST_CHECK_EQUAL( db.get_balance( sam_id, asset_id_type() ).amount.value, sam_balance );

      // unable to update ticket if not to change target type
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), liquid, {} ), fc::exception );
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), liquid, asset(1) ), fc::exception );
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), liquid, asset(100) ), fc::exception );

      // 6 day passed
      generate_blocks( db.head_block_time() + fc::days(6) );
      set_expiration( db, trx );

      // no change
      BOOST_CHECK( tick_1_id(db).account == sam_id );
      BOOST_CHECK( tick_1_id(db).target_type == liquid );
      BOOST_CHECK( tick_1_id(db).current_type == liquid );
      BOOST_CHECK( tick_1_id(db).status == withdrawing );
      BOOST_CHECK( tick_1_id(db).amount == asset(100) );
      BOOST_CHECK_EQUAL( tick_1_id(db).value.value, 100 );
      BOOST_CHECK_EQUAL( db.get_balance( sam_id, asset_id_type() ).amount.value, sam_balance );

      // unable to update ticket if not to change target type
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), liquid, {} ), fc::exception );
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), liquid, asset(1) ), fc::exception );
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), liquid, asset(100) ), fc::exception );

      // 1 day passed
      generate_blocks( db.head_block_time() + fc::days(1) );
      set_expiration( db, trx );

      // the ticket should be freed
      BOOST_CHECK( !db.find( tick_1_id ) );

      BOOST_CHECK_EQUAL( db.get_balance( sam_id, asset_id_type() ).amount.value, sam_balance + 100 );

   } FC_LOG_AND_RETHROW()
}

BOOST_AUTO_TEST_CASE( cancel_charging_from_non_liquid )
{ try {

      INVOKE( one_lock_720_ticket );

      GET_ACTOR( sam );

      int64_t sam_balance = db.get_balance( sam_id, asset_id_type() ).amount.value;

      ticket_id_type tick_1_id; // default value

      // upgrade the ticket
      auto result = update_ticket( tick_1_id(db), lock_forever, {} );
      BOOST_CHECK_EQUAL( result.new_objects.size(), 0u );

      BOOST_CHECK( tick_1_id(db).target_type == lock_forever );
      BOOST_CHECK( tick_1_id(db).current_type == lock_720_days );
      BOOST_CHECK( tick_1_id(db).status == charging );
      BOOST_CHECK( tick_1_id(db).amount == asset(100) );
      BOOST_CHECK_EQUAL( tick_1_id(db).value.value, 100 * 8 );
      BOOST_CHECK_EQUAL( db.get_balance( sam_id, asset_id_type() ).amount.value, sam_balance );

      // 14 days passed
      generate_blocks( db.head_block_time() + fc::days(14) );
      set_expiration( db, trx );

      // no change
      BOOST_CHECK( tick_1_id(db).target_type == lock_forever );
      BOOST_CHECK( tick_1_id(db).current_type == lock_720_days );
      BOOST_CHECK( tick_1_id(db).status == charging );
      BOOST_CHECK( tick_1_id(db).amount == asset(100) );
      BOOST_CHECK_EQUAL( tick_1_id(db).value.value, 100 * 8 );
      BOOST_CHECK_EQUAL( db.get_balance( sam_id, asset_id_type() ).amount.value, sam_balance );

      // cancel charging
      result = update_ticket( tick_1_id(db), lock_720_days, {} );
      BOOST_CHECK_EQUAL( result.new_objects.size(), 0u );

      BOOST_CHECK( tick_1_id(db).account == sam_id );
      BOOST_CHECK( tick_1_id(db).target_type == lock_720_days );
      BOOST_CHECK( tick_1_id(db).current_type == lock_720_days );
      BOOST_CHECK( tick_1_id(db).status == withdrawing );
      BOOST_CHECK( tick_1_id(db).amount == asset(100) );
      BOOST_CHECK_EQUAL( tick_1_id(db).value.value, 100 * 8 );
      BOOST_CHECK_EQUAL( db.get_balance( sam_id, asset_id_type() ).amount.value, sam_balance );

      // unable to update ticket if not to change target type
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_720_days, {} ), fc::exception );
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_720_days, asset(1) ), fc::exception );
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_720_days, asset(100) ), fc::exception );

      // 6 day passed
      generate_blocks( db.head_block_time() + fc::days(6) );
      set_expiration( db, trx );

      // no change
      BOOST_CHECK( tick_1_id(db).account == sam_id );
      BOOST_CHECK( tick_1_id(db).target_type == lock_720_days );
      BOOST_CHECK( tick_1_id(db).current_type == lock_720_days );
      BOOST_CHECK( tick_1_id(db).status == withdrawing );
      BOOST_CHECK( tick_1_id(db).amount == asset(100) );
      BOOST_CHECK_EQUAL( tick_1_id(db).value.value, 100 * 8 );
      BOOST_CHECK_EQUAL( db.get_balance( sam_id, asset_id_type() ).amount.value, sam_balance );

      // unable to update ticket if not to change target type
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_720_days, {} ), fc::exception );
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_720_days, asset(1) ), fc::exception );
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_720_days, asset(100) ), fc::exception );

      // 1 day passed
      generate_blocks( db.head_block_time() + fc::days(1) );
      set_expiration( db, trx );

      // ticket should be stable now
      BOOST_CHECK( tick_1_id(db).account == sam_id );
      BOOST_CHECK( tick_1_id(db).target_type == lock_720_days );
      BOOST_CHECK( tick_1_id(db).current_type == lock_720_days );
      BOOST_CHECK( tick_1_id(db).status == stable );
      BOOST_CHECK( tick_1_id(db).next_auto_update_time == time_point_sec::maximum() );
      BOOST_CHECK( tick_1_id(db).next_type_downgrade_time == time_point_sec::maximum() );
      BOOST_CHECK( tick_1_id(db).amount == asset(100) );
      BOOST_CHECK_EQUAL( tick_1_id(db).value.value, 100 * 8 );
      BOOST_CHECK_EQUAL( db.get_balance( sam_id, asset_id_type() ).amount.value, sam_balance );

      // unable to update ticket if not to change target type
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_720_days, {} ), fc::exception );
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_720_days, asset(1) ), fc::exception );
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_720_days, asset(100) ), fc::exception );

      // 1 day passed
      generate_blocks( db.head_block_time() + fc::days(1) );
      set_expiration( db, trx );

      // no change
      BOOST_CHECK( tick_1_id(db).account == sam_id );
      BOOST_CHECK( tick_1_id(db).target_type == lock_720_days );
      BOOST_CHECK( tick_1_id(db).current_type == lock_720_days );
      BOOST_CHECK( tick_1_id(db).status == stable );
      BOOST_CHECK( tick_1_id(db).next_auto_update_time == time_point_sec::maximum() );
      BOOST_CHECK( tick_1_id(db).next_type_downgrade_time == time_point_sec::maximum() );
      BOOST_CHECK( tick_1_id(db).amount == asset(100) );
      BOOST_CHECK_EQUAL( tick_1_id(db).value.value, 100 * 8 );
      BOOST_CHECK_EQUAL( db.get_balance( sam_id, asset_id_type() ).amount.value, sam_balance );

      // unable to update ticket if not to change target type
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_720_days, {} ), fc::exception );
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_720_days, asset(1) ), fc::exception );
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_720_days, asset(100) ), fc::exception );

   } FC_LOG_AND_RETHROW()
}

BOOST_AUTO_TEST_CASE( update_from_charging_to_withdrawing )
{ try {

      INVOKE( one_lock_720_ticket );

      GET_ACTOR( sam );

      int64_t sam_balance = db.get_balance( sam_id, asset_id_type() ).amount.value;

      ticket_id_type tick_1_id; // default value

      // upgrade the ticket
      auto result = update_ticket( tick_1_id(db), lock_forever, {} );
      BOOST_CHECK_EQUAL( result.new_objects.size(), 0u );

      BOOST_CHECK( tick_1_id(db).target_type == lock_forever );
      BOOST_CHECK( tick_1_id(db).current_type == lock_720_days );
      BOOST_CHECK( tick_1_id(db).status == charging );
      BOOST_CHECK( tick_1_id(db).amount == asset(100) );
      BOOST_CHECK_EQUAL( tick_1_id(db).value.value, 100 * 8 );
      BOOST_CHECK_EQUAL( db.get_balance( sam_id, asset_id_type() ).amount.value, sam_balance );

      // 14 days passed
      generate_blocks( db.head_block_time() + fc::days(14) );
      set_expiration( db, trx );

      // no change
      BOOST_CHECK( tick_1_id(db).target_type == lock_forever );
      BOOST_CHECK( tick_1_id(db).current_type == lock_720_days );
      BOOST_CHECK( tick_1_id(db).status == charging );
      BOOST_CHECK( tick_1_id(db).amount == asset(100) );
      BOOST_CHECK_EQUAL( tick_1_id(db).value.value, 100 * 8 );
      BOOST_CHECK_EQUAL( db.get_balance( sam_id, asset_id_type() ).amount.value, sam_balance );

      // update target
      result = update_ticket( tick_1_id(db), liquid, {} );
      BOOST_CHECK_EQUAL( result.new_objects.size(), 0u );

      BOOST_CHECK( tick_1_id(db).account == sam_id );
      BOOST_CHECK( tick_1_id(db).target_type == liquid );
      BOOST_CHECK( tick_1_id(db).current_type == lock_360_days );
      BOOST_CHECK( tick_1_id(db).status == withdrawing );
      BOOST_CHECK( tick_1_id(db).amount == asset(100) );
      BOOST_CHECK_EQUAL( tick_1_id(db).value.value, 100 * 4 );
      BOOST_CHECK_EQUAL( db.get_balance( sam_id, asset_id_type() ).amount.value, sam_balance );

      // unable to update ticket if not to change target type
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), liquid, {} ), fc::exception );
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), liquid, asset(1) ), fc::exception );
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), liquid, asset(100) ), fc::exception );

   } FC_LOG_AND_RETHROW()
}

BOOST_AUTO_TEST_CASE( update_from_withdrawing_to_charging_step_1 )
{ try {

      INVOKE( update_from_charging_to_withdrawing );

      GET_ACTOR( sam );

      int64_t sam_balance = db.get_balance( sam_id, asset_id_type() ).amount.value;

      ticket_id_type tick_1_id; // default value

      // check the ticket
      BOOST_CHECK( tick_1_id(db).account == sam_id );
      BOOST_CHECK( tick_1_id(db).target_type == liquid );
      BOOST_CHECK( tick_1_id(db).current_type == lock_360_days );
      BOOST_CHECK( tick_1_id(db).status == withdrawing );
      BOOST_CHECK( tick_1_id(db).amount == asset(100) );
      BOOST_CHECK_EQUAL( tick_1_id(db).value.value, 100 * 4 );
      BOOST_CHECK_EQUAL( db.get_balance( sam_id, asset_id_type() ).amount.value, sam_balance );

      // 114 days passed
      generate_blocks( db.head_block_time() + fc::days(114) );
      set_expiration( db, trx );

      // no change
      BOOST_CHECK( tick_1_id(db).account == sam_id );
      BOOST_CHECK( tick_1_id(db).target_type == liquid );
      BOOST_CHECK( tick_1_id(db).current_type == lock_360_days );
      BOOST_CHECK( tick_1_id(db).status == withdrawing );
      BOOST_CHECK( tick_1_id(db).amount == asset(100) );
      BOOST_CHECK_EQUAL( tick_1_id(db).value.value, 100 * 4 );
      BOOST_CHECK_EQUAL( db.get_balance( sam_id, asset_id_type() ).amount.value, sam_balance );

      // upgrade the ticket
      auto result = update_ticket( tick_1_id(db), lock_forever, {} );
      BOOST_CHECK_EQUAL( result.new_objects.size(), 0u );

      BOOST_CHECK( tick_1_id(db).target_type == lock_forever );
      BOOST_CHECK( tick_1_id(db).current_type == lock_360_days );
      BOOST_CHECK( tick_1_id(db).status == charging );
      BOOST_CHECK( tick_1_id(db).amount == asset(100) );
      BOOST_CHECK_EQUAL( tick_1_id(db).value.value, 100 * 4 );
      BOOST_CHECK_EQUAL( db.get_balance( sam_id, asset_id_type() ).amount.value, sam_balance );

      BOOST_CHECK( tick_1_id(db).next_type_downgrade_time != time_point_sec::maximum() );

   } FC_LOG_AND_RETHROW()
}

BOOST_AUTO_TEST_CASE( update_from_withdrawing_to_charging_then_wait )
{ try {

      INVOKE( update_from_withdrawing_to_charging_step_1 );

      GET_ACTOR( sam );

      int64_t sam_balance = db.get_balance( sam_id, asset_id_type() ).amount.value;

      ticket_id_type tick_1_id; // default value

      // check the ticket
      BOOST_CHECK( tick_1_id(db).target_type == lock_forever );
      BOOST_CHECK( tick_1_id(db).current_type == lock_360_days );
      BOOST_CHECK( tick_1_id(db).status == charging );
      BOOST_CHECK( tick_1_id(db).amount == asset(100) );
      BOOST_CHECK_EQUAL( tick_1_id(db).value.value, 100 * 4 );
      BOOST_CHECK_EQUAL( db.get_balance( sam_id, asset_id_type() ).amount.value, sam_balance );

      BOOST_CHECK( tick_1_id(db).next_type_downgrade_time != time_point_sec::maximum() );
      auto down_time = tick_1_id(db).next_type_downgrade_time;

      // 14 days passed
      generate_blocks( db.head_block_time() + fc::days(14) );
      set_expiration( db, trx );

      // no change
      BOOST_CHECK( tick_1_id(db).target_type == lock_forever );
      BOOST_CHECK( tick_1_id(db).current_type == lock_360_days );
      BOOST_CHECK( tick_1_id(db).status == charging );
      BOOST_CHECK( tick_1_id(db).amount == asset(100) );
      BOOST_CHECK_EQUAL( tick_1_id(db).value.value, 100 * 4 );
      BOOST_CHECK_EQUAL( db.get_balance( sam_id, asset_id_type() ).amount.value, sam_balance );

      BOOST_CHECK( tick_1_id(db).next_type_downgrade_time == down_time );

      // 1 day passed
      generate_blocks( db.head_block_time() + fc::days(1) );
      set_expiration( db, trx );

      // the ticket should have upgraded
      BOOST_CHECK( tick_1_id(db).target_type == lock_forever );
      BOOST_CHECK( tick_1_id(db).current_type == lock_720_days );
      BOOST_CHECK( tick_1_id(db).status == charging );
      BOOST_CHECK( tick_1_id(db).next_type_downgrade_time == time_point_sec::maximum() );
      BOOST_CHECK( tick_1_id(db).amount == asset(100) );
      BOOST_CHECK_EQUAL( tick_1_id(db).value.value, 100 * 8 );
      BOOST_CHECK_EQUAL( db.get_balance( sam_id, asset_id_type() ).amount.value, sam_balance );
      BOOST_CHECK( tick_1_id(db).account == sam_id );

   } FC_LOG_AND_RETHROW()
}

BOOST_AUTO_TEST_CASE( update_from_withdrawing_to_charging_then_withdraw_again )
{ try {

      INVOKE( update_from_withdrawing_to_charging_step_1 );

      GET_ACTOR( sam );

      int64_t sam_balance = db.get_balance( sam_id, asset_id_type() ).amount.value;

      ticket_id_type tick_1_id; // default value

      // check the ticket
      BOOST_CHECK( tick_1_id(db).target_type == lock_forever );
      BOOST_CHECK( tick_1_id(db).current_type == lock_360_days );
      BOOST_CHECK( tick_1_id(db).status == charging );
      BOOST_CHECK( tick_1_id(db).amount == asset(100) );
      BOOST_CHECK_EQUAL( tick_1_id(db).value.value, 100 * 4 );
      BOOST_CHECK_EQUAL( db.get_balance( sam_id, asset_id_type() ).amount.value, sam_balance );

      BOOST_CHECK( tick_1_id(db).next_type_downgrade_time != time_point_sec::maximum() );
      auto down_time = tick_1_id(db).next_type_downgrade_time;

      // 6 days passed
      generate_blocks( db.head_block_time() + fc::days(6) );
      set_expiration( db, trx );

      // no change
      BOOST_CHECK( tick_1_id(db).target_type == lock_forever );
      BOOST_CHECK( tick_1_id(db).current_type == lock_360_days );
      BOOST_CHECK( tick_1_id(db).status == charging );
      BOOST_CHECK( tick_1_id(db).amount == asset(100) );
      BOOST_CHECK_EQUAL( tick_1_id(db).value.value, 100 * 4 );
      BOOST_CHECK_EQUAL( db.get_balance( sam_id, asset_id_type() ).amount.value, sam_balance );

      BOOST_CHECK( tick_1_id(db).next_type_downgrade_time == down_time );

      // downgrade again
      auto result = update_ticket( tick_1_id(db), liquid, {} );
      BOOST_CHECK_EQUAL( result.new_objects.size(), 0u );

      // current type should not change
      BOOST_CHECK( tick_1_id(db).target_type == liquid );
      BOOST_CHECK( tick_1_id(db).current_type == lock_360_days );
      BOOST_CHECK( tick_1_id(db).status == withdrawing );
      BOOST_CHECK( tick_1_id(db).amount == asset(100) );
      BOOST_CHECK_EQUAL( tick_1_id(db).value.value, 100 * 4 );
      BOOST_CHECK_EQUAL( db.get_balance( sam_id, asset_id_type() ).amount.value, sam_balance );

      BOOST_CHECK( tick_1_id(db).next_type_downgrade_time == down_time );
      BOOST_CHECK( tick_1_id(db).next_auto_update_time == down_time );

      // X days passed, now about to downgrade
      generate_blocks( down_time - fc::days(1) );
      set_expiration( db, trx );

      // no change
      BOOST_CHECK( tick_1_id(db).target_type == liquid );
      BOOST_CHECK( tick_1_id(db).current_type == lock_360_days );
      BOOST_CHECK( tick_1_id(db).status == withdrawing );
      BOOST_CHECK( tick_1_id(db).amount == asset(100) );
      BOOST_CHECK_EQUAL( tick_1_id(db).value.value, 100 * 4 );
      BOOST_CHECK_EQUAL( db.get_balance( sam_id, asset_id_type() ).amount.value, sam_balance );

      BOOST_CHECK( tick_1_id(db).next_type_downgrade_time == down_time );
      BOOST_CHECK( tick_1_id(db).next_auto_update_time == down_time );

      // upgrade again
      result = update_ticket( tick_1_id(db), lock_720_days, {} );
      BOOST_CHECK_EQUAL( result.new_objects.size(), 0u );

      BOOST_CHECK( tick_1_id(db).target_type == lock_720_days );
      BOOST_CHECK( tick_1_id(db).current_type == lock_360_days );
      BOOST_CHECK( tick_1_id(db).status == charging );
      BOOST_CHECK( tick_1_id(db).amount == asset(100) );
      BOOST_CHECK_EQUAL( tick_1_id(db).value.value, 100 * 4 );
      BOOST_CHECK_EQUAL( db.get_balance( sam_id, asset_id_type() ).amount.value, sam_balance );

      BOOST_CHECK( tick_1_id(db).next_type_downgrade_time == down_time );

      // 6 days passed
      generate_blocks( db.head_block_time() + fc::days(6) );
      set_expiration( db, trx );

      // no change
      BOOST_CHECK( tick_1_id(db).target_type == lock_720_days );
      BOOST_CHECK( tick_1_id(db).current_type == lock_360_days );
      BOOST_CHECK( tick_1_id(db).status == charging );
      BOOST_CHECK( tick_1_id(db).amount == asset(100) );
      BOOST_CHECK_EQUAL( tick_1_id(db).value.value, 100 * 4 );
      BOOST_CHECK_EQUAL( db.get_balance( sam_id, asset_id_type() ).amount.value, sam_balance );

      BOOST_CHECK( tick_1_id(db).next_type_downgrade_time == down_time );
      // should have downgraded if not changed to upgrade
      BOOST_CHECK( tick_1_id(db).next_type_downgrade_time < db.head_block_time() );

      // downgrade again
      result = update_ticket( tick_1_id(db), lock_180_days, {} );
      BOOST_CHECK_EQUAL( result.new_objects.size(), 0u );
      BOOST_CHECK_EQUAL( result.updated_objects.size(), 1u );

      // the ticket should have downgraded
      BOOST_CHECK( tick_1_id(db).target_type == lock_180_days );
      BOOST_CHECK( tick_1_id(db).current_type == lock_180_days );
      BOOST_CHECK( tick_1_id(db).status == withdrawing );
      BOOST_CHECK( tick_1_id(db).amount == asset(100) );
      BOOST_CHECK_EQUAL( tick_1_id(db).value.value, 100 * 2 );
      BOOST_CHECK_EQUAL( db.get_balance( sam_id, asset_id_type() ).amount.value, sam_balance );

      BOOST_CHECK( tick_1_id(db).next_type_downgrade_time == down_time + 180*86400 );
      BOOST_CHECK( tick_1_id(db).next_auto_update_time == down_time + 180*86400 );

      // X days passed, now about to downgrade
      generate_blocks( down_time + 180*86400 - fc::days(1) );
      set_expiration( db, trx );

      // upgrade again
      result = update_ticket( tick_1_id(db), lock_720_days, {} );
      BOOST_CHECK_EQUAL( result.new_objects.size(), 0u );

      BOOST_CHECK( tick_1_id(db).target_type == lock_720_days );
      BOOST_CHECK( tick_1_id(db).current_type == lock_180_days );
      BOOST_CHECK( tick_1_id(db).status == charging );
      BOOST_CHECK( tick_1_id(db).amount == asset(100) );
      BOOST_CHECK_EQUAL( tick_1_id(db).value.value, 100 * 2 );
      BOOST_CHECK_EQUAL( db.get_balance( sam_id, asset_id_type() ).amount.value, sam_balance );

      BOOST_CHECK( tick_1_id(db).next_type_downgrade_time == down_time + 180*86400 );

      // 6 days passed
      generate_blocks( db.head_block_time() + fc::days(6) );
      set_expiration( db, trx );

      // no change
      BOOST_CHECK( tick_1_id(db).target_type == lock_720_days );
      BOOST_CHECK( tick_1_id(db).current_type == lock_180_days );
      BOOST_CHECK( tick_1_id(db).status == charging );
      BOOST_CHECK( tick_1_id(db).amount == asset(100) );
      BOOST_CHECK_EQUAL( tick_1_id(db).value.value, 100 * 2 );
      BOOST_CHECK_EQUAL( db.get_balance( sam_id, asset_id_type() ).amount.value, sam_balance );

      BOOST_CHECK( tick_1_id(db).next_type_downgrade_time == down_time + 180*86400 );
      // should have downgraded if not changed to upgrade
      BOOST_CHECK( tick_1_id(db).next_type_downgrade_time < db.head_block_time() );

      // partially cancel charging
      result = update_ticket( tick_1_id(db), lock_180_days, asset(10) );
      BOOST_REQUIRE_EQUAL( result.new_objects.size(), 1u );
      BOOST_REQUIRE_EQUAL( result.updated_objects.size(), 1u );
      BOOST_CHECK_EQUAL( result.removed_objects.size(), 0u );
      BOOST_CHECK( *result.updated_objects.begin() == tick_1_id );

      // the new ticket is stable
      ticket_id_type tick_2_id = *result.new_objects.begin();

      BOOST_REQUIRE( db.find( tick_2_id ) );
      BOOST_CHECK( tick_2_id(db).target_type == lock_180_days );
      BOOST_CHECK( tick_2_id(db).current_type == lock_180_days );
      BOOST_CHECK( tick_2_id(db).status == stable );
      BOOST_CHECK( tick_2_id(db).next_auto_update_time == time_point_sec::maximum() );
      BOOST_CHECK( tick_2_id(db).next_type_downgrade_time == time_point_sec::maximum() );
      BOOST_CHECK( tick_2_id(db).amount == asset(10) );
      BOOST_CHECK_EQUAL( tick_2_id(db).value.value, 10 * 2 );
      BOOST_CHECK_EQUAL( db.get_balance( sam_id, asset_id_type() ).amount.value, sam_balance );

      // check the remainder
      BOOST_CHECK( tick_1_id(db).target_type == lock_720_days );
      BOOST_CHECK( tick_1_id(db).current_type == lock_180_days );
      BOOST_CHECK( tick_1_id(db).status == charging );
      BOOST_CHECK( tick_1_id(db).amount == asset(90) );
      BOOST_CHECK_EQUAL( tick_1_id(db).value.value, 90 * 2 );
      BOOST_CHECK_EQUAL( db.get_balance( sam_id, asset_id_type() ).amount.value, sam_balance );

      BOOST_CHECK( tick_1_id(db).next_type_downgrade_time == down_time + 180*86400 );
      // should have downgraded if not changed to upgrade
      BOOST_CHECK( tick_1_id(db).next_type_downgrade_time < db.head_block_time() );

      // generate a block
      generate_block();

      // no change
      BOOST_CHECK( tick_1_id(db).target_type == lock_720_days );
      BOOST_CHECK( tick_1_id(db).current_type == lock_180_days );
      BOOST_CHECK( tick_1_id(db).status == charging );
      BOOST_CHECK( tick_1_id(db).amount == asset(90) );
      BOOST_CHECK_EQUAL( tick_1_id(db).value.value, 90 * 2 );
      BOOST_CHECK_EQUAL( db.get_balance( sam_id, asset_id_type() ).amount.value, sam_balance );

      BOOST_CHECK( tick_1_id(db).next_type_downgrade_time == down_time + 180*86400 );
      // should have downgraded if not changed to upgrade
      BOOST_CHECK( tick_1_id(db).next_type_downgrade_time < db.head_block_time() );

      // cancel charging
      result = update_ticket( tick_1_id(db), lock_180_days, {} );
      BOOST_CHECK_EQUAL( result.new_objects.size(), 0u );
      BOOST_CHECK_EQUAL( result.updated_objects.size(), 1u );

      // the ticket is now stable
      BOOST_CHECK( tick_1_id(db).target_type == lock_180_days );
      BOOST_CHECK( tick_1_id(db).current_type == lock_180_days );
      BOOST_CHECK( tick_1_id(db).status == stable );
      BOOST_CHECK( tick_1_id(db).next_auto_update_time == time_point_sec::maximum() );
      BOOST_CHECK( tick_1_id(db).next_type_downgrade_time == time_point_sec::maximum() );
      BOOST_CHECK( tick_1_id(db).amount == asset(90) );
      BOOST_CHECK_EQUAL( tick_1_id(db).value.value, 90 * 2 );
      BOOST_CHECK_EQUAL( db.get_balance( sam_id, asset_id_type() ).amount.value, sam_balance );

      // downgrade again
      bool has_hf_2262 = ( HARDFORK_CORE_2262_PASSED( db.get_dynamic_global_properties().next_maintenance_time ) );
      result = update_ticket( tick_1_id(db), liquid, {} );
      BOOST_CHECK_EQUAL( result.new_objects.size(), 0u );

      BOOST_CHECK( tick_1_id(db).target_type == liquid );
      BOOST_CHECK( tick_1_id(db).current_type == liquid );
      BOOST_CHECK( tick_1_id(db).status == withdrawing );
      BOOST_CHECK( tick_1_id(db).amount == asset(90) );
      BOOST_CHECK_EQUAL( tick_1_id(db).value.value, has_hf_2262 ? 0 : 90 );
      BOOST_CHECK_EQUAL( db.get_balance( sam_id, asset_id_type() ).amount.value, sam_balance );

      BOOST_CHECK( tick_1_id(db).next_type_downgrade_time != time_point_sec::maximum() );
      down_time = tick_1_id(db).next_type_downgrade_time;

      // X days passed, 30 days to downgrade
      generate_blocks( down_time - fc::days(30) );
      set_expiration( db, trx );

      // upgrade again
      result = update_ticket( tick_1_id(db), lock_720_days, {} );
      BOOST_CHECK_EQUAL( result.new_objects.size(), 0u );

      has_hf_2262 = ( HARDFORK_CORE_2262_PASSED( db.get_dynamic_global_properties().next_maintenance_time ) );
      BOOST_CHECK( tick_1_id(db).target_type == lock_720_days );
      BOOST_CHECK( tick_1_id(db).current_type == liquid );
      BOOST_CHECK( tick_1_id(db).status == charging );
      BOOST_CHECK( tick_1_id(db).amount == asset(90) );
      BOOST_CHECK_EQUAL( tick_1_id(db).value.value, has_hf_2262 ? 0 : 90 );
      BOOST_CHECK_EQUAL( db.get_balance( sam_id, asset_id_type() ).amount.value, sam_balance );

      BOOST_CHECK( tick_1_id(db).next_type_downgrade_time == down_time );

      // downgrade again
      result = update_ticket( tick_1_id(db), liquid, {} );
      BOOST_CHECK_EQUAL( result.new_objects.size(), 0u );

      has_hf_2262 = ( HARDFORK_CORE_2262_PASSED( db.get_dynamic_global_properties().next_maintenance_time ) );
      BOOST_CHECK( tick_1_id(db).target_type == liquid );
      BOOST_CHECK( tick_1_id(db).current_type == liquid );
      BOOST_CHECK( tick_1_id(db).status == withdrawing );
      BOOST_CHECK( tick_1_id(db).amount == asset(90) );
      BOOST_CHECK_EQUAL( tick_1_id(db).value.value, has_hf_2262 ? 0 : 90 );
      BOOST_CHECK_EQUAL( db.get_balance( sam_id, asset_id_type() ).amount.value, sam_balance );

      BOOST_CHECK( tick_1_id(db).next_type_downgrade_time == down_time );

      // X days passed, now about to free
      generate_blocks( down_time - fc::days(1) );
      set_expiration( db, trx );

      // upgrade again
      result = update_ticket( tick_1_id(db), lock_720_days, {} );
      BOOST_CHECK_EQUAL( result.new_objects.size(), 0u );

      has_hf_2262 = ( HARDFORK_CORE_2262_PASSED( db.get_dynamic_global_properties().next_maintenance_time ) );
      BOOST_CHECK( tick_1_id(db).target_type == lock_720_days );
      BOOST_CHECK( tick_1_id(db).current_type == liquid );
      BOOST_CHECK( tick_1_id(db).status == charging );
      BOOST_CHECK( tick_1_id(db).amount == asset(90) );
      BOOST_CHECK_EQUAL( tick_1_id(db).value.value, has_hf_2262 ? 0 : 90 );
      BOOST_CHECK_EQUAL( db.get_balance( sam_id, asset_id_type() ).amount.value, sam_balance );

      BOOST_CHECK( tick_1_id(db).next_type_downgrade_time == down_time );

      // 6 days passed
      generate_blocks( db.head_block_time() + fc::days(6) );
      set_expiration( db, trx );

      // no change
      has_hf_2262 = ( HARDFORK_CORE_2262_PASSED( db.get_dynamic_global_properties().next_maintenance_time ) );
      BOOST_CHECK( tick_1_id(db).target_type == lock_720_days );
      BOOST_CHECK( tick_1_id(db).current_type == liquid );
      BOOST_CHECK( tick_1_id(db).status == charging );
      BOOST_CHECK( tick_1_id(db).amount == asset(90) );
      BOOST_CHECK_EQUAL( tick_1_id(db).value.value, has_hf_2262 ? 0 : 90 );
      BOOST_CHECK_EQUAL( db.get_balance( sam_id, asset_id_type() ).amount.value, sam_balance );

      BOOST_CHECK( tick_1_id(db).next_type_downgrade_time == down_time );
      // should have freed if not changed to upgrade
      BOOST_CHECK( tick_1_id(db).next_type_downgrade_time < db.head_block_time() );

      // partially cancel charging
      result = update_ticket( tick_1_id(db), liquid, asset(15) );
      BOOST_REQUIRE_EQUAL( result.new_objects.size(), 1u );
      BOOST_REQUIRE_EQUAL( result.updated_objects.size(), 1u );
      BOOST_REQUIRE_EQUAL( result.removed_objects.size(), 1u );
      BOOST_CHECK( *result.updated_objects.begin() == tick_1_id );

      // the new created ticket is freed already
      ticket_id_type tick_3_id = *result.new_objects.begin();
      BOOST_CHECK( *result.removed_objects.begin() == tick_3_id );
      BOOST_CHECK( !db.find( tick_3_id ) );

      // check the remainder
      has_hf_2262 = ( HARDFORK_CORE_2262_PASSED( db.get_dynamic_global_properties().next_maintenance_time ) );
      BOOST_CHECK( tick_1_id(db).target_type == lock_720_days );
      BOOST_CHECK( tick_1_id(db).current_type == liquid );
      BOOST_CHECK( tick_1_id(db).status == charging );
      BOOST_CHECK( tick_1_id(db).amount == asset(75) );
      BOOST_CHECK_EQUAL( tick_1_id(db).value.value, has_hf_2262 ? 0 : 75 );
      BOOST_CHECK_EQUAL( db.get_balance( sam_id, asset_id_type() ).amount.value, sam_balance + 15 );

      BOOST_CHECK( tick_1_id(db).next_type_downgrade_time == down_time );
      // should have freed if not changed to upgrade
      BOOST_CHECK( tick_1_id(db).next_type_downgrade_time < db.head_block_time() );

      // generate a block
      generate_block();

      // no change
      has_hf_2262 = ( HARDFORK_CORE_2262_PASSED( db.get_dynamic_global_properties().next_maintenance_time ) );
      BOOST_CHECK( tick_1_id(db).target_type == lock_720_days );
      BOOST_CHECK( tick_1_id(db).current_type == liquid );
      BOOST_CHECK( tick_1_id(db).status == charging );
      BOOST_CHECK( tick_1_id(db).amount == asset(75) );
      BOOST_CHECK_EQUAL( tick_1_id(db).value.value, has_hf_2262 ? 0 : 75 );
      BOOST_CHECK_EQUAL( db.get_balance( sam_id, asset_id_type() ).amount.value, sam_balance + 15 );

      BOOST_CHECK( tick_1_id(db).next_type_downgrade_time == down_time );
      // should have freed if not changed to upgrade
      BOOST_CHECK( tick_1_id(db).next_type_downgrade_time < db.head_block_time() );

      // cancel charging
      result = update_ticket( tick_1_id(db), liquid, {} );
      BOOST_CHECK_EQUAL( result.new_objects.size(), 0u );
      BOOST_CHECK_EQUAL( result.updated_objects.size(), 0u );
      BOOST_REQUIRE_EQUAL( result.removed_objects.size(), 1u );
      BOOST_CHECK( *result.removed_objects.begin() == tick_1_id );

      // the ticket is freed
      BOOST_CHECK( !db.find( tick_1_id ) );

      BOOST_CHECK_EQUAL( db.get_balance( sam_id, asset_id_type() ).amount.value, sam_balance + 90 );

      // generate a block
      generate_block();

      // the ticket is freed
      BOOST_CHECK( !db.find( tick_1_id ) );

      BOOST_CHECK_EQUAL( db.get_balance( sam_id, asset_id_type() ).amount.value, sam_balance + 90 );

   } FC_LOG_AND_RETHROW()
}

BOOST_AUTO_TEST_CASE( multiple_tickets )
{ try {

      // Pass the hard fork time
      generate_blocks( HARDFORK_CORE_2103_TIME );
      set_expiration( db, trx );

      ACTORS((sam)(ted));

      auto init_amount = 10000000 * GRAPHENE_BLOCKCHAIN_PRECISION;
      fund( sam, asset(init_amount) );
      fund( ted, asset(init_amount) );

      int64_t sam_balance = init_amount;
      int64_t ted_balance = init_amount;

      // Sam create some tickets
      const ticket_object& tick_1 = create_ticket( sam_id, lock_180_days, asset(1) );
      BOOST_CHECK( tick_1.account == sam_id );
      BOOST_CHECK( tick_1.target_type == lock_180_days );
      BOOST_CHECK( tick_1.current_type == liquid );
      BOOST_CHECK( tick_1.status == charging );
      BOOST_CHECK( tick_1.amount == asset(1) );
      BOOST_CHECK_EQUAL( tick_1.value.value, 1 );
      sam_balance -= 1;
      BOOST_CHECK_EQUAL( db.get_balance( sam_id, asset_id_type() ).amount.value, sam_balance );

      const ticket_object& tick_2 = create_ticket( sam_id, lock_360_days, asset(1000) );
      BOOST_CHECK( tick_2.account == sam_id );
      BOOST_CHECK( tick_2.target_type == lock_360_days );
      BOOST_CHECK( tick_2.current_type == liquid );
      BOOST_CHECK( tick_2.status == charging );
      BOOST_CHECK( tick_2.amount == asset(1000) );
      BOOST_CHECK_EQUAL( tick_2.value.value, 1000 );
      sam_balance -= 1000;
      BOOST_CHECK_EQUAL( db.get_balance( sam_id, asset_id_type() ).amount.value, sam_balance );

      const ticket_object& tick_3 = create_ticket( sam_id, lock_720_days, asset(10) );
      BOOST_CHECK( tick_3.account == sam_id );
      BOOST_CHECK( tick_3.target_type == lock_720_days );
      BOOST_CHECK( tick_3.current_type == liquid );
      BOOST_CHECK( tick_3.status == charging );
      BOOST_CHECK( tick_3.amount == asset(10) );
      BOOST_CHECK_EQUAL( tick_3.value.value, 10 );
      sam_balance -= 10;
      BOOST_CHECK_EQUAL( db.get_balance( sam_id, asset_id_type() ).amount.value, sam_balance );

      // Ted create a ticket
      const ticket_object& tick_4 = create_ticket( ted_id, lock_forever, asset(100000) );
      BOOST_CHECK( tick_4.account == ted_id );
      BOOST_CHECK( tick_4.target_type == lock_forever );
      BOOST_CHECK( tick_4.current_type == liquid );
      BOOST_CHECK( tick_4.status == charging );
      BOOST_CHECK( tick_4.amount == asset(100000) );
      BOOST_CHECK_EQUAL( tick_4.value.value, 100000 );
      ted_balance -= 100000;
      BOOST_CHECK_EQUAL( db.get_balance( ted_id, asset_id_type() ).amount.value, ted_balance );

      ticket_id_type tick_1_id = tick_1.id;
      ticket_id_type tick_2_id = tick_2.id;
      ticket_id_type tick_3_id = tick_3.id;
      ticket_id_type tick_4_id = tick_4.id;

      // one day passed
      generate_blocks( db.head_block_time() + fc::days(1) );
      set_expiration( db, trx );

      // Update ticket 1 to liquid
      generic_operation_result result = update_ticket( tick_1_id(db), liquid, asset(1) );
      BOOST_REQUIRE( db.find( tick_1_id ) );
      BOOST_CHECK( tick_1_id(db).target_type == liquid );
      BOOST_CHECK( tick_1_id(db).current_type == liquid );
      BOOST_CHECK( tick_1_id(db).status == withdrawing );
      BOOST_CHECK( tick_1_id(db).amount == asset(1) );
      BOOST_CHECK_EQUAL( tick_1_id(db).value.value, 1 );

      // Update 30 CORE in ticket 2 to lock_180_days
      result = update_ticket( tick_2_id(db), lock_180_days, asset(30) );
      BOOST_REQUIRE( db.find( tick_2_id ) );
      BOOST_CHECK( tick_2_id(db).target_type == lock_360_days ); // target type of the remaining ticket is unchanged
      BOOST_CHECK( tick_2_id(db).current_type == liquid );
      BOOST_CHECK( tick_2_id(db).status == charging );
      BOOST_CHECK( tick_2_id(db).amount == asset(970) );
      BOOST_CHECK_EQUAL( tick_2_id(db).value.value, 970 );

      BOOST_REQUIRE_EQUAL( result.new_objects.size(), 1u );

      ticket_id_type tick_5_id = *result.new_objects.begin();
      BOOST_REQUIRE( db.find( tick_5_id ) );
      BOOST_CHECK( tick_5_id(db).target_type == lock_180_days ); // target type of the new ticket is set
      BOOST_CHECK( tick_5_id(db).current_type == liquid );
      BOOST_CHECK( tick_5_id(db).status == charging );
      BOOST_CHECK( tick_5_id(db).amount == asset(30) );
      BOOST_CHECK_EQUAL( tick_5_id(db).value.value, 30 );

      BOOST_CHECK( tick_3_id(db).target_type == lock_720_days );
      BOOST_CHECK( tick_3_id(db).current_type == liquid );
      BOOST_CHECK( tick_3_id(db).status == charging );
      BOOST_CHECK( tick_3_id(db).amount == asset(10) );
      BOOST_CHECK_EQUAL( tick_3_id(db).value.value, 10 );

      BOOST_CHECK( tick_4_id(db).target_type == lock_forever );
      BOOST_CHECK( tick_4_id(db).current_type == liquid );
      BOOST_CHECK( tick_4_id(db).status == charging );
      BOOST_CHECK( tick_4_id(db).amount == asset(100000) );
      BOOST_CHECK_EQUAL( tick_4_id(db).value.value, 100000 );

      // 7 days passed
      generate_blocks( db.head_block_time() + fc::days(7) );
      set_expiration( db, trx );

      // ticket 1 should have been freed
      BOOST_CHECK( !db.find( tick_1_id ) );
      sam_balance += 1;
      BOOST_CHECK_EQUAL( db.get_balance( sam_id, asset_id_type() ).amount.value, sam_balance );

      BOOST_CHECK( tick_2_id(db).target_type == lock_360_days );
      BOOST_CHECK( tick_2_id(db).current_type == liquid );
      BOOST_CHECK( tick_2_id(db).status == charging );
      BOOST_CHECK( tick_2_id(db).amount == asset(970) );
      BOOST_CHECK_EQUAL( tick_2_id(db).value.value, 970 );

      BOOST_CHECK( tick_3_id(db).target_type == lock_720_days );
      BOOST_CHECK( tick_3_id(db).current_type == liquid );
      BOOST_CHECK( tick_3_id(db).status == charging );
      BOOST_CHECK( tick_3_id(db).amount == asset(10) );
      BOOST_CHECK_EQUAL( tick_3_id(db).value.value, 10 );

      BOOST_CHECK( tick_4_id(db).target_type == lock_forever );
      BOOST_CHECK( tick_4_id(db).current_type == liquid );
      BOOST_CHECK( tick_4_id(db).status == charging );
      BOOST_CHECK( tick_4_id(db).amount == asset(100000) );
      BOOST_CHECK_EQUAL( tick_4_id(db).value.value, 100000 );

      BOOST_CHECK( tick_5_id(db).target_type == lock_180_days );
      BOOST_CHECK( tick_5_id(db).current_type == liquid );
      BOOST_CHECK( tick_5_id(db).status == charging );
      BOOST_CHECK( tick_5_id(db).amount == asset(30) );
      BOOST_CHECK_EQUAL( tick_5_id(db).value.value, 30 );

      // 7 days passed
      generate_blocks( db.head_block_time() + fc::days(7) );
      set_expiration( db, trx );

      // ticket 2,3,4,5 should have upgraded
      BOOST_CHECK( tick_2_id(db).target_type == lock_360_days );
      BOOST_CHECK( tick_2_id(db).current_type == lock_180_days );
      BOOST_CHECK( tick_2_id(db).status == charging );
      BOOST_CHECK( tick_2_id(db).amount == asset(970) );
      BOOST_CHECK_EQUAL( tick_2_id(db).value.value, 970 * 2 );

      BOOST_CHECK( tick_3_id(db).target_type == lock_720_days );
      BOOST_CHECK( tick_3_id(db).current_type == lock_180_days );
      BOOST_CHECK( tick_3_id(db).status == charging );
      BOOST_CHECK( tick_3_id(db).amount == asset(10) );
      BOOST_CHECK_EQUAL( tick_3_id(db).value.value, 10 * 2 );

      BOOST_CHECK( tick_4_id(db).target_type == lock_forever );
      BOOST_CHECK( tick_4_id(db).current_type == lock_180_days );
      BOOST_CHECK( tick_4_id(db).status == charging );
      BOOST_CHECK( tick_4_id(db).amount == asset(100000) );
      BOOST_CHECK_EQUAL( tick_4_id(db).value.value, 100000 * 2 );

      BOOST_CHECK( tick_5_id(db).target_type == lock_180_days );
      BOOST_CHECK( tick_5_id(db).current_type == lock_180_days );
      BOOST_CHECK( tick_5_id(db).status == stable );
      BOOST_CHECK( tick_5_id(db).next_auto_update_time == time_point_sec::maximum() );
      BOOST_CHECK( tick_5_id(db).next_type_downgrade_time == time_point_sec::maximum() );
      BOOST_CHECK( tick_5_id(db).amount == asset(30) );
      BOOST_CHECK_EQUAL( tick_5_id(db).value.value, 30 * 2 );

      // split ticket 2, cancel upgrade of some
      result = update_ticket( tick_2_id(db), lock_180_days, asset(50) );

      BOOST_CHECK( tick_2_id(db).target_type == lock_360_days );
      BOOST_CHECK( tick_2_id(db).current_type == lock_180_days );
      BOOST_CHECK( tick_2_id(db).status == charging ); // 15 days to finish
      BOOST_CHECK( tick_2_id(db).amount == asset(920) );
      BOOST_CHECK_EQUAL( tick_2_id(db).value.value, 920 * 2 );

      BOOST_REQUIRE_EQUAL( result.new_objects.size(), 1u );

      ticket_id_type tick_6_id = *result.new_objects.begin();
      BOOST_CHECK( tick_6_id(db).target_type == lock_180_days );
      BOOST_CHECK( tick_6_id(db).current_type == lock_180_days );
      BOOST_CHECK( tick_6_id(db).status == withdrawing ); // 7 days to finish
      BOOST_CHECK( tick_6_id(db).amount == asset(50) );
      BOOST_CHECK_EQUAL( tick_6_id(db).value.value, 50 * 2 );

      // split ticket 2 again, downgrade some
      result = update_ticket( tick_2_id(db), liquid, asset(20) );

      BOOST_CHECK( tick_2_id(db).target_type == lock_360_days );
      BOOST_CHECK( tick_2_id(db).current_type == lock_180_days );
      BOOST_CHECK( tick_2_id(db).status == charging ); // 15 days to finish
      BOOST_CHECK( tick_2_id(db).amount == asset(900) );
      BOOST_CHECK_EQUAL( tick_2_id(db).value.value, 900 * 2 );

      BOOST_REQUIRE_EQUAL( result.new_objects.size(), 1u );

      ticket_id_type tick_7_id = *result.new_objects.begin();
      BOOST_CHECK( tick_7_id(db).target_type == liquid );
      BOOST_CHECK( tick_7_id(db).current_type == liquid );
      BOOST_CHECK( tick_7_id(db).status == withdrawing ); // 180 days to finish
      BOOST_CHECK( tick_7_id(db).amount == asset(20) );
      BOOST_CHECK_EQUAL( tick_7_id(db).value.value, 20 );

      // 2 days passed
      generate_blocks( db.head_block_time() + fc::days(2) );
      set_expiration( db, trx );

      // split ticket 5
      result = update_ticket( tick_5_id(db), liquid, asset(12) );

      BOOST_CHECK( tick_5_id(db).target_type == lock_180_days );
      BOOST_CHECK( tick_5_id(db).current_type == lock_180_days );
      BOOST_CHECK( tick_5_id(db).status == stable );
      BOOST_CHECK( tick_5_id(db).next_auto_update_time == time_point_sec::maximum() );
      BOOST_CHECK( tick_5_id(db).next_type_downgrade_time == time_point_sec::maximum() );
      BOOST_CHECK( tick_5_id(db).amount == asset(18) );
      BOOST_CHECK_EQUAL( tick_5_id(db).value.value, 18 * 2 );

      BOOST_REQUIRE_EQUAL( result.new_objects.size(), 1u );

      ticket_id_type tick_51_id = *result.new_objects.begin();
      BOOST_CHECK( tick_51_id(db).target_type == liquid );
      BOOST_CHECK( tick_51_id(db).current_type == liquid );
      BOOST_CHECK( tick_51_id(db).status == withdrawing ); // 180 days to finish
      BOOST_CHECK( tick_51_id(db).amount == asset(12) );
      BOOST_CHECK_EQUAL( tick_51_id(db).value.value, 12 );

      // split ticket 5 again
      result = update_ticket( tick_5_id(db), lock_forever, asset(13) );

      BOOST_CHECK( tick_5_id(db).target_type == lock_180_days );
      BOOST_CHECK( tick_5_id(db).current_type == lock_180_days );
      BOOST_CHECK( tick_5_id(db).status == stable );
      BOOST_CHECK( tick_5_id(db).next_auto_update_time == time_point_sec::maximum() );
      BOOST_CHECK( tick_5_id(db).next_type_downgrade_time == time_point_sec::maximum() );
      BOOST_CHECK( tick_5_id(db).amount == asset(5) );
      BOOST_CHECK_EQUAL( tick_5_id(db).value.value, 5 * 2 );

      BOOST_REQUIRE_EQUAL( result.new_objects.size(), 1u );

      ticket_id_type tick_52_id = *result.new_objects.begin();
      BOOST_CHECK( tick_52_id(db).target_type == lock_forever );
      BOOST_CHECK( tick_52_id(db).current_type == lock_180_days );
      BOOST_CHECK( tick_52_id(db).status == charging ); // 15 days to next step
      BOOST_CHECK( tick_52_id(db).amount == asset(13) );
      BOOST_CHECK_EQUAL( tick_52_id(db).value.value, 13 * 2 );

      // 1 day passed
      generate_blocks( db.head_block_time() + fc::days(1) );
      set_expiration( db, trx );

      // check ticket 5
      BOOST_CHECK( tick_5_id(db).target_type == lock_180_days );
      BOOST_CHECK( tick_5_id(db).current_type == lock_180_days );
      BOOST_CHECK( tick_5_id(db).status == stable );
      BOOST_CHECK( tick_5_id(db).next_auto_update_time == time_point_sec::maximum() );
      BOOST_CHECK( tick_5_id(db).next_type_downgrade_time == time_point_sec::maximum() );
      BOOST_CHECK( tick_5_id(db).amount == asset(5) );
      BOOST_CHECK_EQUAL( tick_5_id(db).value.value, 5 * 2 );

      // downgrade ticket 5
      result = update_ticket( tick_5_id(db), liquid, {} );

      BOOST_CHECK( tick_5_id(db).target_type == liquid );
      BOOST_CHECK( tick_5_id(db).current_type == liquid );
      BOOST_CHECK( tick_5_id(db).status == withdrawing ); // 180 days to finish
      BOOST_CHECK( tick_5_id(db).amount == asset(5) );
      BOOST_CHECK_EQUAL( tick_5_id(db).value.value, 5 );

      BOOST_REQUIRE_EQUAL( result.new_objects.size(), 0u );

      // check ticket 51
      BOOST_CHECK( tick_51_id(db).target_type == liquid );
      BOOST_CHECK( tick_51_id(db).current_type == liquid );
      BOOST_CHECK( tick_51_id(db).status == withdrawing ); // 179 days to finish
      BOOST_CHECK( tick_51_id(db).amount == asset(12) );
      BOOST_CHECK_EQUAL( tick_51_id(db).value.value, 12 );

      // cancel downgrading ticket 51
      result = update_ticket( tick_51_id(db), lock_180_days, asset(12) );

      BOOST_CHECK( tick_51_id(db).target_type == lock_180_days );
      BOOST_CHECK( tick_51_id(db).current_type == liquid );
      BOOST_CHECK( tick_51_id(db).status == charging ); // 15 days to finish
      BOOST_CHECK( tick_51_id(db).amount == asset(12) );
      BOOST_CHECK_EQUAL( tick_51_id(db).value.value, 12 );

      BOOST_REQUIRE_EQUAL( result.new_objects.size(), 0u );

      // check ticket 7
      BOOST_CHECK( tick_7_id(db).target_type == liquid );
      BOOST_CHECK( tick_7_id(db).current_type == liquid );
      BOOST_CHECK( tick_7_id(db).status == withdrawing ); // 177 days to finish
      BOOST_CHECK( tick_7_id(db).amount == asset(20) );
      BOOST_CHECK_EQUAL( tick_7_id(db).value.value, 20 );

      // partly cancel downgrading ticket 7
      result = update_ticket( tick_7_id(db), lock_180_days, asset(17) );

      BOOST_CHECK( tick_7_id(db).target_type == liquid );
      BOOST_CHECK( tick_7_id(db).current_type == liquid );
      BOOST_CHECK( tick_7_id(db).status == withdrawing ); // 177 days to finish
      BOOST_CHECK( tick_7_id(db).amount == asset(3) );
      BOOST_CHECK_EQUAL( tick_7_id(db).value.value, 3 );

      BOOST_REQUIRE_EQUAL( result.new_objects.size(), 1u );

      ticket_id_type tick_8_id = *result.new_objects.begin();
      BOOST_CHECK( tick_8_id(db).target_type == lock_180_days );
      BOOST_CHECK( tick_8_id(db).current_type == liquid );
      BOOST_CHECK( tick_8_id(db).status == charging ); // 15 days to finish
      BOOST_CHECK( tick_8_id(db).amount == asset(17) );
      BOOST_CHECK_EQUAL( tick_8_id(db).value.value, 17 );

      // downgrade some amount of ticket 6
      result = update_ticket( tick_6_id(db), liquid, asset(23) );

      BOOST_CHECK( tick_6_id(db).target_type == lock_180_days );
      BOOST_CHECK( tick_6_id(db).current_type == lock_180_days );
      BOOST_CHECK( tick_6_id(db).status == withdrawing ); // 4 days to finish
      BOOST_CHECK( tick_6_id(db).amount == asset(27) );
      BOOST_CHECK_EQUAL( tick_6_id(db).value.value, 27 * 2 );

      BOOST_REQUIRE_EQUAL( result.new_objects.size(), 1u );

      ticket_id_type tick_9_id = *result.new_objects.begin();
      BOOST_CHECK( tick_9_id(db).target_type == liquid );
      BOOST_CHECK( tick_9_id(db).current_type == lock_180_days );
      BOOST_CHECK( tick_9_id(db).status == withdrawing ); // 4 days to next step
      BOOST_CHECK( tick_9_id(db).amount == asset(23) );
      BOOST_CHECK_EQUAL( tick_9_id(db).value.value, 23 * 2 );

      // 4 days passed
      generate_blocks( db.head_block_time() + fc::days(4) );
      set_expiration( db, trx );

      // ticket 6 should be stable now, ticket 9 should have entered the next step, others no change
      BOOST_CHECK( tick_6_id(db).target_type == lock_180_days );
      BOOST_CHECK( tick_6_id(db).current_type == lock_180_days );
      BOOST_CHECK( tick_6_id(db).status == stable );
      BOOST_CHECK( tick_6_id(db).next_auto_update_time == time_point_sec::maximum() );
      BOOST_CHECK( tick_6_id(db).next_type_downgrade_time == time_point_sec::maximum() );
      BOOST_CHECK( tick_6_id(db).amount == asset(27) );
      BOOST_CHECK_EQUAL( tick_6_id(db).value.value, 27 * 2 );

      BOOST_CHECK( tick_9_id(db).target_type == liquid );
      BOOST_CHECK( tick_9_id(db).current_type == liquid );
      BOOST_CHECK( tick_9_id(db).status == withdrawing ); // 180 days to next step
      BOOST_CHECK( tick_9_id(db).amount == asset(23) );
      BOOST_CHECK_EQUAL( tick_9_id(db).value.value, 23 );

      BOOST_CHECK( tick_7_id(db).target_type == liquid );
      BOOST_CHECK( tick_7_id(db).current_type == liquid );
      BOOST_CHECK( tick_7_id(db).status == withdrawing ); // 173 days to finish
      BOOST_CHECK( tick_7_id(db).amount == asset(3) );
      BOOST_CHECK_EQUAL( tick_7_id(db).value.value, 3 );

      BOOST_CHECK( tick_8_id(db).target_type == lock_180_days );
      BOOST_CHECK( tick_8_id(db).current_type == liquid );
      BOOST_CHECK( tick_8_id(db).status == charging ); // 11 days to finish
      BOOST_CHECK( tick_8_id(db).amount == asset(17) );
      BOOST_CHECK_EQUAL( tick_8_id(db).value.value, 17 );

      BOOST_CHECK( tick_5_id(db).target_type == liquid );
      BOOST_CHECK( tick_5_id(db).current_type == liquid );
      BOOST_CHECK( tick_5_id(db).status == withdrawing ); // 176 days to finish
      BOOST_CHECK( tick_5_id(db).amount == asset(5) );
      BOOST_CHECK_EQUAL( tick_5_id(db).value.value, 5 );

      BOOST_CHECK( tick_51_id(db).target_type == lock_180_days );
      BOOST_CHECK( tick_51_id(db).current_type == liquid );
      BOOST_CHECK( tick_51_id(db).status == charging ); // 11 days to finish
      BOOST_CHECK( tick_51_id(db).amount == asset(12) );
      BOOST_CHECK_EQUAL( tick_51_id(db).value.value, 12 );

      BOOST_CHECK( tick_52_id(db).target_type == lock_forever );
      BOOST_CHECK( tick_52_id(db).current_type == lock_180_days );
      BOOST_CHECK( tick_52_id(db).status == charging ); // 10 days to finish
      BOOST_CHECK( tick_52_id(db).amount == asset(13) );
      BOOST_CHECK_EQUAL( tick_52_id(db).value.value, 13 * 2 );

      BOOST_CHECK( tick_2_id(db).target_type == lock_360_days );
      BOOST_CHECK( tick_2_id(db).current_type == lock_180_days );
      BOOST_CHECK( tick_2_id(db).status == charging ); // 8 days to finish
      BOOST_CHECK( tick_2_id(db).amount == asset(900) );
      BOOST_CHECK_EQUAL( tick_2_id(db).value.value, 900 * 2 );

      BOOST_CHECK( tick_3_id(db).target_type == lock_720_days );
      BOOST_CHECK( tick_3_id(db).current_type == lock_180_days );
      BOOST_CHECK( tick_3_id(db).status == charging ); // 8 days to next step
      BOOST_CHECK( tick_3_id(db).amount == asset(10) );
      BOOST_CHECK_EQUAL( tick_3_id(db).value.value, 10 * 2 );

      BOOST_CHECK( tick_4_id(db).target_type == lock_forever );
      BOOST_CHECK( tick_4_id(db).current_type == lock_180_days );
      BOOST_CHECK( tick_4_id(db).status == charging ); // 8 days to next step
      BOOST_CHECK( tick_4_id(db).amount == asset(100000) );
      BOOST_CHECK_EQUAL( tick_4_id(db).value.value, 100000 * 2 );

      // 8 days passed
      generate_blocks( db.head_block_time() + fc::days(8) );
      set_expiration( db, trx );

      // ticket 2,3,4 should be updated
      BOOST_CHECK( tick_6_id(db).target_type == lock_180_days );
      BOOST_CHECK( tick_6_id(db).current_type == lock_180_days );
      BOOST_CHECK( tick_6_id(db).status == stable );
      BOOST_CHECK( tick_6_id(db).next_auto_update_time == time_point_sec::maximum() );
      BOOST_CHECK( tick_6_id(db).next_type_downgrade_time == time_point_sec::maximum() );
      BOOST_CHECK( tick_6_id(db).amount == asset(27) );
      BOOST_CHECK_EQUAL( tick_6_id(db).value.value, 27 * 2 );

      BOOST_CHECK( tick_9_id(db).target_type == liquid );
      BOOST_CHECK( tick_9_id(db).current_type == liquid );
      BOOST_CHECK( tick_9_id(db).status == withdrawing ); // 172 days to finish
      BOOST_CHECK( tick_9_id(db).amount == asset(23) );
      BOOST_CHECK_EQUAL( tick_9_id(db).value.value, 23 );

      BOOST_CHECK( tick_7_id(db).target_type == liquid );
      BOOST_CHECK( tick_7_id(db).current_type == liquid );
      BOOST_CHECK( tick_7_id(db).status == withdrawing ); // 165 days to finish
      BOOST_CHECK( tick_7_id(db).amount == asset(3) );
      BOOST_CHECK_EQUAL( tick_7_id(db).value.value, 3 );

      BOOST_CHECK( tick_8_id(db).target_type == lock_180_days );
      BOOST_CHECK( tick_8_id(db).current_type == liquid );
      BOOST_CHECK( tick_8_id(db).status == charging ); // 3 days to finish
      BOOST_CHECK( tick_8_id(db).amount == asset(17) );
      BOOST_CHECK_EQUAL( tick_8_id(db).value.value, 17 );

      BOOST_CHECK( tick_5_id(db).target_type == liquid );
      BOOST_CHECK( tick_5_id(db).current_type == liquid );
      BOOST_CHECK( tick_5_id(db).status == withdrawing ); // 168 days to finish
      BOOST_CHECK( tick_5_id(db).amount == asset(5) );
      BOOST_CHECK_EQUAL( tick_5_id(db).value.value, 5 );

      BOOST_CHECK( tick_51_id(db).target_type == lock_180_days );
      BOOST_CHECK( tick_51_id(db).current_type == liquid );
      BOOST_CHECK( tick_51_id(db).status == charging ); // 3 days to finish
      BOOST_CHECK( tick_51_id(db).amount == asset(12) );
      BOOST_CHECK_EQUAL( tick_51_id(db).value.value, 12 );

      BOOST_CHECK( tick_52_id(db).target_type == lock_forever );
      BOOST_CHECK( tick_52_id(db).current_type == lock_180_days );
      BOOST_CHECK( tick_52_id(db).status == charging ); // 2 days to finish
      BOOST_CHECK( tick_52_id(db).amount == asset(13) );
      BOOST_CHECK_EQUAL( tick_52_id(db).value.value, 13 * 2 );

      BOOST_CHECK( tick_2_id(db).target_type == lock_360_days );
      BOOST_CHECK( tick_2_id(db).current_type == lock_360_days );
      BOOST_CHECK( tick_2_id(db).status == stable );
      BOOST_CHECK( tick_2_id(db).next_auto_update_time == time_point_sec::maximum() );
      BOOST_CHECK( tick_2_id(db).next_type_downgrade_time == time_point_sec::maximum() );
      BOOST_CHECK( tick_2_id(db).amount == asset(900) );
      BOOST_CHECK_EQUAL( tick_2_id(db).value.value, 900 * 4 );

      BOOST_CHECK( tick_3_id(db).target_type == lock_720_days );
      BOOST_CHECK( tick_3_id(db).current_type == lock_360_days );
      BOOST_CHECK( tick_3_id(db).status == charging ); // 15 days to next step
      BOOST_CHECK( tick_3_id(db).amount == asset(10) );
      BOOST_CHECK_EQUAL( tick_3_id(db).value.value, 10 * 4 );

      BOOST_CHECK( tick_4_id(db).target_type == lock_forever );
      BOOST_CHECK( tick_4_id(db).current_type == lock_360_days );
      BOOST_CHECK( tick_4_id(db).status == charging ); // 15 days to next step
      BOOST_CHECK( tick_4_id(db).amount == asset(100000) );
      BOOST_CHECK_EQUAL( tick_4_id(db).value.value, 100000 * 4 );

      // 3 days passed
      generate_blocks( db.head_block_time() + fc::days(3) );
      set_expiration( db, trx );

      // ticket 8,51,52 should be updated
      BOOST_CHECK( tick_6_id(db).target_type == lock_180_days );
      BOOST_CHECK( tick_6_id(db).current_type == lock_180_days );
      BOOST_CHECK( tick_6_id(db).status == stable );
      BOOST_CHECK( tick_6_id(db).next_auto_update_time == time_point_sec::maximum() );
      BOOST_CHECK( tick_6_id(db).next_type_downgrade_time == time_point_sec::maximum() );
      BOOST_CHECK( tick_6_id(db).amount == asset(27) );
      BOOST_CHECK_EQUAL( tick_6_id(db).value.value, 27 * 2 );

      BOOST_CHECK( tick_9_id(db).target_type == liquid );
      BOOST_CHECK( tick_9_id(db).current_type == liquid );
      BOOST_CHECK( tick_9_id(db).status == withdrawing ); // 169 days to finish
      BOOST_CHECK( tick_9_id(db).amount == asset(23) );
      BOOST_CHECK_EQUAL( tick_9_id(db).value.value, 23 );

      BOOST_CHECK( tick_7_id(db).target_type == liquid );
      BOOST_CHECK( tick_7_id(db).current_type == liquid );
      BOOST_CHECK( tick_7_id(db).status == withdrawing ); // 162 days to finish
      BOOST_CHECK( tick_7_id(db).amount == asset(3) );
      BOOST_CHECK_EQUAL( tick_7_id(db).value.value, 3 );

      BOOST_CHECK( tick_8_id(db).target_type == lock_180_days );
      BOOST_CHECK( tick_8_id(db).current_type == lock_180_days );
      BOOST_CHECK( tick_8_id(db).status == stable );
      BOOST_CHECK( tick_8_id(db).next_auto_update_time == time_point_sec::maximum() );
      BOOST_CHECK( tick_8_id(db).next_type_downgrade_time == time_point_sec::maximum() );
      BOOST_CHECK( tick_8_id(db).amount == asset(17) );
      BOOST_CHECK_EQUAL( tick_8_id(db).value.value, 17 * 2 );

      BOOST_CHECK( tick_5_id(db).target_type == liquid );
      BOOST_CHECK( tick_5_id(db).current_type == liquid );
      BOOST_CHECK( tick_5_id(db).status == withdrawing ); // 165 days to finish
      BOOST_CHECK( tick_5_id(db).amount == asset(5) );
      BOOST_CHECK_EQUAL( tick_5_id(db).value.value, 5 );

      BOOST_CHECK( tick_51_id(db).target_type == lock_180_days );
      BOOST_CHECK( tick_51_id(db).current_type == lock_180_days );
      BOOST_CHECK( tick_51_id(db).status == stable );
      BOOST_CHECK( tick_51_id(db).next_auto_update_time == time_point_sec::maximum() );
      BOOST_CHECK( tick_51_id(db).next_type_downgrade_time == time_point_sec::maximum() );
      BOOST_CHECK( tick_51_id(db).amount == asset(12) );
      BOOST_CHECK_EQUAL( tick_51_id(db).value.value, 12 * 2 );

      BOOST_CHECK( tick_52_id(db).target_type == lock_forever );
      BOOST_CHECK( tick_52_id(db).current_type == lock_360_days );
      BOOST_CHECK( tick_52_id(db).status == charging ); // 14 days to next step
      BOOST_CHECK( tick_52_id(db).amount == asset(13) );
      BOOST_CHECK_EQUAL( tick_52_id(db).value.value, 13 * 4 );

      BOOST_CHECK( tick_2_id(db).target_type == lock_360_days );
      BOOST_CHECK( tick_2_id(db).current_type == lock_360_days );
      BOOST_CHECK( tick_2_id(db).status == stable );
      BOOST_CHECK( tick_2_id(db).next_auto_update_time == time_point_sec::maximum() );
      BOOST_CHECK( tick_2_id(db).next_type_downgrade_time == time_point_sec::maximum() );
      BOOST_CHECK( tick_2_id(db).amount == asset(900) );
      BOOST_CHECK_EQUAL( tick_2_id(db).value.value, 900 * 4 );

      BOOST_CHECK( tick_3_id(db).target_type == lock_720_days );
      BOOST_CHECK( tick_3_id(db).current_type == lock_360_days );
      BOOST_CHECK( tick_3_id(db).status == charging ); // 12 days to finish
      BOOST_CHECK( tick_3_id(db).amount == asset(10) );
      BOOST_CHECK_EQUAL( tick_3_id(db).value.value, 10 * 4 );

      BOOST_CHECK( tick_4_id(db).target_type == lock_forever );
      BOOST_CHECK( tick_4_id(db).current_type == lock_360_days );
      BOOST_CHECK( tick_4_id(db).status == charging ); // 12 days to next step
      BOOST_CHECK( tick_4_id(db).amount == asset(100000) );
      BOOST_CHECK_EQUAL( tick_4_id(db).value.value, 100000 * 4 );

      // 170 days passed
      generate_blocks( db.head_block_time() + fc::days(170) );
      set_expiration( db, trx );

      // check tickets
      BOOST_CHECK( tick_6_id(db).target_type == lock_180_days );
      BOOST_CHECK( tick_6_id(db).current_type == lock_180_days );
      BOOST_CHECK( tick_6_id(db).status == stable );
      BOOST_CHECK( tick_6_id(db).next_auto_update_time == time_point_sec::maximum() );
      BOOST_CHECK( tick_6_id(db).next_type_downgrade_time == time_point_sec::maximum() );
      BOOST_CHECK( tick_6_id(db).amount == asset(27) );
      BOOST_CHECK_EQUAL( tick_6_id(db).value.value, 27 * 2 );

      BOOST_CHECK( !db.find( tick_9_id ) );
      BOOST_CHECK( !db.find( tick_7_id ) );

      BOOST_CHECK( tick_8_id(db).target_type == lock_180_days );
      BOOST_CHECK( tick_8_id(db).current_type == lock_180_days );
      BOOST_CHECK( tick_8_id(db).status == stable );
      BOOST_CHECK( tick_8_id(db).next_auto_update_time == time_point_sec::maximum() );
      BOOST_CHECK( tick_8_id(db).next_type_downgrade_time == time_point_sec::maximum() );
      BOOST_CHECK( tick_8_id(db).amount == asset(17) );
      BOOST_CHECK_EQUAL( tick_8_id(db).value.value, 17 * 2 );

      BOOST_CHECK( !db.find( tick_5_id ) );

      BOOST_CHECK( tick_51_id(db).target_type == lock_180_days );
      BOOST_CHECK( tick_51_id(db).current_type == lock_180_days );
      BOOST_CHECK( tick_51_id(db).status == stable );
      BOOST_CHECK( tick_51_id(db).next_auto_update_time == time_point_sec::maximum() );
      BOOST_CHECK( tick_51_id(db).next_type_downgrade_time == time_point_sec::maximum() );
      BOOST_CHECK( tick_51_id(db).amount == asset(12) );
      BOOST_CHECK_EQUAL( tick_51_id(db).value.value, 12 * 2 );

      BOOST_CHECK( tick_52_id(db).target_type == lock_forever );
      BOOST_CHECK( tick_52_id(db).current_type == lock_forever );
      BOOST_CHECK( tick_52_id(db).status == withdrawing ); // 39 days to next step
      BOOST_CHECK( tick_52_id(db).amount == asset(13) );
      BOOST_CHECK_EQUAL( tick_52_id(db).value.value, 13 * 8 );

      BOOST_CHECK( tick_2_id(db).target_type == lock_360_days );
      BOOST_CHECK( tick_2_id(db).current_type == lock_360_days );
      BOOST_CHECK( tick_2_id(db).status == stable );
      BOOST_CHECK( tick_2_id(db).next_auto_update_time == time_point_sec::maximum() );
      BOOST_CHECK( tick_2_id(db).next_type_downgrade_time == time_point_sec::maximum() );
      BOOST_CHECK( tick_2_id(db).amount == asset(900) );
      BOOST_CHECK_EQUAL( tick_2_id(db).value.value, 900 * 4 );

      BOOST_CHECK( tick_3_id(db).target_type == lock_720_days );
      BOOST_CHECK( tick_3_id(db).current_type == lock_720_days );
      BOOST_CHECK( tick_3_id(db).status == stable );
      BOOST_CHECK( tick_3_id(db).next_auto_update_time == time_point_sec::maximum() );
      BOOST_CHECK( tick_3_id(db).next_type_downgrade_time == time_point_sec::maximum() );
      BOOST_CHECK( tick_3_id(db).amount == asset(10) );
      BOOST_CHECK_EQUAL( tick_3_id(db).value.value, 10 * 8 );

      BOOST_CHECK( tick_4_id(db).target_type == lock_forever );
      BOOST_CHECK( tick_4_id(db).current_type == lock_forever );
      BOOST_CHECK( tick_4_id(db).status == withdrawing ); // 37 days to next step
      BOOST_CHECK( tick_4_id(db).amount == asset(100000) );
      BOOST_CHECK_EQUAL( tick_4_id(db).value.value, 100000 * 8 );

   } catch (fc::exception& e) {
      edump((e.to_detail_string()));
      throw;
   }
}

BOOST_AUTO_TEST_CASE( hf2262_test )
{ try {

      // Proceed to a time near the core-2262 hard fork.
      // Note: only works if the maintenance interval is less than 14 days
      auto mi = db.get_global_properties().parameters.maintenance_interval;
      generate_blocks( HARDFORK_CORE_2262_TIME - mi );
      set_expiration( db, trx );

      ACTORS((sam));

      auto init_amount = 10000000 * GRAPHENE_BLOCKCHAIN_PRECISION;
      fund( sam, asset(init_amount) );

      int64_t sam_balance = init_amount;

      // create a ticket
      const ticket_object& tick_1 = create_ticket( sam_id, lock_180_days, asset(100) );
      ticket_id_type tick_1_id = tick_1.id;

      BOOST_CHECK( tick_1_id(db).account == sam_id );
      BOOST_CHECK( tick_1_id(db).target_type == lock_180_days );
      BOOST_CHECK( tick_1_id(db).current_type == liquid );
      BOOST_CHECK( tick_1_id(db).status == charging );
      BOOST_CHECK( tick_1_id(db).amount == asset(100) );
      BOOST_CHECK_EQUAL( tick_1_id(db).value.value, 100 );
      sam_balance -= 100;
      BOOST_CHECK_EQUAL( db.get_balance( sam_id, asset_id_type() ).amount.value, sam_balance );

      auto create_time =  db.head_block_time();

      // activate hf2262
      generate_blocks( db.get_dynamic_global_properties().next_maintenance_time );
      generate_block();

      BOOST_REQUIRE( db.head_block_time() < create_time + fc::days(14) );

      BOOST_CHECK( tick_1_id(db).account == sam_id );
      BOOST_CHECK( tick_1_id(db).target_type == lock_180_days );
      BOOST_CHECK( tick_1_id(db).current_type == liquid );
      BOOST_CHECK( tick_1_id(db).status == charging );
      BOOST_CHECK( tick_1_id(db).amount == asset(100) );
      BOOST_CHECK_EQUAL( tick_1_id(db).value.value, 0 );
      BOOST_CHECK_EQUAL( db.get_balance( sam_id, asset_id_type() ).amount.value, sam_balance );

      // 14 days passed
      generate_blocks( create_time + fc::days(14) );
      set_expiration( db, trx );

      // no change
      BOOST_CHECK( tick_1_id(db).account == sam_id );
      BOOST_CHECK( tick_1_id(db).target_type == lock_180_days );
      BOOST_CHECK( tick_1_id(db).current_type == liquid );
      BOOST_CHECK( tick_1_id(db).status == charging );
      BOOST_CHECK( tick_1_id(db).amount == asset(100) );
      BOOST_CHECK_EQUAL( tick_1_id(db).value.value, 0 );
      BOOST_CHECK_EQUAL( db.get_balance( sam_id, asset_id_type() ).amount.value, sam_balance );

      // unable to update ticket if not to change target type
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_180_days, {} ), fc::exception );
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_180_days, asset(1) ), fc::exception );
      BOOST_CHECK_THROW( update_ticket( tick_1_id(db), lock_180_days, asset(100) ), fc::exception );

      // split ticket 1, cancel some
      auto result = update_ticket( tick_1_id(db), liquid, asset(6) );

      BOOST_CHECK( tick_1_id(db).account == sam_id );
      BOOST_CHECK( tick_1_id(db).target_type == lock_180_days );
      BOOST_CHECK( tick_1_id(db).current_type == liquid );
      BOOST_CHECK( tick_1_id(db).status == charging );
      BOOST_CHECK( tick_1_id(db).amount == asset(94) );
      BOOST_CHECK_EQUAL( tick_1_id(db).value.value, 0 );
      BOOST_CHECK_EQUAL( db.get_balance( sam_id, asset_id_type() ).amount.value, sam_balance );

      BOOST_REQUIRE_EQUAL( result.new_objects.size(), 1u );

      ticket_id_type tick_2_id = *result.new_objects.begin();
      BOOST_CHECK( tick_2_id(db).target_type == liquid );
      BOOST_CHECK( tick_2_id(db).current_type == liquid );
      BOOST_CHECK( tick_2_id(db).status == withdrawing );
      BOOST_CHECK( tick_2_id(db).amount == asset(6) );
      BOOST_CHECK_EQUAL( tick_2_id(db).value.value, 0 );

      // 1 day passed
      generate_blocks( db.head_block_time() + fc::days(1) );
      set_expiration( db, trx );

      // ticket should be stable now
      BOOST_CHECK( tick_1_id(db).account == sam_id );
      BOOST_CHECK( tick_1_id(db).target_type == lock_180_days );
      BOOST_CHECK( tick_1_id(db).current_type == lock_180_days );
      BOOST_CHECK( tick_1_id(db).status == stable );
      BOOST_CHECK( tick_1_id(db).next_auto_update_time == time_point_sec::maximum() );
      BOOST_CHECK( tick_1_id(db).next_type_downgrade_time == time_point_sec::maximum() );
      BOOST_CHECK( tick_1_id(db).amount == asset(94) );
      BOOST_CHECK_EQUAL( tick_1_id(db).value.value, 94 * 2 );
      BOOST_CHECK_EQUAL( db.get_balance( sam_id, asset_id_type() ).amount.value, sam_balance );

      // split ticket 1, downgrade some
      result = update_ticket( tick_1_id(db), liquid, asset(10) );

      BOOST_CHECK( tick_1_id(db).account == sam_id );
      BOOST_CHECK( tick_1_id(db).target_type == lock_180_days );
      BOOST_CHECK( tick_1_id(db).current_type == lock_180_days );
      BOOST_CHECK( tick_1_id(db).status == stable );
      BOOST_CHECK( tick_1_id(db).next_auto_update_time == time_point_sec::maximum() );
      BOOST_CHECK( tick_1_id(db).next_type_downgrade_time == time_point_sec::maximum() );
      BOOST_CHECK( tick_1_id(db).amount == asset(84) );
      BOOST_CHECK_EQUAL( tick_1_id(db).value.value, 84 * 2 );
      BOOST_CHECK_EQUAL( db.get_balance( sam_id, asset_id_type() ).amount.value, sam_balance );

      BOOST_REQUIRE_EQUAL( result.new_objects.size(), 1u );

      ticket_id_type tick_3_id = *result.new_objects.begin();
      BOOST_CHECK( tick_3_id(db).target_type == liquid );
      BOOST_CHECK( tick_3_id(db).current_type == liquid );
      BOOST_CHECK( tick_3_id(db).status == withdrawing );
      BOOST_CHECK( tick_3_id(db).amount == asset(10) );
      BOOST_CHECK_EQUAL( tick_3_id(db).value.value, 0 );

      // update ticket 1, downgrade all
      update_ticket( tick_1_id(db), liquid, {} );

      // check new data
      BOOST_CHECK( tick_1_id(db).account == sam_id );
      BOOST_CHECK( tick_1_id(db).target_type == liquid );
      BOOST_CHECK( tick_1_id(db).current_type == liquid );
      BOOST_CHECK( tick_1_id(db).status == withdrawing );
      BOOST_CHECK( tick_1_id(db).amount == asset(84) );
      BOOST_CHECK_EQUAL( tick_1_id(db).value.value, 0 );
      BOOST_CHECK_EQUAL( db.get_balance( sam_id, asset_id_type() ).amount.value, sam_balance );

      // create a new ticket
      const ticket_object& tick_4 = create_ticket( sam_id, lock_360_days, asset(200) );
      ticket_id_type tick_4_id = tick_4.id;

      BOOST_CHECK( tick_4_id(db).account == sam_id );
      BOOST_CHECK( tick_4_id(db).target_type == lock_360_days );
      BOOST_CHECK( tick_4_id(db).current_type == liquid );
      BOOST_CHECK( tick_4_id(db).status == charging );
      BOOST_CHECK( tick_4_id(db).amount == asset(200) );
      BOOST_CHECK_EQUAL( tick_4_id(db).value.value, 0 );
      sam_balance -= 200;
      BOOST_CHECK_EQUAL( db.get_balance( sam_id, asset_id_type() ).amount.value, sam_balance );

   } FC_LOG_AND_RETHROW()
}

BOOST_AUTO_TEST_CASE( hf2262_auto_update_test )
{ try {

      INVOKE( one_lock_360_ticket );

      // activate hf2262
      auto mi = db.get_global_properties().parameters.maintenance_interval;
      generate_blocks( HARDFORK_CORE_2262_TIME - mi );
      generate_blocks( db.get_dynamic_global_properties().next_maintenance_time );

      GET_ACTOR( sam );

      int64_t sam_balance = db.get_balance( sam_id, asset_id_type() ).amount.value;

      ticket_id_type tick_1_id; // default value

      // withdraw the ticket
      auto result = update_ticket( tick_1_id(db), liquid, {} );
      BOOST_CHECK_EQUAL( result.new_objects.size(), 0u );

      BOOST_CHECK( tick_1_id(db).target_type == liquid );
      BOOST_CHECK( tick_1_id(db).current_type == lock_180_days );
      BOOST_CHECK( tick_1_id(db).status == withdrawing );
      BOOST_CHECK( tick_1_id(db).amount == asset(100) );
      BOOST_CHECK_EQUAL( tick_1_id(db).value.value, 100 * 2 );
      BOOST_CHECK_EQUAL( db.get_balance( sam_id, asset_id_type() ).amount.value, sam_balance );

      // 179 days passed
      generate_blocks( db.head_block_time() + fc::days(179) );
      set_expiration( db, trx );

      // no change
      BOOST_CHECK( tick_1_id(db).target_type == liquid );
      BOOST_CHECK( tick_1_id(db).current_type == lock_180_days );
      BOOST_CHECK( tick_1_id(db).status == withdrawing );
      BOOST_CHECK( tick_1_id(db).amount == asset(100) );
      BOOST_CHECK_EQUAL( tick_1_id(db).value.value, 100  * 2 );
      BOOST_CHECK_EQUAL( db.get_balance( sam_id, asset_id_type() ).amount.value, sam_balance );

      // 1 day passed
      generate_blocks( db.head_block_time() + fc::days(1) );
      set_expiration( db, trx );

      // the ticket should have downgraded
      BOOST_CHECK( tick_1_id(db).target_type == liquid );
      BOOST_CHECK( tick_1_id(db).current_type == liquid );
      BOOST_CHECK( tick_1_id(db).status == withdrawing );
      BOOST_CHECK( tick_1_id(db).amount == asset(100) );
      BOOST_CHECK_EQUAL( tick_1_id(db).value.value, 0 );
      BOOST_CHECK_EQUAL( db.get_balance( sam_id, asset_id_type() ).amount.value, sam_balance );

      // 179 days passed
      generate_blocks( db.head_block_time() + fc::days(179) );
      set_expiration( db, trx );

      // no change
      BOOST_CHECK( tick_1_id(db).target_type == liquid );
      BOOST_CHECK( tick_1_id(db).current_type == liquid );
      BOOST_CHECK( tick_1_id(db).status == withdrawing );
      BOOST_CHECK( tick_1_id(db).amount == asset(100) );
      BOOST_CHECK_EQUAL( tick_1_id(db).value.value, 0 );
      BOOST_CHECK_EQUAL( db.get_balance( sam_id, asset_id_type() ).amount.value, sam_balance );

      // 1 day passed
      generate_blocks( db.head_block_time() + fc::days(1) );
      set_expiration( db, trx );

      // the ticket should be freed
      BOOST_CHECK( !db.find( tick_1_id ) );

      BOOST_CHECK_EQUAL( db.get_balance( sam_id, asset_id_type() ).amount.value, sam_balance + 100 );

   } FC_LOG_AND_RETHROW()
}


BOOST_AUTO_TEST_SUITE_END()
