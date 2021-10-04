/*
 * Copyright (c) 2017 Cryptonomex, Inc., and contributors.
 *
 * The MIT License
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include <boost/test/unit_test.hpp>

#include <graphene/chain/database.hpp>
#include <graphene/chain/exceptions.hpp>
#include <graphene/chain/hardfork.hpp>
#include <graphene/chain/is_authorized_asset.hpp>

#include <graphene/chain/account_object.hpp>
#include <graphene/chain/asset_object.hpp>
#include <graphene/chain/market_object.hpp>

#include <graphene/app/database_api.hpp>

#include <fc/crypto/digest.hpp>

#include "../common/database_fixture.hpp"

using namespace graphene::chain;
using namespace graphene::chain::test;

namespace graphene { namespace chain {

struct swan_fixture : database_fixture {
    limit_order_id_type init_standard_swan(share_type amount = 1000, bool disable_bidding = false) {
        standard_users();
        standard_asset(disable_bidding);
        return trigger_swan(amount, amount);
    }

    void standard_users() {
        set_expiration( db, trx );
        ACTORS((borrower)(borrower2)(feedproducer));
        _borrower = borrower_id;
        _borrower2 = borrower2_id;
        _feedproducer = feedproducer_id;

        transfer(committee_account, borrower_id, asset(init_balance));
        transfer(committee_account, borrower2_id, asset(init_balance));
    }

    void standard_asset(bool disable_bidding = false) {
        set_expiration( db, trx );
        const asset_object* bitusd_ptr;
        if( !disable_bidding )
           bitusd_ptr = &create_bitasset("USDBIT", _feedproducer);
        else
        {
           auto cop = make_bitasset("USDBIT", _feedproducer);
           cop.common_options.flags |= disable_collateral_bidding;
           trx.operations.clear();
           trx.operations.push_back(cop);
           trx.validate();
           processed_transaction ptx = PUSH_TX(db, trx, ~0);
           trx.operations.clear();
           bitusd_ptr = &db.get<asset_object>(ptx.operation_results[0].get<object_id_type>());
        }
        const auto& bitusd = *bitusd_ptr;
        _swan = bitusd.id;
        _back = asset_id_type();
        update_feed_producers(swan(), {_feedproducer});
    }

    limit_order_id_type trigger_swan(share_type amount1, share_type amount2) {
        set_expiration( db, trx );
        // starting out with price 1:1
        set_feed( 1, 1 );
        // start out with 2:1 collateral
        borrow(borrower(), swan().amount(amount1), back().amount(2*amount1));
        borrow(borrower2(), swan().amount(amount2), back().amount(4*amount2));

        FC_ASSERT( get_balance(borrower(),  swan()) == amount1 );
        FC_ASSERT( get_balance(borrower2(), swan()) == amount2 );
        FC_ASSERT( get_balance(borrower() , back()) == init_balance - 2*amount1 );
        FC_ASSERT( get_balance(borrower2(), back()) == init_balance - 4*amount2 );

        set_feed( 1, 2 );
        // this sell order is designed to trigger a black swan
        limit_order_id_type oid = create_sell_order( borrower2(), swan().amount(1), back().amount(3) )->id;

        FC_ASSERT( get_balance(borrower(),  swan()) == amount1 );
        FC_ASSERT( get_balance(borrower2(), swan()) == amount2 - 1 );
        FC_ASSERT( get_balance(borrower() , back()) == init_balance - 2*amount1 );
        if( !hf_core_2481_passed() )
           FC_ASSERT( get_balance(borrower2(), back()) == init_balance - 2*amount2 );
        else
        {
           auto mssr = swan().bitasset_data(db).current_feed.maximum_short_squeeze_ratio;
           auto denom = GRAPHENE_COLLATERAL_RATIO_DENOM;
           FC_ASSERT( get_balance(borrower2(), back()) == init_balance - (2*amount2*denom+mssr-1)/mssr);
        }

        BOOST_CHECK( swan().bitasset_data(db).has_settlement() );

        return oid;
    }

    void set_feed(share_type usd, share_type core) {
        price_feed feed;
        feed.maintenance_collateral_ratio = 1750; // need to set this explicitly, testnet has a different default
        feed.settlement_price = swan().amount(usd) / back().amount(core);
        publish_feed(swan(), feedproducer(), feed);
    }

    void expire_feed() {
      generate_blocks(db.head_block_time() + GRAPHENE_DEFAULT_PRICE_FEED_LIFETIME);
      generate_block();
      FC_ASSERT( swan().bitasset_data(db).current_feed.settlement_price.is_null() );
    }

    void wait_for_hf_core_216() {
      generate_blocks( HARDFORK_CORE_216_TIME );
      generate_block();
    }
    void wait_for_hf_core_1270() {
       auto mi = db.get_global_properties().parameters.maintenance_interval;
       generate_blocks(HARDFORK_CORE_1270_TIME - mi);
       wait_for_maintenance();
    }
    void wait_for_hf_core_2481() {
       auto mi = db.get_global_properties().parameters.maintenance_interval;
       generate_blocks(HARDFORK_CORE_2481_TIME - mi);
       wait_for_maintenance();
    }

    bool hf_core_2481_passed() {
       if( !hf2481 ) return false;
       auto maint_time = db.get_dynamic_global_properties().next_maintenance_time;
       return HARDFORK_CORE_2481_PASSED( maint_time );
    }

    void wait_for_maintenance() {
      generate_blocks( db.get_dynamic_global_properties().next_maintenance_time );
      generate_block();
    }

    const account_object& borrower() { return _borrower(db); }
    const account_object& borrower2() { return _borrower2(db); }
    const account_object& feedproducer() { return _feedproducer(db); }
    const asset_object& swan() { return _swan(db); }
    const asset_object& back() { return _back(db); }

    int64_t init_balance = 1000000;
    account_id_type _borrower, _borrower2, _feedproducer;
    asset_id_type _swan, _back;
};

}}

BOOST_FIXTURE_TEST_SUITE( swan_tests, swan_fixture )

/**
 *  This test sets up the minimum condition for a black swan to occur but does
 *  not test the full range of cases that may be possible during a black swan.
 */
