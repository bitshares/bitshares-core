/*
 * Post-quantum memo encryption.
 *
 * Memos are the part of this chain where post-quantum protection is most urgent and least
 * recoverable. A signature only has to resist forgery until its transaction confirms, and an
 * account can migrate its keys at any time. A memo is different: it is encrypted once and
 * stored on chain forever, so everything written under classical ECDH today becomes readable
 * the moment secp256k1 falls, no matter what anyone migrates afterwards. Harvest-now,
 * decrypt-later is not hypothetical for this field.
 *
 * The construction here is hybrid rather than post-quantum alone: the AES key is derived from
 * the ECDH secret and the ML-KEM secret together. That keeps it no weaker than today's memo
 * if this KEM implementation turns out to be flawed, and no weaker than the KEM once
 * secp256k1 breaks. An attacker must defeat both.
 */

#include <boost/test/unit_test.hpp>

#include <graphene/protocol/memo.hpp>
#include <graphene/protocol/account.hpp>
#include <graphene/chain/hardfork.hpp>
#include <graphene/chain/global_property_object.hpp>
#include <fc/crypto/pqc.hpp>
#include <fc/io/raw.hpp>

#include "../common/database_fixture.hpp"

using namespace graphene::chain;
using namespace graphene::protocol;
using namespace graphene::chain::test;

BOOST_AUTO_TEST_SUITE( pq_memo_tests )

namespace {
   fc::ecc::private_key key_of( const std::string& s )
   { return fc::ecc::private_key::regenerate( fc::sha256::hash( s.data(), s.size() ) ); }
}

/// The hybrid memo must round-trip for the intended recipient.
BOOST_AUTO_TEST_CASE( hybrid_memo_round_trips )
{
   auto sender    = key_of( "sender" );
   auto recipient = key_of( "recipient" );
   auto kem       = fc::pq_kem_generate( fc::pq_algorithm::ml_kem_768 );

   const std::string plaintext = "settlement instructions, not for the public record";

   memo_data m;
   m.set_message_pq( sender, recipient.get_public_key(), kem.pk, plaintext );

   BOOST_REQUIRE_MESSAGE( m.pq_ciphertext.valid(), "no KEM ciphertext was attached" );
   BOOST_CHECK_EQUAL( m.pq_ciphertext->size(),
                      fc::pqc_sizes::ciphertext_size( fc::pq_algorithm::ml_kem_768 ) );

   // the recipient, holding both halves
   BOOST_CHECK_EQUAL( m.get_message_pq( recipient, sender.get_public_key(), kem.sk ), plaintext );
   // and the sender, who can also derive the same ECDH secret
   BOOST_CHECK_EQUAL( m.get_message_pq( sender, recipient.get_public_key(), kem.sk ), plaintext );
}

/// Holding only the classical key must not be enough. This is the whole point: if the memo
/// were still readable from the ECDH secret alone, the KEM would be decoration.
BOOST_AUTO_TEST_CASE( classical_key_alone_cannot_read_a_hybrid_memo )
{
   auto sender    = key_of( "sender" );
   auto recipient = key_of( "recipient" );
   auto kem       = fc::pq_kem_generate( fc::pq_algorithm::ml_kem_768 );

   memo_data m;
   m.set_message_pq( sender, recipient.get_public_key(), kem.pk, "quantum adversary reads this?" );

   // the classical decrypt path, with the correct classical keys
   BOOST_CHECK_THROW( m.get_message( recipient, sender.get_public_key() ), fc::exception );
}

/// A different KEM key must not open it either.
BOOST_AUTO_TEST_CASE( a_foreign_kem_key_cannot_read_a_hybrid_memo )
{
   auto sender    = key_of( "sender" );
   auto recipient = key_of( "recipient" );
   auto kem       = fc::pq_kem_generate( fc::pq_algorithm::ml_kem_768 );
   auto other     = fc::pq_kem_generate( fc::pq_algorithm::ml_kem_768 );

   memo_data m;
   m.set_message_pq( sender, recipient.get_public_key(), kem.pk, "for one recipient only" );

   // ML-KEM returns a pseudorandom secret rather than failing, so this surfaces as a
   // decrypt or checksum failure rather than an exception out of the KEM.
   BOOST_CHECK_THROW( m.get_message_pq( recipient, sender.get_public_key(), other.sk ),
                      fc::exception );
}

/// Classical memos must be untouched -- same bytes, same behaviour, no ciphertext attached.
BOOST_AUTO_TEST_CASE( classical_memos_are_unchanged )
{
   auto sender    = key_of( "sender" );
   auto recipient = key_of( "recipient" );

   memo_data m;
   m.set_message( sender, recipient.get_public_key(), "an ordinary memo", 12345 );
   BOOST_CHECK( !m.pq_ciphertext.valid() );
   BOOST_CHECK_EQUAL( m.get_message( recipient, sender.get_public_key() ), "an ordinary memo" );
}

