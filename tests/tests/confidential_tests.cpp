/*
 * Copyright (c) 2015 Cryptonomex, Inc., and contributors.
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
#include <graphene/chain/exceptions.hpp>
#include <graphene/chain/hardfork.hpp>

#include <graphene/chain/account_object.hpp>
#include <graphene/chain/asset_object.hpp>
#include <graphene/chain/committee_member_object.hpp>
#include <graphene/chain/proposal_object.hpp>

#include <graphene/db/simple_index.hpp>
#include <graphene/protocol/confidential.hpp>

#include <fc/crypto/digest.hpp>
#include "../common/database_fixture.hpp"

#include <algorithm>

using namespace graphene::chain;

namespace {

void sort_outputs( vector<blind_output>& outputs )
{
   std::sort( outputs.begin(), outputs.end(),
              []( const blind_output& a, const blind_output& b ) { return a.commitment < b.commitment; } );
}

/// A proof bound to a different commitment. The header still parses.
range_proof_type mismatched_range_proof( const fc::sha256& blind, const fc::sha256& nonce, uint64_t value )
{
   const auto other_blind = fc::sha256::hash( blind );
   const auto other_commitment = fc::ecc::blind( other_blind, value );
   return fc::ecc::range_proof_sign( 0, other_commitment, other_blind, nonce, 0, 0, value );
}

void set_blind_output( blind_output& out, const authority& owner, const fc::sha256& blind,
                       const fc::sha256& nonce, uint64_t value, bool real_proof )
{
   out.owner = owner;
   out.commitment = fc::ecc::blind( blind, value );
   if( real_proof )
      out.range_proof = fc::ecc::range_proof_sign( 0, out.commitment, blind, nonce, 0, 0, value );
   else
      out.range_proof = mismatched_range_proof( blind, nonce, value );
}

int64_t confidential_supply( const database& db )
{
   return asset_id_type()(db).dynamic_asset_data_id(db).confidential_supply.value;
}

} // namespace

BOOST_FIXTURE_TEST_SUITE( confidential_tests, database_fixture )
BOOST_AUTO_TEST_CASE( confidential_test )
{ try {
   ACTORS( (dan)(nathan) )
   const asset_object& core = asset_id_type()(db);

   transfer(account_id_type()(db), dan, core.amount(1000000));

   transfer_to_blind_operation to_blind;
   to_blind.amount = core.amount(1000);
   to_blind.from   = dan.id;

   auto owner1_key = fc::ecc::private_key::generate();
   auto owner1_pub = owner1_key.get_public_key();
   auto owner2_key = fc::ecc::private_key::generate();
   auto owner2_pub = owner2_key.get_public_key();
  
   blind_output out1, out2;
   out1.owner = authority( 1, public_key_type(owner1_pub), 1 );
   out2.owner = authority( 1, public_key_type(owner2_pub), 1 );


   auto InB1  = fc::sha256::hash("InB1");
   auto InB2  = fc::sha256::hash("InB2");
   auto nonce1 = fc::sha256::hash("nonce");
   auto nonce2 = fc::sha256::hash("nonce2");

   out1.commitment  = fc::ecc::blind(InB1,250);
   out1.range_proof = fc::ecc::range_proof_sign( 0, out1.commitment, InB1, nonce1, 0, 0, 250 );

   out2.commitment = fc::ecc::blind(InB2,750);
   out2.range_proof = fc::ecc::range_proof_sign( 0, out2.commitment, InB1, nonce2, 0, 0, 750 );

   to_blind.blinding_factor = fc::ecc::blind_sum( {InB1,InB2}, 2 );
   to_blind.outputs = {out2,out1};

   trx.operations = {to_blind};
   sign( trx,  dan_private_key  );
   PUSH_TX(db, trx);
   trx.clear_signatures();

   BOOST_TEST_MESSAGE( "Transfering from blind to blind with change address" );
   auto Out3B  = fc::sha256::hash("Out3B");
   auto Out4B  = fc::ecc::blind_sum( {InB2,Out3B}, 1 ); // add InB2 - Out3b
   blind_output out3, out4;
   out3.commitment = fc::ecc::blind(Out3B,300);
   out3.range_proof = fc::ecc::range_proof_sign( 0, out3.commitment, InB1, nonce1, 0, 0, 300 );
   out4.commitment = fc::ecc::blind(Out4B,750-300-10);
   out4.range_proof = fc::ecc::range_proof_sign( 0, out3.commitment, InB1, nonce1, 0, 0, 750-300-10 );


   blind_transfer_operation blind_tr;
   blind_tr.fee = core.amount(10);
   blind_tr.inputs.push_back( {out2.commitment, out2.owner} );
   blind_tr.outputs = {out3,out4};
   blind_tr.validate();
   trx.operations = {blind_tr};
   sign( trx,  owner2_key  );
   PUSH_TX(db, trx);

   BOOST_TEST_MESSAGE( "Attempting to double spend the same commitments" );
   blind_tr.fee = core.amount(11);

   Out4B  = fc::ecc::blind_sum( {InB2,Out3B}, 1 ); // add InB2 - Out3b
   out4.commitment = fc::ecc::blind(Out4B,750-300-11);
   auto out4_amount = 750-300-10;
   out4.range_proof = fc::ecc::range_proof_sign( 0, out3.commitment, InB1, nonce1, 0, 0, 750-300-11 );
   blind_tr.outputs = {out4,out3};
   trx.operations = {blind_tr};
   BOOST_REQUIRE_THROW( PUSH_TX(db, trx, ~0), graphene::chain::blind_transfer_unknown_commitment );


   BOOST_TEST_MESSAGE( "Transfering from blind to nathan public" );
   out4.commitment = fc::ecc::blind(Out4B,750-300-10);

   transfer_from_blind_operation from_blind;
   from_blind.fee = core.amount(10);
   from_blind.to  = nathan.id;
   from_blind.amount = core.amount( out4_amount - 10 );
   from_blind.blinding_factor = Out4B;
   from_blind.inputs.push_back( {out4.commitment, out4.owner} );
   trx.operations = {from_blind};
   trx.clear_signatures();
   PUSH_TX(db, trx);

   BOOST_REQUIRE_EQUAL( get_balance( nathan, core ), 750-300-10-10 );

} FC_LOG_AND_RETHROW() }

/**
 * After HARDFORK_CORE_2605, multi-output blind operations verify range proofs.
 * Proofs that only satisfy the header check are rejected. Legitimate transfers
 * still apply. validate() is unchanged, so the same operation is accepted before
 * the hardfork.
 */
