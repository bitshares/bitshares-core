/*
 * Cryptographic property tests for the vendored ML-DSA (FIPS 204) and ML-KEM (FIPS 203)
 * primitives.
 *
 * Everything else in this branch's test suite exercises how post-quantum keys are carried,
 * serialized and gated. Nothing checked that the primitives underneath actually behave like
 * the algorithms they claim to be, and no NIST known-answer vectors are vendored. An
 * implementation wired to the wrong parameter set, or one whose verify() always succeeded,
 * would have passed every test in the repository.
 *
 * These cases do not replace validation against the official FIPS 203/204 vectors, which is
 * the only thing that proves conformance and which should be added when those vectors can be
 * obtained. What they do is catch the integration mistakes that are actually likely: a wrong
 * parameter set, truncated key material, a verify that ignores its arguments, signatures
 * accepted across algorithms or across keys, and a KEM whose two sides disagree.
 */

#include <boost/test/unit_test.hpp>

#include <fc/crypto/pqc.hpp>
#include <fc/crypto/sha256.hpp>

#include <set>
#include <string>

BOOST_AUTO_TEST_SUITE( pq_crypto_tests )

namespace {
   fc::sha256 digest_of( const std::string& s ) { return fc::sha256::hash( s.data(), s.size() ); }
}

/// The parameter sets are fixed by FIPS 203/204. If these ever disagree with the standard,
/// the wrong variant has been wired in and every key and signature on chain is the wrong size.
BOOST_AUTO_TEST_CASE( parameter_sets_match_the_standard )
{
   using fc::pqc_sizes;
   // FIPS 204, ML-DSA
   BOOST_CHECK_EQUAL( pqc_sizes::public_key_size( fc::pq_algorithm::ml_dsa_44 ), 1312u );
   BOOST_CHECK_EQUAL( pqc_sizes::public_key_size( fc::pq_algorithm::ml_dsa_65 ), 1952u );
   BOOST_CHECK_EQUAL( pqc_sizes::public_key_size( fc::pq_algorithm::ml_dsa_87 ), 2592u );
   BOOST_CHECK_EQUAL( pqc_sizes::signature_size ( fc::pq_algorithm::ml_dsa_65 ), 3309u );
   // FIPS 203, ML-KEM
   BOOST_CHECK_EQUAL( pqc_sizes::public_key_size( fc::pq_algorithm::ml_kem_768 ), 1184u );
   BOOST_CHECK_EQUAL( pqc_sizes::ciphertext_size( fc::pq_algorithm::ml_kem_768 ), 1088u );
   BOOST_CHECK_EQUAL( pqc_sizes::shared_secret_size( fc::pq_algorithm::ml_kem_768 ), 32u );
}

/// Generated key material must be the declared length and must not be constant.
BOOST_AUTO_TEST_CASE( keygen_produces_distinct_well_formed_keys )
{
   const auto alg = fc::pq_algorithm::ml_dsa_65;
   std::set<std::string> seen;
   for( int i = 0; i < 4; ++i )
   {
      auto kp = fc::pq_generate_keypair( alg );
      BOOST_REQUIRE( kp.valid() );
      BOOST_CHECK_EQUAL( kp.pub.data().size(), fc::pqc_sizes::public_key_size( alg ) );
      seen.insert( std::string( kp.pub.data().begin(), kp.pub.data().end() ) );
   }
   BOOST_CHECK_MESSAGE( seen.size() == 4, "keygen returned a repeated public key" );
}

/// A signature must verify under the key that made it, and under no other.
BOOST_AUTO_TEST_CASE( signatures_verify_only_under_their_own_key )
{
   auto a = fc::pq_generate_keypair( fc::pq_algorithm::ml_dsa_65 );
   auto b = fc::pq_generate_keypair( fc::pq_algorithm::ml_dsa_65 );
   const auto d = digest_of( "a message to sign" );

   auto sig = a.priv.sign( d );
   BOOST_CHECK_EQUAL( sig.size(), fc::pqc_sizes::signature_size( fc::pq_algorithm::ml_dsa_65 ) );

   BOOST_CHECK( a.pub.verify( d, sig ) );
   BOOST_CHECK_MESSAGE( !b.pub.verify( d, sig ),
                        "a signature verified under an unrelated key" );
}

/// Tampering with either the message or the signature must be rejected. A verify() that
/// ignored its arguments -- the failure mode that makes every other test meaningless --
/// cannot pass this.
BOOST_AUTO_TEST_CASE( tampering_is_rejected )
{
   auto kp = fc::pq_generate_keypair( fc::pq_algorithm::ml_dsa_65 );
   const auto d = digest_of( "the original message" );
   auto sig = kp.priv.sign( d );
   BOOST_REQUIRE( kp.pub.verify( d, sig ) );

   // a different message
   BOOST_CHECK( !kp.pub.verify( digest_of( "the original messagf" ), sig ) );

   // one flipped bit, at each end and in the middle of the signature
   for( size_t pos : { size_t(0), sig.size() / 2, sig.size() - 1 } )
   {
      auto bad = sig;
      bad[pos] = static_cast<char>( bad[pos] ^ 0x01 );
      BOOST_CHECK_MESSAGE( !kp.pub.verify( d, bad ),
                           "a signature with a flipped bit at offset " << pos << " verified" );
   }

   // truncated, and empty
   BOOST_CHECK( !kp.pub.verify( d, std::vector<char>( sig.begin(), sig.end() - 1 ) ) );
   BOOST_CHECK( !kp.pub.verify( d, std::vector<char>() ) );
}

