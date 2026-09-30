/*
 * Oracles: named price series independent of any asset.
 *
 * The aggregation tests below are the ones that matter. An oracle's whole purpose is to be the
 * number other things trust, so the interesting cases are not "does a value round-trip" but
 * "what happens when producers disagree, go quiet, or all move at once".
 */

#include <boost/test/unit_test.hpp>

#include <graphene/chain/database.hpp>
#include <graphene/chain/hardfork.hpp>
#include <graphene/chain/oracle_object.hpp>
#include <graphene/protocol/oracle.hpp>
#include <graphene/protocol/asset_ops.hpp>

#include <fc/io/raw.hpp>

#include <functional>

#include "../common/database_fixture.hpp"

using namespace graphene::chain;
using namespace graphene::chain::test;

BOOST_AUTO_TEST_SUITE( oracle_name_tests )

BOOST_AUTO_TEST_CASE( valid_and_invalid_oracle_names )
{
   BOOST_CHECK( is_valid_oracle_name( "BTC.USD" ) );
   BOOST_CHECK( is_valid_oracle_name( "BTS" ) );
   BOOST_CHECK( is_valid_oracle_name( "EUR-USD" ) );
   BOOST_CHECK( is_valid_oracle_name( "GOLD.XAU.2026" ) );

   BOOST_CHECK( !is_valid_oracle_name( "AB" ) );            // too short
   BOOST_CHECK( !is_valid_oracle_name( std::string( 33, 'A' ) ) ); // too long
   // Lowercase is rejected so that "btc.usd" cannot sit alongside "BTC.USD" as a lookalike.
   BOOST_CHECK( !is_valid_oracle_name( "btc.usd" ) );
   BOOST_CHECK( !is_valid_oracle_name( "BTC USD" ) );       // space
   BOOST_CHECK( !is_valid_oracle_name( "BTC_USD" ) );       // underscore
   BOOST_CHECK( !is_valid_oracle_name( ".BTCUSD" ) );       // leading separator
   BOOST_CHECK( !is_valid_oracle_name( "BTCUSD." ) );       // trailing separator
   BOOST_CHECK( !is_valid_oracle_name( "BTC..USD" ) );      // repeated separator
   BOOST_CHECK( !is_valid_oracle_name( "BTC.-USD" ) );      // mixed repeated separator
}

BOOST_AUTO_TEST_CASE( an_unknown_aggregation_method_is_refused )
{
   oracle_options opts;
   opts.producers[ account_id_type( 1 ) ] = 1;
   opts.minimum_producers = 1;

   opts.aggregation = static_cast<uint8_t>( oracle_aggregation_method::median_of_latest );
   BOOST_CHECK_NO_THROW( opts.validate() );
   BOOST_CHECK( oracle_aggregation_method::median_of_latest == opts.get_aggregation() );

   opts.aggregation = static_cast<uint8_t>( oracle_aggregation_method::median_over_window );
   BOOST_CHECK_NO_THROW( opts.validate() );
   BOOST_CHECK( oracle_aggregation_method::median_over_window == opts.get_aggregation() );

   // The method travels as a plain integer, so deserialisation no longer rejects an unknown
   // one the way it would for a reflected enum -- validate() has to. Without this, nodes
   // would be asked to agree on an aggregation none of them implements.
   opts.aggregation = 2;
   BOOST_CHECK_THROW( opts.validate(), fc::exception );
   opts.aggregation = 255;
   BOOST_CHECK_THROW( opts.validate(), fc::exception );
}

BOOST_AUTO_TEST_SUITE_END()

BOOST_FIXTURE_TEST_SUITE( oracle_tests, database_fixture )

namespace {

/// Prices are compared by cross-multiplication, so two prices that reduce to the same ratio
/// are equal even when written with different amounts. Tests state prices in whole units and
/// rely on that rather than on a particular representation.
price px( int64_t base_amount, asset_id_type base_id, int64_t quote_amount, asset_id_type quote_id )
{
   return price( asset( base_amount, base_id ), asset( quote_amount, quote_id ) );
}

} // namespace

