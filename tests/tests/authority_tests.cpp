/*
 * Copyright (c) 2015, Cryptonomex, Inc.
 * All rights reserved.
 *
 * This source code is provided for evaluation in private test networks only, until September 8, 2015. After this date, this license expires and
 * the code may not be used, modified or distributed for any purpose. Redistribution and use in source and binary forms, with or without modification,
 * are permitted until September 8, 2015, provided that the following conditions are met:
 *
 * 1. The code and/or derivative works are used only for private test networks consisting of no more than 10 P2P nodes.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <boost/test/unit_test.hpp>

#include <graphene/chain/database.hpp>
#include <graphene/chain/exceptions.hpp>
#include <graphene/chain/operations.hpp>

#include <graphene/chain/account_object.hpp>
#include <graphene/chain/asset_object.hpp>
#include <graphene/chain/delegate_object.hpp>
#include <graphene/chain/proposal_object.hpp>

#include <graphene/db/simple_index.hpp>

#include <fc/crypto/digest.hpp>
#include "../common/database_fixture.hpp"

using namespace graphene::chain;

BOOST_FIXTURE_TEST_SUITE( authority_tests, database_fixture )

BOOST_AUTO_TEST_CASE( simple_single_signature )
{ try {
   try {
      fc::ecc::private_key nathan_key = fc::ecc::private_key::generate();
      const account_object& nathan = create_account("nathan", nathan_key.get_public_key());
      const asset_object& core = asset_id_type()(db);
      auto old_balance = fund(nathan);

      transfer_operation op = {asset(),nathan.id, account_id_type(), core.amount(500)};
      trx.operations.push_back(op);
      sign(trx, nathan_key);
      PUSH_TX( db, trx, database::skip_transaction_dupe_check );

      BOOST_CHECK_EQUAL(get_balance(nathan, core), old_balance - 500);
   } catch (fc::exception& e) {
      edump((e.to_detail_string()));
      throw;
   }
} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE( any_two_of_three )
{
   try {
      fc::ecc::private_key nathan_key1 = fc::ecc::private_key::regenerate(fc::digest("key1"));
      fc::ecc::private_key nathan_key2 = fc::ecc::private_key::regenerate(fc::digest("key2"));
      fc::ecc::private_key nathan_key3 = fc::ecc::private_key::regenerate(fc::digest("key3"));
      const account_object& nathan = create_account("nathan", nathan_key1.get_public_key() );
      const asset_object& core = asset_id_type()(db);
      auto old_balance = fund(nathan);

      try {
         account_update_operation op;
         op.account = nathan.id;
         op.active = authority(2, public_key_type(nathan_key1.get_public_key()), 1, public_key_type(nathan_key2.get_public_key()), 1, public_key_type(nathan_key3.get_public_key()), 1);
         op.owner = *op.active;
         trx.operations.push_back(op);
         sign(trx, nathan_key1);
         PUSH_TX( db, trx, database::skip_transaction_dupe_check );
         trx.operations.clear();
         trx.signatures.clear();
      } FC_CAPTURE_AND_RETHROW ((nathan.active))

      transfer_operation op = {asset(), nathan.id, account_id_type(), core.amount(500)};
      trx.operations.push_back(op);
      sign(trx, nathan_key1);
      GRAPHENE_CHECK_THROW(PUSH_TX( db, trx, database::skip_transaction_dupe_check ), fc::exception);
      sign(trx, nathan_key2);
      PUSH_TX( db, trx, database::skip_transaction_dupe_check );
      BOOST_CHECK_EQUAL(get_balance(nathan, core), old_balance - 500);

      trx.signatures.clear();
      sign(trx, nathan_key2);
      sign(trx, nathan_key3);
      PUSH_TX( db, trx, database::skip_transaction_dupe_check );
      BOOST_CHECK_EQUAL(get_balance(nathan, core), old_balance - 1000);

      trx.signatures.clear();
      sign(trx, nathan_key1);
      sign(trx, nathan_key3);
      PUSH_TX( db, trx, database::skip_transaction_dupe_check );
      BOOST_CHECK_EQUAL(get_balance(nathan, core), old_balance - 1500);

      trx.signatures.clear();
      //sign(trx, fc::ecc::private_key::generate());
      sign(trx,nathan_key3);
      GRAPHENE_CHECK_THROW(PUSH_TX( db, trx, database::skip_transaction_dupe_check ), fc::exception);
      BOOST_CHECK_EQUAL(get_balance(nathan, core), old_balance - 1500);
   } catch (fc::exception& e) {
      edump((e.to_detail_string()));
      throw;
   }
}

BOOST_AUTO_TEST_CASE( recursive_accounts )
{
   try {
      fc::ecc::private_key parent1_key = fc::ecc::private_key::generate();
      fc::ecc::private_key parent2_key = fc::ecc::private_key::generate();
      const auto& core = asset_id_type()(db);

      BOOST_TEST_MESSAGE( "Creating parent1 and parent2 accounts" );
      const account_object& parent1 = create_account("parent1", parent1_key.get_public_key());
      const account_object& parent2 = create_account("parent2", parent2_key.get_public_key());

      BOOST_TEST_MESSAGE( "Creating child account that requires both parent1 and parent2 to approve" );
      {
         auto make_child_op = make_account("child");
         make_child_op.owner = authority(2, account_id_type(parent1.id), 1, account_id_type(parent2.id), 1);
         make_child_op.active = authority(2, account_id_type(parent1.id), 1, account_id_type(parent2.id), 1);
         trx.operations.push_back(make_child_op);
         PUSH_TX( db, trx, ~0 );
         trx.operations.clear();
      }

      const account_object& child = get_account("child");
      auto old_balance = fund(child);

      BOOST_TEST_MESSAGE( "Attempting to transfer with no signatures, should fail" );
      transfer_operation op = {asset(), child.id, account_id_type(), core.amount(500)};
      trx.operations.push_back(op);
      GRAPHENE_CHECK_THROW(PUSH_TX( db, trx, database::skip_transaction_dupe_check ), fc::exception);

      BOOST_TEST_MESSAGE( "Attempting to transfer with parent1 signature, should fail" );
      sign(trx,parent1_key);
      GRAPHENE_CHECK_THROW(PUSH_TX( db, trx, database::skip_transaction_dupe_check ), fc::exception);
      trx.signatures.clear();

      BOOST_TEST_MESSAGE( "Attempting to transfer with parent2 signature, should fail" );
      sign(trx,parent2_key);
      GRAPHENE_CHECK_THROW(PUSH_TX( db, trx, database::skip_transaction_dupe_check ), fc::exception);

      BOOST_TEST_MESSAGE( "Attempting to transfer with parent1 and parent2 signature, should succeed" );
      sign(trx,parent1_key);
      PUSH_TX( db, trx, database::skip_transaction_dupe_check );
      BOOST_CHECK_EQUAL(get_balance(child, core), old_balance - 500);
      trx.operations.clear();
      trx.signatures.clear();

      BOOST_TEST_MESSAGE( "Adding a key for the child that can override parents" );
      fc::ecc::private_key child_key = fc::ecc::private_key::generate();
      {
         account_update_operation op;
         op.account = child.id;
         op.active = authority(2, account_id_type(parent1.id), 1, account_id_type(parent2.id), 1,
                               public_key_type(child_key.get_public_key()), 2);
         trx.operations.push_back(op);
         sign(trx,parent1_key);
         sign(trx,parent2_key);
         PUSH_TX( db, trx, database::skip_transaction_dupe_check );
         BOOST_REQUIRE_EQUAL(child.active.num_auths(), 3);
         trx.operations.clear();
         trx.signatures.clear();
      }

      op = {asset(),child.id, account_id_type(), core.amount(500)};
      trx.operations.push_back(op);
      BOOST_TEST_MESSAGE( "Attempting transfer with no signatures, should fail" );
      GRAPHENE_CHECK_THROW(PUSH_TX( db, trx, database::skip_transaction_dupe_check ), fc::exception);
      BOOST_TEST_MESSAGE( "Attempting transfer just parent1, should fail" );
      sign(trx, parent1_key);
      GRAPHENE_CHECK_THROW(PUSH_TX( db, trx, database::skip_transaction_dupe_check ), fc::exception);
      trx.signatures.clear();
      BOOST_TEST_MESSAGE( "Attempting transfer just parent2, should fail" );
      sign(trx, parent2_key);
      GRAPHENE_CHECK_THROW(PUSH_TX( db, trx, database::skip_transaction_dupe_check ), fc::exception);

      BOOST_TEST_MESSAGE( "Attempting transfer both parents, should succeed" );
      sign(trx,  parent1_key);
      PUSH_TX( db, trx, database::skip_transaction_dupe_check );
      BOOST_CHECK_EQUAL(get_balance(child, core), old_balance - 1000);
      trx.signatures.clear();

      BOOST_TEST_MESSAGE( "Attempting transfer with just child key, should succeed" );
      sign(trx, child_key);
      PUSH_TX( db, trx, database::skip_transaction_dupe_check );
      BOOST_CHECK_EQUAL(get_balance(child, core), old_balance - 1500);
      trx.operations.clear();
      trx.signatures.clear();

      BOOST_TEST_MESSAGE( "Creating grandparent account, parent1 now requires authority of grandparent" );
      auto grandparent = create_account("grandparent");
      fc::ecc::private_key grandparent_key = fc::ecc::private_key::generate();
      {
         account_update_operation op;
         op.account = parent1.id;
         op.active = authority(1, account_id_type(grandparent.id), 1);
         op.owner = *op.active;
         trx.operations.push_back(op);
         op.account = grandparent.id;
         op.active = authority(1, public_key_type(grandparent_key.get_public_key()), 1);
         op.owner = *op.active;
         trx.operations.push_back(op);
         PUSH_TX( db, trx, ~0 );
         trx.operations.clear();
         trx.signatures.clear();
      }

      BOOST_TEST_MESSAGE( "Attempt to transfer using old parent keys, should fail" );
      trx.operations.push_back(op);
      sign(trx, parent1_key);
      sign(trx, parent2_key);
      GRAPHENE_CHECK_THROW(PUSH_TX( db, trx, database::skip_transaction_dupe_check ), fc::exception);
      trx.signatures.clear();
      trx.sign( parent2_key );
      trx.sign( grandparent_key );

      BOOST_TEST_MESSAGE( "Attempt to transfer using parent2_key and grandparent_key" );
      PUSH_TX( db, trx, database::skip_transaction_dupe_check );
      BOOST_CHECK_EQUAL(get_balance(child, core), old_balance - 2000);
      trx.clear();

      BOOST_TEST_MESSAGE( "Update grandparent account authority to be genesis account" );
      {
         account_update_operation op;
         op.account = grandparent.id;
         op.active = authority(1, account_id_type(), 1);
         op.owner = *op.active;
         trx.operations.push_back(op);
         PUSH_TX( db, trx, ~0 );
         trx.operations.clear();
         trx.signatures.clear();
      }

      BOOST_TEST_MESSAGE( "Create recursion depth failure" );
      trx.operations.push_back(op);
      sign(trx, parent2_key);
      sign(trx, grandparent_key);
      sign(trx, delegate_priv_key);
      //Fails due to recursion depth.
      GRAPHENE_CHECK_THROW(PUSH_TX( db, trx, database::skip_transaction_dupe_check ), fc::exception);
      BOOST_TEST_MESSAGE( "verify child key can override recursion checks" );
      trx.signatures.clear();
      sign(trx,  child_key);
      PUSH_TX( db, trx, database::skip_transaction_dupe_check );
      BOOST_CHECK_EQUAL(get_balance(child, core), old_balance - 2500);
      trx.operations.clear();
      trx.signatures.clear();

      BOOST_TEST_MESSAGE( "Verify a cycle fails" );
      {
         account_update_operation op;
         op.account = parent1.id;
         op.active = authority(1, account_id_type(child.id), 1);
         op.owner = *op.active;
         trx.operations.push_back(op);
         PUSH_TX( db, trx, ~0 );
         trx.operations.clear();
         trx.signatures.clear();
      }

      trx.operations.push_back(op);
      sign(trx, parent2_key);
      //Fails due to recursion depth.
      GRAPHENE_CHECK_THROW(PUSH_TX( db, trx, database::skip_transaction_dupe_check ), fc::exception);
   } catch (fc::exception& e) {
      edump((e.to_detail_string()));
      throw;
   }
}

BOOST_AUTO_TEST_CASE( proposed_single_account )
{
   using namespace graphene::chain;
   try {
      INVOKE(any_two_of_three);

      fc::ecc::private_key genesis_key = delegate_priv_key;
      fc::ecc::private_key nathan_key1 = fc::ecc::private_key::regenerate(fc::digest("key1"));
      fc::ecc::private_key nathan_key2 = fc::ecc::private_key::regenerate(fc::digest("key2"));
      fc::ecc::private_key nathan_key3 = fc::ecc::private_key::regenerate(fc::digest("key3"));

      const account_object& moneyman = create_account("moneyman", delegate_pub_key);
      const account_object& nathan = get_account("nathan");
      const asset_object& core = asset_id_type()(db);

      transfer(account_id_type()(db), moneyman, core.amount(1000000));

      //Following any_two_of_three, nathan's active authority is satisfied by any two of {key1,key2,key3}
      BOOST_TEST_MESSAGE( "moneyman is creating proposal for nathan to transfer 100 CORE to moneyman" );
      proposal_create_operation op = {moneyman.id, asset(),
                                      {{transfer_operation{asset(),nathan.id, moneyman.get_id(), core.amount(100)}}},
                                      db.head_block_time() + fc::days(1)};
      asset nathan_start_balance = db.get_balance(nathan.get_id(), core.get_id());
      {
         flat_set<account_id_type> active_set, owner_set;
         operation_get_required_active_authorities(op,active_set);
         operation_get_required_owner_authorities(op,owner_set);
         BOOST_CHECK_EQUAL(active_set.size(), 1);
         BOOST_CHECK_EQUAL(owner_set.size(), 0);
         BOOST_CHECK(*active_set.begin() == moneyman.get_id());

         active_set.clear();
         //op.proposed_ops.front().get_required_auth(active_set, owner_set);
         operation_get_required_active_authorities( op.proposed_ops.front().op, active_set );
         operation_get_required_owner_authorities( op.proposed_ops.front().op, owner_set );
         BOOST_CHECK_EQUAL(active_set.size(), 1);
         BOOST_CHECK_EQUAL(owner_set.size(), 0);
         BOOST_CHECK(*active_set.begin() == nathan.id);
      }

      trx.operations.push_back(op);
      trx.set_expiration(db.head_block_id());

      //idump((moneyman));
      trx.sign( delegate_priv_key );
      const proposal_object& proposal = db.get<proposal_object>(PUSH_TX( db, trx ).operation_results.front().get<object_id_type>());

      BOOST_CHECK_EQUAL(proposal.required_active_approvals.size(), 1);
      BOOST_CHECK_EQUAL(proposal.available_active_approvals.size(), 0);
      BOOST_CHECK_EQUAL(proposal.required_owner_approvals.size(), 0);
      BOOST_CHECK_EQUAL(proposal.available_owner_approvals.size(), 0);
      BOOST_CHECK(*proposal.required_active_approvals.begin() == nathan.id);

      trx.operations = {proposal_update_operation{account_id_type(), asset(), proposal.id,{nathan.id},flat_set<account_id_type>{},flat_set<account_id_type>{},flat_set<account_id_type>{},flat_set<public_key_type>{},flat_set<public_key_type>{}}};
      trx.sign(  genesis_key );
      //Genesis may not add nathan's approval.
      GRAPHENE_CHECK_THROW(PUSH_TX( db, trx ), fc::exception);
      trx.operations = {proposal_update_operation{account_id_type(), asset(), proposal.id,{account_id_type()},flat_set<account_id_type>{},flat_set<account_id_type>{},flat_set<account_id_type>{},flat_set<public_key_type>{},flat_set<public_key_type>{}}};
      trx.sign(  genesis_key );
      //Genesis has no stake in the transaction.
      GRAPHENE_CHECK_THROW(PUSH_TX( db, trx ), fc::exception);

      trx.signatures.clear();
      trx.operations = {proposal_update_operation{nathan.id, asset(), proposal.id,{nathan.id},flat_set<account_id_type>{},flat_set<account_id_type>{},flat_set<account_id_type>{},flat_set<public_key_type>{},flat_set<public_key_type>{}}};
      trx.sign(  nathan_key3 );
      trx.sign(  nathan_key2 );
      // TODO: verify the key_id is proper...
      //trx.signatures = {nathan_key3.sign_compact(trx.digest()), nathan_key2.sign_compact(trx.digest())};

      BOOST_CHECK_EQUAL(get_balance(nathan, core), nathan_start_balance.amount.value);
      PUSH_TX( db, trx );
      BOOST_CHECK_EQUAL(get_balance(nathan, core), nathan_start_balance.amount.value - 100);
   } catch (fc::exception& e) {
      edump((e.to_detail_string()));
      throw;
   }
}

/// Verify that genesis authority cannot be invoked in a normal transaction
BOOST_AUTO_TEST_CASE( genesis_authority )
{ try {
   fc::ecc::private_key nathan_key = fc::ecc::private_key::generate();
   fc::ecc::private_key genesis_key = delegate_priv_key;
   const account_object nathan = create_account("nathan", nathan_key.get_public_key());
   const auto& global_params = db.get_global_properties().parameters;

   generate_block();

   // Signatures are for suckers.
   db.modify(db.get_global_properties(), [](global_property_object& p) {
      // Turn the review period WAY down, so it doesn't take long to produce blocks to that point in simulated time.
      p.parameters.committee_proposal_review_period = fc::days(1).to_seconds();
   });

   BOOST_TEST_MESSAGE( "transfering 100000 CORE to nathan, signing with genesis key" );
   trx.operations.push_back(transfer_operation({asset(), account_id_type(), nathan.id, asset(100000)}));
   sign(trx, genesis_key);
   GRAPHENE_CHECK_THROW(PUSH_TX( db, trx ), fc::exception);

   auto sign = [&] { trx.signatures.clear(); trx.sign(nathan_key); };

   proposal_create_operation pop;
   pop.proposed_ops.push_back({trx.operations.front()});
   pop.expiration_time = db.head_block_time() + global_params.committee_proposal_review_period*2;
   pop.fee_paying_account = nathan.id;
   trx.operations = {pop};
   sign();

   // The review period isn't set yet. Make sure it throws.
   GRAPHENE_REQUIRE_THROW( PUSH_TX( db, trx ), proposal_create_review_period_required );
   pop.review_period_seconds = global_params.committee_proposal_review_period / 2;
   trx.operations.back() = pop;
   sign();
   // The review period is too short. Make sure it throws.
   GRAPHENE_REQUIRE_THROW( PUSH_TX( db, trx ), proposal_create_review_period_insufficient );
   pop.review_period_seconds = global_params.committee_proposal_review_period;
   trx.operations.back() = pop;
   sign();
   proposal_object prop = db.get<proposal_object>(PUSH_TX( db, trx ).operation_results.front().get<object_id_type>());
   BOOST_REQUIRE(db.find_object(prop.id));

   BOOST_CHECK(prop.expiration_time == pop.expiration_time);
   BOOST_CHECK(prop.review_period_time && *prop.review_period_time == pop.expiration_time - *pop.review_period_seconds);
   BOOST_CHECK(prop.proposed_transaction.operations.size() == 1);
   BOOST_CHECK_EQUAL(get_balance(nathan, asset_id_type()(db)), 0);
   BOOST_CHECK(!db.get<proposal_object>(prop.id).is_authorized_to_execute(db));

   generate_block();
   BOOST_REQUIRE(db.find_object(prop.id));
   BOOST_CHECK_EQUAL(get_balance(nathan, asset_id_type()(db)), 0);

   BOOST_TEST_MESSAGE( "Checking that the proposal is not authorized to execute" );
   BOOST_REQUIRE(!db.get<proposal_object>(prop.id).is_authorized_to_execute(db));
   trx.operations.clear();
   trx.signatures.clear();
   proposal_update_operation uop;
   uop.fee_paying_account = GRAPHENE_TEMP_ACCOUNT;
   uop.proposal = prop.id;

   uop.key_approvals_to_add.emplace(genesis_key.get_public_key());
   /*
   uop.key_approvals_to_add.emplace(1);
   uop.key_approvals_to_add.emplace(2);
   uop.key_approvals_to_add.emplace(3);
   uop.key_approvals_to_add.emplace(4);
   uop.key_approvals_to_add.emplace(5);
   uop.key_approvals_to_add.emplace(6);
   */
   trx.operations.push_back(uop);
   trx.sign(genesis_key);
   db.push_transaction(trx);
   BOOST_CHECK_EQUAL(get_balance(nathan, asset_id_type()(db)), 0);
   BOOST_CHECK(db.get<proposal_object>(prop.id).is_authorized_to_execute(db));

   generate_blocks(*prop.review_period_time);
   uop.key_approvals_to_add.clear();
   uop.key_approvals_to_add.insert(genesis_key.get_public_key()); // was 7
   trx.operations.back() = uop;
   trx.sign( genesis_key);
   // Should throw because the transaction is now in review.
   GRAPHENE_CHECK_THROW(PUSH_TX( db, trx ), fc::exception);

   generate_blocks(prop.expiration_time);
   BOOST_CHECK_EQUAL(get_balance(nathan, asset_id_type()(db)), 100000);
} FC_LOG_AND_RETHROW() }

