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
#include <graphene/chain/short_order_object.hpp>
#include <graphene/chain/vesting_balance_object.hpp>
#include <graphene/chain/withdraw_permission_object.hpp>
#include <graphene/chain/witness_object.hpp>

#include <fc/crypto/digest.hpp>

#include "../common/database_fixture.hpp"

using namespace graphene::chain;

BOOST_FIXTURE_TEST_SUITE( operation_tests, database_fixture )

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
      REQUIRE_THROW_WITH_VALUE(op, voting_account, account_id_type(999999999));
      REQUIRE_THROW_WITH_VALUE(op, memo_key, key_id_type(999999999));

      auto auth_bak = op.owner;
      op.owner.add_authority(account_id_type(9999999999), 10);
      trx.operations.back() = op;
      op.owner = auth_bak;
      BOOST_REQUIRE_THROW(db.push_transaction(trx, ~0), fc::exception);
      op.owner = auth_bak;
      op.owner.add_authority(key_id_type(9999999999), 10);
      trx.operations.back() = op;
      BOOST_REQUIRE_THROW(db.push_transaction(trx, ~0), fc::exception);
      op.owner = auth_bak;

      trx.operations.back() = op;
      trx.sign( key_id_type(), fc::ecc::private_key::regenerate(fc::sha256::hash(string("genesis"))) );
      trx.validate();
      db.push_transaction(trx, ~0);

      const account_object& nathan_account = *db.get_index_type<account_index>().indices().get<by_name>().find("nathan");
      BOOST_CHECK(nathan_account.id.space() == protocol_ids);
      BOOST_CHECK(nathan_account.id.type() == account_object_type);
      BOOST_CHECK(nathan_account.name == "nathan");

      BOOST_REQUIRE(nathan_account.owner.auths.size() == 1);
      BOOST_CHECK(nathan_account.owner.auths.at(genesis_key) == 123);
      BOOST_REQUIRE(nathan_account.active.auths.size() == 1);
      BOOST_CHECK(nathan_account.active.auths.at(genesis_key) == 321);
      BOOST_CHECK(nathan_account.voting_account == account_id_type());
      BOOST_CHECK(nathan_account.memo_key == genesis_key);

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
      sign(trx, key_id_type(), fc::ecc::private_key::regenerate(fc::sha256::hash(string("genesis"))));

      BOOST_REQUIRE_THROW(db.push_transaction(trx), fc::exception);
      sign(trx, nathan_key.id,nathan_private_key);
      BOOST_REQUIRE_THROW(db.push_transaction(trx), fc::exception);
      trx.signatures.clear();
      op.owner = authority(1, account_id_type(nathan.id), 1);
      trx.operations.back() = op;
      sign(trx, key_id_type(), fc::ecc::private_key::regenerate(fc::sha256::hash(string("genesis"))));
      sign(trx, nathan_key.id, nathan_private_key);
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
      db.push_transaction(trx, ~0);

      account_update_operation op;
      op.account = nathan.id;
      op.owner = authority(2, key_id, 1, key_id_type(), 1);
      op.active = authority(2, key_id, 1, key_id_type(), 1);
      //op.voting_account = key_id;
      op.vote = flat_set<vote_id_type>({active_delegates[0](db).vote_id, active_delegates[5](db).vote_id});
      trx.operations.back() = op;
      db.push_transaction(trx, ~0);

      //BOOST_CHECK(nathan.voting_key == key_id);
      BOOST_CHECK(nathan.memo_key == key_id_type());
      BOOST_CHECK(nathan.active.weight_threshold == 2);
      BOOST_CHECK(nathan.active.auths.size() == 2);
      BOOST_CHECK(nathan.active.auths.at(key_id) == 1);
      BOOST_CHECK(nathan.active.auths.at(key_id_type()) == 1);
      BOOST_CHECK(nathan.owner.weight_threshold == 2);
      BOOST_CHECK(nathan.owner.auths.size() == 2);
      BOOST_CHECK(nathan.owner.auths.at(key_id) == 1);
      BOOST_CHECK(nathan.owner.auths.at(key_id_type()) == 1);
      BOOST_CHECK(nathan.votes.size() == 2);

      /** these votes are no longer tallied in real time
      BOOST_CHECK(active_delegates[0](db).vote(db).total_votes == 30000);
      BOOST_CHECK(active_delegates[1](db).vote(db).total_votes == 0);
      BOOST_CHECK(active_delegates[4](db).vote(db).total_votes == 0);
      BOOST_CHECK(active_delegates[5](db).vote(db).total_votes == 30000);
      BOOST_CHECK(active_delegates[6](db).vote(db).total_votes == 0);
      */

      transfer(account_id_type()(db), nathan, asset(3000000));

      enable_fees();
      op.upgrade_to_prime   = true;
      op.fee     = op.calculate_fee( db.get_global_properties().parameters.current_fees );
      trx.operations.push_back(op);
      db.push_transaction(trx, ~0);

      BOOST_CHECK( nathan.referrer == nathan.id );
      BOOST_CHECK( nathan.referrer_percent == 100 );
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
      db.push_transaction(trx, ~0);

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
      db.push_transaction(trx, ~0);

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
      db.push_transaction(trx, ~0);
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
      db.push_transaction(trx, ~0);
      std::swap(op.new_options.flags, op.new_options.issuer_permissions);
      op.new_issuer = account_id_type();
      trx.operations.back() = op;
      db.push_transaction(trx, ~0);

      {
         asset_publish_feed_operation pop;
         pop.asset_id = bit_usd.get_id();
         pop.publisher = account_id_type(1);
         price_feed feed;
         feed.call_limit = price(bit_usd.amount(5), bit_usd.amount(5));
         feed.short_limit = feed.call_limit;
         REQUIRE_THROW_WITH_VALUE(pop, feed, feed);
         feed.call_limit = price(bit_usd.amount(5), asset(5));
         feed.short_limit = ~feed.call_limit;
         REQUIRE_THROW_WITH_VALUE(pop, feed, feed);
         feed.short_limit = price(asset(4), bit_usd.amount(5));
         REQUIRE_THROW_WITH_VALUE(pop, feed, feed);
         std::swap(feed.call_limit, feed.short_limit);
         pop.feed = feed;
         REQUIRE_THROW_WITH_VALUE(pop, feed.max_margin_period_sec, 0);
         REQUIRE_THROW_WITH_VALUE(pop, feed.required_maintenance_collateral, 0);
         REQUIRE_THROW_WITH_VALUE(pop, feed.required_initial_collateral, 500);
         trx.operations.back() = pop;
         db.push_transaction(trx, ~0);
      }

      trx.operations.clear();
      auto nathan = create_account("nathan");
      op.issuer = account_id_type();
      op.new_issuer = nathan.id;
      trx.operations.emplace_back(op);
      db.push_transaction(trx, ~0);
      BOOST_CHECK(bit_usd.issuer == nathan.id);

      op.issuer = nathan.id;
      op.new_issuer = account_id_type();
      trx.operations.back() = op;
      db.push_transaction(trx, ~0);
      BOOST_CHECK(bit_usd.issuer == account_id_type());
   } catch ( const fc::exception& e ) {
      elog( "${e}", ("e", e.to_detail_string() ) );
      throw;
   }
}

BOOST_AUTO_TEST_CASE( create_short_test )
{
   try {
      const asset_object& bitusd = create_bitasset( "BITUSD" );
      const account_object& shorter_account  = create_account( "shorter" );
      transfer( genesis_account(db), shorter_account, asset( 10000 ) );
      auto first_short = create_short( shorter_account, bitusd.amount(100), asset( 100 ) ); // 1:1 price
      BOOST_REQUIRE( first_short != nullptr );
      BOOST_REQUIRE( create_short( shorter_account, bitusd.amount(100), asset( 200 ) ) ); // 1:2 price
      BOOST_REQUIRE( create_short( shorter_account, bitusd.amount(100), asset( 300 ) ) ); // 1:3 price
      BOOST_REQUIRE_EQUAL( get_balance(shorter_account, asset_id_type()(db) ), 10000-600 );
   }catch ( const fc::exception& e )
   {
      elog( "${e}", ("e", e.to_detail_string() ) );
      throw;
   }
}
BOOST_AUTO_TEST_CASE( cancel_short_test )
{
   try {
      const asset_object& bitusd = create_bitasset( "BITUSD" );
      const account_object& shorter_account  = create_account( "shorter" );
      transfer( genesis_account(db), shorter_account, asset( 10000 ) );
      auto first_short = create_short( shorter_account, bitusd.amount(100), asset( 100 ) ); // 1:1 price
      BOOST_REQUIRE( first_short != nullptr );
      BOOST_REQUIRE( create_short( shorter_account, bitusd.amount(100), asset( 200 ) ) ); // 1:2 price
      BOOST_REQUIRE( create_short( shorter_account, bitusd.amount(100), asset( 300 ) ) ); // 1:3 price
      BOOST_REQUIRE_EQUAL( get_balance(shorter_account, asset_id_type()(db) ), 10000-600 );
      auto refund = cancel_short_order( *first_short );
      BOOST_REQUIRE_EQUAL( get_balance(shorter_account, asset_id_type()(db) ), 10000-500 );
      FC_ASSERT( refund == asset(100) );
   }catch ( const fc::exception& e )
   {
      elog( "${e}", ("e", e.to_detail_string() ) );
      throw;
   }
}

/**
 *  Assume there exists an offer to buy BITUSD
 *  Create a short that exactly matches that offer at a price 2:1
 */