BOOST_AUTO_TEST_CASE( black_swan )
{ try {
      if(hf2481)
         wait_for_hf_core_2481();
      else if(hf1270)
         wait_for_hf_core_1270();

      init_standard_swan();

      force_settle( borrower(), swan().amount(100) );

      expire_feed();
      wait_for_hf_core_216();

      force_settle( borrower(), swan().amount(100) );

      set_feed( 100, 150 );

      BOOST_TEST_MESSAGE( "Verify that we cannot borrow after black swan" );
      GRAPHENE_REQUIRE_THROW( borrow(borrower(), swan().amount(1000), back().amount(2000)), fc::exception )
      trx.operations.clear();
} catch( const fc::exception& e) {
      edump((e.to_detail_string()));
      throw;
   }
}

/**
 * Black swan occurs when price feed falls, triggered by settlement
 * order.
 */
BOOST_AUTO_TEST_CASE( black_swan_issue_346 )
{ try {

      ACTORS((buyer)(seller)(borrower)(borrower2)(settler)(feeder));

      const asset_object& core = asset_id_type()(db);

      int trial = 0;

      vector< const account_object* > actors{ &buyer, &seller, &borrower, &borrower2, &settler, &feeder };

      auto top_up = [&]()
      {
         for( const account_object* actor : actors )
         {
            int64_t bal = get_balance( *actor, core );
            if( bal < init_balance )
               transfer( committee_account, actor->id, asset(init_balance - bal) );
            else if( bal > init_balance )
               transfer( actor->id, committee_account, asset(bal - init_balance) );
         }
      };

      auto setup_asset = [&]() -> const asset_object&
      {
         const asset_object& bitusd = create_bitasset("USDBIT"+fc::to_string(trial)+"X", feeder_id);
         update_feed_producers( bitusd, {feeder.id} );
         BOOST_CHECK( !bitusd.bitasset_data(db).has_settlement() );
         trial++;
         return bitusd;
      };

      /*
       * GRAPHENE_COLLATERAL_RATIO_DENOM
      uint16_t maintenance_collateral_ratio = GRAPHENE_DEFAULT_MAINTENANCE_COLLATERAL_RATIO;
      uint16_t maximum_short_squeeze_ratio = GRAPHENE_DEFAULT_MAX_SHORT_SQUEEZE_RATIO;
      */

      // situations to test:
      // 1. minus short squeeze protection would be black swan, otherwise no
      // 2. issue 346 (price feed drops followed by force settle, drop should trigger BS)
      // 3. feed price < D/C of least collateralized short < call price < highest bid

      auto set_price = [&](
         const asset_object& bitusd,
         const price& settlement_price
         )
      {
         price_feed feed;
         feed.settlement_price = settlement_price;
         feed.core_exchange_rate = settlement_price;
         wdump( (feed.max_short_squeeze_price()) );
         publish_feed( bitusd, feeder, feed );
      };

      auto wait_for_settlement = [&]()
      {
         const auto& idx = db.get_index_type<force_settlement_index>().indices().get<by_expiration>();
         const auto& itr = idx.rbegin();
         if( itr == idx.rend() )
            return;
         generate_blocks( itr->settlement_date );
         BOOST_CHECK( !idx.empty() );
         generate_block();
         BOOST_CHECK( idx.empty() );
      };

      {
         const asset_object& bitusd = setup_asset();
         top_up();
         set_price( bitusd, bitusd.amount(1) / core.amount(5) );  // $0.20
         borrow(borrower, bitusd.amount(100), asset(1000));       // 2x collat
         transfer( borrower, settler, bitusd.amount(100) );

         // drop to $0.02 and settle
         BOOST_CHECK( !bitusd.bitasset_data(db).has_settlement() );
         set_price( bitusd, bitusd.amount(1) / core.amount(50) ); // $0.02
         BOOST_CHECK( bitusd.bitasset_data(db).has_settlement() );
         GRAPHENE_REQUIRE_THROW( borrow( borrower2, bitusd.amount(100), asset(10000) ), fc::exception );
         force_settle( settler, bitusd.amount(100) );

         // wait for forced settlement to execute
         // this would throw on Sep.18 testnet, see #346 (https://github.com/cryptonomex/graphene/issues/346)
         wait_for_settlement();
      }

      // issue 350 (https://github.com/cryptonomex/graphene/issues/350)
      {
         // ok, new asset
         const asset_object& bitusd = setup_asset();
         top_up();
         set_price( bitusd, bitusd.amount(40) / core.amount(1000) ); // $0.04
         borrow( borrower, bitusd.amount(100), asset(5000) );    // 2x collat
         transfer( borrower, seller, bitusd.amount(100) );
         // this order is at $0.019, we should not be able to match against it
         limit_order_id_type oid_019 = create_sell_order( seller, bitusd.amount(39), core.amount(2000) )->id;
         // this order is at $0.020, we should be able to match against it
         limit_order_id_type oid_020 = create_sell_order( seller, bitusd.amount(40), core.amount(2000) )->id;
         set_price( bitusd, bitusd.amount(21) / core.amount(1000) ); // $0.021
         //
         // We attempt to match against $0.019 order and black swan,
         // and this is intended behavior.  See discussion in ticket.
         //
         BOOST_CHECK( bitusd.bitasset_data(db).has_settlement() );
         BOOST_CHECK( db.find_object( oid_019 ) != nullptr );
         BOOST_CHECK( db.find_object( oid_020 ) == nullptr );
      }

   } catch( const fc::exception& e) {
      edump((e.to_detail_string()));
      throw;
   }
}

