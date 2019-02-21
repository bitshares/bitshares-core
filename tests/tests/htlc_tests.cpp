/*
 * Copyright (c) 2018 jmjatlanta and contributors.
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

// below are for random bytes for htlc
#include <vector>
#include <random>
#include <climits>
#include <algorithm>
#include <functional>
// for htlc timeout
#include <chrono>
#include <thread>

#include <boost/test/unit_test.hpp>

#include <boost/container/flat_set.hpp>

#include <fc/optional.hpp>

#include <graphene/chain/protocol/htlc.hpp>

#include <graphene/chain/database.hpp>
#include <graphene/chain/hardfork.hpp>

#include <graphene/chain/balance_object.hpp>
#include <graphene/chain/market_object.hpp>
#include <graphene/chain/htlc_object.hpp>

#include <graphene/app/api.hpp>

#include "../common/database_fixture.hpp"

using namespace graphene::chain;
using namespace graphene::chain::test;

BOOST_FIXTURE_TEST_SUITE( htlc_tests, database_fixture )

void generate_random_preimage(uint16_t key_size, std::vector<char>& vec)
{
	std::independent_bits_engine<std::default_random_engine, CHAR_BIT, unsigned char> rbe;
	std::generate(begin(vec), end(vec), std::ref(rbe));
	return;
}

/****
 * Hash the preimage and put it in a vector
 * @param preimage the preimage
 * @returns a vector that cointains the sha256 hash of the preimage
 */
template<typename H>
H hash_it(std::vector<char> preimage)
{
   return H::hash( (char*)preimage.data(), preimage.size() );
}

flat_map< uint64_t, graphene::chain::fee_parameters > get_htlc_fee_parameters()
{
   flat_map<uint64_t, graphene::chain::fee_parameters> ret_val;

   htlc_create_operation::fee_parameters_type create_param;
   create_param.fee_per_day = 2 * GRAPHENE_BLOCKCHAIN_PRECISION;
   create_param.fee = 2 * GRAPHENE_BLOCKCHAIN_PRECISION;
   ret_val[((operation)htlc_create_operation()).which()] = create_param;

   htlc_redeem_operation::fee_parameters_type redeem_param;
   redeem_param.fee = 2 * GRAPHENE_BLOCKCHAIN_PRECISION;
   redeem_param.fee_per_kb = 2 * GRAPHENE_BLOCKCHAIN_PRECISION;
   ret_val[((operation)htlc_redeem_operation()).which()] = redeem_param;

   htlc_extend_operation::fee_parameters_type extend_param;
   extend_param.fee = 2 * GRAPHENE_BLOCKCHAIN_PRECISION;
   extend_param.fee_per_day = 2 * GRAPHENE_BLOCKCHAIN_PRECISION;
   ret_val[((operation)htlc_extend_operation()).which()] = extend_param;

   return ret_val;
}

/****
 * @brief push through a proposal that sets htlc parameters and fees
 * @param db_fixture the database connection
 */