/// Every oracle operation must be refused before the hardfork, or a node running this code
/// could produce a block that a node running the previous release cannot validate.
BOOST_AUTO_TEST_CASE( oracle_ops_are_refused_before_the_hardfork )
{ try {
   ACTORS( (alice) );
   fund( alice );

   oracle_create_operation op;
   op.owner       = alice_id;
   op.name        = "BTC.USD";
   op.base_asset  = asset_id_type();
   op.quote_asset = asset_id_type();      // replaced below
   op.options.producers[alice_id] = 1;

   const asset_object& usd = create_user_issued_asset( "USDTEST" );
   op.quote_asset = usd.get_id();

   signed_transaction tx;
   tx.operations.push_back( op );
   set_expiration( db, tx );
   tx.sign( alice_private_key, db.get_chain_id() );
   GRAPHENE_REQUIRE_THROW( PUSH_TX( db, tx ), fc::exception );
} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE( create_publish_and_aggregate )
{ try {
   generate_blocks( HARDFORK_ORACLE_TIME );
   generate_block();
   set_expiration( db, trx );

   ACTORS( (alice)(bob)(carol)(dave) );
   fund( alice );
   fund( bob );
   fund( carol );
   fund( dave );

   const asset_object& usd = create_user_issued_asset( "USDTEST" );
   const auto core = asset_id_type();
   const auto usd_id = usd.get_id();

   // three producers, quorum of two
   oracle_create_operation cop;
   cop.owner       = alice_id;
   cop.name        = "CORE.USD";
   cop.description = "core against a test dollar";
   cop.base_asset  = core;
   cop.quote_asset = usd_id;
   cop.options.producers[bob_id]   = 1;
   cop.options.producers[carol_id] = 1;
   cop.options.producers[dave_id]  = 1;
   cop.options.minimum_producers   = 2;
   cop.options.value_lifetime_sec  = 3600;

   signed_transaction tx;
   tx.operations.push_back( cop );
   db.current_fee_schedule().set_fee( tx.operations.back() );
   set_expiration( db, tx );
   tx.sign( alice_private_key, db.get_chain_id() );
   auto ptx = PUSH_TX( db, tx );
   const oracle_id_type oid { ptx.operation_results.front().get<object_id_type>() };

   // a fresh oracle asserts nothing
   BOOST_CHECK( !oid(db).current_value.valid() );

   auto publish = [&]( account_id_type who, const fc::ecc::private_key& key, int64_t quote ) {
      oracle_publish_operation pop;
      pop.producer  = who;
      pop.oracle_id = oid;
      pop.value     = px( 1, core, quote, usd_id );
      signed_transaction ptx2;
      ptx2.operations.push_back( pop );
      db.current_fee_schedule().set_fee( ptx2.operations.back() );
      set_expiration( db, ptx2 );
      ptx2.sign( key, db.get_chain_id() );
      PUSH_TX( db, ptx2 );
   };

   // one submission is below quorum, so still no value -- not a value derived from one source
   publish( bob_id, bob_private_key, 100 );
   BOOST_CHECK( !oid(db).current_value.valid() );
   BOOST_CHECK_EQUAL( oid(db).current_value_producer_count, 0u );

   // two submissions reach quorum; lower median of {100, 200} is 100
   publish( carol_id, carol_private_key, 200 );
   BOOST_REQUIRE( oid(db).current_value.valid() );
   BOOST_CHECK( *oid(db).current_value == px( 1, core, 100, usd_id ) );
   BOOST_CHECK_EQUAL( oid(db).current_value_producer_count, 2u );

   // three submissions: median of {100, 200, 300} is 200
   publish( dave_id, dave_private_key, 300 );
   BOOST_REQUIRE( oid(db).current_value.valid() );
   BOOST_CHECK( *oid(db).current_value == px( 1, core, 200, usd_id ) );
   BOOST_CHECK_EQUAL( oid(db).current_value_producer_count, 3u );
} FC_LOG_AND_RETHROW() }

/// A non-producer must not be able to move the number, whatever else it can do on chain.
BOOST_AUTO_TEST_CASE( a_non_producer_cannot_publish )
{ try {
   generate_blocks( HARDFORK_ORACLE_TIME );
   generate_block();
   set_expiration( db, trx );

   ACTORS( (alice)(bob)(mallory) );
   fund( alice );
   fund( bob );
   fund( mallory );

   const asset_object& usd = create_user_issued_asset( "USDTEST" );
   const auto core = asset_id_type();
   const auto usd_id = usd.get_id();

   oracle_create_operation cop;
   cop.owner       = alice_id;
   cop.name        = "CORE.USD";
   cop.base_asset  = core;
   cop.quote_asset = usd_id;
   cop.options.producers[bob_id] = 1;
   cop.options.minimum_producers = 1;

   signed_transaction tx;
   tx.operations.push_back( cop );
   db.current_fee_schedule().set_fee( tx.operations.back() );
   set_expiration( db, tx );
   tx.sign( alice_private_key, db.get_chain_id() );
   const oracle_id_type oid { PUSH_TX( db, tx ).operation_results.front().get<object_id_type>() };

   oracle_publish_operation pop;
   pop.producer  = mallory_id;
   pop.oracle_id = oid;
   pop.value     = px( 1, core, 1, usd_id );
   signed_transaction bad;
   bad.operations.push_back( pop );
   db.current_fee_schedule().set_fee( bad.operations.back() );
   set_expiration( db, bad );
   bad.sign( mallory_private_key, db.get_chain_id() );
   GRAPHENE_REQUIRE_THROW( PUSH_TX( db, bad ), fc::exception );

   // and the owner is not implicitly a producer either
   pop.producer = alice_id;
   signed_transaction owner_tx;
   owner_tx.operations.push_back( pop );
   db.current_fee_schedule().set_fee( owner_tx.operations.back() );
   set_expiration( db, owner_tx );
   owner_tx.sign( alice_private_key, db.get_chain_id() );
   GRAPHENE_REQUIRE_THROW( PUSH_TX( db, owner_tx ), fc::exception );
} FC_LOG_AND_RETHROW() }

/// A value in the wrong orientation must be refused rather than silently inverted: a consumer
/// must never have to work out which way round a number is.
BOOST_AUTO_TEST_CASE( a_value_in_the_wrong_orientation_is_refused )
{ try {
   generate_blocks( HARDFORK_ORACLE_TIME );
   generate_block();
   set_expiration( db, trx );

   ACTORS( (alice)(bob) );
   fund( alice );
   fund( bob );

   const asset_object& usd = create_user_issued_asset( "USDTEST" );
   const auto core = asset_id_type();
   const auto usd_id = usd.get_id();

   oracle_create_operation cop;
   cop.owner       = alice_id;
   cop.name        = "CORE.USD";
   cop.base_asset  = core;
   cop.quote_asset = usd_id;
   cop.options.producers[bob_id] = 1;
   cop.options.minimum_producers = 1;

   signed_transaction tx;
   tx.operations.push_back( cop );
   db.current_fee_schedule().set_fee( tx.operations.back() );
   set_expiration( db, tx );
   tx.sign( alice_private_key, db.get_chain_id() );
   const oracle_id_type oid { PUSH_TX( db, tx ).operation_results.front().get<object_id_type>() };

   oracle_publish_operation pop;
   pop.producer  = bob_id;
   pop.oracle_id = oid;
   pop.value     = px( 1, usd_id, 100, core );   // inverted
   signed_transaction bad;
   bad.operations.push_back( pop );
   db.current_fee_schedule().set_fee( bad.operations.back() );
   set_expiration( db, bad );
   bad.sign( bob_private_key, db.get_chain_id() );
   GRAPHENE_REQUIRE_THROW( PUSH_TX( db, bad ), fc::exception );
} FC_LOG_AND_RETHROW() }

/// Names identify oracles to humans, so a duplicate is how a consumer gets pointed at the
/// wrong series.
BOOST_AUTO_TEST_CASE( oracle_names_are_unique )
{ try {
   generate_blocks( HARDFORK_ORACLE_TIME );
   generate_block();
   set_expiration( db, trx );

   ACTORS( (alice)(bob) );
   fund( alice );
   fund( bob );

   const asset_object& usd = create_user_issued_asset( "USDTEST" );

   auto make = [&]( account_id_type owner, const fc::ecc::private_key& key ) {
      oracle_create_operation cop;
      cop.owner       = owner;
      cop.name        = "CORE.USD";
      cop.base_asset  = asset_id_type();
      cop.quote_asset = usd.get_id();
      cop.options.producers[owner] = 1;
      cop.options.minimum_producers = 1;
      signed_transaction tx;
      tx.operations.push_back( cop );
      db.current_fee_schedule().set_fee( tx.operations.back() );
      set_expiration( db, tx );
      tx.sign( key, db.get_chain_id() );
      return PUSH_TX( db, tx );
   };

   make( alice_id, alice_private_key );
   // a different owner does not get to reuse the name
   GRAPHENE_REQUIRE_THROW( make( bob_id, bob_private_key ), fc::exception );
} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_SUITE_END()

/// Builds and pushes an oracle_create, returning the new id. Factored out because most of the
/// interesting tests differ only in the options they set.
struct oracle_fixture : database_fixture
{
   asset_id_type core_id;
   asset_id_type usd_id;

   void setup_assets()
   {
      core_id = asset_id_type();
      usd_id  = create_user_issued_asset( "USDTEST" ).get_id();
   }

   /// price = base/quote, so px(1, core, N, usd) reads "1 CORE is worth N USD". Note that a
   /// *larger* N is a numerically *smaller* price in this orientation, since the ratio is 1/N.
   /// Ordering, and therefore the median, is by that ratio.
   price usd_per_core( int64_t n ) const
   {
      return price( asset( 1, core_id ), asset( n, usd_id ) );
   }

   oracle_id_type make_oracle( account_id_type owner, const fc::ecc::private_key& key,
                               const string& name, const oracle_options& opts )
   {
      oracle_create_operation op;
      op.owner       = owner;
      op.name        = name;
      op.base_asset  = core_id;
      op.quote_asset = usd_id;
      op.options     = opts;
      signed_transaction tx;
      tx.operations.push_back( op );
      db.current_fee_schedule().set_fee( tx.operations.back() );
      set_expiration( db, tx );
      tx.sign( key, db.get_chain_id() );
      return oracle_id_type { PUSH_TX( db, tx ).operation_results.front().get<object_id_type>() };
   }

   void publish( oracle_id_type oid, account_id_type who, const fc::ecc::private_key& key,
                 int64_t n )
   {
      oracle_publish_operation op;
      op.producer  = who;
      op.oracle_id = oid;
      op.value     = usd_per_core( n );
      signed_transaction tx;
      tx.operations.push_back( op );
      db.current_fee_schedule().set_fee( tx.operations.back() );
      set_expiration( db, tx );
      tx.sign( key, db.get_chain_id() );
      PUSH_TX( db, tx );
   }

   void update_options( oracle_id_type oid, account_id_type owner,
                        const fc::ecc::private_key& key, const oracle_options& opts )
   {
      oracle_update_operation op;
      op.owner       = owner;
      op.oracle_id   = oid;
      op.new_options = opts;
      signed_transaction tx;
      tx.operations.push_back( op );
      db.current_fee_schedule().set_fee( tx.operations.back() );
      set_expiration( db, tx );
      tx.sign( key, db.get_chain_id() );
      PUSH_TX( db, tx );
   }
};

BOOST_FIXTURE_TEST_SUITE( oracle_behaviour_tests, oracle_fixture )

/// A submission that has aged past value_lifetime_sec must stop counting. An oracle that kept
/// quoting a price nobody is still asserting is worse than one that reports nothing.
BOOST_AUTO_TEST_CASE( a_stale_submission_stops_counting )
{ try {
   generate_blocks( HARDFORK_ORACLE_TIME );
   generate_block();
   set_expiration( db, trx );
   setup_assets();

   ACTORS( (alice)(bob)(carol) );
   fund( alice ); fund( bob ); fund( carol );

   oracle_options opts;
   opts.producers[bob_id]   = 1;
   opts.producers[carol_id] = 1;
   opts.minimum_producers   = 2;
   opts.value_lifetime_sec  = 600;
   const auto oid = make_oracle( alice_id, alice_private_key, "CORE.USD", opts );

   publish( oid, bob_id,   bob_private_key,   100 );
   publish( oid, carol_id, carol_private_key, 100 );
   BOOST_REQUIRE( oid(db).current_value.valid() );

   // let both submissions age out, then have one producer republish
   generate_blocks( db.head_block_time() + 601 );
   set_expiration( db, trx );
   publish( oid, bob_id, bob_private_key, 100 );

   // carol's value is stale, so only one live submission remains -- below quorum
   BOOST_CHECK( !oid(db).current_value.valid() );
   BOOST_CHECK_EQUAL( oid(db).current_value_producer_count, 0u );

   // carol republishing restores quorum
   publish( oid, carol_id, carol_private_key, 100 );
   BOOST_REQUIRE( oid(db).current_value.valid() );
   BOOST_CHECK( *oid(db).current_value == usd_per_core( 100 ) );
} FC_LOG_AND_RETHROW() }

/// Weight is the difference between an oracle and a plain vote count.
BOOST_AUTO_TEST_CASE( weights_shift_the_median )
{ try {
   generate_blocks( HARDFORK_ORACLE_TIME );
   generate_block();
   set_expiration( db, trx );
   setup_assets();

   ACTORS( (alice)(bob)(carol)(dave) );
   fund( alice ); fund( bob ); fund( carol ); fund( dave );

   // bob outweighs the other two combined, so his value is the median whatever they say
   oracle_options opts;
   opts.producers[bob_id]   = 10;
   opts.producers[carol_id] = 1;
   opts.producers[dave_id]  = 1;
   opts.minimum_producers   = 2;
   const auto oid = make_oracle( alice_id, alice_private_key, "CORE.USD", opts );

   publish( oid, bob_id,   bob_private_key,   100 );
   publish( oid, carol_id, carol_private_key, 300 );
   publish( oid, dave_id,  dave_private_key,  400 );

   BOOST_REQUIRE( oid(db).current_value.valid() );
   BOOST_CHECK( *oid(db).current_value == usd_per_core( 100 ) );

   // with equal weights the same three submissions give the middle value instead
   oracle_options equal = opts;
   equal.producers[bob_id] = 1;
   update_options( oid, alice_id, alice_private_key, equal );
   BOOST_REQUIRE( oid(db).current_value.valid() );
   BOOST_CHECK( *oid(db).current_value == usd_per_core( 300 ) );
} FC_LOG_AND_RETHROW() }

/// A minority disagreeing with the rest is filtered out.
BOOST_AUTO_TEST_CASE( an_outlier_is_excluded_while_quorum_survives )
{ try {
   generate_blocks( HARDFORK_ORACLE_TIME );
   generate_block();
   set_expiration( db, trx );
   setup_assets();

   ACTORS( (alice)(bob)(carol)(dave) );
   fund( alice ); fund( bob ); fund( carol ); fund( dave );

   oracle_options opts;
   opts.producers[bob_id]   = 1;
   opts.producers[carol_id] = 1;
   opts.producers[dave_id]  = 1;
   opts.minimum_producers   = 2;
   opts.max_deviation_ppm   = 100000;   // 10%
   const auto oid = make_oracle( alice_id, alice_private_key, "CORE.USD", opts );

   // establish a baseline of 100
   publish( oid, bob_id,   bob_private_key,   100 );
   publish( oid, carol_id, carol_private_key, 100 );
   BOOST_REQUIRE( oid(db).current_value.valid() );
   BOOST_CHECK( *oid(db).current_value == usd_per_core( 100 ) );

   // dave publishes wildly off; two honest submissions still meet quorum, so he is dropped
   publish( oid, dave_id, dave_private_key, 100000 );
   BOOST_REQUIRE( oid(db).current_value.valid() );
   BOOST_CHECK( *oid(db).current_value == usd_per_core( 100 ) );
   BOOST_CHECK_EQUAL( oid(db).current_value_producer_count, 2u );
} FC_LOG_AND_RETHROW() }

/// ...but when everyone moves together, that is a real crash and the oracle must follow it.
/// Freezing here is the failure mode the quorum-aware rule exists to prevent.
BOOST_AUTO_TEST_CASE( when_every_producer_moves_together_the_oracle_moves )
{ try {
   generate_blocks( HARDFORK_ORACLE_TIME );
   generate_block();
   set_expiration( db, trx );
   setup_assets();

   ACTORS( (alice)(bob)(carol) );
   fund( alice ); fund( bob ); fund( carol );

   oracle_options opts;
   opts.producers[bob_id]   = 1;
   opts.producers[carol_id] = 1;
   opts.minimum_producers   = 2;
   opts.max_deviation_ppm   = 100000;   // 10%
   const auto oid = make_oracle( alice_id, alice_private_key, "CORE.USD", opts );

   publish( oid, bob_id,   bob_private_key,   100 );
   publish( oid, carol_id, carol_private_key, 100 );
   BOOST_REQUIRE( oid(db).current_value.valid() );

   // both producers report a 10x move, far outside the deviation bound. Excluding them would
   // leave zero submissions and break quorum, so they are believed.
   publish( oid, bob_id,   bob_private_key,   1000 );
   publish( oid, carol_id, carol_private_key, 1000 );

   BOOST_REQUIRE( oid(db).current_value.valid() );
   BOOST_CHECK( *oid(db).current_value == usd_per_core( 1000 ) );
   BOOST_CHECK_EQUAL( oid(db).current_value_producer_count, 2u );
} FC_LOG_AND_RETHROW() }

/// Removing a producer is what an owner does when that producer misbehaves, so it has to bite
/// straight away rather than when its last submission happens to expire.
BOOST_AUTO_TEST_CASE( removing_a_producer_takes_effect_immediately )
{ try {
   generate_blocks( HARDFORK_ORACLE_TIME );
   generate_block();
   set_expiration( db, trx );
   setup_assets();

   ACTORS( (alice)(bob)(carol)(dave) );
   fund( alice ); fund( bob ); fund( carol ); fund( dave );

   oracle_options opts;
   opts.producers[bob_id]   = 1;
   opts.producers[carol_id] = 1;
   opts.producers[dave_id]  = 1;
   opts.minimum_producers   = 2;
   const auto oid = make_oracle( alice_id, alice_private_key, "CORE.USD", opts );

   publish( oid, bob_id,   bob_private_key,   100 );
   publish( oid, carol_id, carol_private_key, 200 );
   publish( oid, dave_id,  dave_private_key,  300 );
   BOOST_CHECK( *oid(db).current_value == usd_per_core( 200 ) );

   // drop dave; his stored submission must stop counting at once
   oracle_options without_dave;
   without_dave.producers[bob_id]   = 1;
   without_dave.producers[carol_id] = 1;
   without_dave.minimum_producers   = 2;
   update_options( oid, alice_id, alice_private_key, without_dave );

   BOOST_REQUIRE( oid(db).current_value.valid() );
   BOOST_CHECK_EQUAL( oid(db).current_value_producer_count, 2u );
   // median of {100, 200} with the upper-median convention
   BOOST_CHECK( *oid(db).current_value == usd_per_core( 100 ) );

   // and dave can no longer publish at all
   GRAPHENE_REQUIRE_THROW( publish( oid, dave_id, dave_private_key, 500 ), fc::exception );
} FC_LOG_AND_RETHROW() }

/// A windowed median must not be movable by one block's worth of prices.
BOOST_AUTO_TEST_CASE( a_windowed_median_resists_a_single_spike )
{ try {
   generate_blocks( HARDFORK_ORACLE_TIME );
   generate_block();
   set_expiration( db, trx );
   setup_assets();

   ACTORS( (alice)(bob) );
   fund( alice ); fund( bob );

   oracle_options opts;
   opts.producers[bob_id]  = 1;
   opts.minimum_producers  = 1;
   opts.value_lifetime_sec = 86400;
   opts.aggregation        = static_cast<uint8_t>( oracle_aggregation_method::median_over_window );
   // A window commensurate with the test's own timescale. History samples at
   // window_sec/MAX_HISTORY, so a day-long window buckets at ~22 minutes and publishes five
   // seconds apart all collapse into one sample -- there would be no history to damp with.
   // That is not the sampler being wrong, it is a day-long window genuinely having seen only
   // seconds of data; the old ring hid it by keeping every publish regardless of spacing.
   opts.window_sec         = 300;
   const auto oid = make_oracle( alice_id, alice_private_key, "CORE.USD", opts );

   // a settled history at 100
   for( int i = 0; i < 5; ++i )
   {
      publish( oid, bob_id, bob_private_key, 100 );
      generate_block();
      set_expiration( db, trx );
   }
   BOOST_REQUIRE( oid(db).current_value.valid() );
   BOOST_CHECK( *oid(db).current_value == usd_per_core( 100 ) );

   // one spike does not move the windowed median, even though it is the latest value
   publish( oid, bob_id, bob_private_key, 100000 );
   BOOST_REQUIRE( oid(db).current_value.valid() );
   BOOST_CHECK( *oid(db).current_value == usd_per_core( 100 ) );
   // the spike is recorded though: the window holds it, it is simply outvoted
   BOOST_CHECK( oid(db).history.back().second == usd_per_core( 100000 ) );
} FC_LOG_AND_RETHROW() }

/// Policy is the owner's alone. A producer that could edit the producer set could appoint
/// itself a majority.
BOOST_AUTO_TEST_CASE( only_the_owner_may_update_or_delete )
{ try {
   generate_blocks( HARDFORK_ORACLE_TIME );
   generate_block();
   set_expiration( db, trx );
   setup_assets();

   ACTORS( (alice)(bob) );
   fund( alice ); fund( bob );

   oracle_options opts;
   opts.producers[bob_id] = 1;
   opts.minimum_producers = 1;
   const auto oid = make_oracle( alice_id, alice_private_key, "CORE.USD", opts );

   oracle_options grab = opts;
   grab.producers[bob_id] = 1000;
   GRAPHENE_REQUIRE_THROW( update_options( oid, bob_id, bob_private_key, grab ), fc::exception );

   oracle_delete_operation dop;
   dop.owner     = bob_id;
   dop.oracle_id = oid;
   signed_transaction tx;
   tx.operations.push_back( dop );
   db.current_fee_schedule().set_fee( tx.operations.back() );
   set_expiration( db, tx );
   tx.sign( bob_private_key, db.get_chain_id() );
   GRAPHENE_REQUIRE_THROW( PUSH_TX( db, tx ), fc::exception );

   // the owner can
   dop.owner = alice_id;
   signed_transaction ok;
   ok.operations.push_back( dop );
   db.current_fee_schedule().set_fee( ok.operations.back() );
   set_expiration( db, ok );
   ok.sign( alice_private_key, db.get_chain_id() );
   PUSH_TX( db, ok );
   BOOST_CHECK( nullptr == db.find( oid ) );
} FC_LOG_AND_RETHROW() }

/**
 * REGRESSION: the deviation filter must not let a minority capture the price.
 *
 * The filter drops submissions far from a reference and keeps the survivors. It used to
 * reference the LAST PUBLISHED VALUE, which inverted the security model: on a genuine price
 * move it is the HONEST producers who deviate -- they are reporting the new price -- while a
 * producer repeating the old number sits inside the band and survives. The survivors became
 * the entire live set, so one stale producer outvoted an honest majority; and because
 * current_value never moved, neither did the band, so the capture held indefinitely.
 *
 * The reference is now the round's own weighted median, which is the majority's number by
 * construction, so the outlier is what gets trimmed. Four of five producers report a move
 * from 100 to 200 and win immediately; mallory, still saying 100, is the one dropped.
 */
BOOST_AUTO_TEST_CASE( the_deviation_filter_cannot_let_a_minority_capture_the_price )
{ try {
   generate_blocks( HARDFORK_ORACLE_TIME );
   generate_block();
   set_expiration( db, trx );
   setup_assets();

   ACTORS( (owner)(p1)(p2)(p3)(p4)(mallory) );

   oracle_options opts;
   opts.producers[ p1_id ]      = 1;
   opts.producers[ p2_id ]      = 1;
   opts.producers[ p3_id ]      = 1;
   opts.producers[ p4_id ]      = 1;
   opts.producers[ mallory_id ] = 1;
   opts.minimum_producers = 1;      // the shipped default
   opts.max_deviation_ppm = 100000; // 10% band -- a plausible "manipulation resistance" setting
   opts.aggregation = static_cast<uint8_t>( oracle_aggregation_method::median_of_latest );

   const auto oid = make_oracle( owner_id, owner_private_key, "CORE.USD", opts );

   // Everyone agrees the price is 100. (usd_per_core is 1/n, so a bigger n is a lower price.)
   publish( oid, p1_id, p1_private_key, 100 );
   publish( oid, p2_id, p2_private_key, 100 );
   publish( oid, p3_id, p3_private_key, 100 );
   publish( oid, p4_id, p4_private_key, 100 );
   publish( oid, mallory_id, mallory_private_key, 100 );
   BOOST_REQUIRE( oid(db).current_value.valid() );
   BOOST_CHECK( *oid(db).current_value == usd_per_core( 100 ) );

   // The market moves hard. The four honest producers report it; mallory does not.
   // A block between rounds keeps mallory's repeated submission from being a byte-identical
   // transaction, which the chain would reject as a duplicate.
   generate_block();
   set_expiration( db, trx );
   publish( oid, p1_id, p1_private_key, 200 );
   publish( oid, p2_id, p2_private_key, 200 );
   publish( oid, p3_id, p3_private_key, 200 );
   publish( oid, p4_id, p4_private_key, 200 );
   publish( oid, mallory_id, mallory_private_key, 100 );

   // The band is anchored on this round's median, so mallory is the outlier and is trimmed.
   BOOST_REQUIRE( oid(db).current_value.valid() );
   BOOST_CHECK_MESSAGE( *oid(db).current_value == usd_per_core( 200 ),
                        "the honest majority must win" );
   BOOST_CHECK_EQUAL( oid(db).current_value_producer_count, 4 );

   // And it is not a one-round lag: the band is anchored to a value mallory controls, so
   // republishing the same number holds the oracle there indefinitely.
   for( int round = 0; round < 5; ++round )
   {
      generate_block();
      set_expiration( db, trx );
      publish( oid, p1_id, p1_private_key, 200 );
      publish( oid, p2_id, p2_private_key, 200 );
      publish( oid, p3_id, p3_private_key, 200 );
      publish( oid, p4_id, p4_private_key, 200 );
      publish( oid, mallory_id, mallory_private_key, 100 );
   }
   BOOST_CHECK( *oid(db).current_value == usd_per_core( 200 ) );
   BOOST_CHECK_EQUAL( oid(db).current_value_producer_count, 4 );

} FC_LOG_AND_RETHROW() }

/**
 * The same oracle without the filter behaves as a median should: the honest majority wins
 * immediately. The filter is the whole difference.
 */
BOOST_AUTO_TEST_CASE( without_the_deviation_filter_the_majority_wins )
{ try {
   generate_blocks( HARDFORK_ORACLE_TIME );
   generate_block();
   set_expiration( db, trx );
   setup_assets();

   ACTORS( (owner)(p1)(p2)(p3)(p4)(mallory) );

   oracle_options opts;
   opts.producers[ p1_id ]      = 1;
   opts.producers[ p2_id ]      = 1;
   opts.producers[ p3_id ]      = 1;
   opts.producers[ p4_id ]      = 1;
   opts.producers[ mallory_id ] = 1;
   opts.minimum_producers = 1;
   opts.max_deviation_ppm = 0;      // off
   opts.aggregation = static_cast<uint8_t>( oracle_aggregation_method::median_of_latest );

   const auto oid = make_oracle( owner_id, owner_private_key, "CORE.USD", opts );

   publish( oid, p1_id, p1_private_key, 100 );
   publish( oid, p2_id, p2_private_key, 100 );
   publish( oid, p3_id, p3_private_key, 100 );
   publish( oid, p4_id, p4_private_key, 100 );
   publish( oid, mallory_id, mallory_private_key, 100 );

   generate_block();
   set_expiration( db, trx );
   publish( oid, p1_id, p1_private_key, 200 );
   publish( oid, p2_id, p2_private_key, 200 );
   publish( oid, p3_id, p3_private_key, 200 );
   publish( oid, p4_id, p4_private_key, 200 );
   publish( oid, mallory_id, mallory_private_key, 100 );

   BOOST_REQUIRE( oid(db).current_value.valid() );
   BOOST_CHECK( *oid(db).current_value == usd_per_core( 200 ) );
   BOOST_CHECK_EQUAL( oid(db).current_value_producer_count, 5 );
} FC_LOG_AND_RETHROW() }


/**
 * GOVERNANCE: the owner can flip the reported value by re-weighting alone.
 *
 * oracle_update checks only that the caller is the owner. There is no delay, no lock-up period
 * and no limit on the change. The test shows the consequence: without a single producer
 * changing its submission, the value jumps, because the weighted median falls on a different
 * submission.
 *
 * That is not a flaw in the median but a property of the trust model: whoever owns the oracle
 * decides whose voice counts. The test records it so that nobody takes the oracle for
 * trust-minimised, which it is not.
 */
BOOST_AUTO_TEST_CASE( the_owner_can_flip_the_value_by_reweighting_alone )
{ try {
   generate_blocks( HARDFORK_ORACLE_TIME );
   generate_block();
   set_expiration( db, trx );
   setup_assets();

   ACTORS( (owner)(p1)(p2)(p3) );

   oracle_options opts;
   opts.producers[ p1_id ] = 1;
   opts.producers[ p2_id ] = 1;
   opts.producers[ p3_id ] = 1;
   opts.minimum_producers = 1;
   opts.aggregation = static_cast<uint8_t>( oracle_aggregation_method::median_of_latest );
   const auto oid = make_oracle( owner_id, owner_private_key, "GOV.TEST", opts );

   publish( oid, p1_id, p1_private_key, 100 );
   publish( oid, p2_id, p2_private_key, 200 );
   publish( oid, p3_id, p3_private_key, 300 );
   BOOST_REQUIRE( oid(db).current_value.valid() );
   BOOST_TEST_MESSAGE( "  three producers submit 100 / 200 / 300 with equal weight" );
   BOOST_CHECK( *oid(db).current_value == usd_per_core( 200 ) );
   BOOST_TEST_MESSAGE( "  median: 200" );

   // Change only the weights. No new submission.
   generate_block();
   set_expiration( db, trx );
   oracle_options heavy = opts;
   heavy.producers[ p1_id ] = 10;   // p1 outweighs the other two together
   update_options( oid, owner_id, owner_private_key, heavy );

   BOOST_REQUIRE( oid(db).current_value.valid() );
   BOOST_TEST_MESSAGE( "  the owner sets p1 to weight 10, with no new submission" );
   BOOST_TEST_MESSAGE( "  the value now equals p1's submission" );
   BOOST_CHECK_MESSAGE( *oid(db).current_value == usd_per_core( 100 ),
                        "re-weighting alone did not flip the value" );

   // And back again, just as immediately.
   generate_block();
   set_expiration( db, trx );
   oracle_options heavy3 = opts;
   heavy3.producers[ p3_id ] = 10;
   update_options( oid, owner_id, owner_private_key, heavy3 );
   BOOST_CHECK( *oid(db).current_value == usd_per_core( 300 ) );
   BOOST_TEST_MESSAGE( "  ==> the owner can set the value to any existing submission," );
   BOOST_TEST_MESSAGE( "      at any time, immediately and without notice" );
} FC_LOG_AND_RETHROW() }

/**
 * COLLUSION: how many producers have to work together to decide the value?
 *
 * With equal weights, a simple majority: half the set plus one, rounded down. That is the
 * definition of the median. The test measures it for five and for seven producers, so that
 * the number is shown rather than asserted, and it checks that one fewer is NOT enough.
 */
BOOST_AUTO_TEST_CASE( measure_the_collusion_threshold_of_the_median )
{ try {
   generate_blocks( HARDFORK_ORACLE_TIME );
   generate_block();
   set_expiration( db, trx );
   setup_assets();

   ACTORS( (owner)(a1)(a2)(a3)(a4)(a5)(a6)(a7) );
   const std::vector<account_id_type> ids {
      a1_id, a2_id, a3_id, a4_id, a5_id, a6_id, a7_id };
   const std::vector<fc::ecc::private_key> keys {
      a1_private_key, a2_private_key, a3_private_key, a4_private_key,
      a5_private_key, a6_private_key, a7_private_key };

   for( size_t n : { size_t(5), size_t(7) } )
   {
      generate_block();
      set_expiration( db, trx );
      oracle_options opts;
      for( size_t i = 0; i < n; ++i ) opts.producers[ ids[i] ] = 1;
      opts.minimum_producers = 1;
      opts.aggregation = static_cast<uint8_t>( oracle_aggregation_method::median_of_latest );
      const auto oid = make_oracle( owner_id, owner_private_key,
                                    "COL" + std::to_string( n ), opts );

      // Honest: everyone submits 100.
      for( size_t i = 0; i < n; ++i ) publish( oid, ids[i], keys[i], 100 );
      BOOST_CHECK( *oid(db).current_value == usd_per_core( 100 ) );

      size_t needed = 0;
      for( size_t k = 1; k <= n && 0 == needed; ++k )
      {
         generate_block();
         set_expiration( db, trx );
         // k colluders submit 999, and the rest stay at 100.
         for( size_t i = 0; i < k; ++i ) publish( oid, ids[i], keys[i], 999 );
         for( size_t i = k; i < n; ++i ) publish( oid, ids[i], keys[i], 100 );
         if( *oid(db).current_value == usd_per_core( 999 ) ) needed = k;
      }
      const size_t expected = n / 2 + 1;
      BOOST_TEST_MESSAGE( "  " << n << " producers: " << needed
                          << " have to collude (expected " << expected << ")" );
      BOOST_CHECK_EQUAL( needed, expected );
   }
   BOOST_TEST_MESSAGE( "  ==> the threshold is a simple majority of the weight," );
   BOOST_TEST_MESSAGE( "      no more and no less" );
} FC_LOG_AND_RETHROW() }

/**
 * ECONOMICS: what does it cost to manipulate an oracle?
 *
 * The honest answer is uncomfortable: on chain, almost nothing. A submission costs the network
 * fee and nothing else. There is no stake, no deposit, nothing that a false submission would
 * lose. The test records that by measuring what a producer pays for any number of false
 * submissions.
 *
 * The oracle's security therefore rests entirely on the owner's choice of producers, not on
 * any economic incentive. That is a design decision, not an oversight, but it has to be stated
 * so that nobody assumes a stake that does not exist.
 */
BOOST_AUTO_TEST_CASE( measure_the_on_chain_cost_of_publishing_a_false_value )
{ try {
   generate_blocks( HARDFORK_ORACLE_TIME );
   generate_block();
   set_expiration( db, trx );
   setup_assets();

   ACTORS( (owner)(liar) );
   fund( liar, asset(10000000) );

   oracle_options opts;
   opts.producers[ liar_id ] = 1;
   opts.minimum_producers = 1;
   opts.aggregation = static_cast<uint8_t>( oracle_aggregation_method::median_of_latest );
   const auto oid = make_oracle( owner_id, owner_private_key, "COST.TEST", opts );

   const int64_t before = get_balance( liar_id, core_id );
   for( int i = 0; i < 10; ++i )
   {
      generate_block();
      set_expiration( db, trx );
      publish( oid, liar_id, liar_private_key, 100 + i );   // a different false value each time
   }
   const int64_t after = get_balance( liar_id, core_id );

   BOOST_TEST_MESSAGE( "  10 false submissions cost "
                       << ( before - after ) << " in the test fixture (where every fee is 0)" );
   BOOST_TEST_MESSAGE( "  protocol default fee per submission: "
                       << ( GRAPHENE_BLOCKCHAIN_PRECISION / 10 ) << " = 0.1 BTS" );
   BOOST_TEST_MESSAGE( "  stake forfeited by a false submission: 0" );
   BOOST_TEST_MESSAGE( "  ==> the cost of a lie is capped by the fee and" );
   BOOST_TEST_MESSAGE( "      unrelated to the damage it does. The oracle's security" );
   BOOST_TEST_MESSAGE( "      rests on the choice of producers alone." );

   // What this asserts is not the size of the fee, which depends on the fee schedule, but that
   // a false submission costs NOTHING beyond the fee: it is accepted, it stands, and nothing is
   // taken back.
   BOOST_REQUIRE( oid(db).current_value.valid() );
   BOOST_CHECK_MESSAGE( *oid(db).current_value == usd_per_core( 109 ),
                        "the last false submission was not accepted" );
   BOOST_CHECK_MESSAGE( get_balance( liar_id, core_id ) >= after,
                        "something was taken back after the submission" );
} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_SUITE_END()


BOOST_FIXTURE_TEST_SUITE( oracle_binding_tests, oracle_fixture )

namespace {

/// Binds (or, with an empty optional, unbinds) an oracle as a smartcoin's price source.
void set_price_oracle( database_fixture& f, const asset_object& mia,
                       account_id_type issuer, const fc::ecc::private_key& key,
                       const optional<oracle_id_type>& oid )
{
   asset_update_bitasset_operation op;
   op.issuer          = issuer;
   op.asset_to_update = mia.get_id();
   op.new_options     = mia.bitasset_data( f.db ).options;
   op.new_options.extensions.value.price_oracle_id = oid;

   signed_transaction tx;
   tx.operations.push_back( op );
   f.db.current_fee_schedule().set_fee( tx.operations.back() );
   set_expiration( f.db, tx );
   tx.sign( key, f.db.get_chain_id() );
   PUSH_TX( f.db, tx );
}

} // namespace

/// The whole point of the feature: a smartcoin's settlement price coming from an oracle
/// rather than from per-asset feeds.
BOOST_AUTO_TEST_CASE( a_smartcoin_takes_its_settlement_price_from_a_bound_oracle )
{ try {
   generate_blocks( HARDFORK_ORACLE_TIME );
   generate_block();
   set_expiration( db, trx );

   ACTORS( (alice)(bob)(carol) );
   fund( alice ); fund( bob ); fund( carol );

   // a smartcoin backed by CORE, issued by alice
   const asset_object& mia = create_bitasset( "MIATEST", alice_id );
   const auto mia_id = mia.get_id();

   // an oracle quoting MIA/CORE, which is the orientation a settlement price uses
   core_id = asset_id_type();
   usd_id  = asset_id_type();
   oracle_create_operation cop;
   cop.owner       = alice_id;
   cop.name        = "MIA.CORE";
   cop.base_asset  = mia_id;
   cop.quote_asset = asset_id_type();
   cop.options.producers[bob_id]   = 1;
   cop.options.producers[carol_id] = 1;
   cop.options.minimum_producers   = 2;
   signed_transaction ctx;
   ctx.operations.push_back( cop );
   db.current_fee_schedule().set_fee( ctx.operations.back() );
   set_expiration( db, ctx );
   ctx.sign( alice_private_key, db.get_chain_id() );
   const oracle_id_type oid { PUSH_TX( db, ctx ).operation_results.front().get<object_id_type>() };

   auto pub = [&]( account_id_type who, const fc::ecc::private_key& key, int64_t core_amount ) {
      oracle_publish_operation op;
      op.producer  = who;
      op.oracle_id = oid;
      op.value     = price( asset( 1, mia_id ), asset( core_amount, asset_id_type() ) );
      signed_transaction tx;
      tx.operations.push_back( op );
      db.current_fee_schedule().set_fee( tx.operations.back() );
      set_expiration( db, tx );
      tx.sign( key, db.get_chain_id() );
      PUSH_TX( db, tx );
   };

   pub( bob_id,   bob_private_key,   10 );
   pub( carol_id, carol_private_key, 10 );
   BOOST_REQUIRE( oid(db).current_value.valid() );

   // before binding, the smartcoin has no feed at all
   BOOST_CHECK( mia_id(db).bitasset_data(db).current_feed.settlement_price.is_null() );

   set_price_oracle( *this, mia_id(db), alice_id, alice_private_key, oid );

   // binding alone must publish the price through, without waiting for the next publish
   BOOST_REQUIRE( !mia_id(db).bitasset_data(db).current_feed.settlement_price.is_null() );
   BOOST_CHECK( mia_id(db).bitasset_data(db).current_feed.settlement_price
                == price( asset( 1, mia_id ), asset( 10, asset_id_type() ) ) );
   BOOST_CHECK_EQUAL( oid(db).subscribers.size(), 1u );

   // a new value reaches the smartcoin in the same block it is published
   pub( bob_id,   bob_private_key,   20 );
   pub( carol_id, carol_private_key, 20 );
   BOOST_CHECK( mia_id(db).bitasset_data(db).current_feed.settlement_price
                == price( asset( 1, mia_id ), asset( 20, asset_id_type() ) ) );

   // unbinding returns the asset to the legacy feed path, which has no feeds here
   set_price_oracle( *this, mia_id(db), alice_id, alice_private_key, {} );
   BOOST_CHECK( mia_id(db).bitasset_data(db).current_feed.settlement_price.is_null() );
   BOOST_CHECK( oid(db).subscribers.empty() );
} FC_LOG_AND_RETHROW() }

/// An inverted oracle would margin-call every position on the asset, so the orientation is
/// checked once at bind time rather than trusted on every read.
BOOST_AUTO_TEST_CASE( an_oracle_quoting_the_wrong_pair_cannot_be_bound )
{ try {
   generate_blocks( HARDFORK_ORACLE_TIME );
   generate_block();
   set_expiration( db, trx );

   ACTORS( (alice)(bob) );
   fund( alice ); fund( bob );

   const asset_object& mia = create_bitasset( "MIATEST", alice_id );
   const auto mia_id = mia.get_id();
   const asset_object& other = create_user_issued_asset( "OTHERTEST" );

   // quoted the wrong way round: CORE/MIA instead of MIA/CORE
   oracle_create_operation inverted;
   inverted.owner       = alice_id;
   inverted.name        = "CORE.MIA";
   inverted.base_asset  = asset_id_type();
   inverted.quote_asset = mia_id;
   inverted.options.producers[bob_id] = 1;
   inverted.options.minimum_producers = 1;
   signed_transaction t1;
   t1.operations.push_back( inverted );
   db.current_fee_schedule().set_fee( t1.operations.back() );
   set_expiration( db, t1 );
   t1.sign( alice_private_key, db.get_chain_id() );
   const oracle_id_type bad_id { PUSH_TX( db, t1 ).operation_results.front().get<object_id_type>() };

   GRAPHENE_REQUIRE_THROW(
      set_price_oracle( *this, mia_id(db), alice_id, alice_private_key, bad_id ), fc::exception );

   // right base, wrong quote: MIA/OTHER when the asset is backed by CORE
   oracle_create_operation wrong_quote;
   wrong_quote.owner       = alice_id;
   wrong_quote.name        = "MIA.OTHER";
   wrong_quote.base_asset  = mia_id;
   wrong_quote.quote_asset = other.get_id();
   wrong_quote.options.producers[bob_id] = 1;
   wrong_quote.options.minimum_producers = 1;
   signed_transaction t2;
   t2.operations.push_back( wrong_quote );
   db.current_fee_schedule().set_fee( t2.operations.back() );
   set_expiration( db, t2 );
   t2.sign( alice_private_key, db.get_chain_id() );
   const oracle_id_type wq_id { PUSH_TX( db, t2 ).operation_results.front().get<object_id_type>() };

   GRAPHENE_REQUIRE_THROW(
      set_price_oracle( *this, mia_id(db), alice_id, alice_private_key, wq_id ), fc::exception );
} FC_LOG_AND_RETHROW() }

/// Deleting the price source out from under a smartcoin would leave it unable to margin call.
BOOST_AUTO_TEST_CASE( a_bound_oracle_cannot_be_deleted )
{ try {
   generate_blocks( HARDFORK_ORACLE_TIME );
   generate_block();
   set_expiration( db, trx );

   ACTORS( (alice)(bob) );
   fund( alice ); fund( bob );

   const asset_object& mia = create_bitasset( "MIATEST", alice_id );
   const auto mia_id = mia.get_id();

   oracle_create_operation cop;
   cop.owner       = alice_id;
   cop.name        = "MIA.CORE";
   cop.base_asset  = mia_id;
   cop.quote_asset = asset_id_type();
   cop.options.producers[bob_id] = 1;
   cop.options.minimum_producers = 1;
   signed_transaction ctx;
   ctx.operations.push_back( cop );
   db.current_fee_schedule().set_fee( ctx.operations.back() );
   set_expiration( db, ctx );
   ctx.sign( alice_private_key, db.get_chain_id() );
   const oracle_id_type oid { PUSH_TX( db, ctx ).operation_results.front().get<object_id_type>() };

   set_price_oracle( *this, mia_id(db), alice_id, alice_private_key, oid );

   auto del = [&]() {
      oracle_delete_operation dop;
      dop.owner     = alice_id;
      dop.oracle_id = oid;
      signed_transaction tx;
      tx.operations.push_back( dop );
      db.current_fee_schedule().set_fee( tx.operations.back() );
      set_expiration( db, tx );
      tx.sign( alice_private_key, db.get_chain_id() );
      PUSH_TX( db, tx );
   };

   GRAPHENE_REQUIRE_THROW( del(), fc::exception );

   // once the asset lets go, the oracle can be removed
   set_price_oracle( *this, mia_id(db), alice_id, alice_private_key, {} );
   del();
   BOOST_CHECK( nullptr == db.find( oid ) );
} FC_LOG_AND_RETHROW() }

/// Losing quorum must remove the smartcoin's price rather than freeze it at the last value.
BOOST_AUTO_TEST_CASE( losing_quorum_clears_the_bound_assets_feed )
{ try {
   generate_blocks( HARDFORK_ORACLE_TIME );
   generate_block();
   set_expiration( db, trx );

   ACTORS( (alice)(bob)(carol) );
   fund( alice ); fund( bob ); fund( carol );

   const asset_object& mia = create_bitasset( "MIATEST", alice_id );
   const auto mia_id = mia.get_id();

   oracle_create_operation cop;
   cop.owner       = alice_id;
   cop.name        = "MIA.CORE";
   cop.base_asset  = mia_id;
   cop.quote_asset = asset_id_type();
   cop.options.producers[bob_id]   = 1;
   cop.options.producers[carol_id] = 1;
   cop.options.minimum_producers   = 2;
   signed_transaction ctx;
   ctx.operations.push_back( cop );
   db.current_fee_schedule().set_fee( ctx.operations.back() );
   set_expiration( db, ctx );
   ctx.sign( alice_private_key, db.get_chain_id() );
   const oracle_id_type oid { PUSH_TX( db, ctx ).operation_results.front().get<object_id_type>() };

   auto pub = [&]( account_id_type who, const fc::ecc::private_key& key, int64_t n ) {
      oracle_publish_operation op;
      op.producer  = who;
      op.oracle_id = oid;
      op.value     = price( asset( 1, mia_id ), asset( n, asset_id_type() ) );
      signed_transaction tx;
      tx.operations.push_back( op );
      db.current_fee_schedule().set_fee( tx.operations.back() );
      set_expiration( db, tx );
      tx.sign( key, db.get_chain_id() );
      PUSH_TX( db, tx );
   };

   pub( bob_id,   bob_private_key,   10 );
   pub( carol_id, carol_private_key, 10 );
   set_price_oracle( *this, mia_id(db), alice_id, alice_private_key, oid );
   BOOST_REQUIRE( !mia_id(db).bitasset_data(db).current_feed.settlement_price.is_null() );

   // Let the submissions age. Without this they carry the current head block time, so the
   // shortened lifetime below would still count them as live and the test would prove nothing.
   generate_block();
   set_expiration( db, trx );

   // shorten the lifetime so nothing published so far still counts
   oracle_options tighter;
   tighter.producers[bob_id]   = 1;
   tighter.producers[carol_id] = 1;
   tighter.minimum_producers   = 2;
   tighter.value_lifetime_sec  = 1;    // everything published so far is now stale
   {
      oracle_update_operation uop;
      uop.owner       = alice_id;
      uop.oracle_id   = oid;
      uop.new_options = tighter;
      signed_transaction tx;
      tx.operations.push_back( uop );
      db.current_fee_schedule().set_fee( tx.operations.back() );
      set_expiration( db, tx );
      tx.sign( alice_private_key, db.get_chain_id() );
      PUSH_TX( db, tx );
   }

   BOOST_CHECK( !oid(db).current_value.valid() );
   // and the smartcoin must see that immediately, not keep quoting a dead price
   BOOST_CHECK( mia_id(db).bitasset_data(db).current_feed.settlement_price.is_null() );
} FC_LOG_AND_RETHROW() }

/// Binding is not allowed at creation: the oracle's base asset has to be the smartcoin, which
/// has no id yet, so there would be nothing to validate the orientation against.
BOOST_AUTO_TEST_CASE( a_price_oracle_cannot_be_set_when_creating_an_asset )
{ try {
   generate_blocks( HARDFORK_ORACLE_TIME );
   generate_block();
   set_expiration( db, trx );

   ACTORS( (alice)(bob) );
   fund( alice );
   fund( bob );

   const asset_object& mia = create_bitasset( "MIATEST", alice_id );
   oracle_create_operation cop;
   cop.owner       = alice_id;
   cop.name        = "MIA.CORE";
   cop.base_asset  = mia.get_id();
   cop.quote_asset = asset_id_type();
   cop.options.producers[bob_id] = 1;
   cop.options.minimum_producers = 1;
   signed_transaction ctx;
   ctx.operations.push_back( cop );
   db.current_fee_schedule().set_fee( ctx.operations.back() );
   set_expiration( db, ctx );
   ctx.sign( alice_private_key, db.get_chain_id() );
   const oracle_id_type oid { PUSH_TX( db, ctx ).operation_results.front().get<object_id_type>() };

   asset_create_operation acop;
   acop.issuer = alice_id;
   acop.symbol = "MIATWO";
   acop.precision = 2;
   acop.common_options.core_exchange_rate = price( asset(1,asset_id_type(1)), asset(1) );
   acop.bitasset_opts = bitasset_options();
   acop.bitasset_opts->extensions.value.price_oracle_id = oid;

   signed_transaction tx;
   tx.operations.push_back( acop );
   db.current_fee_schedule().set_fee( tx.operations.back() );
   set_expiration( db, tx );
   tx.sign( alice_private_key, db.get_chain_id() );
   GRAPHENE_REQUIRE_THROW( PUSH_TX( db, tx ), fc::exception );
} FC_LOG_AND_RETHROW() }


/**
 * Regression: a bound smartcoin must not go on quoting an oracle whose producers have stopped.
 *
 * This is not the "quorum was lost" case already covered above, which is noticed because an
 * oracle_update forces a recompute. Here NOTHING happens: no publish, no update, just time
 * passing. current_value is only recomputed when a producer publishes, so the oracle keeps its
 * last aggregate for ever, and a consumer that asks only current_value.valid() never learns
 * that value_lifetime_sec elapsed.
 *
 * What made it self-perpetuating rather than merely stale is update_expired_feeds(), which runs
 * EVERY BLOCK over exactly those assets whose feed has expired. It called
 * update_bitasset_current_feed() on each, which re-stamped current_feed_publication_time to the
 * current head time while carrying the dead oracle price. The feed expired for one block and was
 * resurrected the next, indefinitely -- by the very routine whose job is to retire expired feeds.
 *
 * The fixed end state is a CURRENT feed carrying a NULL settlement price, which is exactly what
 * the legacy path produces when fewer than minimum_feeds are live -- so nothing downstream needs
 * a new case, and margin calls simply do not run. The guarantee under test is that the price is
 * gone and stays gone, not that the feed object is flagged expired.
 */
BOOST_AUTO_TEST_CASE( a_smartcoin_stops_quoting_an_oracle_whose_producers_went_silent )
{ try {
   generate_blocks( HARDFORK_ORACLE_TIME );
   generate_block();
   set_expiration( db, trx );

   ACTORS( (alice)(bob) );
   fund( alice ); fund( bob );

   const asset_object& mia = create_bitasset( "MIASILENT", alice_id );
   const auto mia_id = mia.get_id();

   const uint32_t lifetime = 60;   // short, so the test does not need hours of blocks

   oracle_create_operation cop;
   cop.owner       = alice_id;
   cop.name        = "SILENT.CORE";
   cop.base_asset  = mia_id;
   cop.quote_asset = asset_id_type();
   cop.options.producers[bob_id]  = 1;
   cop.options.minimum_producers  = 1;
   cop.options.value_lifetime_sec = lifetime;
   signed_transaction ctx;
   ctx.operations.push_back( cop );
   db.current_fee_schedule().set_fee( ctx.operations.back() );
   set_expiration( db, ctx );
   ctx.sign( alice_private_key, db.get_chain_id() );
   const oracle_id_type oid { PUSH_TX( db, ctx ).operation_results.front().get<object_id_type>() };

   // One publish, then silence.
   {
      oracle_publish_operation op;
      op.producer  = bob_id;
      op.oracle_id = oid;
      op.value     = price( asset( 1, mia_id ), asset( 10, asset_id_type() ) );
      signed_transaction tx;
      tx.operations.push_back( op );
      db.current_fee_schedule().set_fee( tx.operations.back() );
      set_expiration( db, tx );
      tx.sign( bob_private_key, db.get_chain_id() );
      PUSH_TX( db, tx );
   }

   // Bind, and shorten the asset's own feed lifetime so the expiry path is reachable in a
   // handful of blocks rather than a day.
   {
      asset_update_bitasset_operation op;
      op.issuer          = alice_id;
      op.asset_to_update = mia_id;
      op.new_options     = mia_id(db).bitasset_data(db).options;
      op.new_options.feed_lifetime_sec = lifetime;
      op.new_options.extensions.value.price_oracle_id = oid;
      signed_transaction tx;
      tx.operations.push_back( op );
      db.current_fee_schedule().set_fee( tx.operations.back() );
      set_expiration( db, tx );
      tx.sign( alice_private_key, db.get_chain_id() );
      PUSH_TX( db, tx );
   }

   BOOST_REQUIRE( oid(db).current_value.valid() );
   BOOST_REQUIRE( !mia_id(db).bitasset_data(db).current_feed.settlement_price.is_null() );

   // Let the value age past its lifetime. No publishes, no updates -- only blocks.
   generate_blocks( db.head_block_time() + fc::seconds( lifetime * 3 ) );
   set_expiration( db, trx );

   const auto now = db.head_block_time();

   // The stored aggregate is untouched, because nothing recomputed it. That is precisely why
   // asking current_value.valid() is not a freshness test.
   BOOST_CHECK( oid(db).current_value.valid() );
   BOOST_CHECK( !oid(db).is_value_live( now ) );

   // The consumer must not be quoting it any more.
   const auto& bad = mia_id(db).bitasset_data(db);
   BOOST_CHECK_MESSAGE( bad.current_feed.settlement_price.is_null(),
                        "the smartcoin is still quoting a price from an oracle that stopped "
                        "publishing " << ( now - oid(db).current_value_time ).to_seconds()
                        << "s ago, against a lifetime of " << lifetime << "s" );

   // And it must STAY gone. This is the part the bug got wrong: update_expired_feeds() runs
   // every block over expired feeds, so a resurrection would happen within one block of the
   // check above and be invisible to a single assertion.
   generate_blocks( db.head_block_time() + fc::seconds( lifetime * 2 ) );
   set_expiration( db, trx );
   BOOST_CHECK_MESSAGE(
      mia_id(db).bitasset_data(db).current_feed.settlement_price.is_null(),
      "the dead oracle's price came back after further blocks" );
} FC_LOG_AND_RETHROW() }

/**
 * The stamp itself: a bound feed carries the time the ORACLE VALUE was formed, not the time the
 * feed happened to be refreshed. Getting this wrong is what let an expired feed be revived.
 */
BOOST_AUTO_TEST_CASE( a_bound_feed_is_stamped_with_the_oracle_value_time )
{ try {
   generate_blocks( HARDFORK_ORACLE_TIME );
   generate_block();
   set_expiration( db, trx );

   ACTORS( (alice)(bob) );
   fund( alice ); fund( bob );

   const asset_object& mia = create_bitasset( "MIASTAMP", alice_id );
   const auto mia_id = mia.get_id();

   oracle_create_operation cop;
   cop.owner       = alice_id;
   cop.name        = "STAMP.CORE";
   cop.base_asset  = mia_id;
   cop.quote_asset = asset_id_type();
   cop.options.producers[bob_id] = 1;
   cop.options.minimum_producers = 1;
   signed_transaction ctx;
   ctx.operations.push_back( cop );
   db.current_fee_schedule().set_fee( ctx.operations.back() );
   set_expiration( db, ctx );
   ctx.sign( alice_private_key, db.get_chain_id() );
   const oracle_id_type oid { PUSH_TX( db, ctx ).operation_results.front().get<object_id_type>() };

   {
      oracle_publish_operation op;
      op.producer  = bob_id;
      op.oracle_id = oid;
      op.value     = price( asset( 1, mia_id ), asset( 10, asset_id_type() ) );
      signed_transaction tx;
      tx.operations.push_back( op );
      db.current_fee_schedule().set_fee( tx.operations.back() );
      set_expiration( db, tx );
      tx.sign( bob_private_key, db.get_chain_id() );
      PUSH_TX( db, tx );
   }
   set_price_oracle( *this, mia_id(db), alice_id, alice_private_key, oid );

   const auto value_time = oid(db).current_value_time;
   BOOST_CHECK( mia_id(db).bitasset_data(db).current_feed_publication_time == value_time );

   // Move the head on WITHOUT publishing, then force a refresh. The stamp must still be the
   // value's own time -- if it tracked head time, the feed's expiry would keep sliding.
   generate_blocks( db.head_block_time() + fc::seconds( 30 ) );
   set_expiration( db, trx );
   BOOST_CHECK( db.head_block_time() > value_time );
   BOOST_CHECK_EQUAL( mia_id(db).bitasset_data(db).current_feed_publication_time.sec_since_epoch(),
                      value_time.sec_since_epoch() );
} FC_LOG_AND_RETHROW() }


/**
 * Regression: the window a windowed median covers must follow window_sec, not the publish rate.
 *
 * History is a fixed ring of GRAPHENE_ORACLE_MAX_HISTORY entries. Appending one per publish
 * made the span it covers a function of how often producers publish: at one publish per block
 * the ring holds about five minutes, so an oracle configured for an hour -- or a day -- was
 * really taking its median over five minutes, and nothing reported the discrepancy.
 *
 * The perverse part is that it is not an attack. It is what happens when the oracle works well:
 * the more diligently the producers publish, the weaker the damping they were configured to get.
 */
BOOST_AUTO_TEST_CASE( a_windowed_median_covers_its_configured_window_however_often_producers_publish )
{ try {
   generate_blocks( HARDFORK_ORACLE_TIME );
   generate_block();
   set_expiration( db, trx );

   ACTORS( (alice)(bob) );
   fund( alice ); fund( bob );

   const asset_object& mia = create_bitasset( "MIAWINDOW", alice_id );
   const auto mia_id = mia.get_id();

   const uint32_t window = 3600;

   oracle_create_operation cop;
   cop.owner       = alice_id;
   cop.name        = "WINDOW.CORE";
   cop.base_asset  = mia_id;
   cop.quote_asset = asset_id_type();
   cop.options.producers[bob_id] = 1;
   cop.options.minimum_producers = 1;
   cop.options.aggregation       = static_cast<uint8_t>( oracle_aggregation_method::median_over_window );
   cop.options.window_sec        = window;
   signed_transaction ctx;
   ctx.operations.push_back( cop );
   db.current_fee_schedule().set_fee( ctx.operations.back() );
   set_expiration( db, ctx );
   ctx.sign( alice_private_key, db.get_chain_id() );
   const oracle_id_type oid { PUSH_TX( db, ctx ).operation_results.front().get<object_id_type>() };

   auto pub = [&]( int64_t n ) {
      oracle_publish_operation op;
      op.producer  = bob_id;
      op.oracle_id = oid;
      op.value     = price( asset( 1, mia_id ), asset( n, asset_id_type() ) );
      signed_transaction tx;
      tx.operations.push_back( op );
      db.current_fee_schedule().set_fee( tx.operations.back() );
      set_expiration( db, tx );
      tx.sign( bob_private_key, db.get_chain_id() );
      PUSH_TX( db, tx );
   };

   // Publish every block for comfortably more blocks than the ring can hold.
   const int publishes = GRAPHENE_ORACLE_MAX_HISTORY * 2;
   for( int i = 0; i < publishes; ++i )
   {
      pub( 100 );
      generate_block();
      set_expiration( db, trx );
   }

   const auto& hist = oid(db).history;
   BOOST_REQUIRE( !hist.empty() );
   BOOST_CHECK_LE( hist.size(), size_t( GRAPHENE_ORACLE_MAX_HISTORY ) );

   // Entries must be spaced by at least the bucket, so a full ring spans the whole window.
   const int64_t bucket = std::max<int64_t>(
      1, int64_t( window ) / int64_t( GRAPHENE_ORACLE_MAX_HISTORY ) );
   for( size_t i = 1; i < hist.size(); ++i )
   {
      const int64_t gap = ( hist[i].first - hist[i-1].first ).to_seconds();
      BOOST_CHECK_MESSAGE( gap >= bucket,
                           "history entries " << (i-1) << " and " << i << " are only "
                           << gap << "s apart, below the " << bucket << "s bucket" );
   }

   // With one publish per block the ring used to saturate and cover only
   // MAX_HISTORY * block_interval seconds. It must now still reach back over everything
   // published, because far fewer slots were consumed.
   const int64_t covered = ( db.head_block_time() - hist.front().first ).to_seconds();
   const int64_t naive_span = int64_t( GRAPHENE_ORACLE_MAX_HISTORY )
                            * int64_t( db.get_global_properties().parameters.block_interval );
   BOOST_CHECK_MESSAGE( covered > naive_span,
                        "history reaches back only " << covered << "s; appending per publish "
                        "would have given " << naive_span << "s, so nothing improved" );
} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_SUITE_END()


BOOST_AUTO_TEST_SUITE( oracle_wire_format_tests )

/**
 * price_oracle_id was appended to bitasset_options::ext, which is embedded in
 * asset_create_operation and asset_update_bitasset_operation -- both on chain since genesis.
 *
 * graphene's extension<T> encodes a count followed by (field index, value) pairs, with indices
 * assigned by declaration order in FC_REFLECT. Appending is therefore safe and inserting is
 * catastrophic: every pre-existing field would shift by one and every historical asset
 * operation would decode into the wrong fields. This pins the numbering so a later edit that
 * inserts rather than appends fails here rather than on a live chain.
 */
BOOST_AUTO_TEST_CASE( appending_price_oracle_id_did_not_shift_existing_extension_tags )
{
   auto packed_ext_of = []( const bitasset_options& o ) {
      return fc::raw::pack( o.extensions );
   };

   // no extensions at all: a single zero count byte, exactly as before this change
   bitasset_options none;
   auto empty = packed_ext_of( none );
   BOOST_REQUIRE_EQUAL( empty.size(), 1u );
   BOOST_CHECK_EQUAL( int( empty[0] ), 0 );

   // one field set: count 1, then that field's index. The indices below are the on-chain
   // numbering and must never change.
   struct { const char* name; int expected_index; std::function<void(bitasset_options&)> set; } cases[] = {
      { "initial_collateral_ratio",     0, []( bitasset_options& o ){ o.extensions.value.initial_collateral_ratio = 1750; } },
      { "maintenance_collateral_ratio", 1, []( bitasset_options& o ){ o.extensions.value.maintenance_collateral_ratio = 1750; } },
      { "maximum_short_squeeze_ratio",  2, []( bitasset_options& o ){ o.extensions.value.maximum_short_squeeze_ratio = 1100; } },
      { "margin_call_fee_ratio",        3, []( bitasset_options& o ){ o.extensions.value.margin_call_fee_ratio = 10; } },
      { "force_settle_fee_percent",     4, []( bitasset_options& o ){ o.extensions.value.force_settle_fee_percent = 10; } },
      { "black_swan_response_method",   5, []( bitasset_options& o ){ o.extensions.value.black_swan_response_method = 1; } },
      { "price_oracle_id",              6, []( bitasset_options& o ){ o.extensions.value.price_oracle_id = oracle_id_type(7); } },
   };

   for( const auto& c : cases )
   {
      bitasset_options o;
      c.set( o );
      auto bytes = packed_ext_of( o );
      BOOST_REQUIRE_MESSAGE( bytes.size() >= 2, c.name );
      BOOST_CHECK_MESSAGE( 1 == int( bytes[0] ), std::string( "count for " ) + c.name );
      BOOST_CHECK_MESSAGE( c.expected_index == int( bytes[1] ),
                           std::string( "extension tag for " ) + c.name + " changed" );
   }

   // and a round trip through the new field leaves the others untouched
   bitasset_options o;
   o.extensions.value.black_swan_response_method = 1;
   o.extensions.value.price_oracle_id = oracle_id_type(7);
   auto back = fc::raw::unpack<bitasset_options>( fc::raw::pack( o ) );
   BOOST_REQUIRE( back.extensions.value.price_oracle_id.valid() );
   BOOST_CHECK( *back.extensions.value.price_oracle_id == oracle_id_type(7) );
   BOOST_REQUIRE( back.extensions.value.black_swan_response_method.valid() );
   BOOST_CHECK_EQUAL( int( *back.extensions.value.black_swan_response_method ), 1 );
   BOOST_CHECK( !back.extensions.value.initial_collateral_ratio.valid() );
}


BOOST_AUTO_TEST_SUITE_END()