BOOST_FIXTURE_TEST_CASE( fired_delegates, database_fixture )
{ try {
   generate_block();
   fc::ecc::private_key genesis_key = delegate_priv_key;
   fc::ecc::private_key delegate_key = fc::ecc::private_key::generate();

   //Meet nathan. He has a little money.
   const account_object* nathan = &create_account("nathan");
   transfer(account_id_type()(db), *nathan, asset(5000));
   generate_block();
   nathan = &get_account("nathan");
   flat_set<vote_id_type> delegates;

   db.modify(db.get_global_properties(), [](global_property_object& p) {
      // Turn the review period WAY down, so it doesn't take long to produce blocks to that point in simulated time.
      p.parameters.committee_proposal_review_period = fc::days(1).to_seconds();
   });

   for( int i = 0; i < 15; ++i )
   {
      const auto& account = create_account("delegate" + fc::to_string(i+1), delegate_key.get_public_key());
      upgrade_to_lifetime_member(account);
      delegates.insert(create_delegate(account).vote_id);
   }

   //A proposal is created to give nathan lots more money.
   proposal_create_operation pop = proposal_create_operation::genesis_proposal(db);
   pop.fee_paying_account = GRAPHENE_TEMP_ACCOUNT;
   pop.expiration_time = db.head_block_time() + *pop.review_period_seconds * 3;
   pop.proposed_ops.emplace_back(transfer_operation({asset(),account_id_type(), nathan->id, asset(100000)}));
   trx.operations.push_back(pop);
   const proposal_object& prop = db.get<proposal_object>(PUSH_TX( db, trx ).operation_results.front().get<object_id_type>());
   proposal_id_type pid = prop.id;
   BOOST_CHECK(!pid(db).is_authorized_to_execute(db));

   //Genesis key approves of the proposal.
   proposal_update_operation uop;
   uop.fee_paying_account = GRAPHENE_TEMP_ACCOUNT;
   uop.proposal = pid;
   uop.key_approvals_to_add.emplace(delegate_pub_key);
   /* TODO: what should this really be?
   uop.key_approvals_to_add.emplace(2);
   uop.key_approvals_to_add.emplace(3);
   uop.key_approvals_to_add.emplace(4);
   uop.key_approvals_to_add.emplace(5);
   uop.key_approvals_to_add.emplace(6);
   uop.key_approvals_to_add.emplace(7);
   uop.key_approvals_to_add.emplace(8);
   uop.key_approvals_to_add.emplace(9);
   */
   trx.operations.back() = uop;
   trx.sign(genesis_key);
   PUSH_TX( db, trx );
   BOOST_CHECK(pid(db).is_authorized_to_execute(db));

   //Time passes... the proposal is now in its review period.
   generate_blocks(*pid(db).review_period_time);

   fc::time_point_sec maintenance_time = db.get_dynamic_global_properties().next_maintenance_time;
   BOOST_CHECK_LT(maintenance_time.sec_since_epoch(), pid(db).expiration_time.sec_since_epoch());
   //Yay! The proposal to give nathan more money is authorized.
   BOOST_CHECK(pid(db).is_authorized_to_execute(db));

   nathan = &get_account("nathan");
   // no money yet
   BOOST_CHECK_EQUAL(get_balance(*nathan, asset_id_type()(db)), 5000);

   {
      //Oh noes! Nathan votes for a whole new slate of delegates!
      account_update_operation op;
      op.account = nathan->id;
      op.new_options = nathan->options;
      op.new_options->votes = delegates;
      trx.operations.push_back(op);
      trx.set_expiration(db.head_block_time() + GRAPHENE_DEFAULT_MAX_TIME_UNTIL_EXPIRATION);
      PUSH_TX( db, trx, ~0 );
      trx.operations.clear();
   }
   // still no money
   BOOST_CHECK_EQUAL(get_balance(*nathan, asset_id_type()(db)), 5000);

   //Time passes... the set of active delegates gets updated.
   generate_blocks(maintenance_time);
   //The proposal is no longer authorized, because the active delegates got changed.
   BOOST_CHECK(!pid(db).is_authorized_to_execute(db));
   // still no money
   BOOST_CHECK_EQUAL(get_balance(*nathan, asset_id_type()(db)), 5000);

   //Time passes... the proposal has now expired.
   generate_blocks(pid(db).expiration_time);
   BOOST_CHECK(db.find(pid) == nullptr);

   //Nathan never got any more money because the proposal was rejected.
   BOOST_CHECK_EQUAL(get_balance(*nathan, asset_id_type()(db)), 5000);
} FC_LOG_AND_RETHROW() }

