/*
 * Copyright (c) 2021 Abit More, and contributors.
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

#include "../common/database_fixture.hpp"

#include <graphene/chain/hardfork.hpp>
#include <graphene/chain/asset_object.hpp>
#include <graphene/chain/market_object.hpp>

#include <boost/test/unit_test.hpp>

using namespace graphene::chain;
using namespace graphene::chain::test;

BOOST_FIXTURE_TEST_SUITE( bsrm_tests, database_fixture )

/// Tests individual settlement to order
BOOST_AUTO_TEST_CASE( individual_settlement_to_order_test )
{
   try {

      // Advance to core-2467 hard fork
      auto mi = db.get_global_properties().parameters.maintenance_interval;
      generate_blocks(HARDFORK_CORE_2467_TIME - mi);
      generate_blocks(db.get_dynamic_global_properties().next_maintenance_time);
      set_expiration( db, trx );

      ACTORS((sam)(feeder)(borrower)(borrower2)(borrower3)(borrower4)(seller));

      auto init_amount = 10000000 * GRAPHENE_BLOCKCHAIN_PRECISION;
      fund( sam, asset(init_amount) );
      fund( feeder, asset(init_amount) );
      fund( borrower, asset(init_amount) );
      fund( borrower2, asset(init_amount) );
      fund( borrower3, asset(init_amount) );
      fund( borrower4, asset(init_amount) );

      using bsrm_type = bitasset_options::black_swan_response_type;
      uint8_t bsrm_value = static_cast<uint8_t>(bsrm_type::individual_settlement_to_order);

      // Create asset
      asset_create_operation acop;
      acop.issuer = sam_id;
      acop.symbol = "SAMMPA";
      acop.precision = 2;
      acop.common_options.core_exchange_rate = price(asset(1,asset_id_type(1)),asset(1));
      acop.common_options.max_supply = GRAPHENE_MAX_SHARE_SUPPLY;
      acop.common_options.market_fee_percent = 100; // 1%
      acop.common_options.flags = charge_market_fee;
      acop.common_options.issuer_permissions = ASSET_ISSUER_PERMISSION_ENABLE_BITS_MASK;
      acop.bitasset_opts = bitasset_options();
      acop.bitasset_opts->minimum_feeds = 1;
      acop.bitasset_opts->extensions.value.black_swan_response_method = bsrm_value;
      acop.bitasset_opts->extensions.value.margin_call_fee_ratio = 11;

      trx.operations.clear();
      trx.operations.push_back( acop );
      processed_transaction ptx = PUSH_TX(db, trx, ~0);
      const asset_object& mpa = db.get<asset_object>(ptx.operation_results[0].get<object_id_type>());
      asset_id_type mpa_id = mpa.id;

      BOOST_CHECK( mpa.bitasset_data(db).get_black_swan_response_method()
                   == bsrm_type::individual_settlement_to_order );

      // add a price feed publisher and publish a feed
      update_feed_producers( mpa_id, { feeder_id } );

      price_feed f;
      f.settlement_price = price( asset(100,mpa_id), asset(1) );
      f.core_exchange_rate = price( asset(100,mpa_id), asset(1) );
      f.maintenance_collateral_ratio = 1850;
      f.maximum_short_squeeze_ratio = 1250;

      uint16_t feed_icr = 1900;

      publish_feed( mpa_id, feeder_id, f, feed_icr );

      BOOST_CHECK( mpa.bitasset_data(db).median_feed.settlement_price == f.settlement_price );
      BOOST_CHECK( mpa.bitasset_data(db).current_feed.settlement_price == f.settlement_price );
      BOOST_CHECK( !mpa.bitasset_data(db).has_settlement() );
      BOOST_CHECK( !mpa.bitasset_data(db).has_individual_settlement() );
      BOOST_CHECK( !db.find_settled_debt_order(mpa_id) );

      // borrowers borrow some
      // undercollateralization price = 100000:2000 * 1250:1000 = 100000:1600
      const call_order_object* call_ptr = borrow( borrower, asset(100000, mpa_id), asset(2000) );
      BOOST_REQUIRE( call_ptr );
      call_order_id_type call_id = call_ptr->id;

      // undercollateralization price = 100000:2100 * 1250:1000 = 100000:1680
      const call_order_object* call2_ptr = borrow( borrower2, asset(100000, mpa_id), asset(2100) );
      BOOST_REQUIRE( call2_ptr );
      call_order_id_type call2_id = call2_ptr->id;

      // undercollateralization price = 100000:3000 * 1250:1000 = 100000:1760
      const call_order_object* call3_ptr = borrow( borrower3, asset(100000, mpa_id), asset(2200) );
      BOOST_REQUIRE( call3_ptr );
      call_order_id_type call3_id = call3_ptr->id;

      // undercollateralization price = 100000:4000 * 1250:1000 = 100000:2400
      const call_order_object* call4_ptr = borrow( borrower4, asset(100000, mpa_id), asset(2500) );
      BOOST_REQUIRE( call4_ptr );
      call_order_id_type call4_id = call4_ptr->id;

      // Transfer funds to sellers
      transfer( borrower, seller, asset(100000,mpa_id) );
      transfer( borrower2, seller, asset(100000,mpa_id) );
      transfer( borrower3, seller, asset(100000,mpa_id) );
      transfer( borrower4, seller, asset(100000,mpa_id) );

      BOOST_CHECK_EQUAL( call_id(db).debt.value, 100000 );
      BOOST_CHECK_EQUAL( call_id(db).collateral.value, 2000 );
      BOOST_CHECK_EQUAL( call2_id(db).debt.value, 100000 );
      BOOST_CHECK_EQUAL( call2_id(db).collateral.value, 2100 );
      BOOST_CHECK_EQUAL( call3_id(db).debt.value, 100000 );
      BOOST_CHECK_EQUAL( call3_id(db).collateral.value, 2200 );
      BOOST_CHECK_EQUAL( call4_id(db).debt.value, 100000 );
      BOOST_CHECK_EQUAL( call4_id(db).collateral.value, 2500 );

      BOOST_CHECK_EQUAL( get_balance( seller_id, mpa_id ), 400000 );
      BOOST_CHECK_EQUAL( get_balance( seller_id, asset_id_type() ), 0 );

      // publish a new feed so that borrower's debt position is undercollateralized
      f.settlement_price = price( asset(100000,mpa_id), asset(1650) );
      publish_feed( mpa_id, feeder_id, f, feed_icr );
      // call pays price = 100000:1650 * 1000:1250 = 100000:2062.5 = 48.484848485
      // call match price = 100000:1650 * 1000:1239 = 100000:2048.75 = 48.915303153

      // check
      BOOST_CHECK( mpa.bitasset_data(db).median_feed.settlement_price == f.settlement_price );
      BOOST_CHECK( mpa.bitasset_data(db).current_feed.settlement_price == f.settlement_price );
      BOOST_CHECK( !mpa.bitasset_data(db).has_settlement() );
      BOOST_CHECK( !mpa.bitasset_data(db).has_individual_settlement() );
      const limit_order_object* settled_debt = db.find_settled_debt_order(mpa_id);
      BOOST_REQUIRE( settled_debt );

      // call: margin call fee deducted = round_down(2000*11/1250) = 17,
      // fund receives 2000 - 17 = 1983
      BOOST_CHECK_EQUAL( settled_debt->for_sale.value, 1983 );
      BOOST_CHECK_EQUAL( settled_debt->amount_to_receive().amount.value, 100000 );
      // order match price = 100000 / 1983 = 50.428643469

      BOOST_CHECK( !db.find( call_id ) );
      BOOST_CHECK_EQUAL( call2_id(db).debt.value, 100000 );
      BOOST_CHECK_EQUAL( call2_id(db).collateral.value, 2100 );
      BOOST_CHECK_EQUAL( call3_id(db).debt.value, 100000 );
      BOOST_CHECK_EQUAL( call3_id(db).collateral.value, 2200 );
      BOOST_CHECK_EQUAL( call4_id(db).debt.value, 100000 );
      BOOST_CHECK_EQUAL( call4_id(db).collateral.value, 2500 );

      // seller sells some
      const limit_order_object* limit_ptr = create_sell_order( seller, asset(10000,mpa_id), asset(100) );
      // the limit order is filled
      BOOST_CHECK( !limit_ptr );

      // call2 is partially filled
      // limit order gets round_down(10000*(1650/100000)*(1239/1000)) = 204
      // limit order pays round_up(204*(100000/1650)*(1000/1239)) = 9979
      // call2 gets 9979
      // call2 pays round_down(9979*(1650/100000)*(1250/1000)) = 205, margin call fee = 1
      BOOST_CHECK( !db.find( call_id ) );
      BOOST_CHECK_EQUAL( call2_id(db).debt.value, 90021 ); // 100000 - 9979
      BOOST_CHECK_EQUAL( call2_id(db).collateral.value, 1895 ); // 2100 - 205
      BOOST_CHECK_EQUAL( call3_id(db).debt.value, 100000 );
      BOOST_CHECK_EQUAL( call3_id(db).collateral.value, 2200 );
      BOOST_CHECK_EQUAL( call4_id(db).debt.value, 100000 );
      BOOST_CHECK_EQUAL( call4_id(db).collateral.value, 2500 );

      BOOST_CHECK_EQUAL( settled_debt->for_sale.value, 1983 );
      BOOST_CHECK_EQUAL( settled_debt->amount_to_receive().amount.value, 100000 );

      BOOST_CHECK_EQUAL( get_balance( seller_id, mpa_id ), 390021 ); // 400000 - 9979
      BOOST_CHECK_EQUAL( get_balance( seller_id, asset_id_type() ), 204 );

      // publish a new feed so that 2 other debt positions are undercollateralized
      f.settlement_price = price( asset(100000,mpa_id), asset(1800) );
      publish_feed( mpa_id, feeder_id, f, feed_icr );
      // call pays price = 100000:1800 * 1000:1250 = 100000:2250 = 44.444444444
      // call match price = 100000:1800 * 1000:1239 = 100000:2230.2 = 44.83902789

      BOOST_CHECK( mpa.bitasset_data(db).median_feed.settlement_price == f.settlement_price );
      BOOST_CHECK( mpa.bitasset_data(db).current_feed.settlement_price == f.settlement_price );
      BOOST_CHECK( !mpa.bitasset_data(db).has_settlement() );
      BOOST_CHECK( !mpa.bitasset_data(db).has_individual_settlement() );

      BOOST_CHECK( !db.find( call_id ) );
      BOOST_CHECK( !db.find( call2_id ) );
      BOOST_CHECK( !db.find( call3_id ) );
      BOOST_CHECK_EQUAL( call4_id(db).debt.value, 100000 );
      BOOST_CHECK_EQUAL( call4_id(db).collateral.value, 2500 );

      // call2: margin call fee deducted = round_down(1895*11/1250) = 16,
      // fund receives 1895 - 16 = 1879
      // call3: margin call fee deducted = round_down(2200*11/1250) = 19,
      // fund receives 2200 - 19 = 2181
      BOOST_CHECK_EQUAL( settled_debt->for_sale.value, 6043 ); // 1983 + 1879 + 2181
      BOOST_CHECK_EQUAL( settled_debt->amount_to_receive().amount.value, 290021 ); // 100000 + 90021 + 100000
      // order match price = 290021 / 6043 = 47.992884329

      // borrower buys at higher price
      const limit_order_object* buy_high = create_sell_order( borrower, asset(10), asset(100,mpa_id) );
      BOOST_CHECK( buy_high );
      limit_order_id_type buy_high_id = buy_high->id;

      // seller sells some, this will match buy_high,
      // and when it matches call4, it will be cancelled since it is too small
      limit_ptr = create_sell_order( seller, asset(120,mpa_id), asset(1) );
      // the limit order is filled
      BOOST_CHECK( !limit_ptr );
      // buy_high is filled
      BOOST_CHECK( !db.find( buy_high_id ) );

      BOOST_CHECK_EQUAL( call4_id(db).debt.value, 100000 );
      BOOST_CHECK_EQUAL( call4_id(db).collateral.value, 2500 );

      BOOST_CHECK_EQUAL( settled_debt->for_sale.value, 6043 ); // no change
      BOOST_CHECK_EQUAL( settled_debt->amount_to_receive().amount.value, 290021 ); // no change

      BOOST_CHECK_EQUAL( get_balance( seller_id, mpa_id ), 389921 ); // 400000 - 9979 - 100
      BOOST_CHECK_EQUAL( get_balance( seller_id, asset_id_type() ), 214 ); // 204 + 10

      // publish a new feed so that the settled debt order is in the front of the order book
      f.settlement_price = price( asset(100000,mpa_id), asset(1600) );
      publish_feed( mpa_id, feeder_id, f, feed_icr );
      // call pays price = 100000:1600 * 1000:1250 = 100000:2000 = 50
      // call match price = 100000:1600 * 1000:1239 = 100000:1982.4 = 50.443906376

      // borrower buys at higher price
      buy_high = create_sell_order( borrower, asset(10), asset(100,mpa_id) );
      BOOST_CHECK( buy_high );
      buy_high_id = buy_high->id;

      // seller sells some, this will match buy_high,
      // and when it matches the settled debt, it will be cancelled since it is too small
      limit_ptr = create_sell_order( seller, asset(120,mpa_id), asset(1) );
      // the limit order is filled
      BOOST_CHECK( !limit_ptr );
      // buy_high is filled
      BOOST_CHECK( !db.find( buy_high_id ) );

      BOOST_CHECK_EQUAL( settled_debt->for_sale.value, 6043 ); // no change
      BOOST_CHECK_EQUAL( settled_debt->amount_to_receive().amount.value, 290021 ); // no change

      BOOST_CHECK_EQUAL( get_balance( seller_id, mpa_id ), 389821 ); // 400000 - 9979 - 100 - 100
      BOOST_CHECK_EQUAL( get_balance( seller_id, asset_id_type() ), 224 ); // 204 + 10 + 10

      // seller sells some
      limit_ptr = create_sell_order( seller, asset(10000,mpa_id), asset(100) );
      // the limit order is filled
      BOOST_CHECK( !limit_ptr );
      // the settled debt is partially filled
      // limit order receives = round_down(10000*6043/290021) = 208
      // settled debt receives = round_up(208*290021/6043) = 9983

      BOOST_CHECK( !db.find( call_id ) );
      BOOST_CHECK( !db.find( call2_id ) );
      BOOST_CHECK( !db.find( call3_id ) );
      BOOST_CHECK_EQUAL( call4_id(db).debt.value, 100000 );
      BOOST_CHECK_EQUAL( call4_id(db).collateral.value, 2500 );

      BOOST_CHECK_EQUAL( settled_debt->for_sale.value, 5835 ); // 6043 - 208
      BOOST_CHECK_EQUAL( settled_debt->amount_to_receive().amount.value, 280038 ); // 290021 - 9983

      BOOST_CHECK_EQUAL( get_balance( seller_id, mpa_id ), 379838 ); // 400000 - 9979 - 100 - 100 - 9983
      BOOST_CHECK_EQUAL( get_balance( seller_id, asset_id_type() ), 432 ); // 204 + 10 + 10 + 208

      // seller sells some
      limit_ptr = create_sell_order( seller, asset(300000,mpa_id), asset(3000) );
      // the limit order is filled
      BOOST_CHECK( !limit_ptr );

      auto final_check = [&]
      {
         BOOST_CHECK( mpa_id(db).bitasset_data(db).median_feed.settlement_price == f.settlement_price );
         BOOST_CHECK( mpa_id(db).bitasset_data(db).current_feed.settlement_price == f.settlement_price );
         BOOST_CHECK( !mpa_id(db).bitasset_data(db).has_settlement() );
         BOOST_CHECK( !mpa_id(db).bitasset_data(db).has_individual_settlement() );

         // the settled debt is fully filled
         BOOST_CHECK( !db.find_settled_debt_order(mpa_id) );
         // limit order reminder = 300000 - 280038 = 19962
         // call4 is partially filled
         // limit order gets round_down(19962*(1600/100000)*(1239/1000)) = 395
         // limit order pays round_up(395*(100000/1600)*(1000/1239)) = 19926
         // call4 gets 19926
         // call4 pays round_down(19926*(1600/100000)*(1250/1000)) = 398, margin call fee = 3

         BOOST_CHECK_EQUAL( get_balance( seller_id, mpa_id ), 79874 ); // 400000 - 9979 - 100 - 100 - 9983
                                                                       // - 280038 - 19926
         BOOST_CHECK_EQUAL( get_balance( seller_id, asset_id_type() ), 6662 ); // 204 + 10 + 10 + 208 + 5835 + 395

         BOOST_CHECK( !db.find( call_id ) );
         BOOST_CHECK( !db.find( call2_id ) );
         BOOST_CHECK( !db.find( call3_id ) );
         BOOST_CHECK_EQUAL( call4_id(db).debt.value, 80074 ); // 100000 - 19926
         BOOST_CHECK_EQUAL( call4_id(db).collateral.value, 2102 ); // 2500 - 398

      };

      final_check();

      BOOST_TEST_MESSAGE( "Generate a block" );
      generate_block();

      final_check();

   } catch (fc::exception& e) {
      edump((e.to_detail_string()));
      throw;
   }
}

BOOST_AUTO_TEST_SUITE_END()
