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

   // multiple passes,
   // 0 : individual settlement to order, before hf core-2582
   // 1, 2 : individual settlement to fund, before hf core-2582
   // 3 : individual settlement to order, after hf core-2582
   // 4, 5 : individual settlement to fund, after hf core-2582
   // 6 : individual settlement to order, after hf core-2591
   // 7, 8 : individual settlement to fund, after hf core-2591
   for( int i = 0; i < 9; ++ i )
   {
      idump( (i) );

      if( 3 == i )
      {
         // Advance to core-2582 hard fork
         generate_blocks(HARDFORK_CORE_2582_TIME);
         generate_block();
      }
      else if( 6 == i )
      {
         // Advance to core-2591 hard fork
         generate_blocks(HARDFORK_CORE_2591_TIME);
         generate_block();
      }

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
      uint8_t bsrm_value = ( 0 == ( i % 3 ) ) ? static_cast<uint8_t>(bsrm_type::individual_settlement_to_order)
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
      asset_id_type mpa_id = mpa.get_id();

      if( 0 == ( i % 3 ) )
         BOOST_CHECK( mpa.bitasset_data(db).get_black_swan_response_method()
                      == bsrm_type::individual_settlement_to_order );
      else
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
      BOOST_CHECK( !mpa.bitasset_data(db).is_globally_settled() );
      BOOST_CHECK( !mpa.bitasset_data(db).is_individually_settled_to_fund() );
      BOOST_CHECK( !db.find_settled_debt_order(mpa_id) );

      // borrowers borrow some
      // 100000 / 2000 = 50
      // undercollateralization price = 100000:2000 * 1250:1000 = 100000:1600
      const call_order_object* call_ptr = borrow( borrower, asset(100000, mpa_id), asset(2000) );
      BOOST_REQUIRE( call_ptr );
      call_order_id_type call_id = call_ptr->get_id();

      // 100000 / 2100 = 47.619047619
      // undercollateralization price = 100000:2100 * 1250:1000 = 100000:1680
      const call_order_object* call2_ptr = borrow( borrower2, asset(100000, mpa_id), asset(2100) );
      BOOST_REQUIRE( call2_ptr );
      call_order_id_type call2_id = call2_ptr->get_id();

      // 100000 / 2200 = 45.454545455
      // undercollateralization price = 100000:2200 * 1250:1000 = 100000:1760
      const call_order_object* call3_ptr = borrow( borrower3, asset(100000, mpa_id), asset(2200) );
      BOOST_REQUIRE( call3_ptr );
      call_order_id_type call3_id = call3_ptr->get_id();

      // 100000 / 2500 = 40
      // undercollateralization price = 100000:2500 * 1250:1000 = 100000:2000
      const call_order_object* call4_ptr = borrow( borrower4, asset(100000, mpa_id), asset(2500) );
      BOOST_REQUIRE( call4_ptr );
      call_order_id_type call4_id = call4_ptr->get_id();

      // 100000 / 2240 = 44.642857143
      // undercollateralization price = 100000:2240 * 1250:1000 = 100000:1792
      const call_order_object* call5_ptr = borrow( borrower5, asset(1000000, mpa_id), asset(22400) );
      BOOST_REQUIRE( call5_ptr );
      call_order_id_type call5_id = call5_ptr->get_id();

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
      limit_order_id_type sell_low_id = sell_low->get_id();
      BOOST_CHECK_EQUAL( sell_low_id(db).for_sale.value, 10000 );

      // seller sells some
      const limit_order_object* sell_mid = create_sell_order( seller, asset(100000,mpa_id), asset(2000) );
      BOOST_REQUIRE( sell_mid );
      limit_order_id_type sell_mid_id = sell_mid->get_id();
      BOOST_CHECK_EQUAL( sell_mid_id(db).for_sale.value, 100000 );

      // seller4 sells some
      const limit_order_object* sell_mid2 = create_sell_order( seller4, asset(20000,mpa_id), asset(439) );
      BOOST_REQUIRE( sell_mid2 );
      limit_order_id_type sell_mid2_id = sell_mid2->get_id();
      BOOST_CHECK_EQUAL( sell_mid2_id(db).for_sale.value, 20000 );

      // seller sells some
      const limit_order_object* sell_high = create_sell_order( seller, asset(100000,mpa_id), asset(2400) );
      BOOST_REQUIRE( sell_high );
      limit_order_id_type sell_high_id = sell_high->get_id();
      BOOST_CHECK_EQUAL( sell_high_id(db).for_sale.value, 100000 );

      // seller2 settles
      auto result = force_settle( seller2, asset(50000,mpa_id) );
      force_settlement_id_type settle_id { *result.get<extendable_operation_result>().value.new_objects->begin() };
      BOOST_REQUIRE( db.find( settle_id ) );
      BOOST_CHECK_EQUAL( settle_id(db).balance.amount.value, 50000 );

      // seller3 settles
      result = force_settle( seller3, asset(10000,mpa_id) );
      force_settlement_id_type settle2_id { *result.get<extendable_operation_result>().value.new_objects->begin() };
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

         if( 0 == ( i % 3 ) ) // to order
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
            BOOST_CHECK( !mpa_id(db).bitasset_data(db).is_globally_settled() );
            BOOST_CHECK( !mpa_id(db).bitasset_data(db).is_individually_settled_to_fund() );
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
         else // to fund
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

            // call4 is 100000:2500 = 40
            // will be called if median_feed <= 100000:2500 * 1850:1000 = 74
            // sell_mid2 is matched with call4
            // sell_mid2 is smaller thus fully filled
            BOOST_CHECK( !db.find( sell_mid2_id ) );
            // sell_mid2 gets 439, pays 20000
            // call4 gets 20000, pays round_down(439 * 1250/1239) = 442, margin call fee = 3
            // call4 is now (100000-20000):(2500-442) = 80000:2058 = 38.872691934 (< MSSP 44.444444444)
            // will be called if median_feed <= 80000:2058 * 1850:1000 = 71.914480078

            // sell_high price is 100000/2400 = 41.666666667 (< call match price, so will not match)
            BOOST_REQUIRE( db.find( sell_high_id ) );
            BOOST_CHECK_EQUAL( sell_high_id(db).for_sale.value, 100000 );

            // call4 is not undercollateralized
            BOOST_REQUIRE( db.find( call4_id ) );
            BOOST_CHECK_EQUAL( call4_id(db).debt.value, 80000 );
            BOOST_CHECK_EQUAL( call4_id(db).collateral.value, 2058 );

            // check
            BOOST_CHECK( mpa_id(db).bitasset_data(db).median_feed.settlement_price == f.settlement_price );
            BOOST_CHECK( !mpa_id(db).bitasset_data(db).is_globally_settled() );

            BOOST_CHECK( mpa_id(db).bitasset_data(db).is_individually_settled_to_fund() );
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

      if( ( i >= 3 ) && ( 1 == ( i % 3 ) ) ) // additional tests, only pass after hf core-2582
      {
         set_expiration( db, trx );

         // cancel sell_high
         cancel_limit_order( sell_high_id(db) );

         // publish a new feed so that call4 is undercollateralized
         f.settlement_price = price( asset(80000,mpa_id), asset(2057) );
         publish_feed( mpa_id, feeder_id, f, feed_icr );

         auto check_result_1 = [&]
         {
            BOOST_CHECK( !db.find( call4_id ) );
         };

         check_result_1();

         BOOST_TEST_MESSAGE( "Generate a block again" );
         generate_block();

         check_result_1();

         // reset
         db.pop_block();
      }
      else if( ( i >= 3 ) && ( 2 == ( i % 3 ) ) ) // additional tests. NOTE: infinity loop and OOM before hf core-2582
      {
         set_expiration( db, trx );

         // median feed is 100000/1800 = 55.555555556
         // call pays price = 100000:1800 * 1000:1250 = 100000:2250 = 44.444444444
         // call match price = 100000:1800 * 1000:1239 = 100000:2230.2 = 44.83902789
         // (1 / maintenance collateralization) is 100000/1800/1.85 = 30.03003003

         // current feed is capped at (1130029:24885) * (1239:1000) = 56.263047257
         // call pays price is (1130029:24885) * (1239:1250) = 45.010437806
         // call match price is 1130029/24885 = 45.410046213
         // fake (1 / maintenance collateralization) is (1130029/24885)*(1239/1000)/1.85 = 30.412457977

         // borrower4 adds collateral to call4, and setup target CR
         BOOST_TEST_MESSAGE( "Borrower4 adds collateral" );
         borrow( borrower4_id(db), asset(0,mpa_id), asset(605), 1000 );
         // call4 is now 80000:(2058+605) = 80000:2663 = 30.041306797
         // Its CR is still below required MCR, but above the fake MCR (if calculate with the capped feed)

         // seller4 sells some, this should be matched with call4
         // due to TCR, both it and call4 won't be fully filled
         BOOST_TEST_MESSAGE( "Seller4 sells some" );
         const limit_order_object* sell_mid3 = create_sell_order( seller4, asset(20000,mpa_id), asset(439) );
         BOOST_REQUIRE( sell_mid3 );
         limit_order_id_type sell_mid3_id = sell_mid3->get_id();

         auto check_result_2 = [&]
         {
            BOOST_REQUIRE( db.find( sell_mid3_id ) );
            BOOST_CHECK_LT( sell_mid3_id(db).for_sale.value, 20000 );
            BOOST_REQUIRE( db.find( call4_id ) );
            BOOST_CHECK_LT( call4_id(db).debt.value, 80000 );
         };

         check_result_2();

         BOOST_TEST_MESSAGE( "Generate a block again" );
         generate_block();

         check_result_2();

         // reset
         db.pop_block();
      }

      // reset
      db.pop_block();

   } // for i

} FC_LOG_AND_RETHROW() }