BOOST_FIXTURE_TEST_CASE( proposal_two_accounts, database_fixture )
{ try {
   generate_block();

   auto nathan_key = generate_private_key("nathan");
   auto dan_key = generate_private_key("dan");
   const account_object& nathan = create_account("nathan", nathan_key.get_public_key() );
   const account_object& dan = create_account("dan", dan_key.get_public_key() );

   transfer(account_id_type()(db), nathan, asset(100000));
   transfer(account_id_type()(db), dan, asset(100000));

   {
      transfer_operation top;
      top.from = dan.get_id();
      top.to = nathan.get_id();
      top.amount = asset(500);

      proposal_create_operation pop;
      pop.proposed_ops.emplace_back(top);
      std::swap(top.from, top.to);
      pop.proposed_ops.emplace_back(top);

      pop.fee_paying_account = nathan.get_id();
      pop.expiration_time = db.head_block_time() + fc::days(1);
      trx.operations.push_back(pop);
      trx.sign(nathan_key);
      PUSH_TX( db, trx );
      trx.clear();
   }

   const proposal_object& prop = *db.get_index_type<proposal_index>().indices().begin();
   BOOST_CHECK(prop.required_active_approvals.size() == 2);
   BOOST_CHECK(prop.required_owner_approvals.size() == 0);
   BOOST_CHECK(!prop.is_authorized_to_execute(db));

   {
      proposal_id_type pid = prop.id;
      proposal_update_operation uop;
      uop.proposal = prop.id;
      uop.active_approvals_to_add.insert(nathan.get_id());
      uop.fee_paying_account = nathan.get_id();
      trx.operations.push_back(uop);
      trx.sign(nathan_key);
      PUSH_TX( db, trx );
      trx.clear();

      BOOST_CHECK(db.find_object(pid) != nullptr);
      BOOST_CHECK(!prop.is_authorized_to_execute(db));

      uop.active_approvals_to_add = {dan.get_id()};
      trx.operations.push_back(uop);
      trx.sign(nathan_key);
      GRAPHENE_REQUIRE_THROW(PUSH_TX( db, trx ), fc::exception);
      trx.sign(dan_key);
      PUSH_TX( db, trx );

      BOOST_CHECK(db.find_object(pid) == nullptr);
   }
} FC_LOG_AND_RETHROW() }

