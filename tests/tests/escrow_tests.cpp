/*
 * Copyright (c) 2018 oxarbitrage and contributors.
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

#include <graphene/chain/database.hpp>
#include <graphene/chain/hardfork.hpp>

#include <graphene/chain/balance_object.hpp>
#include <graphene/chain/escrow_object.hpp>
#include <graphene/chain/market_object.hpp>


#include "../common/database_fixture.hpp"

using namespace graphene::chain;
using namespace graphene::chain::test;

BOOST_FIXTURE_TEST_SUITE( escrow_tests, database_fixture )

BOOST_AUTO_TEST_CASE( escrow_transfer )
{
      try
      {
         ACTORS( (alice)(bob)(sam) );

         transfer(committee_account, alice_id, asset(100000000));

         BOOST_REQUIRE_EQUAL(get_balance(alice_id, asset_id_type()), 100000000);
         BOOST_REQUIRE_EQUAL(get_balance(bob_id, asset_id_type()), 0);
         BOOST_REQUIRE_EQUAL(get_balance(sam_id, asset_id_type()), 0);

         const asset_object &core = asset_id_type()(db);

         BOOST_TEST_MESSAGE( "Testing: escrow_transfer creating object" );
         {
            escrow_transfer_operation op;
            op.from = alice_id;
            op.to = bob_id;
            op.amount = core.amount(1000);
            op.escrow_id = 0;
            op.agent = sam_id;
            op.json_meta = "";
            op.ratification_deadline = db.head_block_time() + 100;
            op.escrow_expiration = db.head_block_time() + 200;
            trx.operations.push_back(op);
            sign(trx, alice_private_key);
            PUSH_TX(db, trx);
            generate_block();
            trx.clear();
         }

         escrow_object escrow = db.get_escrow( alice_id, 0 );

         BOOST_REQUIRE( escrow.from == alice_id );
         BOOST_REQUIRE( escrow.to == bob_id );
         BOOST_REQUIRE( escrow.escrow_id == 0 );
         BOOST_REQUIRE( escrow.agent == sam_id );
         BOOST_REQUIRE( escrow.disputed == false );
         BOOST_REQUIRE( escrow.to_approved == false );
         BOOST_REQUIRE( escrow.agent_approved == false );

         BOOST_REQUIRE_EQUAL(get_balance(alice_id, asset_id_type()), 99999000);
         BOOST_REQUIRE_EQUAL(get_balance(bob_id, asset_id_type()), 0);
         BOOST_REQUIRE_EQUAL(get_balance(sam_id, asset_id_type()), 0);

         // agent approves
         BOOST_TEST_MESSAGE( "Testing: escrow release" );
         {
            escrow_approve_operation op;
            op.from = alice_id;
            op.to = bob_id;
            op.who = sam_id;
            op.escrow_id = 0;
            op.agent = sam_id;
            op.approve = true;
            trx.operations.push_back(op);
            sign(trx, sam_private_key);
            PUSH_TX(db, trx);
            generate_block();
            trx.clear();
         }

         BOOST_REQUIRE_EQUAL(get_balance(alice_id, asset_id_type()), 99999000);
         BOOST_REQUIRE_EQUAL(get_balance(bob_id, asset_id_type()), 0);
         BOOST_REQUIRE_EQUAL(get_balance(sam_id, asset_id_type()), 0);

         escrow = db.get_escrow( alice_id, 0 );
         // escrow object still there, flags are changing
         BOOST_REQUIRE( escrow.from == alice_id );
         BOOST_REQUIRE( escrow.to == bob_id );
         BOOST_REQUIRE( escrow.escrow_id == 0 );
         BOOST_REQUIRE( escrow.agent == sam_id );
         BOOST_REQUIRE( escrow.disputed == false );
         BOOST_REQUIRE( escrow.to_approved == false );
         BOOST_REQUIRE( escrow.agent_approved == true );

         // bob(to) approves.
         {
            escrow_approve_operation op;
            op.from = alice_id;
            op.to = bob_id;
            op.who = bob_id;
            op.escrow_id = 0;
            op.agent = sam_id;
            op.approve = true;
            trx.operations.push_back(op);
            sign(trx, bob_private_key);
            //sign(trx, alice_private_key);
            PUSH_TX(db, trx);
            generate_block();
            trx.clear();
         }
         BOOST_REQUIRE_EQUAL(get_balance(alice_id, asset_id_type()), 99999000);
         BOOST_REQUIRE_EQUAL(get_balance(bob_id, asset_id_type()), 0);
         BOOST_REQUIRE_EQUAL(get_balance(sam_id, asset_id_type()), 0);

         escrow = db.get_escrow( alice_id, 0 );
         BOOST_REQUIRE( escrow.from == alice_id );
         BOOST_REQUIRE( escrow.to == bob_id );
         BOOST_REQUIRE( escrow.escrow_id == 0 );
         BOOST_REQUIRE( escrow.agent == sam_id );
         BOOST_REQUIRE( escrow.disputed == false );
         BOOST_REQUIRE( escrow.to_approved == true );
         BOOST_REQUIRE( escrow.agent_approved == true );

         // now the escrow haves all the needed aprovals release the funds with alice(bob cant release to himself)
         {
            escrow_release_operation op;

            op.from = alice_id;
            op.to = bob_id;
            op.who = alice_id;
            op.escrow_id = 0;
            op.amount = core.amount(1000);
            trx.operations.push_back(op);
            sign(trx, alice_private_key);
            PUSH_TX(db, trx);
            generate_block();
            trx.clear();
         }
         BOOST_REQUIRE_EQUAL(get_balance(alice_id, asset_id_type()), 99999000);
         BOOST_REQUIRE_EQUAL(get_balance(bob_id, asset_id_type()), 1000);
         BOOST_REQUIRE_EQUAL(get_balance(sam_id, asset_id_type()), 0);

         ///escrow object is deleted
         GRAPHENE_CHECK_THROW(db.get_escrow( alice_id, 0 ), fc::assert_exception);

         // perfecto this is the first scenario where no dispute and everybody approves everything.

      } FC_LOG_AND_RETHROW()
   }


BOOST_AUTO_TEST_CASE( escrow_dispute )
{
   try
   {
      ACTORS( (alice)(bob)(sam) );

      transfer(committee_account, alice_id, asset(100000000));

      BOOST_REQUIRE_EQUAL(get_balance(alice_id, asset_id_type()), 100000000);
      BOOST_REQUIRE_EQUAL(get_balance(bob_id, asset_id_type()), 0);
      BOOST_REQUIRE_EQUAL(get_balance(sam_id, asset_id_type()), 0);

      const asset_object &core = asset_id_type()(db);

      BOOST_TEST_MESSAGE( "escrow is created" );
      {
         escrow_transfer_operation op;
         op.from = alice_id;
         op.to = bob_id;
         op.amount = core.amount(1000);
         op.escrow_id = 0;
         op.agent = sam_id;
         //op.fee = asset( 100, asset_id_type() );
         op.json_meta = "";
         op.ratification_deadline = db.head_block_time() + 100;
         op.escrow_expiration = db.head_block_time() + 200;
         trx.operations.push_back(op);
         sign(trx, alice_private_key);
         PUSH_TX(db, trx);
         generate_block();
         trx.clear();
      }

      escrow_object escrow = db.get_escrow( alice_id, 0 );

      BOOST_REQUIRE( escrow.from == alice_id );
      BOOST_REQUIRE( escrow.to == bob_id );
      BOOST_REQUIRE( escrow.escrow_id == 0 );
      BOOST_REQUIRE( escrow.agent == sam_id );
      BOOST_REQUIRE( escrow.disputed == false );
      BOOST_REQUIRE( escrow.to_approved == false );
      BOOST_REQUIRE( escrow.agent_approved == false );

      BOOST_REQUIRE_EQUAL(get_balance(alice_id, asset_id_type()), 99999000);
      BOOST_REQUIRE_EQUAL(get_balance(bob_id, asset_id_type()), 0);
      BOOST_REQUIRE_EQUAL(get_balance(sam_id, asset_id_type()), 0);

      BOOST_TEST_MESSAGE( "to never send payment to from so from opens a dispute to get money back" );
      {
         escrow_dispute_operation op;
         op.from = alice_id;
         op.to = bob_id;
         op.escrow_id = 0;
         op.who = alice_id;
         trx.operations.push_back(op);
         sign(trx, alice_private_key);
         PUSH_TX(db, trx);
         generate_block();
         trx.clear();
      }

      // check how is the object now(dispute flag should be on and such)
      escrow = db.get_escrow( alice_id, 0 );
      //wdump((escrow));
      BOOST_REQUIRE( escrow.from == alice_id );
      BOOST_REQUIRE( escrow.to == bob_id );
      BOOST_REQUIRE( escrow.escrow_id == 0 );
      BOOST_REQUIRE( escrow.agent == sam_id );
      BOOST_REQUIRE( escrow.disputed == true );
      BOOST_REQUIRE( escrow.to_approved == false );
      BOOST_REQUIRE( escrow.agent_approved == false );

      //wdump((escrow.escrow_expiration));

      // now the agent is in control and can send money back to alice

      // now release back to alice with alice
      {
         escrow_release_operation op;

         op.from = alice_id;
         op.to = alice_id;
         op.who = sam_id;
         op.escrow_id = 0;
         op.amount = core.amount(1000);
         trx.operations.push_back(op);
         sign(trx, sam_private_key);
         PUSH_TX(db, trx);
         generate_block();
         trx.clear();
      }
      BOOST_REQUIRE_EQUAL(get_balance(alice_id, asset_id_type()), 100000000);
      BOOST_REQUIRE_EQUAL(get_balance(bob_id, asset_id_type()), 0);
      BOOST_REQUIRE_EQUAL(get_balance(sam_id, asset_id_type()), 0);

   } FC_LOG_AND_RETHROW()

}

BOOST_AUTO_TEST_CASE( escrow_validations )
{
   try {

      ACTORS( (alice)(bob)(sam)(paul) );

      //enable_fees();
      transfer(committee_account, alice_id, asset(100000000));

      BOOST_REQUIRE_EQUAL(get_balance(alice_id, asset_id_type()), 100000000);
      BOOST_REQUIRE_EQUAL(get_balance(bob_id, asset_id_type()), 0);
      BOOST_REQUIRE_EQUAL(get_balance(sam_id, asset_id_type()), 0);

      const asset_object &core = asset_id_type()(db);
      const auto& bitusd = create_bitasset("USDBIT", paul_id);

      BOOST_TEST_MESSAGE( "create escrow with non core will fail" );

      escrow_transfer_operation op;
      op.from = alice_id;
      op.to = bob_id;
      op.amount = bitusd.amount(1000);
      op.escrow_id = 0;
      op.agent = sam_id;
      op.json_meta = "";
      op.ratification_deadline = db.head_block_time() + 100;
      op.escrow_expiration = db.head_block_time() + 200;
      trx.operations.push_back(op);
      sign(trx, alice_private_key);
      PUSH_TX(db, trx);
      generate_block();
      trx.clear();

   } FC_LOG_AND_RETHROW()
}
/*
BOOST_AUTO_TEST_CASE( escrow_mpa )
{
   //try {

      ACTORS((alice)(bob)(sam)(paul));

      const auto& bitusd = create_bitasset("USDBIT", paul_id);
      const auto& core   = asset_id_type()(db);


      // fund paul and alice
      transfer(committee_account, paul_id, asset(10000000));
      transfer(committee_account, alice_id, asset(10000000));

      // add a feed to asset
      update_feed_producers( bitusd, {paul.id} );
      price_feed current_feed;
      current_feed.maintenance_collateral_ratio = 1750;
      current_feed.maximum_short_squeeze_ratio = 1100;
      current_feed.settlement_price = bitusd.amount( 100 ) / core.amount(5);
      publish_feed( bitusd, paul, current_feed );

      // paul get some bitusd
      auto call_paul = *borrow( paul, bitusd.amount(1000), asset(100));
      call_order_id_type call_paul_id = call_paul.id;
      BOOST_REQUIRE_EQUAL( get_balance( paul, bitusd ), 1000 );

      // and transfer some to rachel
      transfer(paul_id, alice_id, bitusd.amount(300));
      //transfer(feedproducer_id, alice_id, bitusd.amount(200));

      BOOST_REQUIRE_EQUAL( get_balance( alice, bitusd ), 300 );
      BOOST_REQUIRE_EQUAL( get_balance( alice, core ), 10000000 );

      //BOOST_REQUIRE_EQUAL(get_balance(bob_id, asset_id_type()), 0);
      //BOOST_REQUIRE_EQUAL(get_balance(sam_id, asset_id_type()), 0);

   wdump((get_account("alice")));
   const auto& balance_index = db.get_index_type<account_balance_index>().indices();
   for( const account_balance_object& b : balance_index )
      wdump((b));

   BOOST_TEST_MESSAGE( "escrow is created" );
      try {
         escrow_transfer_operation op;
         op.from = alice_id;
         op.to = bob_id;
         op.amount = bitusd.amount(200);
         op.escrow_id = 0;
         op.agent = sam_id;
         //op.agent_fee
         //op.fee = bitusd.amount(100);
         op.json_meta = "";
         //op.ratification_deadline = db.head_block_time() + 100;
         //op.escrow_expiration = db.head_block_time() + 200;
         trx.operations.push_back(op);
         sign(trx, alice_private_key);
         PUSH_TX(db, trx);
         generate_block();
         trx.clear();
      }
      catch( const fc::exception& e )
      {
         wdump((e.to_detail_string()));
      }


      escrow_object escrow = db.get_escrow( alice_id, 0 );
      wdump((escrow));
      BOOST_REQUIRE( escrow.from == alice_id );
      BOOST_REQUIRE( escrow.to == bob_id );
      BOOST_REQUIRE( escrow.escrow_id == 0 );
      BOOST_REQUIRE( escrow.agent == sam_id );
      BOOST_REQUIRE( escrow.disputed == false );
      BOOST_REQUIRE( escrow.to_approved == false );
      BOOST_REQUIRE( escrow.agent_approved == false );



   //} FC_LOG_AND_RETHROW()
}
*/

   BOOST_AUTO_TEST_CASE( escrow_authorities )
   {
      try
      {
         ACTORS( (alice)(bob)(sam) );


// trying approve now
         escrow_approve_operation op2;

         op2.from = alice_id;
         op2.to = bob_id;
         op2.agent = sam_id;
         op2.who = bob_id;
         op2.escrow_id = 0;
         op2.approve = true;


//generate_block();

         flat_set< account_id_type > auths2;
         flat_set< account_id_type > expected2;

         op2.get_required_active_authorities( auths2 );
         wdump((auths2));




         vector<authority> other;
         flat_set<account_id_type> active_set, owner_set;
         operation_get_required_authorities(op2,active_set,owner_set,other);


         wdump((active_set));
         wdump((owner_set));
         wdump((other));

// executing the op

// executing the operation
         set_expiration( db, trx );
         trx.clear();
         trx.operations.push_back(op2);

//auto test = tx_missing_active_auth;
//wdump((test));

         trx.signatures.clear();
         sign(trx, bob_private_key);
         wdump((trx));
         PUSH_TX(db, trx, 0);
         generate_block();
         set_expiration( db, trx );



      } FC_LOG_AND_RETHROW()

   }
BOOST_AUTO_TEST_SUITE_END()
