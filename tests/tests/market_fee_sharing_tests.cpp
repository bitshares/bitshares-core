#include <boost/test/unit_test.hpp>

#include <graphene/chain/database.hpp>
#include <graphene/app/database_api.hpp>
#include <graphene/chain/exceptions.hpp>
#include <graphene/chain/hardfork.hpp>


#include "../common/database_fixture.hpp"

using namespace graphene::chain;
using namespace graphene::chain::test;

namespace fc
{
   template<typename Ch, typename T>
   std::basic_ostream<Ch>& operator<<(std::basic_ostream<Ch>& os, safe<T> const& sf)
   {
      os << sf.value;
      return os;
   }
}

struct reward_database_fixture : database_fixture
{
   reward_database_fixture()
      : database_fixture(HARDFORK_1268_TIME - 100)
   {
   }

   void update_asset( const account_id_type& issuer_id,
                     const fc::ecc::private_key& private_key,
                     const asset_id_type& asset_id,
                     uint16_t reward_percent )
   {
      asset_update_operation op;
      op.issuer = issuer_id;
      op.asset_to_update = asset_id;
      op.new_options = asset_id(db).options;
      op.new_options.extensions.value.reward_percent = reward_percent;

      signed_transaction tx;
      tx.operations.push_back( op );
      db.current_fee_schedule().set_fee( tx.operations.back() );
      set_expiration( db, tx );
      sign( tx, private_key );
      PUSH_TX( db, tx );
   }

   void generate_blocks_past_reward_hardfork()
   {
      database_fixture::generate_blocks( HARDFORK_1268_TIME );
      database_fixture::generate_block();
   }
};

BOOST_FIXTURE_TEST_SUITE( reward_tests, reward_database_fixture )

BOOST_AUTO_TEST_CASE(asset_rewards_test)
{
   try
   {
      ACTORS((registrar)(alicereferrer)(bobreferrer)(izzy)(jill));

      auto register_account = [&](const string& name, const account_object& referrer) -> const account_object&
      {
         uint8_t referrer_percent = 100;
         fc::ecc::private_key _private_key = generate_private_key(name);
         public_key_type _public_key = _private_key.get_public_key();
         return create_account(name, registrar, referrer, referrer_percent, _public_key);
      };

      // Izzy issues asset to Alice
      // Jill issues asset to Bob
      // Alice and Bob trade in the market and pay fees
      // Bob's and Alice's referrers can get reward
      upgrade_to_lifetime_member(registrar);
      upgrade_to_lifetime_member(alicereferrer);
      upgrade_to_lifetime_member(bobreferrer);

      auto alice = register_account("alice", alicereferrer);
      auto bob = register_account("bob", bobreferrer);

      const share_type core_prec = asset::scaled_precision( asset_id_type()(db).precision );

      // Return number of core shares (times precision)
      auto _core = [&]( int64_t x ) -> asset
      {  return asset( x*core_prec );    };

      transfer( committee_account, alice.id, _core(1000000) );
      transfer( committee_account, bob.id, _core(1000000) );
      transfer( committee_account, izzy_id, _core(1000000) );
      transfer( committee_account, jill_id, _core(1000000) );

      constexpr auto izzycoin_reward_percent = 10*GRAPHENE_1_PERCENT;
      constexpr auto jillcoin_reward_percent = 20*GRAPHENE_1_PERCENT;

      constexpr auto izzycoin_market_percent = 10*GRAPHENE_1_PERCENT;
      constexpr auto jillcoin_market_percent = 20*GRAPHENE_1_PERCENT;

      asset_id_type izzycoin_id = create_bitasset( "IZZYCOIN", izzy_id, izzycoin_market_percent ).id;
      asset_id_type jillcoin_id = create_bitasset( "JILLCOIN", jill_id, jillcoin_market_percent ).id;

      GRAPHENE_REQUIRE_THROW( update_asset(izzy_id, izzy_private_key, izzycoin_id, izzycoin_reward_percent), fc::exception );
      generate_blocks_past_reward_hardfork();
      update_asset(izzy_id, izzy_private_key, izzycoin_id, izzycoin_reward_percent);

      update_asset(jill_id, jill_private_key, jillcoin_id, jillcoin_reward_percent);

      const share_type izzy_prec = asset::scaled_precision( asset_id_type(izzycoin_id)(db).precision );
      const share_type jill_prec = asset::scaled_precision( asset_id_type(jillcoin_id)(db).precision );

      auto _izzy = [&]( int64_t x ) -> asset
      {   return asset( x*izzy_prec, izzycoin_id );   };
      auto _jill = [&]( int64_t x ) -> asset
      {   return asset( x*jill_prec, jillcoin_id );   };

      update_feed_producers( izzycoin_id(db), { izzy_id } );
      update_feed_producers( jillcoin_id(db), { jill_id } );

      // Izzycoin is worth 100 BTS
      price_feed feed;
      feed.settlement_price = price( _izzy(1), _core(100) );
      feed.maintenance_collateral_ratio = 175 * GRAPHENE_COLLATERAL_RATIO_DENOM / 100;
      feed.maximum_short_squeeze_ratio = 150 * GRAPHENE_COLLATERAL_RATIO_DENOM / 100;
      publish_feed( izzycoin_id(db), izzy, feed );

      // Jillcoin is worth 30 BTS
      feed.settlement_price = price( _jill(1), _core(30) );
      feed.maintenance_collateral_ratio = 175 * GRAPHENE_COLLATERAL_RATIO_DENOM / 100;
      feed.maximum_short_squeeze_ratio = 150 * GRAPHENE_COLLATERAL_RATIO_DENOM / 100;
      publish_feed( jillcoin_id(db), jill, feed );

      enable_fees();

      // Alice and Bob create some coins
      borrow( alice.id, _izzy( 1500), _core( 600000) );
      borrow( bob.id, _jill(2000), _core(180000) );

      // Alice and Bob place orders which match
      create_sell_order( alice.id, _izzy(1000), _jill(1500) ); // Alice is willing to sell her 1000 Izzy's for 1.5 Jill
      create_sell_order( bob.id, _jill(1500), _izzy(1000) );   // Bob is buying up to 1500 Izzy's for up to 0.6 Jill

      // 1000 Izzys and 1500 Jills are matched, so the fees should be
      //   100 Izzy (10%) and 300 Jill (20%).
      // Bob's and Alice's referrers should get rewards
      share_type bob_refereer_reward = get_market_fee_reward( bob.referrer, izzycoin_id );
      share_type alice_refereer_reward = get_market_fee_reward( alice.referrer, jillcoin_id );

      // Bob's and Alice's registrars should get rewards
      share_type bob_rgistrar_reward = get_market_fee_reward( bob.registrar, izzycoin_id );
      share_type alice_registrar_reward = get_market_fee_reward( alice.registrar, jillcoin_id );

      auto calculate_percent = [](const share_type& value, uint16_t percent)
      {
         auto a(value.value);
         a *= percent;
         a /= GRAPHENE_100_PERCENT;
         return a;
      };

      BOOST_CHECK_GT( bob_refereer_reward, 0 );
      BOOST_CHECK_GT( alice_refereer_reward, 0 );
      BOOST_CHECK_GT( bob_rgistrar_reward, 0 );
      BOOST_CHECK_GT( alice_registrar_reward, 0 );

      const auto izzycoin_market_fee = calculate_percent(_izzy(1000).amount, izzycoin_market_percent);
      const auto izzycoin_reward = calculate_percent(izzycoin_market_fee, izzycoin_reward_percent);
      BOOST_CHECK_EQUAL( izzycoin_reward, bob_refereer_reward + bob_rgistrar_reward );
      BOOST_CHECK_EQUAL( calculate_percent(izzycoin_reward, bob.referrer_rewards_percentage), bob_refereer_reward );

      const auto jillcoin_market_fee = calculate_percent(_jill(1500).amount, jillcoin_market_percent);
      const auto jillcoin_reward = calculate_percent(jillcoin_market_fee, jillcoin_reward_percent);
      BOOST_CHECK_EQUAL( jillcoin_reward, alice_refereer_reward + alice_registrar_reward );
      BOOST_CHECK_EQUAL( calculate_percent(jillcoin_reward, alice.referrer_rewards_percentage), alice_refereer_reward );
   }
   FC_LOG_AND_RETHROW()
}

