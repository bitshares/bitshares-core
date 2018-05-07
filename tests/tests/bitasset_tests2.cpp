/*
 * Copyright (c) 2018 Bitshares Foundation, and contributors.
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

#include <vector>
#include <boost/test/unit_test.hpp>

#include <graphene/chain/database.hpp>
#include <graphene/chain/hardfork.hpp>

#include <graphene/chain/balance_object.hpp>
#include <graphene/chain/budget_record_object.hpp>
#include <graphene/chain/committee_member_object.hpp>
#include <graphene/chain/market_object.hpp>
#include <graphene/chain/withdraw_permission_object.hpp>
#include <graphene/chain/witness_object.hpp>
#include <graphene/chain/worker_object.hpp>

#include <graphene/utilities/tempdir.hpp>

#include <fc/crypto/digest.hpp>

#include "../common/database_fixture.hpp"

using namespace graphene::chain;
using namespace graphene::chain::test;

BOOST_FIXTURE_TEST_SUITE( bitasset_tests, database_fixture )

/*********
 * @brief Update median feeds after feed_lifetime_sec changed
 */
BOOST_AUTO_TEST_CASE( hf_890_test )
{
   BOOST_TEST_MESSAGE("Advance to near hard fork");
   auto maint_interval = db.get_global_properties().parameters.maintenance_interval;
   set_expiration( db, trx );

   ACTORS((buyer)(seller)(borrower)(borrower2)(borrower3)(borrower4)(feedproducer));

   const auto& bitusd = create_bitasset("USDBIT", feedproducer_id);
   const auto& core   = asset_id_type()(db);

   int64_t init_balance(1000000);

   transfer(committee_account, buyer_id, asset(init_balance));
   transfer(committee_account, borrower_id, asset(init_balance));
   transfer(committee_account, borrower2_id, asset(init_balance));
   transfer(committee_account, borrower3_id, asset(init_balance));
   transfer(committee_account, borrower4_id, asset(init_balance));
   update_feed_producers( bitusd, {feedproducer.id} );

   BOOST_TEST_MESSAGE("Add a price feed");
   price_feed current_feed;
   current_feed.maintenance_collateral_ratio = 1750;
   current_feed.maximum_short_squeeze_ratio = 1100;
   current_feed.settlement_price = bitusd.amount( 100 ) / core.amount( 5 );
   publish_feed( bitusd, feedproducer, current_feed );

   BOOST_TEST_MESSAGE("Place some collateralized orders");
   // start out with 200% collateral, call price is 10/175 CORE/USD = 40/700
   const call_order_object& call = *borrow( borrower, bitusd.amount(10), asset(1));
   call_order_id_type call_id = call.id;
   // create another position with 310% collateral, call price is 15.5/175 CORE/USD = 62/700
   const call_order_object& call2 = *borrow( borrower2, bitusd.amount(100000), asset(15500));
   call_order_id_type call2_id = call2.id;
   // create yet another position with 350% collateral, call price is 17.5/175 CORE/USD = 77/700
   const call_order_object& call3 = *borrow( borrower3, bitusd.amount(100000), asset(17500));
   call_order_id_type call3_id = call3.id;
   transfer(borrower, seller, bitusd.amount(10));
   transfer(borrower2, seller, bitusd.amount(100000));
   transfer(borrower3, seller, bitusd.amount(100000));

   BOOST_TEST_MESSAGE("Adjust price feed to get call order into margin call territory");
   current_feed.settlement_price = bitusd.amount( 120 ) / core.amount(10);
   publish_feed( bitusd, feedproducer, current_feed );
   generate_block();
   trx.clear();
   // settlement price = 120 USD / 10 CORE, mssp = 120/11 USD/CORE

   // change feed lifetime
   {
      BOOST_TEST_MESSAGE("Adjust feed lifetime");
      asset_update_bitasset_operation ba_op;
      const asset_object& asset_to_update = bitusd;
      ba_op.asset_to_update = bitusd.get_id();
      ba_op.issuer = asset_to_update.issuer;
      ba_op.new_options.feed_lifetime_sec = HARDFORK_CORE_890_TIME.sec_since_epoch() + 10;
      trx.operations.push_back(ba_op);
      sign(trx, feedproducer_private_key);
      PUSH_TX(db, trx, ~0);
      generate_block();
      trx.clear();
   }
   // TODO: make sure median feed updated
   // check_call_orders() should NOT have been called
   BOOST_TEST_MESSAGE("No orders should have been filled");
   BOOST_CHECK( db.find<call_order_object>( call_id ) ); // should not have been filled
   BOOST_CHECK( db.find<call_order_object>( call2_id ) ); // should not have been filled
   BOOST_CHECK( db.find<call_order_object>( call3_id ) ); // should not have been filled


   // go beyond hardfork
   BOOST_TEST_MESSAGE("Moving beyond hardfork 890");
   generate_blocks(HARDFORK_CORE_890_TIME + maint_interval);
   set_expiration( db, trx );

   // change feed lifetime
   {
      BOOST_TEST_MESSAGE("Adjust feed lifetime again.");
      asset_update_bitasset_operation ba_op;
      const asset_object& asset_to_update = bitusd;
      ba_op.asset_to_update = bitusd.get_id();
      ba_op.issuer = asset_to_update.issuer;
      ba_op.new_options.feed_lifetime_sec = HARDFORK_CORE_890_TIME.sec_since_epoch() + 20;
      trx.operations.push_back(ba_op);
      sign(trx, feedproducer_private_key);
      PUSH_TX(db, trx, ~0);
      generate_block();
      trx.clear();
   }

   // TODO: make sure median feed updated
   // TODO: check call orders should have been called
   BOOST_TEST_MESSAGE("check_call_orders should have been called");
   BOOST_CHECK( !db.find<call_order_object>( call_id ) ); // should have been filled
   BOOST_CHECK( !db.find<call_order_object>( call2_id ) ); // should have been filled
   BOOST_CHECK( !db.find<call_order_object>( call3_id ) ); // should have been filled

}

BOOST_AUTO_TEST_SUITE_END()