/// Tests individual settlement to fund : if disable_force_settle flag is set,
/// * able to settle if the fund is not empty,
/// * and settle order is cancelled when the fund becomes empty
BOOST_AUTO_TEST_CASE( individual_settlement_to_fund_and_disable_force_settle_test )
{ try {

   // Advance to core-2467 hard fork
   auto mi = db.get_global_properties().parameters.maintenance_interval;
   generate_blocks(HARDFORK_CORE_2467_TIME - mi);
   generate_blocks(db.get_dynamic_global_properties().next_maintenance_time);

   // two passes,
   // i == 0 : with valid feed,
   // i == 1 : no feed
   for( int i = 0; i < 2; ++ i )
   {
      idump( (i) );

      set_expiration( db, trx );

      ACTORS((sam)(feeder)(borrower)(borrower2)(seller));

      auto init_amount = 10000000 * GRAPHENE_BLOCKCHAIN_PRECISION;
      fund( sam, asset(init_amount) );
      fund( feeder, asset(init_amount) );

      using bsrm_type = bitasset_options::black_swan_response_type;
      uint8_t bsrm_value = static_cast<uint8_t>(bsrm_type::individual_settlement_to_fund);

      // Create asset
      asset_id_type samcoin_id = create_user_issued_asset( "SAMCOIN", sam_id(db), charge_market_fee,
                                                           price(asset(1, asset_id_type(1)), asset(1)),
                                                           2, 100 ).get_id(); // fee 1%
      issue_uia( borrower, asset(init_amount, samcoin_id) );
      issue_uia( borrower2, asset(init_amount, samcoin_id) );

      asset_create_operation acop;
      acop.issuer = sam_id;
      acop.symbol = "SAMMPA";
      acop.precision = 2;
      acop.common_options.core_exchange_rate = price(asset(1,asset_id_type(1)),asset(1));
      acop.common_options.max_supply = GRAPHENE_MAX_SHARE_SUPPLY;
      acop.common_options.market_fee_percent = 100; // 1%
      acop.common_options.flags = charge_market_fee | disable_force_settle;
      acop.common_options.issuer_permissions = ASSET_ISSUER_PERMISSION_ENABLE_BITS_MASK;
      acop.bitasset_opts = bitasset_options();
      acop.bitasset_opts->minimum_feeds = 1;
      acop.bitasset_opts->feed_lifetime_sec = 300;
      acop.bitasset_opts->short_backing_asset = samcoin_id;
      acop.bitasset_opts->extensions.value.black_swan_response_method = bsrm_value;
      acop.bitasset_opts->extensions.value.margin_call_fee_ratio = 11;
      acop.bitasset_opts->extensions.value.force_settle_fee_percent = 300;

      trx.operations.clear();
      trx.operations.push_back( acop );
      processed_transaction ptx = PUSH_TX(db, trx, ~0);
      const asset_object& mpa = db.get<asset_object>(ptx.operation_results[0].get<object_id_type>());
      asset_id_type mpa_id = mpa.get_id();

      BOOST_CHECK( mpa.bitasset_data(db).get_black_swan_response_method()
                   == bsrm_type::individual_settlement_to_fund );

      // add a price feed publisher and publish a feed
      update_feed_producers( mpa_id, { feeder_id } );

      price_feed f;
      f.settlement_price = price( asset(100,mpa_id), asset(1,samcoin_id) );
      f.core_exchange_rate = price( asset(100,mpa_id), asset(1) );
      f.maintenance_collateral_ratio = 1850;
      f.maximum_short_squeeze_ratio = 1250;

      uint16_t feed_icr = 1900;

      publish_feed( mpa_id, feeder_id, f, feed_icr );

      BOOST_CHECK( mpa_id(db).bitasset_data(db).median_feed.settlement_price == f.settlement_price );
      BOOST_CHECK( mpa_id(db).bitasset_data(db).current_feed.settlement_price == f.settlement_price );
      BOOST_CHECK( !mpa_id(db).bitasset_data(db).is_current_feed_price_capped() );
      BOOST_CHECK( !mpa_id(db).bitasset_data(db).is_globally_settled() );
      BOOST_CHECK( !mpa_id(db).bitasset_data(db).is_individually_settled_to_fund() );
      BOOST_CHECK( !db.find_settled_debt_order(mpa_id) );

      // borrowers borrow some
      // undercollateralization price = 100000:2000 * 1250:1000 = 100000:1600
      const call_order_object* call_ptr = borrow( borrower, asset(100000, mpa_id), asset(2000, samcoin_id) );
      BOOST_REQUIRE( call_ptr );
      call_order_id_type call_id = call_ptr->get_id();

      // undercollateralization price = 100000:2500 * 1250:1000 = 100000:2000
      const call_order_object* call2_ptr = borrow( borrower2, asset(100000, mpa_id), asset(2500, samcoin_id) );
      BOOST_REQUIRE( call2_ptr );
      call_order_id_type call2_id = call2_ptr->get_id();

      // Transfer funds to sellers
      transfer( borrower, seller, asset(100000,mpa_id) );
      transfer( borrower2, seller, asset(100000,mpa_id) );

      BOOST_CHECK_EQUAL( call_id(db).debt.value, 100000 );
      BOOST_CHECK_EQUAL( call_id(db).collateral.value, 2000 );
      BOOST_CHECK_EQUAL( call2_id(db).debt.value, 100000 );
      BOOST_CHECK_EQUAL( call2_id(db).collateral.value, 2500 );

      BOOST_CHECK_EQUAL( get_balance( seller_id, mpa_id ), 200000 );
      BOOST_CHECK_EQUAL( get_balance( seller_id, samcoin_id ), 0 );

      // Unable to settle when the fund is empty and disable_force_settle is set
      BOOST_CHECK_THROW( force_settle( seller, asset(1000,mpa_id) ), fc::exception );

      // publish a new feed so that borrower's debt position is undercollateralized
      f.settlement_price = price( asset(100000,mpa_id), asset(1650,samcoin_id) );
      publish_feed( mpa_id, feeder_id, f, feed_icr );
      // call pays price = 100000:1650 * 1000:1250 = 100000:2062.5 = 48.484848485
      // call match price = 100000:1650 * 1000:1239 = 100000:2044.35 = 48.915303153

      // check
      BOOST_CHECK( mpa_id(db).bitasset_data(db).median_feed.settlement_price == f.settlement_price );
      BOOST_CHECK( !mpa_id(db).bitasset_data(db).is_globally_settled() );
      BOOST_CHECK( mpa_id(db).bitasset_data(db).is_individually_settled_to_fund() );
      BOOST_CHECK( !db.find_settled_debt_order(mpa_id) );

      // call: margin call fee deducted = round_down(2000*11/1250) = 17,
      // fund receives 2000 - 17 = 1983
      BOOST_CHECK_EQUAL( mpa_id(db).bitasset_data(db).individual_settlement_fund.value, 1983 );
      BOOST_CHECK_EQUAL( mpa_id(db).bitasset_data(db).individual_settlement_debt.value, 100000 );

      BOOST_CHECK( mpa_id(db).bitasset_data(db).current_feed.settlement_price
                   == price( asset(100000*1239,mpa_id), asset(1983*1000,samcoin_id) ) );
      BOOST_CHECK( mpa_id(db).bitasset_data(db).is_current_feed_price_capped() );

      BOOST_CHECK( !db.find( call_id ) );
      BOOST_CHECK_EQUAL( call2_id(db).debt.value, 100000 );
      BOOST_CHECK_EQUAL( call2_id(db).collateral.value, 2500 );

      if( 1 == i ) // let the feed expire
      {
         generate_blocks( db.head_block_time() + fc::seconds(350) );
         set_expiration( db, trx );

         BOOST_CHECK( mpa_id(db).bitasset_data(db).median_feed.settlement_price.is_null() );
         BOOST_CHECK( mpa_id(db).bitasset_data(db).current_feed.settlement_price.is_null() );

         BOOST_CHECK( !mpa_id(db).bitasset_data(db).is_globally_settled() );
         BOOST_CHECK( mpa_id(db).bitasset_data(db).is_individually_settled_to_fund() );
         BOOST_CHECK( !db.find_settled_debt_order(mpa_id) );

         BOOST_CHECK_EQUAL( mpa_id(db).bitasset_data(db).individual_settlement_fund.value, 1983 );
         BOOST_CHECK_EQUAL( mpa_id(db).bitasset_data(db).individual_settlement_debt.value, 100000 );

         BOOST_CHECK( !db.find( call_id ) );
         BOOST_CHECK_EQUAL( call2_id(db).debt.value, 100000 );
         BOOST_CHECK_EQUAL( call2_id(db).collateral.value, 2500 );
      }

      // seller settles some : allowed when fund is not empty
      auto result = force_settle( seller_id(db), asset(10000,mpa_id) );
      auto op_result = result.get<extendable_operation_result>().value;
      // seller gets round_down(10000*1983/100000) = 198, market fee 1, finally gets 197
      // seller pays round_up(198*100000/1983) = 9985
      BOOST_CHECK( !op_result.new_objects.valid() ); // no delayed force settlement
      BOOST_REQUIRE( op_result.paid.valid() && 1U == op_result.paid->size() );
      BOOST_CHECK( *op_result.paid->begin() == asset( 9985, mpa_id ) );
      BOOST_REQUIRE( op_result.received.valid() && 1U == op_result.received->size() );
      BOOST_CHECK( *op_result.received->begin() == asset( 197, samcoin_id ) );
      BOOST_REQUIRE( op_result.fees.valid() && 1U == op_result.fees->size() );
      BOOST_CHECK( *op_result.fees->begin() == asset( 1, samcoin_id ) );
      // fund is now (100000-9985):(1983-198) = 90015:1785

      // check
      BOOST_CHECK_EQUAL( mpa_id(db).bitasset_data(db).individual_settlement_fund.value, 1785 );
      BOOST_CHECK_EQUAL( mpa_id(db).bitasset_data(db).individual_settlement_debt.value, 90015 );

      BOOST_CHECK( !mpa_id(db).bitasset_data(db).is_globally_settled() );
      BOOST_CHECK( mpa_id(db).bitasset_data(db).is_individually_settled_to_fund() );
      BOOST_CHECK( !db.find_settled_debt_order(mpa_id) );

      if( 0 == i )
      {
         BOOST_CHECK( mpa_id(db).bitasset_data(db).median_feed.settlement_price == f.settlement_price );
         BOOST_CHECK( mpa_id(db).bitasset_data(db).current_feed.settlement_price
                      == price( asset(90015*1239,mpa_id), asset(1785*1000,samcoin_id) ) );
         BOOST_CHECK( mpa_id(db).bitasset_data(db).is_current_feed_price_capped() );
      }
      else if( 1 == i )
      {
         BOOST_CHECK( mpa_id(db).bitasset_data(db).median_feed.settlement_price.is_null() );
         BOOST_CHECK( mpa_id(db).bitasset_data(db).current_feed.settlement_price.is_null() );
         BOOST_CHECK( !mpa_id(db).bitasset_data(db).is_current_feed_price_capped() );
      }

      BOOST_CHECK( !db.find( call_id ) );
      BOOST_CHECK_EQUAL( call2_id(db).debt.value, 100000 );
      BOOST_CHECK_EQUAL( call2_id(db).collateral.value, 2500 );

      BOOST_CHECK_EQUAL( get_balance( seller_id, mpa_id ), 190015 ); // 200000 - 9985
      BOOST_CHECK_EQUAL( get_balance( seller_id, samcoin_id ), 197 );

      // seller settles more, more than debt in the fund
      result = force_settle( seller_id(db), asset(150000,mpa_id) );
      op_result = result.get<extendable_operation_result>().value;

      auto check_result = [&]
      {
         // seller gets 99041
         // seller pays 1964
         BOOST_CHECK( !op_result.new_objects.valid() ); // no delayed force settlement
         BOOST_REQUIRE( op_result.paid.valid() && 1U == op_result.paid->size() );
         BOOST_CHECK( *op_result.paid->begin() == asset( 90015, mpa_id ) );
         BOOST_REQUIRE( op_result.received.valid() && 1U == op_result.received->size() );
         BOOST_CHECK( *op_result.received->begin() == asset( 1768, samcoin_id ) );
         BOOST_REQUIRE( op_result.fees.valid() && 1U == op_result.fees->size() );
         BOOST_CHECK( *op_result.fees->begin() == asset( 17, samcoin_id ) );
         // fund is now empty

         BOOST_CHECK_EQUAL( mpa_id(db).bitasset_data(db).individual_settlement_fund.value, 0 );
         BOOST_CHECK_EQUAL( mpa_id(db).bitasset_data(db).individual_settlement_debt.value, 0 );

         BOOST_CHECK( !mpa_id(db).bitasset_data(db).is_globally_settled() );
         BOOST_CHECK( !mpa_id(db).bitasset_data(db).is_individually_settled_to_fund() );
         BOOST_CHECK( !db.find_settled_debt_order(mpa_id) );

         if( 0 == i )
         {
            BOOST_CHECK( mpa_id(db).bitasset_data(db).median_feed.settlement_price == f.settlement_price );
            BOOST_CHECK( mpa_id(db).bitasset_data(db).current_feed.settlement_price == f.settlement_price );
            BOOST_CHECK( !mpa_id(db).bitasset_data(db).is_current_feed_price_capped() );
         }
         else if( 1 == i )
         {
            BOOST_CHECK( mpa_id(db).bitasset_data(db).median_feed.settlement_price.is_null() );
            BOOST_CHECK( mpa_id(db).bitasset_data(db).current_feed.settlement_price.is_null() );
            BOOST_CHECK( !mpa_id(db).bitasset_data(db).is_current_feed_price_capped() );
         }

         BOOST_CHECK( !db.find( call_id ) );
         BOOST_CHECK_EQUAL( call2_id(db).debt.value, 100000 );
         BOOST_CHECK_EQUAL( call2_id(db).collateral.value, 2500 );

         BOOST_CHECK_EQUAL( get_balance( seller_id, mpa_id ), 100000 ); // 200000 - 9985 - 90015
         BOOST_CHECK_EQUAL( get_balance( seller_id, samcoin_id ), 1965 ); // 197 + 1768

         // Unable to settle when the fund is empty and disable_force_settle is set
         BOOST_CHECK_THROW( force_settle( seller, asset(1000,mpa_id) ), fc::exception );

      };

      check_result();

      BOOST_TEST_MESSAGE( "Generate a block" );
      generate_block();

      check_result();

      // reset
      db.pop_block();

   } // for i

} FC_LOG_AND_RETHROW() }

/// Tests individual settlement to fund : if there is no sufficient price feeds,
/// * before core-2587 hard fork, cannot settle an amount more than the fund,
/// * after core-2587 hard fork, can settle an amount more than the fund: only pay from the fund, no settle order.
BOOST_AUTO_TEST_CASE( individual_settlement_to_fund_and_no_feed )
{ try {

   // Advance to core-2467 hard fork
   auto mi = db.get_global_properties().parameters.maintenance_interval;
   generate_blocks(HARDFORK_CORE_2467_TIME - mi);
   generate_blocks(db.get_dynamic_global_properties().next_maintenance_time);

   {
      set_expiration( db, trx );

      ACTORS((sam)(feeder)(borrower)(borrower2)(seller));

      auto init_amount = 10000000 * GRAPHENE_BLOCKCHAIN_PRECISION;
      fund( sam, asset(init_amount) );
      fund( feeder, asset(init_amount) );

      using bsrm_type = bitasset_options::black_swan_response_type;
      uint8_t bsrm_value = static_cast<uint8_t>(bsrm_type::individual_settlement_to_fund);

      // Create asset
      asset_id_type samcoin_id = create_user_issued_asset( "SAMCOIN", sam_id(db), charge_market_fee,
                                                           price(asset(1, asset_id_type(1)), asset(1)),
                                                           2, 100 ).get_id(); // fee 1%
      issue_uia( borrower, asset(init_amount, samcoin_id) );
      issue_uia( borrower2, asset(init_amount, samcoin_id) );

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
      acop.bitasset_opts->feed_lifetime_sec = 300;
      acop.bitasset_opts->short_backing_asset = samcoin_id;
      acop.bitasset_opts->extensions.value.black_swan_response_method = bsrm_value;
      acop.bitasset_opts->extensions.value.margin_call_fee_ratio = 11;
      acop.bitasset_opts->extensions.value.force_settle_fee_percent = 300;

      trx.operations.clear();
      trx.operations.push_back( acop );
      processed_transaction ptx = PUSH_TX(db, trx, ~0);
      const asset_object& mpa = db.get<asset_object>(ptx.operation_results[0].get<object_id_type>());
      asset_id_type mpa_id = mpa.get_id();

      BOOST_CHECK( mpa.bitasset_data(db).get_black_swan_response_method()
                   == bsrm_type::individual_settlement_to_fund );

      // add a price feed publisher and publish a feed
      update_feed_producers( mpa_id, { feeder_id } );

      price_feed f;
      f.settlement_price = price( asset(100,mpa_id), asset(1,samcoin_id) );
      f.core_exchange_rate = price( asset(100,mpa_id), asset(1) );
      f.maintenance_collateral_ratio = 1850;
      f.maximum_short_squeeze_ratio = 1250;

      uint16_t feed_icr = 1900;

      publish_feed( mpa_id, feeder_id, f, feed_icr );

      BOOST_CHECK( mpa_id(db).bitasset_data(db).median_feed.settlement_price == f.settlement_price );
      BOOST_CHECK( mpa_id(db).bitasset_data(db).current_feed.settlement_price == f.settlement_price );
      BOOST_CHECK( !mpa_id(db).bitasset_data(db).is_current_feed_price_capped() );
      BOOST_CHECK( !mpa_id(db).bitasset_data(db).is_globally_settled() );
      BOOST_CHECK( !mpa_id(db).bitasset_data(db).is_individually_settled_to_fund() );
      BOOST_CHECK( !db.find_settled_debt_order(mpa_id) );

      // borrowers borrow some
      // undercollateralization price = 100000:2000 * 1250:1000 = 100000:1600
      const call_order_object* call_ptr = borrow( borrower, asset(100000, mpa_id), asset(2000, samcoin_id) );
      BOOST_REQUIRE( call_ptr );
      call_order_id_type call_id = call_ptr->get_id();

      // undercollateralization price = 100000:2500 * 1250:1000 = 100000:2000
      const call_order_object* call2_ptr = borrow( borrower2, asset(100000, mpa_id), asset(2500, samcoin_id) );
      BOOST_REQUIRE( call2_ptr );
      call_order_id_type call2_id = call2_ptr->get_id();

      // Transfer funds to sellers
      transfer( borrower, seller, asset(100000,mpa_id) );
      transfer( borrower2, seller, asset(100000,mpa_id) );

      BOOST_CHECK_EQUAL( call_id(db).debt.value, 100000 );
      BOOST_CHECK_EQUAL( call_id(db).collateral.value, 2000 );
      BOOST_CHECK_EQUAL( call2_id(db).debt.value, 100000 );
      BOOST_CHECK_EQUAL( call2_id(db).collateral.value, 2500 );

      BOOST_CHECK_EQUAL( get_balance( seller_id, mpa_id ), 200000 );
      BOOST_CHECK_EQUAL( get_balance( seller_id, samcoin_id ), 0 );

      // publish a new feed so that borrower's debt position is undercollateralized
      f.settlement_price = price( asset(100000,mpa_id), asset(1650,samcoin_id) );
      publish_feed( mpa_id, feeder_id, f, feed_icr );
      // call pays price = 100000:1650 * 1000:1250 = 100000:2062.5 = 48.484848485
      // call match price = 100000:1650 * 1000:1239 = 100000:2044.35 = 48.915303153

      // check
      BOOST_CHECK( mpa_id(db).bitasset_data(db).median_feed.settlement_price == f.settlement_price );
      BOOST_CHECK( !mpa_id(db).bitasset_data(db).is_globally_settled() );
      BOOST_CHECK( mpa_id(db).bitasset_data(db).is_individually_settled_to_fund() );
      BOOST_CHECK( !db.find_settled_debt_order(mpa_id) );

      // call: margin call fee deducted = round_down(2000*11/1250) = 17,
      // fund receives 2000 - 17 = 1983
      BOOST_CHECK_EQUAL( mpa_id(db).bitasset_data(db).individual_settlement_fund.value, 1983 );
      BOOST_CHECK_EQUAL( mpa_id(db).bitasset_data(db).individual_settlement_debt.value, 100000 );

      BOOST_CHECK( mpa_id(db).bitasset_data(db).current_feed.settlement_price
                   == price( asset(100000*1239,mpa_id), asset(1983*1000,samcoin_id) ) );
      BOOST_CHECK( mpa_id(db).bitasset_data(db).is_current_feed_price_capped() );

      BOOST_CHECK( !db.find( call_id ) );
      BOOST_CHECK_EQUAL( call2_id(db).debt.value, 100000 );
      BOOST_CHECK_EQUAL( call2_id(db).collateral.value, 2500 );

      // let the feed expire
      {
         generate_blocks( db.head_block_time() + fc::seconds(350) );
         set_expiration( db, trx );

         BOOST_CHECK( mpa_id(db).bitasset_data(db).median_feed.settlement_price.is_null() );
         BOOST_CHECK( mpa_id(db).bitasset_data(db).current_feed.settlement_price.is_null() );

         BOOST_CHECK( !mpa_id(db).bitasset_data(db).is_globally_settled() );
         BOOST_CHECK( mpa_id(db).bitasset_data(db).is_individually_settled_to_fund() );
         BOOST_CHECK( !db.find_settled_debt_order(mpa_id) );

         BOOST_CHECK_EQUAL( mpa_id(db).bitasset_data(db).individual_settlement_fund.value, 1983 );
         BOOST_CHECK_EQUAL( mpa_id(db).bitasset_data(db).individual_settlement_debt.value, 100000 );

         BOOST_CHECK( !db.find( call_id ) );
         BOOST_CHECK_EQUAL( call2_id(db).debt.value, 100000 );
         BOOST_CHECK_EQUAL( call2_id(db).collateral.value, 2500 );
      }

      // Before core-2587 hard fork, unable to settle more than the fund when no feed
      BOOST_CHECK_THROW( force_settle( seller, asset(100001,mpa_id) ), fc::exception );

      // Advance to core-2587 hard fork
      generate_blocks( HARDFORK_CORE_2587_TIME );
      generate_block();
      set_expiration( db, trx );

      // able to settle more than the fund
      auto result = force_settle( seller_id(db), asset(100001,mpa_id) );
      auto op_result = result.get<extendable_operation_result>().value;

      auto check_result = [&]
      {
         // seller gets 1983, market fee 19, finally gets 1964
         // seller pays 100000
         BOOST_CHECK( !op_result.new_objects.valid() ); // no delayed force settlement
         BOOST_REQUIRE( op_result.paid.valid() && 1U == op_result.paid->size() );
         BOOST_CHECK( *op_result.paid->begin() == asset( 100000, mpa_id ) );
         BOOST_REQUIRE( op_result.received.valid() && 1U == op_result.received->size() );
         BOOST_CHECK( *op_result.received->begin() == asset( 1964, samcoin_id ) );
         BOOST_REQUIRE( op_result.fees.valid() && 1U == op_result.fees->size() );
         BOOST_CHECK( *op_result.fees->begin() == asset( 19, samcoin_id ) );
         // fund is now empty

         BOOST_CHECK_EQUAL( mpa_id(db).bitasset_data(db).individual_settlement_fund.value, 0 );
         BOOST_CHECK_EQUAL( mpa_id(db).bitasset_data(db).individual_settlement_debt.value, 0 );

         BOOST_CHECK( !mpa_id(db).bitasset_data(db).is_globally_settled() );
         BOOST_CHECK( !mpa_id(db).bitasset_data(db).is_individually_settled_to_fund() );
         BOOST_CHECK( !db.find_settled_debt_order(mpa_id) );

         BOOST_CHECK( mpa_id(db).bitasset_data(db).median_feed.settlement_price.is_null() );
         BOOST_CHECK( mpa_id(db).bitasset_data(db).current_feed.settlement_price.is_null() );
         BOOST_CHECK( !mpa_id(db).bitasset_data(db).is_current_feed_price_capped() );

         BOOST_CHECK( !db.find( call_id ) );
         BOOST_CHECK_EQUAL( call2_id(db).debt.value, 100000 );
         BOOST_CHECK_EQUAL( call2_id(db).collateral.value, 2500 );

         BOOST_CHECK_EQUAL( get_balance( seller_id, mpa_id ), 100000 ); // 200000 - 100000
         BOOST_CHECK_EQUAL( get_balance( seller_id, samcoin_id ), 1964 );

         // Unable to settle when the fund is empty and no feed
         BOOST_CHECK_THROW( force_settle( seller, asset(1000,mpa_id) ), fc::exception );
      };

      check_result();

      BOOST_TEST_MESSAGE( "Generate a block" );
      generate_block();

      check_result();

   }

} FC_LOG_AND_RETHROW() }

