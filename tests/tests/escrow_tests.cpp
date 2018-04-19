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

         // creating the escrow transfer
         {
            escrow_transfer_operation op;
            op.from = alice_id;
            op.to = bob_id;
            op.amount = core.amount(1000);
            op.escrow_id = 0;
            op.agent = sam_id;
            op.agent_fee = core.amount(0);
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
            op.receiver = bob_id;
            op.agent = sam_id;
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

      // escrow is created
      {
         escrow_transfer_operation op;
         op.from = alice_id;
         op.to = bob_id;
         op.amount = core.amount(1000);
         op.escrow_id = 0;
         op.agent = sam_id;
         op.agent_fee = core.amount(0);
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


      // escrow need to be approved by agent and to before a dispute can be raised.
      // aporving
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
      // agent aproves
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


      // "to" never send payment offchain to from so from opens a dispute to get money back
      {
         escrow_dispute_operation op;
         op.from = alice_id;
         op.to = bob_id;
         op.escrow_id = 0;
         op.who = alice_id;
         op.agent = sam_id;
         trx.operations.push_back(op);
         sign(trx, alice_private_key);
         PUSH_TX(db, trx);
         generate_block();
         trx.clear();
      }

      // check how is the object now(dispute flag should be on)
      escrow = db.get_escrow( alice_id, 0 );
      BOOST_REQUIRE( escrow.from == alice_id );
      BOOST_REQUIRE( escrow.to == bob_id );
      BOOST_REQUIRE( escrow.escrow_id == 0 );
      BOOST_REQUIRE( escrow.agent == sam_id );
      BOOST_REQUIRE( escrow.disputed == true );
      BOOST_REQUIRE( escrow.to_approved == true );
      BOOST_REQUIRE( escrow.agent_approved == true );

      // now the agent is in control of he funds, send money back to alice
      {
         escrow_release_operation op;

         op.from = alice_id;
         op.to = bob_id;
         op.who = sam_id;
         op.receiver = alice_id;
         op.agent = sam_id;
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
      asset_id_type core_id = core.id;
      const auto& bitusd = create_bitasset("USDBIT", paul_id);
      asset_id_type bitusd_id = bitusd.id;


      //create escrow with non core will fail
      escrow_transfer_operation op;
      op.from = alice_id;
      op.to = bob_id;
      op.amount = bitusd.amount(1000);
      op.escrow_id = 0;
      op.agent = sam_id;
      op.agent_fee = bitusd.amount(0);
      op.json_meta = "";
      op.ratification_deadline = db.head_block_time() + 100;
      op.escrow_expiration = db.head_block_time() + 200;
      trx.operations.push_back(op);
      sign(trx, alice_private_key);
      //PUSH_TX(db, trx);
      GRAPHENE_REQUIRE_THROW(PUSH_TX( db, trx), fc::assert_exception); // amount.asset_id == asset_id_type():
      generate_block();
      trx.clear();


      // agent fee need to be of the same type as amount
      op.amount = core_id(db).amount(1000);
      op.agent_fee = bitusd_id(db).amount(0);
      trx.operations.push_back(op);
      sign(trx, alice_private_key);
      GRAPHENE_REQUIRE_THROW(PUSH_TX( db, trx), fc::assert_exception); // agent_fee.asset_id == amount.asset_id
      generate_block();
      trx.clear();

      // from and to are the same
      op.to = alice_id;
      op.amount = core_id(db).amount(1000);
      op.agent_fee = core_id(db).amount(0);
      trx.operations.push_back(op);
      sign(trx, alice_private_key);
      GRAPHENE_REQUIRE_THROW(PUSH_TX( db, trx), fc::assert_exception); // from != to
      generate_block();
      trx.clear();

      // agent cant be from
      op.to = bob_id;
      op.agent = alice_id;
      trx.operations.push_back(op);
      sign(trx, alice_private_key);
      GRAPHENE_REQUIRE_THROW(PUSH_TX( db, trx), fc::assert_exception); // from != agent && to != agent
      generate_block();
      trx.clear();

      // agent cant be to
      op.agent = bob_id;
      trx.operations.push_back(op);
      sign(trx, alice_private_key);
      GRAPHENE_REQUIRE_THROW(PUSH_TX( db, trx), fc::assert_exception); // from != agent && to != agent
      generate_block();
      trx.clear();

      // aratification deadline in the past not allowed
      op.agent = sam_id;
      op.ratification_deadline = db.head_block_time() - 1;
      op.escrow_expiration = db.head_block_time() + 200;
      trx.operations.push_back(op);
      sign(trx, alice_private_key);
      //PUSH_TX( db, trx);
      GRAPHENE_REQUIRE_THROW(PUSH_TX( db, trx), fc::assert_exception); // o.ratification_deadline > db().head_block_time():
      generate_block();
      trx.clear();

      // aratification deadline in the past not allowed
      op.ratification_deadline = db.head_block_time() + 1;
      op.escrow_expiration = db.head_block_time() - 1;
      trx.operations.push_back(op);
      sign(trx, alice_private_key);
      //PUSH_TX( db, trx);
      GRAPHENE_REQUIRE_THROW(PUSH_TX( db, trx), fc::assert_exception); // o.escrow_expiration > db().head_block_time():
      generate_block();
      trx.clear();

      // give paul some bts
      transfer(committee_account, alice_id, asset(100));

      // buyt not enough to make requested escrow
      op.ratification_deadline = db.head_block_time() + 100;
      op.escrow_expiration = db.head_block_time() + 100;
      op.from = paul_id;
      op.amount = core_id(db).amount(1000);
      trx.operations.push_back(op);
      sign(trx, paul_private_key);
      //PUSH_TX( db, trx);
      GRAPHENE_REQUIRE_THROW(PUSH_TX( db, trx), fc::assert_exception); // db().get_balance( o.from, o.amount.asset_id ) >= (o.amount + o.fee + o.agent_fee):
      generate_block();
      trx.clear();

      // passing an escrow create op so we can start testing validation in approve
      // escrow is created
      {
         escrow_transfer_operation op;
         op.from = alice_id;
         op.to = bob_id;
         op.amount = core.amount(1000);
         op.escrow_id = 0;
         op.agent = sam_id;
         op.agent_fee = core.amount(0);
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

      // who need to be to or agent, alice cant approve
      escrow_approve_operation op_approve;
      op_approve.from = alice_id;
      op_approve.to = bob_id;
      op_approve.who = alice_id;
      op_approve.escrow_id = 0;
      op_approve.agent = sam_id;
      //op_approve.approve = true;
      trx.operations.push_back(op_approve);
      sign(trx, alice_private_key);
      //sign(trx, alice_private_key);
      //PUSH_TX(db, trx);
      GRAPHENE_REQUIRE_THROW(PUSH_TX( db, trx), fc::assert_exception); // who == to || who == agent:
      generate_block();
      trx.clear();

      // other account not involved in the escrow is not valid either
      op_approve.who = paul_id;
      trx.operations.push_back(op_approve);
      sign(trx, paul_private_key);
      //sign(trx, alice_private_key);
      //PUSH_TX(db, trx);
      GRAPHENE_REQUIRE_THROW(PUSH_TX( db, trx), fc::assert_exception); // who == to || who == agent:
      generate_block();
      trx.clear();

      // any of the involved parties can remove the escrow by using approve = false
      // bob(to) deletes escrow
      op_approve.who = bob_id;
      op_approve.approve = false;
      trx.operations.push_back(op_approve);
      sign(trx, bob_private_key);
      PUSH_TX(db, trx);
      generate_block();
      trx.clear();

      GRAPHENE_REQUIRE_THROW(db.get_escrow( alice_id, 0 ), fc::assert_exception); // escrow not found

      // cretae the escrow again
      {
         escrow_transfer_operation op;
         op.from = alice_id;
         op.to = bob_id;
         op.amount = core.amount(1000);
         op.escrow_id = 1;
         op.agent = sam_id;
         op.agent_fee = core.amount(0);
         op.json_meta = "";
         op.ratification_deadline = db.head_block_time() + 100;
         op.escrow_expiration = db.head_block_time() + 200;
         trx.operations.push_back(op);
         sign(trx, alice_private_key);
         PUSH_TX(db, trx);
         generate_block();
         trx.clear();
      }

      escrow = db.get_escrow( alice_id, 1 );
      BOOST_REQUIRE( escrow.from == alice_id );
      BOOST_REQUIRE( escrow.to == bob_id );
      BOOST_REQUIRE( escrow.escrow_id == 1 );
      BOOST_REQUIRE( escrow.agent == sam_id );
      BOOST_REQUIRE( escrow.disputed == false );
      BOOST_REQUIRE( escrow.to_approved == false );
      BOOST_REQUIRE( escrow.agent_approved == false );


      // bob(to) approves
      //op_approve.who = bob_id;
      op_approve.approve = true;
      op_approve.escrow_id = 1;
      wdump((op_approve));
      trx.operations.push_back(op_approve);
      sign(trx, bob_private_key);
      PUSH_TX(db, trx);
      //GRAPHENE_REQUIRE_THROW(PUSH_TX( db, trx), fc::assert_exception); // who == to || who == agent:
      generate_block();
      trx.clear();

      escrow = db.get_escrow( alice_id, 1 );
      BOOST_REQUIRE( escrow.from == alice_id );
      BOOST_REQUIRE( escrow.to == bob_id );
      BOOST_REQUIRE( escrow.escrow_id == 1 );
      BOOST_REQUIRE( escrow.agent == sam_id );
      BOOST_REQUIRE( escrow.disputed == false );
      BOOST_REQUIRE( escrow.to_approved == true );
      BOOST_REQUIRE( escrow.agent_approved == false );

      // bob tries to approve again
      trx.operations.push_back(op_approve);
      sign(trx, bob_private_key);
      //sign(trx, alice_private_key);
      //PUSH_TX(db, trx);
      GRAPHENE_REQUIRE_THROW(PUSH_TX( db, trx), fc::assert_exception); // !escrow.to_approved: 'to' has already approved the escrow
      generate_block();
      trx.clear();

      // alice tries to release funds but agent has not approved yet!
      {
         escrow_release_operation op;

         op.from = alice_id;
         op.to = bob_id;
         op.who = alice_id;
         op.escrow_id = 1;
         op.amount = core.amount(1000);
         trx.operations.push_back(op);
         sign(trx, alice_private_key);
         GRAPHENE_REQUIRE_THROW(PUSH_TX( db, trx), fc::assert_exception); // e.to_approved && e.agent_approved: Funds cannot be released prior to escrow approval.
         generate_block();
         trx.clear();
      }

      BOOST_REQUIRE_EQUAL(get_balance(alice_id, asset_id_type()), 99999100); // the 1000 of alice are not in her account at this point.
      BOOST_REQUIRE_EQUAL(get_balance(bob_id, asset_id_type()), 0);
      BOOST_REQUIRE_EQUAL(get_balance(sam_id, asset_id_type()), 0);










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
