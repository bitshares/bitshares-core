/*
 * Copyright (c) 2015 Cryptonomex, Inc., and contributors.
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

#include <fc/crypto/digest.hpp>

#include "../common/database_fixture.hpp"

using namespace graphene::chain;
using namespace graphene::chain::test;

BOOST_FIXTURE_TEST_SUITE( swan_tests, database_fixture )

/**
 *  This test sets up the minimum condition for a black swan to occur but does
 *  not test the full range of cases that may be possible during a black swan.
 */
BOOST_AUTO_TEST_CASE( black_swan )
{ try {
      ACTORS((borrower)(borrower2)(feedproducer));

      const auto& bitusd = create_bitasset("USDBIT", feedproducer_id);
      const asset_id_type bitusd_id = bitusd.id;
      const auto& core   = asset_id_type()(db);

      int64_t init_balance(1000000);

      transfer(committee_account, borrower_id, asset(init_balance));
      transfer(committee_account, borrower2_id, asset(init_balance));
      update_feed_producers(bitusd, {feedproducer.id});

      price_feed current_feed;
      current_feed.settlement_price = bitusd.amount(100) / core.amount(100);

      // starting out with price 1:1
      publish_feed(bitusd, feedproducer, current_feed);

      // start out with 2:1 collateral
      borrow(borrower, bitusd.amount(1000), asset(2000));
      borrow(borrower2, bitusd.amount(1000), asset(4000));

      BOOST_REQUIRE_EQUAL( get_balance(borrower, bitusd), 1000 );
      BOOST_REQUIRE_EQUAL( get_balance(borrower2, bitusd), 1000 );
      BOOST_REQUIRE_EQUAL( get_balance(borrower , core), init_balance - 2000 );
      BOOST_REQUIRE_EQUAL( get_balance(borrower2, core), init_balance - 4000 );

      current_feed.settlement_price = bitusd.amount( 100 ) / core.amount(200);
      publish_feed( bitusd, feedproducer, current_feed );

      /// this sell order is designed to trigger a black swan
      create_sell_order( borrower2, bitusd.amount(1000), core.amount(3000) );

      BOOST_CHECK( bitusd.bitasset_data(db).has_settlement() );

      force_settle(borrower, bitusd.amount(100));

      // make sure pricefeeds expire
      generate_blocks(db.head_block_time() + GRAPHENE_DEFAULT_PRICE_FEED_LIFETIME);
      generate_blocks( HARDFORK_CORE_216_TIME );
      generate_blocks(2);

      FC_ASSERT( bitusd_id(db).bitasset_data(db).current_feed.settlement_price.is_null() );
      force_settle(borrower_id(db), asset(100, bitusd_id));

      current_feed.settlement_price = bitusd_id(db).amount(100) / asset_id_type()(db).amount(150);
      publish_feed(bitusd_id(db), feedproducer_id(db), current_feed);

      BOOST_TEST_MESSAGE( "Verify that we cannot borrow after black swan" );
      GRAPHENE_REQUIRE_THROW( borrow(borrower_id(db), asset(1000, bitusd_id), asset(2000)), fc::exception )
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
      const int64_t init_balance(1000000);

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
         // this would throw on Sep.18 testnet, see #346
         wait_for_settlement();
      }

      // issue 350
      {
         // ok, new asset
         const asset_object& bitusd = setup_asset();
         top_up();
         set_price( bitusd, bitusd.amount(40) / core.amount(1000) ); // $0.04
         borrow( borrower, bitusd.amount(100), asset(5000) );    // 2x collat
         transfer( borrower, seller, bitusd.amount(100) );
         limit_order_id_type oid_019 = create_sell_order( seller, bitusd.amount(39), core.amount(2000) )->id;   // this order is at $0.019, we should not be able to match against it
         limit_order_id_type oid_020 = create_sell_order( seller, bitusd.amount(40), core.amount(2000) )->id;   // this order is at $0.020, we should be able to match against it
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
      ACTORS((borrower)(borrower2)(feedproducer));

      const auto& bitusd = create_bitasset("USDBIT", feedproducer_id);
      const asset_id_type bitusd_id = bitusd.id;
      const auto& core   = asset_id_type()(db);

      int64_t init_balance(1000000);

      transfer(committee_account, borrower_id, asset(init_balance));
      transfer(committee_account, borrower2_id, asset(init_balance));
      update_feed_producers(bitusd, {feedproducer.id});

      price_feed current_feed;
      current_feed.settlement_price = bitusd.amount(100) / core.amount(100);

      // starting out with price 1:1
      publish_feed(bitusd, feedproducer, current_feed);

      // start out with 2:1 collateral
      borrow(borrower, bitusd.amount(700), asset(1400));
      borrow(borrower2, bitusd.amount(700), asset(2800));

      current_feed.settlement_price = bitusd.amount( 100 ) / core.amount(200);
      publish_feed( bitusd, feedproducer, current_feed );

      /// this sell order is designed to trigger a black swan
      create_sell_order( borrower2, bitusd.amount(10), core.amount(30) );

      BOOST_CHECK( bitusd.bitasset_data(db).has_settlement() );

      generate_blocks( HARDFORK_CORE_216_TIME );
      generate_blocks(2);

      // revive after price recovers
      current_feed.settlement_price = asset( 700, bitusd_id ) / asset(800);
      publish_feed( bitusd_id(db), feedproducer_id(db), current_feed );
      BOOST_CHECK( bitusd_id(db).bitasset_data(db).has_settlement() );
      current_feed.settlement_price = asset( 701, bitusd_id ) / asset(800);
      publish_feed( bitusd_id(db), feedproducer_id(db), current_feed );
      BOOST_CHECK( !bitusd_id(db).bitasset_data(db).has_settlement() );
} catch( const fc::exception& e) {
      edump((e.to_detail_string()));
      throw;
   }
}

BOOST_AUTO_TEST_SUITE_END()