BOOST_AUTO_TEST_CASE( range_proofs_verified_after_hardfork )
{ try {
   ACTORS( (alice) );
   const asset_object& core = asset_id_type()(db);
   transfer( account_id_type(), alice_id, core.amount( 100000 ) );
   const authority own = authority( 1, alice_public_key, 1 );

   BOOST_REQUIRE( db.head_block_time() < HARDFORK_CORE_2605_TIME );

   const auto b1 = fc::sha256::hash( "pre-1" );
   const auto b2 = fc::sha256::hash( "pre-2" );
   const auto n1 = fc::sha256::hash( "pre-n1" );
   const auto n2 = fc::sha256::hash( "pre-n2" );
   blind_output pre_out1, pre_out2;
   set_blind_output( pre_out1, own, b1, n1, 400, false );
   set_blind_output( pre_out2, own, b2, n2, 600, false );

   transfer_to_blind_operation pre;
   pre.from = alice_id;
   pre.amount = core.amount( 1000 );
   pre.blinding_factor = fc::ecc::blind_sum( { b1, b2 }, 2 );
   pre.outputs = { pre_out1, pre_out2 };
   sort_outputs( pre.outputs );
   pre.validate();
   BOOST_REQUIRE_THROW( verify_blind_output_range_proofs( pre.outputs ), fc::exception );

   trx.clear();
   trx.operations = { pre };
   test::set_expiration( db, trx );
   sign( trx, alice_private_key );
   PUSH_TX( db, trx );
   BOOST_REQUIRE_EQUAL( confidential_supply( db ), 1000 );

   transfer_to_blind_operation pre_proposed = pre;
   pre_proposed.amount = core.amount( 300 );
   const auto pb1 = fc::sha256::hash( "pre-prop-1" );
   const auto pb2 = fc::sha256::hash( "pre-prop-2" );
   blind_output pp1, pp2;
   set_blind_output( pp1, own, pb1, n1, 100, false );
   set_blind_output( pp2, own, pb2, n2, 200, false );
   pre_proposed.blinding_factor = fc::ecc::blind_sum( { pb1, pb2 }, 2 );
   pre_proposed.outputs = { pp1, pp2 };
   sort_outputs( pre_proposed.outputs );
   pre_proposed.validate();

   proposal_create_operation proposal;
   proposal.fee_paying_account = alice_id;
   proposal.expiration_time = db.head_block_time() + 100;
   proposal.proposed_ops.emplace_back( pre_proposed );
   trx.clear();
   trx.operations = { proposal };
   test::set_expiration( db, trx );
   sign( trx, alice_private_key );
   PUSH_TX( db, trx );

   generate_blocks( HARDFORK_CORE_2605_TIME );
   test::set_expiration( db, trx );
   BOOST_REQUIRE( db.head_block_time() >= HARDFORK_CORE_2605_TIME );

   const auto hb1 = fc::sha256::hash( "post-bad-1" );
   const auto hb2 = fc::sha256::hash( "post-bad-2" );
   blind_output bad1, bad2;
   set_blind_output( bad1, own, hb1, n1, 100, false );
   set_blind_output( bad2, own, hb2, n2, 200, false );
   transfer_to_blind_operation bad;
   bad.from = alice_id;
   bad.amount = core.amount( 300 );
   bad.blinding_factor = fc::ecc::blind_sum( { hb1, hb2 }, 2 );
   bad.outputs = { bad1, bad2 };
   sort_outputs( bad.outputs );
   bad.validate();
   BOOST_REQUIRE_THROW( verify_blind_output_range_proofs( bad.outputs ), fc::exception );

   trx.clear();
   trx.operations = { bad };
   test::set_expiration( db, trx );
   sign( trx, alice_private_key );
   BOOST_REQUIRE_THROW( PUSH_TX( db, trx ), fc::exception );
   BOOST_REQUIRE_EQUAL( confidential_supply( db ), 1000 );

   proposal.expiration_time = db.head_block_time() + 100;
   proposal.proposed_ops.clear();
   proposal.proposed_ops.emplace_back( bad );
   trx.clear();
   trx.operations = { proposal };
   test::set_expiration( db, trx );
   sign( trx, alice_private_key );
   BOOST_REQUIRE_THROW( PUSH_TX( db, trx ), fc::exception );
   BOOST_REQUIRE_EQUAL( confidential_supply( db ), 1000 );

   const auto g1 = fc::sha256::hash( "legit-1" );
   const auto g2 = fc::sha256::hash( "legit-2" );
   const auto gn1 = fc::sha256::hash( "legit-n1" );
   const auto gn2 = fc::sha256::hash( "legit-n2" );
   blind_output out1, out2;
   set_blind_output( out1, own, g1, gn1, 250, true );
   set_blind_output( out2, own, g2, gn2, 750, true );
   verify_blind_output_range_proofs( { out1, out2 } );

   transfer_to_blind_operation legit;
   legit.from = alice_id;
   legit.amount = core.amount( 1000 );
   legit.blinding_factor = fc::ecc::blind_sum( { g1, g2 }, 2 );
   legit.outputs = { out1, out2 };
   sort_outputs( legit.outputs );
   legit.validate();
   trx.clear();
   trx.operations = { legit };
   test::set_expiration( db, trx );
   sign( trx, alice_private_key );
   PUSH_TX( db, trx );
   BOOST_REQUIRE_EQUAL( confidential_supply( db ), 2000 );

   const auto g3 = fc::sha256::hash( "legit-3" );
   const auto gn3 = fc::sha256::hash( "legit-n3" );
   const auto g4 = fc::ecc::blind_sum( { g2, g3 }, 1 );
   blind_output out3, out4;
   set_blind_output( out3, own, g3, gn3, 300, true );
   set_blind_output( out4, own, g4, gn2, 440, true );

   blind_output bad3, bad4;
   set_blind_output( bad3, own, g3, gn3, 300, false );
   bad4 = out4;
   bad4.range_proof = mismatched_range_proof( g4, gn2, 440 );
   blind_transfer_operation bad_split;
   bad_split.fee = core.amount( 10 );
   bad_split.inputs.push_back( { out2.commitment, out2.owner } );
   bad_split.outputs = { bad3, bad4 };
   sort_outputs( bad_split.outputs );
   bad_split.validate();
   trx.clear();
   trx.operations = { bad_split };
   test::set_expiration( db, trx );
   sign( trx, alice_private_key );
   BOOST_REQUIRE_THROW( PUSH_TX( db, trx ), fc::exception );
   BOOST_REQUIRE_EQUAL( confidential_supply( db ), 2000 );

   blind_transfer_operation blind_tr;
   blind_tr.fee = core.amount( 10 );
   blind_tr.inputs.push_back( { out2.commitment, out2.owner } );
   blind_tr.outputs = { out3, out4 };
   sort_outputs( blind_tr.outputs );
   blind_tr.validate();
   verify_blind_output_range_proofs( blind_tr.outputs );
   trx.clear();
   trx.operations = { blind_tr };
   test::set_expiration( db, trx );
   sign( trx, alice_private_key );
   PUSH_TX( db, trx );
   BOOST_REQUIRE_EQUAL( confidential_supply( db ), 1990 );

   transfer_from_blind_operation back;
   back.fee = core.amount( 0 );
   back.amount = core.amount( 440 );
   back.to = alice_id;
   back.blinding_factor = g4;
   back.inputs.push_back( { out4.commitment, out4.owner } );
   trx.clear();
   trx.operations = { back };
   test::set_expiration( db, trx );
   sign( trx, alice_private_key );
   PUSH_TX( db, trx );
   BOOST_REQUIRE_EQUAL( confidential_supply( db ), 1550 );

   const auto single_blind = fc::sha256::hash( "single" );
   blind_output single;
   single.owner = own;
   single.commitment = fc::ecc::blind( single_blind, 100 );
   transfer_to_blind_operation one;
   one.from = alice_id;
   one.amount = core.amount( 100 );
   one.blinding_factor = single_blind;
   one.outputs = { single };
   one.validate();
   verify_blind_output_range_proofs( one.outputs );
   trx.clear();
   trx.operations = { one };
   test::set_expiration( db, trx );
   sign( trx, alice_private_key );
   PUSH_TX( db, trx );
   BOOST_REQUIRE_EQUAL( confidential_supply( db ), 1650 );

} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_SUITE_END()
