/*
 * Futures markets: margin-traded contracts settled against an oracle.
 *
 * The mark price is the load-bearing part of stage one. Liquidation is assessed against it, so
 * the tests that matter are the ones about where it comes from, what happens when it is not
 * there, and what a market owner is and is not allowed to change underneath open positions.
 */

#include <boost/test/unit_test.hpp>

#include <graphene/chain/database.hpp>
#include <graphene/chain/hardfork.hpp>
#include <graphene/chain/futures_object.hpp>
#include <graphene/chain/futures_evaluator.hpp>   // futures_margin_required
#include <graphene/chain/oracle_object.hpp>
#include <graphene/protocol/futures.hpp>

#include "../common/database_fixture.hpp"

using namespace graphene::chain;
using namespace graphene::chain::test;

BOOST_AUTO_TEST_SUITE( futures_symbol_tests )

BOOST_AUTO_TEST_CASE( valid_and_invalid_futures_symbols )
{
   BOOST_CHECK( is_valid_futures_symbol( "BTC-PERP" ) );
   BOOST_CHECK( is_valid_futures_symbol( "BTC-2026.03" ) );
   BOOST_CHECK( is_valid_futures_symbol( "ETH" ) );

   BOOST_CHECK( !is_valid_futures_symbol( "AB" ) );
   BOOST_CHECK( !is_valid_futures_symbol( "btc-perp" ) );
   BOOST_CHECK( !is_valid_futures_symbol( "BTC PERP" ) );
   BOOST_CHECK( !is_valid_futures_symbol( "-BTCPERP" ) );
   BOOST_CHECK( !is_valid_futures_symbol( "BTCPERP-" ) );
   BOOST_CHECK( !is_valid_futures_symbol( "BTC--PERP" ) );
}

/// Margin ratios are the only thing standing between a leveraged market and insolvency, so the
/// nonsensical combinations have to be rejected rather than merely discouraged.
BOOST_AUTO_TEST_CASE( margin_ratio_validation )
{
   futures_market_options ok;
   BOOST_CHECK_NO_THROW( ok.validate() );

   // maintenance at or above initial means a position is liquidatable the moment it opens
   futures_market_options equal;
   equal.initial_margin_ratio = 1000;
   equal.maintenance_margin_ratio = 1000;
   BOOST_CHECK_THROW( equal.validate(), fc::exception );

   futures_market_options inverted;
   inverted.initial_margin_ratio = 500;
   inverted.maintenance_margin_ratio = 1000;
   BOOST_CHECK_THROW( inverted.validate(), fc::exception );

   // leverage beyond the cap
   futures_market_options too_much_leverage;
   too_much_leverage.initial_margin_ratio = 10;   // 1000x
   too_much_leverage.maintenance_margin_ratio = 5;
   BOOST_CHECK_THROW( too_much_leverage.validate(), fc::exception );

   futures_market_options zero_maintenance;
   zero_maintenance.maintenance_margin_ratio = 0;
   BOOST_CHECK_THROW( zero_maintenance.validate(), fc::exception );
}


BOOST_AUTO_TEST_SUITE_END()

namespace {

/// Shared setup: an oracle quoting a test asset against CORE, and a market on top of it.
struct futures_fixture : database_fixture
{
   asset_id_type core_id;
   asset_id_type btc_id;

   void setup_assets()
   {
      core_id = asset_id_type();
      btc_id  = create_user_issued_asset( "BTCTEST" ).get_id();
   }

   oracle_id_type make_oracle( account_id_type owner, const fc::ecc::private_key& key,
                               account_id_type producer, const string& name = "BTC.CORE",
                               const optional<uint32_t>& value_lifetime_sec = {} )
   {
      oracle_create_operation op;
      op.owner       = owner;
      op.name        = name;
      op.base_asset  = btc_id;
      op.quote_asset = core_id;
      op.options.producers[producer] = 1;
      op.options.minimum_producers = 1;
      if( value_lifetime_sec.valid() )
         op.options.value_lifetime_sec = *value_lifetime_sec;
      signed_transaction tx;
      tx.operations.push_back( op );
      db.current_fee_schedule().set_fee( tx.operations.back() );
      set_expiration( db, tx );
      tx.sign( key, db.get_chain_id() );
      return oracle_id_type { PUSH_TX( db, tx ).operation_results.front().get<object_id_type>() };
   }

   /// Publishes "one BTC is worth `core` CORE".
   void publish( oracle_id_type oid, account_id_type who, const fc::ecc::private_key& key,
                 int64_t core )
   {
      oracle_publish_operation op;
      op.producer  = who;
      op.oracle_id = oid;
      op.value     = price( asset( 1, btc_id ), asset( core, core_id ) );
      signed_transaction tx;
      tx.operations.push_back( op );
      db.current_fee_schedule().set_fee( tx.operations.back() );
      set_expiration( db, tx );
      tx.sign( key, db.get_chain_id() );
      PUSH_TX( db, tx );
   }

   /// Places an order and returns the resulting order id, or a null id if it fully filled.
   object_id_type place( futures_market_id_type mid, account_id_type who,
                         const fc::ecc::private_key& key, bool is_long,
                         int64_t price, int64_t size, bool fok = false )
   {
      futures_order_create_operation op;
      op.owner              = who;
      op.market_id          = mid;
      op.is_long            = is_long;
      op.price_per_contract = price;
      op.size               = size;
      op.fill_or_kill       = fok;
      signed_transaction tx;
      tx.operations.push_back( op );
      db.current_fee_schedule().set_fee( tx.operations.back() );
      set_expiration( db, tx );
      tx.sign( key, db.get_chain_id() );
      return PUSH_TX( db, tx ).operation_results.front().get<object_id_type>();
   }

   void cancel( futures_order_id_type oid, account_id_type who, const fc::ecc::private_key& key )
   {
      futures_order_cancel_operation op;
      op.owner    = who;
      op.order_id = oid;
      signed_transaction tx;
      tx.operations.push_back( op );
      db.current_fee_schedule().set_fee( tx.operations.back() );
      set_expiration( db, tx );
      tx.sign( key, db.get_chain_id() );
      PUSH_TX( db, tx );
   }

   void adjust_margin( futures_position_id_type pid, account_id_type who,
                       const fc::ecc::private_key& key, int64_t delta )
   {
      futures_position_adjust_margin_operation op;
      op.owner       = who;
      op.position_id = pid;
      op.delta       = delta;
      signed_transaction tx;
      tx.operations.push_back( op );
      db.current_fee_schedule().set_fee( tx.operations.back() );
      set_expiration( db, tx );
      tx.sign( key, db.get_chain_id() );
      PUSH_TX( db, tx );
   }

   void liquidate( futures_position_id_type pid, account_id_type who,
                   const fc::ecc::private_key& key )
   {
      futures_liquidate_operation op;
      op.liquidator  = who;
      op.position_id = pid;
      signed_transaction tx;
      tx.operations.push_back( op );
      db.current_fee_schedule().set_fee( tx.operations.back() );
      set_expiration( db, tx );
      tx.sign( key, db.get_chain_id() );
      PUSH_TX( db, tx );
   }

   void settle_market( futures_market_id_type mid, account_id_type who,
                       const fc::ecc::private_key& key,
                       const optional<futures_position_id_type>& pid = {} )
   {
      futures_settle_operation op;
      op.payer       = who;
      op.market_id   = mid;
      op.position_id = pid;
      signed_transaction tx;
      tx.operations.push_back( op );
      db.current_fee_schedule().set_fee( tx.operations.back() );
      set_expiration( db, tx );
      tx.sign( key, db.get_chain_id() );
      PUSH_TX( db, tx );
   }

   const futures_position_object* position_of( futures_market_id_type mid, account_id_type who )
   {
      const auto& idx = db.get_index_type<futures_position_index>().indices()
                          .get<by_market_owner>();
      auto itr = idx.find( boost::make_tuple( mid, who ) );
      return itr == idx.end() ? nullptr : &(*itr);
   }

   /**
    * Every contract has a long and a short, so sizes must net to zero and open interest must
    * equal the long side. Deliberately NOT asserting that entry values sum to zero: that is
    * only true while closes are symmetric, and believing it hid a real insolvency. Conservation
    * of collateral is checked by verify_asset_supplies, which the fixture runs anyway.
    */
   void check_market_is_balanced( futures_market_id_type mid )
   {
      share_type total_size = 0;
      share_type long_contracts = 0;
      const auto& idx = db.get_index_type<futures_position_index>().indices().get<by_id>();
      for( const auto& p : idx )
      {
         if( p.market_id != mid ) continue;
         total_size += p.size;
         if( p.size > 0 ) long_contracts += p.size;
      }
      BOOST_CHECK_MESSAGE( 0 == total_size.value,
                           "sum of position sizes is " + std::to_string( total_size.value ) );
      BOOST_CHECK_EQUAL( long_contracts.value, mid(db).open_interest.value );
   }

   /// Market options with the mark limit switched off.
   ///
   /// The limit is ON by default, which is the point of it. Tests about liquidation, funding
   /// or the oracle-to-mark conversion need the mark to actually REACH a price, and leaving
   /// damping on would quietly make them tests of damping instead. Disabling it here isolates
   /// what each one is for; damping has its own tests.
   static futures_market_options undamped()
   {
      futures_market_options o;
      o.max_mark_move_ppm = 0;
      return o;
   }

   futures_market_id_type make_market( account_id_type owner, const fc::ecc::private_key& key,
                                       oracle_id_type oid, share_type contract_size,
                                       const optional<time_point_sec>& expiry = {},
                                       const string& symbol = "BTC-PERP",
                                       const optional<futures_market_options>& options = {} )
   {
      futures_market_create_operation op;
      op.owner            = owner;
      op.symbol           = symbol;
      op.oracle_id        = oid;
      op.collateral_asset = core_id;
      op.contract_size    = contract_size;
      op.expiry           = expiry;
      if( options.valid() )
         op.options       = *options;
      signed_transaction tx;
      tx.operations.push_back( op );
      db.current_fee_schedule().set_fee( tx.operations.back() );
      set_expiration( db, tx );
      tx.sign( key, db.get_chain_id() );
      return futures_market_id_type {
         PUSH_TX( db, tx ).operation_results.front().get<object_id_type>() };
   }
};

} // namespace

BOOST_FIXTURE_TEST_SUITE( futures_tests, futures_fixture )

BOOST_AUTO_TEST_CASE( futures_ops_are_refused_before_the_hardfork )
{ try {
   // past the oracle fork so an oracle can exist, but not the futures fork
   generate_blocks( HARDFORK_ORACLE_TIME );
   generate_block();
   set_expiration( db, trx );
   setup_assets();

   ACTORS( (alice)(bob) );
   fund( alice ); fund( bob );

   const auto oid = make_oracle( alice_id, alice_private_key, bob_id );

   // The futures fork is deliberately later than the oracle fork: a market's mark price comes
   // from an oracle, so futures cannot activate first. This asserts that ordering as well as
   // the gate itself -- if the two dates were ever set equal, this test would say so.
   BOOST_REQUIRE( HARDFORK_FUTURES_TIME > HARDFORK_ORACLE_TIME );
   BOOST_REQUIRE( !HARDFORK_FUTURES_PASSED( db.head_block_time() ) );
   GRAPHENE_REQUIRE_THROW( make_market( alice_id, alice_private_key, oid, 1 ), fc::exception );
} FC_LOG_AND_RETHROW() }

/// The mark price is an integer amount of collateral per contract, derived from the oracle's
/// ratio and the contract size. This is the one conversion in the whole design.
BOOST_AUTO_TEST_CASE( the_mark_price_comes_from_the_oracle_and_the_contract_size )
{ try {
   generate_blocks( HARDFORK_FUTURES_TIME );
   generate_block();
   set_expiration( db, trx );
   setup_assets();

   ACTORS( (alice)(bob) );
   fund( alice ); fund( bob );

   const auto oid = make_oracle( alice_id, alice_private_key, bob_id );
   publish( oid, bob_id, bob_private_key, 50000 );   // 1 BTC = 50000 CORE

   // a contract worth 10 units of the base asset
   const auto mid = make_market( alice_id, alice_private_key, oid, 10, {}, "BTC-PERP", undamped() );

   BOOST_REQUIRE( mid(db).mark_price.valid() );
   BOOST_CHECK_EQUAL( mid(db).mark_price->value, 500000 );   // 10 x 50000
   BOOST_CHECK( mid(db).is_tradable( db.head_block_time() ) );

   // a new oracle value moves the mark in the same block it is published
   publish( oid, bob_id, bob_private_key, 60000 );
   BOOST_REQUIRE( mid(db).mark_price.valid() );
   BOOST_CHECK_EQUAL( mid(db).mark_price->value, 600000 );
} FC_LOG_AND_RETHROW() }

/// Without a mark there is no risk measure, so the market must stop rather than trade against
/// a price nobody is asserting.
BOOST_AUTO_TEST_CASE( a_market_without_a_mark_price_is_not_tradable )
{ try {
   generate_blocks( HARDFORK_FUTURES_TIME );
   generate_block();
   set_expiration( db, trx );
   setup_assets();

   ACTORS( (alice)(bob) );
   fund( alice ); fund( bob );

   // an oracle that has never had a value published to it
   const auto oid = make_oracle( alice_id, alice_private_key, bob_id );
   const auto mid = make_market( alice_id, alice_private_key, oid, 1 );

   BOOST_CHECK( !mid(db).mark_price.valid() );
   BOOST_CHECK( !mid(db).is_tradable( db.head_block_time() ) );

   // once a value exists the market comes to life
   publish( oid, bob_id, bob_private_key, 1000 );
   BOOST_REQUIRE( mid(db).mark_price.valid() );
   BOOST_CHECK( mid(db).is_tradable( db.head_block_time() ) );

   // and if the oracle loses quorum the mark goes away again rather than going stale
   oracle_options no_producers;
   no_producers.producers.clear();
   no_producers.minimum_producers = 1;
   oracle_update_operation uop;
   uop.owner       = alice_id;
   uop.oracle_id   = oid;
   uop.new_options = no_producers;
   signed_transaction tx;
   tx.operations.push_back( uop );
   db.current_fee_schedule().set_fee( tx.operations.back() );
   set_expiration( db, tx );
   tx.sign( alice_private_key, db.get_chain_id() );
   PUSH_TX( db, tx );

   BOOST_CHECK( !oid(db).current_value.valid() );
   BOOST_CHECK( !mid(db).mark_price.valid() );
   BOOST_CHECK( !mid(db).is_tradable( db.head_block_time() ) );
} FC_LOG_AND_RETHROW() }

