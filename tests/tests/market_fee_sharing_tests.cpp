#include <boost/test/unit_test.hpp>

#include <graphene/chain/database.hpp>
#include <graphene/app/database_api.hpp>
#include <graphene/chain/exceptions.hpp>
#include <graphene/chain/hardfork.hpp>
#include <graphene/chain/is_authorized_asset.hpp>


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
   using whitelist_market_fee_sharing_t = fc::optional<flat_set<account_id_type>>;

   reward_database_fixture()
      : database_fixture(HARDFORK_1268_TIME - 100)
   {
   }

   void update_asset( const account_id_type& issuer_id,
                      const fc::ecc::private_key& private_key,
                      const asset_id_type& asset_id,
                      uint16_t reward_percent,
                      const whitelist_market_fee_sharing_t &whitelist_market_fee_sharing = whitelist_market_fee_sharing_t{},
                      const flat_set<account_id_type> &blacklist = flat_set<account_id_type>())
   {
      asset_update_operation op;
      op.issuer = issuer_id;
      op.asset_to_update = asset_id;
      op.new_options = asset_id(db).options;
      op.new_options.extensions.value.reward_percent = reward_percent;
      op.new_options.extensions.value.whitelist_market_fee_sharing = whitelist_market_fee_sharing;
      op.new_options.blacklist_authorities = blacklist;

      signed_transaction tx;
      tx.operations.push_back( op );
      db.current_fee_schedule().set_fee( tx.operations.back() );
      set_expiration( db, tx );
      sign( tx, private_key );
      PUSH_TX( db, tx );
   }

   void asset_update_blacklist_authority(const account_id_type& issuer_id,
                                         const asset_id_type& asset_id,
                                         const account_id_type& authority_account_id,
                                         const fc::ecc::private_key& issuer_private_key)
   {
      asset_update_operation uop;
      uop.issuer = issuer_id;
      uop.asset_to_update = asset_id;
      uop.new_options = asset_id(db).options;
      uop.new_options.blacklist_authorities.insert(authority_account_id);

      signed_transaction tx;
      tx.operations.push_back( uop );
      db.current_fee_schedule().set_fee( tx.operations.back() );
      set_expiration( db, tx );
      sign( tx, issuer_private_key );
      PUSH_TX( db, tx );
   }

   void add_account_to_blacklist(const account_id_type& authorizing_account_id,
                                 const account_id_type& blacklisted_account_id,
                                 const fc::ecc::private_key& authorizing_account_private_key)
   {
      account_whitelist_operation wop;
      wop.authorizing_account = authorizing_account_id;
      wop.account_to_list = blacklisted_account_id;
      wop.new_listing = account_whitelist_operation::black_listed;

      signed_transaction tx;
      tx.operations.push_back( wop );
      db.current_fee_schedule().set_fee( tx.operations.back() );
      set_expiration( db, tx );
      sign( tx, authorizing_account_private_key );
      PUSH_TX( db, tx);
   }

   void generate_blocks_past_hf1268()
   {
      database_fixture::generate_blocks( HARDFORK_1268_TIME );
      database_fixture::generate_block();
   }

   asset core_asset(int64_t x )
   {
       return asset( x*core_precision );
   };

   const share_type core_precision = asset::scaled_precision( asset_id_type()(db).precision );

   void create_vesting_balance_object(const account_id_type& account_id, vesting_balance_type balance_type )
   {
      db.create<vesting_balance_object>([&account_id, balance_type] (vesting_balance_object &vbo) {
         vbo.owner = account_id;
         vbo.balance_type = balance_type;
      });
   };
};

BOOST_FIXTURE_TEST_SUITE( fee_sharing_tests, reward_database_fixture )

BOOST_AUTO_TEST_CASE(cannot_create_asset_with_additional_options_before_hf)
{
   try
   {
      ACTOR(issuer);

      price price(asset(1, asset_id_type(1)), asset(1));
      uint16_t market_fee_percent = 100;

      additional_asset_options_t options;
      options.value.reward_percent = 100;
      options.value.whitelist_market_fee_sharing = flat_set<account_id_type>{issuer_id};

      GRAPHENE_CHECK_THROW(create_user_issued_asset("USD",
                                                    issuer,
                                                    charge_market_fee,
                                                    price,
                                                    2,
                                                    market_fee_percent,
                                                    options),
                           fc::assert_exception);
   }
   FC_LOG_AND_RETHROW()
}

