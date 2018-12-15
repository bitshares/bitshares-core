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

#include <graphene/app/api.hpp>

#include "../common/database_fixture.hpp"

using namespace graphene::chain;
using namespace graphene::chain::test;

BOOST_FIXTURE_TEST_SUITE( htlc_tests, database_fixture )

void generate_random_preimage(uint16_t key_size, std::vector<uint8_t>& vec)
{
	std::independent_bits_engine<std::default_random_engine, CHAR_BIT, uint8_t> rbe;
	std::generate(begin(vec), end(vec), std::ref(rbe));
	return;
}

/****
 * Hash the preimage and put it in a vector
 * @param preimage the preimage
 * @returns a vector that cointains the sha256 hash of the preimage
 */
std::vector<uint8_t> hash_it(std::vector<uint8_t> preimage)
{
	fc::sha256 hash = fc::sha256::hash((char*)preimage.data(), preimage.size());
   return std::vector<uint8_t>(hash.data(), hash.data() + hash.data_size());
}

void set_committee_parameters(database_fixture* db_fixture)
{
   // set the committee parameters
   db_fixture->db.modify(db_fixture->db.get_global_properties(), [](global_property_object& p) {
      // htlc options
      graphene::chain::htlc_options params;
      params.max_preimage_size = 1024;
      params.max_timeout_secs = 60 * 60 * 24 * 28;
      p.parameters.extensions.value.updatable_htlc_options = params;
      // htlc operation fees
      p.parameters.current_fees->get<htlc_create_operation>().fee = 2 * GRAPHENE_BLOCKCHAIN_PRECISION;
      p.parameters.current_fees->get<htlc_create_operation>().fee_per_day = 2 * GRAPHENE_BLOCKCHAIN_PRECISION;
   });
}

void advance_past_hardfork(database_fixture* db_fixture)
{
   db_fixture->generate_blocks(HARDFORK_CORE_1468_TIME);
   set_committee_parameters(db_fixture);
   set_expiration(db_fixture->db, db_fixture->trx);
}

BOOST_AUTO_TEST_CASE( htlc_before_hardfork )
{
   ACTORS((alice)(bob));

   int64_t init_balance(100000);

   transfer( committee_account, alice_id, graphene::chain::asset(init_balance) );

   uint16_t preimage_size = 256;
   std::vector<unsigned char> pre_image(256);
   generate_random_preimage(preimage_size, pre_image);
   std::vector<unsigned char> preimage_hash = hash_it(pre_image);

   graphene::chain::htlc_id_type alice_htlc_id;
   // cler everything out
   generate_block();
   trx.clear();
   // Alice tries to put a contract on the blockchain
   {
      graphene::chain::htlc_create_operation create_operation;

      create_operation.amount = graphene::chain::asset( 10000 );
      create_operation.destination = bob_id;
      create_operation.claim_period_seconds = 60;
      create_operation.preimage_hash = preimage_hash;
      create_operation.hash_type = graphene::chain::hash_algorithm::sha256;
      create_operation.preimage_size = preimage_size;
      create_operation.source = alice_id;
      trx.operations.push_back(create_operation);
      sign(trx, alice_private_key);
      GRAPHENE_CHECK_THROW(PUSH_TX(db, trx, ~0), fc::exception);
   }
   // attempt to create a proposal that contains htlc stuff
   {

      graphene::chain::committee_member_update_global_parameters_operation param_op;

      graphene::chain::proposal_create_operation create_operation;

      create_operation.fee_paying_account = committee_account;
      create_operation.review_period_seconds = 60 * 60 * 48;
      create_operation.proposed_ops.emplace_back(param_op);
   }
}