BOOST_FIXTURE_TEST_CASE( proposal_delete, database_fixture )
{ try {
   generate_block();

   auto nathan_key = generate_private_key("nathan");
   auto dan_key = generate_private_key("dan");
   const account_object& nathan = create_account("nathan", nathan_key.get_public_key() );
   const account_object& dan = create_account("dan", dan_key.get_public_key() );

   transfer(account_id_type()(db), nathan, asset(100000));
   transfer(account_id_type()(db), dan, asset(100000));

   {
      transfer_operation top;
      top.from = dan.get_id();
      top.to = nathan.get_id();
      top.amount = asset(500);

      proposal_create_operation pop;
      pop.proposed_ops.emplace_back(top);
      std::swap(top.from, top.to);
      top.amount = asset(6000);
      pop.proposed_ops.emplace_back(top);

      pop.fee_paying_account = nathan.get_id();
      pop.expiration_time = db.head_block_time() + fc::days(1);
      trx.operations.push_back(pop);
      trx.sign(nathan_key);
      PUSH_TX( db, trx );
      trx.clear();
   }

   const proposal_object& prop = *db.get_index_type<proposal_index>().indices().begin();
   BOOST_CHECK(prop.required_active_approvals.size() == 2);
   BOOST_CHECK(prop.required_owner_approvals.size() == 0);
   BOOST_CHECK(!prop.is_authorized_to_execute(db));

   {
      proposal_update_operation uop;
      uop.fee_paying_account = nathan.get_id();
      uop.proposal = prop.id;
      uop.active_approvals_to_add.insert(nathan.get_id());
      trx.operations.push_back(uop);
      trx.sign(nathan_key);
      PUSH_TX( db, trx );
      trx.clear();
      BOOST_CHECK(!prop.is_authorized_to_execute(db));
      BOOST_CHECK_EQUAL(prop.available_active_approvals.size(), 1);

      std::swap(uop.active_approvals_to_add, uop.active_approvals_to_remove);
      trx.operations.push_back(uop);
      trx.sign(nathan_key);
      PUSH_TX( db, trx );
      trx.clear();
      BOOST_CHECK(!prop.is_authorized_to_execute(db));
      BOOST_CHECK_EQUAL(prop.available_active_approvals.size(), 0);
   }

   {
      proposal_id_type pid = prop.id;
      proposal_delete_operation dop;
      dop.fee_paying_account = nathan.get_id();
      dop.proposal = pid;
      trx.operations.push_back(dop);
      trx.sign(nathan_key);
      PUSH_TX( db, trx );
      BOOST_CHECK(db.find_object(pid) == nullptr);
      BOOST_CHECK_EQUAL(get_balance(nathan, asset_id_type()(db)), 100000);
   }
} FC_LOG_AND_RETHROW() }

