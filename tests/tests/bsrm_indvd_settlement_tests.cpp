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

/// Tests individual settlement (to order or fund) : how call orders are being processed when price drops
BOOST_AUTO_TEST_CASE( individual_settlement_test )
{ try {

   // Advance to core-2467 hard fork
   auto mi = db.get_global_properties().parameters.maintenance_interval;
   generate_blocks(HARDFORK_CORE_2467_TIME - mi);
   generate_blocks(db.get_dynamic_global_properties().next_maintenance_time);

   // two passes, one for individual settlement to order, the other for individual settlement to fund
   for( int i = 0; i < 2; ++ i )
   {
      idump( (i) );

      set_expiration( db, trx );

      ACTORS((sam)(feeder)(borrower)(borrower2)(borrower3)(borrower4)(borrower5)(seller)(seller2)(seller3)(seller4));

      auto init_amount = 10000000 * GRAPHENE_BLOCKCHAIN_PRECISION;
      fund( sam, asset(init_amount) );
      fund( feeder, asset(init_amount) );
      fund( borrower, asset(init_amount) );
      fund( borrower2, asset(init_amount) );
      fund( borrower3, asset(init_amount) );
      fund( borrower4, asset(init_amount) );
      fund( borrower5, asset(init_amount) );

      using bsrm_type = bitasset_options::black_swan_response_type;
      uint8_t bsrm_value = (i == 0) ? static_cast<uint8_t>(bsrm_type::individual_settlement_to_order)
                                    : static_cast<uint8_t>(bsrm_type::individual_settlement_to_fund);

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

      if( 0 == i )
         BOOST_CHECK( mpa.bitasset_data(db).get_black_swan_response_method()
                      == bsrm_type::individual_settlement_to_order );
      else if( 1 == i )
         BOOST_CHECK( mpa.bitasset_data(db).get_black_swan_response_method()
                      == bsrm_type::individual_settlement_to_fund );

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
      BOOST_CHECK( !mpa_id(db).bitasset_data(db).is_current_feed_price_capped() );
      BOOST_CHECK( !mpa.bitasset_data(db).has_settlement() );
      BOOST_CHECK( !mpa.bitasset_data(db).has_individual_settlement() );
      BOOST_CHECK( !db.find_settled_debt_order(mpa_id) );

      // borrowers borrow some
      // 100000 / 2000 = 50
      // undercollateralization price = 100000:2000 * 1250:1000 = 100000:1600
      const call_order_object* call_ptr = borrow( borrower, asset(100000, mpa_id), asset(2000) );
      BOOST_REQUIRE( call_ptr );
      call_order_id_type call_id = call_ptr->id;

      // 100000 / 2100 = 47.619047619
      // undercollateralization price = 100000:2100 * 1250:1000 = 100000:1680
      const call_order_object* call2_ptr = borrow( borrower2, asset(100000, mpa_id), asset(2100) );
      BOOST_REQUIRE( call2_ptr );
      call_order_id_type call2_id = call2_ptr->id;

      // 100000 / 2200 = 45.454545455
      // undercollateralization price = 100000:2200 * 1250:1000 = 100000:1760
      const call_order_object* call3_ptr = borrow( borrower3, asset(100000, mpa_id), asset(2200) );
      BOOST_REQUIRE( call3_ptr );
      call_order_id_type call3_id = call3_ptr->id;

      // 100000 / 2500 = 40
      // undercollateralization price = 100000:2500 * 1250:1000 = 100000:2000
      const call_order_object* call4_ptr = borrow( borrower4, asset(100000, mpa_id), asset(2500) );
      BOOST_REQUIRE( call4_ptr );
      call_order_id_type call4_id = call4_ptr->id;

      // 100000 / 2240 = 44.642857143
      // undercollateralization price = 100000:2240 * 1250:1000 = 100000:1792
      const call_order_object* call5_ptr = borrow( borrower5, asset(1000000, mpa_id), asset(22400) );
      BOOST_REQUIRE( call5_ptr );
      call_order_id_type call5_id = call5_ptr->id;

      // Transfer funds to sellers
      transfer( borrower, seller, asset(100000,mpa_id) );
      transfer( borrower2, seller, asset(100000,mpa_id) );
      transfer( borrower3, seller, asset(100000,mpa_id) );
      transfer( borrower4, seller2, asset(50000,mpa_id) );
      transfer( borrower4, seller3, asset(50000,mpa_id) );
      transfer( borrower5, seller4, asset(1000000,mpa_id) );

      BOOST_CHECK_EQUAL( call_id(db).debt.value, 100000 );
      BOOST_CHECK_EQUAL( call_id(db).collateral.value, 2000 );
      BOOST_CHECK_EQUAL( call2_id(db).debt.value, 100000 );
      BOOST_CHECK_EQUAL( call2_id(db).collateral.value, 2100 );
      BOOST_CHECK_EQUAL( call3_id(db).debt.value, 100000 );
      BOOST_CHECK_EQUAL( call3_id(db).collateral.value, 2200 );
      BOOST_CHECK_EQUAL( call4_id(db).debt.value, 100000 );
      BOOST_CHECK_EQUAL( call4_id(db).collateral.value, 2500 );
      BOOST_CHECK_EQUAL( call5_id(db).debt.value, 1000000 );
      BOOST_CHECK_EQUAL( call5_id(db).collateral.value, 22400 );

      BOOST_CHECK_EQUAL( get_balance( borrower_id, asset_id_type() ), init_amount - 2000 );
      BOOST_CHECK_EQUAL( get_balance( borrower2_id, asset_id_type() ), init_amount - 2100 );
      BOOST_CHECK_EQUAL( get_balance( borrower3_id, asset_id_type() ), init_amount - 2200 );
      BOOST_CHECK_EQUAL( get_balance( borrower4_id, asset_id_type() ), init_amount - 2500 );
      BOOST_CHECK_EQUAL( get_balance( borrower5_id, asset_id_type() ), init_amount - 22400 );

      BOOST_CHECK_EQUAL( get_balance( seller_id, mpa_id ), 300000 );
      BOOST_CHECK_EQUAL( get_balance( seller_id, asset_id_type() ), 0 );
      BOOST_CHECK_EQUAL( get_balance( seller2_id, mpa_id ), 50000 );
      BOOST_CHECK_EQUAL( get_balance( seller2_id, asset_id_type() ), 0 );
      BOOST_CHECK_EQUAL( get_balance( seller3_id, mpa_id ), 50000 );
      BOOST_CHECK_EQUAL( get_balance( seller3_id, asset_id_type() ), 0 );
      BOOST_CHECK_EQUAL( get_balance( seller4_id, mpa_id ), 1000000 );
      BOOST_CHECK_EQUAL( get_balance( seller4_id, asset_id_type() ), 0 );

      // seller sells some
      const limit_order_object* sell_low = create_sell_order( seller, asset(10000,mpa_id), asset(190) );
      BOOST_REQUIRE( sell_low );
      limit_order_id_type sell_low_id = sell_low->id;
      BOOST_CHECK_EQUAL( sell_low_id(db).for_sale.value, 10000 );

      // seller sells some
      const limit_order_object* sell_mid = create_sell_order( seller, asset(100000,mpa_id), asset(2000) );
      BOOST_REQUIRE( sell_mid );
      limit_order_id_type sell_mid_id = sell_mid->id;
      BOOST_CHECK_EQUAL( sell_mid_id(db).for_sale.value, 100000 );

      // seller4 sells some
      const limit_order_object* sell_mid2 = create_sell_order( seller4, asset(20000,mpa_id), asset(439) );
      BOOST_REQUIRE( sell_mid2 );
      limit_order_id_type sell_mid2_id = sell_mid2->id;
      BOOST_CHECK_EQUAL( sell_mid2_id(db).for_sale.value, 20000 );

      // seller sells some
      const limit_order_object* sell_high = create_sell_order( seller, asset(100000,mpa_id), asset(2400) );
      BOOST_REQUIRE( sell_high );
      limit_order_id_type sell_high_id = sell_high->id;
      BOOST_CHECK_EQUAL( sell_high_id(db).for_sale.value, 100000 );

      // seller2 settles
      auto result = force_settle( seller2, asset(50000,mpa_id) );
      force_settlement_id_type settle_id = *result.get<extendable_operation_result>().value.new_objects->begin();
      BOOST_REQUIRE( db.find( settle_id ) );
      BOOST_CHECK_EQUAL( settle_id(db).balance.amount.value, 50000 );

      // seller3 settles
      result = force_settle( seller3, asset(10000,mpa_id) );
      force_settlement_id_type settle2_id = *result.get<extendable_operation_result>().value.new_objects->begin();
      BOOST_REQUIRE( db.find( settle2_id ) );
      BOOST_CHECK_EQUAL( settle2_id(db).balance.amount.value, 10000 );

      // check
      BOOST_CHECK_EQUAL( call_id(db).debt.value, 100000 );
      BOOST_CHECK_EQUAL( call_id(db).collateral.value, 2000 );
      BOOST_CHECK_EQUAL( call2_id(db).debt.value, 100000 );
      BOOST_CHECK_EQUAL( call2_id(db).collateral.value, 2100 );
      BOOST_CHECK_EQUAL( call3_id(db).debt.value, 100000 );
      BOOST_CHECK_EQUAL( call3_id(db).collateral.value, 2200 );
      BOOST_CHECK_EQUAL( call4_id(db).debt.value, 100000 );
      BOOST_CHECK_EQUAL( call4_id(db).collateral.value, 2500 );
      BOOST_CHECK_EQUAL( call5_id(db).debt.value, 1000000 );
      BOOST_CHECK_EQUAL( call5_id(db).collateral.value, 22400 );

      BOOST_CHECK_EQUAL( get_balance( seller_id, mpa_id ), 90000 ); // 300000 - 10000 - 100000 - 100000
      BOOST_CHECK_EQUAL( get_balance( seller_id, asset_id_type() ), 0 );
      BOOST_CHECK_EQUAL( get_balance( seller2_id, mpa_id ), 0 ); // 50000 - 50000
      BOOST_CHECK_EQUAL( get_balance( seller2_id, asset_id_type() ), 0 );
      BOOST_CHECK_EQUAL( get_balance( seller3_id, mpa_id ), 40000 ); // 50000 - 10000
      BOOST_CHECK_EQUAL( get_balance( seller3_id, asset_id_type() ), 0 );
      BOOST_CHECK_EQUAL( get_balance( seller4_id, mpa_id ), 980000 ); // 1000000 - 20000
      BOOST_CHECK_EQUAL( get_balance( seller4_id, asset_id_type() ), 0 );

      // publish a new feed so that call, call2, call3 and call5 are undercollateralized
      f.settlement_price = price( asset(100000,mpa_id), asset(1800) );
      publish_feed( mpa_id, feeder_id, f, feed_icr );
      // call pays price = 100000:1800 * 1000:1250 = 100000:2250 = 44.444444444
      // call match price = 100000:1800 * 1000:1239 = 100000:2230.2 = 44.83902789

      auto check_result = [&]
      {
         // sell_low price is 10000/190 = 52.631578947
         // call is matched with sell_low
         // call pays price is (10000/190) * (1239/1250)
         // sell_low is smaller thus fully filled
         BOOST_CHECK( !db.find( sell_low_id ) );
         // sell_low gets 190, pays 10000
         // call gets 10000, pays round_down(190 * 1250/1239) = 191, margin call fee = 1
         // call is now (100000-10000):(2000-191) = 90000:1809 = 49.751243781 (< 50)

         // sell_mid price is 100000/2000 = 50
         // call is matched with sell_mid
         // call pays price is (100000/2000) * (1239/1250)
         // call is smaller
         // call gets 90000, pays round_up(90000 * (2000/100000) * (1250/1239)) = 1815
         // 1815 > 1809, unable to fill

         // call is matched with settle
         // settle is smaller thus fully filled
         BOOST_CHECK( !db.find( settle_id ) );
         // unable to pay at MSSP
         // call pays at its own price
         // settle receives round_down(50000 * (1809/90000) * (1239/1250)) = 996
         // settle pays round_up(996 * (90000/1809) * (1250/1239)) = 49993, refund 7
         // call receives 49993
         // call pays round_down(49993 * 1809/90000) = 1004, margin call fee = 1004 - 996 = 8
         // call is now (90000-49993):(1809-1004) = 40007:805 = 49.698136646 (< 90000:1809)

         // call is matched with sell_mid again
         // call gets 40007, pays round_up(40007 * (2000/100000) * (1250/1239)) = 807
         // 807 > 805, unable to fill

         // call is matched with settle2
         // settle2 is smaller thus fully filled
         BOOST_CHECK( !db.find( settle2_id ) );
         // unable to pay at MSSP
         // call pays at its own price
         // settle2 receives round_down(10000 * (805/40007) * (1239/1250)) = 199
         // settle2 pays round_up(199 * (40007/805) * (1250/1239)) = 9978, refund 22
         // call receives 9978
         // call pays round_down(9978 * 805/40007) = 200, margin call fee = 200 - 199 = 1
         // call is now (40007-9978):(805-200) = 30029:605 = 49.634710744 (< 40007:805)

         // call is matched with sell_mid again
         // call gets 30029, pays round_up(30029 * (2000/100000) * (1250/1239)) = 606
         // 606 > 605, unable to fill

         // no settle order
         // call is individually settled
         BOOST_CHECK( !db.find( call_id ) );
         // fund gets round_up(605 * 1239/1250) = 600, margin call fee = 605 - 600 = 5
         // fund debt = 30029

         if( 0 == i ) // to order
         {
            // call2 is matched with sell_mid
            // the size is the same, consider call2 as smaller
            // call2 gets 100000, pays round_up(100000 * (2000/100000) * (1250/1239)) = 2018, margin call fee = 18
            // 2018 < 2100, able to fill
            BOOST_CHECK( !db.find( call2_id ) );
            BOOST_CHECK( !db.find( sell_mid_id ) );

            // sell_mid2 price is 20000/439 = 45.55808656
            // call pays price is (20000/439) * (1239/1250) = 45.157175399

            // call3 is 100000/2200 = 45.454545455 (>45.157175399), so unable to fill
            // call3 is individually settled
            BOOST_CHECK( !db.find( call3_id ) );
            // fund gets round_up(2200 * 1239/1250) = 2181, margin call fee = 2200 - 2181 = 19
            // fund debt += 100000

            // call5 is 1000000/22400 = 44.642857143 (<45.157175399)
            // call5 is matched with sell_mid2
            // sell_mid2 is smaller thus fully filled
            BOOST_CHECK( !db.find( sell_mid2_id ) );
            // sell_mid2 gets 439, pays 20000
            // call5 gets 20000, pays round_down(439 * 1250/1239) = 442, margin call fee = 3
            // call5 is now (1000000-20000):(22400-442) = 980000:21958 = 44.63065853 (> MSSP 44.444444444)

            // sell_high price is 100000/2400 = 41.666666667 (< call match price, so will not match)
            BOOST_REQUIRE( db.find( sell_high_id ) );
            BOOST_CHECK_EQUAL( sell_high_id(db).for_sale.value, 100000 );

            // call5 is individually settled
            BOOST_CHECK( !db.find( call5_id ) );
            // fund gets round_up(21958 * 1239/1250) = 21765, margin call fee = 21958 - 21765 = 193
            // fund debt += 980000

            // call4 is not undercollateralized
            BOOST_REQUIRE( db.find( call4_id ) );
            BOOST_CHECK_EQUAL( call4_id(db).debt.value, 100000 );
            BOOST_CHECK_EQUAL( call4_id(db).collateral.value, 2500 );

            // check
            BOOST_CHECK( mpa_id(db).bitasset_data(db).median_feed.settlement_price == f.settlement_price );
            BOOST_CHECK( mpa_id(db).bitasset_data(db).current_feed.settlement_price == f.settlement_price );
            BOOST_CHECK( !mpa_id(db).bitasset_data(db).is_current_feed_price_capped() );
            BOOST_CHECK( !mpa_id(db).bitasset_data(db).has_settlement() );
            BOOST_CHECK( !mpa_id(db).bitasset_data(db).has_individual_settlement() );
            const limit_order_object* settled_debt = db.find_settled_debt_order(mpa_id);
            BOOST_REQUIRE( settled_debt );

            BOOST_CHECK_EQUAL( settled_debt->for_sale.value, 24546 ); // 600 + 2181 + 21765
            BOOST_CHECK_EQUAL( settled_debt->amount_to_receive().amount.value, 1110029 ); // 30029 + 100000 + 980000

            BOOST_CHECK_EQUAL( get_balance( borrower_id, asset_id_type() ), init_amount - 2000 );
            BOOST_CHECK_EQUAL( get_balance( borrower2_id, asset_id_type() ), init_amount - 2018 ); // refund 82
            BOOST_CHECK_EQUAL( get_balance( borrower3_id, asset_id_type() ), init_amount - 2200 );
            BOOST_CHECK_EQUAL( get_balance( borrower4_id, asset_id_type() ), init_amount - 2500 );
            BOOST_CHECK_EQUAL( get_balance( borrower5_id, asset_id_type() ), init_amount - 22400 );

            BOOST_CHECK_EQUAL( get_balance( seller_id, mpa_id ), 90000 ); // no change
            BOOST_CHECK_EQUAL( get_balance( seller_id, asset_id_type() ), 2190 ); // 190 + 2000
            BOOST_CHECK_EQUAL( get_balance( seller2_id, mpa_id ), 7 ); // refund 7
            BOOST_CHECK_EQUAL( get_balance( seller2_id, asset_id_type() ), 996 );
            BOOST_CHECK_EQUAL( get_balance( seller3_id, mpa_id ), 40022 ); // refund 22
            BOOST_CHECK_EQUAL( get_balance( seller3_id, asset_id_type() ), 199 );
            BOOST_CHECK_EQUAL( get_balance( seller4_id, mpa_id ), 980000 ); // no change
            BOOST_CHECK_EQUAL( get_balance( seller4_id, asset_id_type() ), 439 ); // 439
         }
         else if( 1 == i ) // to fund
         {
            // sell_mid price is 100000/2000 = 50
            // call pays price is (100000/2000) * (1239:1250) = 49.56

            // median feed is 100000/1800 = 55.555555556
            // call pays price = 100000:1800 * 1000:1250 = 100000:2250 = 44.444444444
            // call match price = 100000:1800 * 1000:1239 = 100000:2230.2 = 44.83902789

            // fund collateral = 600
            // fund debt = 30029
            // current feed is capped at (30029:600) * (1239:1000) = 62.009885
            // call pays price is (30029:600) * (1239:1250) = 49.607908
            // call match price is 30029/600 = 50.048333333 (> sell_mid.price)

            // call2 will not match with sell_mid
            // call2 is individually settled
            BOOST_CHECK( !db.find( call2_id ) );
            // fund gets round_up(2100 * 1239/1250) = 2082, margin call fee = 2100 - 2082 = 18
            // fund debt += 100000

            // fund collateral = 600 + 2082 = 2682
            // fund debt = 30029 + 100000 = 130029
            // current feed is capped at (130029:2682) * (1239:1000) = 60.069325503
            // call pays price is (130029:2682) * (1239:1250) = 48.055460403
            // call match price is 130029/2682 = 48.482102908 (< sell_mid.price)

            // call3 is matched with sell_mid
            // the size is the same, consider call3 as smaller
            // call3 gets 100000, pays round_up(100000 * (2000/100000) * (1250/1239)) = 2018, margin call fee = 18
            // 2018 < 2200, able to fill
            BOOST_CHECK( !db.find( call3_id ) );
            BOOST_CHECK( !db.find( sell_mid_id ) );

            // sell_mid2 price is 20000/439 = 45.55808656
            // call match price is 130029/2682 = 48.482102908 (> sell_mid2.price)

            // call5 will not match with sell_mid2
            // call5 is individually settled
            BOOST_CHECK( !db.find( call5_id ) );
            // fund gets round_up(22400 * 1239/1250) = 22203, margin call fee = 22400 - 22203 = 197
            // fund debt += 1000000

            // fund collateral = 600 + 2082 + 22203 = 24885
            // fund debt = 30029 + 100000 + 100000 = 1130029
            // current feed is capped at (1130029:24885) * (1239:1000) = 56.263047257
            // call pays price is (1130029:24885) * (1239:1250) = 45.010437806
            // call match price is 1130029/24885 = 45.410046213 (< sell_mid2.price)

            // sell_mid2 is matched with call4
            // sell_mid2 is smaller thus fully filled
            BOOST_CHECK( !db.find( sell_mid2_id ) );
            // sell_mid2 gets 439, pays 20000
            // call4 gets 20000, pays round_down(439 * 1250/1239) = 442, margin call fee = 3
            // call4 is now (100000-20000):(2500-442) = 80000:2058 = 38.872691934 (< MSSP 44.444444444)

            // sell_high price is 100000/2400 = 41.666666667 (< call match price, so will not match)
            BOOST_REQUIRE( db.find( sell_high_id ) );
            BOOST_CHECK_EQUAL( sell_high_id(db).for_sale.value, 100000 );

            // call4 is not undercollateralized
            BOOST_REQUIRE( db.find( call4_id ) );
            BOOST_CHECK_EQUAL( call4_id(db).debt.value, 80000 );
            BOOST_CHECK_EQUAL( call4_id(db).collateral.value, 2058 );

            // check
            BOOST_CHECK( mpa_id(db).bitasset_data(db).median_feed.settlement_price == f.settlement_price );
            BOOST_CHECK( !mpa_id(db).bitasset_data(db).has_settlement() );

            BOOST_CHECK( mpa_id(db).bitasset_data(db).has_individual_settlement() );
            BOOST_CHECK( !db.find_settled_debt_order(mpa_id) );

            BOOST_CHECK_EQUAL( mpa_id(db).bitasset_data(db).individual_settlement_fund.value, 24885 );
            BOOST_CHECK_EQUAL( mpa_id(db).bitasset_data(db).individual_settlement_debt.value, 1130029 );

            BOOST_CHECK( mpa_id(db).bitasset_data(db).current_feed.settlement_price
                         == price( asset(1130029*1239,mpa_id), asset(24885*1000) ) );
            BOOST_CHECK( mpa_id(db).bitasset_data(db).is_current_feed_price_capped() );

            BOOST_CHECK_EQUAL( get_balance( borrower_id, asset_id_type() ), init_amount - 2000 );
            BOOST_CHECK_EQUAL( get_balance( borrower2_id, asset_id_type() ), init_amount - 2100 );
            BOOST_CHECK_EQUAL( get_balance( borrower3_id, asset_id_type() ), init_amount - 2018 ); // refund some
            BOOST_CHECK_EQUAL( get_balance( borrower4_id, asset_id_type() ), init_amount - 2500 );
            BOOST_CHECK_EQUAL( get_balance( borrower5_id, asset_id_type() ), init_amount - 22400 );

            BOOST_CHECK_EQUAL( get_balance( seller_id, mpa_id ), 90000 ); // no change
            BOOST_CHECK_EQUAL( get_balance( seller_id, asset_id_type() ), 2190 ); // 190 + 2000
            BOOST_CHECK_EQUAL( get_balance( seller2_id, mpa_id ), 7 ); // refund 7
            BOOST_CHECK_EQUAL( get_balance( seller2_id, asset_id_type() ), 996 );
            BOOST_CHECK_EQUAL( get_balance( seller3_id, mpa_id ), 40022 ); // refund 22
            BOOST_CHECK_EQUAL( get_balance( seller3_id, asset_id_type() ), 199 );
            BOOST_CHECK_EQUAL( get_balance( seller4_id, mpa_id ), 980000 ); // no change
            BOOST_CHECK_EQUAL( get_balance( seller4_id, asset_id_type() ), 439 ); // 439
         }
      };

      check_result();

      BOOST_TEST_MESSAGE( "Generate a block" );
      generate_block();

      check_result();

      // reset
      db.pop_block();

   } // for i

} FC_CAPTURE_AND_RETHROW() }