/** Creates a black swan, recover price feed - asset should be revived
 */
BOOST_AUTO_TEST_CASE( revive_recovered )
{ try {
      init_standard_swan( 700 );

      if(hf2481)
         wait_for_hf_core_2481();
      else if(hf1270)
         wait_for_hf_core_1270();
      else
         wait_for_hf_core_216();

      // revive after price recovers
      set_feed( 700, 800 );
      BOOST_CHECK( swan().bitasset_data(db).has_settlement() );
      set_feed( 701, 800 );
      BOOST_CHECK( !swan().bitasset_data(db).has_settlement() );
} catch( const fc::exception& e) {
      edump((e.to_detail_string()));
      throw;
   }
}

/** Creates a black swan, recover price feed - asset should be revived
 */
BOOST_AUTO_TEST_CASE( recollateralize )
{ try {
      init_standard_swan( 700 );

      // no hardfork yet
      GRAPHENE_REQUIRE_THROW( bid_collateral( borrower2(), back().amount(1000), swan().amount(100) ), fc::exception );

      if(hf2481)
         wait_for_hf_core_2481();
      else if(hf1270)
         wait_for_hf_core_1270();
      else
         wait_for_hf_core_216();

      int64_t b2_balance = get_balance( borrower2(), back() );
      bid_collateral( borrower2(), back().amount(1000), swan().amount(100) );
      BOOST_CHECK_EQUAL( get_balance( borrower2(), back() ), b2_balance - 1000 );
      bid_collateral( borrower2(), back().amount(2000), swan().amount(200) );
      BOOST_CHECK_EQUAL( get_balance( borrower2(), back() ), b2_balance - 2000 );
      bid_collateral( borrower2(), back().amount(1000), swan().amount(0) );
      BOOST_CHECK_EQUAL( get_balance( borrower2(), back() ), b2_balance );

      // can't bid for non-bitassets
      GRAPHENE_REQUIRE_THROW( bid_collateral( borrower2(), swan().amount(100), asset(100) ), fc::exception );
      // can't cancel a non-existant bid
      GRAPHENE_REQUIRE_THROW( bid_collateral( borrower2(), back().amount(0), swan().amount(0) ), fc::exception );
      // can't bid zero collateral
      GRAPHENE_REQUIRE_THROW( bid_collateral( borrower2(), back().amount(0), swan().amount(100) ), fc::exception );
      // can't bid more than we have
      GRAPHENE_REQUIRE_THROW( bid_collateral( borrower2(), back().amount(b2_balance + 100), swan().amount(100) ),
                              fc::exception );
      trx.operations.clear();

      // can't bid on a live bitasset
      const asset_object& bitcny = create_bitasset("CNYBIT", _feedproducer);
      GRAPHENE_REQUIRE_THROW( bid_collateral( borrower2(), asset(100), bitcny.amount(100) ), fc::exception );
      update_feed_producers(bitcny, {_feedproducer});
      price_feed feed;
      feed.settlement_price = bitcny.amount(1) / asset(1);
      publish_feed( bitcny.id, _feedproducer, feed );
      borrow( borrower2(), bitcny.amount(100), asset(1000) );

      // can't bid wrong collateral type
      GRAPHENE_REQUIRE_THROW( bid_collateral( borrower2(), bitcny.amount(100), swan().amount(100) ), fc::exception );

      BOOST_CHECK( swan().dynamic_data(db).current_supply == 1400 );
      BOOST_CHECK( swan().bitasset_data(db).settlement_fund == 2800 );
      BOOST_CHECK( swan().bitasset_data(db).has_settlement() );
      BOOST_CHECK( swan().bitasset_data(db).current_feed.settlement_price.is_null() );

      // doesn't happen without price feed
      bid_collateral( borrower(),  back().amount(1400), swan().amount(700) );
      bid_collateral( borrower2(), back().amount(1400), swan().amount(700) );
      wait_for_maintenance();
      BOOST_CHECK( swan().bitasset_data(db).has_settlement() );

      set_feed(1, 2);
      // doesn't happen if cover is insufficient
      bid_collateral( borrower2(), back().amount(1400), swan().amount(600) );
      wait_for_maintenance();
      BOOST_CHECK( swan().bitasset_data(db).has_settlement() );

      set_feed(1, 2);
      // doesn't happen if some bids have a bad swan price
      bid_collateral( borrower2(), back().amount(1050), swan().amount(700) );
      wait_for_maintenance();
      BOOST_CHECK( swan().bitasset_data(db).has_settlement() );

      set_feed(1, 2);
      // works
      bid_collateral( borrower(),  back().amount(1051), swan().amount(700) );
      bid_collateral( borrower2(), back().amount(2100), swan().amount(1399) );

      // check get_collateral_bids
      graphene::app::database_api db_api( db, &( app.get_options() ));
      GRAPHENE_REQUIRE_THROW( db_api.get_collateral_bids(back().symbol, 100, 0), fc::assert_exception );
      auto swan_symbol = _swan(db).symbol;
      vector<collateral_bid_object> bids = db_api.get_collateral_bids(swan_symbol, 100, 1);
      BOOST_CHECK_EQUAL( 1u, bids.size() );
      FC_ASSERT( _borrower2 == bids[0].bidder );
      bids = db_api.get_collateral_bids(swan_symbol, 1, 0);
      BOOST_CHECK_EQUAL( 1u, bids.size() );
      FC_ASSERT( _borrower == bids[0].bidder );
      bids = db_api.get_collateral_bids(swan_symbol, 100, 0);
      BOOST_CHECK_EQUAL( 2u, bids.size() );
      FC_ASSERT( _borrower == bids[0].bidder );
      FC_ASSERT( _borrower2 == bids[1].bidder );

      BOOST_CHECK( swan().bitasset_data(db).has_settlement() );
      // revive
      wait_for_maintenance();
      BOOST_CHECK( !swan().bitasset_data(db).has_settlement() );
      bids = db_api.get_collateral_bids(swan_symbol, 100, 0);
      BOOST_CHECK( bids.empty() );
} catch( const fc::exception& e) {
      edump((e.to_detail_string()));
      throw;
   }
}