BOOST_AUTO_TEST_CASE( match_short_now_exact )
{
   try {
      const asset_object& bitusd = create_bitasset( "BITUSD" );
      const account_object& shorter_account  = create_account( "shorter" );
      const account_object& buyer_account  = create_account( "buyer" );
      transfer( genesis_account(db), shorter_account, asset( 10000 ) );
      transfer( genesis_account(db), buyer_account, asset( 10000 ) );

      auto buy_order = create_sell_order( buyer_account, asset(200), bitusd.amount(100) );
      BOOST_REQUIRE( buy_order );
      auto first_short = create_short( shorter_account, bitusd.amount(100), asset( 200 ) ); // 1:1 price
      BOOST_REQUIRE( first_short == nullptr );
      print_call_orders();
      //print_short_market("","");
   }catch ( const fc::exception& e )
   {
      elog( "${e}", ("e", e.to_detail_string() ) );
      throw;
   }
}

/**
 *  Assume there exists an offer to buy BITUSD
 *  Create a short that exactly matches that offer at a price 2:1
 */
BOOST_AUTO_TEST_CASE( dont_match_short )
{
   try {
      const asset_object& bitusd = create_bitasset( "BITUSD" );
      const account_object& shorter_account  = create_account( "shorter" );
      const account_object& buyer_account  = create_account( "buyer" );
      transfer( genesis_account(db), shorter_account, asset( 10000 ) );
      transfer( genesis_account(db), buyer_account, asset( 10000 ) );

      create_sell_order( buyer_account, asset(125), bitusd.amount(100) );
      create_sell_order( buyer_account, asset(150), bitusd.amount(100) );
      auto buy_order = create_sell_order( buyer_account, asset(100), bitusd.amount(100) );
      //print_market("","");
      BOOST_REQUIRE( buy_order );
      auto first_short  = create_short( shorter_account, bitusd.amount(100), asset( 200 ) ); // 1:1 price
      auto second_short = create_short( shorter_account, bitusd.amount(100), asset( 300 ) ); // 1:1 price
      auto third_short  = create_short( shorter_account, bitusd.amount(100), asset( 400 ) ); // 1:1 price
      //print_short_market("","");
      BOOST_REQUIRE( first_short && second_short && third_short );
      //print_joint_market("","");
   }catch ( const fc::exception& e )
   {
      elog( "${e}", ("e", e.to_detail_string() ) );
      throw;
   }
}
/**
 *  Assume there exists an offer to buy BITUSD
 *  Create a short that exactly matches that offer at a price 2:1
 */
BOOST_AUTO_TEST_CASE( match_all_short_with_surplus_collaterl )
{
   try {
      const asset_object& bitusd = create_bitasset( "BITUSD" );
      const account_object& shorter_account  = create_account( "shorter" );
      const account_object& buyer_account  = create_account( "buyer" );
      transfer( genesis_account(db), shorter_account, asset( 10000 ) );
      transfer( genesis_account(db), buyer_account, asset( 10000 ) );

      //auto buy_order = create_sell_order( buyer_account, asset(200), bitusd.amount(101) );
      auto buy_order = create_sell_order( buyer_account, asset(300), bitusd.amount(100) );
      print_market("","");
      BOOST_REQUIRE( buy_order );
      auto first_short = create_short( shorter_account, bitusd.amount(100), asset( 200 ) ); // 1:1 price
      print_short_market("","");
      BOOST_REQUIRE( !first_short );
      //print_short_market("","");
      print_call_orders();
   }catch ( const fc::exception& e )
   {
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
      db.push_transaction(trx, ~0);

      const asset_object& test_asset = test_asset_id(db);
      BOOST_CHECK(test_asset.symbol == "TEST");
      BOOST_CHECK(asset(1, test_asset_id) * test_asset.options.core_exchange_rate == asset(2));
      BOOST_CHECK(!test_asset.enforce_white_list());
      BOOST_CHECK(test_asset.options.max_supply == 100000000);
      BOOST_CHECK(!test_asset.bitasset_data_id.valid());
      BOOST_CHECK(test_asset.options.market_fee_percent == GRAPHENE_MAX_MARKET_FEE_PERCENT/100);
      BOOST_REQUIRE_THROW(db.push_transaction(trx, ~0), fc::exception);

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
      db.push_transaction(trx, ~0);
      REQUIRE_THROW_WITH_VALUE(op, new_options.core_exchange_rate, price());
      op.new_options.core_exchange_rate = test.options.core_exchange_rate;
      op.new_issuer = nathan.id;
      trx.operations.back() = op;
      db.push_transaction(trx, ~0);
      op.issuer = nathan.id;
      op.new_issuer.reset();
      op.new_options.flags = transfer_restricted | white_list;
      trx.operations.back() = op;
      db.push_transaction(trx, ~0);
      REQUIRE_THROW_WITH_VALUE(op, new_options.issuer_permissions, test.options.issuer_permissions & ~white_list);
      op.new_options.issuer_permissions = test.options.issuer_permissions & ~white_list;
      op.new_options.flags = 0;
      trx.operations.back() = op;
      db.push_transaction(trx, ~0);
      op.new_options.issuer_permissions = test.options.issuer_permissions;
      op.new_options.flags = test.options.flags;
      BOOST_CHECK(!(test.options.issuer_permissions & white_list));
      REQUIRE_THROW_WITH_VALUE(op, new_options.issuer_permissions, UIA_ASSET_ISSUER_PERMISSION_MASK);
      REQUIRE_THROW_WITH_VALUE(op, new_options.flags, white_list);
      op.new_issuer = account_id_type();
      trx.operations.back() = op;
      db.push_transaction(trx, ~0);
      op.issuer = account_id_type();
      BOOST_REQUIRE_THROW(db.push_transaction(trx, ~0), fc::exception);
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
      db.push_transaction(trx, ~0);

      const asset_dynamic_data_object& test_dynamic_data = test_asset.dynamic_asset_data_id(db);
      BOOST_CHECK_EQUAL(get_balance(nathan_account, test_asset), 5000000);
      BOOST_CHECK(test_dynamic_data.current_supply == 5000000);
      BOOST_CHECK(test_dynamic_data.accumulated_fees == 0);
      BOOST_CHECK(test_dynamic_data.fee_pool == 0);

      db.push_transaction(trx, ~0);

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
      db.push_transaction(trx, ~0);
      BOOST_CHECK_EQUAL(get_balance(nathan, uia), 10000000 - 5000);
      BOOST_CHECK_EQUAL(get_balance(genesis, uia), 5000);

      db.push_transaction(trx, ~0);
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

   print_market( "", "" );
   auto unmatched = create_sell_order( seller_account, core_asset.amount(300), test_asset.amount(150) );
   print_market( "", "" );
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

   print_market( "", "" );
   auto unmatched = create_sell_order( seller_account, core_asset.amount(100), test_asset.amount(100) );
   print_market( "", "" );
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

   print_market( "", "" );
   auto unmatched = create_sell_order( seller_account, core_asset.amount(300), test_asset.amount(150) );
   print_market( "", "" );
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

   print_market( "", "" );
   auto unmatched = create_sell_order( seller_account, core_asset.amount(30), test_asset.amount(150) );
   print_market( "", "" );
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
      db.push_transaction(trx, ~0);

      BOOST_CHECK_EQUAL(get_balance(nathan_account, test_asset),
                        (old_balance - fee - test_asset.amount(100)).amount.value);
      BOOST_CHECK_EQUAL(get_balance(genesis_account, test_asset), 100);
      BOOST_CHECK(asset_dynamic.accumulated_fees == fee.amount);
      BOOST_CHECK(asset_dynamic.fee_pool == 1000000 - core_fee.amount);

      //Do it again, for good measure.
      db.push_transaction(trx, ~0);
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
      db.push_transaction(trx, ~0);

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
         db.push_transaction(trx, ~0);
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
      op.feed.call_limit = price(asset(GRAPHENE_BLOCKCHAIN_PRECISION),bit_usd.amount(30));
      op.feed.short_limit = ~price(asset(GRAPHENE_BLOCKCHAIN_PRECISION),bit_usd.amount(10));
      // We'll expire margins after a month
      op.feed.max_margin_period_sec = fc::days(30).to_seconds();
      // Accept defaults for required collateral
      trx.operations.emplace_back(op);
      db.push_transaction(trx, ~0);

      {
         //Dumb sanity check of some operators. Only here to improve code coverage. :D
         price_feed dummy = op.feed;
         BOOST_CHECK(op.feed == dummy);
         price a(asset(1), bit_usd.amount(2));
         price b(asset(2), bit_usd.amount(2));
         price c(asset(1), bit_usd.amount(2));
         BOOST_CHECK(a < b);
         BOOST_CHECK(b > a);
         BOOST_CHECK(a == c);
         BOOST_CHECK(!(b == c));
      }

      const asset_bitasset_data_object& bitasset = bit_usd.bitasset_data(db);
      BOOST_CHECK(bitasset.current_feed.call_limit.to_real() == GRAPHENE_BLOCKCHAIN_PRECISION / 30.0);
      BOOST_CHECK_EQUAL(bitasset.current_feed.short_limit.to_real(), 10.0 / GRAPHENE_BLOCKCHAIN_PRECISION);
      BOOST_CHECK(bitasset.current_feed.max_margin_period_sec == fc::days(30).to_seconds());
      BOOST_CHECK(bitasset.current_feed.required_initial_collateral == GRAPHENE_DEFAULT_INITIAL_COLLATERAL_RATIO);
      BOOST_CHECK(bitasset.current_feed.required_maintenance_collateral == GRAPHENE_DEFAULT_MAINTENANCE_COLLATERAL_RATIO);

      op.publisher = active_witnesses[1];
      op.feed.call_limit = price(asset(GRAPHENE_BLOCKCHAIN_PRECISION),bit_usd.amount(25));
      op.feed.short_limit = ~price(asset(GRAPHENE_BLOCKCHAIN_PRECISION),bit_usd.amount(20));
      op.feed.max_margin_period_sec = fc::days(10).to_seconds();
      trx.operations.back() = op;
      db.push_transaction(trx, ~0);

      BOOST_CHECK_EQUAL(bitasset.current_feed.call_limit.to_real(), GRAPHENE_BLOCKCHAIN_PRECISION / 25.0);
      BOOST_CHECK_EQUAL(bitasset.current_feed.short_limit.to_real(), 20.0 / GRAPHENE_BLOCKCHAIN_PRECISION);
      BOOST_CHECK(bitasset.current_feed.max_margin_period_sec == fc::days(30).to_seconds());
      BOOST_CHECK(bitasset.current_feed.required_initial_collateral == GRAPHENE_DEFAULT_INITIAL_COLLATERAL_RATIO);
      BOOST_CHECK(bitasset.current_feed.required_maintenance_collateral == GRAPHENE_DEFAULT_MAINTENANCE_COLLATERAL_RATIO);

      op.publisher = active_witnesses[2];
      op.feed.call_limit = price(asset(GRAPHENE_BLOCKCHAIN_PRECISION),bit_usd.amount(40));
      op.feed.short_limit = ~price(asset(GRAPHENE_BLOCKCHAIN_PRECISION),bit_usd.amount(10));
      op.feed.max_margin_period_sec = fc::days(100).to_seconds();
      // But this delegate is an idiot.
      op.feed.required_initial_collateral = 1001;
      op.feed.required_maintenance_collateral = 1000;
      trx.operations.back() = op;
      db.push_transaction(trx, ~0);

      BOOST_CHECK_EQUAL(bitasset.current_feed.call_limit.to_real(), GRAPHENE_BLOCKCHAIN_PRECISION / 30.0);
      BOOST_CHECK_EQUAL(bitasset.current_feed.short_limit.to_real(), 10.0 / GRAPHENE_BLOCKCHAIN_PRECISION);
      BOOST_CHECK(bitasset.current_feed.max_margin_period_sec == fc::days(30).to_seconds());
      BOOST_CHECK(bitasset.current_feed.required_initial_collateral == GRAPHENE_DEFAULT_INITIAL_COLLATERAL_RATIO);
      BOOST_CHECK(bitasset.current_feed.required_maintenance_collateral == GRAPHENE_DEFAULT_MAINTENANCE_COLLATERAL_RATIO);
   } catch (const fc::exception& e) {
      edump((e.to_detail_string()));
      throw;
   }
}

