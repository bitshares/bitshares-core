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
#include <graphene/chain/htlc_object.hpp>
#include <graphene/chain/market_object.hpp>

#include <graphene/app/api.hpp>

#include "../common/database_fixture.hpp"

using namespace graphene::chain;
using namespace graphene::chain::test;

BOOST_FIXTURE_TEST_SUITE( htlc_tests, database_fixture )

void generate_random_preimage(uint16_t key_size, std::vector<unsigned char>& vec)
{
	std::independent_bits_engine<std::default_random_engine, CHAR_BIT, unsigned char> rbe;
	std::generate(begin(vec), end(vec), std::ref(rbe));
	return;
}

std::vector<unsigned char> hash_it(std::vector<unsigned char> preimage)
{
	fc::sha256 hash = fc::sha256::hash(preimage);
	std::vector<unsigned char> ret_val(hash.data_size());
	char* data = hash.data();
	for(size_t i = 0; i < hash.data_size(); i++)
	{
		ret_val[i] = data[i];
	}
	return ret_val;
}

BOOST_AUTO_TEST_CASE( htlc_expires )
{
   ACTORS((alice)(bob));

   int64_t init_balance(100000);

   transfer( committee_account, alice_id, graphene::chain::asset(init_balance) );

   uint16_t key_size = 256;
   std::vector<unsigned char> pre_image(256);
   generate_random_preimage(key_size, pre_image);
   std::vector<unsigned char> key_hash = hash_it(pre_image);

   graphene::chain::htlc_id_type alice_htlc_id;
   // cler everything out
   generate_block();
   trx.clear();
   // Alice puts a contract on the blockchain
   {
      graphene::chain::htlc_create_operation create_operation;

      create_operation.amount = graphene::chain::asset( 10000 );
      create_operation.destination = bob_id;
      create_operation.epoch = fc::time_point::now() + fc::seconds(3);
      create_operation.key_hash = key_hash;
      create_operation.key_size = key_size;
      create_operation.source = alice_id;
      trx.operations.push_back(create_operation);
      sign(trx, alice_private_key);
      try
      {
    	  PUSH_TX(db, trx, ~0);
      } catch (fc::exception& ex)
      {
    	  BOOST_FAIL( ex.to_detail_string(fc::log_level(fc::log_level::all)) );
      }
      trx.clear();
      graphene::chain::signed_block blk = generate_block();
      // can we assume that alice's transaction will be the only one in this block?
      processed_transaction alice_trx = blk.transactions[0];
      alice_htlc_id = alice_trx.operation_results[0].get<object_id_type>();
   }

   // verify funds on hold (make sure this can cover fees)
   BOOST_CHECK_EQUAL( get_balance(alice_id, graphene::chain::asset_id_type()), 90000 );

   // make sure Alice can't get it back before the timeout
   {
      graphene::chain::htlc_update_operation update_operation;
      update_operation.update_issuer = alice_id;
      update_operation.htlc_id = alice_htlc_id;
      trx.operations.push_back(update_operation);
      sign(trx, alice_private_key);
      try
      {
          PUSH_TX(db, trx, ~0);
          BOOST_FAIL("Should not allow Alice to reclaim funds before timeout");
      } catch (fc::exception& ex)
      {
    	  // this should happen
      }
      generate_block();
      trx.clear();
   }

   // make sure Alice can't spend it.
   // make sure Bob (or anyone) can see the details of the transaction
   // let it expire (wait for timeout)
   std::this_thread::sleep_for(std::chrono::seconds(4));
   // send an update operation to reclaim the funds
   {
      graphene::chain::htlc_update_operation update_operation;
      update_operation.update_issuer = alice_id;
      update_operation.htlc_id = alice_htlc_id;
      trx.operations.push_back(update_operation);
      sign(trx, alice_private_key);
      try
      {
          PUSH_TX(db, trx, ~0);
      } catch (fc::exception& ex)
      {
          BOOST_FAIL(ex.to_detail_string(fc::log_level(fc::log_level::all)));
      }
      generate_block();
      trx.clear();
   }
   // verify funds return (what about fees?)
   BOOST_CHECK_EQUAL( get_balance(alice_id, graphene::chain::asset_id_type()), 100000 );
   // verify Bob cannot execute the contract after the fact
}

