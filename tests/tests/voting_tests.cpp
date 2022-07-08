/*
 * Copyright (c) 2018 oxarbitrage, and contributors.
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

#include <graphene/app/database_api.hpp>
#include <graphene/chain/exceptions.hpp>
#include <graphene/chain/hardfork.hpp>

#include <iostream>

#include "../common/database_fixture.hpp"

using namespace graphene::chain;
using namespace graphene::chain::test;


BOOST_FIXTURE_TEST_SUITE(voting_tests, database_fixture)

BOOST_FIXTURE_TEST_CASE( committee_account_initialization_test, database_fixture )
{ try {
   // Check current default committee
   // By default chain is configured with INITIAL_COMMITTEE_MEMBER_COUNT=9 members
   const auto &committee_members = db.get_global_properties().active_committee_members;
   const auto &committee = committee_account(db);

   BOOST_CHECK_EQUAL(committee_members.size(), INITIAL_COMMITTEE_MEMBER_COUNT);
   BOOST_CHECK_EQUAL(committee.active.num_auths(), INITIAL_COMMITTEE_MEMBER_COUNT);

   generate_blocks(HARDFORK_533_TIME);
   generate_blocks(db.get_dynamic_global_properties().next_maintenance_time);
   generate_block();
   set_expiration(db, trx);

   // Check that committee not changed after 533 hardfork
   // vote counting method changed, but any votes are absent
   const auto &committee_members_after_hf533 = db.get_global_properties().active_committee_members;
   const auto &committee_after_hf533 = committee_account(db);
   BOOST_CHECK_EQUAL(committee_members_after_hf533.size(), INITIAL_COMMITTEE_MEMBER_COUNT);
   BOOST_CHECK_EQUAL(committee_after_hf533.active.num_auths(), INITIAL_COMMITTEE_MEMBER_COUNT);

   // You can't use uninitialized committee after 533 hardfork
   // when any user with stake created (create_account method automatically set up votes for committee)
   // committee is incomplete and consist of random active members
   ACTOR(alice);
   fund(alice);
   generate_blocks(db.get_dynamic_global_properties().next_maintenance_time);

   const auto &committee_after_hf533_with_stake = committee_account(db);
   BOOST_CHECK_LT(committee_after_hf533_with_stake.active.num_auths(), INITIAL_COMMITTEE_MEMBER_COUNT);

   // Initialize committee by voting for each memeber and for desired count
   vote_for_committee_and_witnesses(INITIAL_COMMITTEE_MEMBER_COUNT, INITIAL_WITNESS_COUNT);
   generate_blocks(db.get_dynamic_global_properties().next_maintenance_time);

   const auto &committee_members_after_hf533_and_init = db.get_global_properties().active_committee_members;
   const auto &committee_after_hf533_and_init = committee_account(db);
   BOOST_CHECK_EQUAL(committee_members_after_hf533_and_init.size(), INITIAL_COMMITTEE_MEMBER_COUNT);
   BOOST_CHECK_EQUAL(committee_after_hf533_and_init.active.num_auths(), INITIAL_COMMITTEE_MEMBER_COUNT);

} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE(put_my_witnesses)
{
   try
   {
      ACTORS( (witness0)
              (witness1)
              (witness2)
              (witness3)
              (witness4)
              (witness5)
              (witness6)
              (witness7)
              (witness8)
              (witness9)
              (witness10)
              (witness11)
              (witness12)
              (witness13) );

      // Upgrade all accounts to LTM
      upgrade_to_lifetime_member(witness0_id);
      upgrade_to_lifetime_member(witness1_id);
      upgrade_to_lifetime_member(witness2_id);
      upgrade_to_lifetime_member(witness3_id);
      upgrade_to_lifetime_member(witness4_id);
      upgrade_to_lifetime_member(witness5_id);
      upgrade_to_lifetime_member(witness6_id);
      upgrade_to_lifetime_member(witness7_id);
      upgrade_to_lifetime_member(witness8_id);
      upgrade_to_lifetime_member(witness9_id);
      upgrade_to_lifetime_member(witness10_id);
      upgrade_to_lifetime_member(witness11_id);
      upgrade_to_lifetime_member(witness12_id);
      upgrade_to_lifetime_member(witness13_id);

      // Create all the witnesses
      const witness_id_type witness0_witness_id = create_witness(witness0_id, witness0_private_key).id;
      const witness_id_type witness1_witness_id = create_witness(witness1_id, witness1_private_key).id;
      const witness_id_type witness2_witness_id = create_witness(witness2_id, witness2_private_key).id;
      const witness_id_type witness3_witness_id = create_witness(witness3_id, witness3_private_key).id;
      const witness_id_type witness4_witness_id = create_witness(witness4_id, witness4_private_key).id;
      const witness_id_type witness5_witness_id = create_witness(witness5_id, witness5_private_key).id;
      const witness_id_type witness6_witness_id = create_witness(witness6_id, witness6_private_key).id;
      const witness_id_type witness7_witness_id = create_witness(witness7_id, witness7_private_key).id;
      const witness_id_type witness8_witness_id = create_witness(witness8_id, witness8_private_key).id;
      const witness_id_type witness9_witness_id = create_witness(witness9_id, witness9_private_key).id;
      const witness_id_type witness10_witness_id = create_witness(witness10_id, witness10_private_key).id;
      const witness_id_type witness11_witness_id = create_witness(witness11_id, witness11_private_key).id;
      const witness_id_type witness12_witness_id = create_witness(witness12_id, witness12_private_key).id;
      const witness_id_type witness13_witness_id = create_witness(witness13_id, witness13_private_key).id;

      // Create a vector with private key of all witnesses, will be used to activate 9 witnesses at a time
      const vector <fc::ecc::private_key> private_keys = {
            witness0_private_key,
            witness1_private_key,
            witness2_private_key,
            witness3_private_key,
            witness4_private_key,
            witness5_private_key,
            witness6_private_key,
            witness7_private_key,
            witness8_private_key,
            witness9_private_key,
            witness10_private_key,
            witness11_private_key,
            witness12_private_key,
            witness13_private_key

      };

      // create a map with account id and witness id
      const flat_map <account_id_type, witness_id_type> witness_map = {
            {witness0_id, witness0_witness_id},
            {witness1_id, witness1_witness_id},
            {witness2_id, witness2_witness_id},
            {witness3_id, witness3_witness_id},
            {witness4_id, witness4_witness_id},
            {witness5_id, witness5_witness_id},
            {witness6_id, witness6_witness_id},
            {witness7_id, witness7_witness_id},
            {witness8_id, witness8_witness_id},
            {witness9_id, witness9_witness_id},
            {witness10_id, witness10_witness_id},
            {witness11_id, witness11_witness_id},
            {witness12_id, witness12_witness_id},
            {witness13_id, witness13_witness_id}
      };

      // Check current default witnesses, default chain is configured with 9 witnesses
      auto witnesses = db.get_global_properties().active_witnesses;
      BOOST_CHECK_EQUAL(witnesses.size(), INITIAL_WITNESS_COUNT);
      BOOST_CHECK_EQUAL(witnesses.begin()[0].instance.value, 1u);
      BOOST_CHECK_EQUAL(witnesses.begin()[1].instance.value, 2u);
      BOOST_CHECK_EQUAL(witnesses.begin()[2].instance.value, 3u);
      BOOST_CHECK_EQUAL(witnesses.begin()[3].instance.value, 4u);
      BOOST_CHECK_EQUAL(witnesses.begin()[4].instance.value, 5u);
      BOOST_CHECK_EQUAL(witnesses.begin()[5].instance.value, 6u);
      BOOST_CHECK_EQUAL(witnesses.begin()[6].instance.value, 7u);
      BOOST_CHECK_EQUAL(witnesses.begin()[7].instance.value, 8u);
      BOOST_CHECK_EQUAL(witnesses.begin()[8].instance.value, 9u);

      // Activate all witnesses
      // Each witness is voted with incremental stake so last witness created will be the ones with more votes
      int c = 0;
      for (auto l : witness_map) {
         int stake = 100 + c + 10;
         transfer(committee_account, l.first, asset(stake));
         {
            set_expiration(db, trx);
            account_update_operation op;
            op.account = l.first;
            op.new_options = l.first(db).options;
            op.new_options->votes.insert(l.second(db).vote_id);

            trx.operations.push_back(op);
            sign(trx, private_keys.at(c));
            PUSH_TX(db, trx);
            trx.clear();
         }
         ++c;
      }

      // Trigger the new witnesses
      generate_blocks(db.get_dynamic_global_properties().next_maintenance_time);
      generate_block();

      // Check my witnesses are now in control of the system
      witnesses = db.get_global_properties().active_witnesses;
      BOOST_CHECK_EQUAL(witnesses.size(), INITIAL_WITNESS_COUNT);
      BOOST_CHECK_EQUAL(witnesses.begin()[0].instance.value, 16u);
      BOOST_CHECK_EQUAL(witnesses.begin()[1].instance.value, 17u);
      BOOST_CHECK_EQUAL(witnesses.begin()[2].instance.value, 18u);
      BOOST_CHECK_EQUAL(witnesses.begin()[3].instance.value, 19u);
      BOOST_CHECK_EQUAL(witnesses.begin()[4].instance.value, 20u);
      BOOST_CHECK_EQUAL(witnesses.begin()[5].instance.value, 21u);
      BOOST_CHECK_EQUAL(witnesses.begin()[6].instance.value, 22u);
      BOOST_CHECK_EQUAL(witnesses.begin()[7].instance.value, 23u);
      BOOST_CHECK_EQUAL(witnesses.begin()[8].instance.value, 24u);

   } FC_LOG_AND_RETHROW()
}

BOOST_AUTO_TEST_CASE(track_votes_witnesses_enabled)
{
   try
   {
      graphene::app::database_api db_api1(db);

      INVOKE(put_my_witnesses);

      const account_id_type witness1_id= get_account("witness1").id;
      auto witness1_object = db_api1.get_witness_by_account(witness1_id(db).name);
      BOOST_CHECK_EQUAL(witness1_object->total_votes, 111u);

   } FC_LOG_AND_RETHROW()
}

BOOST_AUTO_TEST_CASE(track_votes_witnesses_disabled)
{
   try
   {
      graphene::app::database_api db_api1(db);

      INVOKE(put_my_witnesses);

      const account_id_type witness1_id= get_account("witness1").id;
      auto witness1_object = db_api1.get_witness_by_account(witness1_id(db).name);
      BOOST_CHECK_EQUAL(witness1_object->total_votes, 0u);

   } FC_LOG_AND_RETHROW()
}

BOOST_AUTO_TEST_CASE(put_my_committee_members)
{
   try
   {
      ACTORS( (committee0)
              (committee1)
              (committee2)
              (committee3)
              (committee4)
              (committee5)
              (committee6)
              (committee7)
              (committee8)
              (committee9)
              (committee10)
              (committee11)
              (committee12)
              (committee13) );

      // Upgrade all accounts to LTM
      upgrade_to_lifetime_member(committee0_id);
      upgrade_to_lifetime_member(committee1_id);
      upgrade_to_lifetime_member(committee2_id);
      upgrade_to_lifetime_member(committee3_id);
      upgrade_to_lifetime_member(committee4_id);
      upgrade_to_lifetime_member(committee5_id);
      upgrade_to_lifetime_member(committee6_id);
      upgrade_to_lifetime_member(committee7_id);
      upgrade_to_lifetime_member(committee8_id);
      upgrade_to_lifetime_member(committee9_id);
      upgrade_to_lifetime_member(committee10_id);
      upgrade_to_lifetime_member(committee11_id);
      upgrade_to_lifetime_member(committee12_id);
      upgrade_to_lifetime_member(committee13_id);

      // Create all the committee
      const committee_member_id_type committee0_committee_id = create_committee_member(committee0_id(db)).id;
      const committee_member_id_type committee1_committee_id = create_committee_member(committee1_id(db)).id;
      const committee_member_id_type committee2_committee_id = create_committee_member(committee2_id(db)).id;
      const committee_member_id_type committee3_committee_id = create_committee_member(committee3_id(db)).id;
      const committee_member_id_type committee4_committee_id = create_committee_member(committee4_id(db)).id;
      const committee_member_id_type committee5_committee_id = create_committee_member(committee5_id(db)).id;
      const committee_member_id_type committee6_committee_id = create_committee_member(committee6_id(db)).id;
      const committee_member_id_type committee7_committee_id = create_committee_member(committee7_id(db)).id;
      const committee_member_id_type committee8_committee_id = create_committee_member(committee8_id(db)).id;
      const committee_member_id_type committee9_committee_id = create_committee_member(committee9_id(db)).id;
      const committee_member_id_type committee10_committee_id = create_committee_member(committee10_id(db)).id;
      const committee_member_id_type committee11_committee_id = create_committee_member(committee11_id(db)).id;
      const committee_member_id_type committee12_committee_id = create_committee_member(committee12_id(db)).id;
      const committee_member_id_type committee13_committee_id = create_committee_member(committee13_id(db)).id;

      // Create a vector with private key of all committee members, will be used to activate 9 members at a time
      const vector <fc::ecc::private_key> private_keys = {
            committee0_private_key,
            committee1_private_key,
            committee2_private_key,
            committee3_private_key,
            committee4_private_key,
            committee5_private_key,
            committee6_private_key,
            committee7_private_key,
            committee8_private_key,
            committee9_private_key,
            committee10_private_key,
            committee11_private_key,
            committee12_private_key,
            committee13_private_key
      };

      // create a map with account id and committee member id
      const flat_map <account_id_type, committee_member_id_type> committee_map = {
            {committee0_id, committee0_committee_id},
            {committee1_id, committee1_committee_id},
            {committee2_id, committee2_committee_id},
            {committee3_id, committee3_committee_id},
            {committee4_id, committee4_committee_id},
            {committee5_id, committee5_committee_id},
            {committee6_id, committee6_committee_id},
            {committee7_id, committee7_committee_id},
            {committee8_id, committee8_committee_id},
            {committee9_id, committee9_committee_id},
            {committee10_id, committee10_committee_id},
            {committee11_id, committee11_committee_id},
            {committee12_id, committee12_committee_id},
            {committee13_id, committee13_committee_id}
      };

      // Check current default committee, default chain is configured with 9 committee members
      auto committee_members = db.get_global_properties().active_committee_members;

      BOOST_CHECK_EQUAL(committee_members.size(), INITIAL_COMMITTEE_MEMBER_COUNT);
      BOOST_CHECK_EQUAL(committee_members.begin()[0].instance.value, 0u);
      BOOST_CHECK_EQUAL(committee_members.begin()[1].instance.value, 1u);
      BOOST_CHECK_EQUAL(committee_members.begin()[2].instance.value, 2u);
      BOOST_CHECK_EQUAL(committee_members.begin()[3].instance.value, 3u);
      BOOST_CHECK_EQUAL(committee_members.begin()[4].instance.value, 4u);
      BOOST_CHECK_EQUAL(committee_members.begin()[5].instance.value, 5u);
      BOOST_CHECK_EQUAL(committee_members.begin()[6].instance.value, 6u);
      BOOST_CHECK_EQUAL(committee_members.begin()[7].instance.value, 7u);
      BOOST_CHECK_EQUAL(committee_members.begin()[8].instance.value, 8u);

      // Activate all committee
      // Each committee is voted with incremental stake so last member created will be the ones with more votes
      int c = 0;
      for (auto committee : committee_map) {
         int stake = 100 + c + 10;
         transfer(committee_account, committee.first, asset(stake));
         {
            set_expiration(db, trx);
            account_update_operation op;
            op.account = committee.first;
            op.new_options = committee.first(db).options;

            op.new_options->votes.clear();
            op.new_options->votes.insert(committee.second(db).vote_id);
            op.new_options->num_committee = 1;

            trx.operations.push_back(op);
            sign(trx, private_keys.at(c));
            PUSH_TX(db, trx);
            trx.clear();
         }
         ++c;
      }

      // Trigger the new committee
      generate_blocks(db.get_dynamic_global_properties().next_maintenance_time);
      generate_block();

      // Check my witnesses are now in control of the system
      committee_members = db.get_global_properties().active_committee_members;
      std::sort(committee_members.begin(), committee_members.end());

      BOOST_CHECK_EQUAL(committee_members.size(), INITIAL_COMMITTEE_MEMBER_COUNT);

      // Check my committee members are now in control of the system
      BOOST_CHECK_EQUAL(committee_members.begin()[0].instance.value, 15);
      BOOST_CHECK_EQUAL(committee_members.begin()[1].instance.value, 16);
      BOOST_CHECK_EQUAL(committee_members.begin()[2].instance.value, 17);
      BOOST_CHECK_EQUAL(committee_members.begin()[3].instance.value, 18);
      BOOST_CHECK_EQUAL(committee_members.begin()[4].instance.value, 19);
      BOOST_CHECK_EQUAL(committee_members.begin()[5].instance.value, 20);
      BOOST_CHECK_EQUAL(committee_members.begin()[6].instance.value, 21);
      BOOST_CHECK_EQUAL(committee_members.begin()[7].instance.value, 22);
      BOOST_CHECK_EQUAL(committee_members.begin()[8].instance.value, 23);

   } FC_LOG_AND_RETHROW()
}

BOOST_AUTO_TEST_CASE(track_votes_committee_enabled)
{
   try
   {
      graphene::app::database_api db_api1(db);

      INVOKE(put_my_committee_members);

      const account_id_type committee1_id= get_account("committee1").id;
      auto committee1_object = db_api1.get_committee_member_by_account(committee1_id(db).name);
      BOOST_CHECK_EQUAL(committee1_object->total_votes, 111u);

   } FC_LOG_AND_RETHROW()
}

BOOST_AUTO_TEST_CASE(track_votes_committee_disabled)
{
   try
   {
      graphene::app::database_api db_api1(db);

      INVOKE(put_my_committee_members);

      const account_id_type committee1_id= get_account("committee1").id;
      auto committee1_object = db_api1.get_committee_member_by_account(committee1_id(db).name);
      BOOST_CHECK_EQUAL(committee1_object->total_votes, 0u);

   } FC_LOG_AND_RETHROW()
}

BOOST_AUTO_TEST_CASE(invalid_voting_account)
{
   try
   {
      ACTORS((alice));

      account_id_type invalid_account_id( (uint64_t)999999 );

      BOOST_CHECK( !db.find( invalid_account_id ) );

      graphene::chain::account_update_operation op;
      op.account = alice_id;
      op.new_options = alice.options;
      op.new_options->voting_account = invalid_account_id;
      trx.operations.push_back(op);
      sign(trx, alice_private_key);

      GRAPHENE_REQUIRE_THROW( PUSH_TX( db, trx, ~0 ), fc::exception );

   } FC_LOG_AND_RETHROW()
}
BOOST_AUTO_TEST_CASE(last_voting_date)
{
   try
   {
      ACTORS((alice));

      transfer(committee_account, alice_id, asset(100));

      // we are going to vote for this witness
      auto witness1 = witness_id_type(1)(db);

      auto stats_obj = db.get_account_stats_by_owner(alice_id);
      BOOST_CHECK_EQUAL(stats_obj.last_vote_time.sec_since_epoch(), 0u);

      // alice votes
      graphene::chain::account_update_operation op;
      op.account = alice_id;
      op.new_options = alice.options;
      op.new_options->votes.insert(witness1.vote_id);
      trx.operations.push_back(op);
      sign(trx, alice_private_key);
      PUSH_TX( db, trx, ~0 );

      auto now = db.head_block_time().sec_since_epoch();

      // last_vote_time is updated for alice
      stats_obj = db.get_account_stats_by_owner(alice_id);
      BOOST_CHECK_EQUAL(stats_obj.last_vote_time.sec_since_epoch(), now);

   } FC_LOG_AND_RETHROW()
}
BOOST_AUTO_TEST_CASE(last_voting_date_proxy)
{
   try
   {
      ACTORS((alice)(proxy)(bob));

      transfer(committee_account, alice_id, asset(100));
      transfer(committee_account, bob_id, asset(200));
      transfer(committee_account, proxy_id, asset(300));

      generate_block();

      // witness to vote for
      auto witness1 = witness_id_type(1)(db);

      // round1: alice changes proxy, this is voting activity
      {
         graphene::chain::account_update_operation op;
         op.account = alice_id;
         op.new_options = alice_id(db).options;
         op.new_options->voting_account = proxy_id;
         trx.operations.push_back(op);
         sign(trx, alice_private_key);
         PUSH_TX( db, trx, ~0 );
      }
      // alice last_vote_time is updated
      auto alice_stats_obj = db.get_account_stats_by_owner(alice_id);
      auto round1 = db.head_block_time().sec_since_epoch();
      BOOST_CHECK_EQUAL(alice_stats_obj.last_vote_time.sec_since_epoch(), round1);

      generate_block();

      // round 2: alice update account but no proxy or voting changes are done
      {
         graphene::chain::account_update_operation op;
         op.account = alice_id;
         op.new_options = alice_id(db).options;
         trx.operations.push_back(op);
         sign(trx, alice_private_key);
         set_expiration( db, trx );
         PUSH_TX( db, trx, ~0 );
      }
      // last_vote_time is not updated
      alice_stats_obj = db.get_account_stats_by_owner(alice_id);
      BOOST_CHECK_EQUAL(alice_stats_obj.last_vote_time.sec_since_epoch(), round1);

      generate_block();

      // round 3: bob votes
      {
         graphene::chain::account_update_operation op;
         op.account = bob_id;
         op.new_options = bob_id(db).options;
         op.new_options->votes.insert(witness1.vote_id);
         trx.operations.push_back(op);
         sign(trx, bob_private_key);
         set_expiration( db, trx );
         PUSH_TX(db, trx, ~0);
      }

      // last_vote_time for bob is updated as he voted
      auto round3 = db.head_block_time().sec_since_epoch();
      auto bob_stats_obj = db.get_account_stats_by_owner(bob_id);
      BOOST_CHECK_EQUAL(bob_stats_obj.last_vote_time.sec_since_epoch(), round3);

      generate_block();

      // round 4: proxy votes
      {
         graphene::chain::account_update_operation op;
         op.account = proxy_id;
         op.new_options = proxy_id(db).options;
         op.new_options->votes.insert(witness1.vote_id);
         trx.operations.push_back(op);
         sign(trx, proxy_private_key);
         PUSH_TX(db, trx, ~0);
      }

      // proxy just voted so the last_vote_time is updated
      auto round4 = db.head_block_time().sec_since_epoch();
      auto proxy_stats_obj = db.get_account_stats_by_owner(proxy_id);
      BOOST_CHECK_EQUAL(proxy_stats_obj.last_vote_time.sec_since_epoch(), round4);

      // alice haves proxy, proxy votes but last_vote_time is not updated for alice
      alice_stats_obj = db.get_account_stats_by_owner(alice_id);
      BOOST_CHECK_EQUAL(alice_stats_obj.last_vote_time.sec_since_epoch(), round1);

      // bob haves nothing to do with proxy so last_vote_time is not updated
      bob_stats_obj = db.get_account_stats_by_owner(bob_id);
      BOOST_CHECK_EQUAL(bob_stats_obj.last_vote_time.sec_since_epoch(), round3);

   } FC_LOG_AND_RETHROW()
}

BOOST_AUTO_TEST_CASE( witness_votes_calculation )
{
   try
   {
      auto original_wits = db.get_global_properties().active_witnesses;

      INVOKE( put_my_witnesses );

      GET_ACTOR( witness0 );
      GET_ACTOR( witness1 );
      GET_ACTOR( witness2 );
      GET_ACTOR( witness3 );
      GET_ACTOR( witness4 );
      GET_ACTOR( witness5 );
      GET_ACTOR( witness6 );
      GET_ACTOR( witness7 );
      GET_ACTOR( witness8 );
      GET_ACTOR( witness9 );
      GET_ACTOR( witness10 );
      GET_ACTOR( witness11 );
      GET_ACTOR( witness12 );
      GET_ACTOR( witness13 );

      graphene::app::database_api db_api1(db);

      vector<account_id_type> wit_account_ids = { witness0_id, witness1_id, witness2_id, witness3_id,
                                                  witness4_id, witness5_id, witness6_id, witness7_id,
                                                  witness8_id, witness9_id, witness10_id, witness11_id,
                                                  witness12_id, witness13_id };
      vector<witness_id_type> wit_ids;
      size_t total = wit_account_ids.size();

      for( size_t i = 0; i < total; ++i )
      {
         auto wit_object = db_api1.get_witness_by_account( wit_account_ids[i](db).name );
         BOOST_REQUIRE( wit_object.valid() );
         wit_ids.push_back( wit_object->id );
      }

      generate_blocks( HARDFORK_CORE_2103_TIME - 750 * 86400 );
      set_expiration( db, trx );

      // refresh last_vote_time
      for( size_t i = 0; i < total; ++i )
      {
         account_id_type voter = wit_account_ids[ total - i - 1 ];

         account_update_operation op;
         op.account = voter;
         op.new_options = op.account(db).options;
         op.new_options->voting_account = account_id_type();

         trx.operations.clear();
         trx.operations.push_back(op);
         PUSH_TX(db, trx, ~0);

         op.new_options->voting_account = GRAPHENE_PROXY_TO_SELF_ACCOUNT;
         trx.operations.clear();
         trx.operations.push_back(op);
         PUSH_TX(db, trx, ~0);

         trx.clear();

         generate_blocks( db.head_block_time() + 45 * 86400 );
         set_expiration( db, trx );
      }

      for( size_t i = 0; i < total; ++i )
      {
         BOOST_CHECK_EQUAL( wit_ids[i](db).total_votes, 110u + i );
      }

      generate_blocks( HARDFORK_CORE_2103_TIME );
      generate_blocks( db.get_dynamic_global_properties().next_maintenance_time );
      set_expiration( db, trx );

      uint64_t expected_votes[14];

      expected_votes[0] = 110; // 750 - 45 * 13 = 165 days
      expected_votes[1] = 111; // 210 days
      expected_votes[2] = 112; // 255 days
      expected_votes[3] = 113; // 300 days
      expected_votes[4] = 114; // 345 days
      expected_votes[5] = 115 - 115 / 8; // 390 days
      expected_votes[6] = 116 - 116 * 2 / 8; // 435 days
      expected_votes[7] = 117 - 117 * 3 / 8; // 480 days
      expected_votes[8] = 118 - 118 * 4 / 8; // 525 days
      expected_votes[9] = 119 - 119 * 5 / 8; // 570 days
      expected_votes[10] = 120 - 120 * 6 / 8; // 615 days
      expected_votes[11] = 121 - 121 * 7 / 8; // 660 days
      expected_votes[12] = 0; // 705 days
      expected_votes[13] = 0; // 750 days

      for( size_t i = 0; i < total; ++i )
      {
         BOOST_CHECK_EQUAL( wit_ids[i](db).total_votes, expected_votes[i] );
      }

      flat_set<witness_id_type> expected_active_witnesses = { wit_ids[0], wit_ids[1], wit_ids[2],
                                                              wit_ids[3], wit_ids[4], wit_ids[5],
                                                              wit_ids[6], wit_ids[7], wit_ids[8] };
      BOOST_CHECK( db.get_global_properties().active_witnesses == expected_active_witnesses );

      // new vote
      {
         account_update_operation op;
         op.account = wit_account_ids[12];
         op.new_options = op.account(db).options;
         op.new_options->votes.insert( wit_ids[8](db).vote_id );

         trx.operations.clear();
         trx.operations.push_back(op);
         PUSH_TX(db, trx, ~0);
      }

      generate_blocks( db.get_dynamic_global_properties().next_maintenance_time );

      expected_votes[8] += 122;
      expected_votes[12] = 122;
      for( size_t i = 0; i < total; ++i )
      {
         BOOST_CHECK_EQUAL( wit_ids[i](db).total_votes, expected_votes[i] );
      }

      expected_active_witnesses = { wit_ids[0], wit_ids[1], wit_ids[2],
                                    wit_ids[3], wit_ids[4], wit_ids[5],
                                    wit_ids[6], wit_ids[8], wit_ids[12] };
      BOOST_CHECK( db.get_global_properties().active_witnesses == expected_active_witnesses );

      // create some tickets
      create_ticket( wit_account_ids[4], lock_forever, asset(40) );
      create_ticket( wit_account_ids[7], lock_forever, asset(30) );
      create_ticket( wit_account_ids[7], lock_720_days, asset(20) );

      auto tick_start_time = db.head_block_time();

      generate_blocks( db.get_dynamic_global_properties().next_maintenance_time );

      // votes doesn't change
      for( size_t i = 0; i < total; ++i )
      {
         BOOST_CHECK_EQUAL( wit_ids[i](db).total_votes, expected_votes[i] );
      }
      BOOST_CHECK( db.get_global_properties().active_witnesses == expected_active_witnesses );

      // some days passed
      generate_blocks( tick_start_time + fc::days(15) );
      generate_blocks( db.get_dynamic_global_properties().next_maintenance_time );

      // check votes
      expected_votes[0] = 110; // 180 days
      expected_votes[1] = 111; // 225 days
      expected_votes[2] = 112; // 270 days
      expected_votes[3] = 113; // 315 days
      expected_votes[4] = 114+40 - (114+40) / 8; // 360 days
      expected_votes[5] = 115 - 115 * 2 / 8; // 405 days
      expected_votes[6] = 116 - 116 * 3 / 8; // 450 days, 73
      expected_votes[7] = 117+50 - (117+50) * 4 / 8; // 495 days, 84
      expected_votes[8] = 118 - 118 * 5 / 8 + 122; // 540 days
      expected_votes[9] = 119 - 119 * 6 / 8; // 585 days
      expected_votes[10] = 120 - 120 * 7 / 8; // 630 days
      expected_votes[11] = 0; // 675 days
      expected_votes[12] = 122; // 15 days
      expected_votes[13] = 0; // 765 days

      for( size_t i = 0; i < total; ++i )
      {
         BOOST_CHECK_EQUAL( wit_ids[i](db).total_votes, expected_votes[i] );
      }

      expected_active_witnesses = { wit_ids[0], wit_ids[1], wit_ids[2],
                                    wit_ids[3], wit_ids[4], wit_ids[5],
                                    wit_ids[7], wit_ids[8], wit_ids[12] };
      BOOST_CHECK( db.get_global_properties().active_witnesses == expected_active_witnesses );

      // some days passed
      generate_blocks( tick_start_time + fc::days(30) );
      generate_blocks( db.get_dynamic_global_properties().next_maintenance_time );

      // check votes
      expected_votes[4] = 114+40*3 - (114+40*3) / 8; // 375 days
      expected_votes[7] = 117+50*3 - (117+50*3) * 4 / 8; // 510 days
      for( size_t i = 0; i < total; ++i )
      {
         BOOST_CHECK_EQUAL( wit_ids[i](db).total_votes, expected_votes[i] );
      }
      BOOST_CHECK( db.get_global_properties().active_witnesses == expected_active_witnesses );

      // some days passed
      generate_blocks( tick_start_time + fc::days(45) );
      generate_blocks( db.get_dynamic_global_properties().next_maintenance_time );

      // check votes
      expected_votes[4] = 114+40*7 - (114+40*7) / 8; // 390 days
      expected_votes[7] = 117+50*7 - (117+50*7) * 4 / 8; // 525 days
      for( size_t i = 0; i < total; ++i )
      {
         BOOST_CHECK_EQUAL( wit_ids[i](db).total_votes, expected_votes[i] );
      }
      BOOST_CHECK( db.get_global_properties().active_witnesses == expected_active_witnesses );

      // some days passed
      generate_blocks( tick_start_time + fc::days(60) );
      generate_blocks( db.get_dynamic_global_properties().next_maintenance_time );

      // pob activated
      bool has_hf_2262 = ( HARDFORK_CORE_2262_PASSED( db.get_dynamic_global_properties().next_maintenance_time ) );
      expected_votes[0] = 0; // 225 days
      expected_votes[1] = 0; // 270 days
      expected_votes[2] = 0; // 315 days
      expected_votes[3] = 0; // 360 days
      int64_t base4 = 40 * 8 + (114 - 40) - 40;
      expected_votes[4] = ( has_hf_2262 ? 0 : ( base4 - base4 * 2 / 8 ) ); // 405 days
      expected_votes[5] = 0; // 450 days
      expected_votes[6] = 0; // 495 days
      int64_t base7 = 20 * 8 * 8 + ( has_hf_2262 ? 0 : ( (30 - 20) * 8 + (117 - 30 - 20) - (30 - 20) ) );
      expected_votes[7] = base7 - base7 * 5 / 8; // 540 days
      expected_votes[8] = 0; // 585 days
      expected_votes[9] = 0; // 630 days
      expected_votes[10] = 0; // 675 days
      expected_votes[11] = 0; // 720 days
      expected_votes[12] = 0; // 60 days
      expected_votes[13] = 0; // 810 days

      for( size_t i = 0; i < total; ++i )
      {
         BOOST_CHECK_EQUAL( wit_ids[i](db).total_votes, expected_votes[i] );
      }

      expected_active_witnesses = original_wits;
      expected_active_witnesses.erase( *expected_active_witnesses.rbegin() );
      if( !has_hf_2262 )
      {
         expected_active_witnesses.erase( *expected_active_witnesses.rbegin() );
         expected_active_witnesses.insert( wit_ids[4] );
      }
      expected_active_witnesses.insert( wit_ids[7] );
      BOOST_CHECK( db.get_global_properties().active_witnesses == expected_active_witnesses );

      // some days passed
      generate_blocks( tick_start_time + fc::days(60+180) );
      generate_blocks( db.get_dynamic_global_properties().next_maintenance_time );

      has_hf_2262 = ( HARDFORK_CORE_2262_PASSED( db.get_dynamic_global_properties().next_maintenance_time ) );
      // check votes
      base4 = 40 * 6 + (114 - 40) - 40;
      expected_votes[4] = ( has_hf_2262 ? 0 : (base4 - base4 * 6 / 8) ); // 585 days
      base7 = 20 * 8 * 6 + (30 - 20) * 6 + (117 - 30 - 20) - (30 - 20);
      expected_votes[7] = 0; // 720 days

      for( size_t i = 0; i < total; ++i )
      {
         BOOST_CHECK_EQUAL( wit_ids[i](db).total_votes, expected_votes[i] );
      }

      expected_active_witnesses = original_wits;
      if( !has_hf_2262 )
      {
         expected_active_witnesses.erase( *expected_active_witnesses.rbegin() );
         expected_active_witnesses.insert( wit_ids[4] );
      }
      BOOST_CHECK( db.get_global_properties().active_witnesses == expected_active_witnesses );

   } FC_LOG_AND_RETHROW()
}

BOOST_AUTO_TEST_CASE( committee_votes_calculation )
{
   try
   {
      INVOKE( put_my_committee_members );

      GET_ACTOR( committee0 );
      GET_ACTOR( committee1 );
      GET_ACTOR( committee2 );
      GET_ACTOR( committee3 );
      GET_ACTOR( committee4 );
      GET_ACTOR( committee5 );
      GET_ACTOR( committee6 );
      GET_ACTOR( committee7 );
      GET_ACTOR( committee8 );
      GET_ACTOR( committee9 );
      GET_ACTOR( committee10 );
      GET_ACTOR( committee11 );
      GET_ACTOR( committee12 );
      GET_ACTOR( committee13 );

      graphene::app::database_api db_api1(db);

      vector<account_id_type> com_account_ids = { committee0_id, committee1_id, committee2_id, committee3_id,
                                                  committee4_id, committee5_id, committee6_id, committee7_id,
                                                  committee8_id, committee9_id, committee10_id, committee11_id,
                                                  committee12_id, committee13_id };
      vector<committee_member_id_type> com_ids;
      size_t total = com_account_ids.size();

      for( size_t i = 0; i < total; ++i )
      {
         auto com_object = db_api1.get_committee_member_by_account( com_account_ids[i](db).name );
         BOOST_REQUIRE( com_object.valid() );
         com_ids.push_back( com_object->id );
      }

      generate_blocks( HARDFORK_CORE_2103_TIME - 750 * 86400 );
      set_expiration( db, trx );

      // refresh last_vote_time
      for( size_t i = 0; i < total; ++i )
      {
         account_id_type voter = com_account_ids[ total - i - 1 ];

         account_update_operation op;
         op.account = voter;
         op.new_options = op.account(db).options;
         op.new_options->voting_account = account_id_type();

         trx.operations.clear();
         trx.operations.push_back(op);
         PUSH_TX(db, trx, ~0);

         op.new_options->voting_account = GRAPHENE_PROXY_TO_SELF_ACCOUNT;
         trx.operations.clear();
         trx.operations.push_back(op);
         PUSH_TX(db, trx, ~0);

         trx.clear();

         generate_blocks( db.head_block_time() + 45 * 86400 );
         set_expiration( db, trx );
      }

      for( size_t i = 0; i < total; ++i )
      {
         BOOST_CHECK_EQUAL( com_ids[i](db).total_votes, 110u + i );
      }

      generate_blocks( HARDFORK_CORE_2103_TIME );
      generate_blocks( db.get_dynamic_global_properties().next_maintenance_time );
      set_expiration( db, trx );

      uint64_t expected_votes[14];

      expected_votes[0] = 110; // 750 - 45 * 13 = 165 days
      expected_votes[1] = 111; // 210 days
      expected_votes[2] = 112; // 255 days
      expected_votes[3] = 113; // 300 days
      expected_votes[4] = 114; // 345 days
      expected_votes[5] = 115 - 115 / 8; // 390 days
      expected_votes[6] = 116 - 116 * 2 / 8; // 435 days
      expected_votes[7] = 117 - 117 * 3 / 8; // 480 days
      expected_votes[8] = 118 - 118 * 4 / 8; // 525 days
      expected_votes[9] = 119 - 119 * 5 / 8; // 570 days
      expected_votes[10] = 120 - 120 * 6 / 8; // 615 days
      expected_votes[11] = 121 - 121 * 7 / 8; // 660 days
      expected_votes[12] = 0; // 705 days
      expected_votes[13] = 0; // 750 days

      for( size_t i = 0; i < total; ++i )
      {
         BOOST_CHECK_EQUAL( com_ids[i](db).total_votes, expected_votes[i] );
      }

      vector<committee_member_id_type> expected_active_committee_members = {
                                                              com_ids[0], com_ids[1], com_ids[2],
                                                              com_ids[3], com_ids[4], com_ids[5],
                                                              com_ids[6], com_ids[7], com_ids[8] };
      auto current_committee_members = db.get_global_properties().active_committee_members;
      sort( current_committee_members.begin(), current_committee_members.end() );
      BOOST_CHECK( current_committee_members == expected_active_committee_members );

      // new vote
      {
         account_update_operation op;
         op.account = com_account_ids[12];
         op.new_options = op.account(db).options;
         op.new_options->votes.insert( com_ids[11](db).vote_id );
         op.new_options->votes.insert( com_ids[12](db).vote_id );

         trx.operations.clear();
         trx.operations.push_back(op);
         PUSH_TX(db, trx, ~0);
      }

      generate_blocks( db.get_dynamic_global_properties().next_maintenance_time );
      set_expiration( db, trx );

      expected_votes[11] += 122/2;
      expected_votes[12] = 122/2;
      for( size_t i = 0; i < total; ++i )
      {
         BOOST_CHECK_EQUAL( com_ids[i](db).total_votes, expected_votes[i] );
      }

      expected_active_committee_members = { com_ids[0], com_ids[1], com_ids[2],
                                            com_ids[3], com_ids[4], com_ids[5],
                                            com_ids[6], com_ids[7], com_ids[11] };
      current_committee_members = db.get_global_properties().active_committee_members;
      sort( current_committee_members.begin(), current_committee_members.end() );
      BOOST_CHECK( current_committee_members == expected_active_committee_members );

   } FC_LOG_AND_RETHROW()
}

BOOST_AUTO_TEST_SUITE_END()