BOOST_FIXTURE_TEST_CASE( proposal_owner_authority_delete, database_fixture )
{ try {
   generate_block();

   auto nathan_key = generate_private_key("nathan");
   auto dan_key = generate_private_key("dan");
   const account_object& nathan = create_account("nathan", nathan_key.get_public_key() );
   const account_object& dan = create_account("dan", dan_key.get_public_key() );

   transfer(account_id_type()(db), nathan, asset(100000));
   transfer(account_id_type()(db), dan, asset(100000));

   {
      transfer_operation top;
      top.from = dan.get_id();
      top.to = nathan.get_id();
      top.amount = asset(500);

      account_update_operation uop;
      uop.account = nathan.get_id();
      uop.owner = authority(1, public_key_type(generate_private_key("nathan2").get_public_key()), 1);

      proposal_create_operation pop;
      pop.proposed_ops.emplace_back(top);
      pop.proposed_ops.emplace_back(uop);
      std::swap(top.from, top.to);
      top.amount = asset(6000);
      pop.proposed_ops.emplace_back(top);

      pop.fee_paying_account = nathan.get_id();
      pop.expiration_time = db.head_block_time() + fc::days(1);
      trx.operations.push_back(pop);
      trx.sign(nathan_key);
      PUSH_TX( db, trx );
      trx.clear();
   }

   const proposal_object& prop = *db.get_index_type<proposal_index>().indices().begin();
   BOOST_CHECK_EQUAL(prop.required_active_approvals.size(), 1);
   BOOST_CHECK_EQUAL(prop.required_owner_approvals.size(), 1);
   BOOST_CHECK(!prop.is_authorized_to_execute(db));

   {
      proposal_update_operation uop;
      uop.fee_paying_account = nathan.get_id();
      uop.proposal = prop.id;
      uop.owner_approvals_to_add.insert(nathan.get_id());
      trx.operations.push_back(uop);
      trx.sign(nathan_key);
      PUSH_TX( db, trx );
      trx.clear();
      BOOST_CHECK(!prop.is_authorized_to_execute(db));
      BOOST_CHECK_EQUAL(prop.available_owner_approvals.size(), 1);

      std::swap(uop.owner_approvals_to_add, uop.owner_approvals_to_remove);
      trx.operations.push_back(uop);
      trx.sign(nathan_key);
      PUSH_TX( db, trx );
      trx.clear();
      BOOST_CHECK(!prop.is_authorized_to_execute(db));
      BOOST_CHECK_EQUAL(prop.available_owner_approvals.size(), 0);
   }

   {
      proposal_id_type pid = prop.id;
      proposal_delete_operation dop;
      dop.fee_paying_account = nathan.get_id();
      dop.proposal = pid;
      dop.using_owner_authority = true;
      trx.operations.push_back(dop);
      trx.sign(nathan_key);
      PUSH_TX( db, trx );
      BOOST_CHECK(db.find_object(pid) == nullptr);
      BOOST_CHECK_EQUAL(get_balance(nathan, asset_id_type()(db)), 100000);
   }
} FC_LOG_AND_RETHROW() }