BOOST_AUTO_TEST_CASE( htlc_fulfilled )
{
	   ACTORS((alice)(bob));

	   int64_t init_balance(100000);

	   transfer( committee_account, alice_id, graphene::chain::asset(init_balance) );

	   uint16_t key_size = 256;
	   std::vector<unsigned char> pre_image(256);
	   generate_random_preimage(key_size, pre_image);
	   std::vector<unsigned char> key_hash = hash_it(pre_image);

	   graphene::chain::htlc_id_type alice_htlc_id;
	   // cler everything out
	   generate_block();
	   trx.clear();
	   // Alice puts a contract on the blockchain
	   {
	      graphene::chain::htlc_create_operation create_operation;

	      create_operation.amount = graphene::chain::asset( 10000 );
	      create_operation.destination = bob_id;
	      create_operation.epoch = fc::time_point::now() + fc::seconds(3);
	      create_operation.key_hash = key_hash;
	      create_operation.key_size = key_size;
	      create_operation.source = alice_id;
	      trx.operations.push_back(create_operation);
	      sign(trx, alice_private_key);
	      try
	      {
	    	  PUSH_TX(db, trx, ~0);
	      } catch (fc::exception& ex)
	      {
	    	  BOOST_FAIL( ex.to_detail_string(fc::log_level(fc::log_level::all)) );
	      }
	      trx.clear();
	      graphene::chain::signed_block blk = generate_block();
	      // can we assume that alice's transaction will be the only one in this block?
	      processed_transaction alice_trx = blk.transactions[0];
	      alice_htlc_id = alice_trx.operation_results[0].get<object_id_type>();
	   }

	   // verify funds on hold (make sure this can cover fees)
	   BOOST_CHECK_EQUAL( get_balance(alice_id, graphene::chain::asset_id_type()), 90000 );

	   // make sure Alice can't get it back before the timeout
	   {
	      graphene::chain::htlc_update_operation update_operation;
	      update_operation.update_issuer = alice_id;
	      update_operation.htlc_id = alice_htlc_id;
	      trx.operations.push_back(update_operation);
	      sign(trx, alice_private_key);
	      try
	      {
	          PUSH_TX(db, trx, ~0);
	          BOOST_FAIL("Should not allow Alice to reclaim funds before timeout");
	      } catch (fc::exception& ex)
	      {
	    	  // this should happen
	      }
	      generate_block();
	      trx.clear();
	   }

	   // balance should not have changed
	   BOOST_CHECK_EQUAL( get_balance(alice_id, graphene::chain::asset_id_type()), 90000 );
	   // make sure Bob (or anyone) can see the details of the transaction
	   // send an update operation to claim the funds
	   {
	      graphene::chain::htlc_update_operation update_operation;
	      update_operation.update_issuer = bob_id;
	      update_operation.htlc_id = alice_htlc_id;
	      update_operation.preimage = pre_image;
	      trx.operations.push_back(update_operation);
	      sign(trx, bob_private_key);
	      try
	      {
	          PUSH_TX(db, trx, ~0);
	      } catch (fc::exception& ex)
	      {
	          BOOST_FAIL(ex.to_detail_string(fc::log_level(fc::log_level::all)));
	      }
	      generate_block();
	      trx.clear();
	   }
	   // verify Alice cannot execute the contract after the fact
	   {
	      graphene::chain::htlc_update_operation update_operation;
	      update_operation.update_issuer = alice_id;
	      update_operation.htlc_id = alice_htlc_id;
	      trx.operations.push_back(update_operation);
	      sign(trx, alice_private_key);
	      try
	      {
	          PUSH_TX(db, trx, ~0);
	          BOOST_FAIL("Should not allow Alice to reclaim funds after Bob already claimed them.");
	      } catch (fc::exception& ex)
	      {
	    	  // this should happen
	      }
	      generate_block();
	      trx.clear();
	   }
	   // verify funds end up in Bob's account
	   BOOST_CHECK_EQUAL( get_balance(bob_id,   graphene::chain::asset_id_type()), 10000 );
	   BOOST_CHECK_EQUAL( get_balance(alice_id, graphene::chain::asset_id_type()), 90000 );
}

BOOST_AUTO_TEST_CASE( other_peoples_money )
{
   ACTORS((alice)(bob));

   int64_t init_balance(100000);

   transfer( committee_account, alice_id, graphene::chain::asset(init_balance) );

   uint16_t key_size = 256;
   std::vector<unsigned char> pre_image(256);
   generate_random_preimage(key_size, pre_image);
   std::vector<unsigned char> key_hash = hash_it(pre_image);

   graphene::chain::htlc_id_type alice_htlc_id;
   // cler everything out
   generate_block();
   trx.clear();
   // Bob attempts to put a contract on the blockchain using Alice's funds
   {
      graphene::chain::htlc_create_operation create_operation;
      create_operation.amount = graphene::chain::asset( 10000 );
      create_operation.destination = bob_id;
      create_operation.epoch = fc::time_point::now() + fc::seconds(3);
      create_operation.key_hash = key_hash;
      create_operation.key_size = key_size;
      create_operation.source = alice_id;
      trx.operations.push_back(create_operation);
      sign(trx, bob_private_key);
      try
      {
    	  PUSH_TX(db, trx, database::skip_nothing);
    	  BOOST_FAIL( "Bob stole money from Alice!" );
      } catch (fc::exception& ex)
      {
         // this is supposed to happen
         //BOOST_TEST_MESSAGE("This is the error thrown (expected):");
         //BOOST_TEST_MESSAGE(ex.to_detail_string(fc::log_level(fc::log_level::all)));
      }
      trx.clear();
   }
   // now try the same but with Alice's signature (should work)
   {
      graphene::chain::htlc_create_operation create_operation;
      create_operation.amount = graphene::chain::asset( 10000 );
      create_operation.destination = bob_id;
      create_operation.epoch = fc::time_point::now() + fc::seconds(3);
      create_operation.key_hash = key_hash;
      create_operation.key_size = key_size;
      create_operation.source = alice_id;
      trx.operations.push_back(create_operation);
      sign(trx, alice_private_key);
      try
      {
         PUSH_TX(db, trx, database::skip_nothing);
      } catch (fc::exception& ex)
      {
    	   BOOST_FAIL( "Alice cannot create a contract!" );
      }
      trx.clear();
   }
}

BOOST_AUTO_TEST_SUITE_END()