/** Creates a black swan, bid, adjust bid before/after hf_1692
 */
BOOST_AUTO_TEST_CASE( bid_issue_1692 )
{ try {
   init_standard_swan( 700 );

   generate_blocks( HARDFORK_CORE_1692_TIME - 30 );

   int64_t b2_balance = get_balance( borrower2(), back() );
   bid_collateral( borrower2(), back().amount(1000), swan().amount(100) );
   BOOST_CHECK_EQUAL( get_balance( borrower2(), back() ), b2_balance - 1000 );
   GRAPHENE_REQUIRE_THROW( bid_collateral( borrower2(), back().amount(b2_balance), swan().amount(200) ),
                           fc::assert_exception );
   GRAPHENE_REQUIRE_THROW( bid_collateral( borrower2(), back().amount(b2_balance-999), swan().amount(200) ),
                           fc::assert_exception );

   generate_blocks( HARDFORK_CORE_1692_TIME + 30 );

   bid_collateral( borrower2(), back().amount(b2_balance-999), swan().amount(200) );
   BOOST_CHECK_EQUAL( get_balance( borrower2(), back() ), 999 );
   bid_collateral( borrower2(), back().amount(b2_balance), swan().amount(200) );
   BOOST_CHECK_EQUAL( get_balance( borrower2(), back() ), 0 );
} FC_LOG_AND_RETHROW() }

/** Creates a black swan, settles all debts, recovers price feed - asset should be revived
 */
BOOST_AUTO_TEST_CASE( revive_empty_recovered )
{ try {
      limit_order_id_type oid = init_standard_swan( 1000 );

      if(hf2481)
         wait_for_hf_core_2481();
      else if(hf1270)
         wait_for_hf_core_1270();
      else
         wait_for_hf_core_216();

      set_expiration( db, trx );
      cancel_limit_order( oid(db) );
      force_settle( borrower(), swan().amount(1000) );
      force_settle( borrower2(), swan().amount(1000) );
      BOOST_CHECK_EQUAL( 0, swan().dynamic_data(db).current_supply.value );
      BOOST_CHECK_EQUAL( 0, swan().bitasset_data(db).settlement_fund.value );
      BOOST_CHECK( swan().bitasset_data(db).has_settlement() );

      // revive after price recovers
      set_feed( 1, 1 );
      BOOST_CHECK( !swan().bitasset_data(db).has_settlement() );

      auto& call_idx = db.get_index_type<call_order_index>().indices().get<by_account>();
      auto itr = call_idx.find( boost::make_tuple(_feedproducer, _swan) );
      BOOST_CHECK( itr == call_idx.end() );
} catch( const fc::exception& e) {
      edump((e.to_detail_string()));
      throw;
   }
}

/** Creates a black swan, settles all debts - asset should be revived in next maintenance
 */
BOOST_AUTO_TEST_CASE( revive_empty )
{ try {
      if(hf2481)
         wait_for_hf_core_2481();
      else if(hf1270)
         wait_for_hf_core_1270();
      else
         wait_for_hf_core_216();

      limit_order_id_type oid = init_standard_swan( 1000 );

      cancel_limit_order( oid(db) );
      force_settle( borrower(), swan().amount(1000) );
      force_settle( borrower2(), swan().amount(1000) );
      BOOST_CHECK_EQUAL( 0, swan().dynamic_data(db).current_supply.value );

      BOOST_CHECK( swan().bitasset_data(db).has_settlement() );

      // revive
      wait_for_maintenance();
      BOOST_CHECK( !swan().bitasset_data(db).has_settlement() );
} catch( const fc::exception& e) {
      edump((e.to_detail_string()));
      throw;
   }
}

