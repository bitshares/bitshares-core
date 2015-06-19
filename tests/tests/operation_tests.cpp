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

#include <graphene/chain/operations.hpp>

#include <graphene/chain/account_object.hpp>
#include <graphene/chain/asset_object.hpp>
#include <graphene/chain/database.hpp>
#include <graphene/chain/delegate_object.hpp>
#include <graphene/chain/key_object.hpp>
#include <graphene/chain/limit_order_object.hpp>
#include <graphene/chain/call_order_object.hpp>
#include <graphene/chain/vesting_balance_object.hpp>
#include <graphene/chain/withdraw_permission_object.hpp>
#include <graphene/chain/witness_object.hpp>

#include <fc/crypto/digest.hpp>

#include "../common/database_fixture.hpp"

using namespace graphene::chain;

BOOST_FIXTURE_TEST_SUITE( operation_tests, database_fixture )

BOOST_AUTO_TEST_CASE( feed_limit_logic_test )
{
   try {
      asset usd(100,1);
      asset core(100,0);
      price_feed feed;
      feed.settlement_price = usd / core;

      FC_ASSERT( usd * feed.settlement_price < usd * feed.maintenance_price() );
      FC_ASSERT( usd * feed.maintenance_price() < usd * feed.max_short_squeeze_price() );

   } catch (fc::exception& e) {
      edump((e.to_detail_string()));
      throw;
   }
}
BOOST_AUTO_TEST_CASE( call_order_update_test )
{
   try {
      ACTORS((dan)(sam));
      const auto& bitusd = create_bitasset("BITUSD");
      const auto& core   = asset_id_type()(db);

      transfer(genesis_account, dan_id, asset(10000000));
      update_feed_producers( bitusd, {sam.id} );

      price_feed current_feed; current_feed.settlement_price = bitusd.amount( 100 ) / core.amount(100);
      publish_feed( bitusd, sam, current_feed );

      FC_ASSERT( bitusd.bitasset_data(db).current_feed.settlement_price == current_feed.settlement_price );

      auto default_call_price = ~price::call_price( bitusd.amount(5000), asset(5000), 1750);

      BOOST_TEST_MESSAGE( "attempting to borrow using 2x collateral at 1:1 price now that there is a valid order" );
      borrow( dan, bitusd.amount(5000), asset(10000), default_call_price );
      BOOST_REQUIRE_EQUAL( get_balance( dan, bitusd ), 5000 );
      BOOST_REQUIRE_EQUAL( get_balance( dan, core ), 10000000 - 10000 );

      BOOST_TEST_MESSAGE( "covering 2500 usd and freeing 5000 core..." );
      cover( dan, bitusd.amount(2500), asset(5000), default_call_price );
      BOOST_REQUIRE_EQUAL( get_balance( dan, bitusd ), 2500 );
      BOOST_REQUIRE_EQUAL( get_balance( dan, core ), 10000000 - 10000 + 5000  );

      BOOST_TEST_MESSAGE( "verifying that attempting to cover the full amount without claiming the collateral fails" );
      BOOST_REQUIRE_THROW( cover( dan, bitusd.amount(2500), core.amount(0), default_call_price  ), fc::exception );

      cover( dan, bitusd.amount(2500), core.amount(5000), default_call_price );

      BOOST_REQUIRE_EQUAL( get_balance( dan, bitusd ), 0 );
      BOOST_REQUIRE_EQUAL( get_balance( dan, core ), 10000000  );

      borrow( dan, bitusd.amount(5000), asset(10000), default_call_price );
      BOOST_REQUIRE_EQUAL( get_balance( dan, bitusd ), 5000 );
      BOOST_REQUIRE_EQUAL( get_balance( dan, core ), 10000000 - 10000  );


      // test just increasing collateral
      BOOST_TEST_MESSAGE( "increasing collateral" );
      borrow( dan, bitusd.amount(0), asset(10000), default_call_price );

      BOOST_REQUIRE_EQUAL( get_balance( dan, bitusd ), 5000 );
      BOOST_REQUIRE_EQUAL( get_balance( dan, core ), 10000000 - 20000  );

      // test just decreasing debt
      BOOST_TEST_MESSAGE( "decreasing debt" );
      cover( dan, bitusd.amount(1000), asset(0), default_call_price );

      BOOST_REQUIRE_EQUAL( get_balance( dan, bitusd ), 4000 );
      BOOST_REQUIRE_EQUAL( get_balance( dan, core ), 10000000 - 20000  );

      BOOST_TEST_MESSAGE( "increasing debt without increasing collateral" );
      borrow( dan, bitusd.amount(1000), asset(0), default_call_price );

      BOOST_REQUIRE_EQUAL( get_balance( dan, bitusd ), 5000 );
      BOOST_REQUIRE_EQUAL( get_balance( dan, core ), 10000000 - 20000  );

      BOOST_TEST_MESSAGE( "increasing debt without increasing collateral again" );
      BOOST_REQUIRE_THROW( borrow( dan, bitusd.amount(80000), asset(0), default_call_price ), fc::exception );
      BOOST_TEST_MESSAGE( "attempting to claim all collateral without paying off debt" );
      BOOST_REQUIRE_THROW( cover( dan, bitusd.amount(0), asset(20000), default_call_price ), fc::exception );
      BOOST_TEST_MESSAGE( "attempting reduce collateral without paying off any debt" );
      cover( dan, bitusd.amount(0), asset(1000), default_call_price );

      BOOST_TEST_MESSAGE( "attempting change call price without changing debt/collateral ratio" );
      default_call_price = ~price::call_price( bitusd.amount(100), asset(50), 1750);
      cover( dan, bitusd.amount(0), asset(0), default_call_price );

      BOOST_TEST_MESSAGE( "attempting change call price to be below minimum for debt/collateral ratio" );
      default_call_price = ~price::call_price( bitusd.amount(100), asset(500), 1750);
      BOOST_REQUIRE_THROW( cover( dan, bitusd.amount(0), asset(0), default_call_price ), fc::exception );

   } catch (fc::exception& e) {
      edump((e.to_detail_string()));
      throw;
   }
}

/**
 *  This test sets up a situation where a margin call will be executed and ensures that
 *  it is properly filled.
 *
 *  A margin call can happen in the following situation:
 *  0. there exists a bid above the mas short squeeze price
 *  1. highest bid is lower than the call price of an order
 *  2. the asset is not a prediction market
 *  3. there is a valid price feed
 *
 *  This test creates two scenarios:
 *  a) when the bids are above the short squeese limit (should execute)
 *  b) when the bids are below the short squeeze limit (should not execute)
 */
BOOST_AUTO_TEST_CASE( margin_call_limit_test )
{ try {
      ACTORS((buyer)(seller)(borrower)(borrower2)(feedproducer));

      const auto& bitusd = create_bitasset("BITUSD");
      const auto& core   = asset_id_type()(db);

      int64_t init_balance(1000000);

      transfer(genesis_account, buyer_id, asset(init_balance));
      transfer(genesis_account, borrower_id, asset(init_balance));
      transfer(genesis_account, borrower2_id, asset(init_balance));
      update_feed_producers( bitusd, {feedproducer.id} );

      price_feed current_feed;
      current_feed.settlement_price = bitusd.amount( 100 ) / core.amount(100);
      auto default_call_price = ~price::call_price( bitusd.amount(100), asset(100), 1750);

      // starting out with price 1:1
      publish_feed( bitusd, feedproducer, current_feed );

      // start out with 2:1 collateral
      borrow( borrower, bitusd.amount(1000), asset(2000), default_call_price );
      borrow( borrower2, bitusd.amount(1000), asset(4000), default_call_price );

      BOOST_REQUIRE_EQUAL( get_balance( borrower, bitusd ), 1000 );
      BOOST_REQUIRE_EQUAL( get_balance( borrower2, bitusd ), 1000 );
      BOOST_REQUIRE_EQUAL( get_balance( borrower , core ), init_balance - 2000 );
      BOOST_REQUIRE_EQUAL( get_balance( borrower2, core ), init_balance - 4000 );

      // this should trigger margin call that is below the call limit, but above the
      // protection threshold.
      BOOST_TEST_MESSAGE( "Creating a margin call that is NOT protected by the max short squeeze price" );
      auto order = create_sell_order( borrower2, bitusd.amount(1000), core.amount(1400) );
      BOOST_REQUIRE( order == nullptr );

      BOOST_REQUIRE_EQUAL( get_balance( borrower2, core ), init_balance - 4000 + 1400 );
      BOOST_REQUIRE_EQUAL( get_balance( borrower2, bitusd ), 0 );

      BOOST_REQUIRE_EQUAL( get_balance( borrower, core ), init_balance - 2000 + 600 );
      BOOST_REQUIRE_EQUAL( get_balance( borrower, bitusd ), 1000 );


      BOOST_TEST_MESSAGE( "Creating a margin call that is protected by the max short squeeze price" );
      borrow( borrower, bitusd.amount(1000), asset(2000), default_call_price );
      borrow( borrower2, bitusd.amount(1000), asset(4000), default_call_price );

      // this should trigger margin call without protection from the price feed.
      order = create_sell_order( borrower2, bitusd.amount(1000), core.amount(1800) );
      BOOST_REQUIRE( order != nullptr );
   } catch( const fc::exception& e) {
      edump((e.to_detail_string()));
      throw;
   }
}

/**
 *  This test sets up the minimum condition for a black swan to occur but does
 *  not test the full range of cases that may be possible during a black swan.
 */