BOOST_AUTO_TEST_CASE( htlc_expires )
{
   advance_past_hardfork(this);

   ACTORS((alice)(bob));

   int64_t init_balance(100000);

   transfer( committee_account, alice_id, graphene::chain::asset(init_balance) );

   uint16_t preimage_size = 256;
   std::vector<unsigned char> pre_image(256);
   generate_random_preimage(preimage_size, pre_image);
   std::vector<unsigned char> preimage_hash = hash_it(pre_image);

   graphene::chain::htlc_id_type alice_htlc_id;
   // cler everything out
   generate_block();
   trx.clear();
   // Alice puts a contract on the blockchain
   {
      graphene::chain::htlc_create_operation create_operation;

      create_operation.amount = graphene::chain::asset( 10000 );
      create_operation.destination = bob_id;
      create_operation.claim_period_seconds = 60;
      create_operation.preimage_hash = preimage_hash;
      create_operation.hash_type = graphene::chain::hash_algorithm::sha256;
      create_operation.preimage_size = preimage_size;
      create_operation.source = alice_id;
      trx.operations.push_back(create_operation);
      sign(trx, alice_private_key);
      PUSH_TX(db, trx, ~0);
      trx.clear();
      graphene::chain::signed_block blk = generate_block();
      // can we assume that alice's transaction will be the only one in this block?
      processed_transaction alice_trx = blk.transactions[0];
      alice_htlc_id = alice_trx.operation_results[0].get<object_id_type>();
      generate_block();
   }

   // verify funds on hold (TODO: make sure this can cover fees)
   BOOST_CHECK_EQUAL( get_balance(alice_id, graphene::chain::asset_id_type()), 90000 );

   // make sure Bob (or anyone) can see the details of the transaction
   graphene::app::database_api db_api(db);
   auto obj = db_api.get_objects( {alice_htlc_id }).front();
   graphene::chain::htlc_object htlc = obj.template as<graphene::chain::htlc_object>(GRAPHENE_MAX_NESTED_OBJECTS);

   // let it expire (wait for timeout)
   generate_blocks(fc::time_point_sec(120) );
   // verify funds return (minus the fees)
   BOOST_CHECK_EQUAL( get_balance(alice_id, graphene::chain::asset_id_type()), 90000 );
   // verify Bob cannot execute the contract after the fact
}

BOOST_AUTO_TEST_CASE( htlc_fulfilled )
{
   advance_past_hardfork(this);
	
   ACTORS((alice)(bob));

   int64_t init_balance(1000000);

   transfer( committee_account, alice_id, graphene::chain::asset(init_balance) );
   transfer( committee_account, bob_id, graphene::chain::asset(init_balance) );

   uint16_t preimage_size = 256;
   std::vector<unsigned char> pre_image(preimage_size);
   generate_random_preimage(preimage_size, pre_image);
   std::vector<unsigned char> preimage_hash = hash_it(pre_image);

   graphene::chain::htlc_id_type alice_htlc_id;
   // cler everything out
   generate_block();
   trx.clear();
   enable_fees();
   // Alice puts a contract on the blockchain
   {
      graphene::chain::htlc_create_operation create_operation;

      create_operation.amount = graphene::chain::asset( 100000 );
      create_operation.destination = bob_id;
      create_operation.claim_period_seconds = 86400;
      create_operation.preimage_hash = preimage_hash;
      create_operation.hash_type = graphene::chain::hash_algorithm::sha256;
      create_operation.preimage_size = preimage_size;
      create_operation.source = alice_id;
      create_operation.fee = db.current_fee_schedule().calculate_fee( create_operation );
      trx.operations.push_back(create_operation);
      sign(trx, alice_private_key);
      PUSH_TX(db, trx, ~0);
      trx.clear();
      graphene::chain::signed_block blk = generate_block();
      // can we assume that alice's transaction will be the only one in this block?
      processed_transaction alice_trx = blk.transactions[0];
      alice_htlc_id = alice_trx.operation_results[0].get<object_id_type>();
   }

   // verify funds on hold (make sure this can cover fees)
   BOOST_CHECK_EQUAL( get_balance(alice_id, graphene::chain::asset_id_type()), 700000 );

   // TODO: make sure Bob (or anyone) can see the details of the transaction

   // send an update operation to claim the funds
   {
      graphene::chain::htlc_redeem_operation update_operation;
      update_operation.redeemer = bob_id;
      update_operation.htlc_id = alice_htlc_id;
      update_operation.preimage = pre_image;
      update_operation.fee = db.current_fee_schedule().calculate_fee( update_operation );
      trx.operations.push_back(update_operation);
      sign(trx, bob_private_key);
      try
      {
         PUSH_TX(db, trx, ~0);
      } 
      catch (fc::exception& ex)
      {
         BOOST_FAIL(ex.what());
      }
      generate_block();
      trx.clear();
   }
   // verify funds end up in Bob's account
   BOOST_CHECK_EQUAL( get_balance(bob_id,   graphene::chain::asset_id_type()), 1000000 );
   BOOST_CHECK_EQUAL( get_balance(alice_id, graphene::chain::asset_id_type()), 700000 );
}

