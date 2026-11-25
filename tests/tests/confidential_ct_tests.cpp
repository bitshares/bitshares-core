#include <boost/test/unit_test.hpp>

#include <graphene/chain/confidential_object.hpp>
#include <graphene/chain/hardfork.hpp>
#include <graphene/protocol/confidential.hpp>

#include <bts_ct/bulletproofs_plus.hpp>
#include <bts_ct/clsag.hpp>

#include <fc/crypto/base58.hpp>

#include "../common/database_fixture.hpp"

#include <cstring>
#include <map>

using namespace graphene::chain;
using namespace graphene::protocol;
using graphene::chain::test::set_expiration;

namespace {

struct coin {
   fc::ecc::private_key spend;
   fc::sha256 blind;
   uint64_t value = 0;
   commitment_type commitment;
   authority owner;
   btsct::Scalar spend_scalar;
   btsct::Scalar blind_scalar;
};

stealth_confirmation good_memo()
{
   stealth_confirmation memo;
   memo.one_time_key = public_key_type( fc::ecc::private_key::generate().get_public_key() );
   memo.encrypted_memo.assign( 48, 'm' );
   return memo;
}

btsct::Scalar must_scalar( const fc::sha256& bytes )
{
   btsct::Scalar out;
   BOOST_REQUIRE( btsct::Scalar::from_bytes( reinterpret_cast<const uint8_t*>( bytes.data() ), 32, out ) );
   return out;
}

coin make_coin( uint64_t value )
{
   coin c;
   c.spend = fc::ecc::private_key::generate();
   c.blind = fc::sha256::hash( c.spend.get_secret() );
   c.spend_scalar = must_scalar( c.spend.get_secret() );
   c.blind_scalar = must_scalar( c.blind );
   c.value = value;
   c.commitment = fc::ecc::blind( c.blind, value );
   c.owner = authority( 1, public_key_type( c.spend.get_public_key() ), 1 );
   auto ser = c.spend.get_public_key().serialize();
   btsct::Point on_chain;
   BOOST_REQUIRE( btsct::Point::from_compressed( ser.data(), ser.size(), on_chain ) );
   BOOST_REQUIRE( on_chain == btsct::Point::generator() * c.spend_scalar );
   btsct::Point committed = btsct::Point::commit( c.blind_scalar, value );
   btsct::Point from_fc;
   BOOST_REQUIRE( btsct::Point::from_compressed( c.commitment.data(), c.commitment.size(), from_fc ) );
   BOOST_REQUIRE( committed == from_fc );
   return c;
}

blind_output to_output( const coin& c, bool with_memo, bool with_borromean )
{
   blind_output out;
   out.commitment = c.commitment;
   out.owner = c.owner;
   if( with_memo )
      out.stealth_memo = good_memo();
   if( with_borromean )
   {
      auto nonce = fc::sha256::hash( c.blind );
      out.range_proof = fc::ecc::range_proof_sign( 0, out.commitment, c.blind, nonce, 0, 0, c.value );
   }
   return out;
}

void sort_outputs( vector<blind_output>& outputs )
{
   std::sort( outputs.begin(), outputs.end(),
              []( const blind_output& a, const blind_output& b ) { return a.commitment < b.commitment; } );
}

void attach_bulletproof( vector<blind_output>& outputs, const vector<coin>& coins )
{
   std::map<commitment_type, const coin*> by_c;
   for( const auto& c : coins )
      by_c[c.commitment] = &c;
   vector<uint64_t> values;
   vector<btsct::Scalar> blinds;
   for( const auto& out : outputs )
   {
      values.push_back( by_c[out.commitment]->value );
      blinds.push_back( by_c[out.commitment]->blind_scalar );
   }
   auto proof = btsct::bulletproof_plus_prove( values, blinds );
   auto bytes = btsct::bulletproof_plus_serialize( proof );
   for( auto& out : outputs )
      out.range_proof.clear();
   outputs.front().range_proof.assign( reinterpret_cast<const char*>( bytes.data() ),
                                       reinterpret_cast<const char*>( bytes.data() ) + bytes.size() );
}

asset core_asset( share_type amount )
{
   return asset( amount, asset_id_type() );
}

} // namespace

