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

#include <iostream>

#include "../common/database_fixture.hpp"

using namespace graphene::chain;
using namespace graphene::chain::test;


BOOST_FIXTURE_TEST_SUITE(voting_tests, database_fixture)

BOOST_AUTO_TEST_CASE(put_my_witnesses)
{
   try
   {
      graphene::app::database_api db_api1(db);

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

      // Create a vector with private key of all witnesses, will be used to activate 11 witnesses at a time
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

      // create a map with account id and witness id of the first 11 witnesses
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

      // Check current default witnesses, default chain is configured with 10 witnesses
      auto witnesses = db.get_global_properties().active_witnesses;
      BOOST_CHECK_EQUAL(witnesses.size(), 10);
      BOOST_CHECK_EQUAL(witnesses.begin()[0].instance.value, 1);
      BOOST_CHECK_EQUAL(witnesses.begin()[1].instance.value, 2);
      BOOST_CHECK_EQUAL(witnesses.begin()[2].instance.value, 3);
      BOOST_CHECK_EQUAL(witnesses.begin()[3].instance.value, 4);
      BOOST_CHECK_EQUAL(witnesses.begin()[4].instance.value, 5);
      BOOST_CHECK_EQUAL(witnesses.begin()[5].instance.value, 6);
      BOOST_CHECK_EQUAL(witnesses.begin()[6].instance.value, 7);
      BOOST_CHECK_EQUAL(witnesses.begin()[7].instance.value, 8);
      BOOST_CHECK_EQUAL(witnesses.begin()[8].instance.value, 9);
      BOOST_CHECK_EQUAL(witnesses.begin()[9].instance.value, 10);

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
      BOOST_CHECK_EQUAL(witnesses.size(), 11);
      BOOST_CHECK_EQUAL(witnesses.begin()[0].instance.value, 14);
      BOOST_CHECK_EQUAL(witnesses.begin()[1].instance.value, 15);
      BOOST_CHECK_EQUAL(witnesses.begin()[2].instance.value, 16);
      BOOST_CHECK_EQUAL(witnesses.begin()[3].instance.value, 17);
      BOOST_CHECK_EQUAL(witnesses.begin()[4].instance.value, 18);
      BOOST_CHECK_EQUAL(witnesses.begin()[5].instance.value, 19);
      BOOST_CHECK_EQUAL(witnesses.begin()[6].instance.value, 20);
      BOOST_CHECK_EQUAL(witnesses.begin()[7].instance.value, 21);
      BOOST_CHECK_EQUAL(witnesses.begin()[8].instance.value, 22);
      BOOST_CHECK_EQUAL(witnesses.begin()[9].instance.value, 23);
      BOOST_CHECK_EQUAL(witnesses.begin()[10].instance.value, 24);

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
      BOOST_CHECK_EQUAL(witness1_object->total_votes, 111);

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
      BOOST_CHECK_EQUAL(witness1_object->total_votes, 0);

   } FC_LOG_AND_RETHROW()
}

BOOST_AUTO_TEST_CASE(put_my_committee_members)
{
   try
   {
      graphene::app::database_api db_api1(db);

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

      // Create a vector with private key of all witnesses, will be used to activate 11 witnesses at a time
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

      // create a map with account id and committee id of the first 11 witnesses
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

      // Check current default witnesses, default chain is configured with 10 witnesses
      auto committee_members = db.get_global_properties().active_committee_members;

      BOOST_CHECK_EQUAL(committee_members.size(), 10);
      BOOST_CHECK_EQUAL(committee_members.begin()[0].instance.value, 0);
      BOOST_CHECK_EQUAL(committee_members.begin()[1].instance.value, 1);
      BOOST_CHECK_EQUAL(committee_members.begin()[2].instance.value, 2);
      BOOST_CHECK_EQUAL(committee_members.begin()[3].instance.value, 3);
      BOOST_CHECK_EQUAL(committee_members.begin()[4].instance.value, 4);
      BOOST_CHECK_EQUAL(committee_members.begin()[5].instance.value, 5);
      BOOST_CHECK_EQUAL(committee_members.begin()[6].instance.value, 6);
      BOOST_CHECK_EQUAL(committee_members.begin()[7].instance.value, 7);
      BOOST_CHECK_EQUAL(committee_members.begin()[8].instance.value, 8);
      BOOST_CHECK_EQUAL(committee_members.begin()[9].instance.value, 9);

      // Activate all committee
      // Each witness is voted with incremental stake so last witness created will be the ones with more votes
      int c = 0;
      for (auto committee : committee_map) {
         int stake = 100 + c + 10;
         transfer(committee_account, committee.first, asset(stake));
         {
            set_expiration(db, trx);
            account_update_operation op;
            op.account = committee.first;
            op.new_options = committee.first(db).options;
            op.new_options->votes.insert(committee.second(db).vote_id);

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
      BOOST_CHECK_EQUAL(committee_members.size(), 11);

      /* TODO we are not in full control, seems to committee members have votes by default
      BOOST_CHECK_EQUAL(committee_members.begin()[0].instance.value, 14);
      BOOST_CHECK_EQUAL(committee_members.begin()[1].instance.value, 15);
      BOOST_CHECK_EQUAL(committee_members.begin()[2].instance.value, 16);
      BOOST_CHECK_EQUAL(committee_members.begin()[3].instance.value, 17);
      BOOST_CHECK_EQUAL(committee_members.begin()[4].instance.value, 18);
      BOOST_CHECK_EQUAL(committee_members.begin()[5].instance.value, 19);
      BOOST_CHECK_EQUAL(committee_members.begin()[6].instance.value, 20);
      BOOST_CHECK_EQUAL(committee_members.begin()[7].instance.value, 21);
      BOOST_CHECK_EQUAL(committee_members.begin()[8].instance.value, 22);
      BOOST_CHECK_EQUAL(committee_members.begin()[9].instance.value, 23);
      BOOST_CHECK_EQUAL(committee_members.begin()[10].instance.value, 24);
      */
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
      BOOST_CHECK_EQUAL(committee1_object->total_votes, 111);

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
      BOOST_CHECK_EQUAL(committee1_object->total_votes, 0);

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

BOOST_AUTO_TEST_SUITE_END()