BOOST_AUTO_TEST_CASE(create_asset_with_additional_options_after_hf)
{
   try
   {
      ACTOR(issuer);

      generate_blocks_past_hf1268();

      uint16_t reward_percent = 100;
      flat_set<account_id_type> whitelist = {issuer_id};
      price price(asset(1, asset_id_type(1)), asset(1));
      uint16_t market_fee_percent = 100;

      additional_asset_options_t options;
      options.value.reward_percent = reward_percent;
      options.value.whitelist_market_fee_sharing = whitelist;

      asset_object usd_asset = create_user_issued_asset("USD",
                                                        issuer,
                                                        charge_market_fee,
                                                        price,
                                                        2,
                                                        market_fee_percent,
                                                        options);

      additional_asset_options usd_options = usd_asset.options.extensions.value;
      BOOST_CHECK_EQUAL(reward_percent, *usd_options.reward_percent);
      BOOST_CHECK(whitelist == *usd_options.whitelist_market_fee_sharing);
   }
   FC_LOG_AND_RETHROW()
}

BOOST_AUTO_TEST_CASE(cannot_update_additional_options_before_hf)
{
   try
   {
      ACTOR(issuer);

      asset_object usd_asset = create_user_issued_asset("USD", issuer, charge_market_fee);

      flat_set<account_id_type> whitelist = {issuer_id};
      GRAPHENE_CHECK_THROW(
                  update_asset(issuer_id, issuer_private_key, usd_asset.get_id(), 40, whitelist),
                  fc::assert_exception );
   }
   FC_LOG_AND_RETHROW()
}

BOOST_AUTO_TEST_CASE(update_additional_options_after_hf)
{
   try
   {
      ACTOR(issuer);

      asset_object usd_asset = create_user_issued_asset("USD", issuer, charge_market_fee);

      generate_blocks_past_hf1268();

      uint16_t reward_percent = 40;
      flat_set<account_id_type> whitelist = {issuer_id};
      update_asset(issuer_id, issuer_private_key, usd_asset.get_id(), reward_percent, whitelist);

      asset_object updated_asset = usd_asset.get_id()(db);
      additional_asset_options options = updated_asset.options.extensions.value;
      BOOST_CHECK_EQUAL(reward_percent, *options.reward_percent);
      BOOST_CHECK(whitelist == *options.whitelist_market_fee_sharing);
   }
   FC_LOG_AND_RETHROW()
}

