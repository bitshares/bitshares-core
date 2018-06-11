/*
 * Copyright (c) 2018 Bitshares Foundation, and contributors.
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

#include <boost/test/unit_test.hpp>

#include <graphene/chain/exceptions.hpp>
#include <graphene/chain/account_object.hpp>
#include <graphene/chain/proposal_object.hpp>

#include <fc/crypto/digest.hpp>

#include "../common/database_fixture.hpp"

using namespace graphene::chain;
using namespace graphene::chain::test;

BOOST_FIXTURE_TEST_SUITE( proposal_tests, database_fixture )

BOOST_AUTO_TEST_CASE( proposal_failure )
{
   try
   {
      // create an account for Bob
      fc::ecc::private_key bob_key = fc::ecc::private_key::regenerate(fc::digest("bobkey"));
      const account_object& bob = create_account( "bob", bob_key.get_public_key() );
      const account_id_type bob_id = bob.get_id();
      fund( bob, asset(1000000) );

      // add an account for Alice
      fc::ecc::private_key alice_key = fc::ecc::private_key::regenerate(fc::digest("alicekey"));
      const account_object& alice = create_account( "alice", alice_key.get_public_key() );
      const account_id_type alice_id = alice.get_id();
      fund(alice, asset(1000000) );

      set_expiration( db, trx );

      // create proposal that will eventually fail due to lack of funds
      transfer_operation top;
      top.to = alice_id;
      top.from = bob_id;
      top.amount = asset(2000000);
      proposal_create_operation pop;
      pop.proposed_ops.push_back( { top } );
      pop.expiration_time = db.head_block_time() + fc::days(1);
      pop.fee_paying_account = bob_id;
      trx.operations.push_back( pop );
      trx.signatures.clear();
      sign( trx, bob_key );
      proposal_object prop;

      try
      {
         processed_transaction processed = PUSH_TX( db, trx );
         prop = db.get<proposal_object>(processed.operation_results.front().get<object_id_type>());
      }
      catch( fc::exception& ex )
      {
         BOOST_FAIL( "Unable to create proposal. Reason: " + ex.to_string(fc::log_level(fc::log_level::all)));
      }

      trx.clear();
      generate_block();

      // make sure proposal is still there
      try {
         db.get<proposal_object>(prop.id);
      }
      catch (fc::exception& ex)
      {
         BOOST_FAIL( "proposal object no longer exists after 1 block" );
      }

      // add signature
      proposal_update_operation up_op;
      up_op.proposal = prop.id;
      up_op.fee_paying_account = bob_id;
      up_op.active_approvals_to_add.emplace( bob_id );
      trx.operations.push_back( up_op );
      sign( trx, bob_key );
      PUSH_TX( db, trx );
      trx.clear();

      // check fail reason
      const proposal_object& result = db.get<proposal_object>(prop.id);
      BOOST_CHECK_EQUAL(result.fail_reason, "Assert Exception: insufficient_balance: Insufficient Balance: "
            "10 BTS, unable to transfer '20 BTS' from account 'bob' to 'alice' Unable to transfer 20 BTS "
            "from bob to alice");
   }
   catch (fc::exception& ex)
   {
      BOOST_FAIL(ex.to_string(fc::log_level(fc::log_level::all)));
   }
}

BOOST_AUTO_TEST_SUITE_END()