void set_committee_parameters(database_fixture* db_fixture)
{
   // htlc fees
   // get existing fee_schedule
   const chain_parameters& existing_params = db_fixture->db.get_global_properties().parameters;
   const fee_schedule_type& existing_fee_schedule = *(existing_params.current_fees);
   // create a new fee_shedule
   fee_schedule_type new_fee_schedule;
   new_fee_schedule.scale = GRAPHENE_100_PERCENT;
   // replace the old with the new
   flat_map<uint64_t, graphene::chain::fee_parameters> params_map = get_htlc_fee_parameters();
   for(auto param : existing_fee_schedule.parameters)
   {
      auto itr = params_map.find(param.which());
      if (itr == params_map.end())
         new_fee_schedule.parameters.insert(param);
      else
      {
         new_fee_schedule.parameters.insert( (*itr).second);
      }
   }
   // htlc parameters
   proposal_create_operation cop = proposal_create_operation::committee_proposal(
         db_fixture->db.get_global_properties().parameters, db_fixture->db.head_block_time());
   cop.fee_paying_account = GRAPHENE_TEMP_ACCOUNT;
   cop.expiration_time = db_fixture->db.head_block_time() + *cop.review_period_seconds + 10;
   committee_member_update_global_parameters_operation uop;
   graphene::chain::htlc_options new_params;
   new_params.max_preimage_size = 19200;
   new_params.max_timeout_secs = 60 * 60 * 24 * 28;
   uop.new_parameters.extensions.value.updatable_htlc_options = new_params;
   uop.new_parameters.current_fees = new_fee_schedule;
   cop.proposed_ops.emplace_back(uop);
   
   db_fixture->trx.operations.push_back(cop);
   graphene::chain::processed_transaction proc_trx =db_fixture->db.push_transaction(db_fixture->trx);
   db_fixture->trx.clear();
   proposal_id_type good_proposal_id = proc_trx.operation_results[0].get<object_id_type>();

   proposal_update_operation puo;
   puo.proposal = good_proposal_id;
   puo.fee_paying_account = GRAPHENE_TEMP_ACCOUNT;
   puo.key_approvals_to_add.emplace( db_fixture->init_account_priv_key.get_public_key() );
   db_fixture->trx.operations.push_back(puo);
   db_fixture->sign( db_fixture->trx, db_fixture->init_account_priv_key );
   db_fixture->db.push_transaction(db_fixture->trx);
   db_fixture->trx.clear();

   db_fixture->generate_blocks( good_proposal_id( db_fixture->db ).expiration_time + 5 );
   db_fixture->generate_blocks( db_fixture->db.get_dynamic_global_properties().next_maintenance_time );
   db_fixture->generate_block();   // get the maintenance skip slots out of the way

}

void advance_past_hardfork(database_fixture* db_fixture)
{
   db_fixture->generate_blocks(HARDFORK_CORE_1468_TIME);
   set_expiration(db_fixture->db, db_fixture->trx);
   set_committee_parameters(db_fixture);
   set_expiration(db_fixture->db, db_fixture->trx);
}

BOOST_AUTO_TEST_CASE( htlc_expires )
{
try {
   ACTORS((alice)(bob));

   int64_t init_balance(100 * GRAPHENE_BLOCKCHAIN_PRECISION);

   transfer( committee_account, alice_id, graphene::chain::asset(init_balance) );

   advance_past_hardfork(this);

   uint16_t preimage_size = 256;
   std::vector<char> pre_image(256);
   generate_random_preimage(preimage_size, pre_image);

   graphene::chain::htlc_id_type alice_htlc_id;
   // cler everything out
   generate_block();
   trx.clear();
   // Alice puts a contract on the blockchain
   {
      graphene::chain::htlc_create_operation create_operation;
      BOOST_TEST_MESSAGE("Alice (who has 100 coins, is transferring 2 coins to Bob");
      create_operation.amount = graphene::chain::asset( 3 * GRAPHENE_BLOCKCHAIN_PRECISION );
      create_operation.to = bob_id;
      create_operation.claim_period_seconds = 60;
      create_operation.preimage_hash = hash_it<fc::sha256>( pre_image );
      create_operation.preimage_size = preimage_size;
      create_operation.from = alice_id;
      create_operation.fee = db.get_global_properties().parameters.current_fees->calculate_fee(create_operation);
      trx.operations.push_back(create_operation);
      sign(trx, alice_private_key);
      PUSH_TX(db, trx, ~0);
      trx.clear();
      graphene::chain::signed_block blk = generate_block();
      processed_transaction alice_trx = blk.transactions[0];
      alice_htlc_id = alice_trx.operation_results[0].get<object_id_type>();
      generate_block();
   }

   // verify funds on hold... 100 - 3 = 97, minus the 4 coin fee = 93
   BOOST_CHECK_EQUAL( get_balance(alice_id, graphene::chain::asset_id_type()), 93 * GRAPHENE_BLOCKCHAIN_PRECISION );

   // make sure Bob (or anyone) can see the details of the transaction
   graphene::app::database_api db_api(db);
   auto obj = db_api.get_objects( {alice_htlc_id }).front();
   graphene::chain::htlc_object htlc = obj.template as<graphene::chain::htlc_object>(GRAPHENE_MAX_NESTED_OBJECTS);

   // let it expire (wait for timeout)
   generate_blocks( db.head_block_time() + fc::seconds(120) );
   // verify funds return (minus the fees)
   BOOST_CHECK_EQUAL( get_balance(alice_id, graphene::chain::asset_id_type()), 96 * GRAPHENE_BLOCKCHAIN_PRECISION );
   // verify Bob cannot execute the contract after the fact
} FC_LOG_AND_RETHROW()
}