/// Margin and PnL are in the collateral asset; the oracle must quote against that same asset
/// so no unit conversion -- and therefore no extra rounding -- ever enters position accounting.
BOOST_AUTO_TEST_CASE( the_oracle_must_quote_against_the_collateral_asset )
{ try {
   generate_blocks( HARDFORK_FUTURES_TIME );
   generate_block();
   set_expiration( db, trx );
   setup_assets();

   ACTORS( (alice)(bob) );
   fund( alice ); fund( bob );

   const auto other_id = create_user_issued_asset( "OTHERTEST" ).get_id();

   // oracle quotes BTC/OTHER, but the market wants to margin in CORE
   oracle_create_operation oop;
   oop.owner       = alice_id;
   oop.name        = "BTC.OTHER";
   oop.base_asset  = btc_id;
   oop.quote_asset = other_id;
   oop.options.producers[bob_id] = 1;
   oop.options.minimum_producers = 1;
   signed_transaction otx;
   otx.operations.push_back( oop );
   db.current_fee_schedule().set_fee( otx.operations.back() );
   set_expiration( db, otx );
   otx.sign( alice_private_key, db.get_chain_id() );
   const oracle_id_type mismatched {
      PUSH_TX( db, otx ).operation_results.front().get<object_id_type>() };

   GRAPHENE_REQUIRE_THROW( make_market( alice_id, alice_private_key, mismatched, 1 ),
                           fc::exception );
} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE( futures_symbols_are_unique_and_only_the_owner_may_update )
{ try {
   generate_blocks( HARDFORK_FUTURES_TIME );
   generate_block();
   set_expiration( db, trx );
   setup_assets();

   ACTORS( (alice)(bob) );
   fund( alice ); fund( bob );

   const auto oid = make_oracle( alice_id, alice_private_key, bob_id );
   publish( oid, bob_id, bob_private_key, 1000 );
   const auto mid = make_market( alice_id, alice_private_key, oid, 1 );

   // same symbol, different owner
   GRAPHENE_REQUIRE_THROW( make_market( bob_id, bob_private_key, oid, 1 ), fc::exception );

   auto update_as = [&]( account_id_type who, const fc::ecc::private_key& key ) {
      futures_market_update_operation op;
      op.owner       = who;
      op.market_id   = mid;
      futures_market_options opts = mid(db).options;
      opts.enabled = false;
      op.new_options = opts;
      signed_transaction tx;
      tx.operations.push_back( op );
      db.current_fee_schedule().set_fee( tx.operations.back() );
      set_expiration( db, tx );
      tx.sign( key, db.get_chain_id() );
      PUSH_TX( db, tx );
   };

   GRAPHENE_REQUIRE_THROW( update_as( bob_id, bob_private_key ), fc::exception );

   update_as( alice_id, alice_private_key );
   BOOST_CHECK( !mid(db).options.enabled );
   // halting a market stops trading without destroying anything
   BOOST_CHECK( !mid(db).is_tradable( db.head_block_time() ) );
} FC_LOG_AND_RETHROW() }

/// A market's oracle is fixed at creation, so unlike a smartcoin there is no "unbind first".
/// Deleting the oracle would strand the market without a mark price permanently.
BOOST_AUTO_TEST_CASE( an_oracle_feeding_a_futures_market_cannot_be_deleted )
{ try {
   generate_blocks( HARDFORK_FUTURES_TIME );
   generate_block();
   set_expiration( db, trx );
   setup_assets();

   ACTORS( (alice)(bob) );
   fund( alice ); fund( bob );

   const auto oid = make_oracle( alice_id, alice_private_key, bob_id );
   publish( oid, bob_id, bob_private_key, 1000 );
   make_market( alice_id, alice_private_key, oid, 1 );

   oracle_delete_operation dop;
   dop.owner     = alice_id;
   dop.oracle_id = oid;
   signed_transaction tx;
   tx.operations.push_back( dop );
   db.current_fee_schedule().set_fee( tx.operations.back() );
   set_expiration( db, tx );
   tx.sign( alice_private_key, db.get_chain_id() );
   GRAPHENE_REQUIRE_THROW( PUSH_TX( db, tx ), fc::exception );
} FC_LOG_AND_RETHROW() }

/// Expiry is a promise to the people holding the contract.
BOOST_AUTO_TEST_CASE( expiry_must_be_in_the_future_and_within_range )
{ try {
   generate_blocks( HARDFORK_FUTURES_TIME );
   generate_block();
   set_expiration( db, trx );
   setup_assets();

   ACTORS( (alice)(bob) );
   fund( alice ); fund( bob );

   const auto oid = make_oracle( alice_id, alice_private_key, bob_id );
   publish( oid, bob_id, bob_private_key, 1000 );
   const auto now = db.head_block_time();

   GRAPHENE_REQUIRE_THROW(
      make_market( alice_id, alice_private_key, oid, 1, now - 1, "BTC-PAST" ), fc::exception );
   GRAPHENE_REQUIRE_THROW(
      make_market( alice_id, alice_private_key, oid, 1,
                   now + fc::days( GRAPHENE_FUTURES_MAX_EXPIRY_DAYS + 1 ), "BTC-FAR" ),
      fc::exception );

   const auto dated = make_market( alice_id, alice_private_key, oid, 1,
                                   now + fc::days( 30 ), "BTC-2026" );
   BOOST_CHECK( !dated(db).is_perpetual() );
   BOOST_CHECK( dated(db).is_tradable( db.head_block_time() ) );

   // past its expiry a dated contract stops accepting trades
   BOOST_CHECK( !dated(db).is_tradable( *dated(db).expiry ) );
   BOOST_CHECK( !dated(db).is_tradable( *dated(db).expiry + 1 ) );
} FC_LOG_AND_RETHROW() }


/**
 * The mark must not be whatever the oracle last said.
 *
 * It is the price margin, liquidation and settlement are all measured against, so an outlier
 * print -- manipulated, or a genuine wick that reverts next block -- used to become the mark
 * immediately and could cascade liquidations across every position before reverting.
 *
 * With a limit set, a spike is clipped to what the elapsed time allows and a move that is real
 * still arrives in full, just over seconds instead of instantly.
 */
BOOST_AUTO_TEST_CASE( the_mark_is_rate_limited_when_a_limit_is_set )
{ try {
   generate_blocks( HARDFORK_FUTURES_TIME );
   generate_block();
   set_expiration( db, trx );
   setup_assets();

   ACTORS( (alice)(bob) );
   fund( alice, asset(10000000) ); fund( bob, asset(10000000) );

   const auto oid = make_oracle( alice_id, alice_private_key, bob_id );
   publish( oid, bob_id, bob_private_key, 100000 );

   futures_market_options opts;
   opts.max_mark_move_ppm = 1000;          // 0.1% of the mark per second
   const auto mid = make_market( alice_id, alice_private_key, oid, 1, {}, "BTC-DAMP", opts );

   BOOST_REQUIRE( mid(db).mark_price.valid() );
   BOOST_CHECK_EQUAL( mid(db).mark_price->value, 100000 );   // first mark is taken as-is

   // A block later, publish a print twice the price. Five seconds at 0.1%/s allows 0.5%.
   generate_block();
   set_expiration( db, trx );
   publish( oid, bob_id, bob_private_key, 200000 );

   const int64_t after_spike = mid(db).mark_price->value;
   BOOST_CHECK_MESSAGE( after_spike < 102000,
                        "a doubling print moved the mark to " << after_spike
                        << "; the limit should have held it near 100500" );
   BOOST_CHECK_GT( after_spike, 100000 );   // but it does move toward it

   // The spike reverts. The mark never went anywhere near it.
   publish( oid, bob_id, bob_private_key, 100000 );
   generate_block();
   set_expiration( db, trx );
   publish( oid, bob_id, bob_private_key, 100000 );
   BOOST_CHECK_EQUAL( mid(db).mark_price->value, 100000 );

   // A move that is real arrives in full: hold 110000 and let time pass.
   for( int i = 0; i < 40; ++i )
   {
      generate_block();
      set_expiration( db, trx );
      publish( oid, bob_id, bob_private_key, 110000 );
   }
   BOOST_CHECK_EQUAL( mid(db).mark_price->value, 110000 );
} FC_LOG_AND_RETHROW() }

/// An owner may switch the limit off, and then the mark tracks the oracle exactly. This is
/// the deliberate opt-out, not the default -- see the default asserted below.
BOOST_AUTO_TEST_CASE( the_mark_is_undamped_when_the_limit_is_switched_off )
{ try {
   generate_blocks( HARDFORK_FUTURES_TIME );
   generate_block();
   set_expiration( db, trx );
   setup_assets();

   ACTORS( (alice)(bob) );
   fund( alice, asset(10000000) ); fund( bob, asset(10000000) );

   const auto oid = make_oracle( alice_id, alice_private_key, bob_id );
   publish( oid, bob_id, bob_private_key, 100000 );
   const auto mid = make_market( alice_id, alice_private_key, oid, 1, {}, "BTC-PERP", undamped() );
   BOOST_CHECK_EQUAL( mid(db).options.max_mark_move_ppm, 0u );

   generate_block();
   set_expiration( db, trx );
   publish( oid, bob_id, bob_private_key, 200000 );
   BOOST_CHECK_EQUAL( mid(db).mark_price->value, 200000 );
} FC_LOG_AND_RETHROW() }

/**
 * The default is ON, and it is a specific number rather than whatever fell out.
 *
 * 1000 ppm/s clips a one-block spike to 0.5%, prices a genuine 10% move in within 100 seconds,
 * and allows 6% over a minute. Tighter and liquidations lag a real crash, leaving bad debt;
 * looser and a single print still reprices everything. A market that opts out has to say so.
 */
BOOST_AUTO_TEST_CASE( a_market_is_rate_limited_by_default )
{ try {
   generate_blocks( HARDFORK_FUTURES_TIME );
   generate_block();
   set_expiration( db, trx );
   setup_assets();

   ACTORS( (alice)(bob) );
   fund( alice, asset(10000000) ); fund( bob, asset(10000000) );

   const auto oid = make_oracle( alice_id, alice_private_key, bob_id );
   publish( oid, bob_id, bob_private_key, 100000 );
   const auto mid = make_market( alice_id, alice_private_key, oid, 1 );

   BOOST_CHECK_EQUAL( mid(db).options.max_mark_move_ppm, 1000u );

   // A market created without saying anything about it is protected: a doubling print does
   // not become the price every position is measured against.
   generate_block();
   set_expiration( db, trx );
   publish( oid, bob_id, bob_private_key, 200000 );
   BOOST_CHECK_MESSAGE( mid(db).mark_price->value < 101000,
                        "the default did not damp: mark went to "
                        << mid(db).mark_price->value );
} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_SUITE_END()


BOOST_FIXTURE_TEST_SUITE( futures_trading_tests, futures_fixture )

namespace {
   constexpr int64_t IMR = 1000;   // 10% initial margin, the futures_market_options default
}

/// A crossing pair of orders creates one long and one short at the maker's price.
BOOST_AUTO_TEST_CASE( a_crossing_pair_opens_two_positions )
{ try {
   generate_blocks( HARDFORK_FUTURES_TIME );
   generate_block();
   set_expiration( db, trx );
   setup_assets();

   ACTORS( (alice)(bob)(carol) );
   fund( alice ); fund( bob ); fund( carol );

   const auto oid = make_oracle( alice_id, alice_private_key, bob_id );
   publish( oid, bob_id, bob_private_key, 100 );
   const auto mid = make_market( alice_id, alice_private_key, oid, 1 );

   const auto bob_before   = db.get_balance( bob_id, core_id ).amount;
   const auto carol_before = db.get_balance( carol_id, core_id ).amount;

   // bob rests a bid for 10 contracts at 100; carol sells into it at 90
   place( mid, bob_id, bob_private_key, true, 100, 10 );
   BOOST_CHECK( nullptr == position_of( mid, bob_id ) );   // nothing crossed yet
   place( mid, carol_id, carol_private_key, false, 90, 10 );

   const auto* bob_pos   = position_of( mid, bob_id );
   const auto* carol_pos = position_of( mid, carol_id );
   BOOST_REQUIRE( nullptr != bob_pos );
   BOOST_REQUIRE( nullptr != carol_pos );

   // filled at the MAKER's price of 100, not the taker's 90
   BOOST_CHECK_EQUAL( bob_pos->size.value, 10 );
   BOOST_CHECK_EQUAL( bob_pos->entry_value.value, 1000 );
   BOOST_CHECK_EQUAL( carol_pos->size.value, -10 );
   BOOST_CHECK_EQUAL( carol_pos->entry_value.value, -1000 );

   // 10% of the 1000 notional, for both sides. Carol asked 90 and so reserved only 90; she
   // filled at 100, which is the better price for a seller but a larger notional, and the
   // extra 10 was taken from her balance rather than letting her open under-margined.
   BOOST_CHECK_EQUAL( bob_pos->margin.value, 100 );
   BOOST_CHECK_EQUAL( carol_pos->margin.value, 100 );

   BOOST_CHECK_EQUAL( mid(db).open_interest.value, 10 );
   check_market_is_balanced( mid );

   // margin actually left their balances (fees aside, which are charged in core too, so
   // compare the margin component by checking it is at least the margin)
   BOOST_CHECK( db.get_balance( bob_id, core_id ).amount <= bob_before - 100 );
   BOOST_CHECK( db.get_balance( carol_id, core_id ).amount <= carol_before - 100 );
} FC_LOG_AND_RETHROW() }

/// Closing a position pays out margin plus realised PnL exactly, with no rounding.
BOOST_AUTO_TEST_CASE( closing_a_position_pays_out_margin_plus_pnl )
{ try {
   generate_blocks( HARDFORK_FUTURES_TIME );
   generate_block();
   set_expiration( db, trx );
   setup_assets();

   ACTORS( (alice)(bob)(carol) );
   fund( alice, asset(10000000) ); fund( bob, asset(10000000) ); fund( carol, asset(10000000) );

   const auto oid = make_oracle( alice_id, alice_private_key, bob_id );
   publish( oid, bob_id, bob_private_key, 100 );
   const auto mid = make_market( alice_id, alice_private_key, oid, 1 );

   // open: bob long 10 @ 100, carol short 10 @ 100
   place( mid, bob_id, bob_private_key, true, 100, 10 );
   place( mid, carol_id, carol_private_key, false, 100, 10 );
   check_market_is_balanced( mid );

   // close at 120: bob sells 10, carol buys 10. Bob is up 10 x (120-100) = 200.
   const auto bob_before = db.get_balance( bob_id, core_id ).amount;
   place( mid, carol_id, carol_private_key, true, 120, 10 );   // carol bids to close
   place( mid, bob_id, bob_private_key, false, 120, 10 );      // bob sells into it

   BOOST_CHECK( nullptr == position_of( mid, bob_id ) );
   BOOST_CHECK( nullptr == position_of( mid, carol_id ) );
   BOOST_CHECK_EQUAL( mid(db).open_interest.value, 0 );

   // bob gets back his 100 margin plus 200 profit; the fee is separate and small
   const auto bob_gain = db.get_balance( bob_id, core_id ).amount - bob_before;
   BOOST_CHECK_MESSAGE( bob_gain > 250 && bob_gain <= 300,
                        "bob's payout was " + std::to_string( bob_gain.value ) );

   check_market_is_balanced( mid );
} FC_LOG_AND_RETHROW() }

/// A partial fill leaves the remainder resting, and cancelling returns exactly what is left.
BOOST_AUTO_TEST_CASE( partial_fills_rest_and_cancel_returns_the_reservation )
{ try {
   generate_blocks( HARDFORK_FUTURES_TIME );
   generate_block();
   set_expiration( db, trx );
   setup_assets();

   ACTORS( (alice)(bob)(carol) );
   fund( alice, asset(10000000) ); fund( bob, asset(10000000) ); fund( carol, asset(10000000) );

   const auto oid = make_oracle( alice_id, alice_private_key, bob_id );
   publish( oid, bob_id, bob_private_key, 100 );
   const auto mid = make_market( alice_id, alice_private_key, oid, 1 );

   // bob bids 10 @ 100; carol sells only 4
   const auto raw = place( mid, bob_id, bob_private_key, true, 100, 10 );
   const futures_order_id_type bob_order { raw };
   place( mid, carol_id, carol_private_key, false, 100, 4 );

   BOOST_REQUIRE( nullptr != db.find( bob_order ) );
   BOOST_CHECK_EQUAL( bob_order(db).size.value, 6 );
   // 10 contracts were reserved at 10%; 4 filled, so 60 of the 100 remains reserved
   BOOST_CHECK_EQUAL( bob_order(db).deferred_margin.value, 60 );

   const auto* bob_pos = position_of( mid, bob_id );
   BOOST_REQUIRE( nullptr != bob_pos );
   BOOST_CHECK_EQUAL( bob_pos->size.value, 4 );
   BOOST_CHECK_EQUAL( bob_pos->margin.value, 40 );
   check_market_is_balanced( mid );

   const auto before = db.get_balance( bob_id, core_id ).amount;
   cancel( bob_order, bob_id, bob_private_key );
   BOOST_CHECK( nullptr == db.find( bob_order ) );
   BOOST_CHECK_EQUAL( ( db.get_balance( bob_id, core_id ).amount - before ).value, 60 );
} FC_LOG_AND_RETHROW() }

/// Reducing a position must not open a hedged pair, or a trader would pay margin twice to be
/// flat.
BOOST_AUTO_TEST_CASE( trading_the_other_way_reduces_rather_than_hedges )
{ try {
   generate_blocks( HARDFORK_FUTURES_TIME );
   generate_block();
   set_expiration( db, trx );
   setup_assets();

   ACTORS( (alice)(bob)(carol) );
   fund( alice, asset(10000000) ); fund( bob, asset(10000000) ); fund( carol, asset(10000000) );

   const auto oid = make_oracle( alice_id, alice_private_key, bob_id );
   publish( oid, bob_id, bob_private_key, 100 );
   const auto mid = make_market( alice_id, alice_private_key, oid, 1 );

   place( mid, bob_id, bob_private_key, true, 100, 10 );
   place( mid, carol_id, carol_private_key, false, 100, 10 );
   BOOST_CHECK_EQUAL( position_of( mid, bob_id )->size.value, 10 );

   // bob sells 4 back; carol takes the other side, reducing hers too
   place( mid, bob_id, bob_private_key, false, 100, 4 );
   place( mid, carol_id, carol_private_key, true, 100, 4 );

   BOOST_CHECK_EQUAL( position_of( mid, bob_id )->size.value, 6 );
   BOOST_CHECK_EQUAL( position_of( mid, carol_id )->size.value, -6 );
   BOOST_CHECK_EQUAL( mid(db).open_interest.value, 6 );
   check_market_is_balanced( mid );
} FC_LOG_AND_RETHROW() }

/// Self-matching would let one account pay itself the spread and build a position out of two
/// halves of its own order.
BOOST_AUTO_TEST_CASE( an_order_cannot_match_its_owners_own_order )
{ try {
   generate_blocks( HARDFORK_FUTURES_TIME );
   generate_block();
   set_expiration( db, trx );
   setup_assets();

   ACTORS( (alice)(bob) );
   fund( alice, asset(10000000) ); fund( bob, asset(10000000) );

   const auto oid = make_oracle( alice_id, alice_private_key, bob_id );
   publish( oid, bob_id, bob_private_key, 100 );
   const auto mid = make_market( alice_id, alice_private_key, oid, 1 );

   place( mid, bob_id, bob_private_key, true, 100, 10 );
   GRAPHENE_REQUIRE_THROW( place( mid, bob_id, bob_private_key, false, 90, 10 ), fc::exception );
} FC_LOG_AND_RETHROW() }

/// Margin is reserved when the order is placed, not when it fills.
BOOST_AUTO_TEST_CASE( an_order_that_cannot_be_paid_for_is_refused )
{ try {
   generate_blocks( HARDFORK_FUTURES_TIME );
   generate_block();
   set_expiration( db, trx );
   setup_assets();

   ACTORS( (alice)(bob)(dan) );
   fund( alice, asset(10000000) ); fund( bob, asset(10000000) );
   fund( dan, asset(500) );   // enough for fees, nowhere near enough margin

   const auto oid = make_oracle( alice_id, alice_private_key, bob_id );
   publish( oid, bob_id, bob_private_key, 100 );
   const auto mid = make_market( alice_id, alice_private_key, oid, 1 );

   // 1,000,000 contracts at 100 needs 10,000,000 margin
   GRAPHENE_REQUIRE_THROW( place( mid, dan_id, dan_private_key, true, 100, 1000000 ),
                           fc::exception );
} FC_LOG_AND_RETHROW() }

/// fill_or_kill discards the remainder instead of resting it, and refunds its reservation.
BOOST_AUTO_TEST_CASE( fill_or_kill_does_not_rest )
{ try {
   generate_blocks( HARDFORK_FUTURES_TIME );
   generate_block();
   set_expiration( db, trx );
   setup_assets();

   ACTORS( (alice)(bob)(carol) );
   fund( alice, asset(10000000) ); fund( bob, asset(10000000) ); fund( carol, asset(10000000) );

   const auto oid = make_oracle( alice_id, alice_private_key, bob_id );
   publish( oid, bob_id, bob_private_key, 100 );
   const auto mid = make_market( alice_id, alice_private_key, oid, 1 );

   place( mid, bob_id, bob_private_key, true, 100, 4 );

   const auto before = db.get_balance( carol_id, core_id ).amount;
   place( mid, carol_id, carol_private_key, false, 100, 10, true );   // FOK

   // only 4 could fill; nothing rests
   BOOST_CHECK_EQUAL( position_of( mid, carol_id )->size.value, -4 );
   const auto& book = db.get_index_type<futures_order_index>().indices().get<by_id>();
   size_t resting = 0;
   for( const auto& o : book ) if( o.market_id == mid ) ++resting;
   BOOST_CHECK_EQUAL( resting, 0u );

   // she reserved for 10 and used 40; the other 60 came back
   BOOST_CHECK_EQUAL( ( before - db.get_balance( carol_id, core_id ).amount ).value, 40 );
   check_market_is_balanced( mid );
} FC_LOG_AND_RETHROW() }

/// A halted or unmarked market must not accept orders.
BOOST_AUTO_TEST_CASE( orders_are_refused_when_the_market_is_not_tradable )
{ try {
   generate_blocks( HARDFORK_FUTURES_TIME );
   generate_block();
   set_expiration( db, trx );
   setup_assets();

   ACTORS( (alice)(bob) );
   fund( alice, asset(10000000) ); fund( bob, asset(10000000) );

   const auto oid = make_oracle( alice_id, alice_private_key, bob_id );
   const auto mid = make_market( alice_id, alice_private_key, oid, 1 );

   // no mark price yet
   BOOST_REQUIRE( !mid(db).mark_price.valid() );
   GRAPHENE_REQUIRE_THROW( place( mid, bob_id, bob_private_key, true, 100, 1 ), fc::exception );

   publish( oid, bob_id, bob_private_key, 100 );
   BOOST_CHECK_NO_THROW( place( mid, bob_id, bob_private_key, true, 100, 1 ) );

   // owner halts the market
   futures_market_update_operation uop;
   uop.owner     = alice_id;
   uop.market_id = mid;
   futures_market_options halted = mid(db).options;
   halted.enabled = false;
   uop.new_options = halted;
   signed_transaction tx;
   tx.operations.push_back( uop );
   db.current_fee_schedule().set_fee( tx.operations.back() );
   set_expiration( db, tx );
   tx.sign( alice_private_key, db.get_chain_id() );
   PUSH_TX( db, tx );

   GRAPHENE_REQUIRE_THROW( place( mid, bob_id, bob_private_key, true, 100, 1 ), fc::exception );
} FC_LOG_AND_RETHROW() }


/**
 * A premium held for a moment must not move funding as much as one held all interval.
 *
 * Funding used to read the book mid ONCE, at the instant the interval elapsed. Whichever two
 * orders happened to be best at that moment set the rate for the whole interval, so on a thin
 * book a single non-marketable order placed just before the sample -- and cancelled just after
 * -- moved the mid as far as the cap allowed. That is a repeatable transfer from one side of
 * the market to the other, every interval, for the price of an order that never fills.
 *
 * The premium is now time-weighted across the interval, so a quote only counts for as long as
 * it is actually exposed. This runs the same skew twice: held briefly, then held throughout.
 */
BOOST_AUTO_TEST_CASE( a_momentary_quote_cannot_set_the_funding_rate )
{ try {
   generate_blocks( HARDFORK_FUTURES_TIME );
   generate_block();
   set_expiration( db, trx );
   setup_assets();

   ACTORS( (alice)(bob)(carol) );
   // A mark large enough that the rate cap is not a single unit. At a mark of 100 the cap is
   // ceil(100 * 750/1e6) == 1, so a held premium and a momentary one both saturate it and the
   // two are indistinguishable at integer resolution -- the measurement, not the behaviour.
   const int64_t oracle_price = 10000000;
   fund( alice, asset( 1000000000000 ) );
   fund( bob,   asset( 1000000000000 ) );
   fund( carol, asset( 1000000000000 ) );

   const auto oid = make_oracle( alice_id, alice_private_key, alice_id, "FUND.CORE" );
   publish( oid, alice_id, alice_private_key, oracle_price );
   const auto mid = make_market( alice_id, alice_private_key, oid, 1 );
   BOOST_REQUIRE( mid(db).is_perpetual() );

   const int64_t mark = mid(db).mark_price->value;
   const uint32_t interval = mid(db).options.funding_interval_sec;

   // A thin book centred on the mark, so the baseline premium is zero and any movement is the
   // skew and nothing else. Deliberately NOT a very wide book: with bids and asks far from the
   // mark the mid alone already exceeds the rate cap, and every reading saturates whatever the
   // sampler does -- which would make this test pass for the wrong reason.
   const int64_t cap    = ( mark * 750 + 999999 ) / 1000000;   // futures_ppm_of, rounded up
   const int64_t spread = cap * 4;
   place( mid, bob_id,   bob_private_key,   true,  mark - spread, 1 );
   place( mid, carol_id, carol_private_key, false, mark + spread, 1 );
   BOOST_REQUIRE_GT( cap, 1 );   // otherwise the two runs cannot differ at integer resolution

   // --- run one: skew the book for a single block near the end of the interval ---------
   generate_blocks( db.head_block_time() + fc::seconds( interval - 10 ) );
   set_expiration( db, trx );
   const futures_order_id_type skew_order {
      place( mid, bob_id, bob_private_key, true, mark + spread - 1, 1 ) };
   publish( oid, alice_id, alice_private_key, oracle_price );   // sample the skew
   cancel( skew_order, bob_id, bob_private_key );

   const auto before_brief = mid(db).cumulative_funding;
   generate_blocks( db.head_block_time() + fc::seconds( 20 ) );
   set_expiration( db, trx );
   publish( oid, alice_id, alice_private_key, oracle_price );   // closes the interval
   const auto brief_funding = mid(db).cumulative_funding - before_brief;

   // --- run two: the same skew, held for the whole interval ----------------------------
   const futures_order_id_type held_order {
      place( mid, bob_id, bob_private_key, true, mark + spread - 1, 1 ) };
   const auto before_held = mid(db).cumulative_funding;
   // Publish periodically, as a live oracle would, so the premium is sampled across the span.
   for( int i = 0; i < 4; ++i )
   {
      generate_blocks( db.head_block_time() + fc::seconds( interval / 4 ) );
      set_expiration( db, trx );
      publish( oid, alice_id, alice_private_key, oracle_price );
   }
   const auto held_funding = mid(db).cumulative_funding - before_held;
   cancel( held_order, bob_id, bob_private_key );

   BOOST_TEST_MESSAGE( "funding from a momentary skew: " << brief_funding.value
                       << ", from a skew held all interval: " << held_funding.value );

   // Holding the quote must cost the market more than flashing it. Under the old sampler the
   // two were identical, because only the final instant was ever read.
   BOOST_CHECK_GT( held_funding.value, brief_funding.value );
} FC_LOG_AND_RETHROW() }


/**
 * A market order is an aggressive limit with fill_or_kill, and that already works.
 *
 * There is no separate market-order operation and there does not need to be: the matching loop
 * walks the book while size remains, fills at each MAKER's price, and fill_or_kill discards
 * whatever is left rather than resting it. Price far through the book plus fill_or_kill is
 * immediate-or-cancel against everything available, which is what a market order is.
 *
 * This is a demonstration rather than a change. It is here because "limit orders only" reads
 * like a missing feature, and without a test saying otherwise the next person will build the
 * operation that already exists.
 */
BOOST_AUTO_TEST_CASE( an_aggressive_fill_or_kill_order_sweeps_the_book_like_a_market_order )
{
   generate_blocks( HARDFORK_FUTURES_TIME );
   generate_block();
   set_expiration( db, trx );
   setup_assets();

   ACTORS( (alice)(bob)(carol)(dan) );
   fund( alice, asset(100000000) ); fund( bob, asset(100000000) );
   fund( carol, asset(100000000) ); fund( dan, asset(100000000) );

   const auto oid = make_oracle( alice_id, alice_private_key, alice_id, "SWEEP.CORE" );
   publish( oid, alice_id, alice_private_key, 100 );
   const auto mid = make_market( alice_id, alice_private_key, oid, 1, {}, "BTC-SWEEP",
                                 undamped() );

   // Three asks at rising prices, from two different makers.
   place( mid, bob_id,   bob_private_key,   false, 100, 3 );
   place( mid, carol_id, carol_private_key, false, 105, 3 );
   place( mid, bob_id,   bob_private_key,   false, 110, 3 );

   const auto dan_before = db.get_balance( dan_id, core_id ).amount;

   // Far through the book, immediate-or-cancel: take everything offered, keep nothing resting.
   const auto leftover = place( mid, dan_id, dan_private_key, true, 1000, 20, true );
   BOOST_CHECK_MESSAGE( leftover == object_id_type(),
                        "fill_or_kill left an order resting on the book" );

   // Dan swept all nine contracts...
   const auto* dan_pos = position_of( mid, dan_id );
   BOOST_REQUIRE( nullptr != dan_pos );
   BOOST_CHECK_EQUAL( dan_pos->size.value, 9 );

   // entry_value sits at the MARK, not at the prices paid. apply_fill settles every fill to
   // the mark as it happens, so buying above it realises the difference immediately rather
   // than carrying it as unrealised PnL -- 9 contracts at a mark of 100.
   BOOST_CHECK_EQUAL( dan_pos->entry_value.value, 9 * 100 );

   // What proves he paid the MAKERS' prices rather than his own limit is the cost. Filling
   // nine contracts at 1000 would have cost him an order of magnitude more than filling them
   // across 100, 105 and 110; the difference above the mark is realised on the spot.
   const auto dan_paid = dan_before - db.get_balance( dan_id, core_id ).amount;
   BOOST_CHECK_MESSAGE( dan_paid.value < 9 * 1000 / 4,
                        "the taker paid " << dan_paid.value
                        << ", which looks like a fill at its own limit rather than the book's" );

   // Both makers were filled, including the one priced highest.
   BOOST_REQUIRE( nullptr != position_of( mid, bob_id ) );
   BOOST_REQUIRE( nullptr != position_of( mid, carol_id ) );
   BOOST_CHECK_EQUAL( position_of( mid, bob_id )->size.value, -6 );   // 3 at 100 and 3 at 110
   BOOST_CHECK_EQUAL( position_of( mid, carol_id )->size.value, -3 );

   // The book is empty and nothing of his order survived.
   BOOST_CHECK_EQUAL( mid(db).open_interest.value, 9 );
   BOOST_CHECK( db.get_balance( dan_id, core_id ).amount < dan_before );
   check_market_is_balanced( mid );
}


/**
 * Maker/taker: the taker pays on the notional it lifts, the maker is paid for having been
 * there to lift, and the remainder capitalises the insurance fund.
 *
 * Paying the maker is the substance of it. A venue that charges both sides the same is asking
 * for a book it declines to pay for; the rebate is what makes resting an order worth doing.
 * And the remainder going to the fund rather than to an operator means trading activity
 * capitalises the thing that absorbs a bankruptcy.
 */
BOOST_AUTO_TEST_CASE( a_taker_pays_a_fee_the_maker_is_rebated_and_the_fund_keeps_the_rest )
{
   generate_blocks( HARDFORK_FUTURES_TIME );
   generate_block();
   set_expiration( db, trx );
   setup_assets();

   ACTORS( (alice)(bob)(carol) );
   fund( alice, asset(100000000) ); fund( bob, asset(100000000) );
   fund( carol, asset(100000000) );

   const auto oid = make_oracle( alice_id, alice_private_key, alice_id, "FEE.CORE" );
   publish( oid, alice_id, alice_private_key, 1000 );

   futures_market_options opts = undamped();
   opts.taker_fee_ppm    = 4000;   // 0.4% of notional
   opts.maker_rebate_ppm = 1000;   // 0.1% back to the maker
   const auto mid = make_market( alice_id, alice_private_key, oid, 1, {}, "BTC-FEE", opts );

   // bob rests an ask; carol crosses it.
   place( mid, bob_id, bob_private_key, false, 1000, 10 );

   const auto bob_before   = db.get_balance( bob_id, core_id ).amount;
   const auto carol_before = db.get_balance( carol_id, core_id ).amount;
   const auto fund_before  = mid(db).insurance_fund;

   place( mid, carol_id, carol_private_key, true, 1000, 10 );

   const share_type notional = 10 * 1000;
   const share_type fee      = ( notional.value * 4000 + 999999 ) / 1000000;   // 40
   const share_type rebate   = ( notional.value * 1000 + 999999 ) / 1000000;   // 10

   // The maker is paid, and paid exactly the rebate -- his margin moved into a position, so
   // the only balance change left is what the fee paid him.
   const auto bob_gain = db.get_balance( bob_id, core_id ).amount - bob_before;
   BOOST_CHECK_EQUAL( bob_gain.value, rebate.value );

   // The fund keeps the difference.
   BOOST_CHECK_EQUAL( ( mid(db).insurance_fund - fund_before ).value,
                      ( fee - rebate ).value );

   // And the taker paid it: her outlay exceeds the margin alone by exactly the fee.
   const auto carol_paid = carol_before - db.get_balance( carol_id, core_id ).amount;
   const auto* carol_pos = position_of( mid, carol_id );
   BOOST_REQUIRE( nullptr != carol_pos );
   BOOST_CHECK_EQUAL( ( carol_paid - carol_pos->margin ).value, fee.value );

   check_market_is_balanced( mid );
}

/// A resting order is a maker when it fills, and makers are not charged. Whatever fee was
/// reserved for the crossing part has to come back rather than sit against the remainder.
BOOST_AUTO_TEST_CASE( an_unfilled_remainder_does_not_keep_holding_a_fee_reserve )
{
   generate_blocks( HARDFORK_FUTURES_TIME );
   generate_block();
   set_expiration( db, trx );
   setup_assets();

   ACTORS( (alice)(bob) );
   fund( alice, asset(100000000) ); fund( bob, asset(100000000) );

   const auto oid = make_oracle( alice_id, alice_private_key, alice_id, "FEE2.CORE" );
   publish( oid, alice_id, alice_private_key, 1000 );

   futures_market_options opts = undamped();
   opts.taker_fee_ppm    = 4000;
   opts.maker_rebate_ppm = 0;
   const auto mid = make_market( alice_id, alice_private_key, oid, 1, {}, "BTC-FEE2", opts );

   const auto before = db.get_balance( bob_id, core_id ).amount;

   // Nothing to cross, so the whole order rests and nothing is owed in fees.
   const futures_order_id_type oid_rest {
      place( mid, bob_id, bob_private_key, true, 1000, 10 ) };
   BOOST_REQUIRE( nullptr != db.find( oid_rest ) );

   // Only the margin left his balance; the fee reserve came straight back.
   const auto paid = before - db.get_balance( bob_id, core_id ).amount;
   BOOST_CHECK_EQUAL( paid.value, oid_rest(db).deferred_margin.value );

   // Cancelling returns it, so he ends where he started.
   cancel( oid_rest, bob_id, bob_private_key );
   BOOST_CHECK_EQUAL( db.get_balance( bob_id, core_id ).amount.value, before.value );
}

BOOST_AUTO_TEST_SUITE_END()


BOOST_FIXTURE_TEST_SUITE( futures_invariant_tests, futures_fixture )

/**
 * An asymmetric close: one side exits against a fresh counterparty while the original other
 * side stays open. This is the case that shows what the market-wide invariants actually are.
 *
 * Sum(size) is zero always. Sum(entry_value) is NOT: when a position closes, its realised PnL
 * leaves as cash, and what remains in Sum(entry_value) is exactly the negative of everything
 * paid out so far. Conservation of value is therefore not an entry_value identity -- it is
 * checked by verify_asset_supplies, which accounts for every unit of collateral held in
 * positions, resting orders and the insurance fund.
 */
BOOST_AUTO_TEST_CASE( sum_of_sizes_is_zero_but_entry_values_track_realised_pnl )
{ try {
   generate_blocks( HARDFORK_FUTURES_TIME );
   generate_block();
   set_expiration( db, trx );
   setup_assets();

   ACTORS( (alice)(bob)(carol)(dan) );
   fund( alice, asset(10000000) ); fund( bob, asset(10000000) );
   fund( carol, asset(10000000) ); fund( dan, asset(10000000) );

   const auto oid = make_oracle( alice_id, alice_private_key, bob_id );
   publish( oid, bob_id, bob_private_key, 100 );
   const auto mid = make_market( alice_id, alice_private_key, oid, 1 );

   // bob long 10 @ 100 against carol short 10 @ 100
   place( mid, bob_id, bob_private_key, true, 100, 10 );
   place( mid, carol_id, carol_private_key, false, 100, 10 );

   share_type total_size = 0, total_entry = 0;
   auto tally = [&]() {
      total_size = 0; total_entry = 0;
      const auto& idx = db.get_index_type<futures_position_index>().indices().get<by_id>();
      for( const auto& p : idx )
         if( p.market_id == mid ) { total_size += p.size; total_entry += p.entry_value; }
   };

   tally();
   BOOST_CHECK_EQUAL( total_size.value, 0 );
   BOOST_CHECK_EQUAL( total_entry.value, 0 );   // nothing has closed yet

   // bob exits at 120 against dan, who opens a fresh long. Carol stays short.
   place( mid, dan_id, dan_private_key, true, 120, 10 );
   place( mid, bob_id, bob_private_key, false, 120, 10 );

   BOOST_CHECK( nullptr == position_of( mid, bob_id ) );   // bob is out, paid his 200 profit

   tally();
   // sizes still net to zero: dan is long 10, carol short 10
   BOOST_CHECK_EQUAL( total_size.value, 0 );
   BOOST_CHECK_EQUAL( position_of( mid, dan_id )->size.value, 10 );
   BOOST_CHECK_EQUAL( position_of( mid, carol_id )->size.value, -10 );

   // Because every fill settles the position to the mark, entry values are exactly size x mark
   // afterwards and net to zero again -- dan +1000, carol -1000 at a mark of 100. Dan's loss
   // from buying 20 above the mark was COLLECTED into his margin rather than left unrealised,
   // which is what funds bob's payout. Collateral conservation is checked by
   // verify_asset_supplies, which the fixture runs at the end of this test.
   BOOST_CHECK_EQUAL( total_entry.value, 0 );
   BOOST_CHECK_EQUAL( position_of( mid, dan_id )->unrealized_pnl( 100 ).value, 0 );
   BOOST_CHECK_EQUAL( position_of( mid, carol_id )->unrealized_pnl( 100 ).value, 0 );

   // dan paid for the bad entry: 120 reserved by his order plus 180 topped up = 300 in, and he
   // holds 100 of margin. The 200 difference is exactly bob's profit.
   BOOST_CHECK_EQUAL( position_of( mid, dan_id )->margin.value, 100 );
} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_SUITE_END()


BOOST_FIXTURE_TEST_SUITE( futures_risk_tests, futures_fixture )

/// Opening a pair, then setting up so one side is under water at the mark.
struct opened_pair
{
   futures_market_id_type mid;
   oracle_id_type oid;
};

/// Margin can be added freely; withdrawal is bounded by the INITIAL requirement, not the
/// maintenance one, so a trader cannot withdraw down to the edge of liquidation.
BOOST_AUTO_TEST_CASE( margin_can_be_added_and_withdrawn_within_the_initial_requirement )
{ try {
   generate_blocks( HARDFORK_FUTURES_TIME );
   generate_block();
   set_expiration( db, trx );
   setup_assets();

   ACTORS( (alice)(bob)(carol) );
   fund( alice, asset(10000000) ); fund( bob, asset(10000000) ); fund( carol, asset(10000000) );

   const auto oid = make_oracle( alice_id, alice_private_key, bob_id );
   publish( oid, bob_id, bob_private_key, 100 );
   const auto mid = make_market( alice_id, alice_private_key, oid, 1 );

   place( mid, bob_id, bob_private_key, true, 100, 10 );
   place( mid, carol_id, carol_private_key, false, 100, 10 );

   const auto pid = position_of( mid, bob_id )->get_id();
   BOOST_CHECK_EQUAL( pid(db).margin.value, 100 );

   adjust_margin( pid, bob_id, bob_private_key, 50 );
   BOOST_CHECK_EQUAL( pid(db).margin.value, 150 );

   // requirement is 10 x 100 x 10% = 100, so 50 may come back out
   adjust_margin( pid, bob_id, bob_private_key, -50 );
   BOOST_CHECK_EQUAL( pid(db).margin.value, 100 );

   // but not a satoshi more
   GRAPHENE_REQUIRE_THROW( adjust_margin( pid, bob_id, bob_private_key, -1 ), fc::exception );

   // and not by anyone else
   GRAPHENE_REQUIRE_THROW( adjust_margin( pid, carol_id, carol_private_key, 10 ), fc::exception );

   check_market_is_balanced( mid );
} FC_LOG_AND_RETHROW() }

/// A healthy position must not be liquidatable. This is the check that stops liquidation being
/// used as a weapon.
BOOST_AUTO_TEST_CASE( a_healthy_position_cannot_be_liquidated )
{ try {
   generate_blocks( HARDFORK_FUTURES_TIME );
   generate_block();
   set_expiration( db, trx );
   setup_assets();

   ACTORS( (alice)(bob)(carol)(dan) );
   fund( alice, asset(10000000) ); fund( bob, asset(10000000) );
   fund( carol, asset(10000000) ); fund( dan, asset(10000000) );

   const auto oid = make_oracle( alice_id, alice_private_key, bob_id );
   publish( oid, bob_id, bob_private_key, 100 );
   const auto mid = make_market( alice_id, alice_private_key, oid, 1 );

   place( mid, bob_id, bob_private_key, true, 100, 10 );
   place( mid, carol_id, carol_private_key, false, 100, 10 );

   const auto pid = position_of( mid, bob_id )->get_id();
   GRAPHENE_REQUIRE_THROW( liquidate( pid, dan_id, dan_private_key ), fc::exception );
} FC_LOG_AND_RETHROW() }

/// The mark moving against a leveraged long eventually puts it under the maintenance
/// requirement, and then anyone may take it over.
BOOST_AUTO_TEST_CASE( an_underwater_position_is_liquidated_and_handed_to_the_liquidator )
{ try {
   generate_blocks( HARDFORK_FUTURES_TIME );
   generate_block();
   set_expiration( db, trx );
   setup_assets();

   ACTORS( (alice)(bob)(carol)(dan) );
   fund( alice, asset(10000000) ); fund( bob, asset(10000000) );
   fund( carol, asset(10000000) ); fund( dan, asset(10000000) );

   const auto oid = make_oracle( alice_id, alice_private_key, bob_id );
   publish( oid, bob_id, bob_private_key, 100 );
   const auto mid = make_market( alice_id, alice_private_key, oid, 1, {}, "BTC-PERP", undamped() );

   // bob long 10 @ 100 with 100 margin, 10x leverage
   place( mid, bob_id, bob_private_key, true, 100, 10 );
   place( mid, carol_id, carol_private_key, false, 100, 10 );
   const auto pid = position_of( mid, bob_id )->get_id();

   // the mark drops to 92: bob is down 80, equity 20, maintenance is 10 x 92 x 5% = 46
   publish( oid, bob_id, bob_private_key, 92 );
   BOOST_REQUIRE( mid(db).mark_price.valid() );
   BOOST_CHECK_EQUAL( mid(db).mark_price->value, 92 );
   BOOST_CHECK_EQUAL( pid(db).equity( 92 ).value, 20 );

   const auto bob_before = db.get_balance( bob_id, core_id ).amount;
   const auto dan_before = db.get_balance( dan_id, core_id ).amount;

   liquidate( pid, dan_id, dan_private_key );

   // Only as much as it takes. Equity is 20 against a maintenance requirement of 46, and the
   // owner keeps everything but the penalty on the part taken, so the smallest t satisfying
   //     20 - ceil(t x 92 x 1%)  >=  ceil((10-t) x 92 x 10%)
   // is 9: keeps 11, needs 10. Taking all ten would have cost bob the whole position and
   // charged the penalty on the whole notional to fix a shortfall of 26.
   BOOST_REQUIRE( nullptr != db.find( pid ) );
   BOOST_CHECK( pid(db).owner == bob_id );                   // bob keeps his remainder
   BOOST_CHECK_EQUAL( pid(db).size.value, 1 );
   BOOST_CHECK_EQUAL( pid(db).entry_value.value, 92 );        // its share, at the mark
   BOOST_CHECK_EQUAL( pid(db).unrealized_pnl( 92 ).value, 0 );

   // ... and what he keeps is healthy at a FULL initial margin, which is the point of
   // choosing t this way rather than merely clearing the maintenance line.
   BOOST_CHECK_EQUAL( pid(db).margin.value, 11 );
   BOOST_CHECK( pid(db).equity( 92 ) >= 10 );                 // 1 x 92 x 10%, rounded up

   // dan holds the nine contracts he took, at the mark, on a full initial margin.
   const auto* dan_pos = position_of( mid, dan_id );
   BOOST_REQUIRE( nullptr != dan_pos );
   BOOST_CHECK_EQUAL( dan_pos->size.value, 9 );
   BOOST_CHECK_EQUAL( dan_pos->entry_value.value, 9 * 92 );
   BOOST_CHECK_EQUAL( dan_pos->margin.value, ( 9 * 92 * 1000 + 9999 ) / 10000 );

   // bob is not paid out: he still holds his position, so nothing is returned to his balance.
   BOOST_CHECK_EQUAL( ( db.get_balance( bob_id, core_id ).amount - bob_before ).value, 0 );

   // dan posted the initial margin on what he took, less the penalty he earned for taking it.
   BOOST_CHECK( db.get_balance( dan_id, core_id ).amount < dan_before );

   check_market_is_balanced( mid );
} FC_LOG_AND_RETHROW() }

/// Adding margin is the defence against liquidation, and it must actually work.
BOOST_AUTO_TEST_CASE( adding_margin_prevents_liquidation )
{ try {
   generate_blocks( HARDFORK_FUTURES_TIME );
   generate_block();
   set_expiration( db, trx );
   setup_assets();

   ACTORS( (alice)(bob)(carol)(dan) );
   fund( alice, asset(10000000) ); fund( bob, asset(10000000) );
   fund( carol, asset(10000000) ); fund( dan, asset(10000000) );

   const auto oid = make_oracle( alice_id, alice_private_key, bob_id );
   publish( oid, bob_id, bob_private_key, 100 );
   const auto mid = make_market( alice_id, alice_private_key, oid, 1 );

   place( mid, bob_id, bob_private_key, true, 100, 10 );
   place( mid, carol_id, carol_private_key, false, 100, 10 );
   const auto pid = position_of( mid, bob_id )->get_id();

   publish( oid, bob_id, bob_private_key, 92 );
   // bob tops up before anyone gets to him
   adjust_margin( pid, bob_id, bob_private_key, 200 );

   GRAPHENE_REQUIRE_THROW( liquidate( pid, dan_id, dan_private_key ), fc::exception );
   BOOST_CHECK( pid(db).owner == bob_id );
   check_market_is_balanced( mid );
} FC_LOG_AND_RETHROW() }

/// Liquidating your own position would let a trader take the penalty from themselves and
/// reset their entry at the mark.
BOOST_AUTO_TEST_CASE( an_account_cannot_liquidate_itself )
{ try {
   generate_blocks( HARDFORK_FUTURES_TIME );
   generate_block();
   set_expiration( db, trx );
   setup_assets();

   ACTORS( (alice)(bob)(carol) );
   fund( alice, asset(10000000) ); fund( bob, asset(10000000) ); fund( carol, asset(10000000) );

   const auto oid = make_oracle( alice_id, alice_private_key, bob_id );
   publish( oid, bob_id, bob_private_key, 100 );
   const auto mid = make_market( alice_id, alice_private_key, oid, 1 );

   place( mid, bob_id, bob_private_key, true, 100, 10 );
   place( mid, carol_id, carol_private_key, false, 100, 10 );
   const auto pid = position_of( mid, bob_id )->get_id();

   publish( oid, bob_id, bob_private_key, 92 );
   GRAPHENE_REQUIRE_THROW( liquidate( pid, bob_id, bob_private_key ), fc::exception );
} FC_LOG_AND_RETHROW() }

/// A short is liquidated by the mark moving UP, symmetrically.
BOOST_AUTO_TEST_CASE( a_short_is_liquidated_when_the_mark_rises )
{ try {
   generate_blocks( HARDFORK_FUTURES_TIME );
   generate_block();
   set_expiration( db, trx );
   setup_assets();

   ACTORS( (alice)(bob)(carol)(dan) );
   fund( alice, asset(10000000) ); fund( bob, asset(10000000) );
   fund( carol, asset(10000000) ); fund( dan, asset(10000000) );

   const auto oid = make_oracle( alice_id, alice_private_key, bob_id );
   publish( oid, bob_id, bob_private_key, 100 );
   const auto mid = make_market( alice_id, alice_private_key, oid, 1, {}, "BTC-PERP", undamped() );

   place( mid, bob_id, bob_private_key, true, 100, 10 );
   place( mid, carol_id, carol_private_key, false, 100, 10 );
   const auto cpid = position_of( mid, carol_id )->get_id();
   BOOST_CHECK_EQUAL( cpid(db).size.value, -10 );

   // mark up to 108: carol is down 80, equity 20, maintenance 10 x 108 x 5% = 54
   publish( oid, bob_id, bob_private_key, 108 );
   BOOST_CHECK_EQUAL( cpid(db).equity( 108 ).value, 20 );

   liquidate( cpid, dan_id, dan_private_key );
   BOOST_CHECK( cpid(db).owner == dan_id );
   BOOST_CHECK_EQUAL( cpid(db).size.value, -10 );
   BOOST_CHECK_EQUAL( cpid(db).unrealized_pnl( 108 ).value, 0 );

   check_market_is_balanced( mid );
} FC_LOG_AND_RETHROW() }

/**
 * Regression: a mark whose oracle has gone quiet must stop counting as a mark.
 *
 * market.mark_price is written when a producer publishes and never revisited, so it does not
 * decay. mark_price_time was recorded and read by nothing -- every consumer asked
 * mark_price.valid(), which stays true for ever once set. An oracle that stopped publishing
 * therefore froze the mark, and with it froze margin, liquidation and settlement.
 *
 * Settlement is the one that cannot be undone: it fixes a single price for everyone in the
 * market, and the first caller after expiry snapshotted whatever frozen number was sitting
 * there. A contract whose oracle died weeks before expiry settled the whole market at a
 * weeks-old price.
 */
BOOST_AUTO_TEST_CASE( a_stale_oracle_stops_marking_liquidating_and_settling )
{ try {
   generate_blocks( HARDFORK_FUTURES_TIME );
   generate_block();
   set_expiration( db, trx );
   setup_assets();

   ACTORS( (alice)(bob) );
   fund( alice ); fund( bob );

   const uint32_t lifetime = 60;
   const auto oid = make_oracle( alice_id, alice_private_key, bob_id, "STALE.CORE", lifetime );
   publish( oid, bob_id, bob_private_key, 100 );

   const auto expiry = db.head_block_time() + fc::seconds( 30 );
   const auto mid = make_market( alice_id, alice_private_key, oid, 1, expiry, "BTC-STALE" );
   BOOST_REQUIRE( mid(db).mark_price.valid() );

   // A matched pair, opened while the oracle is still live.
   place( mid, alice_id, alice_private_key, true,  100, 10 );
   place( mid, bob_id,   bob_private_key,   false, 100, 10 );
   BOOST_REQUIRE_GT( mid(db).open_interest.value, 0 );
   const auto* long_pos = position_of( mid, alice_id );
   BOOST_REQUIRE( nullptr != long_pos );
   const auto long_pos_id = long_pos->get_id();

   // Nobody publishes again. Let the contract expire and the value age out.
   generate_blocks( db.head_block_time() + fc::seconds( lifetime * 3 ) );
   set_expiration( db, trx );
   const auto now = db.head_block_time();

   // The cached mark is untouched -- that is the whole point. Freshness cannot be read off it.
   BOOST_CHECK( mid(db).mark_price.valid() );
   BOOST_CHECK( !oid(db).is_value_live( now ) );
   BOOST_CHECK( now >= *mid(db).expiry );

   // Settling here would fix a stale price for every position in the market, for ever.
   GRAPHENE_REQUIRE_THROW( settle_market( mid, alice_id, alice_private_key ), fc::exception );
   BOOST_CHECK( !mid(db).is_settled );

   // Liquidation assesses risk against the mark, so it must refuse too.
   GRAPHENE_REQUIRE_THROW( liquidate( long_pos_id, bob_id, bob_private_key ), fc::exception );

   // A fresh publish restores all of it -- the market is stalled, not bricked.
   publish( oid, bob_id, bob_private_key, 100 );
   BOOST_CHECK( oid(db).is_value_live( db.head_block_time() ) );
   settle_market( mid, alice_id, alice_private_key );
   BOOST_CHECK( mid(db).is_settled );
} FC_LOG_AND_RETHROW() }


/**
 * Regression: a liquidator who already holds a position in the same market.
 *
 * Positions are unique per (market, owner) and liquidation reassigned owner, so this collided
 * on the index and took the node down with SIGABRT -- an ordinary operation aborting every
 * node that processed the block. The liquidated position is merged into the existing one now.
 *
 * Both directions matter. Merging like signs just adds contracts. Merging opposite signs nets
 * them off, which genuinely retires contracts and has to come out of open interest, or the
 * market reports more open contracts than exist.
 */
BOOST_AUTO_TEST_CASE( a_liquidator_may_already_hold_a_position_in_the_market )
{ try {
   generate_blocks( HARDFORK_FUTURES_TIME );
   generate_block();
   set_expiration( db, trx );
   setup_assets();

   ACTORS( (alice)(bob)(carol)(dan) );
   fund( alice, asset(10000000) ); fund( bob, asset(10000000) );
   fund( carol, asset(10000000) ); fund( dan, asset(10000000) );

   const auto oid = make_oracle( alice_id, alice_private_key, bob_id );
   publish( oid, bob_id, bob_private_key, 100 );
   const auto mid = make_market( alice_id, alice_private_key, oid, 1, {}, "BTC-PERP", undamped() );

   // bob long 10 against carol; dan long 4 against alice, so dan already holds a position
   place( mid, bob_id,   bob_private_key,   true,  100, 10 );
   place( mid, carol_id, carol_private_key, false, 100, 10 );
   place( mid, dan_id,   dan_private_key,   true,  100, 4 );
   place( mid, alice_id, alice_private_key, false, 100, 4 );

   const auto pid = position_of( mid, bob_id )->get_id();
   BOOST_REQUIRE( nullptr != position_of( mid, dan_id ) );
   const auto oi_before = mid(db).open_interest;
   BOOST_CHECK_EQUAL( oi_before.value, 14 );

   // equity 20 against a maintenance requirement of 10 x 92 x 5% = 46
   publish( oid, bob_id, bob_private_key, 92 );
   BOOST_REQUIRE_EQUAL( pid(db).equity( 92 ).value, 20 );

   liquidate( pid, dan_id, dan_private_key );

   // Merged, not collided. Liquidation is partial, so bob keeps 1 of his 10 and dan's own
   // long of 4 absorbs the 9 taken.
   BOOST_REQUIRE( nullptr != db.find( pid ) );
   BOOST_CHECK( pid(db).owner == bob_id );
   BOOST_CHECK_EQUAL( pid(db).size.value, 1 );
   const auto* dan_pos = position_of( mid, dan_id );
   BOOST_REQUIRE( nullptr != dan_pos );
   BOOST_CHECK_EQUAL( dan_pos->size.value, 13 );

   // Like signs, so nothing was retired.
   BOOST_CHECK_EQUAL( mid(db).open_interest.value, oi_before.value );
   check_market_is_balanced( mid );
} FC_LOG_AND_RETHROW() }

/// The other direction: the liquidator's own position is on the OPPOSITE side, so taking the
/// liquidated one over nets contracts off and open interest must fall to match.
BOOST_AUTO_TEST_CASE( liquidating_into_an_opposing_position_nets_open_interest_down )
{ try {
   generate_blocks( HARDFORK_FUTURES_TIME );
   generate_block();
   set_expiration( db, trx );
   setup_assets();

   ACTORS( (alice)(bob)(carol)(dan) );
   fund( alice, asset(10000000) ); fund( bob, asset(10000000) );
   fund( carol, asset(10000000) ); fund( dan, asset(10000000) );

   const auto oid = make_oracle( alice_id, alice_private_key, bob_id );
   publish( oid, bob_id, bob_private_key, 100 );
   const auto mid = make_market( alice_id, alice_private_key, oid, 1, {}, "BTC-PERP", undamped() );

   // bob long 10 against carol; dan SHORT 4 against alice
   place( mid, bob_id,   bob_private_key,   true,  100, 10 );
   place( mid, carol_id, carol_private_key, false, 100, 10 );
   place( mid, alice_id, alice_private_key, true,  100, 4 );
   place( mid, dan_id,   dan_private_key,   false, 100, 4 );

   const auto pid = position_of( mid, bob_id )->get_id();
   BOOST_REQUIRE( nullptr != position_of( mid, dan_id ) );
   BOOST_REQUIRE_LT( position_of( mid, dan_id )->size.value, 0 );
   BOOST_CHECK_EQUAL( mid(db).open_interest.value, 14 );

   publish( oid, bob_id, bob_private_key, 92 );
   BOOST_REQUIRE_EQUAL( pid(db).equity( 92 ).value, 20 );

   liquidate( pid, dan_id, dan_private_key );

   // dan was short 4 and took over 9 of bob's 10, so he is left long 5 and four contracts
   // net off. Longs afterwards: bob 1, alice 4, dan 5 == 10, against carol's short 10.
   const auto* dan_pos = position_of( mid, dan_id );
   BOOST_REQUIRE( nullptr != dan_pos );
   BOOST_CHECK_EQUAL( dan_pos->size.value, 5 );
   BOOST_CHECK_EQUAL( mid(db).open_interest.value, 10 );
   check_market_is_balanced( mid );
} FC_LOG_AND_RETHROW() }


/**
 * When the insurance fund cannot absorb a bankruptcy, the traders who profited do.
 *
 * The previous behaviour charged the uncovered remainder to whoever called the liquidation.
 * That reads as fair and is self-defeating: nobody volunteers to buy a loss, so the bankrupt
 * position is never liquidated, and it sits there while the market reports itself solvent.
 *
 * Reaching an insufficient fund takes some doing, and the route matters. Losses are booked
 * into the fund by settle_to_mark BEFORE anything is drawn from it, so at the moment a loser
 * is liquidated the fund normally holds their whole loss. The fund only runs short when it has
 * already paid something out -- and funding does exactly that, because it is applied lazily
 * per position: a receiver touched before the payers takes money the fund has not collected.
 */
BOOST_AUTO_TEST_CASE( an_uncovered_bankruptcy_is_taken_from_the_winning_side )
{ try {
   generate_blocks( HARDFORK_FUTURES_TIME );
   generate_block();
   set_expiration( db, trx );
   setup_assets();

   ACTORS( (alice)(bob)(carol)(dan) );
   fund( alice, asset(10000000) ); fund( bob, asset(10000000) );
   fund( carol, asset(10000000) ); fund( dan, asset(10000000) );

   const auto oid = make_oracle( alice_id, alice_private_key, dan_id, "ADL.CORE" );
   publish( oid, dan_id, dan_private_key, 100 );

   futures_market_options opts;
   opts.funding_interval_sec = 60;          // the minimum, so a tick is reachable
   opts.max_funding_rate_ppm = 10000;       // 1% of the mark per interval
   opts.max_mark_move_ppm    = 0;           // this test is about the bankruptcy, not the mark
   const auto mid = make_market( alice_id, alice_private_key, oid, 1, {}, "BTC-ADL", opts );

   // A LARGE long held by dan drives the fund deeply negative when he collects funding, while
   // a small long held by alice is the one that goes bankrupt. Sizing matters: settling the
   // loser credits the fund with their entire loss before anything is drawn, so socialisation
   // only engages when the fund is more negative than the bankrupt position's own margin.
   place( mid, dan_id,   dan_private_key,   true,  100, 2000 );
   place( mid, bob_id,   bob_private_key,   false, 100, 2000 );
   place( mid, alice_id, alice_private_key, true,  100, 10 );
   place( mid, bob_id,   bob_private_key,   false, 100, 10 );
   const auto alice_pos = position_of( mid, alice_id )->get_id();
   const auto bob_pos   = position_of( mid, bob_id )->get_id();
   const auto dan_pos   = position_of( mid, dan_id )->get_id();

   // A book BELOW the mark makes the premium negative, so longs receive and shorts pay.
   place( mid, carol_id, carol_private_key, true,  60, 1 );
   place( mid, carol_id, carol_private_key, false, 70, 1 );

   for( int i = 0; i < 30; ++i )
   {
      generate_block();
      set_expiration( db, trx );
      publish( oid, dan_id, dan_private_key, 100 );
   }
   BOOST_REQUIRE_LT( mid(db).cumulative_funding.value, 0 );   // longs are owed

   // Touch ONLY the big receiver, so the fund pays out before it has collected from the payers.
   adjust_margin( dan_pos, dan_id, dan_private_key, 1 );
   BOOST_REQUIRE_LT( mid(db).insurance_fund.value, -100 );

   // Now crash the mark so alice is bankrupt: 10 x 85 - 1000 = -150 against ~100 of margin.
   publish( oid, dan_id, dan_private_key, 85 );
   BOOST_REQUIRE_LT( alice_pos(db).equity( 85 ).value, 0 );

   const auto bob_margin_before  = bob_pos(db).margin;
   const auto carol_before       = db.get_balance( carol_id, core_id ).amount;
   const auto fund_before        = mid(db).insurance_fund;
   BOOST_REQUIRE( bob_pos(db).unrealized_pnl( 85 ) > 0 );     // bob profited from the crash

   liquidate( alice_pos, carol_id, carol_private_key );

   // bob gave some of his gain back...
   BOOST_CHECK_MESSAGE( bob_pos(db).margin < bob_margin_before,
                        "the winning side was not touched: bob still has "
                        << bob_pos(db).margin.value );

   // ...and carol was not made to buy the shortfall. She still paid to take the position on,
   // but only the initial margin, not the bankruptcy.
   const auto carol_paid = carol_before - db.get_balance( carol_id, core_id ).amount;
   const auto haircut    = bob_margin_before - bob_pos(db).margin;
   BOOST_CHECK_GT( haircut.value, 0 );
   BOOST_TEST_MESSAGE( "fund before " << fund_before.value
                       << ", haircut on the winner " << haircut.value
                       << ", liquidator paid " << carol_paid.value );

   check_market_is_balanced( mid );
} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_SUITE_END()


BOOST_FIXTURE_TEST_SUITE( futures_settlement_tests, futures_fixture )

/// A dated contract settles once, at a price snapshotted from the oracle, and every position
/// closes against that same number.
BOOST_AUTO_TEST_CASE( a_dated_contract_settles_everyone_at_one_price )
{ try {
   generate_blocks( HARDFORK_FUTURES_TIME );
   generate_block();
   set_expiration( db, trx );
   setup_assets();

   ACTORS( (alice)(bob)(carol) );
   fund( alice, asset(10000000) ); fund( bob, asset(10000000) ); fund( carol, asset(10000000) );

   const auto oid = make_oracle( alice_id, alice_private_key, bob_id );
   publish( oid, bob_id, bob_private_key, 100 );

   const fc::time_point_sec expiry = db.head_block_time() + fc::days( 1 );
   const auto mid = make_market( alice_id, alice_private_key, oid, 1, expiry, "BTC-DATED" );

   place( mid, bob_id, bob_private_key, true, 100, 10 );
   place( mid, carol_id, carol_private_key, false, 100, 10 );
   const auto bob_pos   = position_of( mid, bob_id )->get_id();
   const auto carol_pos = position_of( mid, carol_id )->get_id();

   // before expiry, settlement is refused
   GRAPHENE_REQUIRE_THROW( settle_market( mid, alice_id, alice_private_key ), fc::exception );

   // move past expiry with the mark at 110: bob is up 100, carol down 100
   generate_blocks( expiry + 60 );
   set_expiration( db, trx );
   publish( oid, bob_id, bob_private_key, 110 );

   const auto bob_before   = db.get_balance( bob_id, core_id ).amount;
   const auto carol_before = db.get_balance( carol_id, core_id ).amount;

   settle_market( mid, alice_id, alice_private_key, bob_pos );
   BOOST_CHECK( mid(db).is_settled );
   BOOST_REQUIRE( mid(db).settlement_price.valid() );
   BOOST_CHECK_EQUAL( mid(db).settlement_price->value, 110 );
   BOOST_CHECK( nullptr == db.find( bob_pos ) );

   // bob: 100 margin plus 100 profit
   BOOST_CHECK_EQUAL( ( db.get_balance( bob_id, core_id ).amount - bob_before ).value, 200 );

   // the price is fixed now, so a later oracle move cannot change what carol settles at
   publish( oid, bob_id, bob_private_key, 500 );
   BOOST_CHECK_EQUAL( mid(db).settlement_price->value, 110 );

   settle_market( mid, alice_id, alice_private_key, carol_pos );
   BOOST_CHECK( nullptr == db.find( carol_pos ) );
   // carol: 100 margin less her 100 loss
   BOOST_CHECK_EQUAL( ( db.get_balance( carol_id, core_id ).amount - carol_before ).value, 0 );

   BOOST_CHECK_EQUAL( mid(db).open_interest.value, 0 );
} FC_LOG_AND_RETHROW() }

/// A settled market takes no further orders, and a perpetual can never be settled at all.
BOOST_AUTO_TEST_CASE( settled_markets_stop_trading_and_perpetuals_never_settle )
{ try {
   generate_blocks( HARDFORK_FUTURES_TIME );
   generate_block();
   set_expiration( db, trx );
   setup_assets();

   ACTORS( (alice)(bob)(carol) );
   fund( alice, asset(10000000) ); fund( bob, asset(10000000) ); fund( carol, asset(10000000) );

   const auto oid = make_oracle( alice_id, alice_private_key, bob_id );
   publish( oid, bob_id, bob_private_key, 100 );

   const auto perp = make_market( alice_id, alice_private_key, oid, 1, {}, "BTC-PERP" );
   GRAPHENE_REQUIRE_THROW( settle_market( perp, alice_id, alice_private_key ), fc::exception );

   const fc::time_point_sec expiry = db.head_block_time() + fc::days( 1 );
   const auto dated = make_market( alice_id, alice_private_key, oid, 1, expiry, "BTC-DATED" );

   generate_blocks( expiry + 60 );
   set_expiration( db, trx );
   publish( oid, bob_id, bob_private_key, 100 );

   settle_market( dated, alice_id, alice_private_key );
   BOOST_CHECK( dated(db).is_settled );
   BOOST_CHECK( !dated(db).is_tradable( db.head_block_time() ) );
   GRAPHENE_REQUIRE_THROW( place( dated, bob_id, bob_private_key, true, 100, 1 ), fc::exception );

   // the perpetual is unaffected
   BOOST_CHECK( perp(db).is_tradable( db.head_block_time() ) );
} FC_LOG_AND_RETHROW() }

/// Funding moves value from longs to shorts when the book sits above the mark, and nets to
/// zero across the market.
BOOST_AUTO_TEST_CASE( funding_transfers_from_longs_to_shorts_when_the_book_is_above_the_mark )
{ try {
   generate_blocks( HARDFORK_FUTURES_TIME );
   generate_block();
   set_expiration( db, trx );
   setup_assets();

   ACTORS( (alice)(bob)(carol)(dan) );
   fund( alice, asset(10000000) ); fund( bob, asset(10000000) );
   fund( carol, asset(10000000) ); fund( dan, asset(10000000) );

   const auto oid = make_oracle( alice_id, alice_private_key, bob_id );
   publish( oid, bob_id, bob_private_key, 100 );

   futures_market_create_operation cop;
   cop.owner            = alice_id;
   cop.symbol           = "BTC-PERP";
   cop.oracle_id        = oid;
   cop.collateral_asset = core_id;
   cop.contract_size    = 1;
   cop.options.funding_interval_sec = 60;      // short, so a test can cross it
   cop.options.max_funding_rate_ppm = 10000;   // 1% per interval
   signed_transaction ctx;
   ctx.operations.push_back( cop );
   db.current_fee_schedule().set_fee( ctx.operations.back() );
   set_expiration( db, ctx );
   ctx.sign( alice_private_key, db.get_chain_id() );
   const futures_market_id_type mid {
      PUSH_TX( db, ctx ).operation_results.front().get<object_id_type>() };

   // bob long 10, carol short 10, both at the mark
   place( mid, bob_id, bob_private_key, true, 100, 10 );
   place( mid, carol_id, carol_private_key, false, 100, 10 );
   const auto bob_pos   = position_of( mid, bob_id )->get_id();
   const auto carol_pos = position_of( mid, carol_id )->get_id();
   BOOST_CHECK_EQUAL( mid(db).cumulative_funding.value, 0 );

   // leave a book strictly above the mark: bid 104, ask 108, mid 106 against a mark of 100
   place( mid, dan_id, dan_private_key, true, 104, 1 );
   place( mid, alice_id, alice_private_key, false, 108, 1 );

   // cross a funding interval and refresh the mark
   generate_blocks( db.head_block_time() + 120 );
   set_expiration( db, trx );
   publish( oid, bob_id, bob_private_key, 100 );

   // premium is 6 per contract, capped at 1% of the 100 mark = 1
   BOOST_CHECK_EQUAL( mid(db).cumulative_funding.value, 1 );

   const auto bob_margin_before   = bob_pos(db).margin;
   const auto carol_margin_before = carol_pos(db).margin;

   // touching each position applies the accrued funding
   adjust_margin( bob_pos, bob_id, bob_private_key, 1000 );
   adjust_margin( carol_pos, carol_id, carol_private_key, 1000 );

   // bob is long 10 and pays 10; carol is short 10 and receives 10
   BOOST_CHECK_EQUAL( ( bob_pos(db).margin - bob_margin_before ).value, 1000 - 10 );
   BOOST_CHECK_EQUAL( ( carol_pos(db).margin - carol_margin_before ).value, 1000 + 10 );

   check_market_is_balanced( mid );
} FC_LOG_AND_RETHROW() }

/// The premium used to be built from three separate integer divisions -- impact_bid,
/// impact_ask, then their mid -- and with every operand positive all three truncated the same
/// way, so the premium came out low by up to about 1.5 units in every sample.
///
/// Here the book sits at bids 99 and 100 against asks 102 and 103, so the impact prices are
/// 99.5 and 102.5 and the true mid is 101: a premium of exactly 1 over a mark of 100. Under
/// the old arithmetic 99.5 floored to 99, 102.5 to 102, and their mid of 100.5 to 100, which
/// reported a premium of 0 -- funding switched off entirely, on a book a full point above the
/// mark.
BOOST_AUTO_TEST_CASE( the_premium_is_not_truncated_away_by_its_own_arithmetic )
{ try {
   generate_blocks( HARDFORK_FUTURES_TIME );
   generate_block();
   set_expiration( db, trx );
   setup_assets();

   ACTORS( (alice)(bob)(carol)(dan) );
   fund( alice, asset(10000000) ); fund( bob, asset(10000000) );
   fund( carol, asset(10000000) ); fund( dan, asset(10000000) );

   const auto oid = make_oracle( alice_id, alice_private_key, bob_id );
   publish( oid, bob_id, bob_private_key, 100 );

   futures_market_create_operation cop;
   cop.owner            = alice_id;
   cop.symbol           = "BTC-PERP";
   cop.oracle_id        = oid;
   cop.collateral_asset = core_id;
   cop.contract_size    = 1;
   cop.options.funding_interval_sec = 60;
   cop.options.max_funding_rate_ppm = 10000;   // 1% of a mark of 100 == a cap of 1
   cop.options.impact_size          = 2;       // both resting orders a side are consumed
   signed_transaction ctx;
   ctx.operations.push_back( cop );
   db.current_fee_schedule().set_fee( ctx.operations.back() );
   set_expiration( db, ctx );
   ctx.sign( alice_private_key, db.get_chain_id() );
   const futures_market_id_type mid {
      PUSH_TX( db, ctx ).operation_results.front().get<object_id_type>() };

   // bob long 10, carol short 10, matched at the mark so nothing of theirs rests
   place( mid, bob_id, bob_private_key, true, 100, 10 );
   place( mid, carol_id, carol_private_key, false, 100, 10 );
   const auto bob_pos   = position_of( mid, bob_id )->get_id();
   const auto carol_pos = position_of( mid, carol_id )->get_id();

   // The book: two bids and two asks, one contract each, none of them crossing.
   place( mid, dan_id,   dan_private_key,   true,   99, 1 );
   place( mid, dan_id,   dan_private_key,   true,  100, 1 );
   place( mid, alice_id, alice_private_key, false, 102, 1 );
   place( mid, alice_id, alice_private_key, false, 103, 1 );

   generate_blocks( db.head_block_time() + 120 );
   set_expiration( db, trx );
   publish( oid, bob_id, bob_private_key, 100 );

   // (99+100)/2 == 99.5, (102+103)/2 == 102.5, mid 101, premium 1 -- exactly the cap.
   BOOST_CHECK_EQUAL( mid(db).cumulative_funding.value, 1 );

   const auto bob_margin_before   = bob_pos(db).margin;
   const auto carol_margin_before = carol_pos(db).margin;
   adjust_margin( bob_pos,   bob_id,   bob_private_key,   1000 );
   adjust_margin( carol_pos, carol_id, carol_private_key, 1000 );

   BOOST_CHECK_EQUAL( ( bob_pos(db).margin   - bob_margin_before   ).value, 1000 - 10 );
   BOOST_CHECK_EQUAL( ( carol_pos(db).margin - carol_margin_before ).value, 1000 + 10 );

   check_market_is_balanced( mid );
} FC_LOG_AND_RETHROW() }

/// Funding has to be collected when a position is closed outright, not only when it is
/// touched along the way.
///
/// margin moves only when a position is touched, and closing a position to flat used to be the
/// one path through a fill that touched neither settle_to_mark nor apply_funding. A position
/// opened once, held across funding intervals and closed in a single fill was therefore paid
/// out on the margin it carried before any of them, and everything it owed stayed in
/// cumulative_funding unclaimed -- carried by the settlement pool, which the other side had
/// already paid into.
///
/// bob and carol are exactly symmetric here: same size, same open price, same close price, so
/// their PnL is zero and their margins are equal. The only thing that can separate their
/// balances is funding, and with a funding index of 1 over ten contracts it has to separate
/// them by exactly 20.
BOOST_AUTO_TEST_CASE( closing_a_position_outright_still_pays_its_funding )
{ try {
   generate_blocks( HARDFORK_FUTURES_TIME );
   generate_block();
   set_expiration( db, trx );
   setup_assets();

   ACTORS( (alice)(bob)(carol)(dan) );
   fund( alice, asset(10000000) ); fund( bob, asset(10000000) );
   fund( carol, asset(10000000) ); fund( dan, asset(10000000) );

   const auto oid = make_oracle( alice_id, alice_private_key, bob_id );
   publish( oid, bob_id, bob_private_key, 100 );

   futures_market_create_operation cop;
   cop.owner            = alice_id;
   cop.symbol           = "BTC-PERP";
   cop.oracle_id        = oid;
   cop.collateral_asset = core_id;
   cop.contract_size    = 1;
   cop.options.funding_interval_sec = 60;
   cop.options.max_funding_rate_ppm = 10000;   // 1% of a mark of 100 == a cap of 1
   cop.options.taker_fee_ppm        = 0;       // fees would break the symmetry below
   cop.options.maker_rebate_ppm     = 0;
   signed_transaction ctx;
   ctx.operations.push_back( cop );
   db.current_fee_schedule().set_fee( ctx.operations.back() );
   set_expiration( db, ctx );
   ctx.sign( alice_private_key, db.get_chain_id() );
   const futures_market_id_type mid {
      PUSH_TX( db, ctx ).operation_results.front().get<object_id_type>() };

   // bob long 10, carol short 10, both at the mark
   place( mid, bob_id,   bob_private_key,   true,  100, 10 );
   place( mid, carol_id, carol_private_key, false, 100, 10 );

   // A book above the mark, so the premium is positive and longs pay.
   const futures_order_id_type dan_bid {
      place( mid, dan_id,   dan_private_key,   true,  104, 1 ) };
   const futures_order_id_type alice_ask {
      place( mid, alice_id, alice_private_key, false, 108, 1 ) };

   generate_blocks( db.head_block_time() + 120 );
   set_expiration( db, trx );
   publish( oid, bob_id, bob_private_key, 100 );
   BOOST_REQUIRE_EQUAL( mid(db).cumulative_funding.value, 1 );

   // The book has done its job. It has to come off before bob and carol close against each
   // other, because dan's bid of 104 is better than the 100 bob is selling at: leaving it
   // there sells him a contract at 104, and then carol has only nine to buy and keeps a
   // position. Their close would no longer be the symmetric one this test measures.
   cancel( dan_bid,   dan_id,   dan_private_key );
   cancel( alice_ask, alice_id, alice_private_key );

   // Neither position has been touched since it was opened.
   BOOST_REQUIRE_EQUAL( position_of( mid, bob_id )->last_cumulative_funding.value, 0 );
   BOOST_REQUIRE_EQUAL( position_of( mid, carol_id )->last_cumulative_funding.value, 0 );

   const auto bob_before   = db.get_balance( bob_id, core_id ).amount;
   const auto carol_before = db.get_balance( carol_id, core_id ).amount;

   // Both close outright, in one fill each, against each other at the price they opened at.
   place( mid, bob_id,   bob_private_key,   false, 100, 10 );
   place( mid, carol_id, carol_private_key, true,  100, 10 );

   BOOST_CHECK( !position_of( mid, bob_id ) );
   BOOST_CHECK( !position_of( mid, carol_id ) );

   const auto bob_gain   = db.get_balance( bob_id, core_id ).amount - bob_before;
   const auto carol_gain = db.get_balance( carol_id, core_id ).amount - carol_before;

   // bob is long 10 and owes 10; carol is short 10 and is owed 10.
   BOOST_CHECK_EQUAL( ( carol_gain - bob_gain ).value, 20 );

   check_market_is_balanced( mid );
} FC_LOG_AND_RETHROW() }

/// Price-time priority has to hold on both sides of the book.
///
/// The book is keyed (market, is_long, price, id), and ids increase with time, so scanning
/// forward gives oldest-first at a price level. That is what a taker who is buying does
/// against the asks. A taker who is selling walks the bids backwards to reach the highest
/// price -- and backwards through the id tiebreaker as well, which is newest-first. At one
/// price level the most recently placed bid was being filled before one that had been resting
/// there longer, so a maker could be jumped indefinitely by later orders at their own price.
BOOST_AUTO_TEST_CASE( the_oldest_order_at_a_price_is_filled_first_on_both_sides )
{ try {
   generate_blocks( HARDFORK_FUTURES_TIME );
   generate_block();
   set_expiration( db, trx );
   setup_assets();

   ACTORS( (alice)(bob)(carol)(dan) );
   fund( alice, asset(10000000) ); fund( bob, asset(10000000) );
   fund( carol, asset(10000000) ); fund( dan, asset(10000000) );

   const auto oid = make_oracle( alice_id, alice_private_key, bob_id );
   publish( oid, bob_id, bob_private_key, 100 );

   futures_market_create_operation cop;
   cop.owner            = alice_id;
   cop.symbol           = "BTC-PERP";
   cop.oracle_id        = oid;
   cop.collateral_asset = core_id;
   cop.contract_size    = 1;
   signed_transaction ctx;
   ctx.operations.push_back( cop );
   db.current_fee_schedule().set_fee( ctx.operations.back() );
   set_expiration( db, ctx );
   ctx.sign( alice_private_key, db.get_chain_id() );
   const futures_market_id_type mid {
      PUSH_TX( db, ctx ).operation_results.front().get<object_id_type>() };

   // --- the bid side: bob rests first, dan rests second, both at 100 ------------------
   place( mid, bob_id, bob_private_key, true, 100, 1 );
   place( mid, dan_id, dan_private_key, true, 100, 1 );

   // carol sells one contract into that level
   place( mid, carol_id, carol_private_key, false, 100, 1 );

   BOOST_CHECK_MESSAGE( position_of( mid, bob_id ),
                        "bob rested at this price first and should have been filled first" );
   BOOST_CHECK_MESSAGE( !position_of( mid, dan_id ),
                        "dan rested at the same price later and should still be waiting" );
} FC_LOG_AND_RETHROW() }

/// A funding cap below 100 ppm must still work. It used to be converted into
/// GRAPHENE_100_PERCENT units by an integer divide by 100, which turned every rate under
/// 100 ppm into zero and switched funding off without saying so.
BOOST_AUTO_TEST_CASE( a_small_funding_cap_is_not_rounded_away )
{ try {
   generate_blocks( HARDFORK_FUTURES_TIME );
   generate_block();
   set_expiration( db, trx );
   setup_assets();

   ACTORS( (alice)(bob)(carol)(dan) );
   fund( alice, asset(10000000) ); fund( bob, asset(10000000) );
   fund( carol, asset(10000000) ); fund( dan, asset(10000000) );

   const auto oid = make_oracle( alice_id, alice_private_key, bob_id );
   publish( oid, bob_id, bob_private_key, 100000 );   // a mark big enough for ppm to bite

   futures_market_create_operation cop;
   cop.owner            = alice_id;
   cop.symbol           = "BTC-PERP";
   cop.oracle_id        = oid;
   cop.collateral_asset = core_id;
   cop.contract_size    = 1;
   cop.options.funding_interval_sec = 60;
   cop.options.max_funding_rate_ppm = 50;   // below the old conversion's 100 ppm floor
   signed_transaction ctx;
   ctx.operations.push_back( cop );
   db.current_fee_schedule().set_fee( ctx.operations.back() );
   set_expiration( db, ctx );
   ctx.sign( alice_private_key, db.get_chain_id() );
   const futures_market_id_type mid {
      PUSH_TX( db, ctx ).operation_results.front().get<object_id_type>() };

   // a two-sided book well above the mark, so the premium is large and the cap is what binds
   place( mid, dan_id, dan_private_key, true, 110000, 1 );
   place( mid, alice_id, alice_private_key, false, 120000, 1 );

   generate_blocks( db.head_block_time() + 120 );
   set_expiration( db, trx );
   publish( oid, bob_id, bob_private_key, 100000 );

   // 50 ppm of a 100000 mark is 5, and it must not be zero
   BOOST_CHECK_EQUAL( mid(db).cumulative_funding.value, 5 );
} FC_LOG_AND_RETHROW() }

/// With one side of the book empty there is no mid, so no funding is charged. Guessing a
/// funding rate would move real money on a made-up number.
BOOST_AUTO_TEST_CASE( no_funding_accrues_without_a_two_sided_book )
{ try {
   generate_blocks( HARDFORK_FUTURES_TIME );
   generate_block();
   set_expiration( db, trx );
   setup_assets();

   ACTORS( (alice)(bob)(carol) );
   fund( alice, asset(10000000) ); fund( bob, asset(10000000) ); fund( carol, asset(10000000) );

   const auto oid = make_oracle( alice_id, alice_private_key, bob_id );
   publish( oid, bob_id, bob_private_key, 100 );

   futures_market_create_operation cop;
   cop.owner            = alice_id;
   cop.symbol           = "BTC-PERP";
   cop.oracle_id        = oid;
   cop.collateral_asset = core_id;
   cop.contract_size    = 1;
   cop.options.funding_interval_sec = 60;
   signed_transaction ctx;
   ctx.operations.push_back( cop );
   db.current_fee_schedule().set_fee( ctx.operations.back() );
   set_expiration( db, ctx );
   ctx.sign( alice_private_key, db.get_chain_id() );
   const futures_market_id_type mid {
      PUSH_TX( db, ctx ).operation_results.front().get<object_id_type>() };

   // only bids rest
   place( mid, bob_id, bob_private_key, true, 104, 1 );

   generate_blocks( db.head_block_time() + 120 );
   set_expiration( db, trx );
   publish( oid, bob_id, bob_private_key, 100 );

   BOOST_CHECK_EQUAL( mid(db).cumulative_funding.value, 0 );
} FC_LOG_AND_RETHROW() }


/**
 * REGRESSION: two one-contract orders must not set the funding rate for the whole market.
 *
 * The premium used to be sampled from the mid of best bid and best ask with no regard for the
 * size behind either quote, so a holder could bracket the book with a single contract a side
 * and collect the capped rate on an arbitrarily large position -- paid by the other side, and
 * risking only the one contract actually exposed. Here mallory holds a 500-contract short and
 * tries exactly that.
 *
 * With the premium priced over impact_size contracts of real depth, her two contracts are
 * diluted by the genuine orders resting at the mark, and the rate barely moves.
 */
BOOST_AUTO_TEST_CASE( a_one_contract_quote_cannot_set_the_funding_rate )
{ try {
   generate_blocks( HARDFORK_FUTURES_TIME );
   generate_block();
   set_expiration( db, trx );
   setup_assets();

   ACTORS( (alice)(bob)(carol)(mallory) );
   fund( alice, asset(100000000) ); fund( bob, asset(100000000) );
   fund( carol, asset(100000000) ); fund( mallory, asset(100000000) );

   const auto oid = make_oracle( alice_id, alice_private_key, bob_id );
   publish( oid, bob_id, bob_private_key, 100 );

   futures_market_create_operation cop;
   cop.owner            = alice_id;
   cop.symbol           = "BTC-PERP";
   cop.oracle_id        = oid;
   cop.collateral_asset = core_id;
   cop.contract_size    = 1;
   cop.options.funding_interval_sec = 60;
   cop.options.max_funding_rate_ppm = 10000;   // 1% per interval
   cop.options.impact_size          = 10;
   signed_transaction ctx;
   ctx.operations.push_back( cop );
   db.current_fee_schedule().set_fee( ctx.operations.back() );
   set_expiration( db, ctx );
   ctx.sign( alice_private_key, db.get_chain_id() );
   const futures_market_id_type mid {
      PUSH_TX( db, ctx ).operation_results.front().get<object_id_type>() };

   // Genuine two-sided interest: bob long 500, mallory short 500.
   place( mid, bob_id, bob_private_key, true, 100, 500 );
   place( mid, mallory_id, mallory_private_key, false, 100, 500 );
   const auto bob_pos     = position_of( mid, bob_id )->get_id();
   const auto mallory_pos = position_of( mid, mallory_id )->get_id();
   BOOST_CHECK_EQUAL( mallory_pos(db).size.value, -500 );

   // Real depth at the mark from a third party, on both sides.
   place( mid, carol_id, carol_private_key, true, 99, 20 );
   place( mid, alice_id, alice_private_key, false, 101, 20 );

   // Mallory brackets the book with ONE contract a side, far above the mark of 100.
   place( mid, mallory_id, mallory_private_key, true, 104, 1 );
   place( mid, mallory_id, mallory_private_key, false, 108, 1 );

   generate_blocks( db.head_block_time() + 120 );
   set_expiration( db, trx );
   publish( oid, bob_id, bob_private_key, 100 );

   // Impact bid over 10 contracts: one at 104 then nine at 99 -> 99. Impact ask: one at 108
   // then nine at 101 -> 101. Mid 100, which is the mark. Her two contracts moved nothing.
   BOOST_CHECK_EQUAL( mid(db).cumulative_funding.value, 0 );

   const auto bob_before     = bob_pos(db).margin;
   const auto mallory_before = mallory_pos(db).margin;

   adjust_margin( bob_pos,     bob_id,     bob_private_key,     10000 );
   adjust_margin( mallory_pos, mallory_id, mallory_private_key, 10000 );

   BOOST_CHECK_EQUAL( ( bob_pos(db).margin - bob_before ).value, 10000 );
   BOOST_CHECK_EQUAL( ( mallory_pos(db).margin - mallory_before ).value, 10000 );

   check_market_is_balanced( mid );
} FC_LOG_AND_RETHROW() }

/**
 * The rate still responds when the whole impact depth genuinely trades away from the mark.
 * Manipulation resistance that also stopped real premia from registering would be worse than
 * the bug it replaced.
 */
BOOST_AUTO_TEST_CASE( funding_still_responds_to_a_genuinely_skewed_book )
{ try {
   generate_blocks( HARDFORK_FUTURES_TIME );
   generate_block();
   set_expiration( db, trx );
   setup_assets();

   ACTORS( (alice)(bob)(carol)(dan) );
   fund( alice, asset(100000000) ); fund( bob, asset(100000000) );
   fund( carol, asset(100000000) ); fund( dan, asset(100000000) );

   const auto oid = make_oracle( alice_id, alice_private_key, bob_id );
   publish( oid, bob_id, bob_private_key, 100 );

   futures_market_create_operation cop;
   cop.owner            = alice_id;
   cop.symbol           = "BTC-PERP";
   cop.oracle_id        = oid;
   cop.collateral_asset = core_id;
   cop.contract_size    = 1;
   cop.options.funding_interval_sec = 60;
   cop.options.max_funding_rate_ppm = 10000;
   cop.options.impact_size          = 10;
   signed_transaction ctx;
   ctx.operations.push_back( cop );
   db.current_fee_schedule().set_fee( ctx.operations.back() );
   set_expiration( db, ctx );
   ctx.sign( alice_private_key, db.get_chain_id() );
   const futures_market_id_type mid {
      PUSH_TX( db, ctx ).operation_results.front().get<object_id_type>() };

   place( mid, bob_id, bob_private_key, true, 100, 10 );
   place( mid, carol_id, carol_private_key, false, 100, 10 );
   const auto bob_pos   = position_of( mid, bob_id )->get_id();
   const auto carol_pos = position_of( mid, carol_id )->get_id();

   // A full impact size of depth on each side, genuinely above the mark.
   place( mid, dan_id, dan_private_key, true, 104, 10 );
   place( mid, alice_id, alice_private_key, false, 108, 10 );

   generate_blocks( db.head_block_time() + 120 );
   set_expiration( db, trx );
   publish( oid, bob_id, bob_private_key, 100 );

   // Impact mid is 106 against a mark of 100: a premium of 6, clamped to the 1% cap of 1.
   BOOST_CHECK_EQUAL( mid(db).cumulative_funding.value, 1 );

   const auto bob_before   = bob_pos(db).margin;
   const auto carol_before = carol_pos(db).margin;
   adjust_margin( bob_pos,   bob_id,   bob_private_key,   1000 );
   adjust_margin( carol_pos, carol_id, carol_private_key, 1000 );

   BOOST_CHECK_EQUAL( ( bob_pos(db).margin - bob_before ).value, 1000 - 10 );
   BOOST_CHECK_EQUAL( ( carol_pos(db).margin - carol_before ).value, 1000 + 10 );

   check_market_is_balanced( mid );
} FC_LOG_AND_RETHROW() }


/**
 * ECONOMICS: how far may the mark move before the insurance fund is no longer enough?
 *
 * The existing tests show THAT a bankruptcy is socialised. What they do not say is at which
 * move it happens and how large the shortfall then is -- exactly the number the size of the
 * fund depends on. The test measures it instead of asserting it.
 *
 * Setup: a position opened at 10x leverage, then the mark moved against it step by step. What
 * is measured is the percentage at which equity turns negative -- from there the fund carries
 * the loss, and beyond the fund the opposing side.
 */
BOOST_AUTO_TEST_CASE( measure_the_move_that_exhausts_a_position )
{ try {
   generate_blocks( HARDFORK_FUTURES_TIME );
   generate_block();
   set_expiration( db, trx );
   setup_assets();

   ACTORS( (alice)(bob)(carol) );
   fund( alice, asset(100000000) ); fund( bob, asset(100000000) );
   fund( carol, asset(100000000) );

   const auto oid = make_oracle( alice_id, alice_private_key, bob_id );
   publish( oid, bob_id, bob_private_key, 100 );

   futures_market_create_operation cop;
   cop.owner            = alice_id;
   cop.symbol           = "BTC-PERP";
   cop.oracle_id        = oid;
   cop.collateral_asset = core_id;
   cop.contract_size    = 1;
   cop.options.initial_margin_ratio     = 1000;   // 10x
   cop.options.maintenance_margin_ratio = 500;    // 5%
   cop.options.max_mark_move_ppm        = 0;      // undamped for the measurement
   cop.options.funding_interval_sec     = 86400;  // keeps funding out of the measurement
   signed_transaction ctx;
   ctx.operations.push_back( cop );
   db.current_fee_schedule().set_fee( ctx.operations.back() );
   set_expiration( db, ctx );
   ctx.sign( alice_private_key, db.get_chain_id() );
   const futures_market_id_type mid {
      PUSH_TX( db, ctx ).operation_results.front().get<object_id_type>() };

   place( mid, bob_id, bob_private_key, true, 100, 100 );
   place( mid, carol_id, carol_private_key, false, 100, 100 );
   const auto bob_pos = position_of( mid, bob_id )->get_id();
   const auto notional = 100 * 100;   // 100 contracts at mark 100

   BOOST_TEST_MESSAGE( "  Notional " << notional
                       << ", initial margin " << bob_pos(db).margin.value
                       << " (" << (bob_pos(db).margin.value * 100 / notional) << "% of notional)" );

   int wiped_at = 0;
   for( int pct = 1; pct <= 30 && 0 == wiped_at; ++pct )
   {
      generate_block();
      set_expiration( db, trx );
      publish( oid, bob_id, bob_private_key, 100 - pct );   // the mark falls against the long
      const auto* p = position_of( mid, bob_id );
      if( nullptr == p ) break;
      const share_type mark = *mid(db).mark_price;
      const share_type eq = p->equity( mark );
      if( pct <= 12 || eq <= 0 )
         BOOST_TEST_MESSAGE( "  -" << pct << "%  Mark " << mark.value
                             << "  equity " << eq.value );
      if( eq <= 0 ) wiped_at = pct;
   }

   BOOST_TEST_MESSAGE( "  ==> equity exhausted at -" << wiped_at << "%" );
   // At 10x leverage the equity must be used up at roughly the initial margin, so close to
   // 10%. Much earlier would mean that fees or rounding eat margin; much later, that the margin
   // is not what it claims to be.
   BOOST_CHECK_MESSAGE( wiped_at >= 9 && wiped_at <= 12,
                        "equity exhausted at -" + std::to_string( wiped_at )
                        + "%, expected 9-12%" );
} FC_LOG_AND_RETHROW() }

/**
 * ECONOMICS: a cascade. Several positions with different leverage, and a single move that
 * puts all of them under water.
 *
 * What is checked is not whether a single liquidation works -- other tests do that -- but
 * whether the market still balances afterwards: position sizes summing to zero, open interest
 * consistent, and no value created out of nothing anywhere.
 */
BOOST_AUTO_TEST_CASE( a_cascade_leaves_the_market_balanced )
{ try {
   generate_blocks( HARDFORK_FUTURES_TIME );
   generate_block();
   set_expiration( db, trx );
   setup_assets();

   ACTORS( (alice)(bob)(carol)(dan)(erin) );
   for( auto a : {alice_id, bob_id, carol_id, dan_id, erin_id} )
      fund( a(db), asset(200000000) );

   const auto oid = make_oracle( alice_id, alice_private_key, bob_id );
   publish( oid, bob_id, bob_private_key, 100 );

   futures_market_create_operation cop;
   cop.owner            = alice_id;
   cop.symbol           = "BTC-PERP";
   cop.oracle_id        = oid;
   cop.collateral_asset = core_id;
   cop.contract_size    = 1;
   cop.options.initial_margin_ratio     = 1000;
   cop.options.maintenance_margin_ratio = 500;
   cop.options.max_mark_move_ppm        = 0;
   cop.options.funding_interval_sec     = 86400;
   signed_transaction ctx;
   ctx.operations.push_back( cop );
   db.current_fee_schedule().set_fee( ctx.operations.back() );
   set_expiration( db, ctx );
   ctx.sign( alice_private_key, db.get_chain_id() );
   const futures_market_id_type mid {
      PUSH_TX( db, ctx ).operation_results.front().get<object_id_type>() };

   // Three longs of different size against one short.
   place( mid, bob_id,   bob_private_key,   true, 100, 50 );
   place( mid, carol_id, carol_private_key, false, 100, 50 );
   place( mid, dan_id,   dan_private_key,   true, 100, 30 );
   place( mid, carol_id, carol_private_key, false, 100, 30 );
   place( mid, erin_id,  erin_private_key,  true, 100, 20 );
   place( mid, carol_id, carol_private_key, false, 100, 20 );

   BOOST_TEST_MESSAGE( "  open interest before the move: " << mid(db).open_interest.value );
   check_market_is_balanced( mid );

   const auto fund_before = mid(db).insurance_fund;

   generate_block();
   set_expiration( db, trx );
   publish( oid, bob_id, bob_private_key, 80 );   // -20%: all three longs under water

   BOOST_TEST_MESSAGE( "  mark at 80 (-20%), fund before " << fund_before.value );

   int liquidated = 0;
   for( auto who : {bob_id, dan_id, erin_id} )
   {
      const auto* p = position_of( mid, who );
      if( nullptr == p ) continue;
      const auto pid = p->get_id();
      generate_block();
      set_expiration( db, trx );
      try {
         liquidate( pid, alice_id, alice_private_key );
         ++liquidated;
      } catch( const fc::exception& e ) {
         BOOST_TEST_MESSAGE( "  liquidation refused: "
                             << e.to_string().substr( 0, 90 ) );
      }
   }
   BOOST_TEST_MESSAGE( "  liquidated: " << liquidated << " of 3" );
   BOOST_TEST_MESSAGE( "  fund after: " << mid(db).insurance_fund.value
                       << "  (change " << ( mid(db).insurance_fund - fund_before ).value << ")" );

   // The actual point: after the cascade the market must still balance.
   check_market_is_balanced( mid );
   BOOST_CHECK_MESSAGE( liquidated > 0, "not a single liquidation went through" );
} FC_LOG_AND_RETHROW() }

/**
 * ECONOMICS: can accrued funding alone make a position liquidatable?
 *
 * The rate is capped per interval, but it accumulates. At 0.075% per eight hours that is about
 * 0.225% a day; against a margin of 10% of notional, the question is after how many intervals
 * the position falls without the market having moved.
 */
BOOST_AUTO_TEST_CASE( measure_how_long_funding_alone_takes_to_drain_a_position )
{ try {
   generate_blocks( HARDFORK_FUTURES_TIME );
   generate_block();
   set_expiration( db, trx );
   setup_assets();

   ACTORS( (alice)(bob)(carol)(dan) );
   for( auto a : {alice_id, bob_id, carol_id, dan_id} )
      fund( a(db), asset(200000000) );

   const auto oid = make_oracle( alice_id, alice_private_key, bob_id );
   publish( oid, bob_id, bob_private_key, 100 );

   futures_market_create_operation cop;
   cop.owner            = alice_id;
   cop.symbol           = "BTC-PERP";
   cop.oracle_id        = oid;
   cop.collateral_asset = core_id;
   cop.contract_size    = 1;
   cop.options.initial_margin_ratio     = 1000;
   cop.options.maintenance_margin_ratio = 500;
   cop.options.max_mark_move_ppm        = 0;
   cop.options.funding_interval_sec     = 60;      // so that the test can run through intervals
   cop.options.max_funding_rate_ppm     = 10000;   // 1% per interval, the highest value allowed
   cop.options.impact_size              = 2;
   signed_transaction ctx;
   ctx.operations.push_back( cop );
   db.current_fee_schedule().set_fee( ctx.operations.back() );
   set_expiration( db, ctx );
   ctx.sign( alice_private_key, db.get_chain_id() );
   const futures_market_id_type mid {
      PUSH_TX( db, ctx ).operation_results.front().get<object_id_type>() };

   place( mid, bob_id,   bob_private_key,   true, 100, 100 );
   place( mid, carol_id, carol_private_key, false, 100, 100 );
   const auto bob_pos = position_of( mid, bob_id )->get_id();

   // A book permanently above the mark, with real depth, so that the rate sits at the cap
   // and the long pays.
   place( mid, dan_id,   dan_private_key,   true, 108, 5 );
   place( mid, alice_id, alice_private_key, false, 112, 5 );

   const auto margin0 = bob_pos(db).margin;
   BOOST_TEST_MESSAGE( "  margin at the start: " << margin0.value );

   int intervals = 0;
   for( ; intervals < 40; ++intervals )
   {
      generate_blocks( db.head_block_time() + 70 );
      set_expiration( db, trx );
      publish( oid, bob_id, bob_private_key, 100 );   // mark unchanged
      const auto* p = position_of( mid, bob_id );
      if( nullptr == p ) break;
      // Measured by behaviour instead of recomputed: the position is due exactly when the
      // chain allows a liquidation. That bypasses the internal margin formula and measures
      // what matters.
      // Reading every exception as "still healthy" would be wrong: a liquidation can also
      // fail for reasons that have nothing to do with the position's health. So the reason
      // is logged, and the margin tracked alongside.
      if( intervals < 3 || 0 == intervals % 10 )
         BOOST_TEST_MESSAGE( "  interval " << intervals
                             << "  margin " << p->margin.value
                             << "  size " << p->size.value
                             << "  cum. funding " << mid(db).cumulative_funding.value );
      bool liquidatable = false;
      try {
         liquidate( p->get_id(), carol_id, carol_private_key );
         liquidatable = true;
      } catch( const fc::exception& e ) {
         if( intervals < 3 || 0 == intervals % 10 )
            BOOST_TEST_MESSAGE( "    liquidation refused: "
                                << e.to_string().substr( 0, 100 ) );
      }
      if( liquidatable ) break;
   }
   BOOST_TEST_MESSAGE( "  cumulative funding: " << mid(db).cumulative_funding.value );
   BOOST_TEST_MESSAGE( "  ==> below the maintenance margin after " << intervals
                       << " intervals (mark unchanged)" );

   // Funding alone MUST eventually make a position due. Before the fix to do_evaluate,
   // equity() stayed at 1000 across all 40 intervals while the debt grew to 4000: the
   // position was never liquidatable and would have piled up debt without limit. The upper
   // bound pins down that the rate must not be so high that a healthy position falls within
   // a few intervals.
   BOOST_CHECK_MESSAGE( intervals < 40,
                        "funding alone NEVER made the position due in 40 intervals "
                        "-- the eligibility check ignores accrued funding" );
   BOOST_CHECK_MESSAGE( intervals >= 5,
                        "funding alone liquidates after only "
                        + std::to_string( intervals ) + " intervals" );
} FC_LOG_AND_RETHROW() }


/**
 * ECONOMICS: what does it cost to move the funding rate -- and what does impact_size buy?
 *
 * impact_size is set to 10 because that number was chosen, not because it follows from
 * anything. The test measures what it costs.
 *
 * A first attempt simply had the attacker bid high and measured nothing: a bid above the best
 * ask CROSSES and is filled instead of resting in the book. That is the heart of the matter --
 * to raise the impact price, the attacker first has to BUY UP the honest depth and then post
 * depth of their own. Both cost money, and that is exactly what is counted here.
 */
BOOST_AUTO_TEST_CASE( measure_what_impact_size_costs_an_attacker )
{ try {
   generate_blocks( HARDFORK_FUTURES_TIME );
   generate_block();
   set_expiration( db, trx );
   setup_assets();

   ACTORS( (alice)(bob)(carol)(mallory) );
   for( auto a : {alice_id, bob_id, carol_id, mallory_id} )
      fund( a(db), asset(2000000000) );

   const auto oid = make_oracle( alice_id, alice_private_key, bob_id );
   publish( oid, bob_id, bob_private_key, 100 );

   const int64_t honest_depth = 20;   // honest contracts on the ask side at 101

   BOOST_TEST_MESSAGE( "  mark 100.  honest book: 20 bids at 99, 20 asks at 101." );
   BOOST_TEST_MESSAGE( "  The attacker wants to raise the impact ask. To do so they must" );
   BOOST_TEST_MESSAGE( "  buy up the honest asks and post depth of their own." );

   for( uint32_t impact : { 2u, 10u, 40u } )
   {
      generate_block();
      set_expiration( db, trx );

      futures_market_create_operation cop;
      cop.owner            = alice_id;
      cop.symbol           = "IMP" + std::to_string( impact ) + "-PERP";
      cop.oracle_id        = oid;
      cop.collateral_asset = core_id;
      cop.contract_size    = 1;
      cop.options.funding_interval_sec = 60;
      cop.options.max_funding_rate_ppm = 10000;   // 1%
      cop.options.max_mark_move_ppm    = 0;
      cop.options.impact_size          = impact;
      signed_transaction ctx;
      ctx.operations.push_back( cop );
      db.current_fee_schedule().set_fee( ctx.operations.back() );
      set_expiration( db, ctx );
      ctx.sign( alice_private_key, db.get_chain_id() );
      const futures_market_id_type mid {
         PUSH_TX( db, ctx ).operation_results.front().get<object_id_type>() };

      place( mid, bob_id,   bob_private_key,   true,  100, 50 );
      place( mid, carol_id, carol_private_key, false, 100, 50 );
      place( mid, carol_id, carol_private_key, true,   99, honest_depth );
      place( mid, bob_id,   bob_private_key,   false, 101, honest_depth );

      const int64_t before = get_balance( mallory_id, core_id );

      // Step 1: buy up the honest asks. The attacker pays 101 for something the mark values
      // at 100 -- a loss of 1 per contract, immediately.
      generate_block();
      set_expiration( db, trx );
      place( mid, mallory_id, mallory_private_key, true, 101, honest_depth );

      // Step 2: post depth of their own, high, at least impact_size contracts, or the impact
      // price still mixes in honest orders.
      generate_block();
      set_expiration( db, trx );
      place( mid, mallory_id, mallory_private_key, false, 130,
             static_cast<int64_t>( impact ) );

      // The premium is TIME-WEIGHTED over the interval. A book manipulated only in the last
      // second disappears in the average -- which is why the first attempt measured funding
      // of 0 although the impact mid was at 114. So the attacker has to HOLD the book over the
      // interval, and that exposure is exactly what it costs them.
      for( int k = 0; k < 3; ++k )
      {
         generate_blocks( db.head_block_time() + 40 );
         set_expiration( db, trx );
         publish( oid, bob_id, bob_private_key, 100 );
         BOOST_TEST_MESSAGE( "     after step " << k
                             << ": premium_avg=" << mid(db).premium_avg.value
                             << " premium_last=" << mid(db).premium_last.value
                             << " cum. funding=" << mid(db).cumulative_funding.value );
      }

      // Diagnostics: is the attacker's ask in the book at all, and what does the premium
      // sampler see? Without this, "funding 0" could not be told apart from "the attack had
      // no effect".
      {
         const auto& book = db.get_index_type<futures_order_index>().indices()
                              .get<by_market_book>();
         int64_t nbid = 0, nask = 0; share_type bestbid = 0, bestask = 0;
         for( auto it = book.begin(); it != book.end(); ++it )
         {
            if( it->market_id != mid ) continue;
            if( it->is_long ) { nbid += it->size.value; bestbid = it->price_per_contract; }
            else { if( 0 == nask ) bestask = it->price_per_contract; nask += it->size.value; }
         }
         BOOST_TEST_MESSAGE( "     book: " << nbid << " bids (best " << bestbid.value
                             << "), " << nask << " asks (best " << bestask.value << ")"
                             << "  mark=" << ( mid(db).mark_price.valid()
                                               ? mid(db).mark_price->value : -1 ) );
      }
      const int64_t spent = before - get_balance( mallory_id, core_id );
      BOOST_TEST_MESSAGE( "  impact_size " << impact
                          << ":  capital committed " << spent
                          << "  premium_last " << mid(db).premium_last.value
                          << "  premium_avg " << mid(db).premium_avg.value
                          << "  cum. funding " << mid(db).cumulative_funding.value );

      // Detection is immediate: the impact price sees the manipulated book, and the
      // instantaneous value sits at the cap.
      BOOST_CHECK_MESSAGE( mid(db).premium_last.value > 0,
                           "the impact price did not notice the manipulation" );
      // The effect is NOT immediate: the time-weighted average lags behind, and that is
      // exactly what forces the attacker to hold the book over the interval instead of
      // posting it for a moment.
      BOOST_CHECK_MESSAGE( mid(db).premium_avg.value <= mid(db).premium_last.value,
                           "the average ran ahead of the instantaneous value" );
      check_market_is_balanced( mid );
   }

   BOOST_TEST_MESSAGE( "" );
   BOOST_TEST_MESSAGE( "  ==> measured: the attack costs three things at once." );
   BOOST_TEST_MESSAGE( "      1. buying up the honest depth -- a fixed cost" );
   BOOST_TEST_MESSAGE( "      2. exposing impact_size contracts of one's own --" );
   BOOST_TEST_MESSAGE( "         grows linearly with the parameter (246 / 350 / 740)" );
   BOOST_TEST_MESSAGE( "      3. HOLDING all of it over the funding interval, because the" );
   BOOST_TEST_MESSAGE( "         premium is time-weighted: premium_last hits the cap at once," );
   BOOST_TEST_MESSAGE( "         premium_avg needs the whole interval to get there." );
} FC_LOG_AND_RETHROW() }

/**
 * MMEV against settlement: who decides the price at which EVERYONE settles?
 *
 * It used to be the first caller after expiry, at the mark in that caller's block. A block
 * producer chooses its own blocks, so it had a free option on whichever published value suited
 * it best -- and that price applies to everyone in the contract, fixed once and never touched
 * again. Unlike funding, there is neither a cap nor time-weighting here to damp it.
 *
 * Now the first oracle publication at or after expiry fixes it. Both are checked: that without
 * a publication nothing can be settled at all, and that the price afterwards is the
 * publication's and not one a later caller could have chosen.
 */
BOOST_AUTO_TEST_CASE( the_oracle_fixes_the_settlement_price_not_the_first_caller )
{ try {
   generate_blocks( HARDFORK_FUTURES_TIME );
   generate_block();
   set_expiration( db, trx );
   setup_assets();

   ACTORS( (alice)(bob)(carol)(dan) );
   fund( alice, asset(10000000) ); fund( bob, asset(10000000) );
   fund( carol, asset(10000000) ); fund( dan, asset(10000000) );

   const auto oid = make_oracle( alice_id, alice_private_key, bob_id );
   publish( oid, bob_id, bob_private_key, 100 );

   // A dated contract that expires soon.
   const auto expiry = db.head_block_time() + 300;
   const auto mid = make_market( alice_id, alice_private_key, oid, 1, expiry,
                                 "SET-PERP", undamped() );

   place( mid, bob_id,   bob_private_key,   true,  100, 10 );
   place( mid, carol_id, carol_private_key, false, 100, 10 );

   // Past expiry, with the oracle saying nothing.
   generate_blocks( expiry + 30 );
   set_expiration( db, trx );
   BOOST_REQUIRE( db.head_block_time() > *mid(db).expiry );
   BOOST_CHECK_MESSAGE( !mid(db).is_settled,
                        "the market counted as settled although the oracle had not published" );

   // Without a publication nobody may settle -- not even whoever builds the block.
   GRAPHENE_REQUIRE_THROW( settle_market( mid, dan_id, dan_private_key ), fc::exception );
   BOOST_TEST_MESSAGE( "  after expiry, without a publication: settlement refused" );

   // The first publication at or after expiry fixes the price, without anyone calling anything.
   publish( oid, bob_id, bob_private_key, 88 );
   BOOST_REQUIRE( mid(db).is_settled );
   BOOST_REQUIRE( mid(db).settlement_price.valid() );
   BOOST_CHECK_EQUAL( mid(db).settlement_price->value, 88 );
   BOOST_TEST_MESSAGE( "  the first publication after expiry fixes the price at 88" );

   // A later, more favourable value changes nothing any more -- exactly the choice the first
   // caller used to have.
   publish( oid, bob_id, bob_private_key, 120 );
   BOOST_CHECK_EQUAL( mid(db).settlement_price->value, 88 );
   BOOST_TEST_MESSAGE( "  a later value of 120 does not change the settlement price" );

   settle_market( mid, dan_id, dan_private_key );
   BOOST_TEST_MESSAGE( "  settlement runs at the fixed price" );

   BOOST_TEST_MESSAGE( "" );
   BOOST_TEST_MESSAGE( "  finding: the timing belongs to the oracle, not to the caller." );
   BOOST_TEST_MESSAGE( "  What remains: a producer can delay the first publication after" );
   BOOST_TEST_MESSAGE( "  expiry by the blocks it builds itself -- seconds, not" );
   BOOST_TEST_MESSAGE( "  indefinitely." );
} FC_LOG_AND_RETHROW() }

/**
 * MMEV against the oracle: what a witness achieves by censorship.
 *
 * A witness can keep oracle publications out of its blocks. It cannot falsify the value --
 * that would take a majority of the producers -- but it can let the value go STALE. The
 * question is what the chain then does: keep computing on the old price, or stop.
 *
 * The same position at the same price is measured twice; only the freshness differs. If the
 * liquidation fails on a stale value and goes through on a fresh one, the system fails closed:
 * censorship can PREVENT a liquidation, but it cannot trigger a wrong one. That is the right
 * direction. The most a censor can do is hold a liquidation up, never wrongly seize someone
 * else's position.
 */
BOOST_AUTO_TEST_CASE( a_censored_oracle_stops_liquidation_rather_than_faking_it )
{ try {
   generate_blocks( HARDFORK_FUTURES_TIME );
   generate_block();
   set_expiration( db, trx );
   setup_assets();

   ACTORS( (alice)(bob)(carol)(dan) );
   fund( alice, asset(10000000) ); fund( bob, asset(10000000) );
   fund( carol, asset(10000000) ); fund( dan, asset(10000000) );

   // A short lifetime, so that the value can age within the test.
   const uint32_t lifetime = 300;
   const auto oid = make_oracle( alice_id, alice_private_key, bob_id, "CEN.CORE", lifetime );
   publish( oid, bob_id, bob_private_key, 100 );
   const auto mid = make_market( alice_id, alice_private_key, oid, 1, {}, "CEN-PERP", undamped() );

   place( mid, bob_id,   bob_private_key,   true,  100, 10 );
   place( mid, carol_id, carol_private_key, false, 100, 10 );
   const auto pid = position_of( mid, bob_id )->get_id();

   // Under water: equity 20 against a maintenance requirement of 46.
   publish( oid, bob_id, bob_private_key, 92 );
   BOOST_REQUIRE( mid(db).mark_price.valid() );
   BOOST_CHECK_EQUAL( pid(db).equity( 92 ).value, 20 );

   // Now the oracle stays silent for longer than its lifetime -- exactly what a witness
   // achieves by censorship.
   generate_blocks( db.head_block_time() + int( lifetime ) + 30 );
   set_expiration( db, trx );

   BOOST_TEST_MESSAGE( "  oracle stale: is mark_price still set in the object? "
                       << ( mid(db).mark_price.valid() ? "yes" : "no" ) );

   // The cached mark is still in the object -- it does not expire by itself. The liquidation
   // must still not run, because reads go through the liveness test.
   GRAPHENE_REQUIRE_THROW( liquidate( pid, dan_id, dan_private_key ), fc::exception );
   BOOST_TEST_MESSAGE( "  on a stale value: liquidation refused" );

   // Fresh value, same price, same position: now it has to go through. Without this half,
   // "refused" could not be told apart from "was never liquidatable".
   publish( oid, bob_id, bob_private_key, 92 );
   liquidate( pid, dan_id, dan_private_key );
   BOOST_TEST_MESSAGE( "  on a fresh value at the same price: liquidation goes through" );

   BOOST_TEST_MESSAGE( "" );
   BOOST_TEST_MESSAGE( "  finding: censoring the oracle can hold up a liquidation, but it" );
   BOOST_TEST_MESSAGE( "  cannot trigger a wrong one. The system fails closed." );
} FC_LOG_AND_RETHROW() }

/**
 * MMEV against a resting order: can the block producer fill it worse than its limit?
 *
 * The producer decides which order meets which. If matching could fill a resting order at a
 * worse price than its own, every limit order in the book would be fair game. Both are
 * checked: that a counter-order beyond the limit does NOT match, and that it fills exactly at
 * the limit.
 */
BOOST_AUTO_TEST_CASE( a_resting_order_never_fills_worse_than_its_limit )
{ try {
   generate_blocks( HARDFORK_FUTURES_TIME );
   generate_block();
   set_expiration( db, trx );
   setup_assets();

   ACTORS( (alice)(bob)(carol) );
   fund( alice, asset(10000000) ); fund( bob, asset(10000000) );
   fund( carol, asset(10000000) );

   const auto oid = make_oracle( alice_id, alice_private_key, bob_id );
   publish( oid, bob_id, bob_private_key, 100 );
   const auto mid = make_market( alice_id, alice_private_key, oid, 1 );

   // Bob bids 100: no more than 100 per contract.
   place( mid, bob_id, bob_private_key, true, 100, 10 );
   BOOST_REQUIRE( !position_of( mid, bob_id ) );

   // Carol wants to sell at 110 -- worse for Bob than Bob's limit. That must not match,
   // whatever order the block producer puts the two in.
   place( mid, carol_id, carol_private_key, false, 110, 10 );
   BOOST_CHECK_MESSAGE( !position_of( mid, bob_id ),
                        "a counter-order beyond the limit filled the resting order" );
   BOOST_CHECK_MESSAGE( !position_of( mid, carol_id ),
                        "a counter-order beyond the limit was filled itself" );
   BOOST_TEST_MESSAGE( "  counter-order at 110 against a bid of 100: no fill" );

   // It has to fill exactly at the limit, or the test only measures broken matching.
   place( mid, carol_id, carol_private_key, false, 100, 10 );
   BOOST_REQUIRE( position_of( mid, bob_id ) );
   // entry_value is the running sum of size x price, so 10 contracts at 100 give exactly
   // 1000. A worse fill would show more here.
   BOOST_CHECK_EQUAL( position_of( mid, bob_id )->size.value, 10 );
   BOOST_CHECK_EQUAL( position_of( mid, bob_id )->entry_value.value, 1000 );
   BOOST_TEST_MESSAGE( "  counter-order at 100: filled, entry_value exactly 1000" );

   BOOST_TEST_MESSAGE( "" );
   BOOST_TEST_MESSAGE( "  finding: the limit is the bound, not the ordering." );
} FC_LOG_AND_RETHROW() }

/**
 * What is winning the race for a liquidation worth?
 *
 * This surface cannot be designed away. The liquidator earns the penalty on the part taken
 * over -- that is the incentive that makes anyone clean up undercollateralised positions
 * before they cost the insurance fund. And whoever builds the block wins every race for that
 * incentive, because the block producer decides the order.
 *
 * So the question is not whether a witness earns here. It does. The question is how much, and
 * what limits it -- and that should be measured, not asserted.
 *
 * Two limits are in question:
 *   1. liquidation_penalty_ratio, a market parameter: the cap per unit of notional taken over.
 *      The market operator sets the ceiling with it.
 *   2. Partial liquidation: only what is needed to bring the position back to full initial
 *      margin is taken. The rest stays with the owner and is outside the penalty's base.
 */
BOOST_AUTO_TEST_CASE( measure_what_winning_a_liquidation_race_is_worth )
{ try {
   generate_blocks( HARDFORK_FUTURES_TIME );
   generate_block();
   set_expiration( db, trx );
   setup_assets();

   ACTORS( (alice)(bob)(carol)(mallory) );
   for( auto a : { alice_id, bob_id, carol_id, mallory_id } )
      fund( a(db), asset(4000000000) );

   const int64_t MARK = 100;
   const int64_t SIZE = 100;

   const auto oid = make_oracle( alice_id, alice_private_key, bob_id );
   publish( oid, bob_id, bob_private_key, MARK );
   const auto mid = make_market( alice_id, alice_private_key, oid, 1, {}, "BTC-PERP", undamped() );

   // bob long SIZE, carol short SIZE, both at initial margin
   place( mid, bob_id,   bob_private_key,   true,  MARK, SIZE );
   place( mid, carol_id, carol_private_key, false, MARK, SIZE );
   const auto pid = position_of( mid, bob_id )->get_id();

   const auto& opts = mid(db).options;
   const share_type full_notional = share_type( SIZE ) * MARK;

   // The mark falls until bob slips below the maintenance threshold.
   publish( oid, bob_id, bob_private_key, 94 );
   BOOST_REQUIRE( mid(db).mark_price.valid() );
   const share_type mark_now = *mid(db).mark_price;

   const share_type size_before = pid(db).abs_size();
   const share_type equity_before  = pid(db).equity( mark_now );

   // mallory wins the race -- always, in a block of mallory's own.
   const auto balance_before = db.get_balance( mallory_id, core_id ).amount;
   liquidate( pid, mallory_id, mallory_private_key );
   const auto balance_after = db.get_balance( mallory_id, core_id ).amount;

   const auto* pos = position_of( mid, mallory_id );
   BOOST_REQUIRE( nullptr != pos );

   const share_type taken  = pos->abs_size();
   const share_type paid   = balance_before - balance_after;
   const share_type received  = pos->margin;

   // What mallory extracts is exactly the difference: margin received but not paid for. That
   // IS the penalty.
   const share_type profit = received - paid;
   const share_type penalty = futures_margin_required( taken, mark_now,
                                                      opts.liquidation_penalty_ratio );
   BOOST_CHECK_EQUAL( profit.value, penalty.value );

   // And the instrument check: the measurement must not be small merely because nothing
   // happened.
   BOOST_REQUIRE_GT( taken.value, 0 );
   BOOST_REQUIRE_GT( profit.value, 0 );

   // --- Limit 1: the market parameter -----------------------------------------------------
   // The profit lies on the notional taken over, capped by the ratio.
   const share_type taken_notional = taken * mark_now;
   BOOST_CHECK_LE( profit.value,
                   ( taken_notional.value * opts.liquidation_penalty_ratio + 9999 ) / 10000 );

   // --- Limit 2: what partial liquidation takes off ----------------------------------------
   // Had the takeover covered the whole position, the penalty would lie on the full notional.
   // The difference is what partial liquidation takes away from the window.
   const share_type penalty_if_whole = futures_margin_required(
         size_before, mark_now, opts.liquidation_penalty_ratio );
   BOOST_CHECK_LT( profit.value, penalty_if_whole.value );

   BOOST_TEST_MESSAGE( "MMEV liquidation:"
      << "  position " << size_before.value << " contracts, equity " << equity_before.value
      << ", mark " << mark_now.value );
   BOOST_TEST_MESSAGE( "  taken over " << taken.value << " of " << size_before.value
      << " (" << ( 100 * taken.value / size_before.value ) << " %)" );
   BOOST_TEST_MESSAGE( "  the winner's profit " << profit.value
      << " on " << full_notional.value << " total notional"
      << " = " << ( 10000 * profit.value / full_notional.value ) << " bp" );
   BOOST_TEST_MESSAGE( "  with a full takeover it would have been " << penalty_if_whole.value
      << " -- partial liquidation takes " << ( penalty_if_whole - profit ).value
      << " off it" );

   check_market_is_balanced( mid );
} FC_LOG_AND_RETHROW() }

/**
 * MMEV, step 1: establish the evidence before hardening anything.
 *
 * A witness that produces two consecutive blocks has a window in which nobody can trade
 * against it: it posts its book in the first block, nobody gets in between, and in the second
 * it clears the book again. Within that window it carries no risk -- no counter-trade, no
 * liquidation, no arbitrageur.
 *
 * The question is not whether it can distort the book (obviously it can), but how much
 * funding that moves. The premium is time-weighted, and every span counts with the premium at
 * its START. A window of two blocks therefore contributes at most its own duration to the
 * interval average.
 *
 * It is measured against an honest baseline in the same setup: once with the book left
 * alone, once with an attacker who distorts it for exactly two blocks.
 */
BOOST_AUTO_TEST_CASE( measure_what_two_consecutive_blocks_are_worth )
{ try {
   generate_blocks( HARDFORK_FUTURES_TIME );
   generate_block();
   set_expiration( db, trx );
   setup_assets();

   ACTORS( (alice)(bob)(carol)(mallory) );
   for( auto a : {alice_id, bob_id, carol_id, mallory_id} )
      fund( a(db), asset(4000000000) );

   // A realistic order of magnitude. At a mark of 100 the premium cap (1% = 1 unit) clamps so
   // hard that the time-weighting then rounds to 0 -- the zero would be an artefact of small
   // numbers, not of the mechanism. At 1e6 the cap is 10000 units and the window's share stays
   // visible.
   const int64_t MARK = 1000000;

   const auto oid = make_oracle( alice_id, alice_private_key, bob_id );
   publish( oid, bob_id, bob_private_key, MARK );

   // Realistic intervals: one hour, as perpetuals usually run, and one minute as a
   // cross-check -- the shorter the interval, the more weight a window of fixed length
   // carries.
   std::map<uint32_t, int64_t> moved_by_interval, cost_by_interval;

   for( uint32_t interval : { 60u, 3600u } )
   {
      int64_t funding_honest = 0, funding_attacked = 0, attacker_cost = 0;
      int64_t in_window_premium = 0;

      for( int attacked = 0; attacked < 2; ++attacked )
      {
         generate_block();
         set_expiration( db, trx );

         futures_market_create_operation cop;
         cop.owner            = alice_id;
         cop.symbol           = std::string( "MMEV" ) + ( attacked ? "A" : "H" )
                              + std::to_string( interval ) + "-PERP";
         cop.oracle_id        = oid;
         cop.collateral_asset = core_id;
         cop.contract_size    = 1;
         cop.options.funding_interval_sec = interval;
         cop.options.max_funding_rate_ppm = 10000;   // 1%
         cop.options.max_mark_move_ppm    = 0;
         cop.options.impact_size          = 10;
         signed_transaction ctx;
         ctx.operations.push_back( cop );
         db.current_fee_schedule().set_fee( ctx.operations.back() );
         set_expiration( db, ctx );
         ctx.sign( alice_private_key, db.get_chain_id() );
         const futures_market_id_type mid {
            PUSH_TX( db, ctx ).operation_results.front().get<object_id_type>() };

         // An honest, symmetric book: no premium, and so no funding.
         place( mid, bob_id,   bob_private_key,   true,  MARK, 50 );
         place( mid, carol_id, carol_private_key, false, MARK, 50 );
         place( mid, carol_id, carol_private_key, true,  MARK - MARK/100, 20 );
         place( mid, bob_id,   bob_private_key,   false, MARK + MARK/100, 20 );

         const int64_t balance_before = get_balance( mallory_id, core_id );

         if( attacked )
         {
            // Block 1 of the window: buy up the honest asks and post depth of their own,
            // high. Both in ONE block, because the attacker builds the block.
            generate_block();
            set_expiration( db, trx );
            place( mid, mallory_id, mallory_private_key, true,  MARK + MARK/100, 20 );
            place( mid, mallory_id, mallory_private_key, false, MARK + MARK*3/10, 10 );

            // The oracle keeps publishing independently of the attacker; it is this
            // publication that samples the premium. Without it the window would have no
            // effect, and the test would only prove that nobody was looking.
            generate_block();
            set_expiration( db, trx );
            publish( oid, bob_id, bob_private_key, MARK );
            in_window_premium = mid(db).premium_last.value;
            BOOST_TEST_MESSAGE( "     in the window: premium_last="
                                << mid(db).premium_last.value
                                << " premium_avg=" << mid(db).premium_avg.value );
            {
               const auto& book = db.get_index_type<futures_order_index>().indices()
                                    .get<by_market_book>();
               int64_t nask = 0; share_type bestask = 0;
               for( auto it = book.begin(); it != book.end(); ++it )
                  if( it->market_id == mid && !it->is_long )
                  { if( 0 == nask ) bestask = it->price_per_contract; nask += it->size.value; }
               BOOST_TEST_MESSAGE( "     in the window: " << nask << " asks, best "
                                   << bestask.value );
            }

            // Block 2 of the window: clear it again. Between these two blocks nobody could
            // trade -- that is the whole advantage.
            generate_block();
            set_expiration( db, trx );
            std::vector<futures_order_id_type> mine;
            {
               const auto& book = db.get_index_type<futures_order_index>().indices()
                                    .get<by_market_book>();
               for( auto it = book.begin(); it != book.end(); ++it )
                  if( it->market_id == mid && it->owner == mallory_id )
                     mine.push_back( futures_order_id_type( it->id ) );
            }
            for( auto o : mine )
               cancel( o, mallory_id, mallory_private_key );
         }

         // Let the interval run to its end and close it. The oracle keeps publishing,
         // because every publication resets the mark and samples the premium.
         const auto interval_end = db.head_block_time() + int( interval ) + 10;
         while( db.head_block_time() < interval_end )
         {
            generate_blocks( db.head_block_time() + 20 );
            set_expiration( db, trx );
            publish( oid, bob_id, bob_private_key, MARK );
         }

         BOOST_TEST_MESSAGE( "     at the end of the interval: premium_last="
                             << mid(db).premium_last.value
                             << " premium_avg=" << mid(db).premium_avg.value );
         const int64_t cum = mid(db).cumulative_funding.value;
         if( attacked )
         {
            funding_attacked = cum;
            attacker_cost = balance_before - get_balance( mallory_id, core_id );
         }
         else
            funding_honest = cum;
      }

      const int64_t moved = funding_attacked - funding_honest;
      BOOST_TEST_MESSAGE( "  funding interval " << interval << "s:" );
      BOOST_TEST_MESSAGE( "     funding, honest          " << funding_honest );
      BOOST_TEST_MESSAGE( "     funding after 2 blocks   " << funding_attacked );
      BOOST_TEST_MESSAGE( "     moved                    " << moved );
      BOOST_TEST_MESSAGE( "     attacker's capital       " << attacker_cost );

      moved_by_interval[interval] = moved;
      cost_by_interval[interval] = attacker_cost;

      // Detection must be immediate: the impact price sees the distorted book, and the
      // instantaneous sample sits at the cap. Without this assertion, funding of 0 could not
      // be told apart from "nobody was looking".
      BOOST_CHECK_MESSAGE( in_window_premium >= int64_t( 10000 ),
                           "the impact price did not notice the distortion in the window: "
                           << in_window_premium );
   }

   // The actual finding, as a ratio rather than an absolute value: the window has a fixed
   // length, so its share of the time-weighted average falls in inverse proportion to the
   // interval. From 60s to 3600s that is 60x less; half of that is checked, so that rounding
   // does not pass for a violation.
   BOOST_CHECK_MESSAGE( moved_by_interval[3600] * 30 <= moved_by_interval[60],
                        "the window did not lose weight with the longer interval: "
                        << moved_by_interval[60] << " at 60s against "
                        << moved_by_interval[3600] << " at 3600s" );

   BOOST_TEST_MESSAGE( "" );
   BOOST_TEST_MESSAGE( "  finding: two consecutive blocks are enough to drive the impact" );
   BOOST_TEST_MESSAGE( "  price to the cap -- detection is immediate -- but not enough to" );
   BOOST_TEST_MESSAGE( "  move the time-weighted average." );
   BOOST_TEST_MESSAGE( "  Two mechanisms together: the cap turns a 30 percent distortion" );
   BOOST_TEST_MESSAGE( "  into a sample of 1 percent, and the weighting turns that into" );
   BOOST_TEST_MESSAGE( "  the fraction window/interval." );
   BOOST_TEST_MESSAGE( "  measured: " << moved_by_interval[60] << " at a 60s interval, "
                       << moved_by_interval[3600] << " at 3600s -- with "
                       << cost_by_interval[60] << " capital committed." );
   BOOST_TEST_MESSAGE( "  Not measured: what the attacker EARNS from it. Funding is moved" );
   BOOST_TEST_MESSAGE( "  between the sides, so they would have to hold the right side --" );
   BOOST_TEST_MESSAGE( "  and here they buy the honest asks above the mark, so they start" );
   BOOST_TEST_MESSAGE( "  at a loss." );
} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_SUITE_END()