BOOST_AUTO_TEST_CASE(asset_rewards_test)
{
   try
   {
      ACTORS((registrar)(alicereferrer)(bobreferrer)(izzy)(jill));

      auto register_account = [&](const string& name, const account_object& referrer) -> const account_object&
      {
         uint16_t referrer_percent = GRAPHENE_1_PERCENT;
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

      transfer( committee_account, alice.id, core_asset(1000000) );
      transfer( committee_account, bob.id, core_asset(1000000) );
      transfer( committee_account, izzy_id, core_asset(1000000) );
      transfer( committee_account, jill_id, core_asset(1000000) );

      constexpr auto izzycoin_reward_percent = 10*GRAPHENE_1_PERCENT;
      constexpr auto jillcoin_reward_percent = 20*GRAPHENE_1_PERCENT;

      constexpr auto izzycoin_market_percent = 10*GRAPHENE_1_PERCENT;
      constexpr auto jillcoin_market_percent = 20*GRAPHENE_1_PERCENT;

      asset_id_type izzycoin_id = create_bitasset( "IZZYCOIN", izzy_id, izzycoin_market_percent ).id;
      asset_id_type jillcoin_id = create_bitasset( "JILLCOIN", jill_id, jillcoin_market_percent ).id;

      generate_blocks_past_hf1268();

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
      feed.settlement_price = price( _izzy(1), core_asset(100) );
      feed.maintenance_collateral_ratio = 175 * GRAPHENE_COLLATERAL_RATIO_DENOM / 100;
      feed.maximum_short_squeeze_ratio = 150 * GRAPHENE_COLLATERAL_RATIO_DENOM / 100;
      publish_feed( izzycoin_id(db), izzy, feed );

      // Jillcoin is worth 30 BTS
      feed.settlement_price = price( _jill(1), core_asset(30) );
      feed.maintenance_collateral_ratio = 175 * GRAPHENE_COLLATERAL_RATIO_DENOM / 100;
      feed.maximum_short_squeeze_ratio = 150 * GRAPHENE_COLLATERAL_RATIO_DENOM / 100;
      publish_feed( jillcoin_id(db), jill, feed );

      enable_fees();

      // Alice and Bob create some coins
      borrow( alice.id, _izzy( 1500), core_asset( 600000) );
      borrow( bob.id, _jill(2000), core_asset(180000) );

      // Alice and Bob place orders which match
      create_sell_order( alice.id, _izzy(1000), _jill(1500) ); // Alice is willing to sell her 1000 Izzy's for 1.5 Jill
      create_sell_order( bob.id, _jill(1500), _izzy(1000) );   // Bob is buying up to 1500 Izzy's for up to 0.6 Jill

      // 1000 Izzys and 1500 Jills are matched, so the fees should be
      //   100 Izzy (10%) and 300 Jill (20%).
      // Bob's and Alice's referrers should get rewards
      share_type bob_refereer_reward = get_market_fee_reward( bob.referrer, izzycoin_id );
      share_type alice_refereer_reward = get_market_fee_reward( alice.referrer, jillcoin_id );

      // Bob's and Alice's registrars should get rewards
      share_type bob_registrar_reward = get_market_fee_reward( bob.registrar, izzycoin_id );
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
      BOOST_CHECK_GT( bob_registrar_reward, 0 );
      BOOST_CHECK_GT( alice_registrar_reward, 0 );

      const auto izzycoin_market_fee = calculate_percent(_izzy(1000).amount, izzycoin_market_percent);
      const auto izzycoin_reward = calculate_percent(izzycoin_market_fee, izzycoin_reward_percent);
      BOOST_CHECK_EQUAL( izzycoin_reward, bob_refereer_reward + bob_registrar_reward );
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
      const asset_object jillcoin = create_user_issued_asset( "JCOIN", jill,  charge_market_fee, price, 2, market_fee_percent );

      const account_object alice = create_account("alice", izzy, izzy, 50/*0.5%*/);
      const account_object bob   = create_account("bob",   izzy, izzy, 50/*0.5%*/);

      // prepare users' balance
      issue_uia( alice, jillcoin.amount( 20000000 ) );

      transfer( committee_account, alice.get_id(), core_asset(1000) );
      transfer( committee_account, bob.get_id(),   core_asset(1000) );
      transfer( committee_account, izzy.get_id(),  core_asset(1000) );

      generate_blocks_past_hf1268();
      // update_asset: set referrer percent
      update_asset(jill_id, jill_private_key, jillcoin.get_id(), jillcoin_reward_percent);

      // Alice and Bob place orders which match
      create_sell_order( alice, jillcoin.amount(200000), core_asset(1) );
      create_sell_order( bob, core_asset(1), jillcoin.amount(100000) );

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

BOOST_AUTO_TEST_CASE(create_actors)
{
   try
   {
      ACTORS((jill)(izzyregistrar)(izzyreferrer));

      upgrade_to_lifetime_member(izzyregistrar);
      upgrade_to_lifetime_member(izzyreferrer);

      price price(asset(1, asset_id_type(1)), asset(1));
      uint16_t market_fee_percent = 20 * GRAPHENE_1_PERCENT;
      auto obj = jill_id(db);
      const asset_object jillcoin = create_user_issued_asset( "JCOIN", jill,  charge_market_fee, price, 2, market_fee_percent );

      const account_object alice = create_account("alice", izzyregistrar, izzyreferrer, 50/*0.5%*/);
      const account_object bob   = create_account("bob",   izzyregistrar, izzyreferrer, 50/*0.5%*/);

      // prepare users' balance
      issue_uia( alice, jillcoin.amount( 20000000 ) );

      transfer( committee_account, alice.get_id(), core_asset(1000) );
      transfer( committee_account, bob.get_id(),   core_asset(1000) );
      transfer( committee_account, izzyregistrar.get_id(),  core_asset(1000) );
      transfer( committee_account, izzyreferrer.get_id(),  core_asset(1000) );
   }
   FC_LOG_AND_RETHROW()
}

BOOST_AUTO_TEST_CASE(white_list_is_empty_test)
{
   try
   {
      INVOKE(create_actors);

      generate_blocks_past_hf1268();
      GET_ACTOR(jill);

      constexpr auto jillcoin_reward_percent = 2*GRAPHENE_1_PERCENT;
      const asset_object &jillcoin = get_asset("JCOIN");

      flat_set<account_id_type> whitelist;
      update_asset(jill_id, jill_private_key, jillcoin.get_id(), jillcoin_reward_percent, whitelist);

      GET_ACTOR(izzyregistrar);
      GET_ACTOR(izzyreferrer);
      BOOST_CHECK_EQUAL( get_market_fee_reward( izzyregistrar, jillcoin ), 0 );
      BOOST_CHECK_EQUAL( get_market_fee_reward( izzyreferrer, jillcoin ), 0 );

      GET_ACTOR(alice);
      GET_ACTOR(bob);
      // Alice and Bob place orders which match
      create_sell_order( alice, jillcoin.amount(200000), core_asset(1) );
      create_sell_order( bob, core_asset(1), jillcoin.amount(100000) );

      const auto izzyregistrar_reward = get_market_fee_reward( izzyregistrar, jillcoin );
      const auto izzyreferrer_reward = get_market_fee_reward( izzyreferrer, jillcoin );
      BOOST_CHECK_GT(izzyregistrar_reward , 0);
      BOOST_CHECK_GT(izzyreferrer_reward , 0);
   }
   FC_LOG_AND_RETHROW()
}

BOOST_AUTO_TEST_CASE(white_list_contains_registrar_test)
{
   try
   {
      INVOKE(create_actors);

      generate_blocks_past_hf1268();
      GET_ACTOR(jill);

      constexpr auto jillcoin_reward_percent = 2*GRAPHENE_1_PERCENT;
      const asset_object &jillcoin = get_asset("JCOIN");

      GET_ACTOR(izzyregistrar);
      GET_ACTOR(izzyreferrer);
      flat_set<account_id_type> whitelist = {jill_id, izzyregistrar_id};

      update_asset(jill_id, jill_private_key, jillcoin.get_id(), jillcoin_reward_percent, whitelist);

      BOOST_CHECK_EQUAL( get_market_fee_reward( izzyregistrar, jillcoin ), 0 );
      BOOST_CHECK_EQUAL( get_market_fee_reward( izzyreferrer, jillcoin ), 0 );

      GET_ACTOR(alice);
      GET_ACTOR(bob);
      // Alice and Bob place orders which match
      create_sell_order( alice, jillcoin.amount(200000), core_asset(1) );
      create_sell_order( bob, core_asset(1), jillcoin.amount(100000) );

      const auto izzyregistrar_reward = get_market_fee_reward( izzyregistrar, jillcoin );
      const auto izzyreferrer_reward = get_market_fee_reward( izzyreferrer, jillcoin );
      BOOST_CHECK_GT(izzyregistrar_reward , 0);
      BOOST_CHECK_GT(izzyreferrer_reward , 0);
   }
   FC_LOG_AND_RETHROW()
}

BOOST_AUTO_TEST_CASE(white_list_contains_referrer_test)
{
   try
   {
      INVOKE(create_actors);

      generate_blocks_past_hf1268();
      GET_ACTOR(jill);

      constexpr auto jillcoin_reward_percent = 2*GRAPHENE_1_PERCENT;
      const asset_object &jillcoin = get_asset("JCOIN");

      GET_ACTOR(izzyregistrar);
      GET_ACTOR(izzyreferrer);
      flat_set<account_id_type> whitelist = {jill_id, izzyreferrer_id};

      update_asset(jill_id, jill_private_key, jillcoin.get_id(), jillcoin_reward_percent, whitelist);

      BOOST_CHECK_EQUAL( get_market_fee_reward( izzyregistrar, jillcoin ), 0 );
      BOOST_CHECK_EQUAL( get_market_fee_reward( izzyreferrer, jillcoin ), 0 );

      GET_ACTOR(alice);
      GET_ACTOR(bob);
      // Alice and Bob place orders which match
      create_sell_order( alice, jillcoin.amount(200000), core_asset(1) );
      create_sell_order( bob, core_asset(1), jillcoin.amount(100000) );

      BOOST_CHECK_EQUAL( get_market_fee_reward( izzyregistrar, jillcoin ), 0 );
      BOOST_CHECK_EQUAL( get_market_fee_reward( izzyreferrer, jillcoin ), 0 );
   }
   FC_LOG_AND_RETHROW()
}

BOOST_AUTO_TEST_CASE(white_list_doesnt_contain_registrar_test)
{
   try
   {
      INVOKE(create_actors);

      generate_blocks_past_hf1268();
      GET_ACTOR(jill);

      constexpr auto jillcoin_reward_percent = 2*GRAPHENE_1_PERCENT;
      const asset_object &jillcoin = get_asset("JCOIN");

      GET_ACTOR(alice);
      flat_set<account_id_type> whitelist = {jill_id, alice_id};

      update_asset(jill_id, jill_private_key, jillcoin.get_id(), jillcoin_reward_percent, whitelist);

      GET_ACTOR(izzyregistrar);
      GET_ACTOR(izzyreferrer);
      BOOST_CHECK_EQUAL( get_market_fee_reward( izzyregistrar, jillcoin ), 0 );
      BOOST_CHECK_EQUAL( get_market_fee_reward( izzyreferrer, jillcoin ), 0 );

      GET_ACTOR(bob);
      // Alice and Bob place orders which match
      create_sell_order( alice, jillcoin.amount(200000), core_asset(1) );
      create_sell_order( bob, core_asset(1), jillcoin.amount(100000) );

      BOOST_CHECK_EQUAL( get_market_fee_reward( izzyregistrar, jillcoin ), 0 );
      BOOST_CHECK_EQUAL( get_market_fee_reward( izzyreferrer, jillcoin ), 0);
   }
   FC_LOG_AND_RETHROW()
}

BOOST_AUTO_TEST_CASE(create_asset_via_proposal_test)
{
   try
   {
      ACTOR(issuer);
      price core_exchange_rate(asset(1, asset_id_type(1)), asset(1));
      
      asset_create_operation create_op;
      create_op.issuer = issuer.id;
      create_op.fee = asset();
      create_op.symbol = "ASSET";
      create_op.common_options.max_supply = 0;
      create_op.precision = 2;
      create_op.common_options.core_exchange_rate = core_exchange_rate;
      create_op.common_options.max_supply = GRAPHENE_MAX_SHARE_SUPPLY;
      create_op.common_options.flags = charge_market_fee;

      additional_asset_options_t options;
      options.value.reward_percent = 100;
      options.value.whitelist_market_fee_sharing = flat_set<account_id_type>{issuer_id};
      create_op.common_options.extensions = std::move(options);;

      const auto& curfees = *db.get_global_properties().parameters.current_fees;
      const auto& proposal_create_fees = curfees.get<proposal_create_operation>();
      proposal_create_operation prop;
      prop.fee_paying_account = issuer_id;
      prop.proposed_ops.emplace_back( create_op );
      prop.expiration_time =  db.head_block_time() + fc::days(1);
      prop.fee = asset( proposal_create_fees.fee + proposal_create_fees.price_per_kbyte );

      {
         signed_transaction tx;
         tx.operations.push_back( prop );
         db.current_fee_schedule().set_fee( tx.operations.back() );
         set_expiration( db, tx );
         sign( tx, issuer_private_key );
         GRAPHENE_CHECK_THROW(PUSH_TX( db, tx ), fc::exception);
      }

      generate_blocks_past_hf1268();

      {
         prop.expiration_time =  db.head_block_time() + fc::days(1);
         signed_transaction tx;
         tx.operations.push_back( prop );
         db.current_fee_schedule().set_fee( tx.operations.back() );
         set_expiration( db, tx );
         sign( tx, issuer_private_key );
         PUSH_TX( db, tx );
      }
   }
   FC_LOG_AND_RETHROW()
}

BOOST_AUTO_TEST_CASE(update_asset_via_proposal_test)
{
   try
   {
      ACTOR(issuer);
      asset_object usd_asset = create_user_issued_asset("USD", issuer, charge_market_fee);

      additional_asset_options_t options;
      options.value.reward_percent = 100;
      options.value.whitelist_market_fee_sharing = flat_set<account_id_type>{issuer_id};

      asset_update_operation update_op;
      update_op.issuer = issuer_id;
      update_op.asset_to_update = usd_asset.get_id();
      asset_options new_options;
      update_op.new_options = usd_asset.options;
      update_op.new_options.extensions = std::move(options);

      const auto& curfees = *db.get_global_properties().parameters.current_fees;
      const auto& proposal_create_fees = curfees.get<proposal_create_operation>();
      proposal_create_operation prop;
      prop.fee_paying_account = issuer_id;
      prop.proposed_ops.emplace_back( update_op );
      prop.expiration_time =  db.head_block_time() + fc::days(1);
      prop.fee = asset( proposal_create_fees.fee + proposal_create_fees.price_per_kbyte );

      {
         signed_transaction tx;
         tx.operations.push_back( prop );
         db.current_fee_schedule().set_fee( tx.operations.back() );
         set_expiration( db, tx );
         sign( tx, issuer_private_key );
         GRAPHENE_CHECK_THROW(PUSH_TX( db, tx ), fc::exception);
      }

      generate_blocks_past_hf1268();

      {
         prop.expiration_time =  db.head_block_time() + fc::days(1);
         signed_transaction tx;
         tx.operations.push_back( prop );
         db.current_fee_schedule().set_fee( tx.operations.back() );
         set_expiration( db, tx );
         sign( tx, issuer_private_key );
         PUSH_TX( db, tx );
      }
   }
   FC_LOG_AND_RETHROW()
}

BOOST_AUTO_TEST_CASE(issue_asset){
   try
   {
       ACTORS((alice)(bob)(izzy)(jill));
      // Izzy issues asset to Alice  (Izzycoin market percent - 10%)
      // Jill issues asset to Bob    (Jillcoin market percent - 20%)

      fund( alice, core_asset(1000000) );
      fund( bob, core_asset(1000000) );
      fund( izzy, core_asset(1000000) );
      fund( jill, core_asset(1000000) );

      price price(asset(1, asset_id_type(1)), asset(1));
      constexpr auto izzycoin_market_percent = 10*GRAPHENE_1_PERCENT;
      asset_object izzycoin = create_user_issued_asset( "IZZYCOIN", izzy,  charge_market_fee, price, 2, izzycoin_market_percent );

      constexpr auto jillcoin_market_percent = 20*GRAPHENE_1_PERCENT;
      asset_object jillcoin = create_user_issued_asset( "JILLCOIN", jill,  charge_market_fee, price, 2, jillcoin_market_percent );

      // Alice and Bob create some coins
      issue_uia( alice, izzycoin.amount( 100000 ) );
      issue_uia( bob, jillcoin.amount( 100000 ) );
   }
   FC_LOG_AND_RETHROW()
}

BOOST_AUTO_TEST_CASE(accumulated_fees_before_hf_test)
{
   try
   {
      INVOKE(issue_asset);

      const asset_object &jillcoin = get_asset("JILLCOIN");
      const asset_object &izzycoin = get_asset("IZZYCOIN");

      GET_ACTOR(alice);
      GET_ACTOR(bob);

      // Alice and Bob place orders which match
      create_sell_order( alice_id, izzycoin.amount(100), jillcoin.amount(300) );   // Alice is willing to sell her Izzy's for 3 Jill
      create_sell_order(   bob_id, jillcoin.amount(700), izzycoin.amount(200) );   // Bob is buying up to 200 Izzy's for up to 3.5 Jill

      // 100 Izzys and 300 Jills are matched, so the fees should be
      // 10 Izzy (10%) and 60 Jill (20%).
      BOOST_CHECK( izzycoin.dynamic_asset_data_id(db).accumulated_fees == izzycoin.amount(10).amount );
      BOOST_CHECK( jillcoin.dynamic_asset_data_id(db).accumulated_fees == jillcoin.amount(60).amount );
   }
   FC_LOG_AND_RETHROW()
}

BOOST_AUTO_TEST_CASE(accumulated_fees_after_hf_test)
{
   try
   {
      INVOKE(issue_asset);

      generate_blocks_past_hf1268();

      const asset_object &jillcoin = get_asset("JILLCOIN");
      const asset_object &izzycoin = get_asset("IZZYCOIN");

      GET_ACTOR(alice);
      GET_ACTOR(bob);

      // Alice and Bob place orders which match
      create_sell_order( alice_id, izzycoin.amount(100), jillcoin.amount(300) );   // Alice is willing to sell her Izzy's for 3 Jill
      create_sell_order(   bob_id, jillcoin.amount(700), izzycoin.amount(200) );   // Bob is buying up to 200 Izzy's for up to 3.5 Jill

      // 100 Izzys and 300 Jills are matched, so the fees should be
      // 10 Izzy (10%) and 60 Jill (20%).
      BOOST_CHECK( izzycoin.dynamic_asset_data_id(db).accumulated_fees == izzycoin.amount(10).amount );
      BOOST_CHECK( jillcoin.dynamic_asset_data_id(db).accumulated_fees == jillcoin.amount(60).amount );
   }
   FC_LOG_AND_RETHROW()
}

BOOST_AUTO_TEST_CASE(accumulated_fees_with_additional_options_after_hf_test)
{
   try
   {
      INVOKE(issue_asset);

      generate_blocks_past_hf1268();

      GET_ACTOR(jill);
      GET_ACTOR(izzy);

      const asset_object &jillcoin = get_asset("JILLCOIN");
      const asset_object &izzycoin = get_asset("IZZYCOIN");

      uint16_t reward_percent = 0;
      update_asset(jill_id, jill_private_key, jillcoin.get_id(), reward_percent);
      update_asset(izzy_id, izzy_private_key, izzycoin.get_id(), reward_percent);

      GET_ACTOR(alice);
      GET_ACTOR(bob);

      // Alice and Bob place orders which match
      create_sell_order( alice_id, izzycoin.amount(100), jillcoin.amount(300) );   // Alice is willing to sell her Izzy's for 3 Jill
      create_sell_order(   bob_id, jillcoin.amount(700), izzycoin.amount(200) );   // Bob is buying up to 200 Izzy's for up to 3.5 Jill

      // 100 Izzys and 300 Jills are matched, so the fees should be
      // 10 Izzy (10%) and 60 Jill (20%).
      BOOST_CHECK( izzycoin.dynamic_asset_data_id(db).accumulated_fees == izzycoin.amount(10).amount );
      BOOST_CHECK( jillcoin.dynamic_asset_data_id(db).accumulated_fees == jillcoin.amount(60).amount );
   }
   FC_LOG_AND_RETHROW()
}

BOOST_AUTO_TEST_CASE( create_vesting_balance_with_instant_vesting_policy_before_hf1268_test )
{ try {

   ACTOR(alice);
   fund(alice);

   const asset_object& core = asset_id_type()(db);

   vesting_balance_create_operation op;
   op.fee = core.amount( 0 );
   op.creator = alice_id;
   op.owner = alice_id;
   op.amount = core.amount( 100 );
   op.policy = instant_vesting_policy_initializer{};

   trx.operations.push_back(op);
   set_expiration( db, trx );
   sign(trx, alice_private_key);

   GRAPHENE_REQUIRE_THROW(PUSH_TX( db, trx, ~0 ), fc::exception);

} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE( create_vesting_balance_with_instant_vesting_policy_after_hf1268_test )
{ try {

   ACTOR(alice);
   fund(alice);

   generate_blocks_past_hf1268();

   const asset_object& core = asset_id_type()(db);

   vesting_balance_create_operation op;
   op.fee = core.amount( 0 );
   op.creator = alice_id;
   op.owner = alice_id;
   op.amount = core.amount( 100 );
   op.policy = instant_vesting_policy_initializer{};

   trx.operations.push_back(op);
   set_expiration( db, trx );

   processed_transaction ptx = PUSH_TX( db, trx, ~0 );
   const vesting_balance_id_type& vbid = ptx.operation_results.back().get<object_id_type>();

   auto withdraw = [&](const asset& amount) {
      vesting_balance_withdraw_operation withdraw_op;
      withdraw_op.vesting_balance = vbid;
      withdraw_op.owner = alice_id;
      withdraw_op.amount = amount;

      signed_transaction withdraw_tx;
      withdraw_tx.operations.push_back( withdraw_op );
      set_expiration( db, withdraw_tx );
      sign(withdraw_tx, alice_private_key);
      PUSH_TX( db, withdraw_tx );
   };
   // try to withdraw more then it is on the balance
   GRAPHENE_REQUIRE_THROW(withdraw(op.amount.amount + 1), fc::exception);
   //to withdraw all that is on the balance
   withdraw(op.amount);
   // try to withdraw more then it is on the balance
   GRAPHENE_REQUIRE_THROW(withdraw( core.amount(1) ), fc::exception);
} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE( create_vesting_balance_with_instant_vesting_policy_via_proposal_test )
{ try {

   ACTOR(actor);
   fund(actor);

   const asset_object& core = asset_id_type()(db);

   vesting_balance_create_operation create_op;
   create_op.fee = core.amount( 0 );
   create_op.creator = actor_id;
   create_op.owner = actor_id;
   create_op.amount = core.amount( 100 );
   create_op.policy = instant_vesting_policy_initializer{};

   const auto& curfees = *db.get_global_properties().parameters.current_fees;
   const auto& proposal_create_fees = curfees.get<proposal_create_operation>();
   proposal_create_operation prop;
   prop.fee_paying_account = actor_id;
   prop.proposed_ops.emplace_back( create_op );
   prop.expiration_time =  db.head_block_time() + fc::days(1);
   prop.fee = asset( proposal_create_fees.fee + proposal_create_fees.price_per_kbyte );

   {
      signed_transaction tx;
      tx.operations.push_back( prop );
      db.current_fee_schedule().set_fee( tx.operations.back() );
      set_expiration( db, tx );
      sign( tx, actor_private_key );
      GRAPHENE_CHECK_THROW(PUSH_TX( db, tx ), fc::exception);
   }

   generate_blocks_past_hf1268();

   {
      prop.expiration_time =  db.head_block_time() + fc::days(1);
      signed_transaction tx;
      tx.operations.push_back( prop );
      db.current_fee_schedule().set_fee( tx.operations.back() );
      set_expiration( db, tx );
      sign( tx, actor_private_key );
      PUSH_TX( db, tx );
   }
} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE(white_list_asset_rewards_test)
{
   try
   {
      ACTORS((aliceregistrar)(bobregistrar)(alicereferrer)(bobreferrer)(izzy)(jill));

      // Izzy issues white_list asset to Alice
      // Jill issues white_list asset to Bob
      // Bobreferrer added to blacklist for izzycoin asset
      // Aliceregistrar added to blacklist for jillcoin asset
      // Alice and Bob trade in the market and pay fees
      // Check registrar/referrer rewards
      upgrade_to_lifetime_member(aliceregistrar);
      upgrade_to_lifetime_member(alicereferrer);
      upgrade_to_lifetime_member(bobregistrar);
      upgrade_to_lifetime_member(bobreferrer);
      upgrade_to_lifetime_member(izzy);
      upgrade_to_lifetime_member(jill);

      const account_object alice = create_account("alice", aliceregistrar, alicereferrer, 20*GRAPHENE_1_PERCENT);
      const account_object bob   = create_account("bob", bobregistrar, bobreferrer, 20*GRAPHENE_1_PERCENT);

      fund( alice, core_asset(1000000) );
      fund( bob, core_asset(1000000) );
      fund( izzy, core_asset(1000000) );
      fund( jill, core_asset(1000000) );

      price price(asset(1, asset_id_type(1)), asset(1));
      constexpr auto izzycoin_market_percent = 10*GRAPHENE_1_PERCENT;
      constexpr auto jillcoin_market_percent = 20*GRAPHENE_1_PERCENT;
      const asset_id_type izzycoin_id = create_user_issued_asset( "IZZYCOIN", izzy, charge_market_fee|white_list, price, 0, izzycoin_market_percent ).id;
      const asset_id_type jillcoin_id = create_user_issued_asset( "JILLCOIN", jill, charge_market_fee|white_list, price, 0, jillcoin_market_percent ).id;

      // Alice and Bob create some coins
      issue_uia( alice, izzycoin_id(db).amount( 200000 ) );
      issue_uia( bob, jillcoin_id(db).amount( 200000 ) );

      generate_blocks_past_hf1268();

      constexpr auto izzycoin_reward_percent = 50*GRAPHENE_1_PERCENT;
      constexpr auto jillcoin_reward_percent = 50*GRAPHENE_1_PERCENT;

      update_asset(izzy_id, izzy_private_key, izzycoin_id, izzycoin_reward_percent);
      update_asset(jill_id, jill_private_key, jillcoin_id, jillcoin_reward_percent);

      BOOST_TEST_MESSAGE( "Attempting to blacklist bobreferrer for izzycoin asset" );
      asset_update_blacklist_authority(izzy_id, izzycoin_id, izzy_id, izzy_private_key);
      add_account_to_blacklist(izzy_id, bobreferrer_id, izzy_private_key);
      BOOST_CHECK( !(is_authorized_asset( db, bobreferrer_id(db), izzycoin_id(db) )) );

      BOOST_TEST_MESSAGE( "Attempting to blacklist aliceregistrar for jillcoin asset" );
      asset_update_blacklist_authority(jill_id, jillcoin_id, jill_id, jill_private_key);
      add_account_to_blacklist(jill_id, aliceregistrar_id, jill_private_key);
      BOOST_CHECK( !(is_authorized_asset( db, aliceregistrar_id(db), jillcoin_id(db) )) );

      // Alice and Bob place orders which match
      create_sell_order( alice.id, izzycoin_id(db).amount(1000), jillcoin_id(db).amount(1500) ); // Alice is willing to sell her 1000 Izzy's for 1.5 Jill
      create_sell_order(   bob.id, jillcoin_id(db).amount(1500), izzycoin_id(db).amount(1000) );   // Bob is buying up to 1500 Izzy's for up to 0.6 Jill

      // 1000 Izzys and 1500 Jills are matched, so the fees should be
      //   100 Izzy (10%) and 300 Jill (20%).

      // Only Bob's registrar should get rewards
      share_type bob_registrar_reward = get_market_fee_reward( bob.registrar, izzycoin_id );
      BOOST_CHECK_GT( bob_registrar_reward, 0 );
      BOOST_CHECK_EQUAL( get_market_fee_reward( bob.referrer, izzycoin_id ), 0 );
      BOOST_CHECK_EQUAL( get_market_fee_reward( alice.registrar, jillcoin_id ), 0 );
      BOOST_CHECK_EQUAL( get_market_fee_reward( alice.referrer, jillcoin_id ), 0 );
   }
   FC_LOG_AND_RETHROW()
}

BOOST_AUTO_TEST_CASE( create_vesting_balance_object_test )
{ 
   /**
    * Test checks that an account could have duplicates VBO (with the same asset_type)
    * for any type of vesting_balance_type
    * except vesting_balance_type::market_fee_sharing
   */
   try {

      ACTOR(actor);

      create_vesting_balance_object(actor_id, vesting_balance_type::unspecified);
      create_vesting_balance_object(actor_id, vesting_balance_type::unspecified);

      create_vesting_balance_object(actor_id, vesting_balance_type::cashback);
      create_vesting_balance_object(actor_id, vesting_balance_type::cashback);

      create_vesting_balance_object(actor_id, vesting_balance_type::witness);
      create_vesting_balance_object(actor_id, vesting_balance_type::witness);

      create_vesting_balance_object(actor_id, vesting_balance_type::worker);
      create_vesting_balance_object(actor_id, vesting_balance_type::worker);

      create_vesting_balance_object(actor_id, vesting_balance_type::market_fee_sharing);
      GRAPHENE_CHECK_THROW(create_vesting_balance_object(actor_id, vesting_balance_type::market_fee_sharing), fc::exception);

} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_SUITE_END()