/**
 * The wire format is the part that broke a previous feature on this branch: a field appended
 * to a structure that has been on chain since genesis made every historical block fail to
 * deserialize. memo_data sits inside transfer_operation, so it carries exactly that risk.
 *
 * Under the legacy format the encoding must be identical to a pre-PQ node's, with the
 * ciphertext absent even when the object holds one.
 */
BOOST_AUTO_TEST_CASE( the_ciphertext_never_reaches_the_legacy_wire_format )
{
   auto sender    = key_of( "sender" );
   auto recipient = key_of( "recipient" );
   auto kem       = fc::pq_kem_generate( fc::pq_algorithm::ml_kem_768 );

   memo_data plain;
   plain.set_message( sender, recipient.get_public_key(), "same visible fields", 999 );

   memo_data hybrid;
   hybrid.set_message_pq( sender, recipient.get_public_key(), kem.pk, "same visible fields", 999 );
   BOOST_REQUIRE( hybrid.pq_ciphertext.valid() );

   {
      fc::raw::scoped_pq_format fmt( fc::raw::pq_format::legacy );
      // The legacy encoding is exactly the four pre-existing fields and nothing more.
      std::vector<char> expected;
      auto append = [&expected]( const std::vector<char>& v )
         { expected.insert( expected.end(), v.begin(), v.end() ); };
      append( fc::raw::pack( hybrid.from ) );
      append( fc::raw::pack( hybrid.to ) );
      append( fc::raw::pack( hybrid.nonce ) );
      append( fc::raw::pack( hybrid.message ) );
      BOOST_CHECK( fc::raw::pack( hybrid ) == expected );

      // and it round-trips with the ciphertext dropped, rather than corrupting the stream
      auto back = fc::raw::unpack<memo_data>( fc::raw::pack( hybrid ) );
      BOOST_CHECK( !back.pq_ciphertext.valid() );
      BOOST_CHECK_EQUAL( back.nonce, hybrid.nonce );
   }

   {
      fc::raw::scoped_pq_format fmt( fc::raw::pq_format::current );
      auto bytes = fc::raw::pack( hybrid );
      auto back  = fc::raw::unpack<memo_data>( bytes );
      BOOST_REQUIRE( back.pq_ciphertext.valid() );
      BOOST_CHECK( *back.pq_ciphertext == *hybrid.pq_ciphertext );
      BOOST_CHECK_EQUAL( back.get_message_pq( recipient, sender.get_public_key(), kem.sk ),
                         "same visible fields" );
   }
}

/// A memo carrying no ciphertext must encode identically under both formats, so ordinary
/// traffic is unaffected by the feature existing.
BOOST_AUTO_TEST_CASE( memos_without_a_ciphertext_encode_identically_under_both_formats )
{
   auto sender    = key_of( "sender" );
   auto recipient = key_of( "recipient" );

   memo_data m;
   m.set_message( sender, recipient.get_public_key(), "unaffected", 4242 );

   std::vector<char> legacy_bytes, current_bytes;
   { fc::raw::scoped_pq_format f( fc::raw::pq_format::legacy );  legacy_bytes  = fc::raw::pack( m ); }
   { fc::raw::scoped_pq_format f( fc::raw::pq_format::current ); current_bytes = fc::raw::pack( m ); }

   // current adds only the one-byte "optional is empty" marker
   BOOST_CHECK_EQUAL( current_bytes.size(), legacy_bytes.size() + 1 );
   BOOST_CHECK( std::equal( legacy_bytes.begin(), legacy_bytes.end(), current_bytes.begin() ) );
}


/**
 * A published ML-KEM key is what makes post-quantum memos reachable rather than merely
 * possible -- without one, a sender has nowhere to encapsulate to. account_options carries it,
 * and account_options rides inside account_create_operation and account_update_operation, both
 * on chain since genesis, so it needs the same wire gating memo_data got.
 */
