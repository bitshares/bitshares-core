/*
 * Phase 0-1 tests for the PQ hardfork dual-format serialization
 * (docs/PQ-Hardfork-Implementation-Plan.md).
 *
 * Verifies that pq_format::legacy reproduces the pre-PQ wire format,
 * pq_format::current matches the post-PQ reflected serialization, and
 * both formats round-trip correctly.
 */

#include <boost/test/unit_test.hpp>

#include <graphene/protocol/authority.hpp>
#include <graphene/protocol/transaction.hpp>
#include <graphene/protocol/block.hpp>
#include <graphene/protocol/address.hpp>
#include <graphene/protocol/transfer.hpp>

#include <fc/crypto/pqc.hpp>

#include <fc/io/raw.hpp>
#include <fc/crypto/sha256.hpp>

using namespace graphene::protocol;

BOOST_AUTO_TEST_SUITE( pq_hardfork_tests )

template<typename T>
std::vector<char> pack_field( const T& v )
{
   return fc::raw::pack( v );
}

template<typename... Ts>
std::vector<char> concat_vec( const Ts&... parts )
{
   std::vector<char> result;
   ( result.insert( result.end(), parts.begin(), parts.end() ), ... );
   return result;
}

bool same_pq_signatures( const std::vector<pq_signature>& a, const std::vector<pq_signature>& b )
{
   if( a.size() != b.size() ) return false;
   for( size_t i = 0; i < a.size(); ++i )
      if( !( a[i].key == b[i].key && a[i].signature == b[i].signature ) ) return false;
   return true;
}

BOOST_AUTO_TEST_CASE( pq_format_context_raii )
{
   // Default is legacy in Phase 1
   BOOST_CHECK( fc::raw::get_pq_format() == fc::raw::pq_format::legacy );
   {
      fc::raw::scoped_pq_format fmt( fc::raw::pq_format::current );
      BOOST_CHECK( fc::raw::get_pq_format() == fc::raw::pq_format::current );
   }
   BOOST_CHECK( fc::raw::get_pq_format() == fc::raw::pq_format::legacy );
}

BOOST_AUTO_TEST_CASE( authority_legacy_golden_bytes )
{
   authority a;
   a.weight_threshold = 2;
   a.account_auths[ account_id_type( 42 ) ] = 1;
   a.key_auths[ public_key_type() ] = 1;
   a.pq_key_auths[ pq_public_key_type() ] = 1;
   a.address_auths[ address() ] = 1;

   // Legacy: pq_key_auths absent; fields in pre-PQ order
   std::vector<char> legacy;
   {
      fc::raw::scoped_pq_format fmt( fc::raw::pq_format::legacy );
      legacy = fc::raw::pack( a );
   }
   std::vector<char> expected = concat_vec(
      pack_field( a.weight_threshold ),
      pack_field( a.account_auths ),
      pack_field( a.key_auths ),
      pack_field( a.address_auths ) );
   BOOST_CHECK( legacy == expected );

   // Current: pq_key_auths inserted between key_auths and address_auths
   std::vector<char> current;
   {
      fc::raw::scoped_pq_format fmt( fc::raw::pq_format::current );
      current = fc::raw::pack( a );
   }
   std::vector<char> expected_current = concat_vec(
      pack_field( a.weight_threshold ),
      pack_field( a.account_auths ),
      pack_field( a.key_auths ),
      pack_field( a.pq_key_auths ),
      pack_field( a.address_auths ) );
   BOOST_CHECK( current == expected_current );
   BOOST_CHECK( legacy != current );
}