/**
 *  Assume there exists an offer to buy BITUSD
 *  Create a short that exactly matches that offer at a price 2:1
 */
BOOST_AUTO_TEST_CASE( limit_match_existing_short_exact )
{
   try {
      const asset_object& bitusd = create_bitasset( "BITUSD" );
      const account_object& shorter_account  = create_account( "shorter" );
      const account_object& buyer_account  = create_account( "buyer" );
      transfer( genesis_account(db), shorter_account, asset( 10000 ) );
      transfer( genesis_account(db), buyer_account, asset( 10000 ) );

      create_sell_order( buyer_account, asset(125), bitusd.amount(100) );
      create_sell_order( buyer_account, asset(150), bitusd.amount(100) );
      auto buy_order = create_sell_order( buyer_account, asset(100), bitusd.amount(100) );
      //print_market("","");
      BOOST_REQUIRE( buy_order );
      auto first_short = create_short( shorter_account, bitusd.amount(100), asset( 200 ) ); // 1:1 price
      auto second_short = create_short( shorter_account, bitusd.amount(100), asset( 300 ) ); // 1:1 price
      auto third_short = create_short( shorter_account, bitusd.amount(100), asset( 400 ) ); // 1:1 price
      //print_short_market("","");
      BOOST_REQUIRE( first_short && second_short && third_short );
      //print_joint_market("","");
      auto unmatched_order = create_sell_order( buyer_account, asset(200), bitusd.amount(100) );
      //print_joint_market("","");
      BOOST_REQUIRE( !unmatched_order );
      // now it shouldn't fill
      unmatched_order = create_sell_order( buyer_account, asset(200), bitusd.amount(100) );
      //print_joint_market("","");
      BOOST_REQUIRE( unmatched_order );
      BOOST_CHECK( unmatched_order->amount_for_sale() == asset(200) );
      BOOST_CHECK( unmatched_order->amount_to_receive() == bitusd.amount(100) );
      BOOST_CHECK( second_short->amount_for_sale() == bitusd.amount(100) );
      BOOST_CHECK( third_short->amount_for_sale() == bitusd.amount(100) );
   }catch ( const fc::exception& e )
   {
      elog( "${e}", ("e", e.to_detail_string() ) );
      throw;
   }
}

/**
 *  Assume there exists an offer to buy BITUSD
 *  Create a short that exactly matches that offer at a price 2:1
 */
BOOST_AUTO_TEST_CASE( limit_match_existing_short_partial_exact_price )
{
   try {
      const asset_object& bitusd = create_bitasset( "BITUSD" );
      const account_object& shorter_account  = create_account( "shorter" );
      const account_object& buyer_account  = create_account( "buyer" );
      transfer( genesis_account(db), shorter_account, asset( 10000 ) );
      transfer( genesis_account(db), buyer_account, asset( 10000 ) );

      create_sell_order( buyer_account, asset(125), bitusd.amount(100) );
      create_sell_order( buyer_account, asset(150), bitusd.amount(100) );
      auto buy_order = create_sell_order( buyer_account, asset(100), bitusd.amount(100) );
      //print_market("","");
      BOOST_REQUIRE( buy_order );
      auto first_short = create_short( shorter_account, bitusd.amount(100), asset( 200 ) ); // 1:1 price
      auto second_short = create_short( shorter_account, bitusd.amount(100), asset( 300 ) ); // 1:1 price
      auto third_short = create_short( shorter_account, bitusd.amount(100), asset( 400 ) ); // 1:1 price
      //print_short_market("","");
      BOOST_REQUIRE( first_short && second_short && third_short );
      //print_joint_market("","");
      auto unmatched_order = create_sell_order( buyer_account, asset(100), bitusd.amount(50) );
      //print_joint_market("","");
      BOOST_REQUIRE( !unmatched_order );
      BOOST_CHECK( first_short->amount_for_sale() == bitusd.amount(50) );
      BOOST_CHECK( first_short->get_collateral()  == asset(100) );
      BOOST_CHECK( second_short->amount_for_sale() == bitusd.amount(100) );
      BOOST_CHECK( third_short->amount_for_sale() == bitusd.amount(100) );

   }catch ( const fc::exception& e )
   {
      elog( "${e}", ("e", e.to_detail_string() ) );
      throw;
   }
}
/**
 *  Assume there exists an offer to buy BITUSD
 *  Create a short that exactly matches that offer at a price 2:1
 */
BOOST_AUTO_TEST_CASE( limit_match_existing_short_partial_over_price )
{
   try {
      const asset_object& bitusd = create_bitasset( "BITUSD" );
      const account_object& shorter_account  = create_account( "shorter" );
      const account_object& buyer_account  = create_account( "buyer" );
      transfer( genesis_account(db), shorter_account, asset( 10000 ) );
      transfer( genesis_account(db), buyer_account, asset( 10000 ) );

      create_sell_order( buyer_account, asset(125), bitusd.amount(100) );
      create_sell_order( buyer_account, asset(150), bitusd.amount(100) );
      auto buy_order = create_sell_order( buyer_account, asset(100), bitusd.amount(100) );
      //print_market("","");
      BOOST_REQUIRE( buy_order );
      auto first_short = create_short( shorter_account, bitusd.amount(100), asset( 200 ) ); // 1:1 price
      auto second_short = create_short( shorter_account, bitusd.amount(100), asset( 300 ) ); // 1:1 price
      auto third_short = create_short( shorter_account, bitusd.amount(100), asset( 400 ) ); // 1:1 price
      BOOST_REQUIRE( first_short && second_short && third_short );
      //print_joint_market("","");
      auto unmatched_order = create_sell_order( buyer_account, asset(100), bitusd.amount(40) );
      //print_joint_market("","");
      BOOST_REQUIRE( !unmatched_order );
      BOOST_CHECK( first_short->amount_for_sale() == bitusd.amount(50) );
      BOOST_CHECK( first_short->get_collateral()  == asset(100) );
      BOOST_CHECK( second_short->amount_for_sale() == bitusd.amount(100) );
      BOOST_CHECK( third_short->amount_for_sale() == bitusd.amount(100) );

   }catch ( const fc::exception& e )
   {
      elog( "${e}", ("e", e.to_detail_string() ) );
      throw;
   }
}

/**
 *  Assume there exists an offer to buy BITUSD
 *  Create a short that exactly matches that offer at a price 2:1
 */
BOOST_AUTO_TEST_CASE( limit_match_multiple_existing_short_partial_over_price )
{
   try {
      const asset_object& bitusd = create_bitasset( "BITUSD" );
      const account_object& shorter_account  = create_account( "shorter" );
      const account_object& buyer_account  = create_account( "buyer" );
      transfer( genesis_account(db), shorter_account, asset( 10000 ) );
      transfer( genesis_account(db), buyer_account, asset( 10000 ) );

      create_sell_order( buyer_account, asset(125), bitusd.amount(100) );
      create_sell_order( buyer_account, asset(150), bitusd.amount(100) );
      auto buy_order = create_sell_order( buyer_account, asset(100), bitusd.amount(100) );
      //print_market("","");
      BOOST_REQUIRE( buy_order );
      auto first_short = create_short( shorter_account, bitusd.amount(100), asset( 200 ) ); // 1:1 price
      auto next_short = create_short( shorter_account, bitusd.amount(100), asset( 210 ) ); // 1:1 price
      auto second_short = create_short( shorter_account, bitusd.amount(100), asset( 300 ) ); // 1:1 price
      auto third_short = create_short( shorter_account, bitusd.amount(100), asset( 400 ) ); // 1:1 price
      //print_short_market("","");
      BOOST_REQUIRE( first_short && second_short && third_short );
      auto unmatched_order = create_sell_order( buyer_account, asset(200+115), bitusd.amount(150) );
     // print_joint_market("","");
      BOOST_REQUIRE( !unmatched_order );
      //wdump( (next_short->amount_for_sale().amount)(next_short->get_collateral().amount) );
      BOOST_CHECK( next_short->amount_for_sale() == bitusd.amount(46) );
      BOOST_CHECK( next_short->get_collateral()  == asset(97) );
      BOOST_CHECK( second_short->amount_for_sale() == bitusd.amount(100) );
      BOOST_CHECK( third_short->amount_for_sale() == bitusd.amount(100) );
      print_call_orders();

   }catch ( const fc::exception& e )
   {
      elog( "${e}", ("e", e.to_detail_string() ) );
      throw;
   }
}