BOOST_AUTO_TEST_CASE( account_options_pq_memo_key_is_gated_on_the_wire )
{
   auto kem = fc::pq_kem_generate( fc::pq_algorithm::ml_kem_768 );

   account_options o;
   o.memo_key = key_of( "memo" ).get_public_key();
   o.pq_memo_key = pq_public_key_type();
   o.pq_memo_key->algorithm = fc::pq_algorithm::ml_kem_768;
   o.pq_memo_key->data = kem.pk;

   {
      fc::raw::scoped_pq_format fmt( fc::raw::pq_format::legacy );
      std::vector<char> expected;
      auto append = [&expected]( const std::vector<char>& v )
         { expected.insert( expected.end(), v.begin(), v.end() ); };
      append( fc::raw::pack( o.memo_key ) );
      append( fc::raw::pack( o.voting_account ) );
      append( fc::raw::pack( o.num_witness ) );
      append( fc::raw::pack( o.num_committee ) );
      append( fc::raw::pack( o.votes ) );
      append( fc::raw::pack( o.extensions ) );
      BOOST_CHECK( fc::raw::pack( o ) == expected );

      auto back = fc::raw::unpack<account_options>( fc::raw::pack( o ) );
      BOOST_CHECK( !back.pq_memo_key.valid() );
   }

   {
      fc::raw::scoped_pq_format fmt( fc::raw::pq_format::current );
      auto back = fc::raw::unpack<account_options>( fc::raw::pack( o ) );
      BOOST_REQUIRE( back.pq_memo_key.valid() );
      BOOST_CHECK( back.pq_memo_key->data == kem.pk );
      BOOST_CHECK( back.pq_memo_key->algorithm == fc::pq_algorithm::ml_kem_768 );
   }
}

/// An ML-DSA key in the memo-key field would be a signing key published where senders expect
/// a KEM key; every encapsulation against it would fail. Reject the class, not just bad sizes.
BOOST_AUTO_TEST_CASE( a_signing_key_is_not_accepted_as_a_memo_key )
{
   auto sig_key = fc::pq_generate_keypair( fc::pq_algorithm::ml_dsa_65 );

   account_options o;
   o.memo_key = key_of( "memo" ).get_public_key();
   o.pq_memo_key = pq_public_key_type( sig_key.pub );

   BOOST_CHECK_THROW( o.validate(), fc::exception );
}

/// Raw deserialization of pq_public_key_type performs no length checking, so a malformed key
/// must be caught at the input boundary rather than by the first sender who tries to use it.
BOOST_AUTO_TEST_CASE( a_malformed_memo_key_is_rejected )
{
   auto kem = fc::pq_kem_generate( fc::pq_algorithm::ml_kem_768 );

   account_options o;
   o.memo_key = key_of( "memo" ).get_public_key();
   o.pq_memo_key = pq_public_key_type();
   o.pq_memo_key->algorithm = fc::pq_algorithm::ml_kem_768;
   o.pq_memo_key->data = kem.pk;
   o.pq_memo_key->data.pop_back();          // one byte short of ml_kem_768

   BOOST_CHECK_THROW( o.validate(), fc::exception );

   o.pq_memo_key->data = kem.pk;            // and the correct key still passes
   BOOST_CHECK_NO_THROW( o.validate() );
}

/**
 * A vector for the other implementations.
 *
 * The wallets have to derive the same AES key from the same material, and "I read the formula
 * and wrote it out again" is not evidence of that -- it is the same assumption made twice. So
 * this emits a memo built by the chain's own code, with the keys that produced it, and the
 * JavaScript side is required to recover the plaintext from those bytes alone.
 *
 * The ECC keys are derived from fixed strings so the other side can regenerate them instead of
 * having to import a secret. The KEM secret key is printed because that is also the check of
 * whether the two libraries agree on the ML-KEM key format at all -- a user moving a memo key
 * from the CLI wallet into the browser finds out here, not on a memo they cannot open.
 */
BOOST_AUTO_TEST_CASE( hybrid_memo_cross_implementation_vector )
{
   auto sender    = key_of( "sender" );
   auto recipient = key_of( "recipient" );
   auto kem       = fc::pq_kem_generate( fc::pq_algorithm::ml_kem_768 );

   const std::string plaintext = "cross-implementation vector";
   const uint64_t    nonce     = 1234567890ULL;

   memo_data m;
   m.from = sender.get_public_key();
   m.to   = recipient.get_public_key();
   m.set_message_pq( sender, recipient.get_public_key(), kem.pk, plaintext, nonce );

   BOOST_REQUIRE( m.pq_ciphertext.valid() );
   BOOST_CHECK_EQUAL( m.get_message_pq( recipient, sender.get_public_key(), kem.sk ), plaintext );

   BOOST_TEST_MESSAGE( "VECTOR nonce="      << m.nonce );
   BOOST_TEST_MESSAGE( "VECTOR plaintext="  << plaintext );
   BOOST_TEST_MESSAGE( "VECTOR kem_sk="     << fc::to_hex( kem.sk.data(), kem.sk.size() ) );
   BOOST_TEST_MESSAGE( "VECTOR kem_pk="     << fc::to_hex( kem.pk.data(), kem.pk.size() ) );
   BOOST_TEST_MESSAGE( "VECTOR message="    << fc::to_hex( m.message.data(), m.message.size() ) );
   BOOST_TEST_MESSAGE( "VECTOR ciphertext=" << fc::to_hex( m.pq_ciphertext->data(),
                                                           m.pq_ciphertext->size() ) );
}