/// Tests individual settlement to fund : settles when price drops, and how taker orders would match after that
BOOST_AUTO_TEST_CASE( individual_settlement_to_fund_and_taking_test )
{ try {

   // Advance to core-2467 hard fork
   auto mi = db.get_global_properties().parameters.maintenance_interval;
   generate_blocks(HARDFORK_CORE_2467_TIME - mi);
   generate_blocks(db.get_dynamic_global_properties().next_maintenance_time);

   // multiple passes,
   // i == 0 : settle more than the amount of debt in fund
   // i == 1 : settle exactly the amount of debt in fund, before hf core-2582
   // i == 2 : settle exactly the amount of debt in fund, after hf core-2582
   for( int i = 0; i < 3; ++ i )
   {
      idump( (i) );

      if( 2 == i )
      {
         // Advance to core-2582 hard fork
         generate_blocks(HARDFORK_CORE_2582_TIME);
         generate_block();
      }

      set_expiration( db, trx );

      ACTORS((sam)(feeder)(borrower)(borrower2)(borrower3)(borrower4)(borrower5)(seller)(seller2));

      auto init_amount = 10000000 * GRAPHENE_BLOCKCHAIN_PRECISION;
      fund( sam, asset(init_amount) );
      fund( feeder, asset(init_amount) );
      fund( borrower, asset(init_amount) );
      fund( borrower2, asset(init_amount) );
      fund( borrower3, asset(init_amount) );
      fund( borrower4, asset(init_amount) );
      fund( borrower5, asset(init_amount) );

      using bsrm_type = bitasset_options::black_swan_response_type;
      uint8_t bsrm_value = static_cast<uint8_t>(bsrm_type::individual_settlement_to_fund);

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
      asset_id_type mpa_id = mpa.get_id();

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
      BOOST_CHECK( !mpa.bitasset_data(db).is_globally_settled() );
      BOOST_CHECK( !mpa.bitasset_data(db).is_individually_settled_to_fund() );
      BOOST_CHECK( !db.find_settled_debt_order(mpa_id) );

      // borrowers borrow some
      // undercollateralization price = 100000:2000 * 1250:1000 = 100000:1600
      const call_order_object* call_ptr = borrow( borrower, asset(100000, mpa_id), asset(2000) );
      BOOST_REQUIRE( call_ptr );
      call_order_id_type call_id = call_ptr->get_id();

      // undercollateralization price = 100000:2100 * 1250:1000 = 100000:1680
      const call_order_object* call2_ptr = borrow( borrower2, asset(100000, mpa_id), asset(2100) );
      BOOST_REQUIRE( call2_ptr );
      call_order_id_type call2_id = call2_ptr->get_id();

      // undercollateralization price = 100000:2200 * 1250:1000 = 100000:1760
      const call_order_object* call3_ptr = borrow( borrower3, asset(100000, mpa_id), asset(2200) );
      BOOST_REQUIRE( call3_ptr );
      call_order_id_type call3_id = call3_ptr->get_id();

      // undercollateralization price = 100000:2500 * 1250:1000 = 100000:2000
      const call_order_object* call4_ptr = borrow( borrower4, asset(100000, mpa_id), asset(2500) );
      BOOST_REQUIRE( call4_ptr );
      call_order_id_type call4_id = call4_ptr->get_id();

      // Transfer funds to sellers
      transfer( borrower, seller, asset(100000,mpa_id) );
      transfer( borrower2, seller, asset(100000,mpa_id) );
      transfer( borrower3, seller2, asset(100000,mpa_id) );
      transfer( borrower4, seller2, asset(100000,mpa_id) );

      BOOST_CHECK_EQUAL( call_id(db).debt.value, 100000 );
      BOOST_CHECK_EQUAL( call_id(db).collateral.value, 2000 );
      BOOST_CHECK_EQUAL( call2_id(db).debt.value, 100000 );
      BOOST_CHECK_EQUAL( call2_id(db).collateral.value, 2100 );
      BOOST_CHECK_EQUAL( call3_id(db).debt.value, 100000 );
      BOOST_CHECK_EQUAL( call3_id(db).collateral.value, 2200 );
      BOOST_CHECK_EQUAL( call4_id(db).debt.value, 100000 );
      BOOST_CHECK_EQUAL( call4_id(db).collateral.value, 2500 );

      BOOST_CHECK_EQUAL( get_balance( seller_id, mpa_id ), 200000 );
      BOOST_CHECK_EQUAL( get_balance( seller_id, asset_id_type() ), 0 );
      BOOST_CHECK_EQUAL( get_balance( seller2_id, mpa_id ), 200000 );
      BOOST_CHECK_EQUAL( get_balance( seller2_id, asset_id_type() ), 0 );

      // publish a new feed so that borrower's debt position is undercollateralized
      f.settlement_price = price( asset(100000,mpa_id), asset(1650) );
      publish_feed( mpa_id, feeder_id, f, feed_icr );
      // call pays price = 100000:1650 * 1000:1250 = 100000:2062.5 = 48.484848485
      // call match price = 100000:1650 * 1000:1239 = 100000:2044.35 = 48.915303153

      // check
      BOOST_CHECK( mpa_id(db).bitasset_data(db).median_feed.settlement_price == f.settlement_price );
      BOOST_CHECK( !mpa_id(db).bitasset_data(db).is_globally_settled() );
      BOOST_CHECK( mpa_id(db).bitasset_data(db).is_individually_settled_to_fund() );
      BOOST_CHECK( !db.find_settled_debt_order(mpa_id) );

      // call: margin call fee deducted = round_down(2000*11/1250) = 17,
      // fund receives 2000 - 17 = 1983
      BOOST_CHECK_EQUAL( mpa_id(db).bitasset_data(db).individual_settlement_fund.value, 1983 );
      BOOST_CHECK_EQUAL( mpa_id(db).bitasset_data(db).individual_settlement_debt.value, 100000 );

      BOOST_CHECK( mpa_id(db).bitasset_data(db).current_feed.settlement_price
                   == price( asset(100000*1239,mpa_id), asset(1983*1000) ) );
      BOOST_CHECK( mpa_id(db).bitasset_data(db).is_current_feed_price_capped() );
      // call pays price  (MSSP) = 100000:1983 * 1239:1250 = 49.984871407
      // call match price (MCOP) = 100000:1983 = 50.428643469

      BOOST_CHECK( !db.find( call_id ) );
      BOOST_CHECK_EQUAL( call2_id(db).debt.value, 100000 );
      BOOST_CHECK_EQUAL( call2_id(db).collateral.value, 2100 );
      BOOST_CHECK_EQUAL( call3_id(db).debt.value, 100000 );
      BOOST_CHECK_EQUAL( call3_id(db).collateral.value, 2200 );
      BOOST_CHECK_EQUAL( call4_id(db).debt.value, 100000 );
      BOOST_CHECK_EQUAL( call4_id(db).collateral.value, 2500 );

      // borrower5 is unable to borrow if CR <= real ICR
      // for median_feed: 1650 * 1.9 = 3135
      // for current_feed: 1983 * 1.9 / 1.239 = 3040.9
      BOOST_CHECK_THROW( borrow( borrower5, asset(100000, mpa_id), asset(3135) ), fc::exception );
      const call_order_object* call5_ptr = borrow( borrower5, asset(100000, mpa_id), asset(3136) );
      BOOST_REQUIRE( call5_ptr );
      call_order_id_type call5_id = call5_ptr->get_id();

      BOOST_CHECK_EQUAL( call5_id(db).debt.value, 100000 );
      BOOST_CHECK_EQUAL( call5_id(db).collateral.value, 3136 );

      // seller sells some
      const limit_order_object* limit_ptr = create_sell_order( seller, asset(80000,mpa_id), asset(100) );
      // the limit order is filled
      BOOST_CHECK( !limit_ptr );

      // call2 is partially filled
      // limit order gets round_down(80000*(1983/100000)) = 1586
      // limit order pays round_up(1586*(100000/1983)) = 79980
      // call2 gets 79980
      // call2 pays round_down(79980*(1983/100000)*(1250/1239)) = 1600, margin call fee = 14
      BOOST_CHECK( !db.find( call_id ) );
      BOOST_CHECK_EQUAL( call2_id(db).debt.value, 20020 ); // 100000 - 79980
      BOOST_CHECK_EQUAL( call2_id(db).collateral.value, 500 ); // 2100 - 1600
      // 20020 / 500 = 40.04
      BOOST_CHECK_EQUAL( call3_id(db).debt.value, 100000 );
      BOOST_CHECK_EQUAL( call3_id(db).collateral.value, 2200 );
      // 100000 / 2200 = 45.454545455
      BOOST_CHECK_EQUAL( call4_id(db).debt.value, 100000 );
      BOOST_CHECK_EQUAL( call4_id(db).collateral.value, 2500 );

      BOOST_CHECK_EQUAL( get_balance( seller_id, mpa_id ), 120020 ); // 200000 - 79980
      BOOST_CHECK_EQUAL( get_balance( seller_id, asset_id_type() ), 1586 );
      BOOST_CHECK_EQUAL( get_balance( seller2_id, mpa_id ), 200000 );
      BOOST_CHECK_EQUAL( get_balance( seller2_id, asset_id_type() ), 0 );

      // seller sells more, this order is below MCOP so will not be matched right now
      limit_ptr = create_sell_order( seller, asset(100000,mpa_id), asset(2000) );
      // the limit order is not filled
      BOOST_REQUIRE( limit_ptr );
      limit_order_id_type limit_id = limit_ptr->get_id();

      BOOST_CHECK_EQUAL( limit_ptr->for_sale.value, 100000 );

      // unable to settle too little amount
      BOOST_CHECK_THROW( force_settle( seller2, asset(50,mpa_id) ), fc::exception );

      // seller2 settles
      share_type amount_to_settle = ( 0 == i ? 150000 : 100000 );
      if( 1 == i ) // it will fail
      {
         BOOST_REQUIRE_THROW( force_settle( seller2, asset(amount_to_settle, mpa_id) ), fc::exception );
         generate_block();
         db.pop_block();
         continue;
      }
      auto result = force_settle( seller2, asset(amount_to_settle, mpa_id) );
      auto op_result = result.get<extendable_operation_result>().value;

      auto check_result = [&]
      {
         // seller2 gets 1983
         // seller2 pays 100000
         force_settlement_id_type settle_id;
         if( 0 == i )
         {
            BOOST_REQUIRE( op_result.new_objects.valid() ); // force settlement order created
            settle_id = *op_result.new_objects->begin();
         }
         else if ( 2 == i )
            BOOST_CHECK( !op_result.new_objects.valid() ); // force settlement order not created

         BOOST_REQUIRE( op_result.paid.valid() && 1U == op_result.paid->size() );
         BOOST_CHECK( *op_result.paid->begin() == asset( 100000, mpa_id ) );
         BOOST_REQUIRE( op_result.received.valid() && 1U == op_result.received->size() );
         BOOST_CHECK( *op_result.received->begin() == asset( 1983 ) );
         // fund is now empty

         // check
         BOOST_CHECK( mpa_id(db).bitasset_data(db).median_feed.settlement_price == f.settlement_price );
         BOOST_CHECK( !mpa_id(db).bitasset_data(db).is_globally_settled() );
         BOOST_CHECK( !mpa_id(db).bitasset_data(db).is_individually_settled_to_fund() );
         BOOST_CHECK( !db.find_settled_debt_order(mpa_id) );

         BOOST_CHECK_EQUAL( mpa_id(db).bitasset_data(db).individual_settlement_fund.value, 0 );
         BOOST_CHECK_EQUAL( mpa_id(db).bitasset_data(db).individual_settlement_debt.value, 0 );

         BOOST_CHECK( mpa_id(db).bitasset_data(db).current_feed.settlement_price == f.settlement_price );
         BOOST_CHECK( !mpa_id(db).bitasset_data(db).is_current_feed_price_capped() );

         // the individual settlement fund is now empty, so the price feed is no longer capped
         // call3 is the least collateralized short, matched with the limit order, both filled
         BOOST_CHECK( !db.find(call3_id) );
         BOOST_CHECK( !db.find(limit_id) );
         // same size, consider call3 as smaller
         // call3 match price 100000:2000
         // call3 gets 100000, pays round_up(2000 * 1250/1239) = 2018, margin call fee 18

         if( 0 == i )
         {
            // settle order is matched with call2
            // call2 is smaller
            // call2 gets 20020, pays round_up(20020 * (1650/100000) * (1250/1000)) = 413
            // settle order gets round_up(20020 * (1650/100000) * (1239/1000)) = 410, margin call fee = 3

            // settle order is matched with call4
            // settle order is smaller
            BOOST_CHECK( !db.find(settle_id) );
            // settle order gets round_down((50000-20020) * (1650/100000) * (1239/1000)) = 612
            // settle order pays round_up(612 * (100000/1650) * (1000/1239)) = 29937
            // call4 gets 29937
            // call4 pays round_down(29937 * (1650/100000) * (1250/1000)) = 617, margin call fee = 5
            // call4 is now (100000-29937):(2500-617) = 70063:1883
            BOOST_CHECK_EQUAL( call4_id(db).debt.value, 70063 );
            BOOST_CHECK_EQUAL( call4_id(db).collateral.value, 1883 );

            BOOST_CHECK_EQUAL( get_balance( seller_id, mpa_id ), 20020 ); // 200000 - 79980 - 100000
            BOOST_CHECK_EQUAL( get_balance( seller_id, asset_id_type() ), 3586 ); // 1586 + 2000
            BOOST_CHECK_EQUAL( get_balance( seller2_id, mpa_id ), 50043 ); // 200000 - 100000 - 20020 - 29937
            BOOST_CHECK_EQUAL( get_balance( seller2_id, asset_id_type() ), 3005 ); // 1983 + 410 + 612

            BOOST_CHECK_EQUAL( get_balance( borrower_id, asset_id_type() ), init_amount - 2000 );
            BOOST_CHECK_EQUAL( get_balance( borrower2_id, asset_id_type() ), init_amount - 2013 ); // refund some
            BOOST_CHECK_EQUAL( get_balance( borrower3_id, asset_id_type() ), init_amount - 2018 ); // refund some
            BOOST_CHECK_EQUAL( get_balance( borrower4_id, asset_id_type() ), init_amount - 2500 );
            BOOST_CHECK_EQUAL( get_balance( borrower5_id, asset_id_type() ), init_amount - 3136 );
         }
         else if ( 2 == i )
         {
            // no change to other call orders
            BOOST_CHECK_EQUAL( call2_id(db).debt.value, 20020 );
            BOOST_CHECK_EQUAL( call2_id(db).collateral.value, 500 );
            BOOST_CHECK_EQUAL( call4_id(db).debt.value, 100000 );
            BOOST_CHECK_EQUAL( call4_id(db).collateral.value, 2500 );

            BOOST_CHECK_EQUAL( get_balance( seller_id, mpa_id ), 20020 ); // 200000 - 79980 - 100000
            BOOST_CHECK_EQUAL( get_balance( seller_id, asset_id_type() ), 3586 ); // 1586 + 2000
            BOOST_CHECK_EQUAL( get_balance( seller2_id, mpa_id ), 100000 ); // 200000 - 100000
            BOOST_CHECK_EQUAL( get_balance( seller2_id, asset_id_type() ), 1983 );

            BOOST_CHECK_EQUAL( get_balance( borrower_id, asset_id_type() ), init_amount - 2000 );
            BOOST_CHECK_EQUAL( get_balance( borrower2_id, asset_id_type() ), init_amount - 2100 );
            BOOST_CHECK_EQUAL( get_balance( borrower3_id, asset_id_type() ), init_amount - 2018 ); // refund some
            BOOST_CHECK_EQUAL( get_balance( borrower4_id, asset_id_type() ), init_amount - 2500 );
            BOOST_CHECK_EQUAL( get_balance( borrower5_id, asset_id_type() ), init_amount - 3136 );
         }
      };

      check_result();

      BOOST_TEST_MESSAGE( "Generate a block" );
      generate_block();

      check_result();

      // reset
      db.pop_block();

   } // for i

} FC_LOG_AND_RETHROW() }