/**
 *  Assume there exists an offer to buy BITUSD
 *  Create a short that exactly matches that offer at a price 2:1
 */
BOOST_AUTO_TEST_CASE( limit_dont_match_existing_short_partial_over_price )
{
   try {
      const asset_object& bitusd = create_bitasset( "BITUSD" );
      const account_object& shorter_account  = create_account( "shorter" );
      const account_object& buyer_account  = create_account( "buyer" );
      transfer( genesis_account(db), shorter_account, asset( 10000 ) );
      transfer( genesis_account(db), buyer_account, asset( 10000 ) );

      create_sell_order( buyer_account, asset(125), bitusd.amount(100) );
      create_sell_order( buyer_account, asset(150), bitusd.amount(100) );
      auto buy_order = create_sell_order( buyer_account, asset(100), bitusd.amount(100) );
      BOOST_REQUIRE( buy_order );
      auto first_short = create_short( shorter_account, bitusd.amount(100), asset( 200 ) ); // 1:1 price
      auto second_short = create_short( shorter_account, bitusd.amount(100), asset( 300 ) ); // 1:1 price
      auto third_short = create_short( shorter_account, bitusd.amount(100), asset( 400 ) ); // 1:1 price
      //print_short_market("","");
      BOOST_REQUIRE( first_short && second_short && third_short );
      //print_joint_market("","");
      auto unmatched_order = create_sell_order( buyer_account, asset(100), bitusd.amount(60) );
      BOOST_REQUIRE( unmatched_order );
      BOOST_CHECK( first_short->amount_for_sale() == bitusd.amount(100) );
      BOOST_CHECK( first_short->get_collateral()  == asset(200) );
      BOOST_CHECK( second_short->amount_for_sale() == bitusd.amount(100) );
      BOOST_CHECK( third_short->amount_for_sale() == bitusd.amount(100) );
   }catch ( const fc::exception& e )
   {
      elog( "${e}", ("e", e.to_detail_string() ) );
      throw;
   }
}

BOOST_AUTO_TEST_CASE( multiple_shorts_matching_multiple_bids_in_order )
{ try {
   const asset_object& bitusd = create_bitasset( "BITUSD" );
   const account_object& shorter1_account  = create_account( "shorter1" );
   const account_object& shorter2_account  = create_account( "shorter2" );
   const account_object& shorter3_account  = create_account( "shorter3" );
   const account_object& buyer_account  = create_account( "buyer" );
   transfer( genesis_account(db), shorter1_account, asset( 10000 ) );
   transfer( genesis_account(db), shorter2_account, asset( 10000 ) );
   transfer( genesis_account(db), shorter3_account, asset( 10000 ) );
   transfer( genesis_account(db), buyer_account, asset( 10000 ) );

   BOOST_REQUIRE( create_sell_order( buyer_account, asset(125), bitusd.amount(100) ) );
   BOOST_REQUIRE( create_sell_order( buyer_account, asset(150), bitusd.amount(100) ) );
   BOOST_REQUIRE( create_sell_order( buyer_account, asset(200), bitusd.amount(100) ) );
   print_joint_market("","");
   BOOST_REQUIRE( !create_short( shorter1_account, bitusd.amount(100), asset( 200 ) ) );
   BOOST_REQUIRE( !create_short( shorter2_account, bitusd.amount(100), asset( 150 ) ) );
   BOOST_REQUIRE( !create_short( shorter3_account, bitusd.amount(100), asset( 125 ) ) );
   print_call_orders();

   auto& index = db.get_index_type<call_order_index>().indices().get<by_account>();
   BOOST_CHECK(index.find(boost::make_tuple(buyer_account.id, bitusd.id)) == index.end());
   BOOST_CHECK(index.find(boost::make_tuple(shorter1_account.id, bitusd.id)) != index.end());
   BOOST_CHECK(index.find(boost::make_tuple(shorter1_account.id, bitusd.id))->get_debt() == bitusd.amount(100) );
   BOOST_CHECK(index.find(boost::make_tuple(shorter1_account.id, bitusd.id))->call_price == price(asset(300), bitusd.amount(100)) );
   BOOST_CHECK(index.find(boost::make_tuple(shorter2_account.id, bitusd.id)) != index.end());
   BOOST_CHECK(index.find(boost::make_tuple(shorter2_account.id, bitusd.id))->get_debt() == bitusd.amount(100) );
   BOOST_CHECK(index.find(boost::make_tuple(shorter3_account.id, bitusd.id)) != index.end());
   BOOST_CHECK(index.find(boost::make_tuple(shorter3_account.id, bitusd.id))->get_debt() == bitusd.amount(100) );
}catch ( const fc::exception& e )
{
  elog( "${e}", ("e", e.to_detail_string() ) );
  throw;
} }

BOOST_AUTO_TEST_CASE( full_cover_test )
{
   try {
      INVOKE(multiple_shorts_matching_multiple_bids_in_order);
      const asset_object& bit_usd = get_asset("BITUSD");
      const asset_object& core = asset_id_type()(db);
      const account_object& debt_holder = get_account("shorter1");
      const account_object& usd_holder = get_account("buyer");
      auto& index = db.get_index_type<call_order_index>().indices().get<by_account>();

      BOOST_CHECK(index.find(boost::make_tuple(debt_holder.id, bit_usd.id)) != index.end());

      transfer(usd_holder, debt_holder, bit_usd.amount(100), bit_usd.amount(0));

      call_order_update_operation op;
      op.funding_account = debt_holder.id;
      op.collateral_to_add = core.amount(-400);
      op.amount_to_cover = bit_usd.amount(100);

      trx.operations.push_back(op);
      REQUIRE_THROW_WITH_VALUE(op, funding_account, usd_holder.id);
      REQUIRE_THROW_WITH_VALUE(op, amount_to_cover, bit_usd.amount(-20));
      REQUIRE_THROW_WITH_VALUE(op, amount_to_cover, bit_usd.amount(200));
      REQUIRE_THROW_WITH_VALUE(op, collateral_to_add, core.amount(GRAPHENE_INITIAL_SUPPLY));
      REQUIRE_THROW_WITH_VALUE(op, collateral_to_add, bit_usd.amount(20));
      REQUIRE_THROW_WITH_VALUE(op, maintenance_collateral_ratio, 2);
      trx.operations.back() = op;
      db.push_transaction(trx, ~0);

      BOOST_CHECK_EQUAL(get_balance(debt_holder, bit_usd), 0);
      BOOST_CHECK(index.find(boost::make_tuple(debt_holder.id, bit_usd.id)) == index.end());
   } catch( fc::exception& e) {
      edump((e.to_detail_string()));
      throw;
   }
}

BOOST_AUTO_TEST_CASE( partial_cover_test )
{
   try {
      INVOKE(multiple_shorts_matching_multiple_bids_in_order);
      const asset_object& bit_usd = get_asset("BITUSD");
      const asset_object& core = asset_id_type()(db);
      const account_object& debt_holder = get_account("shorter1");
      const account_object& usd_holder = get_account("buyer");
      auto& index = db.get_index_type<call_order_index>().indices().get<by_account>();
      const call_order_object& debt = *index.find(boost::make_tuple(debt_holder.id, bit_usd.id));

      BOOST_CHECK(index.find(boost::make_tuple(debt_holder.id, bit_usd.id)) != index.end());

      ilog("..." );
      transfer(usd_holder, debt_holder, bit_usd.amount(50), bit_usd.amount(0));
      ilog("..." );
      BOOST_CHECK_EQUAL(get_balance(debt_holder, bit_usd), 50);

      trx.operations.clear();
      call_order_update_operation op;
      op.funding_account = debt_holder.id;
      op.collateral_to_add = core.amount(0);
      op.amount_to_cover = bit_usd.amount(50);
      trx.operations.push_back(op);
      db.push_transaction(trx, ~0);

      BOOST_CHECK_EQUAL(get_balance(debt_holder, bit_usd), 0);
      BOOST_CHECK(index.find(boost::make_tuple(debt_holder.id, bit_usd.id)) != index.end());
      BOOST_CHECK_EQUAL(debt.debt.value, 50);
      BOOST_CHECK_EQUAL(debt.collateral.value, 400);
      BOOST_CHECK(debt.call_price == price(core.amount(300), bit_usd.amount(50)));

      op.collateral_to_add = core.amount(52);
      op.amount_to_cover = bit_usd.amount(0);
      trx.operations.back() = op;
      db.push_transaction(trx, ~0);
      ilog("..." );

      BOOST_CHECK(debt.call_price == price(core.amount(339), bit_usd.amount(50)));

      op.collateral_to_add = core.amount(0);
      op.amount_to_cover = bit_usd.amount(0);
      op.maintenance_collateral_ratio = 1800;
      REQUIRE_THROW_WITH_VALUE(op, maintenance_collateral_ratio, 1300);
      REQUIRE_THROW_WITH_VALUE(op, maintenance_collateral_ratio, 2500);
      op.collateral_to_add = core.amount(8);
      trx.operations.back() = op;
      db.push_transaction(trx, ~0);

      BOOST_CHECK(debt.call_price == price(core.amount(368), bit_usd.amount(50)));

      op.amount_to_cover = bit_usd.amount(50);
      op.collateral_to_add.amount = 0;
      trx.operations.back() = op;
      BOOST_CHECK_EQUAL(get_balance(debt_holder, bit_usd), 0);
      BOOST_CHECK_THROW(db.push_transaction(trx, ~0), fc::exception);

      trx.operations.clear();
      ilog("..." );
      transfer(usd_holder, debt_holder, bit_usd.amount(50), bit_usd.amount(0));
      trx.operations.clear();
      op.collateral_to_add.amount = -460;
      op.validate();
      ilog("..." );
      trx.operations.push_back(op);
      db.push_transaction(trx, ~0);

      BOOST_CHECK(index.find(boost::make_tuple(debt_holder.id, bit_usd.id)) == index.end());
   } catch( fc::exception& e) {
      edump((e.to_detail_string()));
      throw;
   }
}

