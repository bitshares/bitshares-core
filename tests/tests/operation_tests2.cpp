/*
 * Copyright (c) 2015, Cryptonomex, Inc.
 * All rights reserved.
 *
 * This source code is provided for evaluation in private test networks only, until September 8, 2015. After this date, this license expires and
 * the code may not be used, modified or distributed for any purpose. Redistribution and use in source and binary forms, with or without modification,
 * are permitted until September 8, 2015, provided that the following conditions are met:
 *
 * 1. The code and/or derivative works are used only for private test networks consisting of no more than 10 P2P nodes.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <boost/test/unit_test.hpp>

#include <graphene/chain/database.hpp>
#include <graphene/chain/operations.hpp>
#include <graphene/chain/key_object.hpp>
#include <graphene/chain/asset_object.hpp>
#include <graphene/chain/account_object.hpp>
#include <graphene/chain/witness_object.hpp>
#include <graphene/chain/delegate_object.hpp>
#include <graphene/chain/call_order_object.hpp>
#include <graphene/chain/vesting_balance_object.hpp>
#include <graphene/chain/withdraw_permission_object.hpp>

#include <graphene/chain/predicate.hpp>
#include <graphene/chain/db_reflect_cmp.hpp>

#include <fc/crypto/digest.hpp>

#include "../common/database_fixture.hpp"

using namespace graphene::chain;

BOOST_FIXTURE_TEST_SUITE( operation_tests, database_fixture )

BOOST_AUTO_TEST_CASE( withdraw_permission_create )
{ try {
   auto nathan_private_key = generate_private_key("nathan");
   auto dan_private_key = generate_private_key("dan");
   key_id_type nathan_key_id = register_key(nathan_private_key.get_public_key()).id;
   key_id_type dan_key_id = register_key(dan_private_key.get_public_key()).id;
   account_id_type nathan_id = create_account("nathan", nathan_key_id).id;
   account_id_type dan_id = create_account("dan", dan_key_id).id;
   transfer(account_id_type(), nathan_id, asset(1000));
   generate_block();
   trx.set_expiration(db.head_block_time() + GRAPHENE_DEFAULT_MAX_TIME_UNTIL_EXPIRATION);

   {
      withdraw_permission_create_operation op;
      op.authorized_account = dan_id;
      op.withdraw_from_account = nathan_id;
      op.withdrawal_limit = asset(5);
      op.withdrawal_period_sec = fc::hours(1).to_seconds();
      op.periods_until_expiration = 5;
      op.period_start_time = db.head_block_time() + db.get_global_properties().parameters.block_interval*5;
      trx.operations.push_back(op);
      REQUIRE_OP_VALIDATION_FAILURE(op, withdrawal_limit, asset());
      REQUIRE_OP_VALIDATION_FAILURE(op, periods_until_expiration, 0);
      REQUIRE_OP_VALIDATION_FAILURE(op, withdraw_from_account, dan_id);
      REQUIRE_OP_VALIDATION_FAILURE(op, withdrawal_period_sec, 0);
      REQUIRE_THROW_WITH_VALUE(op, withdrawal_limit, asset(10, 10));
      REQUIRE_THROW_WITH_VALUE(op, authorized_account, account_id_type(1000));
      REQUIRE_THROW_WITH_VALUE(op, period_start_time, fc::time_point_sec(10000));
      REQUIRE_THROW_WITH_VALUE(op, withdrawal_period_sec, 1);
      trx.operations.back() = op;
   }

   trx.sign(nathan_key_id, nathan_private_key);
   PUSH_TX( db, trx );
   trx.clear();
} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE( withdraw_permission_test )
{ try {
   INVOKE(withdraw_permission_create);

   auto nathan_private_key = generate_private_key("nathan");
   auto dan_private_key = generate_private_key("dan");
   account_id_type nathan_id = get_account("nathan").id;
   account_id_type dan_id = get_account("dan").id;
   key_id_type dan_key_id = dan_id(db).active.auths.begin()->first;
   withdraw_permission_id_type permit;
   trx.set_expiration(db.head_block_time() + GRAPHENE_DEFAULT_MAX_TIME_UNTIL_EXPIRATION);

   fc::time_point_sec first_start_time;
   {
      const withdraw_permission_object& permit_object = permit(db);
      BOOST_CHECK(permit_object.authorized_account == dan_id);
      BOOST_CHECK(permit_object.withdraw_from_account == nathan_id);
      BOOST_CHECK(permit_object.period_start_time > db.head_block_time());
      first_start_time = permit_object.period_start_time;
      BOOST_CHECK(permit_object.withdrawal_limit == asset(5));
      BOOST_CHECK(permit_object.withdrawal_period_sec == fc::hours(1).to_seconds());
      BOOST_CHECK(permit_object.expiration == first_start_time + permit_object.withdrawal_period_sec*5 );
   }

   generate_blocks(2);

   {
      withdraw_permission_claim_operation op;
      op.withdraw_permission = permit;
      op.withdraw_from_account = nathan_id;
      op.withdraw_to_account = dan_id;
      op.amount_to_withdraw = asset(1);
      trx.operations.push_back(op);
      //Throws because we haven't entered the first withdrawal period yet.
      BOOST_REQUIRE_THROW(PUSH_TX( db, trx ), fc::exception);
      //Get to the actual withdrawal period
      generate_blocks(permit(db).period_start_time);

      REQUIRE_THROW_WITH_VALUE(op, withdraw_permission, withdraw_permission_id_type(5));
      REQUIRE_THROW_WITH_VALUE(op, withdraw_from_account, dan_id);
      REQUIRE_THROW_WITH_VALUE(op, withdraw_from_account, account_id_type());
      REQUIRE_THROW_WITH_VALUE(op, withdraw_to_account, nathan_id);
      REQUIRE_THROW_WITH_VALUE(op, withdraw_to_account, account_id_type());
      REQUIRE_THROW_WITH_VALUE(op, amount_to_withdraw, asset(10));
      REQUIRE_THROW_WITH_VALUE(op, amount_to_withdraw, asset(6));
      trx.clear();
      trx.operations.push_back(op);
      trx.sign(dan_key_id, dan_private_key);
      PUSH_TX( db, trx );

      // would be legal on its own, but doesn't work because trx already withdrew
      REQUIRE_THROW_WITH_VALUE(op, amount_to_withdraw, asset(5));

      // Make sure we can withdraw again this period, as long as we're not exceeding the periodic limit
      trx.operations.back() = op;    // withdraw 1
      trx.ref_block_prefix++;        // make it different from previous trx so it's non-duplicate
      trx.sign(dan_key_id, dan_private_key);
      PUSH_TX( db, trx );
      trx.clear();
   }

   BOOST_CHECK_EQUAL(get_balance(nathan_id, asset_id_type()), 998);
   BOOST_CHECK_EQUAL(get_balance(dan_id, asset_id_type()), 2);

   {
      const withdraw_permission_object& permit_object = permit(db);
      BOOST_CHECK(permit_object.authorized_account == dan_id);
      BOOST_CHECK(permit_object.withdraw_from_account == nathan_id);
      BOOST_CHECK(permit_object.period_start_time == first_start_time);
      BOOST_CHECK(permit_object.withdrawal_limit == asset(5));
      BOOST_CHECK(permit_object.withdrawal_period_sec == fc::hours(1).to_seconds());
      BOOST_CHECK_EQUAL(permit_object.claimed_this_period.value, 2 );
      BOOST_CHECK(permit_object.expiration == first_start_time + 5*permit_object.withdrawal_period_sec );
      generate_blocks(first_start_time + permit_object.withdrawal_period_sec);
      // lazy update:  verify period_start_time isn't updated until new trx occurs
      BOOST_CHECK(permit_object.period_start_time == first_start_time );
   }

   {
      transfer(nathan_id, dan_id, asset(997));
      withdraw_permission_claim_operation op;
      op.withdraw_permission = permit;
      op.withdraw_from_account = nathan_id;
      op.withdraw_to_account = dan_id;
      op.amount_to_withdraw = asset(5);
      trx.operations.push_back(op);
      trx.sign(dan_key_id, dan_private_key);
      //Throws because nathan doesn't have the money
      BOOST_CHECK_THROW(PUSH_TX( db, trx ), fc::exception);
      op.amount_to_withdraw = asset(1);
      trx.operations.back() = op;
      trx.sign(dan_key_id, dan_private_key);
      PUSH_TX( db, trx );
   }

   BOOST_CHECK_EQUAL(get_balance(nathan_id, asset_id_type()), 0);
   BOOST_CHECK_EQUAL(get_balance(dan_id, asset_id_type()), 1000);
   trx.clear();
   transfer(dan_id, nathan_id, asset(1000));

   {
      const withdraw_permission_object& permit_object = permit(db);
      BOOST_CHECK(permit_object.authorized_account == dan_id);
      BOOST_CHECK(permit_object.withdraw_from_account == nathan_id);
      BOOST_CHECK(permit_object.period_start_time == first_start_time + permit_object.withdrawal_period_sec );
      BOOST_CHECK(permit_object.expiration == first_start_time + 5*permit_object.withdrawal_period_sec );
      BOOST_CHECK(permit_object.withdrawal_limit == asset(5));
      BOOST_CHECK(permit_object.withdrawal_period_sec == fc::hours(1).to_seconds());
      generate_blocks(permit_object.expiration);
   }
   // Ensure the permit object has been garbage collected
   BOOST_CHECK(db.find_object( permit ) == nullptr );

   {
      withdraw_permission_claim_operation op;
      op.withdraw_permission = permit;
      op.withdraw_from_account = nathan_id;
      op.withdraw_to_account = dan_id;
      op.amount_to_withdraw = asset(5);
      trx.operations.push_back(op);
      trx.sign(dan_key_id, dan_private_key);
      //Throws because the permission has expired
      BOOST_CHECK_THROW(PUSH_TX( db, trx ), fc::exception);
   }
} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE( withdraw_permission_nominal_case )
{ try {
   INVOKE(withdraw_permission_create);

   auto nathan_private_key = generate_private_key("nathan");
   auto dan_private_key = generate_private_key("dan");
   account_id_type nathan_id = get_account("nathan").id;
   account_id_type dan_id = get_account("dan").id;
   key_id_type dan_key_id = dan_id(db).active.auths.begin()->first;
   withdraw_permission_id_type permit;
   trx.set_expiration(db.head_block_time() + GRAPHENE_DEFAULT_MAX_TIME_UNTIL_EXPIRATION);

   while(true)
   {
      const withdraw_permission_object& permit_object = permit(db);
      //wdump( (permit_object) );
      withdraw_permission_claim_operation op;
      op.withdraw_permission = permit;
      op.withdraw_from_account = nathan_id;
      op.withdraw_to_account = dan_id;
      op.amount_to_withdraw = asset(5);
      trx.operations.push_back(op);
      // ref_block_prefix is timestamp, so treat it as a rollable nonce
      // so tx's have different txid's
      trx.ref_block_prefix++;
      trx.sign(dan_key_id, dan_private_key);
      PUSH_TX( db, trx );
      // tx's involving withdraw_permissions can't delete it even
      // if no further withdrawals are possible
      BOOST_CHECK(db.find_object(permit) != nullptr);
      BOOST_CHECK( permit_object.claimed_this_period == 5 );
      trx.clear();
      generate_blocks(
           permit_object.period_start_time
         + permit_object.withdrawal_period_sec );
      if( db.find_object(permit) == nullptr )
         break;
   }

   BOOST_CHECK_EQUAL(get_balance(nathan_id, asset_id_type()), 975);
   BOOST_CHECK_EQUAL(get_balance(dan_id, asset_id_type()), 25);
} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE( withdraw_permission_update )
{ try {
   INVOKE(withdraw_permission_create);

   auto nathan_private_key = generate_private_key("nathan");
   account_id_type nathan_id = get_account("nathan").id;
   account_id_type dan_id = get_account("dan").id;
   key_id_type nathan_key_id = nathan_id(db).active.auths.begin()->first;
   withdraw_permission_id_type permit;
   trx.set_expiration(db.head_block_time() + GRAPHENE_DEFAULT_MAX_TIME_UNTIL_EXPIRATION);

   {
      withdraw_permission_update_operation op;
      op.permission_to_update = permit;
      op.authorized_account = dan_id;
      op.withdraw_from_account = nathan_id;
      op.periods_until_expiration = 2;
      op.period_start_time = db.head_block_time() + 10;
      op.withdrawal_period_sec = 10;
      op.withdrawal_limit = asset(12);
      trx.operations.push_back(op);
      REQUIRE_THROW_WITH_VALUE(op, periods_until_expiration, 0);
      REQUIRE_THROW_WITH_VALUE(op, withdrawal_period_sec, 0);
      REQUIRE_THROW_WITH_VALUE(op, withdrawal_limit, asset(1, 12));
      REQUIRE_THROW_WITH_VALUE(op, withdrawal_limit, asset(0));
      REQUIRE_THROW_WITH_VALUE(op, withdraw_from_account, account_id_type(0));
      REQUIRE_THROW_WITH_VALUE(op, authorized_account, account_id_type(0));
      REQUIRE_THROW_WITH_VALUE(op, period_start_time, db.head_block_time() - 50);
      trx.operations.back() = op;
      trx.sign(nathan_key_id, nathan_private_key);
      PUSH_TX( db, trx );
   }

   {
      const withdraw_permission_object& permit_object = db.get(permit);
      BOOST_CHECK(permit_object.authorized_account == dan_id);
      BOOST_CHECK(permit_object.withdraw_from_account == nathan_id);
      BOOST_CHECK(permit_object.period_start_time == db.head_block_time() + 10);
      BOOST_CHECK(permit_object.withdrawal_limit == asset(12));
      BOOST_CHECK(permit_object.withdrawal_period_sec == 10);
      // BOOST_CHECK(permit_object.remaining_periods == 2);
   }
} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE( withdraw_permission_delete )
{ try {
   INVOKE(withdraw_permission_update);

   withdraw_permission_delete_operation op;
   op.authorized_account = get_account("dan").id;
   op.withdraw_from_account = get_account("nathan").id;
   trx.set_expiration(db.head_block_id());
   trx.operations.push_back(op);
   trx.sign(get_account("nathan").active.auths.begin()->first, generate_private_key("nathan"));
   PUSH_TX( db, trx );
} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE( mia_feeds )
{ try {
   ACTORS((nathan)(dan)(ben)(vikram));
   asset_id_type bit_usd_id = create_bitasset("BITUSD").id;

   {
      asset_update_operation op;
      const asset_object& obj = bit_usd_id(db);
      op.asset_to_update = bit_usd_id;
      op.issuer = obj.issuer;
      op.new_issuer = nathan_id;
      op.new_options = obj.options;
      trx.operations.push_back(op);
      PUSH_TX( db, trx, ~0 );
      generate_block();
      trx.clear();
   }
   {
      asset_update_feed_producers_operation op;
      op.asset_to_update = bit_usd_id;
      op.issuer = nathan_id;
      op.new_feed_producers = {dan_id, ben_id, vikram_id};
      trx.operations.push_back(op);
      trx.sign(nathan_key_id, nathan_private_key);
      PUSH_TX( db, trx );
      generate_block(database::skip_nothing);
   }
   {
      const asset_bitasset_data_object& obj = bit_usd_id(db).bitasset_data(db);
      BOOST_CHECK_EQUAL(obj.feeds.size(), 3);
      BOOST_CHECK(obj.current_feed == price_feed());
   }
   {
      const asset_object& bit_usd = bit_usd_id(db);
      asset_publish_feed_operation op({asset(), vikram_id});
      op.asset_id = bit_usd_id;
      op.feed.settlement_price = ~price(asset(GRAPHENE_BLOCKCHAIN_PRECISION),bit_usd.amount(30));
      // We'll expire margins after a month
      // Accept defaults for required collateral
      trx.operations.emplace_back(op);
      PUSH_TX( db, trx, ~0 );

      const asset_bitasset_data_object& bitasset = bit_usd.bitasset_data(db);
      BOOST_CHECK(bitasset.current_feed.settlement_price.to_real() == 30.0 / GRAPHENE_BLOCKCHAIN_PRECISION);
      BOOST_CHECK(bitasset.current_feed.maintenance_collateral_ratio == GRAPHENE_DEFAULT_MAINTENANCE_COLLATERAL_RATIO);

      op.publisher = ben_id;
      op.feed.settlement_price = ~price(asset(GRAPHENE_BLOCKCHAIN_PRECISION),bit_usd.amount(25));
      trx.operations.back() = op;
      PUSH_TX( db, trx, ~0 );

      BOOST_CHECK_EQUAL(bitasset.current_feed.settlement_price.to_real(), 30.0 / GRAPHENE_BLOCKCHAIN_PRECISION);
      BOOST_CHECK(bitasset.current_feed.maintenance_collateral_ratio == GRAPHENE_DEFAULT_MAINTENANCE_COLLATERAL_RATIO);

      op.publisher = dan_id;
      op.feed.settlement_price = ~price(asset(GRAPHENE_BLOCKCHAIN_PRECISION),bit_usd.amount(40));
      op.feed.maximum_short_squeeze_ratio = 1001;
      op.feed.maintenance_collateral_ratio = 1001;
      trx.operations.back() = op;
      PUSH_TX( db, trx, ~0 );

      BOOST_CHECK_EQUAL(bitasset.current_feed.settlement_price.to_real(), 30.0 / GRAPHENE_BLOCKCHAIN_PRECISION);
      BOOST_CHECK(bitasset.current_feed.maintenance_collateral_ratio == GRAPHENE_DEFAULT_MAINTENANCE_COLLATERAL_RATIO);

      op.publisher = nathan_id;
      trx.operations.back() = op;
      BOOST_CHECK_THROW(PUSH_TX( db, trx, ~0 ), fc::exception);
   }
} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE( witness_create )
{ try {
   ACTOR(nathan);
   upgrade_to_lifetime_member(nathan_id);
   trx.clear();
   witness_id_type nathan_witness_id = create_witness(nathan_id, nathan_key_id, nathan_private_key).id;
   // Give nathan some voting stake
   transfer(genesis_account, nathan_id, asset(10000000));
   generate_block();
   trx.set_expiration(db.head_block_id());

   {
      account_update_operation op;
      op.account = nathan_id;
      op.new_options = nathan_id(db).options;
      op.new_options->votes.insert(nathan_witness_id(db).vote_id);
      op.new_options->num_witness = std::count_if(op.new_options->votes.begin(), op.new_options->votes.end(),
                                                  [](vote_id_type id) { return id.type() == vote_id_type::witness; });
      op.new_options->num_committee = std::count_if(op.new_options->votes.begin(), op.new_options->votes.end(),
                                                    [](vote_id_type id) { return id.type() == vote_id_type::committee; });
      trx.operations.push_back(op);
      trx.sign(nathan_key_id, nathan_private_key);
      PUSH_TX( db, trx );
      trx.clear();
   }

   generate_blocks(db.get_dynamic_global_properties().next_maintenance_time);
   const auto& witnesses = db.get_global_properties().active_witnesses;

   // make sure we're in active_witnesses
   auto itr = std::find(witnesses.begin(), witnesses.end(), nathan_witness_id);
   BOOST_CHECK(itr != witnesses.end());

   generate_blocks(witnesses.size());

   // make sure we're scheduled to produce
   vector<witness_id_type> near_witnesses = db.get_near_witness_schedule();
   BOOST_CHECK( std::find( near_witnesses.begin(), near_witnesses.end(), nathan_witness_id )
                != near_witnesses.end() );

   struct generator_helper {
      database_fixture& f;
      witness_id_type nathan_id;
      fc::ecc::private_key nathan_key;
      bool nathan_generated_block;

      void operator()(witness_id_type id) {
         if( id == nathan_id )
         {
            nathan_generated_block = true;
            f.generate_block(0, nathan_key);
         } else
            f.generate_block(0);
         BOOST_CHECK_EQUAL(f.db.get_dynamic_global_properties().current_witness.instance.value, id.instance.value);
         f.db.get_near_witness_schedule();
      }
   };

   generator_helper h = std::for_each(near_witnesses.begin(), near_witnesses.end(),
                                      generator_helper{*this, nathan_witness_id, nathan_private_key, false});
   BOOST_CHECK(h.nathan_generated_block);
   generate_block(0, nathan_private_key);
} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE_EXPECTED_FAILURES( unimp_global_settle_test, 1 )
BOOST_AUTO_TEST_CASE( unimp_global_settle_test )
{
   BOOST_FAIL( "TODO - Reimplement this" );
   /*
   try {
   ACTORS((nathan)(ben)(valentine)(dan));
   asset_id_type bit_usd_id = create_bitasset("BITUSD", nathan_id, 100, global_settle | charge_market_fee).get_id();
   transfer(genesis_account, ben_id, asset(10000));
   transfer(genesis_account, valentine_id, asset(10000));
   transfer(genesis_account, dan_id, asset(10000));
   create_short(ben_id, asset(1000, bit_usd_id), asset(1000));
   create_sell_order(valentine_id, asset(1000), asset(1000, bit_usd_id));
   create_short(valentine_id, asset(500, bit_usd_id), asset(600));
   create_sell_order(dan_id, asset(600), asset(500, bit_usd_id));

   BOOST_CHECK_EQUAL(get_balance(valentine_id, bit_usd_id), 990);
   BOOST_CHECK_EQUAL(get_balance(valentine_id, asset_id_type()), 8400);
   BOOST_CHECK_EQUAL(get_balance(ben_id, bit_usd_id), 0);
   BOOST_CHECK_EQUAL(get_balance(ben_id, asset_id_type()), 9000);
   BOOST_CHECK_EQUAL(get_balance(dan_id, bit_usd_id), 495);
   BOOST_CHECK_EQUAL(get_balance(dan_id, asset_id_type()), 9400);

   {
      asset_global_settle_operation op;
      op.asset_to_settle = bit_usd_id;
      op.issuer = nathan_id;
      op.settle_price = ~price(asset(10), asset(11, bit_usd_id));
      trx.clear();
      trx.operations.push_back(op);
      REQUIRE_THROW_WITH_VALUE(op, settle_price, ~price(asset(2001), asset(1000, bit_usd_id)));
      REQUIRE_THROW_WITH_VALUE(op, asset_to_settle, asset_id_type());
      REQUIRE_THROW_WITH_VALUE(op, asset_to_settle, asset_id_type(100));
      REQUIRE_THROW_WITH_VALUE(op, issuer, account_id_type(2));
      trx.operations.back() = op;
      trx.sign(nathan_key_id, nathan_private_key);
      PUSH_TX( db, trx );
   }

   BOOST_CHECK_EQUAL(get_balance(valentine_id, bit_usd_id), 0);
   BOOST_CHECK_EQUAL(get_balance(valentine_id, asset_id_type()), 10046);
   BOOST_CHECK_EQUAL(get_balance(ben_id, bit_usd_id), 0);
   BOOST_CHECK_EQUAL(get_balance(ben_id, asset_id_type()), 10091);
   BOOST_CHECK_EQUAL(get_balance(dan_id, bit_usd_id), 0);
   BOOST_CHECK_EQUAL(get_balance(dan_id, asset_id_type()), 9850);
} FC_LOG_AND_RETHROW()
   */
}