/// Tests individual settlement to fund:
/// * Before hf core-2591, forced-settlements are filled at individual settlement fund price
/// * After hf core-2591, forced-settlements are filled at margin call order price (MCOP)
BOOST_AUTO_TEST_CASE( individual_settlement_to_fund_and_taking_price_test )
{ try {

   // Advance to a recent hard fork
   generate_blocks(HARDFORK_CORE_2582_TIME);
   generate_block();

   // multiple passes,
   // i == 0 : before hf core-2591, settle less than the amount of debt in fund
   // i == 1 : before hf core-2591, settle exactly the amount of debt in fund
   // i == 2 : before hf core-2591, settle more than the amount of debt in fund
   // i == 3 : after hf core-2591, settle less than the amount of debt in fund
   // i == 4 : after hf core-2591, settle exactly the amount of debt in fund
   // i == 5 : after hf core-2591, settle more than the amount of debt in fund
   for( int i = 0; i < 6; ++ i )
   {
      idump( (i) );

      if( 3 == i )
      {
         // Advance to core-2591 hard fork
         generate_blocks(HARDFORK_CORE_2591_TIME);
         generate_block();
      }

      set_expiration( db, trx );

      ACTORS((sam)(feeder)(borrower)(borrower2)(borrower3)(borrower4)(borrower5)(seller)(seller2));

      auto init_amount = 10000000 * GRAPHENE_BLOCKCHAIN_PRECISION;
      fund( sam, asset(init_amount) );
      fund( feeder, asset(init_amount) );
      fund( borrower, asset(init_amount) );
      fund( borrower2, asset(init_amount) );
      fund( borrower3, asset(init_amount) );
      fund( borrower4, asset(init_amount) );
      fund( borrower5, asset(init_amount) );

      using bsrm_type = bitasset_options::black_swan_response_type;
      uint8_t bsrm_value = static_cast<uint8_t>(bsrm_type::individual_settlement_to_fund);

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
      asset_id_type mpa_id = mpa.get_id();

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

      BOOST_CHECK( mpa_id(db).bitasset_data(db).median_feed.settlement_price == f.settlement_price );
      BOOST_CHECK( mpa_id(db).bitasset_data(db).current_feed.settlement_price == f.settlement_price );
      BOOST_CHECK( !mpa_id(db).bitasset_data(db).is_current_feed_price_capped() );
      BOOST_CHECK( !mpa_id(db).bitasset_data(db).is_globally_settled() );
      BOOST_CHECK( !mpa_id(db).bitasset_data(db).is_individually_settled_to_fund() );
      BOOST_CHECK( !db.find_settled_debt_order(mpa_id) );

      // borrowers borrow some
      // undercollateralization price = 100000:2000 * 1250:1000 = 100000:1600
      const call_order_object* call_ptr = borrow( borrower, asset(100000, mpa_id), asset(2000) );
      BOOST_REQUIRE( call_ptr );
      call_order_id_type call_id = call_ptr->get_id();

      // undercollateralization price = 100000:2100 * 1250:1000 = 100000:1680
      const call_order_object* call2_ptr = borrow( borrower2, asset(100000, mpa_id), asset(2100) );
      BOOST_REQUIRE( call2_ptr );
      call_order_id_type call2_id = call2_ptr->get_id();

      // undercollateralization price = 100000:2200 * 1250:1000 = 100000:1760
      const call_order_object* call3_ptr = borrow( borrower3, asset(100000, mpa_id), asset(2200) );
      BOOST_REQUIRE( call3_ptr );
      call_order_id_type call3_id = call3_ptr->get_id();

      // undercollateralization price = 100000:2500 * 1250:1000 = 100000:2000
      const call_order_object* call4_ptr = borrow( borrower4, asset(100000, mpa_id), asset(2500) );
      BOOST_REQUIRE( call4_ptr );
      call_order_id_type call4_id = call4_ptr->get_id();

      // Transfer funds to sellers
      transfer( borrower, seller, asset(100000,mpa_id) );
      transfer( borrower2, seller, asset(100000,mpa_id) );
      transfer( borrower3, seller2, asset(100000,mpa_id) );
      transfer( borrower4, seller2, asset(100000,mpa_id) );

      BOOST_CHECK_EQUAL( call_id(db).debt.value, 100000 );
      BOOST_CHECK_EQUAL( call_id(db).collateral.value, 2000 );
      BOOST_CHECK_EQUAL( call2_id(db).debt.value, 100000 );
      BOOST_CHECK_EQUAL( call2_id(db).collateral.value, 2100 );
      BOOST_CHECK_EQUAL( call3_id(db).debt.value, 100000 );
      BOOST_CHECK_EQUAL( call3_id(db).collateral.value, 2200 );
      BOOST_CHECK_EQUAL( call4_id(db).debt.value, 100000 );
      BOOST_CHECK_EQUAL( call4_id(db).collateral.value, 2500 );

      BOOST_CHECK_EQUAL( get_balance( seller_id, mpa_id ), 200000 );
      BOOST_CHECK_EQUAL( get_balance( seller_id, asset_id_type() ), 0 );
      BOOST_CHECK_EQUAL( get_balance( seller2_id, mpa_id ), 200000 );
      BOOST_CHECK_EQUAL( get_balance( seller2_id, asset_id_type() ), 0 );

      // publish a new feed so that borrower's debt position is undercollateralized
      f.settlement_price = price( asset(100000,mpa_id), asset(1650) );
      publish_feed( mpa_id, feeder_id, f, feed_icr );
      // call pays price = 100000:1650 * 1000:1250 = 100000:2062.5 = 48.484848485
      // call match price = 100000:1650 * 1000:1239 = 100000:2044.35 = 48.915303153

      // check
      BOOST_CHECK( mpa_id(db).bitasset_data(db).median_feed.settlement_price == f.settlement_price );
      BOOST_CHECK( !mpa_id(db).bitasset_data(db).is_globally_settled() );
      BOOST_CHECK( mpa_id(db).bitasset_data(db).is_individually_settled_to_fund() );
      BOOST_CHECK( !db.find_settled_debt_order(mpa_id) );

      // call: margin call fee deducted = round_down(2000*11/1250) = 17,
      // fund receives 2000 - 17 = 1983
      BOOST_CHECK_EQUAL( mpa_id(db).bitasset_data(db).individual_settlement_fund.value, 1983 );
      BOOST_CHECK_EQUAL( mpa_id(db).bitasset_data(db).individual_settlement_debt.value, 100000 );

      BOOST_CHECK( mpa_id(db).dynamic_data(db).accumulated_collateral_fees == 17 );

      BOOST_CHECK( mpa_id(db).bitasset_data(db).current_feed.settlement_price
                   == price( asset(100000*1239,mpa_id), asset(1983*1000) ) );
      BOOST_CHECK( mpa_id(db).bitasset_data(db).is_current_feed_price_capped() );
      // call pays price  (MSSP) = 100000:1983 * 1239:1250 = 49.984871407
      // call match price (MCOP) = 100000:1983 = 50.428643469

      BOOST_CHECK( !db.find( call_id ) );
      BOOST_CHECK_EQUAL( call2_id(db).debt.value, 100000 );
      BOOST_CHECK_EQUAL( call2_id(db).collateral.value, 2100 );
      BOOST_CHECK_EQUAL( call3_id(db).debt.value, 100000 );
      BOOST_CHECK_EQUAL( call3_id(db).collateral.value, 2200 );
      BOOST_CHECK_EQUAL( call4_id(db).debt.value, 100000 );
      BOOST_CHECK_EQUAL( call4_id(db).collateral.value, 2500 );

      // borrower5 is unable to borrow if CR <= real ICR
      // for median_feed: 1650 * 1.9 = 3135
      // for current_feed: 1983 * 1.9 / 1.239 = 3040.9
      BOOST_CHECK_THROW( borrow( borrower5, asset(100000, mpa_id), asset(3135) ), fc::exception );
      const call_order_object* call5_ptr = borrow( borrower5, asset(100000, mpa_id), asset(3136) );
      BOOST_REQUIRE( call5_ptr );
      call_order_id_type call5_id = call5_ptr->get_id();

      BOOST_CHECK_EQUAL( call5_id(db).debt.value, 100000 );
      BOOST_CHECK_EQUAL( call5_id(db).collateral.value, 3136 );

      // seller sells some
      const limit_order_object* limit_ptr = create_sell_order( seller, asset(80000,mpa_id), asset(100) );
      // the limit order is filled
      BOOST_CHECK( !limit_ptr );

      // call2 is partially filled
      // limit order gets round_down(80000*(1983/100000)) = 1586
      // limit order pays round_up(1586*(100000/1983)) = 79980
      // call2 gets 79980
      // call2 pays round_down(79980*(1983/100000)*(1250/1239)) = 1600, margin call fee = 14
      BOOST_CHECK( !db.find( call_id ) );
      BOOST_CHECK_EQUAL( call2_id(db).debt.value, 20020 ); // 100000 - 79980
      BOOST_CHECK_EQUAL( call2_id(db).collateral.value, 500 ); // 2100 - 1600
      // 20020 / 500 = 40.04
      BOOST_CHECK_EQUAL( call3_id(db).debt.value, 100000 );
      BOOST_CHECK_EQUAL( call3_id(db).collateral.value, 2200 );
      // 100000 / 2200 = 45.454545455
      BOOST_CHECK_EQUAL( call4_id(db).debt.value, 100000 );
      BOOST_CHECK_EQUAL( call4_id(db).collateral.value, 2500 );

      BOOST_CHECK_EQUAL( get_balance( seller_id, mpa_id ), 120020 ); // 200000 - 79980
      BOOST_CHECK_EQUAL( get_balance( seller_id, asset_id_type() ), 1586 );
      BOOST_CHECK_EQUAL( get_balance( seller2_id, mpa_id ), 200000 );
      BOOST_CHECK_EQUAL( get_balance( seller2_id, asset_id_type() ), 0 );

      BOOST_CHECK( mpa_id(db).dynamic_data(db).accumulated_collateral_fees == 31 ); // 17 + 14

      // seller sells more, this order is below MCOP so will not be matched right now
      limit_ptr = create_sell_order( seller, asset(100000,mpa_id), asset(2000) );
      // the limit order is not filled
      BOOST_REQUIRE( limit_ptr );
      limit_order_id_type limit_id = limit_ptr->get_id();

      BOOST_CHECK_EQUAL( limit_ptr->for_sale.value, 100000 );

      // unable to settle too little amount
      BOOST_CHECK_THROW( force_settle( seller2, asset(50,mpa_id) ), fc::exception );

      // publish a new feed so that current_feed is no longer capped
      f.settlement_price = price( asset(100000,mpa_id), asset(1450) );
      publish_feed( mpa_id, feeder_id, f, feed_icr );
      // call pays price  (MSSP) = 100000:1450 * 1000:1250 = 10000000:181250 = 55.172413793
      // call match price (MCOP) = 100000:1450 * 1000:1239 = 10000000:179655 = 55.662241518

      // check
      BOOST_CHECK( mpa_id(db).bitasset_data(db).median_feed.settlement_price == f.settlement_price );
      BOOST_CHECK( !mpa_id(db).bitasset_data(db).is_globally_settled() );
      BOOST_CHECK( mpa_id(db).bitasset_data(db).is_individually_settled_to_fund() );
      BOOST_CHECK( !db.find_settled_debt_order(mpa_id) );

      BOOST_CHECK_EQUAL( mpa_id(db).bitasset_data(db).individual_settlement_fund.value, 1983 );
      BOOST_CHECK_EQUAL( mpa_id(db).bitasset_data(db).individual_settlement_debt.value, 100000 );

      BOOST_CHECK( mpa_id(db).bitasset_data(db).current_feed.settlement_price == f.settlement_price );
      BOOST_CHECK( !mpa_id(db).bitasset_data(db).is_current_feed_price_capped() );

      const auto& get_amount_to_settle = [&]() {
         switch(i) {
         case 0:
         case 3:
            return 90000;
         case 1:
         case 4:
            return 100000;
         case 2:
         case 5:
         default:
            return 110000;
         }
      };

      // seller2 settles
      share_type amount_to_settle = get_amount_to_settle();
      auto result = force_settle( seller2, asset(amount_to_settle, mpa_id) );
      auto op_result = result.get<extendable_operation_result>().value;

      auto check_result = [&]
      {
         force_settlement_id_type settle_id;
         if( 0 == i )
         {
            BOOST_CHECK( !op_result.new_objects.valid() ); // force settlement order not created

            // receives = round_down(90000 * 1983 / 100000) = 1784
            // pays = round_up(1784 * 100000 / 1983) = 89965
            // settlement fund = 1983 - 1784 = 199
            // settlement debt = 100000 - 89965 = 10035
            BOOST_REQUIRE( op_result.paid.valid() && 1U == op_result.paid->size() );
            BOOST_CHECK( *op_result.paid->begin() == asset( 89965, mpa_id ) );
            BOOST_REQUIRE( op_result.received.valid() && 1U == op_result.received->size() );
            BOOST_CHECK( *op_result.received->begin() == asset( 1784 ) );

            BOOST_CHECK( mpa_id(db).bitasset_data(db).is_individually_settled_to_fund() );
            BOOST_CHECK_EQUAL( mpa_id(db).bitasset_data(db).individual_settlement_fund.value, 199 );
            BOOST_CHECK_EQUAL( mpa_id(db).bitasset_data(db).individual_settlement_debt.value, 10035 );

            BOOST_CHECK_EQUAL( call2_id(db).debt.value, 20020 );
            BOOST_CHECK_EQUAL( call2_id(db).collateral.value, 500 );
            // 20020 / 500 = 40.04
            BOOST_CHECK_EQUAL( call3_id(db).debt.value, 100000 );
            BOOST_CHECK_EQUAL( call3_id(db).collateral.value, 2200 );
            // 100000 / 2200 = 45.454545455

            BOOST_REQUIRE( db.find(limit_id) );
            BOOST_CHECK_EQUAL( limit_id(db).for_sale.value, 100000 );

            BOOST_CHECK( mpa_id(db).dynamic_data(db).accumulated_collateral_fees == 31 ); // 17 + 14

            BOOST_CHECK_EQUAL( get_balance( seller_id, mpa_id ), 20020 ); // 200000 - 79980 - 100000
            BOOST_CHECK_EQUAL( get_balance( seller_id, asset_id_type() ), 1586 );
            BOOST_CHECK_EQUAL( get_balance( seller2_id, mpa_id ), 110035 ); // 200000 - 89965
            BOOST_CHECK_EQUAL( get_balance( seller2_id, asset_id_type() ), 1784 );
         }
         else if( 1 == i )
         {
            BOOST_CHECK( !op_result.new_objects.valid() ); // force settlement order not created

            BOOST_REQUIRE( op_result.paid.valid() && 1U == op_result.paid->size() );
            BOOST_CHECK( *op_result.paid->begin() == asset( 100000, mpa_id ) );
            BOOST_REQUIRE( op_result.received.valid() && 1U == op_result.received->size() );
            BOOST_CHECK( *op_result.received->begin() == asset( 1983 ) );

            BOOST_CHECK( !mpa_id(db).bitasset_data(db).is_individually_settled_to_fund() );
            BOOST_CHECK_EQUAL( mpa_id(db).bitasset_data(db).individual_settlement_fund.value, 0 );
            BOOST_CHECK_EQUAL( mpa_id(db).bitasset_data(db).individual_settlement_debt.value, 0 );

            BOOST_CHECK_EQUAL( call2_id(db).debt.value, 20020 );
            BOOST_CHECK_EQUAL( call2_id(db).collateral.value, 500 );
            // 20020 / 500 = 40.04
            BOOST_CHECK_EQUAL( call3_id(db).debt.value, 100000 );
            BOOST_CHECK_EQUAL( call3_id(db).collateral.value, 2200 );
            // 100000 / 2200 = 45.454545455

            BOOST_REQUIRE( db.find(limit_id) );
            BOOST_CHECK_EQUAL( limit_id(db).for_sale.value, 100000 );

            BOOST_CHECK( mpa_id(db).dynamic_data(db).accumulated_collateral_fees == 31 ); // 17 + 14

            BOOST_CHECK_EQUAL( get_balance( seller_id, mpa_id ), 20020 ); // 200000 - 79980 - 100000
            BOOST_CHECK_EQUAL( get_balance( seller_id, asset_id_type() ), 1586 );
            BOOST_CHECK_EQUAL( get_balance( seller2_id, mpa_id ), 100000 ); // 200000 - 100000
            BOOST_CHECK_EQUAL( get_balance( seller2_id, asset_id_type() ), 1983 );
         }
         else if( 2 == i )
         {
            // force settlement order created
            BOOST_REQUIRE( op_result.new_objects.valid() && 1U == op_result.new_objects->size() );
            settle_id = *op_result.new_objects->begin();

            BOOST_REQUIRE( op_result.paid.valid() && 1U == op_result.paid->size() );
            BOOST_CHECK( *op_result.paid->begin() == asset( 100000, mpa_id ) );
            BOOST_REQUIRE( op_result.received.valid() && 1U == op_result.received->size() );
            BOOST_CHECK( *op_result.received->begin() == asset( 1983 ) );

            BOOST_CHECK( !mpa_id(db).bitasset_data(db).is_individually_settled_to_fund() );
            BOOST_CHECK_EQUAL( mpa_id(db).bitasset_data(db).individual_settlement_fund.value, 0 );
            BOOST_CHECK_EQUAL( mpa_id(db).bitasset_data(db).individual_settlement_debt.value, 0 );

            // settle order is matched with call3
            // settle order is smaller
            BOOST_CHECK( !db.find(settle_id) );
            // settle order gets round_down((110000-100000) * (1450/100000) * (1239/1000)) = 179
            // settle order pays round_up(179 * (100000/1450) * (1000/1239)) = 9964
            // call3 gets 9964
            // call3 pays round_down(9964 * (1450/100000) * (1250/1000)) = 180, margin call fee = 1
            // call3 is now (100000-9964):(2200-180) = 90036:2020
            BOOST_CHECK_EQUAL( call2_id(db).debt.value, 20020 );
            BOOST_CHECK_EQUAL( call2_id(db).collateral.value, 500 );
            // 20020 / 500 = 40.04
            BOOST_CHECK_EQUAL( call3_id(db).debt.value, 90036 );
            BOOST_CHECK_EQUAL( call3_id(db).collateral.value, 2020 );
            // 90036 / 2020 = 44.572277228

            BOOST_REQUIRE( db.find(limit_id) );
            BOOST_CHECK_EQUAL( limit_id(db).for_sale.value, 100000 );

            BOOST_CHECK( mpa_id(db).dynamic_data(db).accumulated_collateral_fees == 32 ); // 17 + 14 + 1

            BOOST_CHECK_EQUAL( get_balance( seller_id, mpa_id ), 20020 ); // 200000 - 79980 - 100000
            BOOST_CHECK_EQUAL( get_balance( seller_id, asset_id_type() ), 1586 );
            BOOST_CHECK_EQUAL( get_balance( seller2_id, mpa_id ), 90036 ); // 200000 - 100000 - 9964
            BOOST_CHECK_EQUAL( get_balance( seller2_id, asset_id_type() ), 2162 ); // 1983 + 179

            BOOST_CHECK_EQUAL( get_balance( borrower_id, asset_id_type() ), init_amount - 2000 );
            BOOST_CHECK_EQUAL( get_balance( borrower2_id, asset_id_type() ), init_amount - 2100 );
            BOOST_CHECK_EQUAL( get_balance( borrower3_id, asset_id_type() ), init_amount - 2200 );
            BOOST_CHECK_EQUAL( get_balance( borrower4_id, asset_id_type() ), init_amount - 2500 );
            BOOST_CHECK_EQUAL( get_balance( borrower5_id, asset_id_type() ), init_amount - 3136 );
         }
         else if( 3 == i )
         {
            BOOST_CHECK( !op_result.new_objects.valid() ); // force settlement order not created

            // settlement fund pays = round_down(90000 * 1983 / 100000) = 1784
            // seller2 pays = round_up(1784 * 100000 / 1983) = 89965
            // settlement fund = 1983 - 1784 = 199
            // settlement debt = 100000 - 89965 = 10035
            // seller2 would receive = round_up(89965 * 179655 / 10000000 ) = 1617 (<1784, so ok)
            // collateral fee = 1784 - 1617 = 167
            BOOST_REQUIRE( op_result.paid.valid() && 1U == op_result.paid->size() );
            BOOST_CHECK( *op_result.paid->begin() == asset( 89965, mpa_id ) );
            BOOST_REQUIRE( op_result.received.valid() && 1U == op_result.received->size() );
            BOOST_CHECK( *op_result.received->begin() == asset( 1617 ) );
            BOOST_REQUIRE( op_result.fees.valid() && 2U == op_result.fees->size() );
            BOOST_CHECK( *op_result.fees->begin() == asset( 167 ) );

            BOOST_CHECK( mpa_id(db).bitasset_data(db).is_individually_settled_to_fund() );
            BOOST_CHECK_EQUAL( mpa_id(db).bitasset_data(db).individual_settlement_fund.value, 199 );
            BOOST_CHECK_EQUAL( mpa_id(db).bitasset_data(db).individual_settlement_debt.value, 10035 );

            BOOST_CHECK_EQUAL( call2_id(db).debt.value, 20020 );
            BOOST_CHECK_EQUAL( call2_id(db).collateral.value, 500 );
            // 20020 / 500 = 40.04
            BOOST_CHECK_EQUAL( call3_id(db).debt.value, 100000 );
            BOOST_CHECK_EQUAL( call3_id(db).collateral.value, 2200 );
            // 100000 / 2200 = 45.454545455

            BOOST_REQUIRE( db.find(limit_id) );
            BOOST_CHECK_EQUAL( limit_id(db).for_sale.value, 100000 );

            BOOST_CHECK( mpa_id(db).dynamic_data(db).accumulated_collateral_fees == 198 ); // 17 + 14 + 167

            BOOST_CHECK_EQUAL( get_balance( seller_id, mpa_id ), 20020 ); // 200000 - 79980 - 100000
            BOOST_CHECK_EQUAL( get_balance( seller_id, asset_id_type() ), 1586 );
            BOOST_CHECK_EQUAL( get_balance( seller2_id, mpa_id ), 110035 ); // 200000 - 89965
            BOOST_CHECK_EQUAL( get_balance( seller2_id, asset_id_type() ), 1617 );
         }
         else if( 4 == i )
         {
            BOOST_CHECK( !op_result.new_objects.valid() ); // force settlement order not created

            // settlement fund pays = 1983
            // seller2 pays = 100000
            // settlement fund = 0
            // settlement debt = 0
            // seller2 would receive = round_up(100000 * 179655 / 10000000 ) = 1797 (<1983, so ok)
            // collateral fee = 1983 - 1797 = 186
            BOOST_REQUIRE( op_result.paid.valid() && 1U == op_result.paid->size() );
            BOOST_CHECK( *op_result.paid->begin() == asset( 100000, mpa_id ) );
            BOOST_REQUIRE( op_result.received.valid() && 1U == op_result.received->size() );
            BOOST_CHECK( *op_result.received->begin() == asset( 1797 ) );
            BOOST_REQUIRE( op_result.fees.valid() && 2U == op_result.fees->size() );
            BOOST_CHECK( *op_result.fees->begin() == asset( 186 ) );

            BOOST_CHECK( !mpa_id(db).bitasset_data(db).is_individually_settled_to_fund() );
            BOOST_CHECK_EQUAL( mpa_id(db).bitasset_data(db).individual_settlement_fund.value, 0 );
            BOOST_CHECK_EQUAL( mpa_id(db).bitasset_data(db).individual_settlement_debt.value, 0 );

            BOOST_CHECK_EQUAL( call2_id(db).debt.value, 20020 );
            BOOST_CHECK_EQUAL( call2_id(db).collateral.value, 500 );
            // 20020 / 500 = 40.04
            BOOST_CHECK_EQUAL( call3_id(db).debt.value, 100000 );
            BOOST_CHECK_EQUAL( call3_id(db).collateral.value, 2200 );
            // 100000 / 2200 = 45.454545455

            BOOST_REQUIRE( db.find(limit_id) );
            BOOST_CHECK_EQUAL( limit_id(db).for_sale.value, 100000 );

            BOOST_CHECK( mpa_id(db).dynamic_data(db).accumulated_collateral_fees == 217 ); // 17 + 14 + 186

            BOOST_CHECK_EQUAL( get_balance( seller_id, mpa_id ), 20020 ); // 200000 - 79980 - 100000
            BOOST_CHECK_EQUAL( get_balance( seller_id, asset_id_type() ), 1586 );
            BOOST_CHECK_EQUAL( get_balance( seller2_id, mpa_id ), 100000 ); // 200000 - 100000
            BOOST_CHECK_EQUAL( get_balance( seller2_id, asset_id_type() ), 1797 );
         }
         else if( 5 == i )
         {
            // force settlement order created
            BOOST_REQUIRE( op_result.new_objects.valid() && 1U == op_result.new_objects->size() );
            settle_id = *op_result.new_objects->begin();

            // settlement fund pays = 1983
            // seller2 pays = 100000
            // settlement fund = 0
            // settlement debt = 0
            // seller2 would receive = round_up(100000 * 179655 / 10000000 ) = 1797 (<1983, so ok)
            // collateral fee = 1983 - 1797 = 186
            BOOST_REQUIRE( op_result.paid.valid() && 1U == op_result.paid->size() );
            BOOST_CHECK( *op_result.paid->begin() == asset( 100000, mpa_id ) );
            BOOST_REQUIRE( op_result.received.valid() && 1U == op_result.received->size() );
            BOOST_CHECK( *op_result.received->begin() == asset( 1797 ) );
            BOOST_REQUIRE( op_result.fees.valid() && 2U == op_result.fees->size() );
            BOOST_CHECK( *op_result.fees->begin() == asset( 186 ) );

            BOOST_CHECK( !mpa_id(db).bitasset_data(db).is_individually_settled_to_fund() );
            BOOST_CHECK_EQUAL( mpa_id(db).bitasset_data(db).individual_settlement_fund.value, 0 );
            BOOST_CHECK_EQUAL( mpa_id(db).bitasset_data(db).individual_settlement_debt.value, 0 );

            // settle order is matched with call3
            // settle order is smaller
            BOOST_CHECK( !db.find(settle_id) );
            // settle order gets round_down((110000-100000) * (1450/100000) * (1239/1000)) = 179
            // settle order pays round_up(179 * (100000/1450) * (1000/1239)) = 9964
            // call3 gets 9964
            // call3 pays round_down(9964 * (1450/100000) * (1250/1000)) = 180, margin call fee = 1
            // call3 is now (100000-9964):(2200-180) = 90036:2020
            BOOST_CHECK_EQUAL( call2_id(db).debt.value, 20020 );
            BOOST_CHECK_EQUAL( call2_id(db).collateral.value, 500 );
            // 20020 / 500 = 40.04
            BOOST_CHECK_EQUAL( call3_id(db).debt.value, 90036 );
            BOOST_CHECK_EQUAL( call3_id(db).collateral.value, 2020 );
            // 90036 / 2020 = 44.572277228

            BOOST_REQUIRE( db.find(limit_id) );
            BOOST_CHECK_EQUAL( limit_id(db).for_sale.value, 100000 );

            BOOST_CHECK( mpa_id(db).dynamic_data(db).accumulated_collateral_fees == 218 ); // 17 + 14 + 186 + 1

            BOOST_CHECK_EQUAL( get_balance( seller_id, mpa_id ), 20020 ); // 200000 - 79980 - 100000
            BOOST_CHECK_EQUAL( get_balance( seller_id, asset_id_type() ), 1586 );
            BOOST_CHECK_EQUAL( get_balance( seller2_id, mpa_id ), 90036 ); // 200000 - 100000 - 9964
            BOOST_CHECK_EQUAL( get_balance( seller2_id, asset_id_type() ), 1976 ); // 1797 + 179

            BOOST_CHECK_EQUAL( get_balance( borrower_id, asset_id_type() ), init_amount - 2000 );
            BOOST_CHECK_EQUAL( get_balance( borrower2_id, asset_id_type() ), init_amount - 2100 );
            BOOST_CHECK_EQUAL( get_balance( borrower3_id, asset_id_type() ), init_amount - 2200 );
            BOOST_CHECK_EQUAL( get_balance( borrower4_id, asset_id_type() ), init_amount - 2500 );
            BOOST_CHECK_EQUAL( get_balance( borrower5_id, asset_id_type() ), init_amount - 3136 );
         }

      };

      check_result();

      BOOST_TEST_MESSAGE( "Generate a block" );
      generate_block();

      check_result();

      // reset
      db.pop_block();

   } // for i

} FC_LOG_AND_RETHROW() }