BOOST_AUTO_TEST_CASE( authority_round_trip_both_formats )
{
   authority a;
   a.weight_threshold = 1;
   a.key_auths[ public_key_type() ] = 1;
   a.address_auths[ address() ] = 1;

   // Both formats produce compatible bytes for pq-free data
   std::vector<char> legacy, current;
   {
      fc::raw::scoped_pq_format fmt( fc::raw::pq_format::legacy );
      legacy = fc::raw::pack( a );
   }
   {
      fc::raw::scoped_pq_format fmt( fc::raw::pq_format::current );
      current = fc::raw::pack( a );
   }
   // current adds empty pq_key_auths count -> formats differ
   BOOST_CHECK( legacy != current );

   // Round-trips
   {
      fc::raw::scoped_pq_format fmt( fc::raw::pq_format::legacy );
      authority b = fc::raw::unpack<authority>( legacy );
      BOOST_CHECK( b.weight_threshold == a.weight_threshold );
      BOOST_CHECK( b.key_auths == a.key_auths );
      BOOST_CHECK( b.address_auths == a.address_auths );
      BOOST_CHECK( b.pq_key_auths.empty() );
   }
   {
      fc::raw::scoped_pq_format fmt( fc::raw::pq_format::current );
      authority b = fc::raw::unpack<authority>( current );
      BOOST_CHECK( b == a );
   }
}

BOOST_AUTO_TEST_CASE( signed_transaction_dual_format )
{
   signed_transaction tx;
   tx.ref_block_num = 10;
   tx.ref_block_prefix = 0xdeadbeef;
   tx.expiration = fc::time_point_sec( 1000000 );
   operation op = transfer_operation();
   op.get<transfer_operation>().from = account_id_type( 100 );
   op.get<transfer_operation>().to = account_id_type( 101 );
   tx.operations.push_back( op );
   tx.signatures.push_back( signature_type() );
   tx.pq_signatures.push_back( pq_signature() );

   // Legacy: base + signatures, no pq_signatures
   std::vector<char> legacy;
   {
      fc::raw::scoped_pq_format fmt( fc::raw::pq_format::legacy );
      legacy = fc::raw::pack( tx );
   }
   std::vector<char> expected_legacy = concat_vec(
      pack_field( static_cast<const transaction&>( tx ) ),
      pack_field( tx.signatures ) );
   BOOST_CHECK( legacy == expected_legacy );

   // Current: base + signatures + pq_signatures
   std::vector<char> current;
   {
      fc::raw::scoped_pq_format fmt( fc::raw::pq_format::current );
      current = fc::raw::pack( tx );
   }
   // .value, not the field itself: pq_signatures is an fc::pq_gated, so packing the field
   // here -- outside the scoped `current` block above -- would correctly emit nothing. Naming
   // the wrapped vector states what is being asserted: the bytes the gated field contributes
   // when the format says it is present.
   std::vector<char> expected_current = concat_vec(
      pack_field( static_cast<const transaction&>( tx ) ),
      pack_field( tx.signatures ),
      pack_field( tx.pq_signatures.value ) );
   BOOST_CHECK( current == expected_current );
   BOOST_CHECK( legacy != current );

   // pack_size must agree with the actual byte count
   {
      fc::raw::scoped_pq_format fmt( fc::raw::pq_format::legacy );
      BOOST_CHECK( fc::raw::pack_size( tx ) == legacy.size() );
   }
   {
      fc::raw::scoped_pq_format fmt( fc::raw::pq_format::current );
      BOOST_CHECK( fc::raw::pack_size( tx ) == current.size() );
   }

   // Round-trips
   {
      fc::raw::scoped_pq_format fmt( fc::raw::pq_format::legacy );
      signed_transaction b = fc::raw::unpack<signed_transaction>( legacy );
      BOOST_CHECK( b.pq_signatures.empty() );
      BOOST_CHECK( b.signatures == tx.signatures );
      BOOST_CHECK( b.operations.size() == tx.operations.size() );
   }
   {
      fc::raw::scoped_pq_format fmt( fc::raw::pq_format::current );
      signed_transaction b = fc::raw::unpack<signed_transaction>( current );
      BOOST_CHECK( same_pq_signatures( b.pq_signatures, tx.pq_signatures ) );
      BOOST_CHECK( b.signatures == tx.signatures );
   }
}