BOOST_AUTO_TEST_CASE( black_swan )
{ try {
      ACTORS((buyer)(seller)(borrower)(borrower2)(feedproducer));

      const auto& bitusd = create_bitasset("BITUSD");
      const auto& core   = asset_id_type()(db);

      int64_t init_balance(1000000);

      transfer(genesis_account, buyer_id, asset(init_balance));
      transfer(genesis_account, borrower_id, asset(init_balance));
      transfer(genesis_account, borrower2_id, asset(init_balance));
      update_feed_producers( bitusd, {feedproducer.id} );

      price_feed current_feed;
      current_feed.settlement_price = bitusd.amount( 100 ) / core.amount(100);
      auto default_call_price = ~price::call_price( bitusd.amount(100), asset(100), 1750);

      // starting out with price 1:1
      publish_feed( bitusd, feedproducer, current_feed );

      // start out with 2:1 collateral
      borrow( borrower, bitusd.amount(1000), asset(2000), default_call_price );
      borrow( borrower2, bitusd.amount(1000), asset(4000), default_call_price );

      BOOST_REQUIRE_EQUAL( get_balance( borrower, bitusd ), 1000 );
      BOOST_REQUIRE_EQUAL( get_balance( borrower2, bitusd ), 1000 );
      BOOST_REQUIRE_EQUAL( get_balance( borrower , core ), init_balance - 2000 );
      BOOST_REQUIRE_EQUAL( get_balance( borrower2, core ), init_balance - 4000 );

      current_feed.settlement_price = bitusd.amount( 100 ) / core.amount(200);
      publish_feed( bitusd, feedproducer, current_feed );

      /// this sell order is designed to trigger a black swan
      auto order = create_sell_order( borrower2, bitusd.amount(1000), core.amount(3000) );

      FC_ASSERT( bitusd.bitasset_data(db).has_settlement() );
      wdump(( bitusd.bitasset_data(db) ));

      force_settle( borrower, bitusd.amount(100) );
  
      BOOST_TEST_MESSAGE( "Verify that we cannot borrow after black swan" );
      BOOST_REQUIRE_THROW( borrow( borrower, bitusd.amount(1000), asset(2000), default_call_price ), fc::exception );
   } catch( const fc::exception& e) {
      edump((e.to_detail_string()));
      throw;
   }
}

BOOST_AUTO_TEST_CASE( prediction_market )
{ try {
      ACTORS((judge)(dan)(nathan));

      const auto& pmark = create_prediction_market("PMARK", judge_id);
      const auto& core  = asset_id_type()(db);

      int64_t init_balance(1000000);
      transfer(genesis_account, judge_id, asset(init_balance));
      transfer(genesis_account, dan_id, asset(init_balance));
      transfer(genesis_account, nathan_id, asset(init_balance));

      auto default_call_price = ~price::call_price( pmark.amount(100), asset(100), 1750);

      BOOST_TEST_MESSAGE( "Require throw for mismatch collateral amounts" );
      BOOST_REQUIRE_THROW( borrow( dan, pmark.amount(1000), asset(2000), default_call_price ), fc::exception );

      BOOST_TEST_MESSAGE( "Open position with equal collateral" );
      borrow( dan, pmark.amount(1000), asset(1000), default_call_price );

      BOOST_TEST_MESSAGE( "Cover position with unequal asset should fail." );
      BOOST_REQUIRE_THROW( cover( dan, pmark.amount(500), asset(1000), default_call_price ), fc::exception );

      BOOST_TEST_MESSAGE( "Cover half of position with equal ammounts" );
      cover( dan, pmark.amount(500), asset(500), default_call_price );

      BOOST_TEST_MESSAGE( "Verify that forced settlment fails before global settlement" );
      BOOST_REQUIRE_THROW( force_settle( dan, pmark.amount(100) ), fc::exception );
      
      BOOST_TEST_MESSAGE( "Shouldn't be allowed to force settle at more than 1 collateral per debt" );
      BOOST_REQUIRE_THROW( force_global_settle( pmark, pmark.amount(100) / core.amount(105) ), fc::exception );

      force_global_settle( pmark, pmark.amount(100) / core.amount(95) );

      BOOST_TEST_MESSAGE( "Verify that forced settlment succeedes after global settlement" );
      force_settle( dan, pmark.amount(100) );

   } catch( const fc::exception& e) {
      edump((e.to_detail_string()));
      throw;
   }
}