/// Tests individual settlement to order : settles when price drops, and the settled-debt order is matched as maker
/// * Before hf core-2591, the settled-debt order is filled at its own price (collateral amount / debt amount)
/// * After hf core-2591, the settled-debt order is filled at margin call order price (MCOP)
BOOST_AUTO_TEST_CASE( individual_settlement_to_order_and_matching_as_maker_test )
{ try {

   // Advance to core-2467 hard fork
   auto mi = db.get_global_properties().parameters.maintenance_interval;
   generate_blocks(HARDFORK_CORE_2467_TIME - mi);
   generate_blocks(db.get_dynamic_global_properties().next_maintenance_time);
   set_expiration( db, trx );

   // multiple passes,
   // i == 0 : before hf core-2591
   // i == 1 : after hf core-2591
   for( int i = 0; i < 2; ++i )
   {
      idump( (i) );

      if( 1 == i )
      {
         // Advance to core-2591 hard fork
         generate_blocks(HARDFORK_CORE_2591_TIME);
         generate_block();
      }

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
      asset_id_type mpa_id = mpa.get_id();

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
      BOOST_CHECK( !mpa.bitasset_data(db).is_globally_settled() );
      BOOST_CHECK( !mpa.bitasset_data(db).is_individually_settled_to_fund() );
      BOOST_CHECK( !db.find_settled_debt_order(mpa_id) );

      // borrowers borrow some
      // undercollateralization price = 100000:2000 * 1250:1000 = 100000:1600
      const call_order_object* call_ptr = borrow( borrower, asset(100000, mpa_id), asset(2000) );
      BOOST_REQUIRE( call_ptr );
      call_order_id_type call_id = call_ptr->get_id();

      // undercollateralization price = 100000:2100 * 1250:1000 = 100000:1680
      const call_order_object* call2_ptr = borrow( borrower2, asset(100000, mpa_id), asset(2100) );
      BOOST_REQUIRE( call2_ptr );
      call_order_id_type call2_id = call2_ptr->get_id();

      // undercollateralization price = 100000:2200 * 1250:1000 = 100000:1760
      const call_order_object* call3_ptr = borrow( borrower3, asset(100000, mpa_id), asset(2200) );
      BOOST_REQUIRE( call3_ptr );
      call_order_id_type call3_id = call3_ptr->get_id();

      // undercollateralization price = 100000:2500 * 1250:1000 = 100000:2000
      const call_order_object* call4_ptr = borrow( borrower4, asset(100000, mpa_id), asset(2500) );
      BOOST_REQUIRE( call4_ptr );
      call_order_id_type call4_id = call4_ptr->get_id();

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
      // call match price = 100000:1650 * 1000:1239 = 100000:2044.35 = 48.915303153

      // check
      BOOST_CHECK( mpa.bitasset_data(db).median_feed.settlement_price == f.settlement_price );
      BOOST_CHECK( mpa.bitasset_data(db).current_feed.settlement_price == f.settlement_price );
      BOOST_CHECK( !mpa.bitasset_data(db).is_globally_settled() );
      BOOST_CHECK( !mpa.bitasset_data(db).is_individually_settled_to_fund() );
      const limit_order_object* settled_debt = db.find_settled_debt_order(mpa_id);
      BOOST_REQUIRE( settled_debt );

      // call: margin call fee deducted = round_down(2000*11/1250) = 17,
      // fund receives 2000 - 17 = 1983
      BOOST_CHECK( settled_debt->is_settled_debt );
      BOOST_CHECK_EQUAL( settled_debt->for_sale.value, 1983 );
      BOOST_CHECK_EQUAL( settled_debt->amount_to_receive().amount.value, 100000 );
      BOOST_CHECK_EQUAL( mpa_id(db).bitasset_data(db).individual_settlement_debt.value, 100000 );
      BOOST_CHECK_EQUAL( mpa_id(db).bitasset_data(db).individual_settlement_fund.value, 1983 );
      BOOST_CHECK( settled_debt->sell_price == asset(1983)/asset(100000,mpa_id) );
      // order match price = 100000 / 1983 = 50.428643469

      BOOST_CHECK( !db.find( call_id ) );
      BOOST_CHECK_EQUAL( call2_id(db).debt.value, 100000 );
      BOOST_CHECK_EQUAL( call2_id(db).collateral.value, 2100 );
      BOOST_CHECK_EQUAL( call3_id(db).debt.value, 100000 );
      BOOST_CHECK_EQUAL( call3_id(db).collateral.value, 2200 );
      BOOST_CHECK_EQUAL( call4_id(db).debt.value, 100000 );
      BOOST_CHECK_EQUAL( call4_id(db).collateral.value, 2500 );

      BOOST_CHECK_EQUAL( mpa_id(db).dynamic_data(db).accumulated_collateral_fees.value, 17 );

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

      // no change to the settled-debt order
      settled_debt = db.find_settled_debt_order(mpa_id);
      BOOST_REQUIRE( settled_debt );
      BOOST_CHECK( settled_debt->is_settled_debt );
      BOOST_CHECK_EQUAL( settled_debt->for_sale.value, 1983 );
      BOOST_CHECK_EQUAL( settled_debt->amount_to_receive().amount.value, 100000 );
      BOOST_CHECK_EQUAL( mpa_id(db).bitasset_data(db).individual_settlement_debt.value, 100000 );
      BOOST_CHECK_EQUAL( mpa_id(db).bitasset_data(db).individual_settlement_fund.value, 1983 );
      BOOST_CHECK( settled_debt->sell_price == asset(1983)/asset(100000,mpa_id) );

      BOOST_CHECK_EQUAL( get_balance( seller_id, mpa_id ), 390021 ); // 400000 - 9979
      BOOST_CHECK_EQUAL( get_balance( seller_id, asset_id_type() ), 204 );

      BOOST_CHECK_EQUAL( mpa_id(db).dynamic_data(db).accumulated_collateral_fees.value, 18 ); // 17 + 1

      // publish a new feed so that 2 other debt positions are undercollateralized
      f.settlement_price = price( asset(100000,mpa_id), asset(1800) );
      publish_feed( mpa_id, feeder_id, f, feed_icr );
      // call pays price = 100000:1800 * 1000:1250 = 100000:2250 = 44.444444444
      // call match price = 100000:1800 * 1000:1239 = 100000:2230.2 = 44.83902789

      BOOST_CHECK( mpa.bitasset_data(db).median_feed.settlement_price == f.settlement_price );
      BOOST_CHECK( mpa.bitasset_data(db).current_feed.settlement_price == f.settlement_price );
      BOOST_CHECK( !mpa.bitasset_data(db).is_globally_settled() );
      BOOST_CHECK( !mpa.bitasset_data(db).is_individually_settled_to_fund() );

      BOOST_CHECK( !db.find( call_id ) );
      BOOST_CHECK( !db.find( call2_id ) );
      BOOST_CHECK( !db.find( call3_id ) );
      BOOST_CHECK_EQUAL( call4_id(db).debt.value, 100000 );
      BOOST_CHECK_EQUAL( call4_id(db).collateral.value, 2500 );

      // call2: margin call fee deducted = round_down(1895*11/1250) = 16,
      // fund receives 1895 - 16 = 1879
      // call3: margin call fee deducted = round_down(2200*11/1250) = 19,
      // fund receives 2200 - 19 = 2181
      settled_debt = db.find_settled_debt_order(mpa_id);
      BOOST_REQUIRE( settled_debt );
      BOOST_CHECK( settled_debt->is_settled_debt );
      BOOST_CHECK_EQUAL( settled_debt->for_sale.value, 6043 ); // 1983 + 1879 + 2181
      BOOST_CHECK_EQUAL( settled_debt->amount_to_receive().amount.value, 290021 ); // 100000 + 90021 + 100000
      BOOST_CHECK_EQUAL( mpa_id(db).bitasset_data(db).individual_settlement_debt.value, 290021 );
      BOOST_CHECK_EQUAL( mpa_id(db).bitasset_data(db).individual_settlement_fund.value, 6043 );
      BOOST_CHECK( settled_debt->sell_price == asset(6043)/asset(290021,mpa_id) );
      // order match price = 290021 / 6043 = 47.992884329

      BOOST_CHECK_EQUAL( mpa_id(db).dynamic_data(db).accumulated_collateral_fees.value, 53 ); // 17 + 1 + 16 + 19

      // borrower buys at higher price
      const limit_order_object* buy_high = create_sell_order( borrower, asset(10), asset(100,mpa_id) );
      BOOST_CHECK( buy_high );
      limit_order_id_type buy_high_id = buy_high->get_id();

      // seller sells some, this will match buy_high,
      // and when it matches call4, it will be cancelled since it is too small
      limit_ptr = create_sell_order( seller, asset(120,mpa_id), asset(1) );
      // the limit order is filled
      BOOST_CHECK( !limit_ptr );
      // buy_high is filled
      BOOST_CHECK( !db.find( buy_high_id ) );

      BOOST_CHECK_EQUAL( call4_id(db).debt.value, 100000 );
      BOOST_CHECK_EQUAL( call4_id(db).collateral.value, 2500 );

      // no change to the settled-debt order
      settled_debt = db.find_settled_debt_order(mpa_id);
      BOOST_REQUIRE( settled_debt );
      BOOST_CHECK( settled_debt->is_settled_debt );
      BOOST_CHECK_EQUAL( settled_debt->for_sale.value, 6043 );
      BOOST_CHECK_EQUAL( settled_debt->amount_to_receive().amount.value, 290021 );
      BOOST_CHECK_EQUAL( mpa_id(db).bitasset_data(db).individual_settlement_debt.value, 290021 );
      BOOST_CHECK_EQUAL( mpa_id(db).bitasset_data(db).individual_settlement_fund.value, 6043 );
      BOOST_CHECK( settled_debt->sell_price == asset(6043)/asset(290021,mpa_id) );

      BOOST_CHECK_EQUAL( get_balance( seller_id, mpa_id ), 389921 ); // 400000 - 9979 - 100
      BOOST_CHECK_EQUAL( get_balance( seller_id, asset_id_type() ), 214 ); // 204 + 10

      // publish a new feed so that
      // * before hf core-2591, the settled debt order is in the front of the order book
      // * after hf core-2591, the settled debt order is updated to be behind the margin call orders
      f.settlement_price = price( asset(100000,mpa_id), asset(1600) );
      publish_feed( mpa_id, feeder_id, f, feed_icr );
      // call pays price = 100000:1600 * 1000:1250 = 100000:2000 = 50
      // call match price = 100000:1600 * 1000:1239 = 100000:1982.4 = 50.443906376

      if( 0 == i )
      {
         // no change to the settled-debt order
         settled_debt = db.find_settled_debt_order(mpa_id);
         BOOST_REQUIRE( settled_debt );
         BOOST_CHECK( settled_debt->is_settled_debt );
         BOOST_CHECK_EQUAL( settled_debt->for_sale.value, 6043 );
         BOOST_CHECK_EQUAL( settled_debt->amount_to_receive().amount.value, 290021 );
         BOOST_CHECK_EQUAL( mpa_id(db).bitasset_data(db).individual_settlement_debt.value, 290021 );
         BOOST_CHECK_EQUAL( mpa_id(db).bitasset_data(db).individual_settlement_fund.value, 6043 );
         BOOST_CHECK( settled_debt->sell_price == asset(6043)/asset(290021,mpa_id) );
      }
      else if( 1 == i )
      {
         // the settled-debt order is updated
         settled_debt = db.find_settled_debt_order(mpa_id);
         BOOST_REQUIRE( settled_debt );
         BOOST_CHECK( settled_debt->is_settled_debt );
         BOOST_CHECK_EQUAL( settled_debt->for_sale.value, 5750 ); // round_up(290021 * 19824 / 1000000)
         BOOST_CHECK_EQUAL( settled_debt->amount_to_receive().amount.value, 290052 ); //round_down(5750*1000000/19824)
         BOOST_CHECK_EQUAL( mpa_id(db).bitasset_data(db).individual_settlement_debt.value, 290021 );
         BOOST_CHECK_EQUAL( mpa_id(db).bitasset_data(db).individual_settlement_fund.value, 6043 );
         BOOST_CHECK( settled_debt->sell_price == asset(19824)/asset(1000000,mpa_id) );
      }

      // borrower buys at higher price
      buy_high = create_sell_order( borrower, asset(10), asset(100,mpa_id) );
      BOOST_CHECK( buy_high );
      buy_high_id = buy_high->get_id();

      // seller sells some, this will match buy_high, then
      // * before hf core-2591, when it matches the settled debt, it will be cancelled since it is too small
      // * after hf core-2591, when it matches a call order, it will be cancelled since it is too small
      limit_ptr = create_sell_order( seller, asset(120,mpa_id), asset(1) );
      // the limit order is filled
      BOOST_CHECK( !limit_ptr );
      // buy_high is filled
      BOOST_CHECK( !db.find( buy_high_id ) );

      if( 0 == i )
      {
         // no change to the settled-debt order
         settled_debt = db.find_settled_debt_order(mpa_id);
         BOOST_REQUIRE( settled_debt );
         BOOST_CHECK( settled_debt->is_settled_debt );
         BOOST_CHECK_EQUAL( settled_debt->for_sale.value, 6043 );
         BOOST_CHECK_EQUAL( settled_debt->amount_to_receive().amount.value, 290021 );
         BOOST_CHECK_EQUAL( mpa_id(db).bitasset_data(db).individual_settlement_debt.value, 290021 );
         BOOST_CHECK_EQUAL( mpa_id(db).bitasset_data(db).individual_settlement_fund.value, 6043 );
         BOOST_CHECK( settled_debt->sell_price == asset(6043)/asset(290021,mpa_id) );
      }
      else if( 1 == i )
      {
         // no change to the settled-debt order
         settled_debt = db.find_settled_debt_order(mpa_id);
         BOOST_REQUIRE( settled_debt );
         BOOST_CHECK( settled_debt->is_settled_debt );
         BOOST_CHECK_EQUAL( settled_debt->for_sale.value, 5750 );
         BOOST_CHECK_EQUAL( settled_debt->amount_to_receive().amount.value, 290052 );
         BOOST_CHECK_EQUAL( mpa_id(db).bitasset_data(db).individual_settlement_debt.value, 290021 );
         BOOST_CHECK_EQUAL( mpa_id(db).bitasset_data(db).individual_settlement_fund.value, 6043 );
         BOOST_CHECK( settled_debt->sell_price == asset(19824)/asset(1000000,mpa_id) );
      }

      BOOST_CHECK_EQUAL( get_balance( seller_id, mpa_id ), 389821 ); // 400000 - 9979 - 100 - 100
      BOOST_CHECK_EQUAL( get_balance( seller_id, asset_id_type() ), 224 ); // 204 + 10 + 10

      BOOST_CHECK_EQUAL( mpa_id(db).dynamic_data(db).accumulated_collateral_fees.value, 53 ); // 17 + 1 + 16 + 19

      // seller sells some
      limit_ptr = create_sell_order( seller, asset(10000,mpa_id), asset(100) );
      // the limit order is filled
      BOOST_CHECK( !limit_ptr );

      if( 0 == i )
      {
         // the settled debt is partially filled
         // limit order receives = round_down(10000*6043/290021) = 208
         // settled debt receives = round_up(208*290021/6043) = 9983

         BOOST_CHECK( !db.find( call_id ) );
         BOOST_CHECK( !db.find( call2_id ) );
         BOOST_CHECK( !db.find( call3_id ) );
         BOOST_CHECK_EQUAL( call4_id(db).debt.value, 100000 );
         BOOST_CHECK_EQUAL( call4_id(db).collateral.value, 2500 );

         settled_debt = db.find_settled_debt_order(mpa_id);
         BOOST_REQUIRE( settled_debt );
         BOOST_CHECK( settled_debt->is_settled_debt );
         BOOST_CHECK_EQUAL( settled_debt->for_sale.value, 5835 ); // 6043 - 208
         BOOST_CHECK_EQUAL( settled_debt->amount_to_receive().amount.value, 280038 ); // 290021 - 9983
         BOOST_CHECK_EQUAL( mpa_id(db).bitasset_data(db).individual_settlement_debt.value, 280038 );
         BOOST_CHECK_EQUAL( mpa_id(db).bitasset_data(db).individual_settlement_fund.value, 5835 );
         BOOST_CHECK( settled_debt->sell_price == asset(5835)/asset(280038,mpa_id) );

         BOOST_CHECK_EQUAL( get_balance( seller_id, mpa_id ), 379838 ); // 400000 - 9979 - 100 - 100 - 9983
         BOOST_CHECK_EQUAL( get_balance( seller_id, asset_id_type() ), 432 ); // 204 + 10 + 10 + 208

         BOOST_CHECK_EQUAL( mpa_id(db).dynamic_data(db).accumulated_collateral_fees.value, 53 ); // no change
      }
      else if( 1 == i )
      {
         // call4 is partially filled
         // limit order gets round_down(10000*(1600/100000)*(1239/1000)) = 198
         // limit order pays round_up(198*(100000/1600)*(1000/1239)) = 9988
         // call4 gets 9988
         // call4 pays round_down(9988*(1600/100000)*(1250/1000)) = 199, margin call fee = 1

         BOOST_CHECK( !db.find( call_id ) );
         BOOST_CHECK( !db.find( call2_id ) );
         BOOST_CHECK( !db.find( call3_id ) );
         BOOST_CHECK_EQUAL( call4_id(db).debt.value, 90012 ); // 100000 - 9988
         BOOST_CHECK_EQUAL( call4_id(db).collateral.value, 2301 ); // 2500 - 199

         // no change to the settled-debt order
         settled_debt = db.find_settled_debt_order(mpa_id);
         BOOST_REQUIRE( settled_debt );
         BOOST_CHECK( settled_debt->is_settled_debt );
         BOOST_CHECK_EQUAL( settled_debt->for_sale.value, 5750 );
         BOOST_CHECK_EQUAL( settled_debt->amount_to_receive().amount.value, 290052 );
         BOOST_CHECK_EQUAL( mpa_id(db).bitasset_data(db).individual_settlement_debt.value, 290021 );
         BOOST_CHECK_EQUAL( mpa_id(db).bitasset_data(db).individual_settlement_fund.value, 6043 );
         BOOST_CHECK( settled_debt->sell_price == asset(19824)/asset(1000000,mpa_id) );

         BOOST_CHECK_EQUAL( get_balance( seller_id, mpa_id ), 379833 ); // 400000 - 9979 - 100 - 100 - 9988
         BOOST_CHECK_EQUAL( get_balance( seller_id, asset_id_type() ), 422 ); // 204 + 10 + 10 + 198

         BOOST_CHECK_EQUAL( mpa_id(db).dynamic_data(db).accumulated_collateral_fees.value, 54 ); // 53 + 1
      }

      // seller sells some
      limit_ptr = create_sell_order( seller, asset(300000,mpa_id), asset(3000) );
      // the limit order is filled
      BOOST_CHECK( !limit_ptr );

      auto check_result = [&]
      {
         BOOST_CHECK( mpa_id(db).bitasset_data(db).median_feed.settlement_price == f.settlement_price );
         BOOST_CHECK( mpa_id(db).bitasset_data(db).current_feed.settlement_price == f.settlement_price );
         BOOST_CHECK( !mpa_id(db).bitasset_data(db).is_globally_settled() );
         BOOST_CHECK( !mpa_id(db).bitasset_data(db).is_individually_settled_to_fund() );

         if( 0 == i )
         {
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

            BOOST_CHECK_EQUAL( mpa_id(db).dynamic_data(db).accumulated_collateral_fees.value, 56 ); // 53 + 3
         }
         else if( 1 == i )
         {
            // call4 is fully filled
            BOOST_CHECK( !db.find( call_id ) );
            BOOST_CHECK( !db.find( call2_id ) );
            BOOST_CHECK( !db.find( call3_id ) );
            BOOST_CHECK( !db.find( call4_id ) );
            // call4 gets 90012
            // limit order gets round_up(90012*(1600/100000)*(1239/1000)) = 1785
            // call4 pays round_up(90012*(1600/100000)*(1250/1000)) = 1801, margin call fee = 1801 - 1785 = 16

            // limit order reminder = 300000 - 90012 = 209988
            // the settled debt is partially filled
            // limit order receives = round_down(209988*19824/1000000) = 4162
            // settled debt receives = round_up(4162*1000000/19824) = 209948
            // settled debt pays = round_down(209948*6043/290021) = 4374, collateral fee = 4374 - 4162 = 212

            settled_debt = db.find_settled_debt_order(mpa_id);
            BOOST_REQUIRE( settled_debt );
            BOOST_CHECK( settled_debt->is_settled_debt );
            BOOST_CHECK_EQUAL( settled_debt->for_sale.value, 1588 ); // round_up( 80073 * 19824 / 1000000 )
            BOOST_CHECK_EQUAL( settled_debt->amount_to_receive().amount.value, 80104 ); //rnd_down(1588*1000000/19824)
            BOOST_CHECK_EQUAL( mpa_id(db).bitasset_data(db).individual_settlement_debt.value, 80073 ); // 290021
                                                                                                       // - 209948
            BOOST_CHECK_EQUAL( mpa_id(db).bitasset_data(db).individual_settlement_fund.value, 1669 ); // 6043 - 4374
            BOOST_CHECK( settled_debt->sell_price == asset(19824)/asset(1000000,mpa_id) );

            BOOST_CHECK_EQUAL( get_balance( seller_id, mpa_id ), 79873 ); // 400000 - 9979 - 100 - 100 - 9988
                                                                          // - 90012 - 209948
            BOOST_CHECK_EQUAL( get_balance( seller_id, asset_id_type() ), 6369 ); // 204 + 10 + 10 + 198 + 1785 + 4162

            BOOST_CHECK_EQUAL( mpa_id(db).dynamic_data(db).accumulated_collateral_fees.value, 282 ); // 54 + 16 + 212
         }

      };

      check_result();

      BOOST_TEST_MESSAGE( "Generate a block" );
      generate_block();

      check_result();

      if( 1 == i )
      {

         // undercollateralization price = 100000:5000 * 1250:1000 = 100000:4000
         const call_order_object* call5_ptr = borrow( borrower4_id(db), asset(100000, mpa_id), asset(5000) );
         BOOST_REQUIRE( call5_ptr );
         call_order_id_type call5_id = call5_ptr->get_id();

         BOOST_CHECK_EQUAL( call5_id(db).debt.value, 100000 );
         BOOST_CHECK_EQUAL( call5_id(db).collateral.value, 5000 );

         transfer( borrower4_id(db), seller_id(db), asset(100000,mpa_id) );

         BOOST_CHECK_EQUAL( get_balance( seller_id, mpa_id ), 179873 ); // 79873 + 100000
         BOOST_CHECK_EQUAL( get_balance( seller_id, asset_id_type() ), 6369 ); // no change

         // seller sells some
         limit_ptr = create_sell_order( seller_id(db), asset(100000,mpa_id), asset(1000) );
         // the limit order is partially filled
         BOOST_REQUIRE( limit_ptr );
         limit_order_id_type limit_id = limit_ptr->get_id();

         auto check_result_1 = [&]
         {
            // the settled-debt order is fully filled
            BOOST_CHECK( !db.find_settled_debt_order(mpa_id) );

            // settled debt receives = 80073
            // limit order receives = round_up(80073*19824/1000000) = 1588
            // settled debt pays = 1669, collateral fee = 1669 - 1588 = 81

            BOOST_CHECK_EQUAL( limit_id(db).for_sale.value, 19927 ); // 100000 - 80073

            BOOST_CHECK_EQUAL( get_balance( seller_id, mpa_id ), 79873 ); // 179873 - 100000
            BOOST_CHECK_EQUAL( get_balance( seller_id, asset_id_type() ), 7957 ); // 6369 + 1588

            BOOST_CHECK_EQUAL( mpa_id(db).dynamic_data(db).accumulated_collateral_fees.value, 363 ); // 282 + 81
         };

         check_result_1();

         BOOST_TEST_MESSAGE( "Generate a new block" );
         generate_block();

         check_result_1();

         // reset
         db.pop_block();
      }

      // reset
      db.pop_block();

   } // for i

} FC_LOG_AND_RETHROW() }