BOOST_FIXTURE_TEST_SUITE( confidential_ct_tests, database_fixture )

BOOST_AUTO_TEST_CASE( stealth_memo_and_bulletproof_hardfork_boundary )
{ try {
   ACTORS( (alice) );
   transfer( account_id_type()(db), alice, core_asset( 100000000 ) );
   const asset_object& core = asset_id_type()(db);

   coin early = make_coin( 1000 );
   transfer_to_blind_operation op;
   op.from = alice.id;
   op.amount = core.amount( 1000 );
   op.blinding_factor = early.blind;
   op.outputs = { to_output( early, false, false ) };
   op.fee = db.current_fee_schedule().calculate_fee( op );
   trx.operations = { op };
   set_expiration( db, trx );
   sign( trx, alice_private_key );
   PUSH_TX( db, trx );
   trx.clear();

   generate_blocks( HARDFORK_BSIP_CT1_TIME );
   BOOST_REQUIRE( db.head_block_time() >= HARDFORK_BSIP_CT1_TIME );
   BOOST_REQUIRE( db.head_block_time() < HARDFORK_BSIP_CT2_TIME );

   coin mid = make_coin( 2000 );
   transfer_to_blind_operation missing;
   missing.from = alice.id;
   missing.amount = core.amount( 2000 );
   missing.blinding_factor = mid.blind;
   missing.outputs = { to_output( mid, false, true ) };
   missing.fee = db.current_fee_schedule().calculate_fee( missing );
   trx.operations = { missing };
   set_expiration( db, trx );
   sign( trx, alice_private_key );
   BOOST_REQUIRE_THROW( PUSH_TX( db, trx ), fc::exception );
   trx.clear();

   transfer_to_blind_operation linked = missing;
   linked.outputs[0].stealth_memo = good_memo();
   linked.outputs[0].stealth_memo->to = public_key_type( mid.spend.get_public_key() );
   linked.fee = db.current_fee_schedule().calculate_fee( linked );
   trx.operations = { linked };
   set_expiration( db, trx );
   sign( trx, alice_private_key );
   BOOST_REQUIRE_THROW( PUSH_TX( db, trx ), fc::exception );
   trx.clear();

   transfer_to_blind_operation huge = missing;
   huge.outputs[0].stealth_memo = good_memo();
   huge.outputs[0].stealth_memo->encrypted_memo.assign( GRAPHENE_STEALTH_MEMO_MAX_SIZE + 1, 'Z' );
   huge.fee = db.current_fee_schedule().calculate_fee( huge );
   trx.operations = { huge };
   set_expiration( db, trx );
   sign( trx, alice_private_key );
   BOOST_REQUIRE_THROW( PUSH_TX( db, trx ), fc::exception );
   trx.clear();

   transfer_to_blind_operation ok = missing;
   ok.outputs[0].stealth_memo = good_memo();
   ok.fee = db.current_fee_schedule().calculate_fee( ok );
   trx.operations = { ok };
   set_expiration( db, trx );
   sign( trx, alice_private_key );
   PUSH_TX( db, trx );
   trx.clear();

   generate_blocks( HARDFORK_BSIP_CT2_TIME );
   coin late = make_coin( 3000 );
   transfer_to_blind_operation borromean;
   borromean.from = alice.id;
   borromean.amount = core.amount( 3000 );
   borromean.blinding_factor = late.blind;
   borromean.outputs = { to_output( late, true, true ) };
   borromean.fee = db.current_fee_schedule().calculate_fee( borromean );
   trx.operations = { borromean };
   set_expiration( db, trx );
   sign( trx, alice_private_key );
   BOOST_REQUIRE_THROW( PUSH_TX( db, trx ), fc::exception );
   trx.clear();

   transfer_to_blind_operation bp = borromean;
   attach_bulletproof( bp.outputs, { late } );
   bp.fee = db.current_fee_schedule().calculate_fee( bp );
   auto packed = fc::raw::pack( bp );
   BOOST_CHECK( packed == fc::raw::pack( bp ) );
   trx.operations = { bp };
   set_expiration( db, trx );
   sign( trx, alice_private_key );
   PUSH_TX( db, trx );
   trx.clear();

   transfer_to_blind_operation empty_proof = bp;
   empty_proof.outputs[0].range_proof.clear();
   empty_proof.fee = db.current_fee_schedule().calculate_fee( empty_proof );
   trx.operations = { empty_proof };
   set_expiration( db, trx );
   sign( trx, alice_private_key );
   BOOST_REQUIRE_THROW( PUSH_TX( db, trx ), fc::exception );
   trx.clear();

   transfer_to_blind_operation truncated = bp;
   truncated.outputs[0].range_proof.resize( 8 );
   truncated.fee = db.current_fee_schedule().calculate_fee( truncated );
   trx.operations = { truncated };
   set_expiration( db, trx );
   sign( trx, alice_private_key );
   BOOST_REQUIRE_THROW( PUSH_TX( db, trx ), fc::exception );
   trx.clear();

   transfer_to_blind_operation forged = bp;
   forged.outputs[0].range_proof[30] ^= 0x01;
   forged.fee = db.current_fee_schedule().calculate_fee( forged );
   trx.operations = { forged };
   set_expiration( db, trx );
   sign( trx, alice_private_key );
   BOOST_REQUIRE_THROW( PUSH_TX( db, trx ), fc::exception );
   trx.clear();

   // Commitment that cancels a real one is not in range.
   coin neg = late;
   neg.value = 1;
   neg.commitment = fc::ecc::blind( neg.blind, 1 );
   transfer_to_blind_operation mismatch = bp;
   mismatch.outputs[0].commitment = neg.commitment;
   mismatch.blinding_factor = neg.blind;
   mismatch.amount = core.amount( 1 );
   mismatch.fee = db.current_fee_schedule().calculate_fee( mismatch );
   trx.operations = { mismatch };
   set_expiration( db, trx );
   sign( trx, alice_private_key );
   BOOST_REQUIRE_THROW( PUSH_TX( db, trx ), fc::exception );
   trx.clear();

} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE( ring_transfer_double_spend_and_replay )
{ try {
   ACTORS( (alice)(bob) );
   transfer( account_id_type()(db), alice, core_asset( 1000000000 ) );
   generate_blocks( HARDFORK_BSIP_CT3_TIME );
   const asset_object& core = asset_id_type()(db);

   constexpr uint64_t each = 50000000;
   vector<coin> coins;
   for( uint32_t i = 0; i < GRAPHENE_RING_SIZE; ++i )
      coins.push_back( make_coin( each ) );
   // The default transaction size cap is 2048 bytes, so deposit four outputs at a time.
   for( uint32_t base = 0; base < coins.size(); base += 4 )
   {
      vector<coin> batch( coins.begin() + base, coins.begin() + base + 4 );
      vector<blind_output> outputs;
      vector<blind_factor_type> blinds;
      share_type total = 0;
      for( const auto& c : batch )
      {
         outputs.push_back( to_output( c, true, false ) );
         blinds.push_back( c.blind );
         total += c.value;
      }
      sort_outputs( outputs );
      attach_bulletproof( outputs, batch );
      transfer_to_blind_operation deposit;
      deposit.from = alice.id;
      deposit.amount = core.amount( total );
      deposit.blinding_factor = fc::ecc::blind_sum( blinds, blinds.size() );
      deposit.outputs = outputs;
      deposit.fee = db.current_fee_schedule().calculate_fee( deposit );
      trx.operations = { deposit };
      set_expiration( db, trx );
      sign( trx, alice_private_key );
      PUSH_TX( db, trx );
      trx.clear();
   }

   const auto supply_after_deposit =
      core.dynamic_asset_data_id(db).confidential_supply;

   // Legacy spend is closed.
   transfer_from_blind_operation legacy;
   legacy.fee = core.amount( 1 );
   legacy.amount = core.amount( each - 1 );
   legacy.to = bob.id;
   legacy.blinding_factor = coins[0].blind;
   legacy.inputs.push_back( { coins[0].commitment, coins[0].owner } );
   trx.operations = { legacy };
   BOOST_REQUIRE_THROW( PUSH_TX( db, trx ), fc::exception );
   trx.clear();

   // Build a ring in commitment order. The real coin is coins[3].
   vector<coin> ordered = coins;
   std::sort( ordered.begin(), ordered.end(),
              []( const coin& a, const coin& b ) { return a.commitment < b.commitment; } );
   const size_t real = 3 % ordered.size();
   // Pick a stable real index: the coin whose spend key we use is ordered[real].
   const coin& spent = ordered[real];

   btsct::Point real_pub;
   auto real_ser = spent.spend.get_public_key().serialize();
   BOOST_REQUIRE( btsct::Point::from_compressed( real_ser.data(), real_ser.size(), real_pub ) );
   btsct::Point image = btsct::clsag_key_image( real_pub, spent.spend_scalar );
   uint8_t image_bytes[33];
   image.to_compressed( image_bytes );

   fc::sha256 pseudo_blind = fc::sha256::hash( fc::sha256::hash( spent.blind ) );
   btsct::Scalar pseudo_scalar = must_scalar( pseudo_blind );
   commitment_type pseudo = fc::ecc::blind( pseudo_blind, spent.value );
   btsct::Scalar z = spent.blind_scalar - pseudo_scalar;

   ring_input rin;
   rin.pseudo_commitment = pseudo;
   rin.members.reserve( ordered.size() );
   vector<btsct::Point> pubs;
   vector<btsct::Point> commits;
   for( const auto& c : ordered )
   {
      rin.members.push_back( c.commitment );
      auto ser = c.spend.get_public_key().serialize();
      btsct::Point pub, commit;
      BOOST_REQUIRE( btsct::Point::from_compressed( ser.data(), ser.size(), pub ) );
      BOOST_REQUIRE( btsct::Point::from_compressed( c.commitment.data(), c.commitment.size(), commit ) );
      pubs.push_back( pub );
      commits.push_back( commit );
   }
   memcpy( rin.key_image.data(), image_bytes, 33 );
   btsct::Point pseudo_point;
   BOOST_REQUIRE( btsct::Point::from_compressed( pseudo.data(), pseudo.size(), pseudo_point ) );

   // Fee is fixed before the signature. The CLSAG message includes the fee and not
   // the signature bytes, so filling `clsag` afterwards does not invalidate it.
   // Same blinding factor on the pseudo-output and the change output makes the
   // public fee the only value that leaves the commitment equation.
   const int64_t fee_value = 5000000;
   const uint64_t change_value = spent.value - static_cast<uint64_t>( fee_value );
   coin change;
   change.spend = fc::ecc::private_key::generate();
   change.owner = authority( 1, public_key_type( change.spend.get_public_key() ), 1 );
   change.blind = pseudo_blind;
   change.blind_scalar = pseudo_scalar;
   change.value = change_value;
   change.commitment = fc::ecc::blind( change.blind, change.value );

   ring_blind_transfer_operation ring;
   ring.inputs = { rin };
   ring.fee = core.amount( fee_value );
   ring_output rout;
   rout.commitment = change.commitment;
   rout.owner = change.owner;
   rout.stealth_memo = good_memo();
   ring.outputs = { rout };
   {
      auto proof = btsct::bulletproof_plus_prove( { change.value }, { change.blind_scalar } );
      auto bytes = btsct::bulletproof_plus_serialize( proof );
      ring.range_proof.assign( reinterpret_cast<const char*>( bytes.data() ),
                               reinterpret_cast<const char*>( bytes.data() ) + bytes.size() );
   }
   vector<commitment_type> out_c{ rout.commitment };
   auto digest = ring_signature_message( ring.fee, asset(), account_id_type(), blind_factor_type(),
                                         ring.inputs, out_c, ring.range_proof );
   uint8_t msg[32];
   memcpy( msg, digest.data(), 32 );
   auto sig = btsct::clsag_sign( msg, pubs, spent.spend_scalar, commits, z, pseudo_point, real );
   auto sig_bytes = btsct::clsag_serialize( sig );
   ring.inputs[0].clsag.assign( reinterpret_cast<const char*>( sig_bytes.data() ),
                                reinterpret_cast<const char*>( sig_bytes.data() ) + sig_bytes.size() );
   BOOST_REQUIRE( db.current_fee_schedule().calculate_fee( ring ).amount <= ring.fee.amount );
   ring.validate();
   set_expiration( db, trx );
   trx.operations = { ring };
   PUSH_TX( db, trx );
   trx.clear();

   const auto& by_commit = db.get_index_type<blinded_balance_index>().indices().get<by_commitment>();
   BOOST_CHECK( by_commit.find( spent.commitment ) != by_commit.end() );
   BOOST_CHECK( by_commit.find( change.commitment ) != by_commit.end() );
   const auto& images = db.get_index_type<key_image_index>().indices().get<by_key_image>();
   commitment_type image_id;
   memcpy( image_id.data(), image_bytes, 33 );
   BOOST_CHECK( images.find( image_id ) != images.end() );
   BOOST_CHECK_EQUAL( core.dynamic_asset_data_id(db).confidential_supply.value,
                      (supply_after_deposit - ring.fee.amount).value );

   // Double spend of the same key image, even with a fresh signature, is rejected.
   ring_blind_transfer_operation again = ring;
   set_expiration( db, trx );
   trx.operations = { again };
   BOOST_REQUIRE_THROW( PUSH_TX( db, trx ), fc::exception );
   trx.clear();

   // Produce a signed block and replay it. skip_nothing makes the harness sign the
   // block; the default skip leaves a zero witness signature, which a block dated
   // after the machine clock is required to validate.
   signed_block blk = generate_block( database::skip_nothing );
   db.pop_block();
   PUSH_BLOCK( db, blk, database::skip_nothing );
   BOOST_CHECK( db.get_index_type<key_image_index>().indices().get<by_key_image>().find( image_id )
                != db.get_index_type<key_image_index>().indices().get<by_key_image>().end() );

   const auto& listed = db.get_index_type<blinded_balance_index>().indices().get<by_asset_commitment>();
   BOOST_CHECK( listed.lower_bound( boost::make_tuple( asset_id_type(), commitment_type() ) ) != listed.end() );

} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE( ring_fee_and_malformed )
{ try {
   ring_blind_transfer_operation::fee_params_t k;
   ring_blind_transfer_operation op;
   op.inputs.resize( 1 );
   op.inputs[0].members.resize( GRAPHENE_RING_SIZE );
   auto low = op.calculate_fee( k );
   op.inputs.resize( 2 );
   op.inputs[1].members.resize( GRAPHENE_RING_SIZE );
   auto high = op.calculate_fee( k );
   BOOST_CHECK( high > low );
   BOOST_CHECK( low > 0 );

   BOOST_CHECK_THROW( base_operation::calculate_data_fee( UINT64_C(0xffffffffffffffff),
                                                          UINT64_C(0xffffffffffffffff) ),
                      fc::exception );

   ring_input bad;
   bad.members.resize( GRAPHENE_RING_SIZE - 1 );
   ring_blind_transfer_operation tiny;
   tiny.inputs = { bad };
   BOOST_CHECK_THROW( tiny.validate(), fc::exception );

   ring_transfer_from_blind_operation exit_op;
   exit_op.fee = core_asset( 1 );
   exit_op.amount = core_asset( 1 );
   exit_op.amount.asset_id = asset_id_type( 1 );
   BOOST_CHECK_THROW( exit_op.validate(), fc::exception );
} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE( sixteen_output_deposit_and_bad_rings )
{ try {
   ACTORS( (alice) );
   transfer( account_id_type()(db), alice, core_asset( 2000000000 ) );
   generate_blocks( HARDFORK_BSIP_CT3_TIME );
   const asset_object& core = asset_id_type()(db);
   const asset_object& usd = create_user_issued_asset( "USD", alice, charge_market_fee );
   issue_uia( alice, usd.amount( 100000000 ) );

   constexpr uint64_t each = 20000000;
   vector<coin> coins;
   for( uint32_t i = 0; i < GRAPHENE_RING_SIZE; ++i )
      coins.push_back( make_coin( each ) );
   vector<blind_output> outputs;
   vector<blind_factor_type> blinds;
   share_type total = 0;
   for( const auto& c : coins )
   {
      outputs.push_back( to_output( c, true, false ) );
      blinds.push_back( c.blind );
      total += c.value;
   }
   sort_outputs( outputs );
   attach_bulletproof( outputs, coins );
   transfer_to_blind_operation deposit;
   deposit.from = alice.id;
   deposit.amount = core.amount( total );
   deposit.blinding_factor = fc::ecc::blind_sum( blinds, blinds.size() );
   deposit.outputs = outputs;
   deposit.fee = db.current_fee_schedule().calculate_fee( deposit );
   trx.operations = { deposit };
   set_expiration( db, trx );
   sign( trx, alice_private_key );
   const auto packed_size = fc::raw::pack_size( trx );
   BOOST_CHECK( packed_size > GRAPHENE_DEFAULT_MAX_TRANSACTION_SIZE );
   BOOST_CHECK( packed_size <= GRAPHENE_CONFIDENTIAL_TX_SIZE_ALLOWANCE );
   PUSH_TX( db, trx );
   trx.clear();

   transfer_to_blind_operation too_big = deposit;
   for( auto& out : too_big.outputs )
   {
      out.stealth_memo = good_memo();
      out.stealth_memo->encrypted_memo.assign( GRAPHENE_STEALTH_MEMO_MAX_SIZE, 'Q' );
   }
   too_big.fee = db.current_fee_schedule().calculate_fee( too_big );
   trx.operations = { too_big };
   set_expiration( db, trx );
   sign( trx, alice_private_key );
   BOOST_CHECK( fc::raw::pack_size( trx ) > GRAPHENE_CONFIDENTIAL_TX_SIZE_ALLOWANCE );
   BOOST_REQUIRE_THROW( PUSH_TX( db, trx ), fc::exception );
   trx.clear();

   coin foreign = make_coin( 1000 );
   {
      vector<blind_output> one{ to_output( foreign, true, false ) };
      attach_bulletproof( one, { foreign } );
      transfer_to_blind_operation uia;
      uia.from = alice.id;
      uia.amount = usd.amount( 1000 );
      uia.blinding_factor = foreign.blind;
      uia.outputs = one;
      uia.fee = db.current_fee_schedule().calculate_fee( uia );
      trx.operations = { uia };
      set_expiration( db, trx );
      sign( trx, alice_private_key );
      PUSH_TX( db, trx );
      trx.clear();
   }

   vector<coin> mixed = coins;
   mixed[0] = foreign;
   std::sort( mixed.begin(), mixed.end(),
              []( const coin& a, const coin& b ) { return a.commitment < b.commitment; } );
   size_t real = 0;
   for( size_t i = 0; i < mixed.size(); ++i )
      if( mixed[i].commitment != foreign.commitment ) { real = i; break; }

   const int64_t fee_value = 1000000;
   const coin& spent = mixed[real];
   fc::sha256 pseudo_blind = fc::sha256::hash( fc::sha256::hash( spent.blind ) );
   btsct::Scalar pseudo_scalar = must_scalar( pseudo_blind );
   commitment_type pseudo = fc::ecc::blind( pseudo_blind, spent.value );
   coin change;
   change.spend = fc::ecc::private_key::generate();
   change.owner = authority( 1, public_key_type( change.spend.get_public_key() ), 1 );
   change.blind = pseudo_blind;
   change.blind_scalar = pseudo_scalar;
   change.value = spent.value - static_cast<uint64_t>( fee_value );
   change.commitment = fc::ecc::blind( change.blind, change.value );

   ring_blind_transfer_operation ring;
   ring.fee = core.amount( fee_value );
   ring_input rin;
   rin.pseudo_commitment = pseudo;
   vector<btsct::Point> pubs, commits;
   for( const auto& c : mixed )
   {
      rin.members.push_back( c.commitment );
      auto ser = c.spend.get_public_key().serialize();
      btsct::Point pub, commit;
      BOOST_REQUIRE( btsct::Point::from_compressed( ser.data(), ser.size(), pub ) );
      BOOST_REQUIRE( btsct::Point::from_compressed( c.commitment.data(), c.commitment.size(), commit ) );
      pubs.push_back( pub );
      commits.push_back( commit );
   }
   btsct::Point image = btsct::clsag_key_image( pubs[real], spent.spend_scalar );
   uint8_t image_bytes[33];
   image.to_compressed( image_bytes );
   memcpy( rin.key_image.data(), image_bytes, 33 );
   ring.inputs = { rin };
   ring_output rout;
   rout.commitment = change.commitment;
   rout.owner = change.owner;
   rout.stealth_memo = good_memo();
   ring.outputs = { rout };
   {
      auto proof = btsct::bulletproof_plus_prove( { change.value }, { change.blind_scalar } );
      auto bytes = btsct::bulletproof_plus_serialize( proof );
      ring.range_proof.assign( reinterpret_cast<const char*>( bytes.data() ),
                               reinterpret_cast<const char*>( bytes.data() ) + bytes.size() );
   }
   auto digest = ring_signature_message( ring.fee, asset(), account_id_type(), blind_factor_type(),
                                         ring.inputs, { rout.commitment }, ring.range_proof );
   uint8_t msg[32];
   memcpy( msg, digest.data(), 32 );
   btsct::Point pseudo_point;
   BOOST_REQUIRE( btsct::Point::from_compressed( pseudo.data(), pseudo.size(), pseudo_point ) );
   auto sig = btsct::clsag_sign( msg, pubs, spent.spend_scalar, commits,
                                 spent.blind_scalar - pseudo_scalar, pseudo_point, real );
   auto sig_bytes = btsct::clsag_serialize( sig );
   ring.inputs[0].clsag.assign( reinterpret_cast<const char*>( sig_bytes.data() ),
                                reinterpret_cast<const char*>( sig_bytes.data() ) + sig_bytes.size() );
   ring.validate();
   set_expiration( db, trx );
   trx.operations = { ring };
   BOOST_REQUIRE_THROW( PUSH_TX( db, trx ), fc::exception );
   trx.clear();

   ring_blind_transfer_operation short_ring = ring;
   short_ring.inputs[0].members.pop_back();
   BOOST_CHECK_THROW( short_ring.validate(), fc::exception );
   set_expiration( db, trx );
   trx.operations = { short_ring };
   BOOST_REQUIRE_THROW( PUSH_TX( db, trx ), fc::exception );
   trx.clear();

   ring_blind_transfer_operation dup_member = ring;
   dup_member.inputs[0].members[1] = dup_member.inputs[0].members[0];
   BOOST_CHECK_THROW( dup_member.validate(), fc::exception );
   set_expiration( db, trx );
   trx.operations = { dup_member };
   BOOST_REQUIRE_THROW( PUSH_TX( db, trx ), fc::exception );
   trx.clear();

   ring_blind_transfer_operation dup_image = ring;
   dup_image.inputs.push_back( ring.inputs[0] );
   BOOST_CHECK_THROW( dup_image.validate(), fc::exception );
   set_expiration( db, trx );
   trx.operations = { dup_image };
   BOOST_REQUIRE_THROW( PUSH_TX( db, trx ), fc::exception );
   trx.clear();
} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_SUITE_END()