BOOST_AUTO_TEST_CASE( signed_transaction_digest_depends_on_format )
{
   signed_transaction tx;
   tx.ref_block_num = 1;
   tx.expiration = fc::time_point_sec( 2000000 );
   tx.operations.push_back( operation( transfer_operation() ) );
   tx.pq_signatures.push_back( pq_signature() );

   digest_type legacy_digest, current_digest;
   {
      fc::raw::scoped_pq_format fmt( fc::raw::pq_format::legacy );
      digest_type::encoder enc;
      fc::raw::pack( enc, tx );
      legacy_digest = enc.result();
   }
   {
      fc::raw::scoped_pq_format fmt( fc::raw::pq_format::current );
      digest_type::encoder enc;
      fc::raw::pack( enc, tx );
      current_digest = enc.result();
   }
   BOOST_CHECK( legacy_digest != current_digest );

   // Without pq content the digests differ too (current has empty count)
   signed_transaction clean = tx;
   clean.pq_signatures.clear();
   digest_type clean_legacy, clean_current;
   {
      fc::raw::scoped_pq_format fmt( fc::raw::pq_format::legacy );
      digest_type::encoder enc;
      fc::raw::pack( enc, clean );
      clean_legacy = enc.result();
   }
   {
      fc::raw::scoped_pq_format fmt( fc::raw::pq_format::current );
      digest_type::encoder enc;
      fc::raw::pack( enc, clean );
      clean_current = enc.result();
   }
   BOOST_CHECK( clean_legacy != clean_current );
}

BOOST_AUTO_TEST_CASE( signed_block_dual_format )
{
   signed_block b;
   b.previous = block_id_type();
   b.timestamp = fc::time_point_sec( 3000000 );
   b.witness = witness_id_type( 1 );
   b.witness_signature = signature_type();
   b.witness_pq_signature = pq_signature();

   signed_transaction tx;
   tx.ref_block_num = 2;
   tx.expiration = fc::time_point_sec( 3000001 );
   tx.operations.push_back( operation( transfer_operation() ) );
   tx.signatures.push_back( signature_type() );
   tx.pq_signatures.push_back( pq_signature() );
   b.transactions.push_back( processed_transaction( tx ) );

   // Legacy: header without witness_pq_signature
   std::vector<char> legacy, expected_legacy;
   {
      fc::raw::scoped_pq_format fmt( fc::raw::pq_format::legacy );
      legacy = fc::raw::pack( b );
      expected_legacy = concat_vec(
         pack_field( static_cast<const block_header&>( b ) ),
         pack_field( b.witness_signature ),
         pack_field( b.transactions ) );
   }
   BOOST_CHECK( legacy == expected_legacy );

   // Current: header + witness_signature + witness_pq_signature
   std::vector<char> current, expected_current;
   {
      fc::raw::scoped_pq_format fmt( fc::raw::pq_format::current );
      current = fc::raw::pack( b );
      expected_current = concat_vec(
         pack_field( static_cast<const block_header&>( b ) ),
         pack_field( b.witness_signature ),
         pack_field( b.witness_pq_signature ),
         pack_field( b.transactions ) );
   }
   BOOST_CHECK( current == expected_current );

   // Round-trips
   {
      fc::raw::scoped_pq_format fmt( fc::raw::pq_format::legacy );
      signed_block b2 = fc::raw::unpack<signed_block>( legacy );
      BOOST_CHECK( b2.witness_pq_signature.valid() == false );
      BOOST_CHECK( fc::raw::pack( b2 ) == legacy );
   }
   {
      fc::raw::scoped_pq_format fmt( fc::raw::pq_format::current );
      signed_block b2 = fc::raw::unpack<signed_block>( current );
      BOOST_CHECK( b2.witness_pq_signature.valid() == true );
      BOOST_CHECK( fc::raw::pack( b2 ) == current );
   }
}