/** Creates a black swan, settles all debts - asset should be revived in next maintenance
 */
BOOST_AUTO_TEST_CASE( revive_empty_with_bid )
{ try {
      if(hf2481)
         wait_for_hf_core_2481();
      else if(hf1270)
         wait_for_hf_core_1270();
      else
         wait_for_hf_core_216();

      standard_users();
      standard_asset();

      set_feed( 1, 1 );
      borrow(borrower(), swan().amount(1000), back().amount(2000));
      borrow(borrower2(), swan().amount(1000), back().amount(1967));

      set_feed( 1, 2 );
      // this sell order is designed to trigger a black swan
      limit_order_id_type oid = create_sell_order( borrower2(), swan().amount(1), back().amount(3) )->id;
      BOOST_CHECK( swan().bitasset_data(db).has_settlement() );

      cancel_limit_order( oid(db) );
      force_settle( borrower(), swan().amount(500) );
      force_settle( borrower(), swan().amount(500) );
      force_settle( borrower2(), swan().amount(667) );
      force_settle( borrower2(), swan().amount(333) );
      BOOST_CHECK_EQUAL( 0, swan().dynamic_data(db).current_supply.value );
      BOOST_CHECK_EQUAL( 0, swan().bitasset_data(db).settlement_fund.value );

      bid_collateral( borrower(), back().amount(3000), swan().amount(700) );

      BOOST_CHECK( swan().bitasset_data(db).has_settlement() );

      // revive
      wait_for_maintenance();
      BOOST_CHECK( !swan().bitasset_data(db).has_settlement() );
      graphene::app::database_api db_api( db, &( app.get_options() ));
      auto swan_symbol = _swan(db).symbol;
      vector<collateral_bid_object> bids = db_api.get_collateral_bids(swan_symbol, 100, 0);
      BOOST_CHECK( bids.empty() );

      auto& call_idx = db.get_index_type<call_order_index>().indices().get<by_account>();
      auto itr = call_idx.find( boost::make_tuple(_borrower, _swan) );
      BOOST_CHECK( itr == call_idx.end() );
      itr = call_idx.find( boost::make_tuple(_feedproducer, _swan) );
      BOOST_CHECK( itr == call_idx.end() );
} catch( const fc::exception& e) {
      edump((e.to_detail_string()));
      throw;
   }
}

BOOST_AUTO_TEST_CASE(black_swan_after_hf1270)
{ try {
   hf1270 = true;
   INVOKE(black_swan);

} FC_LOG_AND_RETHROW() }

// black_swan_issue_346_hf1270 is skipped as it is already failing with HARDFORK_CORE_834_TIME