BOOST_FIXTURE_TEST_CASE( proposal_owner_authority_complete, database_fixture )
{ try {
   generate_block();

   auto nathan_key = generate_private_key("nathan");
   auto dan_key = generate_private_key("dan");
   const account_object& nathan = create_account("nathan", nathan_key.get_public_key() );
   const account_object& dan = create_account("dan", dan_key.get_public_key() );

   transfer(account_id_type()(db), nathan, asset(100000));
   transfer(account_id_type()(db), dan, asset(100000));

   {
      transfer_operation top;
      top.from = dan.get_id();
      top.to = nathan.get_id();
      top.amount = asset(500);

      account_update_operation uop;
      uop.account = nathan.get_id();
      uop.owner = authority(1, public_key_type(generate_private_key("nathan2").get_public_key()), 1);

      proposal_create_operation pop;
      pop.proposed_ops.emplace_back(top);
      pop.proposed_ops.emplace_back(uop);
      std::swap(top.from, top.to);
      top.amount = asset(6000);
      pop.proposed_ops.emplace_back(top);

      pop.fee_paying_account = nathan.get_id();
      pop.expiration_time = db.head_block_time() + fc::days(1);
      trx.operations.push_back(pop);
      trx.sign(nathan_key);
      PUSH_TX( db, trx );
      trx.clear();
   }

   const proposal_object& prop = *db.get_index_type<proposal_index>().indices().begin();
   BOOST_CHECK_EQUAL(prop.required_active_approvals.size(), 1);
   BOOST_CHECK_EQUAL(prop.required_owner_approvals.size(), 1);
   BOOST_CHECK(!prop.is_authorized_to_execute(db));

   {
      proposal_id_type pid = prop.id;
      proposal_update_operation uop;
      uop.fee_paying_account = nathan.get_id();
      uop.proposal = prop.id;
      uop.key_approvals_to_add.insert(dan.active.key_auths.begin()->first);
      trx.operations.push_back(uop);
      trx.sign(nathan_key);
      trx.sign(dan_key);
      PUSH_TX( db, trx );
      trx.clear();
      BOOST_CHECK(!prop.is_authorized_to_execute(db));
      BOOST_CHECK_EQUAL(prop.available_key_approvals.size(), 1);

      std::swap(uop.key_approvals_to_add, uop.key_approvals_to_remove);
      trx.operations.push_back(uop);
      trx.sign(nathan_key);
      trx.sign(dan_key);
      PUSH_TX( db, trx );
      trx.clear();
      BOOST_CHECK(!prop.is_authorized_to_execute(db));
      BOOST_CHECK_EQUAL(prop.available_key_approvals.size(), 0);

      std::swap(uop.key_approvals_to_add, uop.key_approvals_to_remove);
      // Survive trx dupe check
      trx.set_expiration(db.head_block_id(), 5);
      trx.operations.push_back(uop);
      trx.sign(nathan_key);
      trx.sign(dan_key);
      PUSH_TX( db, trx );
      trx.clear();
      BOOST_CHECK(!prop.is_authorized_to_execute(db));
      BOOST_CHECK_EQUAL(prop.available_key_approvals.size(), 1);

      uop.key_approvals_to_add.clear();
      uop.owner_approvals_to_add.insert(nathan.get_id());
      trx.operations.push_back(uop);
      trx.sign(nathan_key);
      PUSH_TX( db, trx );
      trx.clear();
      BOOST_CHECK(db.find_object(pid) == nullptr);
   }
} FC_LOG_AND_RETHROW() }