BOOST_AUTO_TEST_CASE( limit_order_matching_mix_of_shorts_and_limits )
{ try {
   const asset_object& bitusd      = create_bitasset( "BITUSD" );
   const asset_object& core         = get_asset( GRAPHENE_SYMBOL );
   const account_object& shorter1  = create_account( "shorter1" );
   const account_object& shorter2  = create_account( "shorter2" );
   const account_object& shorter3  = create_account( "shorter3" );
   const account_object& buyer1    = create_account( "buyer1" );
   const account_object& buyer2    = create_account( "buyer2" );
   const account_object& buyer3    = create_account( "buyer3" );

   transfer( genesis_account(db), shorter1, core.amount( 10000 ) );
   transfer( genesis_account(db), shorter2, core.amount( 10000 ) );
   transfer( genesis_account(db), shorter3, core.amount( 10000 ) );
   transfer( genesis_account(db), buyer1, core.amount( 10000 ) );
   transfer( genesis_account(db), buyer2, core.amount( 10000 ) );
   transfer( genesis_account(db), buyer3, core.amount( 10000 ) );

   // create some BitUSD
   BOOST_REQUIRE( create_sell_order( buyer1, core.amount(1000), bitusd.amount(1000) ) );
   BOOST_REQUIRE( !create_short( shorter1, bitusd.amount(1000), core.amount(1000) )   );
   BOOST_REQUIRE_EQUAL( get_balance(buyer1, bitusd), 990 ); // 1000 - 1% fee

   // create a mixture of BitUSD sells and shorts
   BOOST_REQUIRE( create_short(      shorter1, bitusd.amount(100), core.amount(125) )   );
   BOOST_REQUIRE( create_sell_order( buyer1,   bitusd.amount(100), core.amount(150) )   );
   BOOST_REQUIRE( create_short(      shorter2, bitusd.amount(100), core.amount(200) )   );
   BOOST_REQUIRE( create_sell_order( buyer1,   bitusd.amount(100), core.amount(225) )   );
   BOOST_REQUIRE( create_short(      shorter3, bitusd.amount(100), core.amount(250) )   );

   print_joint_market("",""); // may have bugs

   // buy up everything but the highest order
   auto unfilled_order = create_sell_order( buyer2, core.amount(700), bitusd.amount(311) );
   if( unfilled_order ) wdump((*unfilled_order));
   print_joint_market("","");
   if( unfilled_order ) wdump((*unfilled_order));
   BOOST_REQUIRE( !unfilled_order );
   BOOST_REQUIRE_EQUAL( get_balance(buyer2, bitusd), 396 );

   print_joint_market("","");
   print_call_orders();

}catch ( const fc::exception& e )
{
  elog( "${e}", ("e", e.to_detail_string() ) );
  throw;
} }

BOOST_AUTO_TEST_CASE( big_short )
{
   try {
      const asset_object& bitusd      = create_bitasset( "BITUSD" );
      const asset_object& core         = get_asset( GRAPHENE_SYMBOL );
      const account_object& shorter1  = create_account( "shorter1" );
      const account_object& buyer1    = create_account( "buyer1" );
      const account_object& buyer2    = create_account( "buyer2" );
      const account_object& buyer3    = create_account( "buyer3" );

      transfer( genesis_account(db), shorter1, asset( 10000 ) );
      transfer( genesis_account(db), buyer1, asset( 10000 ) );
      transfer( genesis_account(db), buyer2, asset( 10000 ) );
      transfer( genesis_account(db), buyer3, asset( 10000 ) );

      create_sell_order(buyer1, core.amount(500), bitusd.amount(500));
      create_sell_order(buyer2, core.amount(500), bitusd.amount(600));
      auto unmatched_buy3 = create_sell_order(buyer3, core.amount(500), bitusd.amount(700));

      auto unmatched = create_short(shorter1, bitusd.amount(1300), core.amount(800));
      if( unmatched ) wdump((*unmatched));

      BOOST_CHECK( !unmatched );
      BOOST_CHECK( unmatched_buy3 );
      BOOST_CHECK_EQUAL( unmatched_buy3->amount_for_sale().amount.value, 358);
      // The extra 1 is rounding leftovers; it has to go somewhere.
      BOOST_CHECK_EQUAL( unmatched_buy3->amount_to_receive().amount.value, 501);
      // All three buyers offered 500 CORE for varying numbers of dollars.
      BOOST_CHECK_EQUAL(get_balance(buyer1, core), 9500);
      BOOST_CHECK_EQUAL(get_balance(buyer2, core), 9500);
      BOOST_CHECK_EQUAL(get_balance(buyer3, core), 9500);
      // Sans the 1% market fee, buyer1 got 500 USD, buyer2 got 600 USD
      BOOST_CHECK_EQUAL(get_balance(buyer1, bitusd), 495);
      BOOST_CHECK_EQUAL(get_balance(buyer2, bitusd), 594);
      // Buyer3 wanted 700 USD, but the shorter only had 1300-500-600=200 left, so buyer3 got 200.
      BOOST_CHECK_EQUAL(get_balance(buyer3, bitusd), 198);
      // Shorter1 never had any USD, so he shouldn't have any now. He paid 800 CORE, so he should have 9200 left.
      BOOST_CHECK_EQUAL(get_balance(shorter1, bitusd), 0);
      BOOST_CHECK_EQUAL(get_balance(shorter1, core), 9200);

      const auto& call_index = db.get_index_type<call_order_index>().indices().get<by_account>();
      const auto call_itr = call_index.find(boost::make_tuple(shorter1.id, bitusd.id));
      BOOST_CHECK(call_itr != call_index.end());
      const call_order_object& call_object = *call_itr;
      BOOST_CHECK(call_object.borrower == shorter1.id);
      //  800 from shorter1, 500 from buyer1 and buyer2 each, 500/700*200 from buyer3 totals 1942
      BOOST_CHECK_EQUAL(call_object.collateral.value, 1942);
      // Shorter1 sold 1300 USD. Make sure that's recorded accurately.
      BOOST_CHECK_EQUAL(call_object.debt.value, 1300);
      // 13 USD was paid in market fees.
      BOOST_CHECK_EQUAL(bitusd.dynamic_asset_data_id(db).accumulated_fees.value, 13);
   } catch( const fc::exception& e) {
      edump((e.to_detail_string()));
      throw;
   }
}

BOOST_AUTO_TEST_CASE( big_short2 )
{
   try {
      const asset_object& bitusd      = create_bitasset( "BITUSD" );
      const asset_object& core         = get_asset( GRAPHENE_SYMBOL );
      const account_object& shorter1  = create_account( "shorter1" );
      const account_object& buyer1    = create_account( "buyer1" );
      const account_object& buyer2    = create_account( "buyer2" );
      const account_object& buyer3    = create_account( "buyer3" );

      transfer( genesis_account(db), shorter1, asset( 10000 ) );
      transfer( genesis_account(db), buyer1, asset( 10000 ) );
      transfer( genesis_account(db), buyer2, asset( 10000 ) );
      transfer( genesis_account(db), buyer3, asset( 10000 ) );

      create_sell_order(buyer1, core.amount(500), bitusd.amount(500));
      create_sell_order(buyer2, core.amount(500), bitusd.amount(600));
      auto unmatched_buy3 = create_sell_order(buyer3, core.amount(500), bitusd.amount(700));

      //We want to perfectly match the first two orders, so that's 1100 USD at 500/600 = 916
      auto unmatched = create_short(shorter1, bitusd.amount(1100), core.amount(916));
      if( unmatched ) wdump((*unmatched));

      BOOST_CHECK( !unmatched );
      BOOST_CHECK( unmatched_buy3 );
      BOOST_CHECK_EQUAL( unmatched_buy3->amount_for_sale().amount.value, 500);
      // The extra 1 is rounding leftovers; it has to go somewhere.
      BOOST_CHECK_EQUAL( unmatched_buy3->amount_to_receive().amount.value, 700);
      // All three buyers offered 500 CORE for varying numbers of dollars.
      BOOST_CHECK_EQUAL(get_balance(buyer1, core), 9500);
      BOOST_CHECK_EQUAL(get_balance(buyer2, core), 9500);
      BOOST_CHECK_EQUAL(get_balance(buyer3, core), 9500);
      // Sans the 1% market fee, buyer1 got 500 USD, buyer2 got 600 USD
      BOOST_CHECK_EQUAL(get_balance(buyer1, bitusd), 495);
      BOOST_CHECK_EQUAL(get_balance(buyer2, bitusd), 594);
      // Buyer3's order wasn't matched. He should have no USD.
      BOOST_CHECK_EQUAL(get_balance(buyer3, bitusd), 0);
      // Shorter1 never had any USD, so he shouldn't have any now. He paid 916 CORE, so he should have 9084 left.
      BOOST_CHECK_EQUAL(get_balance(shorter1, bitusd), 0);
      BOOST_CHECK_EQUAL(get_balance(shorter1, core), 9084);

      const auto& call_index = db.get_index_type<call_order_index>().indices().get<by_account>();
      const auto call_itr = call_index.find(boost::make_tuple(shorter1.id, bitusd.id));
      BOOST_CHECK(call_itr != call_index.end());
      const call_order_object& call_object = *call_itr;
      BOOST_CHECK(call_object.borrower == shorter1.id);
      // 916 from shorter1, 500 from buyer1 and buyer2 each adds to 1916
      BOOST_CHECK_EQUAL(call_object.collateral.value, 1916);
      // Shorter1 sold 1100 USD. Make sure that's recorded accurately.
      BOOST_CHECK_EQUAL(call_object.debt.value, 1100);
      // 11 USD was paid in market fees.
      BOOST_CHECK_EQUAL(bitusd.dynamic_asset_data_id(db).accumulated_fees.value, 11);
   } catch( const fc::exception& e) {
      edump((e.to_detail_string()));
      throw;
   }
}