BOOST_AUTO_TEST_CASE( worker_create_test )
{ try {
   ACTOR(nathan);
   upgrade_to_lifetime_member(nathan_id);
   generate_block();

   {
      worker_create_operation op;
      op.owner = nathan_id;
      op.daily_pay = 1000;
      op.initializer = vesting_balance_worker_type::initializer(1);
      op.work_begin_date = db.head_block_time() + 10;
      op.work_end_date = op.work_begin_date + fc::days(2);
      trx.clear();
      trx.operations.push_back(op);
      REQUIRE_THROW_WITH_VALUE(op, daily_pay, -1);
      REQUIRE_THROW_WITH_VALUE(op, daily_pay, 0);
      REQUIRE_THROW_WITH_VALUE(op, owner, account_id_type(1000));
      REQUIRE_THROW_WITH_VALUE(op, work_begin_date, db.head_block_time() - 10);
      REQUIRE_THROW_WITH_VALUE(op, work_end_date, op.work_begin_date);
      trx.operations.back() = op;
      trx.sign(nathan_key_id, nathan_private_key);
      PUSH_TX( db, trx );
   }

   const worker_object& worker = worker_id_type()(db);
   BOOST_CHECK(worker.worker_account == nathan_id);
   BOOST_CHECK(worker.daily_pay == 1000);
   BOOST_CHECK(worker.work_begin_date == db.head_block_time() + 10);
   BOOST_CHECK(worker.work_end_date == db.head_block_time() + 10 + fc::days(2));
   BOOST_CHECK(worker.vote_for.type() == vote_id_type::worker);
   BOOST_CHECK(worker.vote_against.type() == vote_id_type::worker);

   const vesting_balance_object& balance = worker.worker.get<vesting_balance_worker_type>().balance(db);
   BOOST_CHECK(balance.owner == nathan_id);
   BOOST_CHECK(balance.balance == asset(0));
   BOOST_CHECK(balance.policy.get<cdd_vesting_policy>().vesting_seconds == fc::days(1).to_seconds());
} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE( worker_pay_test )
{ try {
   INVOKE(worker_create_test);
   GET_ACTOR(nathan);
   generate_blocks(db.get_dynamic_global_properties().next_maintenance_time);
   transfer(genesis_account, nathan_id, asset(100000));

   {
      account_update_operation op;
      op.account = nathan_id;
      op.new_options = nathan_id(db).options;
      op.new_options->votes.insert(worker_id_type()(db).vote_for);
      trx.operations.push_back(op);
      PUSH_TX( db, trx, ~0 );
      trx.clear();
   }
   {
      asset_burn_operation op;
      op.payer = account_id_type();
      op.amount_to_burn = asset(GRAPHENE_INITIAL_SUPPLY/2);
      trx.operations.push_back(op);
      PUSH_TX( db, trx, ~0 );
      trx.clear();
   }

   BOOST_CHECK_EQUAL(worker_id_type()(db).worker.get<vesting_balance_worker_type>().balance(db).balance.amount.value, 0);
   generate_blocks(db.get_dynamic_global_properties().next_maintenance_time);
   BOOST_CHECK_EQUAL(worker_id_type()(db).worker.get<vesting_balance_worker_type>().balance(db).balance.amount.value, 1000);
   generate_blocks(db.head_block_time() + fc::hours(12));

   {
      vesting_balance_withdraw_operation op;
      op.vesting_balance = worker_id_type()(db).worker.get<vesting_balance_worker_type>().balance;
      op.amount = asset(500);
      op.owner = nathan_id;
      trx.set_expiration(db.head_block_id());
      trx.operations.push_back(op);
      trx.sign(nathan_key_id, nathan_private_key);
      PUSH_TX( db, trx );
      trx.signatures.clear();
      REQUIRE_THROW_WITH_VALUE(op, amount, asset(1));
      trx.clear();
   }

   BOOST_CHECK_EQUAL(get_balance(nathan_id, asset_id_type()), 100500);
   BOOST_CHECK_EQUAL(worker_id_type()(db).worker.get<vesting_balance_worker_type>().balance(db).balance.amount.value, 500);

   {
      account_update_operation op;
      op.account = nathan_id;
      op.new_options = nathan_id(db).options;
      op.new_options->votes.erase(worker_id_type()(db).vote_for);
      trx.operations.push_back(op);
      PUSH_TX( db, trx, ~0 );
      trx.clear();
   }

   generate_blocks(db.head_block_time() + fc::hours(12));
   BOOST_CHECK_EQUAL(worker_id_type()(db).worker.get<vesting_balance_worker_type>().balance(db).balance.amount.value, 500);

   {
      vesting_balance_withdraw_operation op;
      op.vesting_balance = worker_id_type()(db).worker.get<vesting_balance_worker_type>().balance;
      op.amount = asset(500);
      op.owner = nathan_id;
      trx.set_expiration(db.head_block_id());
      trx.operations.push_back(op);
      REQUIRE_THROW_WITH_VALUE(op, amount, asset(500));
      generate_blocks(db.head_block_time() + fc::hours(12));
      trx.set_expiration(db.head_block_id());
      REQUIRE_THROW_WITH_VALUE(op, amount, asset(501));
      trx.operations.back() = op;
      trx.sign(nathan_key_id, nathan_private_key);
      PUSH_TX( db, trx );
      trx.signatures.clear();
      trx.clear();
   }

   BOOST_CHECK_EQUAL(get_balance(nathan_id, asset_id_type()), 101000);
   BOOST_CHECK_EQUAL(worker_id_type()(db).worker.get<vesting_balance_worker_type>().balance(db).balance.amount.value, 0);
} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE( refund_worker_test )
{try{
   ACTOR(nathan);
   upgrade_to_lifetime_member(nathan_id);
   generate_block();
   generate_blocks(db.get_dynamic_global_properties().next_maintenance_time);
   trx.set_expiration(db.head_block_id());

   {
      worker_create_operation op;
      op.owner = nathan_id;
      op.daily_pay = 1000;
      op.initializer = refund_worker_type::initializer();
      op.work_begin_date = db.head_block_time() + 10;
      op.work_end_date = op.work_begin_date + fc::days(2);
      trx.clear();
      trx.operations.push_back(op);
      REQUIRE_THROW_WITH_VALUE(op, daily_pay, -1);
      REQUIRE_THROW_WITH_VALUE(op, daily_pay, 0);
      REQUIRE_THROW_WITH_VALUE(op, owner, account_id_type(1000));
      REQUIRE_THROW_WITH_VALUE(op, work_begin_date, db.head_block_time() - 10);
      REQUIRE_THROW_WITH_VALUE(op, work_end_date, op.work_begin_date);
      trx.operations.back() = op;
      trx.sign(nathan_key_id, nathan_private_key);
      PUSH_TX( db, trx );
      trx.clear();
   }

   const worker_object& worker = worker_id_type()(db);
   BOOST_CHECK(worker.worker_account == nathan_id);
   BOOST_CHECK(worker.daily_pay == 1000);
   BOOST_CHECK(worker.work_begin_date == db.head_block_time() + 10);
   BOOST_CHECK(worker.work_end_date == db.head_block_time() + 10 + fc::days(2));
   BOOST_CHECK(worker.vote_for.type() == vote_id_type::worker);
   BOOST_CHECK(worker.vote_against.type() == vote_id_type::worker);

   transfer(genesis_account, nathan_id, asset(100000));

   {
      account_update_operation op;
      op.account = nathan_id;
      op.new_options = nathan_id(db).options;
      op.new_options->votes.insert(worker_id_type()(db).vote_for);
      trx.operations.push_back(op);
      PUSH_TX( db, trx, ~0 );
      trx.clear();
   }
   {
      asset_burn_operation op;
      op.payer = account_id_type();
      op.amount_to_burn = asset(GRAPHENE_INITIAL_SUPPLY/2);
      trx.operations.push_back(op);
      PUSH_TX( db, trx, ~0 );
      trx.clear();
   }

   // auto supply = asset_id_type()(db).dynamic_data(db).current_supply;
   verify_asset_supplies();
   generate_blocks(db.get_dynamic_global_properties().next_maintenance_time);
   verify_asset_supplies();
   BOOST_CHECK_EQUAL(worker_id_type()(db).worker.get<refund_worker_type>().total_burned.value, 1000);
   generate_blocks(db.get_dynamic_global_properties().next_maintenance_time);
   verify_asset_supplies();
   BOOST_CHECK_EQUAL(worker_id_type()(db).worker.get<refund_worker_type>().total_burned.value, 2000);
   generate_blocks(db.get_dynamic_global_properties().next_maintenance_time);
   BOOST_CHECK(!db.get(worker_id_type()).is_active(db.head_block_time()));
   BOOST_CHECK_EQUAL(worker_id_type()(db).worker.get<refund_worker_type>().total_burned.value, 2000);
}FC_LOG_AND_RETHROW()}

BOOST_AUTO_TEST_CASE_EXPECTED_FAILURES( unimp_force_settlement_unavailable, 1 )
BOOST_AUTO_TEST_CASE( unimp_force_settlement_unavailable )
{
   BOOST_FAIL( "TODO - Reimplement this" );
   /*
   try {
   auto private_key = delegate_priv_key;
   auto private_key = generate_private_key("genesis");
>>>>>>> short_refactor
   account_id_type nathan_id = create_account("nathan").get_id();
   account_id_type shorter1_id = create_account("shorter1").get_id();
   account_id_type shorter2_id = create_account("shorter2").get_id();
   account_id_type shorter3_id = create_account("shorter3").get_id();
   transfer(account_id_type()(db), nathan_id(db), asset(100000000));
   transfer(account_id_type()(db), shorter1_id(db), asset(100000000));
   transfer(account_id_type()(db), shorter2_id(db), asset(100000000));
   transfer(account_id_type()(db), shorter3_id(db), asset(100000000));
   asset_id_type bit_usd = create_bitasset("BITUSD", GRAPHENE_TEMP_ACCOUNT, 0, disable_force_settle).get_id();
   FC_ASSERT( bit_usd(db).is_market_issued() );
   {
      asset_update_bitasset_operation op;
      op.asset_to_update = bit_usd;
      op.issuer = bit_usd(db).issuer;
      op.new_options = bit_usd(db).bitasset_data(db).options;
      op.new_options.maximum_force_settlement_volume = 9000;
      trx.clear();
      trx.operations.push_back(op);
      PUSH_TX( db, trx, ~0 );
      trx.clear();
   }
   generate_block();

   create_short(shorter1_id(db), asset(1000, bit_usd), asset(1000));
   create_sell_order(nathan_id(db), asset(1000), asset(1000, bit_usd));
   create_short(shorter2_id(db), asset(2000, bit_usd), asset(1999));
   create_sell_order(nathan_id(db), asset(1999), asset(2000, bit_usd));
   create_short(shorter3_id(db), asset(3000, bit_usd), asset(2990));
   create_sell_order(nathan_id(db), asset(2990), asset(3000, bit_usd));
   BOOST_CHECK_EQUAL(get_balance(nathan_id, bit_usd), 6000);

   transfer(nathan_id(db), account_id_type()(db), db.get_balance(nathan_id, asset_id_type()));

   {
      asset_update_bitasset_operation uop;
      uop.issuer = bit_usd(db).issuer;
      uop.asset_to_update = bit_usd;
      uop.new_options = bit_usd(db).bitasset_data(db).options;
      uop.new_options.force_settlement_delay_sec = 100;
      uop.new_options.force_settlement_offset_percent = 100;
      trx.operations.push_back(uop);
   } {
      asset_update_feed_producers_operation uop;
      uop.asset_to_update = bit_usd;
      uop.issuer = bit_usd(db).issuer;
      uop.new_feed_producers = {nathan_id};
      trx.operations.push_back(uop);
   } {
      asset_publish_feed_operation pop;
      pop.asset_id = bit_usd;
      pop.publisher = nathan_id;
      price_feed feed;
      feed.settlement_price = price(asset(1),asset(1, bit_usd));
      feed.call_limit = price::min(0, bit_usd);
      pop.feed = feed;
      trx.operations.push_back(pop);
   }
   trx.sign(key_id_type(),private_key);
   PUSH_TX( db, trx );
   trx.clear();

   asset_settle_operation sop;
   sop.account = nathan_id;
   sop.amount = asset(50, bit_usd);
   trx.operations = {sop};
   //Force settlement is disabled; check that it fails
   BOOST_CHECK_THROW(PUSH_TX( db, trx, ~0 ), fc::exception);
   {
      //Enable force settlement
      asset_update_operation op;
      op.issuer = bit_usd(db).issuer;
      op.asset_to_update = bit_usd;
      op.new_options = bit_usd(db).options;
      op.new_options.flags &= ~disable_force_settle;
      trx.operations = {op};
      trx.sign(key_id_type(), private_key);
      PUSH_TX( db, trx );
      trx.operations = {sop};
   }
   REQUIRE_THROW_WITH_VALUE(sop, amount, asset(999999, bit_usd));
   trx.operations.back() = sop;
   trx.sign(key_id_type(),private_key);

   //Partially settle a call
   force_settlement_id_type settle_id = PUSH_TX( db, trx ).operation_results.front().get<object_id_type>();
   trx.clear();
   call_order_id_type call_id = db.get_index_type<call_order_index>().indices().get<by_collateral>().begin()->id;
   BOOST_CHECK_EQUAL(settle_id(db).balance.amount.value, 50);
   BOOST_CHECK_EQUAL(call_id(db).debt.value, 3000);
   BOOST_CHECK(settle_id(db).owner == nathan_id);

   generate_blocks(settle_id(db).settlement_date);
   BOOST_CHECK(db.find(settle_id) == nullptr);
   BOOST_CHECK_EQUAL(get_balance(nathan_id, asset_id_type()), 49);
   BOOST_CHECK_EQUAL(call_id(db).debt.value, 2950);

   {
      //Disable force settlement
      asset_update_operation op;
      op.issuer = bit_usd(db).issuer;
      op.asset_to_update = bit_usd;
      op.new_options = bit_usd(db).options;
      op.new_options.flags |= disable_force_settle;
      trx.operations.push_back(op);
      trx.set_expiration(db.head_block_id());
      trx.sign(key_id_type(), private_key);
      PUSH_TX( db, trx );
      //Check that force settlements were all canceled
      BOOST_CHECK(db.get_index_type<force_settlement_index>().indices().empty());
      BOOST_CHECK_EQUAL(get_balance(nathan_id, bit_usd), bit_usd(db).dynamic_data(db).current_supply.value);
   }
   } FC_LOG_AND_RETHROW()
    */
}

BOOST_AUTO_TEST_CASE( assert_op_test )
{
   try {
   // create some objects
   auto nathan_private_key = generate_private_key("nathan");
   auto dan_private_key = generate_private_key("dan");
   public_key_type nathan_public_key = nathan_private_key.get_public_key();
   public_key_type dan_public_key = dan_private_key.get_public_key();
   key_id_type nathan_key_id = register_key(nathan_public_key).id;
   key_id_type dan_key_id = register_key(dan_public_key).id;
   account_id_type nathan_id = create_account("nathan", nathan_key_id).id;

   assert_operation op;
   decltype( key_object::key_data ) lit_key = nathan_public_key;

   // nathan checks that his public key is equal to the given value.
   op.fee_paying_account = nathan_id;
   op.predicates = vector< vector< char > >();
   op.predicates.push_back(
      fc::raw::pack(
      predicate(
      pred_field_lit_cmp( nathan_key_id, 1, fc::raw::pack( lit_key ), opc_equal_to )
      )
      ) );
   trx.operations.push_back(op);
   trx.sign( nathan_key_id, nathan_private_key );
   PUSH_TX( db, trx );

   // nathan checks that his public key is not equal to the given value (fail)
   op.predicates.back() =
      fc::raw::pack(
      predicate(
      pred_field_lit_cmp( nathan_key_id, 1, fc::raw::pack( lit_key ), opc_not_equal_to )
      )
      );
   trx.operations.back() = op;
   trx.sign( nathan_key_id, nathan_private_key );
   BOOST_CHECK_THROW( PUSH_TX( db, trx ), fc::exception );
   } FC_LOG_AND_RETHROW()
}

// TODO:  Write linear VBO tests

BOOST_AUTO_TEST_SUITE_END()
