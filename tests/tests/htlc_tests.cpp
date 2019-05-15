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
	std::independent_bits_engine<std::default_random_engine, CHAR_BIT, unsigned int> rbe;
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
   std::shared_ptr<fee_schedule_type> new_fee_schedule = std::make_shared<fee_schedule_type>();
   new_fee_schedule->scale = GRAPHENE_100_PERCENT;
   // replace the old with the new
   flat_map<uint64_t, graphene::chain::fee_parameters> params_map = get_htlc_fee_parameters();
   for(auto param : existing_fee_schedule.parameters)
   {
      auto itr = params_map.find(param.which());
      if (itr == params_map.end())
         new_fee_schedule->parameters.insert(param);
      else
      {
         new_fee_schedule->parameters.insert( (*itr).second);
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

   // someone else attempts to extend it (bob says he's alice, but he's not)
   {
      graphene::chain::htlc_extend_operation bad_extend;
      bad_extend.htlc_id = alice_htlc_id;
      bad_extend.seconds_to_add = 10;
      bad_extend.fee = db.get_global_properties().parameters.current_fees->calculate_fee(bad_extend);
      bad_extend.update_issuer = alice_id;
      trx.operations.push_back(bad_extend);
      sign(trx, bob_private_key);
      GRAPHENE_CHECK_THROW( PUSH_TX(db, trx, database::skip_nothing ), fc::exception );
      trx.clear();
   }
   // someone else attempts to extend it (bob wants to extend Alice's contract)
   {
      graphene::chain::htlc_extend_operation bad_extend;
      bad_extend.htlc_id = alice_htlc_id;
      bad_extend.seconds_to_add = 10;
      bad_extend.fee = db.get_global_properties().parameters.current_fees->calculate_fee(bad_extend);
      bad_extend.update_issuer = bob_id;
      trx.operations.push_back(bad_extend);
      sign(trx, bob_private_key);
      GRAPHENE_CHECK_THROW( PUSH_TX(db, trx, ~0 ), fc::exception );
      trx.clear();
   }
   // attempt to extend it with too much time
   {
      graphene::chain::htlc_extend_operation big_extend;
      big_extend.htlc_id = alice_htlc_id;
      big_extend.seconds_to_add = db.get_global_properties().parameters.extensions.value.updatable_htlc_options->max_timeout_secs + 10;
      big_extend.fee = db.get_global_properties().parameters.current_fees->calculate_fee(big_extend);
      big_extend.update_issuer = alice_id;
      trx.operations.push_back(big_extend);
      sign(trx, alice_private_key);
      GRAPHENE_CHECK_THROW( PUSH_TX(db, trx, ~0), fc::exception );
      trx.clear();
   }

   // attempt to extend properly
   {
      graphene::chain::htlc_extend_operation extend;
      extend.htlc_id = alice_htlc_id;
      extend.seconds_to_add = 10;
      extend.fee = db.get_global_properties().parameters.current_fees->calculate_fee(extend);
      extend.update_issuer = alice_id;
      trx.operations.push_back(extend);
      sign(trx, alice_private_key);
      PUSH_TX(db, trx, ~0);
      trx.clear();
   }

   // let it expire (wait for timeout)
   generate_blocks( db.head_block_time() + fc::seconds(120) );
   // verify funds return (minus the fees)
   BOOST_CHECK_EQUAL( get_balance(alice_id, graphene::chain::asset_id_type()), 92 * GRAPHENE_BLOCKCHAIN_PRECISION );
   // verify Bob cannot execute the contract after the fact
} FC_LOG_AND_RETHROW()
}