BOOST_AUTO_TEST_CASE( big_short3 )
{
   try {
      const asset_object& bitusd      = create_bitasset( "BITUSD" );
      const asset_object& core         = get_asset( GRAPHENE_SYMBOL );
      const account_object& shorter1  = create_account( "shorter1" );
      const account_object& buyer1    = create_account( "buyer1" );
      const account_object& buyer2    = create_account( "buyer2" );
      const account_object& buyer3    = create_account( "buyer3" );

      transfer( genesis_account(db), shorter1, asset( 10000 ) );
      transfer( genesis_account(db), buyer1, asset( 10000 ) );
      transfer( genesis_account(db), buyer2, asset( 10000 ) );
      transfer( genesis_account(db), buyer3, asset( 10000 ) );

      create_short(shorter1, bitusd.amount(1300), core.amount(800));

      print_joint_market("","");

      create_sell_order(buyer1, core.amount(500), bitusd.amount(500));
      create_sell_order(buyer2, core.amount(500), bitusd.amount(600));
      auto unmatched_buy3 = create_sell_order(buyer3, core.amount(500), bitusd.amount(700));

      print_joint_market("","");

      BOOST_CHECK( unmatched_buy3 );
      BOOST_CHECK_EQUAL( unmatched_buy3->amount_for_sale().amount.value, 500);
      BOOST_CHECK_EQUAL( unmatched_buy3->amount_to_receive().amount.value, 700);
      BOOST_CHECK_EQUAL(get_balance(buyer1, core), 9500);
      BOOST_CHECK_EQUAL(get_balance(buyer2, core), 9500);
      BOOST_CHECK_EQUAL(get_balance(buyer3, core), 9500);
      BOOST_CHECK_EQUAL(get_balance(buyer1, bitusd), 804);
      BOOST_CHECK_EQUAL(get_balance(buyer2, bitusd), 484);
      BOOST_CHECK_EQUAL(get_balance(buyer3, bitusd), 0);
      BOOST_CHECK_EQUAL(get_balance(shorter1, bitusd), 0);
      BOOST_CHECK_EQUAL(get_balance(shorter1, core), 9200);

      const auto& call_index = db.get_index_type<call_order_index>().indices().get<by_account>();
      const auto call_itr = call_index.find(boost::make_tuple(shorter1.id, bitusd.id));
      BOOST_CHECK(call_itr != call_index.end());
      const call_order_object& call_object = *call_itr;
      BOOST_CHECK(call_object.borrower == shorter1.id);
      BOOST_CHECK_EQUAL(call_object.collateral.value, 1600);
      BOOST_CHECK_EQUAL(call_object.debt.value, 1300);
      BOOST_CHECK_EQUAL(bitusd.dynamic_asset_data_id(db).accumulated_fees.value, 12);
   } catch( const fc::exception& e) {
      edump((e.to_detail_string()));
      throw;
   }
}

/**
  * Originally, this test exposed a bug in vote tallying causing the total number of votes to exceed the number of
  * voting shares. This bug was resolved in commit 489b0dafe981c3b96b17f23cfc9ddc348173c529
  */
BOOST_AUTO_TEST_CASE(break_vote_count)
{
   try {
      const asset_object& bitusd      = create_bitasset( "BITUSD" );
      const asset_object& core        = get_asset( GRAPHENE_SYMBOL );
      const account_object& shorter1  = create_account( "shorter1" );
      const account_object& buyer1    = create_account( "buyer1" );

      transfer( genesis_account(db), shorter1, asset( 100000000 ) );
      transfer( genesis_account(db), buyer1, asset( 100000000 ) );

      create_short(shorter1, bitusd.amount(1300), core.amount(800));

      create_sell_order(buyer1, core.amount(500), bitusd.amount(500));

      BOOST_CHECK_EQUAL(get_balance(buyer1, core), 99999500);
      BOOST_CHECK_EQUAL(get_balance(buyer1, bitusd), 804);
      BOOST_CHECK_EQUAL(get_balance(shorter1, bitusd), 0);
      BOOST_CHECK_EQUAL(get_balance(shorter1, core), 99999200);

      create_sell_order(shorter1, core.amount(90000000), bitusd.amount(1));
   } catch( const fc::exception& e) {
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

      ilog( "=================================== START===================================\n\n");
      create_sell_order(core_seller, core.amount(1), test.amount(900000));
      ilog( "=================================== STEP===================================\n\n");
      create_sell_order(core_buyer, test.amount(900001), core.amount(1));
   } catch( const fc::exception& e) {
      edump((e.to_detail_string()));
      throw;
   }
}

BOOST_AUTO_TEST_CASE( margin_call_limit_test )
{ try {
      const asset_object& bitusd      = create_bitasset( "BITUSD" );
      const asset_object& core         = get_asset( GRAPHENE_SYMBOL );

      db.modify( bitusd.bitasset_data(db), [&]( asset_bitasset_data_object& usd ){
                 usd.current_feed.call_limit = core.amount(3) / bitusd.amount(1);
                 });

      const account_object& shorter1  = create_account( "shorter1" );
      const account_object& shorter2  = create_account( "shorter2" );
      const account_object& buyer1    = create_account( "buyer1" );
      const account_object& buyer2    = create_account( "buyer2" );

      transfer( genesis_account(db), shorter1, asset( 10000 ) );
      transfer( genesis_account(db), shorter2, asset( 10000 ) );
      transfer( genesis_account(db), buyer1, asset( 10000 ) );
      transfer( genesis_account(db), buyer2, asset( 10000 ) );

      BOOST_REQUIRE( create_sell_order( buyer1, asset(1000), bitusd.amount(1000) ) );
      BOOST_REQUIRE( !create_short( shorter1, bitusd.amount(1000), asset(1000) )   );
      BOOST_REQUIRE_EQUAL( get_balance(buyer1, bitusd), 990 ); // 1000 - 1% fee

      const auto& call_index = db.get_index_type<call_order_index>().indices().get<by_account>();
      const auto call_itr = call_index.find(boost::make_tuple(shorter1.id, bitusd.id));
      BOOST_REQUIRE( call_itr != call_index.end() );
      const call_order_object& call = *call_itr;
      BOOST_CHECK(call.get_collateral() == core.amount(2000));
      BOOST_CHECK(call.get_debt() == bitusd.amount(1000));
      BOOST_CHECK(call.call_price == price(core.amount(1500), bitusd.amount(1000)));
      BOOST_CHECK_EQUAL(get_balance(shorter1, core), 9000);

      ilog( "=================================== START===================================\n\n");
      // this should cause the highest bid to below the margin call threshold
      // which means it should be filled by the cover
      auto unmatched = create_sell_order( buyer1, bitusd.amount(495), core.amount(750) );
      if( unmatched ) edump((*unmatched));
      BOOST_CHECK( !unmatched );
      BOOST_CHECK(call.get_debt() == bitusd.amount(505));
      BOOST_CHECK(call.get_collateral() == core.amount(1250));

      auto below_call_price = create_sell_order(buyer1, bitusd.amount(200), core.amount(1));
      BOOST_REQUIRE(below_call_price);
      auto above_call_price = create_sell_order(buyer1, bitusd.amount(200), core.amount(303));
      BOOST_REQUIRE(above_call_price);
      auto above_id = above_call_price->id;

      cancel_limit_order(*below_call_price);
      BOOST_CHECK_THROW(db.get_object(above_id), fc::exception);
      BOOST_CHECK(call.get_debt() == bitusd.amount(305));
      BOOST_CHECK(call.get_collateral() == core.amount(947));

      below_call_price = create_sell_order(buyer1, bitusd.amount(200), core.amount(1));
      BOOST_REQUIRE(below_call_price);
      auto below_id = below_call_price->id;
      above_call_price = create_sell_order(buyer1, bitusd.amount(95), core.amount(144));
      BOOST_REQUIRE(above_call_price);
      above_id = above_call_price->id;
      auto match_below_call = create_sell_order(buyer2, core.amount(1), bitusd.amount(200));
      BOOST_CHECK(!match_below_call);

      BOOST_CHECK_THROW(db.get_object(above_id), fc::exception);
      BOOST_CHECK_THROW(db.get_object(below_id), fc::exception);
      BOOST_CHECK(call.get_debt() == bitusd.amount(210));
      BOOST_CHECK(call.get_collateral() == core.amount(803));
   } catch( const fc::exception& e) {
      edump((e.to_detail_string()));
      throw;
   }
}