BOOST_AUTO_TEST_CASE( htlc_fulfilled )
{
try {
   ACTORS((alice)(bob));

   int64_t init_balance(100 * GRAPHENE_BLOCKCHAIN_PRECISION);

   transfer( committee_account, alice_id, graphene::chain::asset(init_balance) );
   transfer( committee_account, bob_id, graphene::chain::asset(init_balance) );

   advance_past_hardfork(this);
   
   uint16_t preimage_size = 256;
   std::vector<char> pre_image(preimage_size);
   generate_random_preimage(preimage_size, pre_image);

   graphene::chain::htlc_id_type alice_htlc_id;
   // clear everything out
   generate_block();
   trx.clear();
   // Alice puts a contract on the blockchain
   {
      graphene::chain::htlc_create_operation create_operation;

      create_operation.amount = graphene::chain::asset( 20 * GRAPHENE_BLOCKCHAIN_PRECISION );
      create_operation.to = bob_id;
      create_operation.claim_period_seconds = 86400;
      create_operation.preimage_hash = hash_it<fc::sha1>( pre_image );
      create_operation.preimage_size = preimage_size;
      create_operation.from = alice_id;
      create_operation.fee = db.current_fee_schedule().calculate_fee( create_operation );
      trx.operations.push_back( create_operation );
      sign(trx, alice_private_key);
      PUSH_TX(db, trx, ~0);
      trx.clear();
      graphene::chain::signed_block blk = generate_block();
      processed_transaction alice_trx = blk.transactions[0];
      alice_htlc_id = alice_trx.operation_results[0].get<object_id_type>();
   }

   // make sure Alice's money gets put on hold (100 - 20 - 4(fee) )
   BOOST_CHECK_EQUAL( get_balance( alice_id, graphene::chain::asset_id_type()), 76 * GRAPHENE_BLOCKCHAIN_PRECISION );

   // extend the timeout so that Bob has more time
   {
      graphene::chain::htlc_extend_operation extend_operation;
      extend_operation.htlc_id = alice_htlc_id;
      extend_operation.seconds_to_add = 86400;
      extend_operation.update_issuer = alice_id;
      extend_operation.fee = db.current_fee_schedule().calculate_fee( extend_operation );
      trx.operations.push_back( extend_operation );
      sign( trx, alice_private_key );
      PUSH_TX( db, trx, ~0 );
      trx.clear();
      generate_blocks( db.head_block_time() + fc::seconds(87000) );
      set_expiration( db, trx );
   }

   // make sure Alice's money is still on hold, and account for extra fee
   BOOST_CHECK_EQUAL( get_balance( alice_id, graphene::chain::asset_id_type()), 72 * GRAPHENE_BLOCKCHAIN_PRECISION );

   // send a redeem operation to claim the funds
   {
      graphene::chain::htlc_redeem_operation update_operation;
      update_operation.redeemer = bob_id;
      update_operation.htlc_id = alice_htlc_id;
      update_operation.preimage = pre_image;
      update_operation.fee = db.current_fee_schedule().calculate_fee( update_operation );
      trx.operations.push_back( update_operation );
      sign(trx, bob_private_key);
      PUSH_TX( db, trx, ~0 );
      generate_block();
      trx.clear();
   }
   // verify funds end up in Bob's account (100 + 20 - 4(fee) )
   BOOST_CHECK_EQUAL( get_balance(bob_id,   graphene::chain::asset_id_type()), 116 * GRAPHENE_BLOCKCHAIN_PRECISION );
   // verify funds remain out of Alice's acount ( 100 - 20 - 4 )
   BOOST_CHECK_EQUAL( get_balance(alice_id, graphene::chain::asset_id_type()), 72 * GRAPHENE_BLOCKCHAIN_PRECISION );
} FC_LOG_AND_RETHROW()
}