BOOST_AUTO_TEST_CASE( htlc_fulfilled )
{
try {
   ACTORS((alice)(bob)(joker));

   int64_t init_balance(100 * GRAPHENE_BLOCKCHAIN_PRECISION);

   transfer( committee_account, alice_id, graphene::chain::asset(init_balance) );
   transfer( committee_account, bob_id, graphene::chain::asset(init_balance) );
   transfer( committee_account, joker_id, graphene::chain::asset(init_balance) );

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

   // grab number of history objects to make sure everyone gets notified
   size_t alice_num_history = get_operation_history(alice_id).size();
   size_t bob_num_history = get_operation_history(bob_id).size();
   size_t joker_num_history = get_operation_history(joker_id).size();

   // joker sends a redeem operation to claim the funds for bob
   {
      graphene::chain::htlc_redeem_operation update_operation;
      update_operation.redeemer = joker_id;
      update_operation.htlc_id = alice_htlc_id;
      update_operation.preimage = pre_image;
      update_operation.fee = db.current_fee_schedule().calculate_fee( update_operation );
      trx.operations.push_back( update_operation );
      sign(trx, joker_private_key);
      PUSH_TX( db, trx, ~0 );
      generate_block();
      trx.clear();
   }
   // verify funds end up in Bob's account (100 + 20 )
   BOOST_CHECK_EQUAL( get_balance(bob_id,   graphene::chain::asset_id_type()), 120 * GRAPHENE_BLOCKCHAIN_PRECISION );
   // verify funds remain out of Alice's acount ( 100 - 20 - 4 )
   BOOST_CHECK_EQUAL( get_balance(alice_id, graphene::chain::asset_id_type()), 72 * GRAPHENE_BLOCKCHAIN_PRECISION );
   // verify all three get notified
   BOOST_CHECK_EQUAL( get_operation_history(alice_id).size(), alice_num_history + 1);
   BOOST_CHECK_EQUAL( get_operation_history(bob_id).size(), bob_num_history + 1);
   BOOST_CHECK_EQUAL( get_operation_history(joker_id).size(), joker_num_history + 1);
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
         std::shared_ptr<fee_schedule_type> new_fee_schedule = std::make_shared<fee_schedule_type>();
         new_fee_schedule->scale = existing_fee_schedule.scale;
         // replace the old with the new
         flat_map<uint64_t, graphene::chain::fee_parameters> params_map = get_htlc_fee_parameters();
         for(auto param : existing_fee_schedule.parameters)
         {
            auto itr = params_map.find(param.which());
            if (itr == params_map.end())
               new_fee_schedule->parameters.insert(param);
            else
            {
               new_fee_schedule->parameters.insert( (*itr).second);
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
         std::shared_ptr<fee_schedule_type> new_fee_schedule = std::make_shared<fee_schedule_type>();
         new_fee_schedule->scale = existing_fee_schedule.scale;
         // replace the old with the new
         flat_map<uint64_t, graphene::chain::fee_parameters> params_map = get_htlc_fee_parameters();
         for(auto param : existing_fee_schedule.parameters)
         {
            auto itr = params_map.find(param.which());
            if (itr == params_map.end())
               new_fee_schedule->parameters.insert(param);
            else
            {
               new_fee_schedule->parameters.insert( (*itr).second);
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
      const graphene::chain::fee_schedule& current_fee_schedule = *(db.get_global_properties().parameters.current_fees);
      const htlc_create_operation::fee_parameters_type& htlc_fee 
            = current_fee_schedule.get<htlc_create_operation>();
      BOOST_CHECK_EQUAL(htlc_fee.fee, 2 * GRAPHENE_BLOCKCHAIN_PRECISION);
      
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

BOOST_AUTO_TEST_CASE( htlc_blacklist )
{
try {
   ACTORS((nathan)(alice)(bob));

   upgrade_to_lifetime_member( nathan );

   // create a UIA
   const asset_id_type uia_id = create_user_issued_asset( "NATHANCOIN", nathan, white_list ).id;
   // Make a whitelist authority
   {
      BOOST_TEST_MESSAGE( "Changing the whitelist authority" );
      asset_update_operation uop;
      uop.issuer = nathan_id;
      uop.asset_to_update = uia_id;
      uop.new_options = uia_id(db).options;
      uop.new_options.blacklist_authorities.insert(nathan_id);
      trx.operations.push_back(uop);
      PUSH_TX( db, trx, ~0 );
      trx.operations.clear();
   }


   int64_t init_balance(100 * GRAPHENE_BLOCKCHAIN_PRECISION);
   fund( alice, graphene::chain::asset(init_balance) );
   fund( bob, graphene::chain::asset(init_balance) );

   advance_past_hardfork(this);

   // blacklist bob
   {
      graphene::chain::account_whitelist_operation op;
      op.authorizing_account = nathan_id;
      op.account_to_list = bob_id;
      op.new_listing = graphene::chain::account_whitelist_operation::account_listing::black_listed;
      op.fee = db.current_fee_schedule().calculate_fee( op );
      trx.operations.push_back( op );
      sign( trx, nathan_private_key );
      PUSH_TX( db, trx, ~0 );
      trx.clear();
      generate_block();
   }

   issue_uia( alice_id, asset( init_balance, uia_id ) );

   uint16_t preimage_size = 256;
   std::vector<char> pre_image(preimage_size);
   generate_random_preimage(preimage_size, pre_image);

   // Alice attempts to put a contract on the blockchain
   {
      graphene::chain::htlc_create_operation create_operation;

      create_operation.amount = graphene::chain::asset( 20 * GRAPHENE_BLOCKCHAIN_PRECISION, uia_id );
      create_operation.to = bob_id;
      create_operation.claim_period_seconds = 86400;
      create_operation.preimage_hash = hash_it<fc::sha1>( pre_image );
      create_operation.preimage_size = preimage_size;
      create_operation.from = alice_id;
      create_operation.fee = db.current_fee_schedule().calculate_fee( create_operation );
      trx.operations.push_back( create_operation );
      sign(trx, alice_private_key);
      // bob cannot accept it, so it fails
      GRAPHENE_CHECK_THROW( PUSH_TX( db, trx, ~0 ), fc::exception );
      trx.clear();
   }

   // unblacklist Bob
   {
      graphene::chain::account_whitelist_operation op;
      op.authorizing_account = nathan_id;
      op.account_to_list = bob_id;
      op.new_listing = graphene::chain::account_whitelist_operation::account_listing::no_listing;
      op.fee = db.current_fee_schedule().calculate_fee( op );
      trx.operations.push_back( op );
      sign( trx, nathan_private_key );
      PUSH_TX( db, trx, ~0 );
      trx.clear();
      generate_block();
   }

   graphene::chain::htlc_id_type alice_htlc_id;

   // Alice again attempts to put a contract on the blockchain
   {
      graphene::chain::htlc_create_operation create_operation;

      create_operation.amount = graphene::chain::asset( 20 * GRAPHENE_BLOCKCHAIN_PRECISION, uia_id );
      create_operation.to = bob_id;
      create_operation.claim_period_seconds = 86400;
      create_operation.preimage_hash = hash_it<fc::sha1>( pre_image );
      create_operation.preimage_size = preimage_size;
      create_operation.from = alice_id;
      create_operation.fee = db.current_fee_schedule().calculate_fee( create_operation );
      trx.operations.push_back( create_operation );
      sign(trx, alice_private_key);
      // bob can now accept it, so it works
      PUSH_TX( db, trx, ~0 );
      trx.clear();
      graphene::chain::signed_block blk = generate_block();
      processed_transaction alice_trx = blk.transactions[0];
      alice_htlc_id = alice_trx.operation_results[0].get<object_id_type>();
   }

   // blacklist bob
   {
      graphene::chain::account_whitelist_operation op;
      op.authorizing_account = nathan_id;
      op.account_to_list = bob_id;
      op.new_listing = graphene::chain::account_whitelist_operation::account_listing::black_listed;
      op.fee = db.current_fee_schedule().calculate_fee( op );
      trx.operations.push_back( op );
      sign( trx, nathan_private_key );
      PUSH_TX( db, trx, ~0 );
      trx.clear();
      generate_block();
   }

   // bob can redeem even though he's blacklisted
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

} FC_LOG_AND_RETHROW()
}

BOOST_AUTO_TEST_CASE(htlc_database_api) {
try {

   ACTORS((alice)(bob)(carl)(dan));

   int64_t init_balance(100 * GRAPHENE_BLOCKCHAIN_PRECISION);

   transfer( committee_account, alice_id, graphene::chain::asset(init_balance) );

   generate_blocks(HARDFORK_CORE_1468_TIME);
   set_expiration( db, trx );

   set_committee_parameters(this);

   uint16_t preimage_size = 256;
   std::vector<char> pre_image(256);
   std::independent_bits_engine<std::default_random_engine, CHAR_BIT, unsigned char> rbe;
   std::generate(begin(pre_image), end(pre_image), std::ref(rbe));
   graphene::chain::htlc_id_type alice_htlc_id_bob;
   graphene::chain::htlc_id_type alice_htlc_id_carl;
   graphene::chain::htlc_id_type alice_htlc_id_dan;

   generate_block();
   set_expiration( db, trx );
   trx.clear();
   // alice puts a htlc contract to bob
   {
      graphene::chain::htlc_create_operation create_operation;
      BOOST_TEST_MESSAGE("Alice, who has 100 coins, is transferring 3 coins to Bob");
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
      set_expiration( db, trx );
      graphene::chain::signed_block blk = generate_block();
      processed_transaction alice_trx = blk.transactions[0];
      alice_htlc_id_bob = alice_trx.operation_results[0].get<object_id_type>();
      generate_block();
      set_expiration( db, trx );
   }

   trx.clear();
   // alice puts a htlc contract to carl
   {
      graphene::chain::htlc_create_operation create_operation;
      BOOST_TEST_MESSAGE("Alice, who has 100 coins, is transferring 3 coins to Carl");
      create_operation.amount = graphene::chain::asset( 3 * GRAPHENE_BLOCKCHAIN_PRECISION );
      create_operation.to = carl_id;
      create_operation.claim_period_seconds = 60;
      create_operation.preimage_hash = hash_it<fc::sha256>( pre_image );
      create_operation.preimage_size = preimage_size;
      create_operation.from = alice_id;
      create_operation.fee = db.get_global_properties().parameters.current_fees->calculate_fee(create_operation);
      trx.operations.push_back(create_operation);
      sign(trx, alice_private_key);
      PUSH_TX(db, trx, ~0);
      trx.clear();
      set_expiration( db, trx );
      graphene::chain::signed_block blk = generate_block();
      processed_transaction alice_trx = blk.transactions[0];
      alice_htlc_id_carl = alice_trx.operation_results[0].get<object_id_type>();
      generate_block();
      set_expiration( db, trx );
   }

   trx.clear();
   // alice puts a htlc contract to dan
   {
      graphene::chain::htlc_create_operation create_operation;
      BOOST_TEST_MESSAGE("Alice, who has 100 coins, is transferring 3 coins to Dan");
      create_operation.amount = graphene::chain::asset( 3 * GRAPHENE_BLOCKCHAIN_PRECISION );
      create_operation.to = dan_id;
      create_operation.claim_period_seconds = 60;
      create_operation.preimage_hash = hash_it<fc::sha256>( pre_image );
      create_operation.preimage_size = preimage_size;
      create_operation.from = alice_id;
      create_operation.fee = db.get_global_properties().parameters.current_fees->calculate_fee(create_operation);
      trx.operations.push_back(create_operation);
      sign(trx, alice_private_key);
      PUSH_TX(db, trx, ~0);
      trx.clear();
      set_expiration( db, trx );
      graphene::chain::signed_block blk = generate_block();
      processed_transaction alice_trx = blk.transactions[0];
      alice_htlc_id_dan = alice_trx.operation_results[0].get<object_id_type>();
      generate_block();
      set_expiration( db, trx );
   }

   graphene::app::database_api db_api(db, &(this->app.get_options()) ) ;

   auto htlc = db_api.get_htlc(alice_htlc_id_bob);
   BOOST_CHECK_EQUAL( htlc->id.instance(), 0);
   BOOST_CHECK_EQUAL( htlc->transfer.from.instance.value, 16 );
   BOOST_CHECK_EQUAL( htlc->transfer.to.instance.value, 17 );

   htlc = db_api.get_htlc(alice_htlc_id_carl);
   BOOST_CHECK_EQUAL( htlc->id.instance(), 1);
   BOOST_CHECK_EQUAL( htlc->transfer.from.instance.value, 16 );
   BOOST_CHECK_EQUAL( htlc->transfer.to.instance.value, 18 );

   htlc = db_api.get_htlc(alice_htlc_id_dan);
   BOOST_CHECK_EQUAL( htlc->id.instance(), 2);
   BOOST_CHECK_EQUAL( htlc->transfer.from.instance.value, 16 );
   BOOST_CHECK_EQUAL( htlc->transfer.to.instance.value, 19 );

   auto htlcs_alice = db_api.get_htlc_by_from(alice.name, graphene::chain::htlc_id_type(0), 100);
   BOOST_CHECK_EQUAL( htlcs_alice.size(), 3 );
   BOOST_CHECK_EQUAL( htlcs_alice[0].id.instance(), 0 );
   BOOST_CHECK_EQUAL( htlcs_alice[1].id.instance(), 1 );
   BOOST_CHECK_EQUAL( htlcs_alice[2].id.instance(), 2 );

   htlcs_alice = db_api.get_htlc_by_from(alice.name, graphene::chain::htlc_id_type(1), 1);
   BOOST_CHECK_EQUAL( htlcs_alice.size(), 1 );
   BOOST_CHECK_EQUAL( htlcs_alice[0].id.instance(), 1 );

   htlcs_alice = db_api.get_htlc_by_from(alice.name, graphene::chain::htlc_id_type(1), 2);
   BOOST_CHECK_EQUAL( htlcs_alice.size(), 2 );
   BOOST_CHECK_EQUAL( htlcs_alice[0].id.instance(), 1 );
   BOOST_CHECK_EQUAL( htlcs_alice[1].id.instance(), 2 );

   auto htlcs_bob = db_api.get_htlc_by_to(bob.name, graphene::chain::htlc_id_type(0), 100);
   BOOST_CHECK_EQUAL( htlcs_bob.size(), 1 );
   BOOST_CHECK_EQUAL( htlcs_bob[0].id.instance(), 0 );

   auto htlcs_carl = db_api.get_htlc_by_to(carl.name, graphene::chain::htlc_id_type(0), 100);
   BOOST_CHECK_EQUAL( htlcs_carl.size(), 1 );
   BOOST_CHECK_EQUAL( htlcs_carl[0].id.instance(), 1 );

   auto htlcs_dan = db_api.get_htlc_by_to(dan.name, graphene::chain::htlc_id_type(0), 100);
   BOOST_CHECK_EQUAL( htlcs_dan.size(), 1 );
   BOOST_CHECK_EQUAL( htlcs_dan[0].id.instance(), 2 );

   auto full = db_api.get_full_accounts({alice.name}, false);
   BOOST_CHECK_EQUAL( full[alice.name].htlcs.size(), 3 );

   full = db_api.get_full_accounts({bob.name}, false);
   BOOST_CHECK_EQUAL( full[bob.name].htlcs.size(), 1 );

} catch (fc::exception &e) {
      edump((e.to_detail_string()));
      throw;
   }
}



BOOST_AUTO_TEST_SUITE_END()