BOOST_FIXTURE_TEST_CASE( max_authority_membership, database_fixture )
{
   try
   {
      //Get a sane head block time
      generate_block();

      db.modify(db.get_global_properties(), [](global_property_object& p) {
         p.parameters.committee_proposal_review_period = fc::hours(1).to_seconds();
      });

      transaction tx;
      processed_transaction ptx;

      private_key_type genesis_key = delegate_priv_key;
      // Sam is the creator of accounts
      private_key_type sam_key = generate_private_key("sam");

      account_object sam_account_object = create_account( "sam", sam_key );
      upgrade_to_lifetime_member(sam_account_object);
      account_object genesis_account_object = genesis_account(db);

      const asset_object& core = asset_id_type()(db);

      transfer(genesis_account_object, sam_account_object, core.amount(100000));

      // have Sam create some keys

      int keys_to_create = 2*GRAPHENE_DEFAULT_MAX_AUTHORITY_MEMBERSHIP;
      vector<private_key_type> private_keys;

      tx = transaction();
      private_keys.reserve( keys_to_create );
      for( int i=0; i<keys_to_create; i++ )
      {
         string seed = "this_is_a_key_" + std::to_string(i);
         private_key_type privkey = generate_private_key( seed );
         private_keys.push_back( privkey );
      }
      ptx = PUSH_TX( db, tx, ~0 );

      vector<public_key_type> key_ids;

      key_ids.reserve( keys_to_create );
      for( int i=0; i<keys_to_create; i++ )
          key_ids.push_back( private_keys[i].get_public_key() );

      // now try registering accounts with n keys, 0 < n < 20

      // TODO:  Make sure it throws / accepts properly when
      //   max_account_authority is changed in global parameteres

      for( int num_keys=1; num_keys<=keys_to_create; num_keys++ )
      {
         // try registering account with n keys

         authority test_authority;
         test_authority.weight_threshold = num_keys;

         for( int i=0; i<num_keys; i++ )
            test_authority.key_auths[ key_ids[i] ] = 1;

         auto check_tx = [&]( const authority& owner_auth,
                              const authority& active_auth )
         {
             const uint16_t max_authority_membership = GRAPHENE_DEFAULT_MAX_AUTHORITY_MEMBERSHIP;
             account_create_operation anon_create_op;
             transaction tx;

             anon_create_op.owner = owner_auth;
             anon_create_op.active = active_auth;
             anon_create_op.registrar = sam_account_object.id;
             anon_create_op.options.memo_key = sam_account_object.options.memo_key;
             anon_create_op.name = generate_anon_acct_name();

             tx.operations.push_back( anon_create_op );

             if( num_keys > max_authority_membership )
             {
                GRAPHENE_REQUIRE_THROW(PUSH_TX( db, tx, ~0 ), fc::exception);
             }
             else
             {
                PUSH_TX( db, tx, ~0 );
             }
             return;
         };

         check_tx( sam_account_object.owner, test_authority  );
         check_tx( test_authority, sam_account_object.active );
         check_tx( test_authority, test_authority );
      }
   }
   FC_LOG_AND_RETHROW()
}