BOOST_AUTO_TEST_CASE( other_peoples_money )
{
   advance_past_hardfork(this);
	
   ACTORS((alice)(bob));

   int64_t init_balance(100000);

   transfer( committee_account, alice_id, graphene::chain::asset(init_balance) );

   uint16_t preimage_size = 256;
   std::vector<unsigned char> pre_image(256);
   generate_random_preimage(preimage_size, pre_image);
   std::vector<unsigned char> preimage_hash = hash_it(pre_image);

   graphene::chain::htlc_id_type alice_htlc_id;
   // cler everything out
   generate_block();
   trx.clear();
   // Bob attempts to put a contract on the blockchain using Alice's funds
   {
      graphene::chain::htlc_create_operation create_operation;
      create_operation.amount = graphene::chain::asset( 10000 );
      create_operation.destination = bob_id;
      create_operation.claim_period_seconds = 3;
      create_operation.preimage_hash = preimage_hash;
      create_operation.preimage_size = preimage_size;
      create_operation.source = alice_id;
      trx.operations.push_back(create_operation);
      sign(trx, bob_private_key);
      GRAPHENE_CHECK_THROW(PUSH_TX(db, trx, database::skip_nothing), fc::exception);
      trx.clear();
   }
   // now try the same but with Alice's signature (should work)
   {
      graphene::chain::htlc_create_operation create_operation;
      create_operation.amount = graphene::chain::asset( 10000 );
      create_operation.destination = bob_id;
      create_operation.claim_period_seconds = 3;
      create_operation.preimage_hash = preimage_hash;
      create_operation.hash_type = graphene::chain::hash_algorithm::sha256;
      create_operation.preimage_size = preimage_size;
      create_operation.source = alice_id;
      trx.operations.push_back(create_operation);
      sign(trx, alice_private_key);
      PUSH_TX(db, trx, database::skip_nothing);
      trx.clear();
   }
}

BOOST_AUTO_TEST_CASE( set_htlc_params )
{ 
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

   // now things should start working...
   advance_past_hardfork(this);

   proposal_id_type good_proposal_id;
   BOOST_TEST_MESSAGE( "Creating a proposal to change the max_preimage_size to 2048" );
   {
      proposal_create_operation cop = proposal_create_operation::committee_proposal(db.get_global_properties().parameters, db.head_block_time());
      cop.fee_paying_account = GRAPHENE_TEMP_ACCOUNT;
      cop.expiration_time = db.head_block_time() + *cop.review_period_seconds + 10;
      committee_member_update_global_parameters_operation uop;
      graphene::chain::htlc_options new_params;
      new_params.max_preimage_size = 2048;
      new_params.max_timeout_secs = 60 * 60 * 24 * 28;
      uop.new_parameters.extensions.value.updatable_htlc_options = new_params;
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

   BOOST_CHECK_EQUAL(db.get_global_properties().parameters.extensions.value.updatable_htlc_options->max_preimage_size, 1024u);

   BOOST_TEST_MESSAGE( "Generating blocks until proposal expires" );
   generate_blocks(good_proposal_id(db).expiration_time + 5);
   BOOST_TEST_MESSAGE( "Verify that the parameters still have not changed" );
   BOOST_CHECK_EQUAL(db.get_global_properties().parameters.extensions.value.updatable_htlc_options->max_preimage_size, 1024u);

   BOOST_TEST_MESSAGE( "Generating blocks until next maintenance interval" );
   generate_blocks(db.get_dynamic_global_properties().next_maintenance_time);
   generate_block();   // get the maintenance skip slots out of the way

   BOOST_TEST_MESSAGE( "Verify that the change has been implemented" );
   BOOST_CHECK_EQUAL(db.get_global_properties().parameters.extensions.value.updatable_htlc_options->max_preimage_size, 2048u);
}

BOOST_AUTO_TEST_SUITE_END()
