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


BOOST_AUTO_TEST_CASE( escrow_before_hf )
{
   try
   {
      ACTORS( (alice)(bob)(sam) );

      transfer(committee_account, alice_id, asset(100000000));
      const asset_object &core = asset_id_type()(db);

      // creating the escrow fails before hf
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
         GRAPHENE_REQUIRE_THROW(PUSH_TX( db, trx), fc::assert_exception); // db().head_block_time() > HARDFORK_ESCROW_TIME
         generate_block();
         trx.clear();
      }

      // approve op fails before hf

      // dispute fails before hf

      // escrow release fails before hf

   } FC_LOG_AND_RETHROW()
}

BOOST_AUTO_TEST_CASE( escrow_transfer )
{
   try
   {
      generate_blocks( HARDFORK_ESCROW_TIME );
      generate_block();
      set_expiration( db, trx );

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

   } FC_LOG_AND_RETHROW()
}

BOOST_AUTO_TEST_CASE( escrow_dispute )
{
   try
   {
      generate_blocks( HARDFORK_ESCROW_TIME );
      generate_block();
      set_expiration( db, trx );

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
         PUSH_TX(db, trx);
         generate_block();
         trx.clear();
      }

      // sam(agent) aproves
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

      // bob(to) never send payment offchain to alice(from) so alice(from) opens a dispute to get her money back
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

      // now the agent is in control of the funds, send money back to alice
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

BOOST_AUTO_TEST_CASE( escrow_expire )
{
   try {
      generate_blocks( HARDFORK_ESCROW_TIME );
      generate_block();
      set_expiration( db, trx );

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


      BOOST_REQUIRE_EQUAL(get_balance(alice_id, asset_id_type()), 99999000);
      BOOST_REQUIRE_EQUAL(get_balance(bob_id, asset_id_type()), 0);
      BOOST_REQUIRE_EQUAL(get_balance(sam_id, asset_id_type()), 0);

      // to and agent approves
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

      // escrow expires
      generate_blocks(db.head_block_time() + 201);
      set_expiration( db, trx );

      // alice release to herself
      {
         escrow_release_operation op;
         op.from = alice_id;
         op.to = bob_id;
         op.who = alice_id;
         op.receiver = alice_id;
         op.agent = sam_id;
         op.escrow_id = 0;
         op.amount = core.amount(1000);
         trx.operations.push_back(op);
         sign(trx, alice_private_key);
         PUSH_TX(db, trx);
         generate_block();
         trx.clear();
      }
      BOOST_REQUIRE_EQUAL(get_balance(alice_id, asset_id_type()), 100000000);
      BOOST_REQUIRE_EQUAL(get_balance(bob_id, asset_id_type()), 0);
      BOOST_REQUIRE_EQUAL(get_balance(sam_id, asset_id_type()), 0);

   } FC_LOG_AND_RETHROW()

}

BOOST_AUTO_TEST_CASE( escrow_ratification_deadline )
{
   try {
      // TODO: this test
   } FC_LOG_AND_RETHROW()
}

BOOST_AUTO_TEST_CASE( escrow_agent_fees )
{
   try {
      // TODO: this test
   } FC_LOG_AND_RETHROW()
}

BOOST_AUTO_TEST_CASE( escrow_validations )
{
   try {
      generate_blocks( HARDFORK_ESCROW_TIME );
      generate_block();
      set_expiration( db, trx );

      ACTORS( (alice)(bob)(sam)(paul) );

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

      // ratification deadline in the past, not allowed
      op.agent = sam_id;
      op.ratification_deadline = db.head_block_time() - 1;
      op.escrow_expiration = db.head_block_time() + 200;
      trx.operations.push_back(op);
      sign(trx, alice_private_key);
      GRAPHENE_REQUIRE_THROW(PUSH_TX( db, trx), fc::assert_exception); // o.ratification_deadline > db().head_block_time():
      generate_block();
      trx.clear();

      // expire in the past, not allowed
      op.ratification_deadline = db.head_block_time() + 1;
      op.escrow_expiration = db.head_block_time() - 1;
      trx.operations.push_back(op);
      sign(trx, alice_private_key);
      GRAPHENE_REQUIRE_THROW(PUSH_TX( db, trx), fc::assert_exception); // o.escrow_expiration > db().head_block_time():
      generate_block();
      trx.clear();

      // paul haves not enough core to make escrow
      op.ratification_deadline = db.head_block_time() + 100;
      op.escrow_expiration = db.head_block_time() + 100;
      op.from = paul_id;
      op.amount = core_id(db).amount(1000);
      trx.operations.push_back(op);
      sign(trx, paul_private_key);
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

      BOOST_REQUIRE_EQUAL(get_balance(alice_id, asset_id_type()), 99999000);
      BOOST_REQUIRE_EQUAL(get_balance(bob_id, asset_id_type()), 0);
      BOOST_REQUIRE_EQUAL(get_balance(sam_id, asset_id_type()), 0);

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
      op_approve.approve = true;
      trx.operations.push_back(op_approve);
      sign(trx, alice_private_key);
      GRAPHENE_REQUIRE_THROW(PUSH_TX( db, trx), fc::assert_exception); // who == to || who == agent:
      generate_block();
      trx.clear();

      // other account not involved in the escrow is not valid either
      op_approve.who = paul_id;
      trx.operations.push_back(op_approve);
      sign(trx, paul_private_key);
      GRAPHENE_REQUIRE_THROW(PUSH_TX( db, trx), fc::assert_exception); // who == to || who == agent:
      generate_block();
      trx.clear();

      BOOST_REQUIRE_EQUAL(get_balance(alice_id, asset_id_type()), 99999000);
      BOOST_REQUIRE_EQUAL(get_balance(bob_id, asset_id_type()), 0);
      BOOST_REQUIRE_EQUAL(get_balance(sam_id, asset_id_type()), 0);


      // any of the involved parties can remove the escrow by using approve = false
      // bob(to) deletes escrow
      op_approve.who = bob_id;
      op_approve.approve = false;
      trx.operations.push_back(op_approve);
      sign(trx, bob_private_key);
      PUSH_TX(db, trx);
      generate_block();
      trx.clear();

      // money returns to alice!
      BOOST_REQUIRE_EQUAL(get_balance(alice_id, asset_id_type()), 100000000);
      BOOST_REQUIRE_EQUAL(get_balance(bob_id, asset_id_type()), 0);
      BOOST_REQUIRE_EQUAL(get_balance(sam_id, asset_id_type()), 0);

      // cancel the escrow with agent
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

      BOOST_REQUIRE_EQUAL(get_balance(alice_id, asset_id_type()), 99999000);
      BOOST_REQUIRE_EQUAL(get_balance(bob_id, asset_id_type()), 0);
      BOOST_REQUIRE_EQUAL(get_balance(sam_id, asset_id_type()), 0);

      // alice cant cancel
      op_approve.who = alice_id;
      op_approve.approve = false;
      op_approve.escrow_id = 1;
      trx.operations.push_back(op_approve);
      sign(trx, alice_private_key);
      GRAPHENE_REQUIRE_THROW(PUSH_TX( db, trx), fc::assert_exception); // who == to || who == agent:
      generate_block();
      trx.clear();

      set_expiration( db, trx );

      //but agent can
      op_approve.who = sam_id;
      op_approve.approve = false;
      op_approve.escrow_id = 1;
      trx.operations.push_back(op_approve);
      sign(trx, sam_private_key);
      PUSH_TX( db, trx);
      generate_block();
      trx.clear();

      BOOST_REQUIRE_EQUAL(get_balance(alice_id, asset_id_type()), 100000000);
      BOOST_REQUIRE_EQUAL(get_balance(bob_id, asset_id_type()), 0);
      BOOST_REQUIRE_EQUAL(get_balance(sam_id, asset_id_type()), 0);

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
      op_approve.who = bob_id;
      op_approve.approve = true;
      op_approve.escrow_id = 1;
      trx.operations.push_back(op_approve);
      sign(trx, bob_private_key);
      PUSH_TX(db, trx);
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
      GRAPHENE_REQUIRE_THROW(PUSH_TX( db, trx), fc::assert_exception); // !escrow.to_approved: 'to' has already approved the escrow
      generate_block();
      trx.clear();

      // alice tries to release funds but agent has not approved yet
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

      BOOST_REQUIRE_EQUAL(get_balance(alice_id, asset_id_type()), 99999000); // the 1000 of alice are not in her account at this point.
      BOOST_REQUIRE_EQUAL(get_balance(bob_id, asset_id_type()), 0);
      BOOST_REQUIRE_EQUAL(get_balance(sam_id, asset_id_type()), 0);

      // moving to dispute validations

      // dispute cant be raised before all parties approved the escrow
      {
         escrow_dispute_operation op;
         op.from = alice_id;
         op.to = bob_id;
         op.escrow_id = 1;
         op.who = alice_id;
         op.agent = sam_id;
         trx.operations.push_back(op);
         sign(trx, alice_private_key);
         GRAPHENE_REQUIRE_THROW(PUSH_TX( db, trx), fc::assert_exception); // e.to_approved && e.agent_approved
         generate_block();
         trx.clear();
      }

      // agent approves so we can move forward to dispute
      op_approve.who = sam_id;
      op_approve.approve = true;
      op_approve.escrow_id = 1;
      trx.operations.push_back(op_approve);
      sign(trx, sam_private_key);
      PUSH_TX(db, trx);
      generate_block();
      trx.clear();

      // someone from outside will not be able to raise dispute
      {
         escrow_dispute_operation op;
         op.from = alice_id;
         op.to = bob_id;
         op.escrow_id = 1;
         op.who = paul_id;
         op.agent = sam_id;
         trx.operations.push_back(op);
         sign(trx, paul_private_key);
         GRAPHENE_REQUIRE_THROW(PUSH_TX( db, trx), fc::assert_exception); // who == from || who == to
         generate_block();
         trx.clear();
      }

      // agent dont match
      {
         escrow_dispute_operation op;
         op.from = alice_id;
         op.to = bob_id;
         op.escrow_id = 1;
         op.who = alice_id;
         op.agent = alice_id;
         trx.operations.push_back(op);
         sign(trx, alice_private_key);
         GRAPHENE_REQUIRE_THROW(PUSH_TX( db, trx), fc::assert_exception); // e.agent == o.agent
         generate_block();
         trx.clear();
      }

      // to dont match
      {
         escrow_dispute_operation op;
         op.from = alice_id;
         op.to = alice_id;
         op.escrow_id = 1;
         op.who = alice_id;
         op.agent = sam_id;
         trx.operations.push_back(op);
         sign(trx, alice_private_key);
         GRAPHENE_REQUIRE_THROW(PUSH_TX( db, trx), fc::assert_exception); // e.to == o.to
         generate_block();
         trx.clear();
      }

      escrow = db.get_escrow( alice_id, 1 );
      BOOST_REQUIRE( escrow.from == alice_id );
      BOOST_REQUIRE( escrow.to == bob_id );
      BOOST_REQUIRE( escrow.escrow_id == 1 );
      BOOST_REQUIRE( escrow.agent == sam_id );
      BOOST_REQUIRE( escrow.disputed == false );
      BOOST_REQUIRE( escrow.to_approved == true );
      BOOST_REQUIRE( escrow.agent_approved == true );

      // raising dispute
      {
         escrow_dispute_operation op;
         op.from = alice_id;
         op.to = bob_id;
         op.escrow_id = 1;
         op.who = alice_id;
         op.agent = sam_id;
         trx.operations.push_back(op);
         sign(trx, alice_private_key);
         PUSH_TX(db, trx);
         generate_block();
         trx.clear();
      }

      // raising again will fail
      {
         escrow_dispute_operation op;
         op.from = alice_id;
         op.to = bob_id;
         op.escrow_id = 1;
         op.who = alice_id;
         op.agent = sam_id;
         trx.operations.push_back(op);
         sign(trx, alice_private_key);
         GRAPHENE_REQUIRE_THROW(PUSH_TX( db, trx), fc::assert_exception); // !e.disputed: escrow is already under dispute
         generate_block();
         trx.clear();
      }

      // checking dispute flag
      escrow = db.get_escrow( alice_id, 1 );
      BOOST_REQUIRE( escrow.from == alice_id );
      BOOST_REQUIRE( escrow.to == bob_id );
      BOOST_REQUIRE( escrow.escrow_id == 1 );
      BOOST_REQUIRE( escrow.agent == sam_id );
      BOOST_REQUIRE( escrow.disputed == true );
      BOOST_REQUIRE( escrow.to_approved == true );
      BOOST_REQUIRE( escrow.agent_approved == true );

      // now escrow is under dispute lets test the validation in the release

      // nobody expect agent can release in a disputed op
      {
         escrow_release_operation op;
         op.from = alice_id;
         op.to = bob_id;
         op.who = alice_id;
         op.escrow_id = 1;
         op.receiver = bob_id;
         op.agent = sam_id;
         op.amount = core.amount(1000);
         trx.operations.push_back(op);
         sign(trx, alice_private_key);
         GRAPHENE_REQUIRE_THROW(PUSH_TX( db, trx), fc::assert_exception); // o.who == e.agent
         generate_block();
         trx.clear();
      }
      {
         escrow_release_operation op;
         op.from = alice_id;
         op.to = bob_id;
         op.who = bob_id;
         op.escrow_id = 1;
         op.receiver = bob_id;
         op.agent = sam_id;
         op.amount = core.amount(1000);
         trx.operations.push_back(op);
         sign(trx, bob_private_key);
         GRAPHENE_REQUIRE_THROW(PUSH_TX( db, trx), fc::assert_exception); // o.who == e.agent
         generate_block();
         trx.clear();
      }

      // amount must be in bts
      {
         escrow_release_operation op;
         op.from = alice_id;
         op.to = bob_id;
         op.who = sam_id;
         op.escrow_id = 1;
         op.receiver = bob_id;
         op.agent = sam_id;
         op.amount = bitusd_id(db).amount(1000);
         trx.operations.push_back(op);
         sign(trx, sam_private_key);
         GRAPHENE_REQUIRE_THROW(PUSH_TX( db, trx), fc::assert_exception); // amount.asset_id == asset_id_type()
         generate_block();
         trx.clear();
      }
      set_expiration( db, trx );
      // amount is greater than in escrow!
      {
         escrow_release_operation op;
         op.from = alice_id;
         op.to = bob_id;
         op.who = sam_id;
         op.escrow_id = 1;
         op.receiver = bob_id;
         op.agent = sam_id;
         op.amount = core_id(db).amount(2000);
         trx.operations.push_back(op);
         sign(trx, sam_private_key);
         GRAPHENE_REQUIRE_THROW(PUSH_TX( db, trx), fc::assert_exception); // e.amount >= o.amount && e.amount.asset_id == o.amount.asset_id:
         generate_block();
         trx.clear();
      }

      // agent can release an amount smaller than what it is in escrow
      {
         escrow_release_operation op;
         op.from = alice_id;
         op.to = bob_id;
         op.who = sam_id;
         op.escrow_id = 1;
         op.receiver = bob_id;
         op.agent = sam_id;
         op.amount = core_id(db).amount(500);
         trx.operations.push_back(op);
         sign(trx, sam_private_key);
         PUSH_TX(db, trx);
         generate_block();
         trx.clear();
      }

      // escrow still there as there is still pending amount:
      escrow = db.get_escrow( alice_id, 1 );
      BOOST_REQUIRE( escrow.amount.amount == 500);

      // release the other half of the amount
      {
         escrow_release_operation op;
         op.from = alice_id;
         op.to = bob_id;
         op.who = sam_id;
         op.escrow_id = 1;
         op.receiver = bob_id;
         op.agent = sam_id;
         op.amount = core_id(db).amount(500);
         trx.operations.push_back(op);
         sign(trx, sam_private_key);
         PUSH_TX(db, trx);
         generate_block();
         trx.clear();
      }
      // escrow is gone now
      GRAPHENE_REQUIRE_THROW(db.get_escrow( alice_id, 1 ), fc::assert_exception); // escrow not found

      // create new escrow with no dispute flag
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

      // bob(to) cant release funds to himself
      {
         escrow_release_operation op;
         op.from = alice_id;
         op.to = bob_id;
         op.who = bob_id;
         op.escrow_id = 0;
         op.receiver = bob_id;
         op.agent = sam_id;
         op.amount = core_id(db).amount(500);
         trx.operations.push_back(op);
         sign(trx, bob_private_key);
         GRAPHENE_REQUIRE_THROW(PUSH_TX( db, trx), fc::assert_exception); // o.receiver == e.from: 'to' must release funds to 'from'
         generate_block();
         trx.clear();
      }

      // let alice send everything to bob
      {
         escrow_release_operation op;
         op.from = alice_id;
         op.to = bob_id;
         op.who = alice_id;
         op.escrow_id = 0;
         op.receiver = bob_id;
         op.agent = sam_id;
         op.amount = core_id(db).amount(1000);
         trx.operations.push_back(op);
         sign(trx, alice_private_key);
         PUSH_TX( db, trx);
         generate_block();
         trx.clear();
      }

      BOOST_REQUIRE_EQUAL(get_balance(alice_id, asset_id_type()), 99998000);
      BOOST_REQUIRE_EQUAL(get_balance(bob_id, asset_id_type()), 2000);
      BOOST_REQUIRE_EQUAL(get_balance(sam_id, asset_id_type()), 0);

   } FC_LOG_AND_RETHROW()
}

BOOST_AUTO_TEST_CASE( escrow_authorities )
{
   try
   {
      generate_blocks( HARDFORK_ESCROW_TIME );
      generate_block();
      set_expiration( db, trx );

      ACTORS( (alice)(bob)(sam) );
      const auto& core   = asset_id_type()(db);
      //asset_id_type core_id = core.id;

      transfer(committee_account, alice_id, asset(100000000));

      // escrow transfer create
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

      flat_set< account_id_type > auths;
      flat_set< account_id_type > expected;

      op.get_required_active_authorities( auths );
      expected.insert( alice_id );
      BOOST_REQUIRE( auths == expected );

      trx.operations.push_back(op);
      sign(trx, alice_private_key);
      PUSH_TX(db, trx);
      generate_block();
      trx.clear();

      // escrow approve
      escrow_approve_operation op_a;
      op_a.from = alice_id;
      op_a.to = bob_id;
      op_a.who = sam_id;
      op_a.escrow_id = 0;
      op_a.agent = sam_id;
      op_a.approve = true;

      op_a.get_required_active_authorities( auths );
      expected.insert( alice_id );
      BOOST_REQUIRE( auths == expected );
      //trx.operations.push_back(op);
      //sign(trx, sam_private_key);
      //PUSH_TX(db, trx);
      //generate_block();
      //trx.clear();


      // escrow dispute

      // escrow release

   } FC_LOG_AND_RETHROW()
}


BOOST_AUTO_TEST_CASE( escrow_expire_auto )
{
   try
   {
      generate_blocks( HARDFORK_ESCROW_TIME );
      generate_block();
      set_expiration( db, trx );

      ACTORS( (alice)(bob)(sam) );
      const auto& core   = asset_id_type()(db);

      transfer(committee_account, alice_id, asset(100000000));

      // escrow transfer create
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

      auto escrow = db.get_escrow( alice_id, 0 );
      BOOST_REQUIRE( escrow.from == alice_id );
      BOOST_REQUIRE( escrow.to == bob_id );
      BOOST_REQUIRE( escrow.escrow_id == 0 );
      BOOST_REQUIRE( escrow.agent == sam_id );
      BOOST_REQUIRE( escrow.disputed == false );
      BOOST_REQUIRE( escrow.to_approved == false );
      BOOST_REQUIRE( escrow.agent_approved == false );

      // escrow expires
      generate_blocks(db.head_block_time() + 201);
      set_expiration( db, trx );

      generate_blocks(db.get_dynamic_global_properties().next_maintenance_time);

      GRAPHENE_CHECK_THROW(db.get_escrow( alice_id, 0 ), fc::assert_exception);

      // money is returned automatically to alice at expiration
      BOOST_REQUIRE_EQUAL(get_balance(alice_id, asset_id_type()), 100000000);


   } FC_LOG_AND_RETHROW()
}


BOOST_AUTO_TEST_SUITE_END()