/// Tests individual settlement to order :
///   after hf core-2591, the settled-debt order is matched as taker when price feed is updated
BOOST_AUTO_TEST_CASE( individual_settlement_to_order_and_matching_as_taker_test )
{ try {

   // Advance to core-2467 hard fork
   auto mi = db.get_global_properties().parameters.maintenance_interval;
   generate_blocks(HARDFORK_CORE_2467_TIME - mi);
   generate_blocks(db.get_dynamic_global_properties().next_maintenance_time);
   set_expiration( db, trx );

   // multiple passes,
   // i == 0 : before hf core-2591
   // i >= 1 : after hf core-2591
   for( int i = 0; i < 6; ++i )
   {
      idump( (i) );

      if( 1 == i )
      {
         // Advance to core-2591 hard fork
         generate_blocks(HARDFORK_CORE_2591_TIME);
         generate_block();
      }

      set_expiration( db, trx );

      ACTORS((sam)(feeder)(borrower)(borrower2)(seller)(seller2));

      auto init_amount = 10000000 * GRAPHENE_BLOCKCHAIN_PRECISION;
      fund( sam, asset(init_amount) );
      fund( feeder, asset(init_amount) );
      fund( borrower, asset(init_amount) );
      fund( borrower2, asset(init_amount) );

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
      asset_id_type mpa_id = mpa.get_id();

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
      BOOST_CHECK( !mpa.bitasset_data(db).is_globally_settled() );
      BOOST_CHECK( !mpa.bitasset_data(db).is_individually_settled_to_fund() );
      BOOST_CHECK( !db.find_settled_debt_order(mpa_id) );

      // borrowers borrow some
      // undercollateralization price = 100000:2000 * 1250:1000 = 100000:1600
      const call_order_object* call_ptr = borrow( borrower, asset(100000, mpa_id), asset(2000) );
      BOOST_REQUIRE( call_ptr );
      call_order_id_type call_id = call_ptr->get_id();

      // Transfer funds to sellers
      transfer( borrower, seller, asset(100000,mpa_id) );

      BOOST_CHECK_EQUAL( call_id(db).debt.value, 100000 );
      BOOST_CHECK_EQUAL( call_id(db).collateral.value, 2000 );

      BOOST_CHECK_EQUAL( get_balance( seller_id, mpa_id ), 100000 );
      BOOST_CHECK_EQUAL( get_balance( seller_id, asset_id_type() ), 0 );

      // publish a new feed so that borrower's debt position is undercollateralized
      f.settlement_price = price( asset(100000,mpa_id), asset(1650) );
      publish_feed( mpa_id, feeder_id, f, feed_icr );
      // call pays price = 100000:1650 * 1000:1250 = 100000:2062.5 = 48.484848485
      // call match price = 100000:1650 * 1000:1239 = 100000:2044.35 = 48.915303153

      // check
      BOOST_CHECK( mpa.bitasset_data(db).median_feed.settlement_price == f.settlement_price );
      BOOST_CHECK( mpa.bitasset_data(db).current_feed.settlement_price == f.settlement_price );
      BOOST_CHECK( !mpa.bitasset_data(db).is_globally_settled() );
      BOOST_CHECK( !mpa.bitasset_data(db).is_individually_settled_to_fund() );
      const limit_order_object* settled_debt = db.find_settled_debt_order(mpa_id);
      BOOST_REQUIRE( settled_debt );

      // call: margin call fee deducted = round_down(2000*11/1250) = 17,
      // fund receives 2000 - 17 = 1983
      BOOST_CHECK( settled_debt->is_settled_debt );
      BOOST_CHECK_EQUAL( settled_debt->for_sale.value, 1983 );
      BOOST_CHECK_EQUAL( mpa_id(db).bitasset_data(db).individual_settlement_debt.value, 100000 );
      BOOST_CHECK_EQUAL( mpa_id(db).bitasset_data(db).individual_settlement_fund.value, 1983 );
      BOOST_CHECK( settled_debt->sell_price == asset(1983)/asset(100000,mpa_id) );
      // order match price = 100000 / 1983 = 50.428643469

      BOOST_CHECK( !db.find( call_id ) );

      BOOST_CHECK_EQUAL( mpa_id(db).dynamic_data(db).accumulated_collateral_fees.value, 17 );

      // seller sells some
      const limit_order_object* limit_ptr = create_sell_order( seller, asset(10000,mpa_id), asset(100) );
      // the limit order is filled
      BOOST_CHECK( !limit_ptr );

      // the settled debt is partially filled
      // limit order receives = round_down(10000*1983/100000) = 198
      // settled debt receives = round_up(198*100000/1983) = 9985
      // settled debt pays = 198, collateral fee = 0

      settled_debt = db.find_settled_debt_order(mpa_id);
      BOOST_REQUIRE( settled_debt );
      BOOST_CHECK( settled_debt->is_settled_debt );
      BOOST_CHECK_EQUAL( settled_debt->for_sale.value, 1785 ); // 1983 - 198
      BOOST_CHECK_EQUAL( mpa_id(db).bitasset_data(db).individual_settlement_debt.value, 90015 ); // 100000 - 9985
      BOOST_CHECK_EQUAL( mpa_id(db).bitasset_data(db).individual_settlement_fund.value, 1785 );
      if( 0 == i )
         BOOST_CHECK( settled_debt->sell_price == asset(1785)/asset(90015,mpa_id) );
      else
         BOOST_CHECK( settled_debt->sell_price == asset(1983)/asset(100000,mpa_id) );

      BOOST_CHECK_EQUAL( get_balance( seller_id, mpa_id ), 90015 ); // 100000 - 9985
      BOOST_CHECK_EQUAL( get_balance( seller_id, asset_id_type() ), 198 );

      BOOST_CHECK_EQUAL( mpa_id(db).dynamic_data(db).accumulated_collateral_fees.value, 17 );

      // publish a new feed (collateral price rises)
      f.settlement_price = price( asset(200,mpa_id), asset(1) );
      publish_feed( mpa_id, feeder_id, f, feed_icr );
      // call pays price = 200:1 * 1000:1250 = 200000:1250 = 160
      // call match price = 200:1 * 1000:1239 = 200000:1239 = 161.420500404

      settled_debt = db.find_settled_debt_order(mpa_id);
      BOOST_REQUIRE( settled_debt );
      BOOST_CHECK( settled_debt->is_settled_debt );
      if( 0 == i )
         BOOST_CHECK_EQUAL( settled_debt->for_sale.value, 1785 );
      else
         BOOST_CHECK_EQUAL( settled_debt->for_sale.value, 558 ); // round_up( 90015 * 1239 / 200000 )
      BOOST_CHECK_EQUAL( mpa_id(db).bitasset_data(db).individual_settlement_debt.value, 90015 );
      BOOST_CHECK_EQUAL( mpa_id(db).bitasset_data(db).individual_settlement_fund.value, 1785 );
      if( 0 == i )
         BOOST_CHECK( settled_debt->sell_price == asset(1785)/asset(90015,mpa_id) );
      else
         BOOST_CHECK( settled_debt->sell_price == asset(1239)/asset(200000,mpa_id) );

      // seller sells some
      limit_ptr = create_sell_order( seller, asset(10000,mpa_id), asset(150) );
      if( 0 == i )
      {
         // the limit order is filled
         BOOST_CHECK( !limit_ptr );

         // the settled debt is partially filled
         // limit order receives = round_down(10000*1785/90015) = 198
         // settled debt receives = round_up(198*90015/1785) = 9985
         // settled debt pays = 198, collateral fee = 0
         settled_debt = db.find_settled_debt_order(mpa_id);
         BOOST_REQUIRE( settled_debt );
         BOOST_CHECK( settled_debt->is_settled_debt );
         BOOST_CHECK_EQUAL( settled_debt->for_sale.value, 1587 ); // 1983 - 198 - 198
         BOOST_CHECK_EQUAL( mpa_id(db).bitasset_data(db).individual_settlement_debt.value, 80030 ); // 100000 - 9985
                                                                                                    // - 9985
         BOOST_CHECK_EQUAL( mpa_id(db).bitasset_data(db).individual_settlement_fund.value, 1587 );
         BOOST_CHECK( settled_debt->sell_price == asset(1587)/asset(80030,mpa_id) );

         BOOST_CHECK_EQUAL( mpa_id(db).dynamic_data(db).accumulated_collateral_fees.value, 17 );

         BOOST_TEST_MESSAGE( "Generate a block" );
         generate_block();

         // reset
         db.pop_block();
         // this branch ends here
         continue;
      }

      // the limit order is not filled
      BOOST_REQUIRE( limit_ptr );
      limit_order_id_type limit_id = limit_ptr->get_id();

      BOOST_CHECK_EQUAL( limit_id(db).for_sale.value, 10000 );

      // the settled-debt order is unchanged
      settled_debt = db.find_settled_debt_order(mpa_id);
      BOOST_REQUIRE( settled_debt );
      BOOST_CHECK( settled_debt->is_settled_debt );
      BOOST_CHECK_EQUAL( settled_debt->for_sale.value, 558 );
      BOOST_CHECK_EQUAL( mpa_id(db).bitasset_data(db).individual_settlement_debt.value, 90015 );
      BOOST_CHECK_EQUAL( mpa_id(db).bitasset_data(db).individual_settlement_fund.value, 1785 );
      BOOST_CHECK( settled_debt->sell_price == asset(1239)/asset(200000,mpa_id) );

      BOOST_CHECK_EQUAL( get_balance( seller_id, mpa_id ), 80015 ); // 100000 - 9985 - 10000
      BOOST_CHECK_EQUAL( get_balance( seller_id, asset_id_type() ), 198 );

      BOOST_CHECK_EQUAL( mpa_id(db).dynamic_data(db).accumulated_collateral_fees.value, 17 );

      call_order_id_type call2_id;
      limit_order_id_type limit2_id;
      if( 1 == i )
      {
         // do nothing here so that there is no call order exists
         // so the settled-debt order will match the limit order on the next price feed update
      }
      if( 2 == i )
      {
         // create a small call order that will go undercollateralized on the next price feed update
         // so the settled-debt order after merged the new call order will still be well collateralized
         // and will match the limit order
         // undercollateralization price = 10000:100 * 1250:1000 = 100000:800
         const call_order_object* call2_ptr = borrow( borrower2, asset(10000, mpa_id), asset(100) );
         BOOST_REQUIRE( call2_ptr );
         call2_id = call2_ptr->get_id();

         BOOST_CHECK_EQUAL( call2_id(db).debt.value, 10000 );
         BOOST_CHECK_EQUAL( call2_id(db).collateral.value, 100 );
      }
      else if( 3 == i )
      {
         // create a huge call order that will go undercollateralized on the next price feed update
         // so the settled-debt order after merged the new call order will be undercollateralized too
         // and will not match the limit order
         // undercollateralization price = 1000000:10000 * 1250:1000 = 100000:800
         const call_order_object* call2_ptr = borrow( borrower2, asset(1000000, mpa_id), asset(10000) );
         BOOST_REQUIRE( call2_ptr );
         call2_id = call2_ptr->get_id();

         BOOST_CHECK_EQUAL( call2_id(db).debt.value, 1000000 );
         BOOST_CHECK_EQUAL( call2_id(db).collateral.value, 10000 );
      }
      else if( 4 == i )
      {
         // create a big call order that will be margin called on the next price feed update
         // so the settled-debt order will have no limit order to match with
         // undercollateralization price = 100000:2400 * 1250:1000 = 100000:1920
         const call_order_object* call2_ptr = borrow( borrower2, asset(100000, mpa_id), asset(2400) );
         BOOST_REQUIRE( call2_ptr );
         call2_id = call2_ptr->get_id();

         BOOST_CHECK_EQUAL( call2_id(db).debt.value, 100000 );
         BOOST_CHECK_EQUAL( call2_id(db).collateral.value, 2400 );
      }
      else if( 5 == i )
      {
         // create a big call order that will not be margin called on the next price feed update
         // so the settled-debt order will match the limit order
         // undercollateralization price = 100000:5000 * 1250:1000 = 100000:4000
         const call_order_object* call2_ptr = borrow( borrower2, asset(100000, mpa_id), asset(5000) );
         BOOST_REQUIRE( call2_ptr );
         call2_id = call2_ptr->get_id();

         BOOST_CHECK_EQUAL( call2_id(db).debt.value, 100000 );
         BOOST_CHECK_EQUAL( call2_id(db).collateral.value, 5000 );

         // Transfer funds to sellers
         transfer( borrower2, seller2, asset(100000,mpa_id) );

         BOOST_CHECK_EQUAL( get_balance( seller2_id, mpa_id ), 100000 );
         BOOST_CHECK_EQUAL( get_balance( seller2_id, asset_id_type() ), 0 );

         // seller2 sells some
         const limit_order_object* limit2_ptr = create_sell_order( seller2, asset(100000,mpa_id), asset(1550) );
         BOOST_REQUIRE( limit2_ptr );
         limit2_id = limit2_ptr->get_id();

         BOOST_CHECK_EQUAL( limit2_id(db).for_sale.value, 100000 );

         BOOST_CHECK_EQUAL( get_balance( seller2_id, mpa_id ), 0 ); // 100000 - 100000
         BOOST_CHECK_EQUAL( get_balance( seller2_id, asset_id_type() ), 0 );
      }

      // publish a new feed (collateral price drops)
      f.settlement_price = price( asset(100000,mpa_id), asset(1350) );
      publish_feed( mpa_id, feeder_id, f, feed_icr );
      // call pays price  (MSSP) = 100000:1350 * 1000:1250 = 100000:1687.5 = 59.259259259
      // call match price (MCOP) = 100000:1350 * 1000:1239 = 100000:1672.65 = 59.78537052

      auto check_result = [&]
      {
         BOOST_CHECK( mpa_id(db).bitasset_data(db).median_feed.settlement_price == f.settlement_price );
         BOOST_CHECK( mpa_id(db).bitasset_data(db).current_feed.settlement_price == f.settlement_price );
         BOOST_CHECK( !mpa_id(db).bitasset_data(db).is_globally_settled() );
         BOOST_CHECK( !mpa_id(db).bitasset_data(db).is_individually_settled_to_fund() );

         // the settled-debt order was:
         // settled_debt_amount = 90015
         // settled_collateral_amount = 1785

         // the limit order was selling 10000 MPA for 150 CORE

         if( 1 == i )
         {
            // the settled-debt order is matched with the limit order
            // the limit order is fully filled
            BOOST_CHECK( !db.find( limit_id ) );

            // the settled-debt order is partially filled, match price is 10000:150
            // limit order receives = 150
            // settled debt receives = 10000
            // settled debt pays = round_down(10000*1785/90015) = 198, collateral fee = 198 - 150 = 48

            settled_debt = db.find_settled_debt_order(mpa_id);
            BOOST_REQUIRE( settled_debt );
            BOOST_CHECK( settled_debt->is_settled_debt );
            BOOST_CHECK_EQUAL( settled_debt->for_sale.value, 1339 ); // round_up( 80015 * 167265 / 10000000 )
            BOOST_CHECK_EQUAL( mpa_id(db).bitasset_data(db).individual_settlement_debt.value, 80015 ); //90015 - 10000
            BOOST_CHECK_EQUAL( mpa_id(db).bitasset_data(db).individual_settlement_fund.value, 1587 ); // 1785 - 198
            BOOST_CHECK( settled_debt->sell_price == asset(167265)/asset(10000000,mpa_id) );

            BOOST_CHECK_EQUAL( get_balance( seller_id, mpa_id ), 80015 ); // 100000 - 9985 - 10000
            BOOST_CHECK_EQUAL( get_balance( seller_id, asset_id_type() ), 348 ); // 198 + 150

            BOOST_CHECK_EQUAL( mpa_id(db).dynamic_data(db).accumulated_collateral_fees.value, 65 ); // 17 + 48
         }
         else if( 2 == i )
         {
            // call2 is individually settled
            BOOST_CHECK( !db.find( call2_id ) );

            // margin call fee deducted = round_down(100*11/1250) = 0,
            // fund receives 100, collateral = 1785 + 100 = 1885
            // fund debt = 90015 + 10000 = 100015
            // fund price = 100015 / 2785 = 53.058355438 < MCOP 59.78537052

            // the settled-debt order is matched with the limit order
            // the limit order is fully filled
            BOOST_CHECK( !db.find( limit_id ) );

            // the settled-debt order is partially filled, match price is 10000:150
            // limit order receives = 150
            // settled debt receives = 10000
            // settled debt pays = round_down(10000*1885/100015) = 188, collateral fee = 188 - 150 = 38

            settled_debt = db.find_settled_debt_order(mpa_id);
            BOOST_REQUIRE( settled_debt );
            BOOST_CHECK( settled_debt->is_settled_debt );
            BOOST_CHECK_EQUAL( settled_debt->for_sale.value, 1506 ); // round_up( 90015 * 167265 / 10000000 )
            BOOST_CHECK_EQUAL( mpa_id(db).bitasset_data(db).individual_settlement_debt.value, 90015 ); //90015 - 10000
                                                                                                       // + 10000
            BOOST_CHECK_EQUAL( mpa_id(db).bitasset_data(db).individual_settlement_fund.value, 1697 ); // 1785 + 100
                                                                                                      // - 188
            BOOST_CHECK( settled_debt->sell_price == asset(167265)/asset(10000000,mpa_id) );

            BOOST_CHECK_EQUAL( get_balance( seller_id, mpa_id ), 80015 ); // 100000 - 9985 - 10000
            BOOST_CHECK_EQUAL( get_balance( seller_id, asset_id_type() ), 348 ); // 198 + 150

            BOOST_CHECK_EQUAL( mpa_id(db).dynamic_data(db).accumulated_collateral_fees.value, 55 ); // 17 + 38
         }
         else if( 3 == i )
         {
            // call2 is individually settled
            BOOST_CHECK( !db.find( call2_id ) );

            // margin call fee deducted = round_down(10000*11/1250) = 88,
            // fund receives 10000 - 88 = 9912, collateral = 1785 + 9912 = 11697
            // fund debt = 90015 + 1000000 = 1090015
            // fund price = 1090015 / 11697 = 93.187569462 > MCOP 59.78537052

            // the settled-debt order can't be matched with the limit order

            BOOST_REQUIRE( db.find( limit_id ) );
            BOOST_CHECK_EQUAL( limit_id(db).for_sale.value, 10000 ); // no change

            settled_debt = db.find_settled_debt_order(mpa_id);
            BOOST_REQUIRE( settled_debt );
            BOOST_CHECK( settled_debt->is_settled_debt );
            BOOST_CHECK_EQUAL( settled_debt->for_sale.value, 11697 );
            BOOST_CHECK_EQUAL( mpa_id(db).bitasset_data(db).individual_settlement_debt.value, 1090015 );
            BOOST_CHECK_EQUAL( mpa_id(db).bitasset_data(db).individual_settlement_fund.value, 11697 );
            BOOST_CHECK( settled_debt->sell_price == asset(11697)/asset(1090015,mpa_id) );

            BOOST_CHECK_EQUAL( get_balance( seller_id, mpa_id ), 80015 ); // no change
            BOOST_CHECK_EQUAL( get_balance( seller_id, asset_id_type() ), 198 ); // no change

            BOOST_CHECK_EQUAL( mpa_id(db).dynamic_data(db).accumulated_collateral_fees.value, 105 ); // 17 + 88
         }
         else if( 4 == i )
         {
            // call2 is margin called, matched with the limit order
            // the limit order is fully filled
            BOOST_CHECK( !db.find( limit_id ) );

            // call2 is partially filled
            // limit order receives = 150
            // call2 receives = 10000
            // margin call fee = round_down(150*11/1250) = 1
            // call2 pays 150 + 1 = 151

            BOOST_REQUIRE( db.find( call2_id ) );
            BOOST_CHECK_EQUAL( call2_id(db).debt.value, 90000 ); // 100000 - 10000
            BOOST_CHECK_EQUAL( call2_id(db).collateral.value, 2249 ); // 2400 - 151

            // the settled-debt order is not matched

            settled_debt = db.find_settled_debt_order(mpa_id);
            BOOST_REQUIRE( settled_debt );
            BOOST_CHECK( settled_debt->is_settled_debt );
            BOOST_CHECK_EQUAL( settled_debt->for_sale.value, 1506 ); // round_up( 90015 * 167265 / 10000000 )
            BOOST_CHECK_EQUAL( mpa_id(db).bitasset_data(db).individual_settlement_debt.value, 90015 );
            BOOST_CHECK_EQUAL( mpa_id(db).bitasset_data(db).individual_settlement_fund.value, 1785 );
            BOOST_CHECK( settled_debt->sell_price == asset(167265)/asset(10000000,mpa_id) );

            BOOST_CHECK_EQUAL( get_balance( seller_id, mpa_id ), 80015 ); // 100000 - 9985 - 10000
            BOOST_CHECK_EQUAL( get_balance( seller_id, asset_id_type() ), 348 ); // 198 + 150

            BOOST_CHECK_EQUAL( mpa_id(db).dynamic_data(db).accumulated_collateral_fees.value, 18 ); // 17 + 1
         }
         else if( 5 == i )
         {
            // call2 is unchanged
            BOOST_REQUIRE( db.find( call2_id ) );
            BOOST_CHECK_EQUAL( call2_id(db).debt.value, 100000 );
            BOOST_CHECK_EQUAL( call2_id(db).collateral.value, 5000 );

            // the settled-debt order is matched with the limit order
            // the limit order is fully filled
            BOOST_CHECK( !db.find( limit_id ) );

            // the settled-debt order is partially filled, match price is 10000:150
            // limit order receives = 150
            // settled debt receives = 10000, settled_debt = 90015 - 10000 = 80015
            // settled debt pays = round_down(10000*1785/90015) = 198, collateral fee = 198 - 150 = 48
            // settled_collateral = 1785 - 198 = 1587

            // then, the settled-debt order is matched with limit2
            // the settled-debt order is fully filled, match price is 10000:155
            // settled debt receives = 80015
            // limit2 receives = round_up(80015*155/10000) = 1241
            // settled debt pays = 1587, collateral fee = 1587 - 1241 = 346

            BOOST_CHECK( !db.find_settled_debt_order(mpa_id) );

            BOOST_CHECK_EQUAL( limit2_id(db).for_sale.value, 19985 ); // 100000 - 80015

            BOOST_CHECK_EQUAL( get_balance( seller_id, mpa_id ), 80015 ); // 100000 - 9985 - 10000
            BOOST_CHECK_EQUAL( get_balance( seller_id, asset_id_type() ), 348 ); // 198 + 150

            BOOST_CHECK_EQUAL( get_balance( seller2_id, mpa_id ), 0 ); // 100000 - 100000
            BOOST_CHECK_EQUAL( get_balance( seller2_id, asset_id_type() ), 1241 );

            BOOST_CHECK_EQUAL( mpa_id(db).dynamic_data(db).accumulated_collateral_fees.value, 411 ); // 17 + 48 + 346
         }
      };

      check_result();

      BOOST_TEST_MESSAGE( "Generate a block" );
      generate_block();

      check_result();

      // reset
      db.pop_block();

   } // for i

} FC_LOG_AND_RETHROW() }

