#include <boost/test/unit_test.hpp>

#include <graphene/chain/database.hpp>
#include <graphene/chain/global_property_object.hpp>
#include <graphene/chain/hardfork.hpp>
#include <graphene/chain/witness_object.hpp>
#include <graphene/protocol/transaction.hpp>

#include <fc/crypto/pqc.hpp>

#include "../common/database_fixture.hpp"

using namespace graphene::chain;
using namespace graphene::protocol;
using namespace graphene::chain::test;

BOOST_FIXTURE_TEST_SUITE( pq_hybrid_tests, database_fixture )

BOOST_AUTO_TEST_CASE( pq_key_base58_roundtrip )
{ try {
   // protocol-level public key round trip (BTS prefix + 'P' + alg byte + pk)
   fc::pq_private_key priv = fc::pq_private_key::generate( fc::pq_algorithm::ml_dsa_65 );
   pq_public_key_type pub( priv.get_public_key() );
   BOOST_CHECK( pub == pq_public_key_type::from_base58( pub.to_base58() ) );

   // private key round trip (payload = alg byte + sk + pk)
   fc::pq_private_key priv2 = fc::pq_private_key::from_base58( priv.to_base58() );
   BOOST_CHECK( priv2 == priv );

   // deterministic re-derivation (Approach A: no new backup material)
   auto seed = fc::sha256::hash( "some existing entropy" );
   fc::pq_private_key a1 = fc::pq_private_key::regenerate_from_seed( fc::pq_algorithm::ml_dsa_65, seed );
   fc::pq_private_key a2 = fc::pq_private_key::regenerate_from_seed( fc::pq_algorithm::ml_dsa_65, seed );
   BOOST_CHECK( a1 == a2 );
   BOOST_CHECK( a1 != priv );
} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE( pq_hybrid_sign_and_verify_authority )
{ try {
   chain_id_type chain_id( fc::sha256::hash( "testnet" ) );

   fc::ecc::private_key legacy_key = generate_private_key( "pq-test-legacy" );
   fc::pq_private_key pq_priv = fc::pq_private_key::generate( fc::pq_algorithm::ml_dsa_65 );
   pq_public_key_type pq_pub( pq_priv.get_public_key() );

   auto no_custom = []( account_id_type, const operation&, rejected_predicate_map* )
                        -> vector<authority> { return {}; };

   auto make_tx = [&]() -> signed_transaction {
      signed_transaction stx;
      stx.ref_block_num = 0;
      stx.ref_block_prefix = 0;
      stx.expiration = db.head_block_time() + fc::hours( 1 );
      operation op = transfer_operation();
      op.get<transfer_operation>().from = account_id_type( 100 );
      op.get<transfer_operation>().to   = account_id_type( 101 );
      stx.operations.push_back( op );
      return stx;
   };

   // --- 1) legacy-only authority still verifies via key_auths (unchanged) ---
   {
      authority active;
      active.weight_threshold = 1;
      active.key_auths[ legacy_key.get_public_key() ] = 1;
      authority owner = active;
      auto get_active = [&]( account_id_type ) -> const authority* { return &active; };
      auto get_owner  = [&]( account_id_type ) -> const authority* { return &owner;  };

      signed_transaction stx = make_tx();
      stx.sign( legacy_key, chain_id );
      BOOST_CHECK_NO_THROW( graphene::protocol::verify_authority(
            stx.operations, stx.get_signature_keys( chain_id ),
            get_active, get_owner, no_custom, true, false,
            GRAPHENE_MAX_SIG_CHECK_DEPTH, false, true,
            flat_set<account_id_type>(), flat_set<account_id_type>(), flat_set<pq_public_key_type>() ) );
   }

   // --- 2) hybrid authority: ECDSA AND ML-DSA signatures are both required
   //        and both verify (migration posture: either scheme alone is
   //        insufficient, together they authorize) ---
   {
      authority hybrid;
      hybrid.weight_threshold = 2;
      hybrid.key_auths[ legacy_key.get_public_key() ] = 1;
      hybrid.pq_key_auths[ pq_pub ] = 1;
      authority owner = hybrid;
      auto get_active = [&]( account_id_type ) -> const authority* { return &hybrid; };
      auto get_owner  = [&]( account_id_type ) -> const authority* { return &owner;  };
      auto verify = [&]( const signed_transaction& t ) {
         flat_set<pq_public_key_type> pq_signees;
         for( const auto& s : t.pq_signatures )
            pq_signees.insert( s.key );
         graphene::protocol::verify_authority(
            t.operations, t.get_signature_keys( chain_id ),
            get_active, get_owner, no_custom, true, false,
            GRAPHENE_MAX_SIG_CHECK_DEPTH, false, true,
            flat_set<account_id_type>(), flat_set<account_id_type>(), pq_signees );
      };

      // ECDSA alone is not enough
      signed_transaction ecdsa_only = make_tx();
      ecdsa_only.sign( legacy_key, chain_id );
      BOOST_CHECK_THROW( verify( ecdsa_only ), tx_missing_active_auth );

      // PQ alone is not enough
      signed_transaction pq_only = make_tx();
      pq_only.sign_pq( pq_priv, chain_id );
      BOOST_CHECK_THROW( verify( pq_only ), tx_missing_active_auth );

      // hybrid (legacy + PQ) authorizes; both signatures bind to the same
      // digest (transaction::sig_digest does not cover the signature vectors)
      signed_transaction hybrid_trx = make_tx();
      hybrid_trx.sign( legacy_key, chain_id );
      pq_signature sig = hybrid_trx.sign_pq( pq_priv, chain_id );
      BOOST_CHECK( sig.key == pq_pub );
      BOOST_CHECK_NO_THROW( verify( hybrid_trx ) );
   }

   // an unused (extra) PQ signature is still rejected
   {
      authority single;
      single.weight_threshold = 1;
      single.key_auths[ legacy_key.get_public_key() ] = 1;
      authority owner = single;
      auto get_active = [&]( account_id_type ) -> const authority* { return &single; };
      auto get_owner  = [&]( account_id_type ) -> const authority* { return &owner;  };
      auto verify_irrelevant = [&]( const signed_transaction& t ) {
         flat_set<pq_public_key_type> pq_signees;
         for( const auto& s : t.pq_signatures )
            pq_signees.insert( s.key );
         graphene::protocol::verify_authority(
            t.operations, t.get_signature_keys( chain_id ),
            get_active, get_owner, no_custom, true, false,
            GRAPHENE_MAX_SIG_CHECK_DEPTH, false, true,
            flat_set<account_id_type>(), flat_set<account_id_type>(), pq_signees );
      };

signed_transaction unused = make_tx();
      unused.sign( legacy_key, chain_id );
      fc::pq_private_key other = fc::pq_private_key::generate();
      unused.sign_pq( other, chain_id );
      bool threw = false;
      try { verify_irrelevant( unused ); }
      catch( const tx_irrelevant_sig& ) { threw = true; }
      BOOST_CHECK( threw );
   }

   // --- 3) PQ-only authority (post-migration) ---
   {
      authority pq_only_auth;
      pq_only_auth.weight_threshold = 1;
      pq_only_auth.pq_key_auths[ pq_pub ] = 1;
      authority owner = pq_only_auth;
      auto get_active = [&]( account_id_type ) -> const authority* { return &pq_only_auth; };
      auto get_owner  = [&]( account_id_type ) -> const authority* { return &owner;  };

      signed_transaction stx = make_tx();
      stx.sign_pq( pq_priv, chain_id );
      flat_set<pq_public_key_type> pq_signees;
      for( const auto& s : stx.pq_signatures )
         pq_signees.insert( s.key );
      BOOST_CHECK_NO_THROW( graphene::protocol::verify_authority(
            stx.operations, stx.get_signature_keys( chain_id ),
            get_active, get_owner, no_custom, false, false,
            GRAPHENE_MAX_SIG_CHECK_DEPTH, false, true,
            flat_set<account_id_type>(), flat_set<account_id_type>(), pq_signees ) );
   }
} FC_LOG_AND_RETHROW() }

// end-to-end through the chain database: a real account's active authority in
// the object database carries a PQ key (via the account_update evaluator),
// and the resulting PQ-signed transaction both verifies and applies
// end-to-end through the chain database: a real account's active authority in
// the object database carries a PQ key (via the account_update evaluator),
// and the resulting PQ-signed transaction both verifies and applies
BOOST_AUTO_TEST_CASE( pq_authority_in_object_database )
{ try {
   // PQ features are gated behind both the hardfork time AND the
   // pq_serialization_active committee parameter (see HARDFORK_PQ_0_PASSED
   // usages in account_evaluator.cpp / db_block.cpp); tests must activate
   // both explicitly, rather than relying on either default
   // hardfork. Directly modifying global_property_object is the standard
   // test-only shortcut for chain parameters that are normally set via a
   // committee proposal (see e.g. change_fees() in database_fixture.cpp).
   generate_blocks( HARDFORK_PQ_0_TIME );
   generate_block();
   db.modify( db.get_global_properties(), []( global_property_object& p ) {
      p.parameters.extensions.value.pq_serialization_active = true;
   } );
   // create_account()'s private-key overload pushes the shared `trx` member without
   // refreshing its expiration itself, so it must be refreshed here after jumping the
   // clock forward to HARDFORK_PQ_0_TIME (~2030) -- otherwise it retains a stale
   // pre-genesis-relative expiration and every subsequent transaction is rejected.
   set_expiration( db, trx );

   fc::ecc::private_key alice_key = generate_private_key( "pq-alice-key" );
   const account_object& alice = create_account( "pq-alice", alice_key );
   const account_object& bob   = create_account( "pq-bob" );
   fund( alice );

   fc::pq_private_key pq_priv = fc::pq_private_key::generate( fc::pq_algorithm::ml_dsa_65 );
   pq_public_key_type pq_pub( pq_priv.get_public_key() );

   // re-key alice to a mixed authority: one legacy key + one PQ key, both
   // with weight equal to the threshold (either signature alone suffices)
   authority new_active;
   new_active.weight_threshold = 1;
   new_active.key_auths[ alice_key.get_public_key() ] = 1;
   new_active.pq_key_auths[ pq_pub ] = 1;

   // wallet: update the active authority (still signed with the legacy key)
   account_update_operation up_op;
   up_op.account = alice.id;
   up_op.active  = new_active;
   signed_transaction up_tx;
   up_tx.operations.push_back( up_op );
   set_expiration( db, up_tx );
   // Explicit format required: this test has activated pq_serialization_active, so
   // chain-state-derived verification (_apply_transaction) expects pq_format::current.
   // The fixture's generic sign() helper (and sign()/sign_pq()'s own default parameter)
   // can't know that -- their default is pq_format::legacy, correct for the vastly more
   // common pre-activation case but wrong here. A real wallet computes this from chain
   // state before every sign call (see wallet_sign.cpp's pq_fmt handling); tests that
   // activate PQ explicitly must do the same.
   up_tx.sign( alice_key, db.get_chain_id(), fc::raw::pq_format::current );
   PUSH_TX( db, up_tx );

   // the object database now holds the PQ key in alice's active authority
   const account_object& alice_db = db.get<account_object>( alice.id );
   BOOST_CHECK_EQUAL( alice_db.active.pq_key_auths.size(), 1u );
   BOOST_CHECK( alice_db.active.pq_key_auths.find( pq_pub ) != alice_db.active.pq_key_auths.end() );

   // a transfer signed with the PQ key applies with full validation
   transfer_operation xfer;
   xfer.from   = alice.id;
   xfer.to     = bob.id;
   xfer.amount = asset( 1000 );
   signed_transaction xfer_tx;
   xfer_tx.operations.push_back( xfer );
   set_expiration( db, xfer_tx );
   xfer_tx.sign_pq( pq_priv, db.get_chain_id() );
   PUSH_TX( db, xfer_tx );
   BOOST_CHECK_EQUAL( db.get_balance( bob, db.get( asset_id_type() ) ).amount.value, 1000 );

   // a tampered PQ signature is rejected by full validation
   signed_transaction tampered = xfer_tx;
   tampered.operations[0].get<transfer_operation>().amount.amount = 1001;
   set_expiration( db, tampered );
   tampered.sign_pq( pq_priv, db.get_chain_id() );
   tampered.pq_signatures.back().signature.back() ^= 0xFF;
   bool tampered_rejected = false;
   try { PUSH_TX( db, tampered ); }
   catch( const fc::exception& ) { tampered_rejected = true; }
   BOOST_CHECK( tampered_rejected );

   // an unknown (not in the account authority) PQ key does not authorize
   signed_transaction wrong = xfer_tx;
   wrong.operations[0].get<transfer_operation>().amount.amount = 1002;
   set_expiration( db, wrong );
   wrong.pq_signatures.clear();
   fc::pq_private_key other = fc::pq_private_key::generate();
   wrong.sign_pq( other, db.get_chain_id() );
   bool wrong_rejected = false;
   try { PUSH_TX( db, wrong ); }
   catch( const fc::exception& ) { wrong_rejected = true; }
   BOOST_CHECK( wrong_rejected );

   generate_block();
} FC_LOG_AND_RETHROW() }

/* post-quantum witness: block production and validation */
BOOST_AUTO_TEST_CASE( pq_witness_block_signing )
{ try {
   // See pq_authority_in_object_database for why both the hardfork time and
   // the pq_serialization_active flag must be activated explicitly.
   generate_blocks( HARDFORK_PQ_0_TIME );
   generate_block();
   db.modify( db.get_global_properties(), []( global_property_object& p ) {
      p.parameters.extensions.value.pq_serialization_active = true;
   } );

   fc::pq_private_key pq_wif_key = fc::pq_private_key::generate( fc::pq_algorithm::ml_dsa_65 );
   pq_public_key_type pq_pub( pq_wif_key.get_public_key() );

   // attach a PQ signing key to the currently scheduled (genesis) witness
   witness_id_type wit_id = db.get_scheduled_witness( 1 );
   const witness_object& wit = db.get( wit_id );
   BOOST_CHECK( wit.signing_key == init_account_priv_key.get_public_key() );
   db.modify( wit, [&]( witness_object& w ){ w.pq_signing_key = pq_pub; } );

   // without the PQ private key, block production must fail
   BOOST_CHECK_THROW(
      db.generate_block( db.get_slot_time( 1 ), wit_id, init_account_priv_key, database::skip_nothing ),
      fc::assert_exception );

   // with the PQ private key, a PQ-signed block is produced and passes full
   // validation (generate_block pushes the block with skip_nothing)
   signed_block b = db.generate_block( db.get_slot_time( 1 ), wit_id, init_account_priv_key,
                                       database::skip_nothing, pq_wif_key );
   BOOST_CHECK( b.witness_pq_signature.valid() );
   BOOST_CHECK( b.validate_signee_pq( pq_pub ) );

   // the block was pushed: head block exists and the PQ path was exercised
   BOOST_CHECK( db.head_block_num() > 0 );

   // a PQ-signed block is rejected when the witness carries no PQ key
   db.modify( wit, [&]( witness_object& w ){ w.pq_signing_key.reset(); } );
   BOOST_CHECK_THROW(
      db.generate_block( db.get_slot_time( 1 ), wit_id, init_account_priv_key,
                         database::skip_nothing, pq_wif_key ),
      fc::assert_exception );
} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_SUITE_END()