BOOST_AUTO_TEST_CASE(asset_claim_reward_test)
{
   try
   {
      ACTORS((jill)(izzy));
      constexpr auto jillcoin_reward_percent = 2*GRAPHENE_1_PERCENT;

      upgrade_to_lifetime_member(izzy);

      price price(asset(1, asset_id_type(1)), asset(1));
      uint16_t market_fee_percent = 20 * GRAPHENE_1_PERCENT;
      auto obj = jill_id(db);
      const asset_object jillcoin = create_user_issued_asset( "JCOIN", jill,  charge_market_fee, price, 2, market_fee_percent );

      const share_type core_prec = asset::scaled_precision( asset_id_type()(db).precision );

      // return number of core shares (times precision)
      auto _core = [&core_prec]( int64_t x ) -> asset { return asset( x*core_prec ); };

      const account_object alice = create_account("alice", izzy, izzy, 50);
      const account_object bob   = create_account("bob",   izzy, izzy, 50);

      // prepare users' balance
      issue_uia( alice, jillcoin.amount( 20000000 ) );

      transfer( committee_account, alice.get_id(), _core(1000) );
      transfer( committee_account, bob.get_id(),   _core(1000) );
      transfer( committee_account, izzy.get_id(),  _core(1000) );

      generate_blocks_past_reward_hardfork();
      // update_asset: set referrer percent
      update_asset(jill_id, jill_private_key, jillcoin.get_id(), jillcoin_reward_percent);

      // Alice and Bob place orders which match
      create_sell_order( alice, jillcoin.amount(200000), _core(1) );
      create_sell_order( bob, _core(1), jillcoin.amount(100000) );

      const int64_t izzy_reward = get_market_fee_reward( izzy, jillcoin );
      const int64_t izzy_balance = get_balance( izzy, jillcoin );

      BOOST_CHECK_GT(izzy_reward, 0);

      auto claim_reward = [&]( account_object referrer, asset amount_to_claim, fc::ecc::private_key private_key )
      {
        vesting_balance_withdraw_operation op;
        op.vesting_balance = vesting_balance_id_type(0);
        op.owner = referrer.get_id();
        op.amount = amount_to_claim;

        signed_transaction tx;
        tx.operations.push_back( op );
        db.current_fee_schedule().set_fee( tx.operations.back() );
        set_expiration( db, tx );
        sign( tx, private_key );
        PUSH_TX( db, tx );
      };

      const int64_t amount_to_claim = 3;
      claim_reward( izzy, jillcoin.amount(amount_to_claim), izzy_private_key );

      BOOST_CHECK_EQUAL(get_balance( izzy, jillcoin ), izzy_balance + amount_to_claim);
      BOOST_CHECK_EQUAL(get_market_fee_reward( izzy, jillcoin ), izzy_reward - amount_to_claim);
   }
   FC_LOG_AND_RETHROW()
}
BOOST_AUTO_TEST_SUITE_END()