/**
 * An address auth must be satisfiable by a post-quantum key.
 *
 * address_auths names a 20-byte hash, not a key type, but the signature checker only ever
 * derived addresses from secp256k1 keys -- so an account protected by address_auths could
 * never be moved to post-quantum keys at all. It would stay quantum-vulnerable no matter what
 * else it did, and nothing in the interface said so.
 *
 * The derivation is the classic one, ripemd160(sha512(bytes)), over the key's identity: the
 * algorithm followed by the raw public key. The algorithm is in there so two parameter sets
 * cannot land on the same address, and the optional legacy companion key is not, so attaching
 * one does not move where the PQ key lives.
 */
BOOST_AUTO_TEST_CASE( an_address_auth_can_be_satisfied_by_a_pq_key )
{
   const auto priv = fc::pq_private_key::generate( fc::pq_algorithm::ml_dsa_65 );
   const pq_public_key_type pub( priv.get_public_key() );
   const address a( pub );

   // Deterministic, and distinct from a different key's.
   BOOST_CHECK( address( pub ) == a );
   const auto other = fc::pq_private_key::generate( fc::pq_algorithm::ml_dsa_65 );
   BOOST_CHECK( address( pq_public_key_type( other.get_public_key() ) ) != a );

   // Attaching a legacy companion key must NOT move the address: it is a migration
   // convenience, not part of which key this is.
   pq_public_key_type hybrid = pub;
   hybrid.legacy = fc::ecc::private_key::generate().get_public_key().serialize();
   BOOST_CHECK( address( hybrid ) == a );

   // The parameter set is part of the identity, so the same bytes under a different
   // algorithm must land somewhere else.
   pq_public_key_type relabelled = pub;
   relabelled.algorithm = fc::pq_algorithm::ml_dsa_87;
   BOOST_CHECK( address( relabelled ) != a );

   // Now the authority itself. An account whose active authority is an address_auth
   // naming that address must be movable by the PQ key alone.
   authority auth;
   auth.weight_threshold = 1;
   auth.address_auths[ a ] = 1;

   transfer_operation op;
   op.from = account_id_type(17);
   op.to   = account_id_type(18);
   op.amount = asset( 1 );

   const chain_id_type chain_id = fc::sha256::hash( std::string( "pq-address-test" ) );
   signed_transaction tx;
   tx.operations.push_back( op );
   {
      fc::raw::scoped_pq_format fmt( fc::raw::pq_format::current );
      tx.sign_pq( priv, chain_id, fc::raw::pq_format::current );
      BOOST_REQUIRE_EQUAL( tx.pq_signatures.size(), 1u );

      const auto get_active = [&auth]( account_id_type ) { return &auth; };
      const auto get_owner  = [&auth]( account_id_type ) { return &auth; };
      const auto get_custom = []( account_id_type, const operation&, rejected_predicate_map* )
                              { return vector<authority>(); };

      BOOST_CHECK_NO_THROW( tx.verify_authority( chain_id, get_active, get_owner, get_custom,
                                                 true, true ) );

      // The same authority is NOT satisfied by an unrelated PQ key.
      signed_transaction wrong;
      wrong.operations.push_back( op );
      wrong.sign_pq( other, chain_id, fc::raw::pq_format::current );
      BOOST_CHECK_THROW( wrong.verify_authority( chain_id, get_active, get_owner, get_custom,
                                                    true, true ), fc::exception );

      flat_set<pq_public_key_type> pq_keys;
      for( const auto& sig : tx.pq_signatures ) pq_keys.insert( sig.key );

      // And with allow_pq off -- which is what the pre-hardfork path passes -- the PQ
      // signature is not considered at all, so the very same transaction fails.
      BOOST_CHECK_THROW(
         graphene::protocol::verify_authority( tx.operations, flat_set<public_key_type>(),
                                               get_active, get_owner, get_custom, true, true,
                                               GRAPHENE_MAX_SIG_CHECK_DEPTH, false,
                                               false /* allow_pq */,
                                               flat_set<account_id_type>(),
                                               flat_set<account_id_type>(),
                                               pq_keys ),
         fc::exception );
   }
}

BOOST_AUTO_TEST_SUITE_END()