BOOST_AUTO_TEST_CASE( margin_call_limit_test_protected )
{ try {
      const asset_object& bitusd      = create_bitasset( "BITUSD" );
      const asset_object& core         = get_asset( GRAPHENE_SYMBOL );

      db.modify( bitusd.bitasset_data(db), [&]( asset_bitasset_data_object& usd ){
                 usd.current_feed.call_limit = core.amount(1) / bitusd.amount(1);
                 });

      const account_object& shorter1  = create_account( "shorter1" );
      const account_object& shorter2  = create_account( "shorter2" );
      const account_object& buyer1    = create_account( "buyer1" );
      const account_object& buyer2    = create_account( "buyer2" );

      transfer( genesis_account(db), shorter1, asset( 10000 ) );
      transfer( genesis_account(db), shorter2, asset( 10000 ) );
      transfer( genesis_account(db), buyer1, asset( 10000 ) );
      transfer( genesis_account(db), buyer2, asset( 10000 ) );

      BOOST_REQUIRE( create_sell_order( buyer1, asset(1000), bitusd.amount(1000) ) );
      BOOST_REQUIRE( !create_short( shorter1, bitusd.amount(1000), asset(1000) )   );
      BOOST_REQUIRE_EQUAL( get_balance(buyer1, bitusd), 990 ); // 1000 - 1% fee

      ilog( "=================================== START===================================\n\n");
      // this should cause the highest bid to below the margin call threshold
      // which means it should be filled by the cover
      auto unmatched = create_sell_order( buyer1, bitusd.amount(990), core.amount(1500) );
      if( unmatched ) edump((*unmatched));
      BOOST_REQUIRE( unmatched );

   } catch( const fc::exception& e) {
      edump((e.to_detail_string()));
      throw;
   }
}

BOOST_AUTO_TEST_CASE( dont_margin_call_limit_test )
{ try {
      const asset_object& bitusd      = create_bitasset( "BITUSD" );
      const asset_object& core         = get_asset( GRAPHENE_SYMBOL );

      db.modify( bitusd.bitasset_data(db), [&]( asset_bitasset_data_object& usd ){
                 usd.current_feed.call_limit = core.amount(3) / bitusd.amount(1);
                 });

      const account_object& shorter1  = create_account( "shorter1" );
      const account_object& shorter2  = create_account( "shorter2" );
      const account_object& buyer1    = create_account( "buyer1" );
      const account_object& buyer2    = create_account( "buyer2" );

      transfer( genesis_account(db), shorter1, asset( 10000 ) );
      transfer( genesis_account(db), shorter2, asset( 10000 ) );
      transfer( genesis_account(db), buyer1, asset( 10000 ) );
      transfer( genesis_account(db), buyer2, asset( 10000 ) );

      BOOST_REQUIRE( create_sell_order( buyer1, asset(1000), bitusd.amount(1000) ) );
      BOOST_REQUIRE( !create_short( shorter1, bitusd.amount(1000), asset(1000) )   );
      BOOST_REQUIRE_EQUAL( get_balance(buyer1, bitusd), 990 ); // 1000 - 1% fee

      // this should cause the highest bid to below the margin call threshold
      // which means it should be filled by the cover
      auto unmatched = create_sell_order( buyer1, bitusd.amount(990), core.amount(1100) );
      if( unmatched ) edump((*unmatched));
      BOOST_REQUIRE( unmatched );

   } catch( const fc::exception& e) {
      edump((e.to_detail_string()));
      throw;
   }
}

BOOST_AUTO_TEST_CASE( margin_call_short_test )
{ try {
      const asset_object& bitusd      = create_bitasset( "BITUSD" );
      const asset_object& core         = get_asset( GRAPHENE_SYMBOL );

      db.modify( bitusd.bitasset_data(db), [&]( asset_bitasset_data_object& usd ){
                 usd.current_feed.call_limit = core.amount(3) / bitusd.amount(1);
                 });

      const account_object& shorter1  = create_account( "shorter1" );
      const account_object& shorter2  = create_account( "shorter2" );
      const account_object& buyer1    = create_account( "buyer1" );
      const account_object& buyer2    = create_account( "buyer2" );

      transfer( genesis_account(db), shorter1, asset( 10000 ) );
      transfer( genesis_account(db), shorter2, asset( 10000 ) );
      transfer( genesis_account(db), buyer1, asset( 10000 ) );
      transfer( genesis_account(db), buyer2, asset( 10000 ) );

      BOOST_REQUIRE( create_sell_order( buyer1, asset(1000), bitusd.amount(1000) ) );
      BOOST_REQUIRE( !create_short( shorter1, bitusd.amount(1000), asset(1000) )   );
      BOOST_REQUIRE_EQUAL( get_balance(buyer1, bitusd), 990 ); // 1000 - 1% fee
      ilog( "=================================== START===================================\n\n");

      // this should cause the highest bid to below the margin call threshold
      // which means it should be filled by the cover
      auto unmatched = create_short( buyer1, bitusd.amount(990), core.amount(1500) );
      if( unmatched ) edump((*unmatched));
      BOOST_REQUIRE( !unmatched );

   } catch( const fc::exception& e) {
      edump((e.to_detail_string()));
      throw;
   }
}

BOOST_AUTO_TEST_CASE( margin_call_short_test_limit_protected )
{ try {
      const asset_object& bitusd      = create_bitasset( "BITUSD" );
      const asset_object& core         = get_asset( GRAPHENE_SYMBOL );

      db.modify( bitusd.bitasset_data(db), [&]( asset_bitasset_data_object& usd ){
                 usd.current_feed.call_limit = core.amount(3) / bitusd.amount(4);
                 });

      const account_object& shorter1  = create_account( "shorter1" );
      const account_object& shorter2  = create_account( "shorter2" );
      const account_object& buyer1    = create_account( "buyer1" );
      const account_object& buyer2    = create_account( "buyer2" );

      transfer( genesis_account(db), shorter1, asset( 10000 ) );
      transfer( genesis_account(db), shorter2, asset( 10000 ) );
      transfer( genesis_account(db), buyer1, asset( 10000 ) );
      transfer( genesis_account(db), buyer2, asset( 10000 ) );

      BOOST_REQUIRE( create_sell_order( buyer1, asset(1000), bitusd.amount(1000) ) );
      BOOST_REQUIRE( !create_short( shorter1, bitusd.amount(1000), asset(1000) )   );
      BOOST_REQUIRE_EQUAL( get_balance(buyer1, bitusd), 990 ); // 1000 - 1% fee
      ilog( "=================================== START===================================\n\n");

      // this should cause the highest bid to below the margin call threshold
      // which means it should be filled by the cover
      auto unmatched = create_short( buyer1, bitusd.amount(990), core.amount(1500) );
      if( unmatched ) edump((*unmatched));
      BOOST_REQUIRE( unmatched );

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
   BOOST_CHECK_THROW(db.push_transaction(trx, ~0), fc::exception);
   op.fill_or_kill = false;
   trx.operations.back() = op;
   db.push_transaction(trx, ~0);
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

   // budget should be 25 satoshis based on 30 blocks at 5-second interval
   // with 17 / 2**32 rate per block
   const int ref_budget = 125;
   // set to a value which will exhaust ref_budget after three witnesses
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
   enable_fees(100000000);
   BOOST_CHECK_GT(db.current_fee_schedule().at(prime_upgrade_fee_type).value, 0);

   BOOST_CHECK_EQUAL(core->dynamic_asset_data_id(db).accumulated_fees.value, 0);
   account_update_operation uop;
   uop.account = nathan->get_id();
   uop.upgrade_to_prime = true;
   trx.set_expiration(db.head_block_id());
   trx.operations.push_back(uop);
   trx.visit(operation_set_fee(db.current_fee_schedule()));
   trx.validate();
   trx.sign(key_id_type(),generate_private_key("genesis"));
   db.push_transaction(trx);
   trx.clear();
   BOOST_CHECK_EQUAL(get_balance(*nathan, *core), 9000000000);
   BOOST_CHECK_EQUAL(core->dynamic_asset_data_id(db).accumulated_fees.value, 210000000);
   // TODO:  Replace this with another check
   //BOOST_CHECK_EQUAL(account_id_type()(db).statistics(db).cashback_rewards.value, 1000000000-210000000);

   generate_block();
   nathan = &get_account("nathan");
   core = &asset_id_type()(db);
   const witness_object* witness = &db.fetch_block_by_number(db.head_block_num())->witness(db);

   BOOST_CHECK_GT(core->dynamic_asset_data_id(db).accumulated_fees.value, 0);
   BOOST_CHECK_EQUAL(witness->accumulated_income.value, 0);

   auto schedule_maint = [&]()
   {
      // now we do maintenance
      db.modify( db.get_dynamic_global_properties(), [&]( dynamic_global_property_object& _dpo )
      {
         _dpo.next_maintenance_time = db.head_block_time() + 1;
      } );
   } ;

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
   // first witness paid from old budget (so no pay)
   BOOST_CHECK_EQUAL( core->burned(db).value, 0 );
   generate_block();
   BOOST_CHECK_EQUAL( core->burned(db).value, 210000000 - ref_budget );
   BOOST_CHECK_EQUAL( db.get_dynamic_global_properties().witness_budget.value, ref_budget );
   witness = &db.fetch_block_by_number(db.head_block_num())->witness(db);
   BOOST_CHECK_EQUAL( witness->accumulated_income.value, 0 );
   // second witness finally gets paid!
   generate_block();
   witness = &db.fetch_block_by_number(db.head_block_num())->witness(db);
   BOOST_CHECK_EQUAL( witness->accumulated_income.value, witness_ppb );
   BOOST_CHECK_EQUAL( db.get_dynamic_global_properties().witness_budget.value, ref_budget - witness_ppb );
   const witness_object* paid_witness = witness;

   // full payment to next witness
   generate_block();
   witness = &db.fetch_block_by_number(db.head_block_num())->witness(db);
   BOOST_CHECK_EQUAL( witness->accumulated_income.value, witness_ppb );
   BOOST_CHECK_EQUAL( db.get_dynamic_global_properties().witness_budget.value, ref_budget - 2 * witness_ppb );

   // partial payment to last witness
   generate_block();
   witness = &db.fetch_block_by_number(db.head_block_num())->witness(db);
   BOOST_CHECK_EQUAL( witness->accumulated_income.value, ref_budget - 2 * witness_ppb );
   BOOST_CHECK_EQUAL( db.get_dynamic_global_properties().witness_budget.value, 0 );

   generate_block();
   witness = &db.fetch_block_by_number(db.head_block_num())->witness(db);
   BOOST_CHECK_EQUAL( witness->accumulated_income.value, 0 );
   BOOST_CHECK_EQUAL( db.get_dynamic_global_properties().witness_budget.value, 0 );

   trx.set_expiration(db.head_block_time() + GRAPHENE_DEFAULT_MAX_TIME_UNTIL_EXPIRATION);
   // last one was unpaid, so pull out a paid one for checks
   witness = paid_witness;
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
   trx.sign(key_id_type(),generate_private_key("genesis"));
   db.push_transaction(trx);
   trx.clear();

   BOOST_CHECK_EQUAL(get_balance(witness->witness_account(db), *core), witness_ppb - 1/*fee*/);
   BOOST_CHECK_EQUAL(core->burned(db).value, 210000000 - ref_budget );
   BOOST_CHECK_EQUAL(witness->accumulated_income.value, 0);
} FC_LOG_AND_RETHROW() }

