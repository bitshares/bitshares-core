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
#include <graphene/chain/proposal_object.hpp>

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
   BOOST_CHECK_EQUAL(obj.owner.instance.value, 123u);

   // Modify dummy object, check that changes stick
   db.modify(obj, [](account_balance_object& obj) {
      obj.owner = account_id_type(234);
   });
   BOOST_CHECK_EQUAL(obj_id(db).owner.instance.value, 234u);

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

BOOST_AUTO_TEST_CASE( direct_index_test )
{ try {
   try {
      const graphene::db::primary_index< account_index, 6 > small_chunkbits( db );
      BOOST_FAIL( "Expected assertion failure!" );
   } catch( const fc::assert_exception& expected ) {}

   graphene::db::primary_index< account_index, 8 > my_accounts( db );
   const auto& direct = my_accounts.get_secondary_index<graphene::db::direct_index< account_object, 8 >>();
   BOOST_CHECK_EQUAL( 0u, my_accounts.indices().size() );
   BOOST_CHECK( nullptr == direct.find( account_id_type( 1 ) ) );
   // BOOST_CHECK_THROW( direct.find( asset_id_type( 1 ) ), fc::assert_exception ); // compile-time error
   BOOST_CHECK_THROW( direct.find( object_id_type( asset_id_type( 1 ) ) ), fc::assert_exception );
   BOOST_CHECK_THROW( direct.get( account_id_type( 1 ) ), fc::assert_exception );

   account_object test_account;
   test_account.id = account_id_type(1);
   test_account.name = "account1";

   my_accounts.load( fc::raw::pack( test_account ) );

   BOOST_CHECK_EQUAL( 1u, my_accounts.indices().size() );
   BOOST_CHECK( nullptr == direct.find( account_id_type( 0 ) ) );
   BOOST_CHECK( nullptr == direct.find( account_id_type( 2 ) ) );
   BOOST_CHECK( nullptr != direct.find( account_id_type( 1 ) ) );
   BOOST_CHECK_EQUAL( test_account.name, direct.get( test_account.id ).name );

   // The following assumes that MAX_HOLE = 100
   test_account.id = account_id_type(102);
   test_account.name = "account102";
   // highest insert was 1, direct.next is 2 => 102 is highest allowed instance
   my_accounts.load( fc::raw::pack( test_account ) );
   BOOST_CHECK_EQUAL( test_account.name, direct.get( test_account.id ).name );

   // direct.next is now 103, but index sequence counter is 0
   my_accounts.create( [] ( object& o ) {
       account_object& acct = dynamic_cast< account_object& >( o );
       BOOST_CHECK_EQUAL( 0u, acct.id.instance() );
       acct.name = "account0";
   } );

   test_account.id = account_id_type(50);
   test_account.name = "account50";
   my_accounts.load( fc::raw::pack( test_account ) );

   // can handle nested modification
   my_accounts.modify( direct.get( account_id_type(0) ), [&direct,&my_accounts] ( object& outer ) {
      account_object& _outer = dynamic_cast< account_object& >( outer );
      my_accounts.modify( direct.get( account_id_type(50) ), [] ( object& inner ) {
         account_object& _inner = dynamic_cast< account_object& >( inner );
         _inner.referrer = account_id_type(102);
      });
      _outer.options.voting_account = GRAPHENE_PROXY_TO_SELF_ACCOUNT;
   });

   // direct.next is still 103, so 204 is not allowed
   test_account.id = account_id_type(204);
   test_account.name = "account204";
   GRAPHENE_REQUIRE_THROW( my_accounts.load( fc::raw::pack( test_account ) ), fc::assert_exception );
   // This is actually undefined behaviour. The object has been inserted into
   // the primary index, but the secondary has refused to insert it!
   BOOST_CHECK_EQUAL( 5u, my_accounts.indices().size() );

   uint32_t count = 0;
   for( uint32_t i = 0; i < 250; i++ )
   {
      const account_object* aptr = dynamic_cast< const account_object* >( my_accounts.find( account_id_type( i ) ) );
      if( aptr )
      {
         count++;
         BOOST_CHECK( aptr->id.instance() == 0 || aptr->id.instance() == 1
                      || aptr->id.instance() == 50 || aptr->id.instance() == 102 );
         BOOST_CHECK_EQUAL( i, aptr->id.instance() );
         BOOST_CHECK_EQUAL( "account" + std::to_string( i ), aptr->name );
      }
   }
   BOOST_CHECK_EQUAL( count, my_accounts.indices().size() - 1 );

   GRAPHENE_REQUIRE_THROW( my_accounts.modify( direct.get( account_id_type( 1 ) ), [] ( object& acct ) {
      acct.id = account_id_type(2);
   }), fc::assert_exception );
   // This is actually undefined behaviour. The object has been modified, but
   // but the secondary has not updated its representation
} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE( required_approval_index_test ) // see https://github.com/bitshares/bitshares-core/issues/1719
{ try {
   ACTORS( (alice)(bob)(charlie)(agnetha)(benny)(carlos) );

   database db1;
   db1.initialize_indexes();
   const auto& proposals = db1.get_index_type< primary_index< proposal_index > >();
   const auto& required_approvals = proposals.get_secondary_index< required_approval_index >()._account_to_proposals;

   // Create a proposal
   const auto& prop = db1.create<proposal_object>( [this,alice_id,agnetha_id]( object& o ) {
      proposal_object& prop = static_cast<proposal_object&>(o);
      prop.proposer = committee_account;
      prop.required_active_approvals.insert( alice_id );
      prop.required_owner_approvals.insert( agnetha_id );
   });

   BOOST_CHECK_EQUAL( 2u, required_approvals.size() );
   BOOST_REQUIRE( required_approvals.find( alice_id )   != required_approvals.end() );
   BOOST_REQUIRE( required_approvals.find( agnetha_id ) != required_approvals.end() );
   BOOST_CHECK_EQUAL( 1u, required_approvals.find(alice_id)->second.size() );
   BOOST_CHECK_EQUAL( 1u, required_approvals.find(agnetha_id)->second.size() );

   // add approvals
   db1.modify( prop, [bob_id,benny_id]( object& o ) {
      proposal_object& prop = static_cast<proposal_object&>(o);
      prop.available_active_approvals.insert( bob_id );
      prop.available_owner_approvals.insert( benny_id );
   });

   BOOST_CHECK_EQUAL( 4u, required_approvals.size() );
   BOOST_REQUIRE( required_approvals.find( bob_id )   != required_approvals.end() );
   BOOST_REQUIRE( required_approvals.find( benny_id ) != required_approvals.end() );
   BOOST_CHECK_EQUAL( 1u, required_approvals.find(bob_id)->second.size() );
   BOOST_CHECK_EQUAL( 1u, required_approvals.find(benny_id)->second.size() );

   // remove approvals + add others
   db1.modify( prop, [bob_id,charlie_id,benny_id,carlos_id]( object& o ) {
      proposal_object& prop = static_cast<proposal_object&>(o);
      prop.available_active_approvals.insert( charlie_id );
      prop.available_owner_approvals.insert( carlos_id );
      prop.available_active_approvals.erase( bob_id );
      prop.available_owner_approvals.erase( benny_id );
   });

   BOOST_CHECK_EQUAL( 4u, required_approvals.size() );
   BOOST_REQUIRE( required_approvals.find( charlie_id ) != required_approvals.end() );
   BOOST_REQUIRE( required_approvals.find( carlos_id )  != required_approvals.end() );
   BOOST_CHECK_EQUAL( 1u, required_approvals.find(charlie_id)->second.size() );
   BOOST_CHECK_EQUAL( 1u, required_approvals.find(carlos_id)->second.size() );

   // simulate save/restore
   std::vector<char> serialized = fc::raw::pack( prop );
   database db2;
   db2.initialize_indexes();
   const auto& reloaded_proposals = db2.get_index_type< primary_index< proposal_index > >();
   const auto& reloaded_approvals = reloaded_proposals.get_secondary_index<required_approval_index>()
                                                                                              ._account_to_proposals;
   const_cast< primary_index< proposal_index >& >( reloaded_proposals ).load( serialized );
   const auto& prop2 = *reloaded_proposals.indices().begin();

   BOOST_CHECK_EQUAL( 4u, reloaded_approvals.size() );
   BOOST_REQUIRE( reloaded_approvals.find( charlie_id ) != reloaded_approvals.end() );
   BOOST_REQUIRE( reloaded_approvals.find( carlos_id )  != reloaded_approvals.end() );
   BOOST_CHECK_EQUAL( 1u, reloaded_approvals.find(charlie_id)->second.size() );
   BOOST_CHECK_EQUAL( 1u, reloaded_approvals.find(carlos_id)->second.size() );

   db2.modify( prop2, []( object& o ) {
      proposal_object& prop = static_cast<proposal_object&>(o);
      prop.available_active_approvals.clear();
      prop.available_owner_approvals.clear();
   });

   BOOST_CHECK_EQUAL( 2u, reloaded_approvals.size() );
   BOOST_REQUIRE( reloaded_approvals.find( alice_id )   != reloaded_approvals.end() );
   BOOST_REQUIRE( reloaded_approvals.find( agnetha_id ) != reloaded_approvals.end() );

   db2.remove( prop2 );

   BOOST_CHECK_EQUAL( 0u, reloaded_approvals.size() );

   db1.remove( prop );

   BOOST_CHECK_EQUAL( 0u, required_approvals.size() );

} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_SUITE_END()