BOOST_FIXTURE_TEST_CASE( bogus_signature, database_fixture )
{
   try
   {
      private_key_type genesis_key = delegate_priv_key;
      // Sam is the creator of accounts
      private_key_type alice_key = generate_private_key("alice");
      private_key_type bob_key = generate_private_key("bob");
      private_key_type charlie_key = generate_private_key("charlie");

      account_object genesis_account_object = genesis_account(db);
      account_object alice_account_object = create_account( "alice", alice_key );
      account_object bob_account_object = create_account( "bob", bob_key );
      account_object charlie_account_object = create_account( "charlie", charlie_key );

      // unneeded, comment it out to silence compiler warning
      //key_id_type bob_key_id = bob_account_object.memo_key;

      uint32_t skip = database::skip_transaction_dupe_check;

      // send from Sam -> Alice, signed by Sam

      const asset_object& core = asset_id_type()(db);
      transfer(genesis_account_object, alice_account_object, core.amount(100000));

      operation xfer_op = transfer_operation(
         {core.amount(0),
          alice_account_object.id,
          bob_account_object.id,
          core.amount( 5000 ),
          memo_data() });
      xfer_op.visit( operation_set_fee( db.current_fee_schedule() ) );

      trx.clear();
      trx.operations.push_back( xfer_op );

      BOOST_TEST_MESSAGE( "Transfer signed by alice" );
      trx.sign(alice_key );

      flat_set<account_id_type> active_set, owner_set;
      vector<authority> others;
      trx.get_required_authorities( active_set, owner_set, others );

      PUSH_TX( db,  trx, skip  );

      trx.operations.push_back( xfer_op );
      BOOST_TEST_MESSAGE( "Invalidating Alices Signature" );
      // Alice's signature is now invalid
      GRAPHENE_REQUIRE_THROW( PUSH_TX( db,  trx, skip  ), fc::exception );
      // Re-sign, now OK (sig is replaced)
      BOOST_TEST_MESSAGE( "Resign with Alice's Signature" );
      trx.signatures.clear();
      trx.sign( alice_key );
      PUSH_TX( db,  trx, skip  );

      trx.signatures.clear();
      trx.operations.pop_back();
      trx.sign( alice_key );
      trx.sign( charlie_key );
      // Signed by third-party Charlie (irrelevant key, not in authority)
      GRAPHENE_REQUIRE_THROW( PUSH_TX( db,  trx, skip  ), fc::exception );
   }
   FC_LOG_AND_RETHROW()
}

BOOST_FIXTURE_TEST_CASE( voting_account, database_fixture )
{ try {
   ACTORS((nathan)(vikram));
   upgrade_to_lifetime_member(nathan_id);
   upgrade_to_lifetime_member(vikram_id);
   delegate_id_type nathan_delegate = create_delegate(nathan_id(db)).id;
   delegate_id_type vikram_delegate = create_delegate(vikram_id(db)).id;

   //wdump((db.get_balance(account_id_type(), asset_id_type())));
   generate_block();

   //wdump((db.get_balance(account_id_type(), asset_id_type())));
   transfer(account_id_type(), nathan_id, asset(1000000));
   transfer(account_id_type(), vikram_id, asset(100));

   {
      account_update_operation op;
      op.account = nathan_id;
      op.new_options = nathan_id(db).options;
      op.new_options->voting_account = vikram_id;
      op.new_options->votes = flat_set<vote_id_type>{nathan_delegate(db).vote_id};
      op.new_options->num_committee = 1;
      trx.operations.push_back(op);
      trx.sign(nathan_private_key);
      PUSH_TX( db, trx );
      trx.clear();
   }
   {
      account_update_operation op;
      op.account = vikram_id;
      op.new_options = vikram_id(db).options;
      op.new_options->votes.insert(vikram_delegate(db).vote_id);
      op.new_options->num_committee = 11;
      trx.operations.push_back(op);
      trx.sign(vikram_private_key);
      // Fails because num_committee is larger than the cardinality of committee members being voted for
      GRAPHENE_CHECK_THROW(PUSH_TX( db, trx ), fc::exception);
      op.new_options->num_committee = 3;
      trx.operations = {op};
      trx.signatures.clear();
      trx.sign(vikram_private_key);
      PUSH_TX( db, trx );
      trx.clear();
   }

   generate_blocks(db.get_dynamic_global_properties().next_maintenance_time + GRAPHENE_DEFAULT_BLOCK_INTERVAL);
   BOOST_CHECK(std::find(db.get_global_properties().active_delegates.begin(),
                         db.get_global_properties().active_delegates.end(),
                         nathan_delegate) == db.get_global_properties().active_delegates.end());
   BOOST_CHECK(std::find(db.get_global_properties().active_delegates.begin(),
                         db.get_global_properties().active_delegates.end(),
                         vikram_delegate) != db.get_global_properties().active_delegates.end());
} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_SUITE_END()