/**
 *  To have a secure random number we need to ensure that the same
 *  delegate does not get to produce two blocks in a row.  There is
 *  always a chance that the last delegate of one round will be the
 *  first delegate of the next round.
 *
 *  This means that when we shuffle delegates we need to make sure
 *  that there is at least N/2 delegates between consecutive turns
 *  of the same delegate.    This means that durring the random
 *  shuffle we need to restrict the placement of delegates to maintain
 *  this invariant.
 */
BOOST_AUTO_TEST_CASE_EXPECTED_FAILURES( unimp_delegate_groups_test, 1 )
BOOST_AUTO_TEST_CASE( unimp_delegate_groups_test )
{
   BOOST_FAIL( "not implemented" );
}


/**
 * This test should simulate a prediction market which means the following:
 *
 * 1) Issue a BitAsset without Forced Settling & With Global Settling
 * 2) Don't Publish any Price Feeds
 * 3) Ensure that margin calls do not occur even if the highest bid would indicate it
 * 4) Match some Orders
 * 5) Trigger Global Settle on the Asset
 * 6) The maitenance collateral must always be 1:1
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
BOOST_AUTO_TEST_CASE_EXPECTED_FAILURES( unimp_global_settle_test, 1 )
BOOST_AUTO_TEST_CASE( unimp_global_settle_test )
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
 *  This test sets up the minimum condition for a black swan to occur but does
 *  not test the full range of cases that may be possible during a black swan.
 */
BOOST_AUTO_TEST_CASE( margin_call_black_swan )
{ try {
      const asset_object& bitusd      = create_bitasset( "BITUSD" );
      const asset_object& core         = get_asset( GRAPHENE_SYMBOL );

      db.modify( bitusd.bitasset_data(db), [&]( asset_bitasset_data_object& usd ){
                 usd.current_feed.call_limit = core.amount(30) / bitusd.amount(1);
                 });

      const account_object& shorter1  = create_account( "shorter1" );
      const account_object& shorter2  = create_account( "shorter2" );
      const account_object& buyer1    = create_account( "buyer1" );
      const account_object& buyer2    = create_account( "buyer2" );

      transfer( genesis_account(db), shorter1, asset( 10000 ) );
      transfer( genesis_account(db), shorter2, asset( 10000 ) );
      transfer( genesis_account(db), buyer1, asset( 10000 ) );
      transfer( genesis_account(db), buyer2, asset( 10000 ) );

      BOOST_REQUIRE( create_sell_order( buyer1, asset(1000), bitusd.amount(1000) ) );
      BOOST_REQUIRE( !create_short( shorter1, bitusd.amount(1000), asset(1000) )   );
      BOOST_REQUIRE_EQUAL( get_balance(buyer1, bitusd), 990 ); // 1000 - 1% fee

      verify_asset_supplies();
      ilog( "=================================== START===================================\n\n");
      // this should cause the highest bid to below the margin call threshold
      // which means it should be filled by the cover, except the cover does not
      // have enough collateral and thus a black swan event should occur.
      auto unmatched = create_sell_order( buyer1, bitusd.amount(990), core.amount(5000) );
      if( unmatched ) edump((*unmatched));
      /** black swans should cause all of the bitusd to be converted into backing
       * asset at the price of the least collateralized call position at the time. This
       * means that this sell order would be removed.
       */
      BOOST_REQUIRE( !unmatched );

   } catch( const fc::exception& e) {
      edump((e.to_detail_string()));
      throw;
   }
}
/**
 *  This test sets up a far more complex blackswan scenerio where the
 *  BitUSD exists in the following places:
 *
 *  0) Limit Orders for the BitAsset
 *  1) Limit Orders for UIA Assets
 *  2) Short Orders for BitAsset backed by BitUSD
 *  3) Call Orders for BitAsset backed by BitUSD
 *  4) Issuer Fees
 *  5) Bond Market Collateral
 *
 *  This test should fail until the black swan handling code can
 *  perform a recursive blackswan for any other BitAssets that use
 *  BitUSD as collateral.
 */
BOOST_AUTO_TEST_CASE_EXPECTED_FAILURES( unimp_advanced_black_swan, 1 )
BOOST_AUTO_TEST_CASE( unimp_advanced_black_swan )
{
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
   upgrade_to_prime(sam);

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

BOOST_AUTO_TEST_CASE( bond_create_offer_test )
{ try {
   bond_create_offer_operation op;
   op.fee = asset( 0, 0 );
   op.creator = account_id_type();
   op.amount = asset( 1, 0 );
   op.collateral_rate = price( asset( 1, 0 ), asset( 1, 1 ) );
   op.min_loan_period_sec = 1;
   op.loan_period_sec = 1;

   // Fee must be non-negative
   REQUIRE_OP_VALIDATION_SUCCESS( op, fee, asset( 1, 0 ) );
   REQUIRE_OP_VALIDATION_SUCCESS( op, fee, asset( 0, 0 ) );
   REQUIRE_OP_VALIDATION_FAILURE( op, fee, asset( -1, 0 ) );

   // Amount must be positive
   REQUIRE_OP_VALIDATION_SUCCESS( op, amount, asset( 1, 0 ) );
   REQUIRE_OP_VALIDATION_FAILURE( op, amount, asset( 0, 0 ) );
   REQUIRE_OP_VALIDATION_FAILURE( op, amount, asset( -1, 0 ) );

   // Collateral rate must be valid
   REQUIRE_OP_VALIDATION_SUCCESS( op, collateral_rate, price( asset( 1, 0 ), asset( 1, 1 ) ) );
   REQUIRE_OP_VALIDATION_FAILURE( op, collateral_rate, price( asset( 0, 0 ), asset( 1, 1 ) ) );
   REQUIRE_OP_VALIDATION_FAILURE( op, collateral_rate, price( asset( 1, 0 ), asset( 0, 1 ) ) );
   REQUIRE_OP_VALIDATION_FAILURE( op, collateral_rate, price( asset( 1, 0 ), asset( 1, 0 ) ) );

   // Min loan period must be at least 1 sec
   REQUIRE_OP_VALIDATION_SUCCESS( op, min_loan_period_sec, 1 );
   REQUIRE_OP_VALIDATION_FAILURE( op, min_loan_period_sec, 0 );

   // Loan period must be greater than min load period
   REQUIRE_OP_VALIDATION_SUCCESS( op, loan_period_sec, op.min_loan_period_sec + 1 );
   REQUIRE_OP_VALIDATION_FAILURE( op, loan_period_sec, 0 );

   // Interest APR cannot be greater than max
   REQUIRE_OP_VALIDATION_FAILURE( op, interest_apr, GRAPHENE_MAX_INTEREST_APR + 1 );
   REQUIRE_OP_VALIDATION_SUCCESS( op, interest_apr, GRAPHENE_MAX_INTEREST_APR );
   REQUIRE_OP_VALIDATION_SUCCESS( op, interest_apr, 0 );

   // Setup world state we will need to test actual evaluation
   INVOKE( create_uia );
   const auto& test_asset = get_asset( "TEST" );
   const auto& nathan_account = create_account( "nathan" );
   transfer( account_id_type()( db ), nathan_account, asset( 1, 0 ) );

   op.creator = nathan_account.get_id();
   op.collateral_rate.quote.asset_id = test_asset.get_id();
   trx.operations.emplace_back( op );

   // Insufficient funds in creator account
   REQUIRE_THROW_WITH_VALUE( op, creator, account_id_type( 1 ) );

   // Insufficient principle
   REQUIRE_THROW_WITH_VALUE( op, amount, asset( 2, 0 ) );

   // Insufficient collateral
   op.offer_to_borrow = true;
   REQUIRE_THROW_WITH_VALUE( op, amount, asset( 1, test_asset.get_id() ) );

   // This op should be fully valid
   REQUIRE_OP_EVALUATION_SUCCESS( op, offer_to_borrow, false );
} FC_LOG_AND_RETHROW() }

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

      processed_transaction ptx = db.push_transaction( tx, ~0 );
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