/**
 * The sender cannot read back their own hybrid memo. This is a real change in behaviour and
 * it is pinned here so nobody assumes otherwise.
 *
 * With a classical memo, ECDH is symmetric: the sender re-derives the same shared secret from
 * their own private key and the recipient's public one, which is why a wallet can show you
 * what you wrote in a transfer you sent years ago. ML-KEM is not symmetric. Encapsulation
 * produces the shared secret as output, the sender holds nothing that reproduces it, and only
 * the recipient's secret key recovers it from the ciphertext.
 *
 * So a wallet that writes hybrid memos must keep its own copy locally if it wants to show
 * sent memos back to the user. There is no way to recover them from the chain.
 */
BOOST_AUTO_TEST_CASE( sender_cannot_reread_own_hybrid_memo )
{
   auto sender    = key_of( "sender" );
   auto recipient = key_of( "recipient" );
   auto kem       = fc::pq_kem_generate( fc::pq_algorithm::ml_kem_768 );

   memo_data m;
   m.set_message_pq( sender, recipient.get_public_key(), kem.pk, "what did I write?" );

   // The recipient can.
   BOOST_CHECK_EQUAL( m.get_message_pq( recipient, sender.get_public_key(), kem.sk ),
                      "what did I write?" );

   // The sender cannot: they have the classical half but never held the KEM secret key, and
   // the encapsulated secret was not kept.
   BOOST_CHECK_THROW( m.get_message_pq( sender, recipient.get_public_key(), kem.pk ),
                      fc::exception );

   // And the same memo read the classical way fails too -- the KEM half is genuinely load
   // bearing, not decoration alongside a key that would have worked on its own.
   BOOST_CHECK_THROW( m.get_message( recipient, sender.get_public_key() ), fc::exception );
}

BOOST_AUTO_TEST_SUITE_END()

BOOST_FIXTURE_TEST_SUITE( pq_memo_chain_tests, database_fixture )

/**
 * Before activation the field does not reach the wire at all, so an operation carrying one
 * must be rejected rather than confirmed-and-silently-stripped: otherwise the sender's
 * account_update succeeds, the key is discarded, and they believe post-quantum memos are
 * reachable when nothing was ever published.
 */
BOOST_AUTO_TEST_CASE( a_memo_key_is_refused_before_activation_and_accepted_after )
{ try {
   ACTORS( (alice) );
   fund( alice );

   auto kem = fc::pq_kem_generate( fc::pq_algorithm::ml_kem_768 );
   pq_public_key_type kem_pub;
   kem_pub.algorithm = fc::pq_algorithm::ml_kem_768;
   kem_pub.data = kem.pk;

   auto build_update = [&]() {
      account_update_operation op;
      op.account = alice_id;
      account_options opts = alice_id( db ).options;
      opts.pq_memo_key = kem_pub;
      op.new_options = opts;
      return op;
   };

   {
      signed_transaction tx;
      tx.operations.push_back( build_update() );
      set_expiration( db, tx );
      tx.sign( alice_private_key, db.get_chain_id() );
      GRAPHENE_REQUIRE_THROW( PUSH_TX( db, tx ), fc::exception );
   }

   // now cross the hardfork and turn on the committee flag, exactly as pqc_hybrid_tests does
   generate_blocks( HARDFORK_PQ_0_TIME );
   generate_block();
   db.modify( db.get_global_properties(), []( global_property_object& p ) {
      p.parameters.extensions.value.pq_serialization_active = true;
   } );
   set_expiration( db, trx );

   {
      signed_transaction tx;
      tx.operations.push_back( build_update() );
      set_expiration( db, tx );
      // post-activation, chain-state-derived verification expects pq_format::current
      tx.sign( alice_private_key, db.get_chain_id(), fc::raw::pq_format::current );
      PUSH_TX( db, tx );
   }

   const auto& stored = alice_id( db ).options.pq_memo_key;
   BOOST_REQUIRE( stored.valid() );
   BOOST_CHECK( stored->data == kem.pk );

   // and the published key is usable: a memo encrypted to it round-trips
   auto sender = generate_private_key( "memo-sender" );
   memo_data m;
   m.set_message_pq( sender, alice_public_key, stored->data, "readable only with both halves" );
   BOOST_CHECK_EQUAL( m.get_message_pq( sender, alice_public_key, kem.sk ),
                      "readable only with both halves" );
} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_SUITE_END()