BOOST_AUTO_TEST_CASE( create_account_test )
{
   try {
      trx.operations.push_back(make_account());
      account_create_operation op = trx.operations.back().get<account_create_operation>();

      REQUIRE_THROW_WITH_VALUE(op, registrar, account_id_type(9999999));
      REQUIRE_THROW_WITH_VALUE(op, fee, asset(-1));
      REQUIRE_THROW_WITH_VALUE(op, name, "!");
      REQUIRE_THROW_WITH_VALUE(op, name, "Sam");
      REQUIRE_THROW_WITH_VALUE(op, name, "saM");
      REQUIRE_THROW_WITH_VALUE(op, name, "sAm");
      REQUIRE_THROW_WITH_VALUE(op, name, "6j");
      REQUIRE_THROW_WITH_VALUE(op, name, "j-");
      REQUIRE_THROW_WITH_VALUE(op, name, "-j");
      REQUIRE_THROW_WITH_VALUE(op, name, "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
      REQUIRE_THROW_WITH_VALUE(op, name, "aaaa.");
      REQUIRE_THROW_WITH_VALUE(op, name, ".aaaa");
      REQUIRE_THROW_WITH_VALUE(op, options.voting_account, account_id_type(999999999));
      REQUIRE_THROW_WITH_VALUE(op, options.memo_key, key_id_type(999999999));

      auto auth_bak = op.owner;
      op.owner.add_authority(account_id_type(9999999999), 10);
      trx.operations.back() = op;
      op.owner = auth_bak;
      BOOST_REQUIRE_THROW(PUSH_TX( db, trx, ~0 ), fc::exception);
      op.owner = auth_bak;
      op.owner.add_authority(key_id_type(9999999999), 10);
      trx.operations.back() = op;
      BOOST_REQUIRE_THROW(PUSH_TX( db, trx, ~0 ), fc::exception);
      op.owner = auth_bak;

      trx.operations.back() = op;
      trx.sign(key_id_type(), delegate_priv_key);
      trx.validate();
      PUSH_TX( db, trx, ~0 );

      const account_object& nathan_account = *db.get_index_type<account_index>().indices().get<by_name>().find("nathan");
      BOOST_CHECK(nathan_account.id.space() == protocol_ids);
      BOOST_CHECK(nathan_account.id.type() == account_object_type);
      BOOST_CHECK(nathan_account.name == "nathan");

      BOOST_REQUIRE(nathan_account.owner.auths.size() == 1);
      BOOST_CHECK(nathan_account.owner.auths.at(genesis_key) == 123);
      BOOST_REQUIRE(nathan_account.active.auths.size() == 1);
      BOOST_CHECK(nathan_account.active.auths.at(genesis_key) == 321);
      BOOST_CHECK(nathan_account.options.voting_account == account_id_type());
      BOOST_CHECK(nathan_account.options.memo_key == genesis_key);

      const account_statistics_object& statistics = nathan_account.statistics(db);
      BOOST_CHECK(statistics.id.space() == implementation_ids);
      BOOST_CHECK(statistics.id.type() == impl_account_statistics_object_type);
   } catch (fc::exception& e) {
      edump((e.to_detail_string()));
      throw;
   }
}

BOOST_AUTO_TEST_CASE( child_account )
{
   try {
      INVOKE(create_account_test);
      fc::ecc::private_key child_private_key = fc::ecc::private_key::generate();
      fc::ecc::private_key nathan_private_key = fc::ecc::private_key::generate();
      const auto& child_key = register_key(child_private_key.get_public_key());
      const auto& nathan_key = register_key(nathan_private_key.get_public_key());
      const account_object& nathan = get_account("nathan");
      const account_object& root = create_account("root");
      upgrade_to_lifetime_member(root);

      skip_key_index_test = true;
      db.modify(nathan, [nathan_key](account_object& a) {
         a.owner = authority(1, nathan_key.get_id(), 1);
         a.active = authority(1, nathan_key.get_id(), 1);
      });

      BOOST_CHECK(nathan.active.get_keys() == vector<key_id_type>{nathan_key.get_id()});

      auto op = make_account("nathan/child");
      op.registrar = root.id;
      op.owner = authority(1, child_key.get_id(), 1);
      op.active = authority(1, child_key.get_id(), 1);
      trx.operations.emplace_back(op);
      trx.sign({}, delegate_priv_key);

      BOOST_REQUIRE_THROW(PUSH_TX( db, trx ), fc::exception);
      sign(trx, nathan_key.id,nathan_private_key);
      BOOST_REQUIRE_THROW(PUSH_TX( db, trx ), fc::exception);
      trx.signatures.clear();
      op.owner = authority(1, account_id_type(nathan.id), 1);
      trx.operations = {op};
      trx.sign({}, delegate_priv_key);
      trx.sign(nathan_key.id, nathan_private_key);
      db.push_transaction(trx);

      BOOST_CHECK( get_account("nathan/child").active.auths == op.active.auths );
   } catch (fc::exception& e) {
      edump((e.to_detail_string()));
      throw;
   }
}

BOOST_AUTO_TEST_CASE( update_account )
{
   try {
      INVOKE(create_account_test);
      const account_object& nathan = get_account("nathan");
      const fc::ecc::private_key nathan_new_key = fc::ecc::private_key::generate();
      const key_id_type key_id = db.get_index<key_object>().get_next_id();
      const auto& active_delegates = db.get_global_properties().active_delegates;

      transfer(account_id_type()(db), nathan, asset(30000));

      trx.operations.emplace_back(key_create_operation({asset(),nathan.id,address(nathan_new_key.get_public_key())}));
      PUSH_TX( db, trx, ~0 );

      account_update_operation op;
      op.account = nathan.id;
      op.owner = authority(2, key_id, 1, key_id_type(), 1);
      op.active = authority(2, key_id, 1, key_id_type(), 1);
      op.new_options = nathan.options;
      op.new_options->votes = flat_set<vote_id_type>({active_delegates[0](db).vote_id, active_delegates[5](db).vote_id});
      op.new_options->num_committee = 2;
      trx.operations.back() = op;
      PUSH_TX( db, trx, ~0 );

      BOOST_CHECK(nathan.options.memo_key == key_id_type());
      BOOST_CHECK(nathan.active.weight_threshold == 2);
      BOOST_CHECK(nathan.active.auths.size() == 2);
      BOOST_CHECK(nathan.active.auths.at(key_id) == 1);
      BOOST_CHECK(nathan.active.auths.at(key_id_type()) == 1);
      BOOST_CHECK(nathan.owner.weight_threshold == 2);
      BOOST_CHECK(nathan.owner.auths.size() == 2);
      BOOST_CHECK(nathan.owner.auths.at(key_id) == 1);
      BOOST_CHECK(nathan.owner.auths.at(key_id_type()) == 1);
      BOOST_CHECK(nathan.options.votes.size() == 2);

      /** these votes are no longer tallied in real time
      BOOST_CHECK(active_delegates[0](db).vote(db).total_votes == 30000);
      BOOST_CHECK(active_delegates[1](db).vote(db).total_votes == 0);
      BOOST_CHECK(active_delegates[4](db).vote(db).total_votes == 0);
      BOOST_CHECK(active_delegates[5](db).vote(db).total_votes == 30000);
      BOOST_CHECK(active_delegates[6](db).vote(db).total_votes == 0);
      */

      transfer(account_id_type()(db), nathan, asset(3000000));

      enable_fees();
      {
         account_upgrade_operation op;
         op.account_to_upgrade = nathan.id;
         op.upgrade_to_lifetime_member = true;
         op.fee = op.calculate_fee(db.get_global_properties().parameters.current_fees);
         trx.operations = {op};
         PUSH_TX( db, trx, ~0 );
      }

      BOOST_CHECK( nathan.is_lifetime_member() );
   } catch (fc::exception& e) {
      edump((e.to_detail_string()));
      throw;
   }
}

BOOST_AUTO_TEST_CASE( transfer_core_asset )
{
   try {
      INVOKE(create_account_test);

      account_id_type genesis_account;
      asset genesis_balance = db.get_balance(account_id_type(), asset_id_type());

      const account_object& nathan_account = *db.get_index_type<account_index>().indices().get<by_name>().find("nathan");
      trx.operations.push_back(transfer_operation({asset(),genesis_account,
                                                   nathan_account.id,
                                                   asset(10000),
                                                   memo_data()
                                                  }));
      trx.visit( operation_set_fee( db.current_fee_schedule() ) );

      asset fee = trx.operations.front().get<transfer_operation>().fee;
      trx.validate();
      PUSH_TX( db, trx, ~0 );

      BOOST_CHECK_EQUAL(get_balance(account_id_type()(db), asset_id_type()(db)),
                        (genesis_balance.amount - 10000 - fee.amount).value);
      genesis_balance = db.get_balance(account_id_type(), asset_id_type());

      BOOST_CHECK_EQUAL(get_balance(nathan_account, asset_id_type()(db)), 10000);

      trx = signed_transaction();
      trx.operations.push_back(transfer_operation({asset(),
                                                   nathan_account.id,
                                                   genesis_account,
                                                   asset(2000),
                                                   memo_data()
                                                  }));
      trx.visit( operation_set_fee( db.current_fee_schedule() ) );

      fee = trx.operations.front().get<transfer_operation>().fee;
      trx.validate();
      PUSH_TX( db, trx, ~0 );

      BOOST_CHECK_EQUAL(get_balance(nathan_account, asset_id_type()(db)), 8000 - fee.amount.value);
      BOOST_CHECK_EQUAL(get_balance(account_id_type()(db), asset_id_type()(db)), genesis_balance.amount.value + 2000);

   } catch (fc::exception& e) {
      edump((e.to_detail_string()));
      throw;
   }
}

BOOST_AUTO_TEST_CASE( create_delegate )
{
   try {
      delegate_create_operation op;
      op.delegate_account = account_id_type();
      op.fee = asset();
      trx.operations.push_back(op);

      REQUIRE_THROW_WITH_VALUE(op, delegate_account, account_id_type(99999999));
      REQUIRE_THROW_WITH_VALUE(op, fee, asset(-600));
      trx.operations.back() = op;

      delegate_id_type delegate_id = db.get_index_type<primary_index<simple_index<delegate_object>>>().get_next_id();
      PUSH_TX( db, trx, ~0 );
      const delegate_object& d = delegate_id(db);

      BOOST_CHECK(d.delegate_account == account_id_type());
   } catch (fc::exception& e) {
      edump((e.to_detail_string()));
      throw;
   }
}

BOOST_AUTO_TEST_CASE( create_mia )
{
   try {
      const asset_object& bitusd = create_bitasset( "BITUSD" );
      BOOST_CHECK(bitusd.symbol == "BITUSD");
      BOOST_CHECK(bitusd.bitasset_data(db).options.short_backing_asset == asset_id_type());
      BOOST_CHECK(bitusd.dynamic_asset_data_id(db).current_supply == 0);
      BOOST_REQUIRE_THROW( create_bitasset("BITUSD"), fc::exception);
   } catch ( const fc::exception& e ) {
      elog( "${e}", ("e", e.to_detail_string() ) );
      throw;
   }
}

BOOST_AUTO_TEST_CASE( update_mia )
{
   try {
      INVOKE(create_mia);
      generate_block();
      const asset_object& bit_usd = get_asset("BITUSD");

      asset_update_operation op;
      op.issuer = bit_usd.issuer;
      op.asset_to_update = bit_usd.id;
      op.new_options = bit_usd.options;
      trx.operations.emplace_back(op);

      trx.operations.back() = op;
      PUSH_TX( db, trx, ~0 );
      std::swap(op.new_options.flags, op.new_options.issuer_permissions);
      op.new_issuer = account_id_type();
      trx.operations.back() = op;
      PUSH_TX( db, trx, ~0 );

      {
         asset_publish_feed_operation pop;
         pop.asset_id = bit_usd.get_id();
         pop.publisher = get_account("init0").get_id();
         price_feed feed;
         feed.settlement_price = price(bit_usd.amount(5), bit_usd.amount(5));
         REQUIRE_THROW_WITH_VALUE(pop, feed, feed);
         feed.settlement_price = ~price(bit_usd.amount(5), asset(5));
         REQUIRE_THROW_WITH_VALUE(pop, feed, feed);
         feed.settlement_price = price(bit_usd.amount(5), asset(5));
         pop.feed = feed;
         REQUIRE_THROW_WITH_VALUE(pop, feed.maintenance_collateral_ratio, 0);
         trx.operations.back() = pop;
         PUSH_TX( db, trx, ~0 );
      }

      trx.operations.clear();
      auto nathan = create_account("nathan");
      op.issuer = account_id_type();
      op.new_issuer = nathan.id;
      trx.operations.emplace_back(op);
      PUSH_TX( db, trx, ~0 );
      BOOST_CHECK(bit_usd.issuer == nathan.id);

      op.issuer = nathan.id;
      op.new_issuer = account_id_type();
      trx.operations.back() = op;
      PUSH_TX( db, trx, ~0 );
      BOOST_CHECK(bit_usd.issuer == account_id_type());
   } catch ( const fc::exception& e ) {
      elog( "${e}", ("e", e.to_detail_string() ) );
      throw;
   }
}


BOOST_AUTO_TEST_CASE( create_uia )
{
   try {
      asset_id_type test_asset_id = db.get_index<asset_object>().get_next_id();
      asset_create_operation creator;
      creator.issuer = account_id_type();
      creator.fee = asset();
      creator.symbol = "TEST";
      creator.common_options.max_supply = 100000000;
      creator.precision = 2;
      creator.common_options.market_fee_percent = GRAPHENE_MAX_MARKET_FEE_PERCENT/100; /*1%*/
      creator.common_options.issuer_permissions = UIA_ASSET_ISSUER_PERMISSION_MASK;
      creator.common_options.flags = charge_market_fee;
      creator.common_options.core_exchange_rate = price({asset(2),asset(1,1)});
      trx.operations.push_back(std::move(creator));
      PUSH_TX( db, trx, ~0 );

      const asset_object& test_asset = test_asset_id(db);
      BOOST_CHECK(test_asset.symbol == "TEST");
      BOOST_CHECK(asset(1, test_asset_id) * test_asset.options.core_exchange_rate == asset(2));
      BOOST_CHECK(!test_asset.enforce_white_list());
      BOOST_CHECK(test_asset.options.max_supply == 100000000);
      BOOST_CHECK(!test_asset.bitasset_data_id.valid());
      BOOST_CHECK(test_asset.options.market_fee_percent == GRAPHENE_MAX_MARKET_FEE_PERCENT/100);
      BOOST_REQUIRE_THROW(PUSH_TX( db, trx, ~0 ), fc::exception);

      const asset_dynamic_data_object& test_asset_dynamic_data = test_asset.dynamic_asset_data_id(db);
      BOOST_CHECK(test_asset_dynamic_data.current_supply == 0);
      BOOST_CHECK(test_asset_dynamic_data.accumulated_fees == 0);
      BOOST_CHECK(test_asset_dynamic_data.fee_pool == 0);

      auto op = trx.operations.back().get<asset_create_operation>();
      op.symbol = "TESTFAIL";
      REQUIRE_THROW_WITH_VALUE(op, issuer, account_id_type(99999999));
      REQUIRE_THROW_WITH_VALUE(op, common_options.max_supply, -1);
      REQUIRE_THROW_WITH_VALUE(op, common_options.max_supply, 0);
      REQUIRE_THROW_WITH_VALUE(op, symbol, "A");
      REQUIRE_THROW_WITH_VALUE(op, symbol, "qqq");
      REQUIRE_THROW_WITH_VALUE(op, symbol, "11");
      REQUIRE_THROW_WITH_VALUE(op, symbol, ".AAA");
      REQUIRE_THROW_WITH_VALUE(op, symbol, "AAA.");
      REQUIRE_THROW_WITH_VALUE(op, symbol, "AB CD");
      REQUIRE_THROW_WITH_VALUE(op, symbol, "ABCDEFGHIJKLMNOPQRSTUVWXYZ");
      REQUIRE_THROW_WITH_VALUE(op, common_options.core_exchange_rate, price({asset(-100), asset(1)}));
      REQUIRE_THROW_WITH_VALUE(op, common_options.core_exchange_rate, price({asset(100),asset(-1)}));
   } catch(fc::exception& e) {
      edump((e.to_detail_string()));
      throw;
   }
}

BOOST_AUTO_TEST_CASE( update_uia )
{
   using namespace graphene;
   try {
      INVOKE(create_uia);
      const auto& test = get_asset("TEST");
      const auto& nathan = create_account("nathan");

      asset_update_operation op;
      op.issuer = test.issuer;
      op.asset_to_update = test.id;
      op.new_options = test.options;

      trx.operations.push_back(op);

      //Cannot change issuer to same as before
      REQUIRE_THROW_WITH_VALUE(op, new_issuer, test.issuer);
      //Cannot convert to an MIA
      REQUIRE_THROW_WITH_VALUE(op, new_options.issuer_permissions, ASSET_ISSUER_PERMISSION_MASK);
      REQUIRE_THROW_WITH_VALUE(op, new_options.core_exchange_rate, price(asset(5), asset(5)));

      op.new_options.core_exchange_rate = price(asset(3), test.amount(5));
      trx.operations.back() = op;
      PUSH_TX( db, trx, ~0 );
      REQUIRE_THROW_WITH_VALUE(op, new_options.core_exchange_rate, price());
      op.new_options.core_exchange_rate = test.options.core_exchange_rate;
      op.new_issuer = nathan.id;
      trx.operations.back() = op;
      PUSH_TX( db, trx, ~0 );
      op.issuer = nathan.id;
      op.new_issuer.reset();
      op.new_options.flags = transfer_restricted | white_list;
      trx.operations.back() = op;
      PUSH_TX( db, trx, ~0 );
      REQUIRE_THROW_WITH_VALUE(op, new_options.issuer_permissions, test.options.issuer_permissions & ~white_list);
      op.new_options.issuer_permissions = test.options.issuer_permissions & ~white_list;
      op.new_options.flags = 0;
      trx.operations.back() = op;
      PUSH_TX( db, trx, ~0 );
      op.new_options.issuer_permissions = test.options.issuer_permissions;
      op.new_options.flags = test.options.flags;
      BOOST_CHECK(!(test.options.issuer_permissions & white_list));
      REQUIRE_THROW_WITH_VALUE(op, new_options.issuer_permissions, UIA_ASSET_ISSUER_PERMISSION_MASK);
      REQUIRE_THROW_WITH_VALUE(op, new_options.flags, white_list);
      op.new_issuer = account_id_type();
      trx.operations.back() = op;
      PUSH_TX( db, trx, ~0 );
      op.issuer = account_id_type();
      BOOST_REQUIRE_THROW(PUSH_TX( db, trx, ~0 ), fc::exception);
      op.new_issuer.reset();
   } catch(fc::exception& e) {
      edump((e.to_detail_string()));
      throw;
   }
}

BOOST_AUTO_TEST_CASE( issue_uia )
{
   try {
      INVOKE(create_uia);
      INVOKE(create_account_test);

      const asset_object& test_asset = *db.get_index_type<asset_index>().indices().get<by_symbol>().find("TEST");
      const account_object& nathan_account = *db.get_index_type<account_index>().indices().get<by_name>().find("nathan");

      asset_issue_operation op({asset(),test_asset.issuer, test_asset.amount(5000000),  nathan_account.id});
      trx.operations.push_back(op);

      REQUIRE_THROW_WITH_VALUE(op, asset_to_issue, asset(200));
      REQUIRE_THROW_WITH_VALUE(op, fee, asset(-1));
      REQUIRE_THROW_WITH_VALUE(op, issue_to_account, account_id_type(999999999));

      trx.operations.back() = op;
      PUSH_TX( db, trx, ~0 );

      const asset_dynamic_data_object& test_dynamic_data = test_asset.dynamic_asset_data_id(db);
      BOOST_CHECK_EQUAL(get_balance(nathan_account, test_asset), 5000000);
      BOOST_CHECK(test_dynamic_data.current_supply == 5000000);
      BOOST_CHECK(test_dynamic_data.accumulated_fees == 0);
      BOOST_CHECK(test_dynamic_data.fee_pool == 0);

      PUSH_TX( db, trx, ~0 );

      BOOST_CHECK_EQUAL(get_balance(nathan_account, test_asset), 10000000);
      BOOST_CHECK(test_dynamic_data.current_supply == 10000000);
      BOOST_CHECK(test_dynamic_data.accumulated_fees == 0);
      BOOST_CHECK(test_dynamic_data.fee_pool == 0);
   } catch(fc::exception& e) {
      edump((e.to_detail_string()));
      throw;
   }
}

BOOST_AUTO_TEST_CASE( transfer_uia )
{
   try {
      INVOKE(issue_uia);

      const asset_object& uia = *db.get_index_type<asset_index>().indices().get<by_symbol>().find("TEST");
      const account_object& nathan = *db.get_index_type<account_index>().indices().get<by_name>().find("nathan");
      const account_object& genesis = account_id_type()(db);

      BOOST_CHECK_EQUAL(get_balance(nathan, uia), 10000000);
      trx.operations.push_back(transfer_operation({asset(),nathan.id, genesis.id, uia.amount(5000)}));
      PUSH_TX( db, trx, ~0 );
      BOOST_CHECK_EQUAL(get_balance(nathan, uia), 10000000 - 5000);
      BOOST_CHECK_EQUAL(get_balance(genesis, uia), 5000);

      PUSH_TX( db, trx, ~0 );
      BOOST_CHECK_EQUAL(get_balance(nathan, uia), 10000000 - 10000);
      BOOST_CHECK_EQUAL(get_balance(genesis, uia), 10000);
   } catch(fc::exception& e) {
      edump((e.to_detail_string()));
      throw;
   }
}


BOOST_AUTO_TEST_CASE( create_buy_uia_multiple_match_new )
{ try {
   INVOKE( issue_uia );
   const asset_object&   core_asset     = get_asset( "TEST" );
   const asset_object&   test_asset     = get_asset( GRAPHENE_SYMBOL );
   const account_object& nathan_account = get_account( "nathan" );
   const account_object& buyer_account  = create_account( "buyer" );
   const account_object& seller_account = create_account( "seller" );

   transfer( genesis_account(db), buyer_account, test_asset.amount( 10000 ) );
   transfer( nathan_account, seller_account, core_asset.amount(10000) );

   BOOST_CHECK_EQUAL( get_balance( buyer_account, test_asset ), 10000 );

   limit_order_id_type first_id  = create_sell_order( buyer_account, test_asset.amount(100), core_asset.amount(100) )->id;
   limit_order_id_type second_id = create_sell_order( buyer_account, test_asset.amount(100), core_asset.amount(200) )->id;
   limit_order_id_type third_id  = create_sell_order( buyer_account, test_asset.amount(100), core_asset.amount(300) )->id;

   BOOST_CHECK_EQUAL( get_balance( buyer_account, test_asset ), 9700 );

   //print_market( "", "" );
   auto unmatched = create_sell_order( seller_account, core_asset.amount(300), test_asset.amount(150) );
   //print_market( "", "" );
   BOOST_CHECK( !db.find( first_id ) );
   BOOST_CHECK( !db.find( second_id ) );
   BOOST_CHECK( db.find( third_id ) );
   if( unmatched ) wdump((*unmatched));
   BOOST_CHECK( !unmatched );

   BOOST_CHECK_EQUAL( get_balance( seller_account, test_asset ), 200 );
   BOOST_CHECK_EQUAL( get_balance( buyer_account, core_asset ), 297 );
   BOOST_CHECK_EQUAL( core_asset.dynamic_asset_data_id(db).accumulated_fees.value , 3 );
 }
 catch ( const fc::exception& e )
 {
    elog( "${e}", ("e", e.to_detail_string() ) );
    throw;
 }
}

BOOST_AUTO_TEST_CASE( create_buy_exact_match_uia )
{ try {
   INVOKE( issue_uia );
   const asset_object&   test_asset     = get_asset( "TEST" );
   const asset_object&   core_asset     = get_asset( GRAPHENE_SYMBOL );
   const account_object& nathan_account = get_account( "nathan" );
   const account_object& buyer_account  = create_account( "buyer" );
   const account_object& seller_account = create_account( "seller" );

   transfer( genesis_account(db), seller_account, asset( 10000 ) );
   transfer( nathan_account, buyer_account, test_asset.amount(10000) );

   BOOST_CHECK_EQUAL( get_balance( buyer_account, test_asset ), 10000 );

   limit_order_id_type first_id  = create_sell_order( buyer_account, test_asset.amount(100), core_asset.amount(100) )->id;
   limit_order_id_type second_id = create_sell_order( buyer_account, test_asset.amount(100), core_asset.amount(200) )->id;
   limit_order_id_type third_id  = create_sell_order( buyer_account, test_asset.amount(100), core_asset.amount(300) )->id;

   BOOST_CHECK_EQUAL( get_balance( buyer_account, test_asset ), 9700 );

   //print_market( "", "" );
   auto unmatched = create_sell_order( seller_account, core_asset.amount(100), test_asset.amount(100) );
   //print_market( "", "" );
   BOOST_CHECK( !db.find( first_id ) );
   BOOST_CHECK( db.find( second_id ) );
   BOOST_CHECK( db.find( third_id ) );
   if( unmatched ) wdump((*unmatched));
   BOOST_CHECK( !unmatched );

   BOOST_CHECK_EQUAL( get_balance( seller_account, test_asset ), 99 );
   BOOST_CHECK_EQUAL( get_balance( buyer_account, core_asset ), 100 );
   BOOST_CHECK_EQUAL( test_asset.dynamic_asset_data_id(db).accumulated_fees.value , 1 );
 }
 catch ( const fc::exception& e )
 {
    elog( "${e}", ("e", e.to_detail_string() ) );
    throw;
 }
}


BOOST_AUTO_TEST_CASE( create_buy_uia_multiple_match_new_reverse )
{ try {
   INVOKE( issue_uia );
   const asset_object&   test_asset     = get_asset( "TEST" );
   const asset_object&   core_asset     = get_asset( GRAPHENE_SYMBOL );
   const account_object& nathan_account = get_account( "nathan" );
   const account_object& buyer_account  = create_account( "buyer" );
   const account_object& seller_account = create_account( "seller" );

   transfer( genesis_account(db), seller_account, asset( 10000 ) );
   transfer( nathan_account, buyer_account, test_asset.amount(10000),test_asset.amount(0) );

   BOOST_CHECK_EQUAL( get_balance( buyer_account, test_asset ), 10000 );

   limit_order_id_type first_id  = create_sell_order( buyer_account, test_asset.amount(100), core_asset.amount(100) )->id;
   limit_order_id_type second_id = create_sell_order( buyer_account, test_asset.amount(100), core_asset.amount(200) )->id;
   limit_order_id_type third_id  = create_sell_order( buyer_account, test_asset.amount(100), core_asset.amount(300) )->id;

   BOOST_CHECK_EQUAL( get_balance( buyer_account, test_asset ), 9700 );

   //print_market( "", "" );
   auto unmatched = create_sell_order( seller_account, core_asset.amount(300), test_asset.amount(150) );
   //print_market( "", "" );
   BOOST_CHECK( !db.find( first_id ) );
   BOOST_CHECK( !db.find( second_id ) );
   BOOST_CHECK( db.find( third_id ) );
   if( unmatched ) wdump((*unmatched));
   BOOST_CHECK( !unmatched );

   BOOST_CHECK_EQUAL( get_balance( seller_account, test_asset ), 198 );
   BOOST_CHECK_EQUAL( get_balance( buyer_account, core_asset ), 300 );
   BOOST_CHECK_EQUAL( test_asset.dynamic_asset_data_id(db).accumulated_fees.value , 2 );
 }
 catch ( const fc::exception& e )
 {
    elog( "${e}", ("e", e.to_detail_string() ) );
    throw;
 }
}

BOOST_AUTO_TEST_CASE( create_buy_uia_multiple_match_new_reverse_fract )
{ try {
   INVOKE( issue_uia );
   const asset_object&   test_asset     = get_asset( "TEST" );
   const asset_object&   core_asset     = get_asset( GRAPHENE_SYMBOL );
   const account_object& nathan_account = get_account( "nathan" );
   const account_object& buyer_account  = create_account( "buyer" );
   const account_object& seller_account = create_account( "seller" );

   transfer( genesis_account(db), seller_account, asset( 30 ) );
   transfer( nathan_account, buyer_account, test_asset.amount(10000),test_asset.amount(0) );

   BOOST_CHECK_EQUAL( get_balance( buyer_account, test_asset ), 10000 );
   BOOST_CHECK_EQUAL( get_balance( buyer_account, core_asset ), 0 );
   BOOST_CHECK_EQUAL( get_balance( seller_account, core_asset ), 30 );

   limit_order_id_type first_id  = create_sell_order( buyer_account, test_asset.amount(100), core_asset.amount(10) )->id;
   limit_order_id_type second_id = create_sell_order( buyer_account, test_asset.amount(100), core_asset.amount(20) )->id;
   limit_order_id_type third_id  = create_sell_order( buyer_account, test_asset.amount(100), core_asset.amount(30) )->id;

   BOOST_CHECK_EQUAL( get_balance( buyer_account, test_asset ), 9700 );

   //print_market( "", "" );
   auto unmatched = create_sell_order( seller_account, core_asset.amount(30), test_asset.amount(150) );
   //print_market( "", "" );
   BOOST_CHECK( !db.find( first_id ) );
   BOOST_CHECK( !db.find( second_id ) );
   BOOST_CHECK( db.find( third_id ) );
   if( unmatched ) wdump((*unmatched));
   BOOST_CHECK( !unmatched );

   BOOST_CHECK_EQUAL( get_balance( seller_account, test_asset ), 198 );
   BOOST_CHECK_EQUAL( get_balance( buyer_account, core_asset ), 30 );
   BOOST_CHECK_EQUAL( get_balance( seller_account, core_asset ), 0 );
   BOOST_CHECK_EQUAL( test_asset.dynamic_asset_data_id(db).accumulated_fees.value , 2 );
 }
 catch ( const fc::exception& e )
 {
    elog( "${e}", ("e", e.to_detail_string() ) );
    throw;
 }
}


BOOST_AUTO_TEST_CASE( uia_fees )
{
   try {
      INVOKE( issue_uia );

      enable_fees();

      const asset_object& test_asset = get_asset("TEST");
      const asset_dynamic_data_object& asset_dynamic = test_asset.dynamic_asset_data_id(db);
      const account_object& nathan_account = get_account("nathan");
      const account_object& genesis_account = account_id_type()(db);

      fund_fee_pool(genesis_account, test_asset, 1000000);
      BOOST_CHECK(asset_dynamic.fee_pool == 1000000);

      transfer_operation op({test_asset.amount(0), nathan_account.id, genesis_account.id, test_asset.amount(100)});
      op.fee = asset(op.calculate_fee(db.current_fee_schedule())) * test_asset.options.core_exchange_rate;
      BOOST_CHECK(op.fee.asset_id == test_asset.id);
      asset old_balance = db.get_balance(nathan_account.get_id(), test_asset.get_id());
      asset fee = op.fee;
      BOOST_CHECK(fee.amount > 0);
      asset core_fee = fee*test_asset.options.core_exchange_rate;
      trx.operations.push_back(std::move(op));
      PUSH_TX( db, trx, ~0 );

      BOOST_CHECK_EQUAL(get_balance(nathan_account, test_asset),
                        (old_balance - fee - test_asset.amount(100)).amount.value);
      BOOST_CHECK_EQUAL(get_balance(genesis_account, test_asset), 100);
      BOOST_CHECK(asset_dynamic.accumulated_fees == fee.amount);
      BOOST_CHECK(asset_dynamic.fee_pool == 1000000 - core_fee.amount);

      //Do it again, for good measure.
      PUSH_TX( db, trx, ~0 );
      BOOST_CHECK_EQUAL(get_balance(nathan_account, test_asset),
                        (old_balance - fee - fee - test_asset.amount(200)).amount.value);
      BOOST_CHECK_EQUAL(get_balance(genesis_account, test_asset), 200);
      BOOST_CHECK(asset_dynamic.accumulated_fees == fee.amount + fee.amount);
      BOOST_CHECK(asset_dynamic.fee_pool == 1000000 - core_fee.amount - core_fee.amount);

      op = std::move(trx.operations.back().get<transfer_operation>());
      trx.operations.clear();
      op.amount = asset(20);

      asset genesis_balance_before = db.get_balance(account_id_type(), asset_id_type());
      BOOST_CHECK_EQUAL(get_balance(nathan_account, asset_id_type()(db)), 0);
      transfer(genesis_account, nathan_account, asset(20));
      BOOST_CHECK_EQUAL(get_balance(nathan_account, asset_id_type()(db)), 20);

      trx.operations.emplace_back(std::move(op));
      PUSH_TX( db, trx, ~0 );

      BOOST_CHECK_EQUAL(get_balance(nathan_account, asset_id_type()(db)), 0);
      BOOST_CHECK_EQUAL(get_balance(nathan_account, test_asset),
                        (old_balance - fee - fee - fee - test_asset.amount(200)).amount.value);
      BOOST_CHECK_EQUAL(get_balance(genesis_account, test_asset), 200);
      BOOST_CHECK_EQUAL(get_balance(genesis_account, asset_id_type()(db)),
                        (genesis_balance_before - asset(GRAPHENE_BLOCKCHAIN_PRECISION)).amount.value);
      BOOST_CHECK(asset_dynamic.accumulated_fees == fee.amount.value * 3);
      BOOST_CHECK(asset_dynamic.fee_pool == 1000000 - core_fee.amount.value * 3);
   } catch (fc::exception& e) {
      edump((e.to_detail_string()));
      throw;
   }
}

BOOST_AUTO_TEST_CASE( cancel_limit_order_test )
{ try {
   INVOKE( issue_uia );
   const asset_object&   test_asset     = get_asset( "TEST" );
   const account_object& buyer_account  = create_account( "buyer" );

   transfer( genesis_account(db), buyer_account, asset( 10000 ) );

   BOOST_CHECK_EQUAL( get_balance(buyer_account, asset_id_type()(db)), 10000 );
   auto sell_order = create_sell_order( buyer_account, asset(1000), test_asset.amount(100+450*1) );
   FC_ASSERT( sell_order );
   auto refunded = cancel_limit_order( *sell_order );
   BOOST_CHECK( refunded == asset(1000) );
   BOOST_CHECK_EQUAL( get_balance(buyer_account, asset_id_type()(db)), 10000 );
 }
 catch ( const fc::exception& e )
 {
    elog( "${e}", ("e", e.to_detail_string() ) );
    throw;
 }
}

BOOST_AUTO_TEST_CASE( delegate_feeds )
{
   using namespace graphene::chain;
   try {
      INVOKE( create_mia );
      {
         asset_update_operation uop(get_asset("BITUSD"));
         uop.new_issuer = account_id_type();
         trx.operations.push_back(uop);
         PUSH_TX( db, trx, ~0 );
         trx.clear();
      }
      generate_block();
      const asset_object& bit_usd = get_asset("BITUSD");
      auto& global_props = db.get_global_properties();
      const vector<account_id_type> active_witnesses(global_props.witness_accounts.begin(),
                                                      global_props.witness_accounts.end());
      BOOST_REQUIRE_EQUAL(active_witnesses.size(), 10);

      asset_publish_feed_operation op({asset(), active_witnesses[0]});
      op.asset_id = bit_usd.get_id();
      op.feed.settlement_price = ~price(asset(GRAPHENE_BLOCKCHAIN_PRECISION),bit_usd.amount(30));
      // Accept defaults for required collateral
      trx.operations.emplace_back(op);
      PUSH_TX( db, trx, ~0 );

      const asset_bitasset_data_object& bitasset = bit_usd.bitasset_data(db);
      BOOST_CHECK(bitasset.current_feed.settlement_price.to_real() == 30.0 / GRAPHENE_BLOCKCHAIN_PRECISION);
      BOOST_CHECK(bitasset.current_feed.maintenance_collateral_ratio == GRAPHENE_DEFAULT_MAINTENANCE_COLLATERAL_RATIO);

      op.publisher = active_witnesses[1];
      op.feed.settlement_price = ~price(asset(GRAPHENE_BLOCKCHAIN_PRECISION),bit_usd.amount(25));
      trx.operations.back() = op;
      PUSH_TX( db, trx, ~0 );

      BOOST_CHECK_EQUAL(bitasset.current_feed.settlement_price.to_real(), 30.0 / GRAPHENE_BLOCKCHAIN_PRECISION);
      BOOST_CHECK(bitasset.current_feed.maintenance_collateral_ratio == GRAPHENE_DEFAULT_MAINTENANCE_COLLATERAL_RATIO);

      op.publisher = active_witnesses[2];
      op.feed.settlement_price = ~price(asset(GRAPHENE_BLOCKCHAIN_PRECISION),bit_usd.amount(40));
      // But this delegate is an idiot.
      op.feed.maintenance_collateral_ratio = 1001;
      trx.operations.back() = op;
      PUSH_TX( db, trx, ~0 );

      BOOST_CHECK_EQUAL(bitasset.current_feed.settlement_price.to_real(), 30.0 / GRAPHENE_BLOCKCHAIN_PRECISION);
      BOOST_CHECK(bitasset.current_feed.maintenance_collateral_ratio == GRAPHENE_DEFAULT_MAINTENANCE_COLLATERAL_RATIO);
   } catch (const fc::exception& e) {
      edump((e.to_detail_string()));
      throw;
   }
}


/**
 *  Create an order such that when the trade executes at the
 *  requested price the resulting payout to one party is 0
 *
 * I am unable to actually create such an order; I'm not sure it's possible. What I have done is create an order which
 * broke an assert in the matching algorithm.
 */
BOOST_AUTO_TEST_CASE( trade_amount_equals_zero )
{
   try {
      INVOKE(issue_uia);
      const asset_object& test = get_asset( "TEST" );
      const asset_object& core = get_asset( GRAPHENE_SYMBOL );
      const account_object& core_seller = create_account( "shorter1" );
      const account_object& core_buyer = get_account("nathan");

      transfer( genesis_account(db), core_seller, asset( 100000000 ) );

      BOOST_CHECK_EQUAL(get_balance(core_buyer, core), 0);
      BOOST_CHECK_EQUAL(get_balance(core_buyer, test), 10000000);
      BOOST_CHECK_EQUAL(get_balance(core_seller, test), 0);
      BOOST_CHECK_EQUAL(get_balance(core_seller, core), 100000000);

      //ilog( "=================================== START===================================\n\n");
      create_sell_order(core_seller, core.amount(1), test.amount(900000));
      //ilog( "=================================== STEP===================================\n\n");
      create_sell_order(core_buyer, test.amount(900001), core.amount(1));
   } catch( const fc::exception& e) {
      edump((e.to_detail_string()));
      throw;
   }
}


/**
 *  Create an order that cannot be filled immediately and have the
 *  transaction fail.
 */
BOOST_AUTO_TEST_CASE( limit_order_fill_or_kill )
{ try {
   INVOKE(issue_uia);
   const account_object& nathan = get_account("nathan");
   const asset_object& test = get_asset("TEST");
   const asset_object& core = asset_id_type()(db);

   limit_order_create_operation op;
   op.seller = nathan.id;
   op.amount_to_sell = test.amount(500);
   op.min_to_receive = core.amount(500);
   op.fill_or_kill = true;

   trx.operations.clear();
   trx.operations.push_back(op);
   BOOST_CHECK_THROW(PUSH_TX( db, trx, ~0 ), fc::exception);
   op.fill_or_kill = false;
   trx.operations.back() = op;
   PUSH_TX( db, trx, ~0 );
} FC_LOG_AND_RETHROW() }

/// Shameless code coverage plugging. Otherwise, these calls never happen.
BOOST_AUTO_TEST_CASE( fill_order )
{ try {
   fill_order_operation o;
   flat_set<account_id_type> auths;
   o.get_required_auth(auths, auths);
   BOOST_CHECK_THROW(o.validate(), fc::exception);
   o.calculate_fee(db.current_fee_schedule());
} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE( witness_withdraw_pay_test )
{ try {
   // there is an immediate maintenance interval in the first block
   //   which will initialize last_budget_time
   generate_block();

   // Based on the size of the reserve fund later in the test, the witness budget will be set to this value
   const int ref_budget = 624;
   const int witness_ppb = 55;

   db.modify( db.get_global_properties(), [&]( global_property_object& _gpo )
   {
      _gpo.parameters.witness_pay_per_block = witness_ppb;
   } );

   // Make an account and upgrade it to prime, so that witnesses get some pay
   create_account("nathan");
   transfer(account_id_type()(db), get_account("nathan"), asset(10000000000));
   generate_block();

   const asset_object* core = &asset_id_type()(db);
   const account_object* nathan = &get_account("nathan");
   enable_fees(105000000);
   BOOST_CHECK_GT(db.current_fee_schedule().membership_lifetime_fee, 0);

   BOOST_CHECK_EQUAL(core->dynamic_asset_data_id(db).accumulated_fees.value, 0);
   account_upgrade_operation uop;
   uop.account_to_upgrade = nathan->get_id();
   uop.upgrade_to_lifetime_member = true;
   trx.set_expiration(db.head_block_id());
   trx.operations.push_back(uop);
   trx.visit(operation_set_fee(db.current_fee_schedule()));
   trx.validate();
   trx.sign(key_id_type(),delegate_priv_key);
   db.push_transaction(trx);
   trx.clear();
   BOOST_CHECK_EQUAL(get_balance(*nathan, *core), 8950000000);

   generate_block();
   nathan = &get_account("nathan");
   core = &asset_id_type()(db);
   const witness_object* witness = &db.fetch_block_by_number(db.head_block_num())->witness(db);

   BOOST_CHECK_EQUAL(witness->accumulated_income.value, 0);

   auto schedule_maint = [&]()
   {
      // now we do maintenance
      db.modify( db.get_dynamic_global_properties(), [&]( dynamic_global_property_object& _dpo )
      {
         _dpo.next_maintenance_time = db.head_block_time() + 1;
      } );
   };

   // generate some blocks
   while( db.head_block_num() < 30 )
   {
      generate_block();
      witness = &db.fetch_block_by_number(db.head_block_num())->witness(db);
      BOOST_CHECK_EQUAL( witness->accumulated_income.value, 0 );
   }
   BOOST_CHECK_EQUAL( db.head_block_num(), 30 );
   // maintenance will be in block 31.  time of block 31 - time of block 1 = 30 * 5 seconds.

   schedule_maint();
   // The 80% lifetime referral fee went to the committee account, which burned it. Check that it's here.
   BOOST_CHECK_EQUAL( core->burned(db).value, 840000000 );
   generate_block();
   BOOST_CHECK_EQUAL( core->burned(db).value, 840000000 + 210000000 - ref_budget );
   BOOST_CHECK_EQUAL( db.get_dynamic_global_properties().witness_budget.value, ref_budget );
   witness = &db.fetch_block_by_number(db.head_block_num())->witness(db);
   // first witness paid from old budget (so no pay)
   BOOST_CHECK_EQUAL( witness->accumulated_income.value, 0 );
   // second witness finally gets paid!
   generate_block();
   witness = &db.fetch_block_by_number(db.head_block_num())->witness(db);
   BOOST_CHECK_EQUAL( witness->accumulated_income.value, witness_ppb );
   BOOST_CHECK_EQUAL( db.get_dynamic_global_properties().witness_budget.value, ref_budget - witness_ppb );

   generate_block();
   witness = &db.fetch_block_by_number(db.head_block_num())->witness(db);
   BOOST_CHECK_EQUAL( witness->accumulated_income.value, witness_ppb );
   BOOST_CHECK_EQUAL( db.get_dynamic_global_properties().witness_budget.value, ref_budget - 2 * witness_ppb );

   generate_block();
   witness = &db.fetch_block_by_number(db.head_block_num())->witness(db);
   BOOST_CHECK_EQUAL( witness->accumulated_income.value, witness_ppb );
   BOOST_CHECK_EQUAL( db.get_dynamic_global_properties().witness_budget.value, ref_budget - 3 * witness_ppb );

   generate_block();
   witness = &db.fetch_block_by_number(db.head_block_num())->witness(db);
   BOOST_CHECK_EQUAL( witness->accumulated_income.value, witness_ppb );
   BOOST_CHECK_EQUAL( db.get_dynamic_global_properties().witness_budget.value, ref_budget - 4 * witness_ppb );

   trx.set_expiration(db.head_block_time() + GRAPHENE_DEFAULT_MAX_TIME_UNTIL_EXPIRATION);
   // Withdraw the witness's pay
   enable_fees(1);
   witness_withdraw_pay_operation wop;
   wop.from_witness = witness->id;
   wop.to_account = witness->witness_account;
   wop.amount = witness->accumulated_income;
   trx.operations.push_back(wop);
   REQUIRE_THROW_WITH_VALUE(wop, amount, witness->accumulated_income.value * 2);
   trx.operations.back() = wop;
   trx.visit(operation_set_fee(db.current_fee_schedule()));
   trx.validate();
   db.push_transaction(trx, database::skip_authority_check);
   trx.clear();

   BOOST_CHECK_EQUAL(get_balance(witness->witness_account(db), *core), witness_ppb - 1/*fee*/);
   BOOST_CHECK_EQUAL(core->burned(db).value, 840000000 + 210000000 - ref_budget );
   BOOST_CHECK_EQUAL(witness->accumulated_income.value, 0);
} FC_LOG_AND_RETHROW() }

/**
 * This test should simulate a prediction market which means the following:
 *
 * 1) Issue a BitAsset without Forced Settling & With Global Settling
 * 2) Don't Publish any Price Feeds
 * 3) Ensure that margin calls do not occur even if the highest bid would indicate it
 * 4) Match some Orders
 * 5) Trigger Global Settle on the Asset
 * 6) The maintenance collateral must always be 1:1
 */
BOOST_AUTO_TEST_CASE_EXPECTED_FAILURES( unimp_prediction_market_test, 1 )
BOOST_AUTO_TEST_CASE( unimp_prediction_market_test )
{
   BOOST_FAIL( "not implemented" );
}

/**
 *  This test should verify that the asset_global_settle operation works as expected,
 *  make sure that global settling cannot be performed by anyone other than the
 *  issuer and only if the global settle bit is set.
 */
BOOST_AUTO_TEST_CASE_EXPECTED_FAILURES( unimp_global_settle_test_2, 1 )
BOOST_AUTO_TEST_CASE( unimp_global_settle_test_2 )
{
   BOOST_FAIL( "not implemented" );
}

/**
 *  Asset Burn Test should make sure that all assets except bitassets
 *  can be burned and all supplies add up.
 */
BOOST_AUTO_TEST_CASE_EXPECTED_FAILURES( unimp_burn_asset_test, 1 )
BOOST_AUTO_TEST_CASE( unimp_burn_asset_test )
{
   BOOST_FAIL( "not implemented" );
}

/**
 * This test demonstrates how using the call_order_update_operation to
 * increase the maintenance collateral ratio above the current market
 * price, perhaps setting it to infinity.
 */
BOOST_AUTO_TEST_CASE_EXPECTED_FAILURES( unimp_cover_with_collateral_test, 1 )
BOOST_AUTO_TEST_CASE( unimp_cover_with_collateral_test )
{
   BOOST_FAIL( "not implemented" );
}

BOOST_AUTO_TEST_CASE_EXPECTED_FAILURES( unimp_bulk_discount_test, 1 )
BOOST_AUTO_TEST_CASE( unimp_bulk_discount_test )
{
   // commented out to silence compiler warnings
   //const account_object& shorter1  = create_account( "alice" );
   //const account_object& shorter2  = create_account( "bob" );
   BOOST_FAIL( "not implemented" );
}
/**
 *  Assume the referrer gets 99% of transaction fee
 */
BOOST_AUTO_TEST_CASE_EXPECTED_FAILURES( unimp_transfer_cashback_test, 1 )
BOOST_AUTO_TEST_CASE( unimp_transfer_cashback_test )
{
   try {
   BOOST_FAIL( "Rewrite this test with VBO based cashback" );
#if 0
   generate_blocks(1);

   const account_object& sam  = create_account( "sam" );
   transfer(account_id_type()(db), sam, asset(30000));
   upgrade_to_lifetime_member(sam);

   ilog( "Creating alice" );
   const account_object& alice  = create_account( "alice", sam, sam, 0 );
   ilog( "Creating bob" );
   const account_object& bob    = create_account( "bob", sam, sam, 0 );

   transfer(account_id_type()(db), alice, asset(300000));

   enable_fees();

   transfer(alice, bob, asset(100000));

   BOOST_REQUIRE_EQUAL( alice.statistics(db).lifetime_fees_paid.value, GRAPHENE_BLOCKCHAIN_PRECISION  );

   const asset_dynamic_data_object& core_asset_data = db.get_core_asset().dynamic_asset_data_id(db);
   // 1% of fee goes to witnesses
   BOOST_CHECK_EQUAL(core_asset_data.accumulated_fees.value,
                     GRAPHENE_BLOCKCHAIN_PRECISION/100/*witness*/ + GRAPHENE_BLOCKCHAIN_PRECISION/5 /*burn*/);
   // 99% of fee goes to referrer / registrar sam
   BOOST_CHECK_EQUAL( sam.statistics(db).cashback_rewards.value,
                      GRAPHENE_BLOCKCHAIN_PRECISION - GRAPHENE_BLOCKCHAIN_PRECISION/100/*witness*/  - GRAPHENE_BLOCKCHAIN_PRECISION/5/*burn*/);

#endif
   } catch( const fc::exception& e )
   {
      edump((e.to_detail_string()));
      throw;
   }
}

BOOST_AUTO_TEST_CASE( vesting_balance_create_test )
{ try {
   INVOKE( create_uia );

   const asset_object& core = asset_id_type()(db);
   const asset_object& test_asset = get_asset( "TEST" );

   vesting_balance_create_operation op;
   op.fee = core.amount( 0 );
   op.creator = account_id_type();
   op.owner = account_id_type();
   op.amount = test_asset.amount( 100 );
   op.vesting_seconds = 60*60*24;

   // Fee must be non-negative
   REQUIRE_OP_VALIDATION_SUCCESS( op, fee, core.amount(  1 )  );
   REQUIRE_OP_VALIDATION_SUCCESS( op, fee, core.amount(  0 )  );
   REQUIRE_OP_VALIDATION_FAILURE( op, fee, core.amount( -1 ) );

   // Amount must be positive
   REQUIRE_OP_VALIDATION_SUCCESS( op, amount, core.amount(  1 ) );
   REQUIRE_OP_VALIDATION_FAILURE( op, amount, core.amount(  0 ) );
   REQUIRE_OP_VALIDATION_FAILURE( op, amount, core.amount( -1 ) );

   // Min vesting period must be at least 1 sec
   REQUIRE_OP_VALIDATION_SUCCESS( op, vesting_seconds, 1 );
   REQUIRE_OP_VALIDATION_FAILURE( op, vesting_seconds, 0 );

   // Setup world state we will need to test actual evaluation
   const account_object& alice_account = create_account( "alice" );
   const account_object& bob_account = create_account( "bob" );

   transfer( genesis_account(db), alice_account, core.amount( 100000 ) );

   op.creator = alice_account.get_id();
   op.owner = alice_account.get_id();

   account_id_type nobody = account_id_type( 1234 );

   trx.operations.push_back( op );
   // Invalid account_id's
   REQUIRE_THROW_WITH_VALUE( op, creator, nobody );
   REQUIRE_THROW_WITH_VALUE( op,   owner, nobody );

   // Insufficient funds
   REQUIRE_THROW_WITH_VALUE( op, amount, core.amount( 999999999 ) );
   // Alice can fund a bond to herself or to Bob
   op.amount = core.amount( 1000 );
   REQUIRE_OP_EVALUATION_SUCCESS( op, owner, alice_account.get_id() );
   REQUIRE_OP_EVALUATION_SUCCESS( op, owner,   bob_account.get_id() );
} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE( vesting_balance_withdraw_test )
{ try {
   INVOKE( create_uia );
   // required for head block time
   generate_block();

   const asset_object& core = asset_id_type()(db);
   const asset_object& test_asset = get_asset( "TEST" );

   vesting_balance_withdraw_operation op;
   op.fee = core.amount( 0 );
   op.vesting_balance = vesting_balance_id_type();
   op.owner = account_id_type();
   op.amount = test_asset.amount( 100 );

   // Fee must be non-negative
   REQUIRE_OP_VALIDATION_SUCCESS( op, fee, core.amount(  1 )  );
   REQUIRE_OP_VALIDATION_SUCCESS( op, fee, core.amount(  0 )  );
   REQUIRE_OP_VALIDATION_FAILURE( op, fee, core.amount( -1 ) );

   // Amount must be positive
   REQUIRE_OP_VALIDATION_SUCCESS( op, amount, core.amount(  1 ) );
   REQUIRE_OP_VALIDATION_FAILURE( op, amount, core.amount(  0 ) );
   REQUIRE_OP_VALIDATION_FAILURE( op, amount, core.amount( -1 ) );

   // Setup world state we will need to test actual evaluation
   const account_object& alice_account = create_account( "alice" );
   const account_object& bob_account = create_account( "bob" );

   transfer( genesis_account(db), alice_account, core.amount( 1000000 ) );

   auto spin_vbo_clock = [&]( const vesting_balance_object& vbo, uint32_t dt_secs )
   {
      // HACK:  This just modifies the DB creation record to be further
      //    in the past
      db.modify( vbo, [&]( vesting_balance_object& _vbo )
      {
         _vbo.policy.get<cdd_vesting_policy>().coin_seconds_earned_last_update -= dt_secs;
      } );
   };

   auto create_vbo = [&](
      account_id_type creator, account_id_type owner,
      asset amount, uint32_t vesting_seconds, uint32_t elapsed_seconds
      ) -> const vesting_balance_object&
   {
      transaction tx;

      vesting_balance_create_operation create_op;
      create_op.fee = core.amount( 0 );
      create_op.creator = creator;
      create_op.owner = owner;
      create_op.amount = amount;
      create_op.vesting_seconds = vesting_seconds;
      tx.operations.push_back( create_op );

      processed_transaction ptx = PUSH_TX( db,  tx, ~0  );
      const vesting_balance_object& vbo = vesting_balance_id_type(
         ptx.operation_results[0].get<object_id_type>())(db);

      if( elapsed_seconds > 0 )
         spin_vbo_clock( vbo, elapsed_seconds );
      return vbo;
   };

   auto top_up = [&]()
   {
      trx.clear();
      transfer( genesis_account(db),
         alice_account,
         core.amount( 1000000 - db.get_balance( alice_account, core ).amount )
         );
      FC_ASSERT( db.get_balance( alice_account, core ).amount == 1000000 );
      trx.clear();
      trx.operations.push_back( op );
   };

   trx.clear();
   trx.operations.push_back( op );

   {
      // Try withdrawing a single satoshi
      const vesting_balance_object& vbo = create_vbo(
         alice_account.id, alice_account.id, core.amount( 10000 ), 1000, 0);

      FC_ASSERT( db.get_balance( alice_account,       core ).amount ==  990000 );

      op.vesting_balance = vbo.id;
      op.owner = alice_account.id;

      REQUIRE_THROW_WITH_VALUE( op, amount, core.amount(1) );

      // spin the clock and make sure we can withdraw 1/1000 in 1 second
      spin_vbo_clock( vbo, 1 );
      // Alice shouldn't be able to withdraw 11, it's too much
      REQUIRE_THROW_WITH_VALUE( op, amount, core.amount(11) );
      op.amount = core.amount( 1 );
      // Bob shouldn't be able to withdraw anything
      REQUIRE_THROW_WITH_VALUE( op, owner, bob_account.id );
      // Shouldn't be able to get out different asset than was put in
      REQUIRE_THROW_WITH_VALUE( op, amount, test_asset.amount(1) );
      // Withdraw the max, we are OK...
      REQUIRE_OP_EVALUATION_SUCCESS( op, amount, core.amount(10) );
      FC_ASSERT( db.get_balance( alice_account,       core ).amount ==  990010 );
      top_up();
   }

   // Make sure we can withdraw the correct amount after 999 seconds
   {
      const vesting_balance_object& vbo = create_vbo(
         alice_account.id, alice_account.id, core.amount( 10000 ), 1000, 999);

      FC_ASSERT( db.get_balance( alice_account,       core ).amount ==  990000 );

      op.vesting_balance = vbo.id;
      op.owner = alice_account.id;
      // Withdraw one satoshi too much, no dice
      REQUIRE_THROW_WITH_VALUE( op, amount, core.amount(9991) );
      // Withdraw just the right amount, success!
      REQUIRE_OP_EVALUATION_SUCCESS( op, amount, core.amount(9990) );
      FC_ASSERT( db.get_balance( alice_account,       core ).amount ==  999990 );
      top_up();
   }

   // Make sure we can withdraw the whole thing after 1000 seconds
   {
      const vesting_balance_object& vbo = create_vbo(
         alice_account.id, alice_account.id, core.amount( 10000 ), 1000, 1000);

      FC_ASSERT( db.get_balance( alice_account,       core ).amount ==  990000 );

      op.vesting_balance = vbo.id;
      op.owner = alice_account.id;
      // Withdraw one satoshi too much, no dice
      REQUIRE_THROW_WITH_VALUE( op, amount, core.amount(10001) );
      // Withdraw just the right amount, success!
      REQUIRE_OP_EVALUATION_SUCCESS( op, amount, core.amount(10000) );
      FC_ASSERT( db.get_balance( alice_account,       core ).amount == 1000000 );
   }

   // Make sure that we can't withdraw a single extra satoshi no matter how old it is
   {
      const vesting_balance_object& vbo = create_vbo(
         alice_account.id, alice_account.id, core.amount( 10000 ), 1000, 123456);

      FC_ASSERT( db.get_balance( alice_account,       core ).amount ==  990000 );

      op.vesting_balance = vbo.id;
      op.owner = alice_account.id;
      // Withdraw one satoshi too much, no dice
      REQUIRE_THROW_WITH_VALUE( op, amount, core.amount(10001) );
      // Withdraw just the right amount, success!
      REQUIRE_OP_EVALUATION_SUCCESS( op, amount, core.amount(10000) );
      FC_ASSERT( db.get_balance( alice_account,       core ).amount == 1000000 );
   }

   // Try withdrawing in three max installments:
   //   5000 after  500      seconds
   //   2000 after  400 more seconds
   //   3000 after 1000 more seconds
   {
      const vesting_balance_object& vbo = create_vbo(
         alice_account.id, alice_account.id, core.amount( 10000 ), 1000, 0);

      FC_ASSERT( db.get_balance( alice_account,       core ).amount ==  990000 );

      op.vesting_balance = vbo.id;
      op.owner = alice_account.id;
      REQUIRE_THROW_WITH_VALUE     ( op, amount, core.amount(   1) );
      spin_vbo_clock( vbo, 499 );
      REQUIRE_THROW_WITH_VALUE     ( op, amount, core.amount(5000) );
      spin_vbo_clock( vbo,   1 );
      REQUIRE_THROW_WITH_VALUE     ( op, amount, core.amount(5001) );
      REQUIRE_OP_EVALUATION_SUCCESS( op, amount, core.amount(5000) );
      FC_ASSERT( db.get_balance( alice_account,       core ).amount ==  995000 );

      spin_vbo_clock( vbo, 399 );
      REQUIRE_THROW_WITH_VALUE     ( op, amount, core.amount(2000) );
      spin_vbo_clock( vbo,   1 );
      REQUIRE_THROW_WITH_VALUE     ( op, amount, core.amount(2001) );
      REQUIRE_OP_EVALUATION_SUCCESS( op, amount, core.amount(2000) );
      FC_ASSERT( db.get_balance( alice_account,       core ).amount ==  997000 );

      spin_vbo_clock( vbo, 999 );
      REQUIRE_THROW_WITH_VALUE     ( op, amount, core.amount(3000) );
      spin_vbo_clock( vbo, 1   );
      REQUIRE_THROW_WITH_VALUE     ( op, amount, core.amount(3001) );
      REQUIRE_OP_EVALUATION_SUCCESS( op, amount, core.amount(3000) );
      FC_ASSERT( db.get_balance( alice_account,       core ).amount == 1000000 );
   }

   //
   // Increase by 10,000 csd / sec initially.
   // After 500 seconds, we have 5,000,000 csd.
   // Withdraw 2,000, we are now at 8,000 csd / sec.
   // At 8,000 csd / sec, it will take us 625 seconds to mature.
   //
   {
      const vesting_balance_object& vbo = create_vbo(
         alice_account.id, alice_account.id, core.amount( 10000 ), 1000, 0);

      FC_ASSERT( db.get_balance( alice_account,       core ).amount ==  990000 );

      op.vesting_balance = vbo.id;
      op.owner = alice_account.id;
      REQUIRE_THROW_WITH_VALUE     ( op, amount, core.amount(   1) );
      spin_vbo_clock( vbo, 500 );
      REQUIRE_OP_EVALUATION_SUCCESS( op, amount, core.amount(2000) );
      FC_ASSERT( db.get_balance( alice_account,       core ).amount ==  992000 );

      spin_vbo_clock( vbo, 624 );
      REQUIRE_THROW_WITH_VALUE     ( op, amount, core.amount(8000) );
      spin_vbo_clock( vbo,   1 );
      REQUIRE_THROW_WITH_VALUE     ( op, amount, core.amount(8001) );
      REQUIRE_OP_EVALUATION_SUCCESS( op, amount, core.amount(8000) );
      FC_ASSERT( db.get_balance( alice_account,       core ).amount == 1000000 );
   }
   // TODO:  Test with non-core asset and Bob account
} FC_LOG_AND_RETHROW() }

// TODO:  Write linear VBO tests

BOOST_AUTO_TEST_SUITE_END()
