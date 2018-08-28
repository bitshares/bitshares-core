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

#include <graphene/chain/account_object.hpp>

#include <fc/crypto/digest.hpp>

#include "../common/database_fixture.hpp"

using namespace graphene::chain;

BOOST_FIXTURE_TEST_SUITE( database_tests, database_fixture )

BOOST_AUTO_TEST_CASE( undo_test )
{
   try {
      database db;
      auto ses = db._undo_db.start_undo_session();
      const auto& bal_obj1 = db.create<account_balance_object>( [&]( account_balance_object& obj ){
               /* no balances right now */
      });
      auto id1 = bal_obj1.id;
      // abandon changes
      ses.undo();
      // start a new session
      ses = db._undo_db.start_undo_session();

      const auto& bal_obj2 = db.create<account_balance_object>( [&]( account_balance_object& obj ){
               /* no balances right now */
      });
      auto id2 = bal_obj2.id;
      BOOST_CHECK( id1 == id2 );
   } catch ( const fc::exception& e )
   {
      edump( (e.to_detail_string()) );
      throw;
   }
}

/**
 * Check that database modify() functors that throw do not get caught by boost, which will remove the object
 */
BOOST_AUTO_TEST_CASE(failed_modify_test)
{ try {
   database db;
   // Create dummy object
   const auto& obj = db.create<account_balance_object>([](account_balance_object& obj) {
                     obj.owner = account_id_type(123);
                  });
   account_balance_id_type obj_id = obj.id;
   BOOST_CHECK_EQUAL(obj.owner.instance.value, 123);

   // Modify dummy object, check that changes stick
   db.modify(obj, [](account_balance_object& obj) {
      obj.owner = account_id_type(234);
   });
   BOOST_CHECK_EQUAL(obj_id(db).owner.instance.value, 234);

   // Throw exception when modifying object, check that object still exists after
   BOOST_CHECK_THROW(db.modify(obj, [](account_balance_object& obj) {
      throw 5;
   }), int);
   BOOST_CHECK_NE((long)db.find_object(obj_id), (long)nullptr);
} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE( flat_index_test )
{ try {
   ACTORS((sam));
   const auto& bitusd = create_bitasset("USDBIT", sam.id);
   const asset_id_type bitusd_id = bitusd.id;
   update_feed_producers(bitusd, {sam.id});
   price_feed current_feed;
   current_feed.settlement_price = bitusd.amount(100) / asset(100);
   publish_feed(bitusd, sam, current_feed);
   BOOST_CHECK_EQUAL( (int)bitusd.bitasset_data_id->instance, 1 );
   BOOST_CHECK( !(*bitusd.bitasset_data_id)(db).current_feed.settlement_price.is_null() );
   try {
      auto ses = db._undo_db.start_undo_session();
      const auto& obj1 = db.create<asset_bitasset_data_object>( [&]( asset_bitasset_data_object& obj ){
          obj.settlement_fund = 17;
      });
      BOOST_REQUIRE_EQUAL( obj1.settlement_fund.value, 17 );
      throw std::string("Expected");
      // With flat_index, obj1 will not really be removed from the index
   } catch ( const std::string& e )
   { // ignore
   }

   // force maintenance
   const auto& dynamic_global_props = db.get<dynamic_global_property_object>(dynamic_global_property_id_type());
   generate_blocks(dynamic_global_props.next_maintenance_time, true);

   BOOST_CHECK( !(*bitusd_id(db).bitasset_data_id)(db).current_feed.settlement_price.is_null() );
} FC_CAPTURE_AND_RETHROW() }

BOOST_AUTO_TEST_CASE( merge_test )
{
   try {
      database db;
      auto ses = db._undo_db.start_undo_session();
      db.create<account_balance_object>( [&]( account_balance_object& obj ){
          obj.balance = 42;
      });
      ses.merge();

      auto balance = db.get_balance( account_id_type(), asset_id_type() );
      BOOST_CHECK_EQUAL( 42, balance.amount.value );
   } catch ( const fc::exception& e )
   {
      edump( (e.to_detail_string()) );
      throw;
   }
}

BOOST_AUTO_TEST_SUITE_END()