/// Tests individual settlement to order : settles when price drops, and how orders are being matched after settled
BOOST_AUTO_TEST_CASE( individual_settlement_to_order_and_taking_test )
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

      // undercollateralization price = 100000:2200 * 1250:1000 = 100000:1760
      const call_order_object* call3_ptr = borrow( borrower3, asset(100000, mpa_id), asset(2200) );
      BOOST_REQUIRE( call3_ptr );
      call_order_id_type call3_id = call3_ptr->id;

      // undercollateralization price = 100000:2500 * 1250:1000 = 100000:2000
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

/// Tests a scenario that force settlements get cancelled on expiration when there is no debt position
/// due to individual settlement to order
BOOST_AUTO_TEST_CASE( settle_order_cancel_due_to_no_debt_position )
{
   try {

      // Advance to core-2467 hard fork
      auto mi = db.get_global_properties().parameters.maintenance_interval;
      generate_blocks(HARDFORK_CORE_2467_TIME - mi);
      generate_blocks(db.get_dynamic_global_properties().next_maintenance_time);
      set_expiration( db, trx );

      ACTORS((sam)(feeder)(borrower)(seller));

      auto init_amount = 10000000 * GRAPHENE_BLOCKCHAIN_PRECISION;
      fund( sam, asset(init_amount) );
      fund( feeder, asset(init_amount) );
      fund( borrower, asset(init_amount) );

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
      acop.bitasset_opts->feed_lifetime_sec = 86400;
      acop.bitasset_opts->force_settlement_delay_sec = 600;
      acop.bitasset_opts->extensions.value.black_swan_response_method = bsrm_value;
      acop.bitasset_opts->extensions.value.margin_call_fee_ratio = 11;

      trx.operations.clear();
      trx.operations.push_back( acop );
      processed_transaction ptx = PUSH_TX(db, trx, ~0);
      const asset_object& mpa = db.get<asset_object>(ptx.operation_results[0].get<object_id_type>());
      asset_id_type mpa_id = mpa.id;

      BOOST_CHECK( mpa.bitasset_data(db).get_black_swan_response_method()
                   == bsrm_type::individual_settlement_to_order );

      acop.symbol = "SAMMPA2";
      acop.bitasset_opts->force_settlement_delay_sec = 60000;
      trx.operations.clear();
      trx.operations.push_back( acop );
      ptx = PUSH_TX(db, trx, ~0);
      const asset_object& mpa2 = db.get<asset_object>(ptx.operation_results[0].get<object_id_type>());
      asset_id_type mpa2_id = mpa2.id;

      // add a price feed publisher and publish a feed
      update_feed_producers( mpa_id, { feeder_id } );
      update_feed_producers( mpa2_id, { feeder_id } );

      price_feed f;
      f.settlement_price = price( asset(100,mpa_id), asset(1) );
      f.core_exchange_rate = price( asset(100,mpa_id), asset(1) );
      f.maintenance_collateral_ratio = 1850;
      f.maximum_short_squeeze_ratio = 1250;

      uint16_t feed_icr = 1900;

      publish_feed( mpa_id, feeder_id, f, feed_icr );

      price_feed f2;
      f2.settlement_price = price( asset(100,mpa2_id), asset(1) );
      f2.core_exchange_rate = price( asset(100,mpa2_id), asset(1) );
      f2.maintenance_collateral_ratio = 1850;
      f2.maximum_short_squeeze_ratio = 1250;

      publish_feed( mpa2_id, feeder_id, f2, feed_icr );

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
      const call_order_object* call2_ptr = borrow( borrower, asset(100000, mpa2_id), asset(2100) );
      BOOST_REQUIRE( call2_ptr );
      call_order_id_type call2_id = call2_ptr->id;

      // Transfer funds to sellers
      transfer( borrower, seller, asset(100000,mpa_id) );
      transfer( borrower, seller, asset(100000,mpa2_id) );

      BOOST_CHECK_EQUAL( call_id(db).debt.value, 100000 );
      BOOST_CHECK_EQUAL( call_id(db).collateral.value, 2000 );
      BOOST_CHECK_EQUAL( call2_id(db).debt.value, 100000 );
      BOOST_CHECK_EQUAL( call2_id(db).collateral.value, 2100 );

      BOOST_CHECK_EQUAL( get_balance( seller_id, mpa_id ), 100000 );
      BOOST_CHECK_EQUAL( get_balance( seller_id, mpa2_id ), 100000 );
      BOOST_CHECK_EQUAL( get_balance( seller_id, asset_id_type() ), 0 );

      // publish a new feed so that borrower's debt position is undercollateralized
      f.settlement_price = price( asset(100000,mpa_id), asset(1650) );
      publish_feed( mpa_id, feeder_id, f, feed_icr );
      // call pays price = 100000:1650 * 1000:1250 = 100000:2062.5 = 48.484848485
      // call match price = 100000:1650 * 1000:1239 = 100000:2048.75 = 48.915303153

      // check
      BOOST_CHECK( mpa_id(db).bitasset_data(db).median_feed.settlement_price == f.settlement_price );
      BOOST_CHECK( mpa_id(db).bitasset_data(db).current_feed.settlement_price == f.settlement_price );
      BOOST_CHECK( !mpa_id(db).bitasset_data(db).has_settlement() );
      BOOST_CHECK( !mpa_id(db).bitasset_data(db).has_individual_settlement() );
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

      // seller settles some
      auto result = force_settle( seller, asset(11100,mpa_id) );
      force_settlement_id_type settle_id = *result.get<extendable_operation_result>().value.new_objects->begin();
      BOOST_REQUIRE( db.find(settle_id) );

      BOOST_CHECK_EQUAL( settle_id(db).balance.amount.value, 11100 );

      result = force_settle( seller, asset(11100,mpa2_id) );
      force_settlement_id_type settle2_id = *result.get<extendable_operation_result>().value.new_objects->begin();
      BOOST_REQUIRE( db.find(settle2_id) );

      BOOST_CHECK_EQUAL( settle2_id(db).balance.amount.value, 11100 );

      BOOST_CHECK_EQUAL( get_balance( seller_id, mpa_id ), 88900 ); // 100000 - 11100
      BOOST_CHECK_EQUAL( get_balance( seller_id, mpa2_id ), 88900 ); // 100000 - 11100
      BOOST_CHECK_EQUAL( get_balance( seller_id, asset_id_type() ), 0 );

      // let the first settle order expire
      generate_blocks( db.head_block_time() + fc::seconds(600) );

      // check
      BOOST_CHECK( mpa_id(db).bitasset_data(db).median_feed.settlement_price == f.settlement_price );
      BOOST_CHECK( mpa_id(db).bitasset_data(db).current_feed.settlement_price == f.settlement_price );
      BOOST_CHECK( !mpa_id(db).bitasset_data(db).has_settlement() );
      BOOST_CHECK( !mpa_id(db).bitasset_data(db).has_individual_settlement() );
      settled_debt = db.find_settled_debt_order(mpa_id);
      BOOST_REQUIRE( settled_debt );

      BOOST_CHECK_EQUAL( settled_debt->for_sale.value, 1983 ); // no change
      BOOST_CHECK_EQUAL( settled_debt->amount_to_receive().amount.value, 100000 );

      // the first settle order is cancelled
      BOOST_REQUIRE( !db.find(settle_id) );

      // no change to the second settle order
      BOOST_REQUIRE( db.find(settle2_id) );
      BOOST_CHECK_EQUAL( settle2_id(db).balance.amount.value, 11100 );

      BOOST_CHECK_EQUAL( get_balance( seller_id, mpa_id ), 100000 );
      BOOST_CHECK_EQUAL( get_balance( seller_id, mpa2_id ), 88900 ); // 100000 - 11100
      BOOST_CHECK_EQUAL( get_balance( seller_id, asset_id_type() ), 0 );

   } catch (fc::exception& e) {
      edump((e.to_detail_string()));
      throw;
   }
}

BOOST_AUTO_TEST_SUITE_END()