BOOST_AUTO_TEST_CASE( other_peoples_money )
{
try {
   advance_past_hardfork(this);
	
   ACTORS((alice)(bob));

   int64_t init_balance(100 * GRAPHENE_BLOCKCHAIN_PRECISION );

   transfer( committee_account, alice_id, graphene::chain::asset(init_balance) );

   uint16_t preimage_size = 256;
   std::vector<char> pre_image(256);
   generate_random_preimage(preimage_size, pre_image);

   graphene::chain::htlc_id_type alice_htlc_id;
   // cler everything out
   generate_block();
   trx.clear();
   // Bob attempts to put a contract on the blockchain using Alice's funds
   {
      graphene::chain::htlc_create_operation create_operation;
      create_operation.amount = graphene::chain::asset( 1 * GRAPHENE_BLOCKCHAIN_PRECISION );
      create_operation.to = bob_id;
      create_operation.claim_period_seconds = 3;
      create_operation.preimage_hash = hash_it<fc::ripemd160>( pre_image );
      create_operation.preimage_size = preimage_size;
      create_operation.from = alice_id;
      create_operation.fee = db.current_fee_schedule().calculate_fee( create_operation );
      trx.operations.push_back(create_operation);
      sign(trx, bob_private_key);
      GRAPHENE_CHECK_THROW( PUSH_TX( db, trx ), fc::exception);
      trx.clear();
   }
   // now try the same but with Alice's signature (should work)
   {
      graphene::chain::htlc_create_operation create_operation;
      create_operation.amount = graphene::chain::asset( 1 * GRAPHENE_BLOCKCHAIN_PRECISION );
      create_operation.to = bob_id;
      create_operation.claim_period_seconds = 3;
      create_operation.preimage_hash = hash_it<fc::ripemd160>( pre_image );
      create_operation.preimage_size = preimage_size;
      create_operation.from = alice_id;
      create_operation.fee = db.current_fee_schedule().calculate_fee( create_operation );
      trx.operations.push_back(create_operation);
      sign(trx, alice_private_key);
      PUSH_TX( db, trx );
      trx.clear();
   }
} FC_LOG_AND_RETHROW()
}