BOOST_AUTO_TEST_CASE(revive_recovered_hf1270)
{ try {
   hf1270 = true;
   INVOKE(revive_recovered);

} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE(recollateralize_hf1270)
{ try {
   hf1270 = true;
   INVOKE(recollateralize);

} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE(revive_empty_recovered_hf1270)
{ try {
   hf1270 = true;
   INVOKE(revive_empty_recovered);

} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE(revive_empty_hf1270)
{ try {
   hf1270 = true;
   INVOKE(revive_empty);

} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE(revive_empty_with_bid_hf1270)
{ try {
   hf1270 = true;
   INVOKE(revive_empty_with_bid);

} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE(black_swan_after_hf2481)
{ try {
   hf2481 = true;
   INVOKE(black_swan);

} FC_LOG_AND_RETHROW() }

// black_swan_issue_346_hf2481 is skipped as it is already failing with HARDFORK_CORE_834_TIME

BOOST_AUTO_TEST_CASE(revive_recovered_hf2481)
{ try {
   hf2481 = true;
   INVOKE(revive_recovered);

} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE(recollateralize_hf2481)
{ try {
   hf2481 = true;
   INVOKE(recollateralize);

} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE(revive_empty_recovered_hf2481)
{ try {
   hf2481 = true;
   INVOKE(revive_empty_recovered);

} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE(revive_empty_hf2481)
{ try {
   hf2481 = true;
   INVOKE(revive_empty);

} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE(revive_empty_with_bid_hf2481)
{ try {
   hf2481 = true;
   INVOKE(revive_empty_with_bid);

} FC_LOG_AND_RETHROW() }

/** Creates a black swan, bids on more than outstanding debt
 */
BOOST_AUTO_TEST_CASE( overflow )
{ try {
   init_standard_swan( 700 );

   wait_for_hf_core_216();

   bid_collateral( borrower(),  back().amount(2200), swan().amount(GRAPHENE_MAX_SHARE_SUPPLY - 1) );
   bid_collateral( borrower2(), back().amount(2100), swan().amount(1399) );
   set_feed(1, 2);
   wait_for_maintenance();

   auto& call_idx = db.get_index_type<call_order_index>().indices().get<by_account>();
   auto itr = call_idx.find( boost::make_tuple(_borrower, _swan) );
   BOOST_REQUIRE( itr != call_idx.end() );
   BOOST_CHECK_EQUAL( 1, itr->debt.value );
   itr = call_idx.find( boost::make_tuple(_borrower2, _swan) );
   BOOST_REQUIRE( itr != call_idx.end() );
   BOOST_CHECK_EQUAL( 1399, itr->debt.value );

   BOOST_CHECK( !swan().bitasset_data(db).has_settlement() );
} FC_LOG_AND_RETHROW() }

/// Tests what kind of assets can have the disable_collateral_bidding flag / issuer permission
BOOST_AUTO_TEST_CASE( hf2281_asset_permissions_flags_test )
{
   try {

      // Advance to core-2281 hard fork
      auto mi = db.get_global_properties().parameters.maintenance_interval;
      generate_blocks(HARDFORK_CORE_2281_TIME - mi);
      generate_blocks(db.get_dynamic_global_properties().next_maintenance_time);
      set_expiration( db, trx );

      ACTORS((sam));

      auto init_amount = 10000000 * GRAPHENE_BLOCKCHAIN_PRECISION;
      fund( sam, asset(init_amount) );

      // Able to create a PM with the disable_collateral_bidding bit in flags
      create_prediction_market( "TESTPMTEST", sam_id, 0, disable_collateral_bidding );

      // Able to create a MPA with the disable_collateral_bidding bit in flags
      create_bitasset( "TESTBITTEST", sam_id, 0, disable_collateral_bidding );

      // Unable to create a UIA with the disable_collateral_bidding bit in flags
      BOOST_CHECK_THROW( create_user_issued_asset( "TESTUIA", sam_id(db), disable_collateral_bidding ),
                         fc::exception );

      // create a PM with a zero market_fee_percent
      const asset_object& pm = create_prediction_market( "TESTPM", sam_id, 0, charge_market_fee );
      asset_id_type pm_id = pm.id;

      // create a MPA with a zero market_fee_percent
      const asset_object& mpa = create_bitasset( "TESTBIT", sam_id, 0, charge_market_fee );
      asset_id_type mpa_id = mpa.id;

      // create a UIA with a zero market_fee_percent
      const asset_object& uia = create_user_issued_asset( "TESTUIA", sam_id(db), charge_market_fee );
      asset_id_type uia_id = uia.id;

      // Prepare for asset update
      asset_update_operation auop;
      auop.issuer = sam_id;

      // Able to set disable_collateral_bidding bit in flags for PM
      auop.asset_to_update = pm_id;
      auop.new_options = pm_id(db).options;
      auop.new_options.flags |= disable_collateral_bidding;
      trx.operations.clear();
      trx.operations.push_back( auop );
      PUSH_TX(db, trx, ~0);
      // Able to propose
      propose( auop );

      // Able to set disable_collateral_bidding bit in flags for MPA
      auop.asset_to_update = mpa_id;
      auop.new_options = mpa_id(db).options;
      auop.new_options.flags |= disable_collateral_bidding;
      trx.operations.clear();
      trx.operations.push_back( auop );
      PUSH_TX(db, trx, ~0);
      // Able to propose
      propose( auop );

      // Unable to set disable_collateral_bidding bit in flags for UIA
      auop.asset_to_update = uia_id;
      auop.new_options = uia_id(db).options;
      auop.new_options.flags |= disable_collateral_bidding;
      trx.operations.clear();
      trx.operations.push_back( auop );
      BOOST_CHECK_THROW( PUSH_TX(db, trx, ~0), fc::exception );
      // Able to propose
      propose( auop );

      // Able to set disable_collateral_bidding bit in issuer_permissions for PM
      auop.asset_to_update = pm_id;
      auop.new_options = pm_id(db).options;
      auop.new_options.issuer_permissions |= disable_collateral_bidding;
      trx.operations.clear();
      trx.operations.push_back( auop );
      PUSH_TX(db, trx, ~0);
      // Able to propose
      propose( auop );

      // Able to set disable_collateral_bidding bit in issuer_permissions for MPA
      auop.asset_to_update = mpa_id;
      auop.new_options = mpa_id(db).options;
      auop.new_options.issuer_permissions |= disable_collateral_bidding;
      trx.operations.clear();
      trx.operations.push_back( auop );
      PUSH_TX(db, trx, ~0);
      // Able to propose
      propose( auop );

      // Unable to set disable_collateral_bidding bit in issuer_permissions for UIA
      auop.asset_to_update = uia_id;
      auop.new_options = uia_id(db).options;
      auop.new_options.issuer_permissions |= disable_collateral_bidding;
      trx.operations.clear();
      trx.operations.push_back( auop );
      BOOST_CHECK_THROW( PUSH_TX(db, trx, ~0), fc::exception );
      // But able to propose
      propose( auop );

      // Unable to create a UIA with disable_collateral_bidding permission bit
      asset_create_operation acop;
      acop.issuer = sam_id;
      acop.symbol = "SAMCOIN";
      acop.precision = 2;
      acop.common_options.core_exchange_rate = price(asset(1,asset_id_type(1)),asset(1));
      acop.common_options.max_supply = GRAPHENE_MAX_SHARE_SUPPLY;
      acop.common_options.market_fee_percent = 100;
      acop.common_options.flags = charge_market_fee;
      acop.common_options.issuer_permissions = UIA_ASSET_ISSUER_PERMISSION_MASK | disable_collateral_bidding;

      trx.operations.clear();
      trx.operations.push_back( acop );
      BOOST_CHECK_THROW( PUSH_TX(db, trx, ~0), fc::exception );

      // Unable to propose either
      BOOST_CHECK_THROW( propose( acop ), fc::exception );

      // Able to create UIA without disable_collateral_bidding permission bit
      acop.common_options.issuer_permissions = UIA_ASSET_ISSUER_PERMISSION_MASK;
      trx.operations.clear();
      trx.operations.push_back( acop );
      PUSH_TX(db, trx, ~0);

      // Able to create a MPA with disable_collateral_bidding permission bit
      acop.symbol = "SAMMPA";
      acop.common_options.issuer_permissions = UIA_ASSET_ISSUER_PERMISSION_MASK | disable_collateral_bidding;
      acop.bitasset_opts = bitasset_options();

      trx.operations.clear();
      trx.operations.push_back( acop );
      PUSH_TX(db, trx, ~0);

      // Able to propose
      propose( acop );

      // Able to create a PM with disable_collateral_bidding permission bit
      acop.symbol = "SAMPM";
      acop.precision = asset_id_type()(db).precision;
      acop.is_prediction_market = true;
      acop.common_options.issuer_permissions = UIA_ASSET_ISSUER_PERMISSION_MASK | global_settle
                                                                                | disable_collateral_bidding;
      acop.bitasset_opts = bitasset_options();

      trx.operations.clear();
      trx.operations.push_back( acop );
      PUSH_TX(db, trx, ~0);

      // Able to propose
      propose( acop );

      generate_block();

   } catch (fc::exception& e) {
      edump((e.to_detail_string()));
      throw;
   }
}

/// Tests whether asset owner has permission to update the disable_collateral_bidding flag and the permission
BOOST_AUTO_TEST_CASE( hf2281_asset_owner_permission_test )
{
   try {

      // Advance to core-2281 hard fork
      auto mi = db.get_global_properties().parameters.maintenance_interval;
      generate_blocks(HARDFORK_CORE_2281_TIME - mi);
      generate_blocks(db.get_dynamic_global_properties().next_maintenance_time);
      set_expiration( db, trx );

      ACTORS((sam)(feeder));

      auto init_amount = 10000000 * GRAPHENE_BLOCKCHAIN_PRECISION;
      fund( sam, asset(init_amount) );
      fund( feeder, asset(init_amount) );

      // create a MPA with a zero market_fee_percent
      const asset_object& mpa = create_bitasset( "TESTBIT", sam_id, 0, charge_market_fee );
      asset_id_type mpa_id = mpa.id;

      BOOST_CHECK( mpa_id(db).can_bid_collateral() );

      // add a price feed publisher and publish a feed
      update_feed_producers( mpa_id, { feeder_id } );

      price_feed f;
      f.settlement_price = price( asset(1,mpa_id), asset(1) );
      f.core_exchange_rate = price( asset(1,mpa_id), asset(1) );
      f.maintenance_collateral_ratio = 1850;
      f.maximum_short_squeeze_ratio = 1250;

      uint16_t feed_icr = 1900;

      publish_feed( mpa_id, feeder_id, f, feed_icr );

      // Prepare for asset update
      asset_update_operation auop;
      auop.issuer = sam_id;
      auop.asset_to_update = mpa_id;
      auop.new_options = mpa_id(db).options;

      // update disable_collateral_bidding flag
      auop.new_options.flags |= disable_collateral_bidding;
      trx.operations.clear();
      trx.operations.push_back( auop );
      PUSH_TX(db, trx, ~0);

      // check
      BOOST_CHECK( !mpa_id(db).can_bid_collateral() );

      // disable owner's permission to update the disable_collateral_bidding flag
      auop.new_options.issuer_permissions |= disable_collateral_bidding;
      trx.operations.clear();
      trx.operations.push_back( auop );
      PUSH_TX(db, trx, ~0);

      // check
      BOOST_CHECK( !mpa_id(db).can_bid_collateral() );

      // check that owner can not update the disable_collateral_bidding flag
      auop.new_options.flags &= ~disable_collateral_bidding;
      trx.operations.clear();
      trx.operations.push_back( auop );
      BOOST_CHECK_THROW( PUSH_TX(db, trx, ~0), fc::exception );
      auop.new_options = mpa_id(db).options;

      // check
      BOOST_CHECK( !mpa_id(db).can_bid_collateral() );

      // enable owner's permission to update the disable_collateral_bidding flag
      auop.new_options.issuer_permissions &= ~disable_collateral_bidding;
      trx.operations.clear();
      trx.operations.push_back( auop );
      PUSH_TX(db, trx, ~0);

      // check
      BOOST_CHECK( !mpa_id(db).can_bid_collateral() );

      // check that owner can update the disable_collateral_bidding flag
      auop.new_options.flags &= ~disable_collateral_bidding;
      trx.operations.clear();
      trx.operations.push_back( auop );
      PUSH_TX(db, trx, ~0);

      // check
      BOOST_CHECK( mpa_id(db).can_bid_collateral() );

      // Sam borrow some
      borrow( sam, asset(1000, mpa_id), asset(2000) );

      // disable owner's permission to update the disable_collateral_bidding flag
      auop.new_options.issuer_permissions |= disable_collateral_bidding;
      trx.operations.clear();
      trx.operations.push_back( auop );
      PUSH_TX(db, trx, ~0);

      // check
      BOOST_CHECK( mpa_id(db).can_bid_collateral() );

      // check that owner can not update the disable_collateral_bidding flag
      auop.new_options.flags |= disable_collateral_bidding;
      trx.operations.clear();
      trx.operations.push_back( auop );
      BOOST_CHECK_THROW( PUSH_TX(db, trx, ~0), fc::exception );
      auop.new_options = mpa_id(db).options;

      // check
      BOOST_CHECK( mpa_id(db).can_bid_collateral() );

      // unable to enable the permission due to non-zero supply
      auop.new_options.issuer_permissions &= ~disable_collateral_bidding;
      trx.operations.clear();
      trx.operations.push_back( auop );
      BOOST_CHECK_THROW( PUSH_TX(db, trx, ~0), fc::exception );

      // check
      BOOST_CHECK( mpa_id(db).can_bid_collateral() );

      generate_block();

   } catch (fc::exception& e) {
      edump((e.to_detail_string()));
      throw;
   }
}

/// Tests the disable_collateral_bidding bit in asset flags
BOOST_AUTO_TEST_CASE( disable_collateral_bidding_test )
{ try {
   init_standard_swan( 2000 );

   // Advance to core-2281 hard fork
   auto mi = db.get_global_properties().parameters.maintenance_interval;
   generate_blocks(HARDFORK_CORE_2281_TIME - mi);
   generate_blocks(db.get_dynamic_global_properties().next_maintenance_time);
   set_expiration( db, trx );

   BOOST_CHECK( swan().can_bid_collateral() );

   bid_collateral( borrower(), back().amount(3000), swan().amount(700) );
   bid_collateral( borrower2(), back().amount(300), swan().amount(600) );

   graphene::app::database_api db_api( db, &( app.get_options() ));
   auto swan_symbol = swan().symbol;
   vector<collateral_bid_object> bids = db_api.get_collateral_bids(swan_symbol, 100, 0);
   BOOST_CHECK_EQUAL( bids.size(), 2u );

   // Disable collateral bidding
   asset_update_operation auop;
   auop.issuer = swan().issuer;
   auop.asset_to_update = swan().id;
   auop.new_options = swan().options;
   auop.new_options.flags |= disable_collateral_bidding;
   trx.operations.clear();
   trx.operations.push_back( auop );
   PUSH_TX(db, trx, ~0);

   BOOST_CHECK( !swan().can_bid_collateral() );

   // Check that existing bids are cancelled
   bids = db_api.get_collateral_bids(swan_symbol, 100, 0);
   BOOST_CHECK_EQUAL( bids.size(), 0u );

   BOOST_CHECK( swan().bitasset_data(db).has_settlement() );

   // Unable to bid
   BOOST_CHECK_THROW( bid_collateral( borrower(), back().amount(3000), swan().amount(700) ), fc::exception );

   // Enable collateral bidding
   auop.new_options.flags &= ~disable_collateral_bidding;
   trx.operations.clear();
   trx.operations.push_back( auop );
   PUSH_TX(db, trx, ~0);

   BOOST_CHECK( swan().can_bid_collateral() );

   // Able to bid again
   bid_collateral( borrower(), back().amount(3000), swan().amount(700) );
   bid_collateral( borrower2(), back().amount(300), swan().amount(600) );

   bids = db_api.get_collateral_bids(swan_symbol, 100, 0);
   BOOST_CHECK_EQUAL( bids.size(), 2u );

   generate_block();

   BOOST_CHECK( swan().bitasset_data(db).has_settlement() );

} FC_LOG_AND_RETHROW() }

/// Tests cancelling of collateral bids at hard fork time if the disable_collateral_bidding bit in asset flags was
/// already set due to a bug
BOOST_AUTO_TEST_CASE( disable_collateral_bidding_cross_hardfork_test )
{ try {
   init_standard_swan( 2000, true );

   wait_for_hf_core_216();

   BOOST_CHECK( !swan().can_bid_collateral() );

   bid_collateral( borrower(), back().amount(3000), swan().amount(700) );
   bid_collateral( borrower2(), back().amount(300), swan().amount(600) );

   graphene::app::database_api db_api( db, &( app.get_options() ));
   auto swan_symbol = swan().symbol;
   vector<collateral_bid_object> bids = db_api.get_collateral_bids(swan_symbol, 100, 0);
   BOOST_CHECK_EQUAL( bids.size(), 2u );

   BOOST_CHECK( swan().bitasset_data(db).has_settlement() );

   // Advance to core-2281 hard fork
   auto mi = db.get_global_properties().parameters.maintenance_interval;
   generate_blocks(HARDFORK_CORE_2281_TIME - mi);
   generate_blocks(db.get_dynamic_global_properties().next_maintenance_time);
   set_expiration( db, trx );

   BOOST_CHECK( !swan().can_bid_collateral() );

   // Check that existing bids are cancelled
   bids = db_api.get_collateral_bids(swan_symbol, 100, 0);
   BOOST_CHECK_EQUAL( bids.size(), 0u );

   BOOST_CHECK( swan().bitasset_data(db).has_settlement() );

   // Unable to bid
   BOOST_CHECK_THROW( bid_collateral( borrower(), back().amount(3000), swan().amount(700) ), fc::exception );

   generate_block();

   BOOST_CHECK( swan().bitasset_data(db).has_settlement() );

} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_SUITE_END()