/// A signature from one parameter set must not verify under another. Accepting across
/// algorithms would let a signer downgrade to the weakest set the code supports.
BOOST_AUTO_TEST_CASE( signatures_do_not_cross_parameter_sets )
{
   auto weak   = fc::pq_generate_keypair( fc::pq_algorithm::ml_dsa_44 );
   auto strong = fc::pq_generate_keypair( fc::pq_algorithm::ml_dsa_87 );
   const auto d = digest_of( "cross-set check" );

   auto weak_sig = weak.priv.sign( d );
   BOOST_CHECK( weak.pub.verify( d, weak_sig ) );
   BOOST_CHECK_MESSAGE( !strong.pub.verify( d, weak_sig ),
                        "an ML-DSA-44 signature verified under an ML-DSA-87 key" );
}

/// Seeded generation must be reproducible, and distinct seeds must give distinct keys --
/// the property a wallet relies on when it derives a key it expects to recreate later.
BOOST_AUTO_TEST_CASE( seeded_keygen_is_deterministic )
{
   const auto alg = fc::pq_algorithm::ml_dsa_65;
   fc::sha256 seed_a = digest_of( "seed one" );
   fc::sha256 seed_b = digest_of( "seed two" );

   auto a1 = fc::pq_generate_keypair_from_seed( alg, seed_a );
   auto a2 = fc::pq_generate_keypair_from_seed( alg, seed_a );
   auto b1 = fc::pq_generate_keypair_from_seed( alg, seed_b );
   BOOST_REQUIRE( a1.valid() && a2.valid() && b1.valid() );

   BOOST_CHECK( a1.pub == a2.pub );
   BOOST_CHECK_MESSAGE( !( a1.pub == b1.pub ), "two different seeds produced the same key" );

   // and the reproduced key is functionally the same key, not merely equal bytes
   const auto d = digest_of( "signed by the reproduced key" );
   BOOST_CHECK( a1.pub.verify( d, a2.priv.sign( d ) ) );
}

/// Both sides of the KEM must arrive at the same secret, and a different key must not.
BOOST_AUTO_TEST_CASE( kem_encapsulation_round_trips )
{
   const auto alg = fc::pq_algorithm::ml_kem_768;
   auto kp    = fc::pq_kem_generate( alg );
   auto other = fc::pq_kem_generate( alg );
   BOOST_REQUIRE( kp.valid() && other.valid() );
   BOOST_CHECK_EQUAL( kp.pk.size(), fc::pqc_sizes::public_key_size( alg ) );

   auto enc = fc::pq_kem_encapsulate( alg, kp.pk );
   BOOST_REQUIRE( enc.valid );
   BOOST_CHECK_EQUAL( enc.ciphertext.size(), fc::pqc_sizes::ciphertext_size( alg ) );
   BOOST_CHECK_EQUAL( enc.shared_secret.size(), fc::pqc_sizes::shared_secret_size( alg ) );

   auto recovered = fc::pq_kem_decapsulate( alg, kp.sk, enc.ciphertext );
   BOOST_CHECK_MESSAGE( recovered == enc.shared_secret,
                        "encapsulator and decapsulator derived different secrets" );

   // ML-KEM is designed to return a pseudorandom secret rather than fail on a foreign
   // ciphertext, so the requirement is that it does not match, not that it throws.
   auto wrong = fc::pq_kem_decapsulate( alg, other.sk, enc.ciphertext );
   BOOST_CHECK_MESSAGE( wrong != enc.shared_secret,
                        "decapsulating with an unrelated secret key recovered the secret" );
}

/// Two encapsulations to the same public key must not produce the same secret; a KEM whose
/// randomness had been stubbed out would silently reuse session keys.
BOOST_AUTO_TEST_CASE( kem_secrets_are_not_reused )
{
   const auto alg = fc::pq_algorithm::ml_kem_768;
   auto kp = fc::pq_kem_generate( alg );
   auto one = fc::pq_kem_encapsulate( alg, kp.pk );
   auto two = fc::pq_kem_encapsulate( alg, kp.pk );
   BOOST_REQUIRE( one.valid && two.valid );
   BOOST_CHECK_MESSAGE( one.shared_secret != two.shared_secret,
                        "two encapsulations produced the same shared secret" );
   BOOST_CHECK( one.ciphertext != two.ciphertext );
}

BOOST_AUTO_TEST_SUITE_END()