BOOST_AUTO_TEST_CASE( htlc_hardfork_test )
{ 
   try {
      {
         // try to set committee parameters before hardfork
         proposal_create_operation cop = proposal_create_operation::committee_proposal(
               db.get_global_properties().parameters, db.head_block_time());
         cop.fee_paying_account = GRAPHENE_TEMP_ACCOUNT;
         cop.expiration_time = db.head_block_time() + *cop.review_period_seconds + 10;
         committee_member_update_global_parameters_operation cmuop;
         graphene::chain::htlc_options new_params;
         new_params.max_preimage_size = 2048;
         new_params.max_timeout_secs = 60 * 60 * 24 * 28;
         cmuop.new_parameters.extensions.value.updatable_htlc_options = new_params;
         cop.proposed_ops.emplace_back(cmuop);
         trx.operations.push_back(cop);
         // update with signatures
         proposal_update_operation uop;
         uop.fee_paying_account = GRAPHENE_TEMP_ACCOUNT;
         uop.active_approvals_to_add = {get_account("init0").get_id(), get_account("init1").get_id(),
                                       get_account("init2").get_id(), get_account("init3").get_id(),
                                       get_account("init4").get_id(), get_account("init5").get_id(),
                                       get_account("init6").get_id(), get_account("init7").get_id()};
         trx.operations.push_back(uop);
         sign( trx, init_account_priv_key );
         BOOST_TEST_MESSAGE("Sending proposal.");
         GRAPHENE_CHECK_THROW(db.push_transaction(trx), fc::exception);
         BOOST_TEST_MESSAGE("Verifying that proposal did not succeeed.");
         BOOST_CHECK(!db.get_global_properties().parameters.extensions.value.updatable_htlc_options.valid());
         trx.clear();
      }

      {
         BOOST_TEST_MESSAGE("Attempting to set HTLC fees before hard fork.");

         // get existing fee_schedule
         const chain_parameters& existing_params = db.get_global_properties().parameters;
         const fee_schedule_type& existing_fee_schedule = *(existing_params.current_fees);
         // create a new fee_shedule
         fee_schedule_type new_fee_schedule;
         new_fee_schedule.scale = existing_fee_schedule.scale;
         // replace the old with the new
         flat_map<uint64_t, graphene::chain::fee_parameters> params_map = get_htlc_fee_parameters();
         for(auto param : existing_fee_schedule.parameters)
         {
            auto itr = params_map.find(param.which());
            if (itr == params_map.end())
               new_fee_schedule.parameters.insert(param);
            else
            {
               new_fee_schedule.parameters.insert( (*itr).second);
            }
         }
         proposal_create_operation cop = proposal_create_operation::committee_proposal(
               db.get_global_properties().parameters, db.head_block_time());
         cop.fee_paying_account = GRAPHENE_TEMP_ACCOUNT;
         cop.expiration_time = db.head_block_time() + *cop.review_period_seconds + 10;
         committee_member_update_global_parameters_operation uop;
         uop.new_parameters.current_fees = new_fee_schedule;
         cop.proposed_ops.emplace_back(uop);
         cop.fee = asset( 100000 );
         trx.operations.push_back( cop );
         GRAPHENE_CHECK_THROW(db.push_transaction( trx ), fc::exception);
         trx.clear();
      }

      // now things should start working...
      BOOST_TEST_MESSAGE("Advancing to HTLC hardfork time.");
      advance_past_hardfork(this);

      proposal_id_type good_proposal_id;
      BOOST_TEST_MESSAGE( "Creating a proposal to change the max_preimage_size to 2048 and set higher fees" );
      {
         // get existing fee_schedule
         const chain_parameters& existing_params = db.get_global_properties().parameters;
         const fee_schedule_type& existing_fee_schedule = *(existing_params.current_fees);
         // create a new fee_shedule
         fee_schedule_type new_fee_schedule;
         new_fee_schedule.scale = existing_fee_schedule.scale;
         // replace the old with the new
         flat_map<uint64_t, graphene::chain::fee_parameters> params_map = get_htlc_fee_parameters();
         for(auto param : existing_fee_schedule.parameters)
         {
            auto itr = params_map.find(param.which());
            if (itr == params_map.end())
               new_fee_schedule.parameters.insert(param);
            else
            {
               new_fee_schedule.parameters.insert( (*itr).second);
            }
         }
         proposal_create_operation cop = proposal_create_operation::committee_proposal(db.get_global_properties().parameters, db.head_block_time());
         cop.fee_paying_account = GRAPHENE_TEMP_ACCOUNT;
         cop.expiration_time = db.head_block_time() + *cop.review_period_seconds + 10;
         committee_member_update_global_parameters_operation uop;
         graphene::chain::htlc_options new_params;
         new_params.max_preimage_size = 2048;
         new_params.max_timeout_secs = 60 * 60 * 24 * 28;
         uop.new_parameters.extensions.value.updatable_htlc_options = new_params;
         uop.new_parameters.current_fees = new_fee_schedule;
         cop.proposed_ops.emplace_back(uop);
         trx.operations.push_back(cop);
         graphene::chain::processed_transaction proc_trx =db.push_transaction(trx);
         good_proposal_id = proc_trx.operation_results[0].get<object_id_type>();
      }

      BOOST_TEST_MESSAGE( "Updating proposal by signing with the committee_member private key" );
      {
         proposal_update_operation uop;
         uop.proposal = good_proposal_id;
         uop.fee_paying_account = GRAPHENE_TEMP_ACCOUNT;
         uop.active_approvals_to_add = {get_account("init0").get_id(), get_account("init1").get_id(),
                                       get_account("init2").get_id(), get_account("init3").get_id(),
                                       get_account("init4").get_id(), get_account("init5").get_id(),
                                       get_account("init6").get_id(), get_account("init7").get_id()};
         trx.operations.push_back(uop);
         sign( trx, init_account_priv_key );
         db.push_transaction(trx);
         BOOST_CHECK(good_proposal_id(db).is_authorized_to_execute(db));
      }
      BOOST_TEST_MESSAGE( "Verifying that the parameters didn't change immediately" );

      BOOST_CHECK_EQUAL(db.get_global_properties().parameters.extensions.value.updatable_htlc_options->max_preimage_size, 19200u);

      BOOST_TEST_MESSAGE( "Generating blocks until proposal expires" );
      generate_blocks(good_proposal_id(db).expiration_time + 5);
      BOOST_TEST_MESSAGE( "Verify that the parameters still have not changed" );
      BOOST_CHECK_EQUAL(db.get_global_properties().parameters.extensions.value.updatable_htlc_options->max_preimage_size, 19200u);

      BOOST_TEST_MESSAGE( "Generating blocks until next maintenance interval" );
      generate_blocks(db.get_dynamic_global_properties().next_maintenance_time);
      generate_block();   // get the maintenance skip slots out of the way

      BOOST_TEST_MESSAGE( "Verify that the change has been implemented" );
      BOOST_CHECK_EQUAL(db.get_global_properties().parameters.extensions.value.updatable_htlc_options->max_preimage_size, 2048u);
      BOOST_CHECK_EQUAL(db.get_global_properties().parameters.current_fees->get<htlc_create_operation>().fee, 2 * GRAPHENE_BLOCKCHAIN_PRECISION);
      
} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE( htlc_before_hardfork )
{ try {
   ACTORS((alice)(bob));

   int64_t init_balance(100000);

   transfer( committee_account, alice_id, graphene::chain::asset(init_balance) );

   uint16_t preimage_size = 256;
   std::vector<char> pre_image(256);
   generate_random_preimage(preimage_size, pre_image);

   graphene::chain::htlc_id_type alice_htlc_id;
   // clear everything out
   generate_block();
   trx.clear();

   // Alice tries to put a contract on the blockchain
   {
      graphene::chain::htlc_create_operation create_operation;

      create_operation.amount = graphene::chain::asset( 10000 );
      create_operation.to = bob_id;
      create_operation.claim_period_seconds = 60;
      create_operation.preimage_hash = hash_it<fc::sha256>( pre_image );
      create_operation.preimage_size = preimage_size;
      create_operation.from = alice_id;
      trx.operations.push_back(create_operation);
      sign(trx, alice_private_key);
      GRAPHENE_CHECK_THROW(PUSH_TX(db, trx, ~0), fc::exception);
      trx.clear();
   }

   // Propose htlc_create
   {
      proposal_create_operation pco;
      pco.expiration_time = db.head_block_time() + fc::minutes(1);
      pco.fee_paying_account = alice_id;

      graphene::chain::htlc_create_operation create_operation;
      create_operation.amount = graphene::chain::asset( 10000 );
      create_operation.to = bob_id;
      create_operation.claim_period_seconds = 60;
      create_operation.preimage_hash = hash_it<fc::sha256>( pre_image );
      create_operation.preimage_size = preimage_size;
      create_operation.from = alice_id;

      pco.proposed_ops.emplace_back( create_operation );
      trx.operations.push_back( pco );
      GRAPHENE_CHECK_THROW( PUSH_TX( db, trx, ~0 ), fc::assert_exception );
      trx.clear();
   }

   // Propose htlc_redeem
   {
      proposal_create_operation pco;
      pco.expiration_time = db.head_block_time() + fc::minutes(1);
      pco.fee_paying_account = alice_id;

      graphene::chain::htlc_redeem_operation rop;
      rop.redeemer = bob_id;
      rop.htlc_id = alice_htlc_id;
      string preimage_str = "Arglebargle";
      rop.preimage.insert( rop.preimage.begin(), preimage_str.begin(), preimage_str.end() );

      pco.proposed_ops.emplace_back( rop );
      trx.operations.push_back( pco );
      GRAPHENE_CHECK_THROW( PUSH_TX( db, trx, ~0 ), fc::assert_exception );
      trx.clear();
   }

   // Propose htlc_extend
   {
      proposal_create_operation pco;
      pco.expiration_time = db.head_block_time() + fc::minutes(1);
      pco.fee_paying_account = alice_id;

      graphene::chain::htlc_extend_operation xop;
      xop.htlc_id = alice_htlc_id;
      xop.seconds_to_add = 100;
      xop.update_issuer = alice_id;

      pco.proposed_ops.emplace_back( xop );
      trx.operations.push_back( pco );
      GRAPHENE_CHECK_THROW( PUSH_TX( db, trx, ~0 ), fc::assert_exception );
      trx.clear();
   }
} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE( fee_calculations )
{
   // create
   {
      htlc_create_operation::fee_parameters_type create_fee;
      create_fee.fee = 2;
      create_fee.fee_per_day = 2;
      htlc_create_operation create;
      // no days
      create.claim_period_seconds = 0;
      BOOST_CHECK_EQUAL( create.calculate_fee(create_fee).value, 2 );
      // exactly 1 day
      create.claim_period_seconds = 60 * 60 * 24;
      BOOST_CHECK_EQUAL( create.calculate_fee(create_fee).value, 4 );
      // tad over a day
      create.claim_period_seconds++;
      BOOST_CHECK_EQUAL( create.calculate_fee(create_fee).value, 6 );
   }
   // redeem
   {
      htlc_redeem_operation::fee_parameters_type redeem_fee;
      redeem_fee.fee_per_kb = 2;
      redeem_fee.fee = 2;
      htlc_redeem_operation redeem;
      // no preimage
      redeem.preimage = std::vector<char>();
      BOOST_CHECK_EQUAL( redeem.calculate_fee( redeem_fee ).value, 2 ) ;
      // exactly 1KB
      std::string test(1024, 'a');
      redeem.preimage = std::vector<char>( test.begin(), test.end() );
      BOOST_CHECK_EQUAL( redeem.calculate_fee( redeem_fee ).value, 4 ) ;
      // just 1 byte over 1KB
      std::string larger(1025, 'a');
      redeem.preimage = std::vector<char>( larger.begin(), larger.end() );
      BOOST_CHECK_EQUAL( redeem.calculate_fee( redeem_fee ).value, 6 ) ;
   }
   // extend
   {
      htlc_extend_operation::fee_parameters_type extend_fee;
      extend_fee.fee = 2;
      extend_fee.fee_per_day = 2;
      htlc_extend_operation extend;
      // no days
      extend.seconds_to_add = 0;
      BOOST_CHECK_EQUAL( extend.calculate_fee( extend_fee ).value, 2 );
      // exactly 1 day
      extend.seconds_to_add = 60 * 60 * 24;
      BOOST_CHECK_EQUAL( extend.calculate_fee( extend_fee ).value, 4 );
      // 1 day and 1 second
      extend.seconds_to_add++;
      BOOST_CHECK_EQUAL( extend.calculate_fee( extend_fee ).value, 6 );
   }
}

BOOST_AUTO_TEST_SUITE_END()