/// Tests a scenario that force settlements get cancelled on expiration when there is no debt position
/// due to individual settlement to order
BOOST_AUTO_TEST_CASE( settle_order_cancel_due_to_no_debt_position )
{ try {

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
      asset_id_type mpa_id = mpa.get_id();

      BOOST_CHECK( mpa.bitasset_data(db).get_black_swan_response_method()
                   == bsrm_type::individual_settlement_to_order );

      acop.symbol = "SAMMPA2";
      acop.bitasset_opts->force_settlement_delay_sec = 60000;
      trx.operations.clear();
      trx.operations.push_back( acop );
      ptx = PUSH_TX(db, trx, ~0);
      const asset_object& mpa2 = db.get<asset_object>(ptx.operation_results[0].get<object_id_type>());
      asset_id_type mpa2_id = mpa2.get_id();

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
      BOOST_CHECK( !mpa.bitasset_data(db).is_globally_settled() );
      BOOST_CHECK( !mpa.bitasset_data(db).is_individually_settled_to_fund() );
      BOOST_CHECK( !db.find_settled_debt_order(mpa_id) );

      // borrowers borrow some
      // undercollateralization price = 100000:2000 * 1250:1000 = 100000:1600
      const call_order_object* call_ptr = borrow( borrower, asset(100000, mpa_id), asset(2000) );
      BOOST_REQUIRE( call_ptr );
      call_order_id_type call_id = call_ptr->get_id();

      // undercollateralization price = 100000:2100 * 1250:1000 = 100000:1680
      const call_order_object* call2_ptr = borrow( borrower, asset(100000, mpa2_id), asset(2100) );
      BOOST_REQUIRE( call2_ptr );
      call_order_id_type call2_id = call2_ptr->get_id();

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
      // call match price = 100000:1650 * 1000:1239 = 100000:2044.35 = 48.915303153

      // check
      BOOST_CHECK( mpa_id(db).bitasset_data(db).median_feed.settlement_price == f.settlement_price );
      BOOST_CHECK( mpa_id(db).bitasset_data(db).current_feed.settlement_price == f.settlement_price );
      BOOST_CHECK( !mpa_id(db).bitasset_data(db).is_globally_settled() );
      BOOST_CHECK( !mpa_id(db).bitasset_data(db).is_individually_settled_to_fund() );
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
      force_settlement_id_type settle_id { *result.get<extendable_operation_result>().value.new_objects->begin() };
      BOOST_REQUIRE( db.find(settle_id) );

      BOOST_CHECK_EQUAL( settle_id(db).balance.amount.value, 11100 );

      result = force_settle( seller, asset(11100,mpa2_id) );
      force_settlement_id_type settle2_id { *result.get<extendable_operation_result>().value.new_objects->begin() };
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
      BOOST_CHECK( !mpa_id(db).bitasset_data(db).is_globally_settled() );
      BOOST_CHECK( !mpa_id(db).bitasset_data(db).is_individually_settled_to_fund() );
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

} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_SUITE_END()
