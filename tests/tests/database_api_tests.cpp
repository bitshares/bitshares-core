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

#include <graphene/app/database_api.hpp>
#include <graphene/chain/hardfork.hpp>

#include <fc/crypto/digest.hpp>
#include <fc/crypto/hex.hpp>

#include "../common/database_fixture.hpp"

#include <random>

using namespace graphene::chain;
using namespace graphene::chain::test;

BOOST_FIXTURE_TEST_SUITE(database_api_tests, database_fixture)

BOOST_AUTO_TEST_CASE(is_registered)
{
   try {
      /***
       * Arrange
       */
      auto nathan_private_key = generate_private_key("nathan");
      public_key_type nathan_public = nathan_private_key.get_public_key();

      auto dan_private_key = generate_private_key("dan");
      public_key_type dan_public = dan_private_key.get_public_key();

      auto unregistered_private_key = generate_private_key("unregistered");
      public_key_type unregistered_public = unregistered_private_key.get_public_key();


      /***
       * Act
       */
      create_account("dan", dan_private_key.get_public_key());
      create_account("nathan", nathan_private_key.get_public_key());
      // Unregistered key will not be registered with any account


      /***
       * Assert
       */
      graphene::app::database_api db_api1(db);
      BOOST_CHECK_THROW( db_api1.is_public_key_registered((string) nathan_public), fc::exception );

      graphene::app::application_options opt = app.get_options();
      opt.has_api_helper_indexes_plugin = true;
      graphene::app::database_api db_api( db, &opt );

      BOOST_CHECK(db_api.is_public_key_registered((string) nathan_public));
      BOOST_CHECK(db_api.is_public_key_registered((string) dan_public));
      BOOST_CHECK(!db_api.is_public_key_registered((string) unregistered_public));

   } FC_LOG_AND_RETHROW()
}

BOOST_AUTO_TEST_CASE( get_potential_signatures_owner_and_active )
{
   try {
      fc::ecc::private_key nathan_key1 = fc::ecc::private_key::regenerate(fc::digest("key1"));
      fc::ecc::private_key nathan_key2 = fc::ecc::private_key::regenerate(fc::digest("key2"));
      public_key_type pub_key_active( nathan_key1.get_public_key() );
      public_key_type pub_key_owner( nathan_key2.get_public_key() );
      const account_object& nathan = create_account("nathan", nathan_key1.get_public_key() );

      try {
         account_update_operation op;
         op.account = nathan.id;
         op.active = authority(1, pub_key_active, 1);
         op.owner = authority(1, pub_key_owner, 1);
         trx.operations.push_back(op);
         sign(trx, nathan_key1);
         PUSH_TX( db, trx, database::skip_transaction_dupe_check );
         trx.clear();
      } FC_CAPTURE_AND_RETHROW ((nathan.active))

      // this op requires active
      transfer_operation op;
      op.from = nathan.id;
      op.to = account_id_type();
      trx.operations.push_back(op);

      graphene::app::database_api db_api(db);
      set<public_key_type> pub_keys = db_api.get_potential_signatures( trx );

      BOOST_CHECK( pub_keys.find( pub_key_active ) != pub_keys.end() );
      BOOST_CHECK( pub_keys.find( pub_key_owner ) != pub_keys.end() );

      trx.operations.clear();

      // this op requires owner
      account_update_operation auop;
      auop.account = nathan.id;
      auop.owner = authority(1, pub_key_owner, 1);
      trx.operations.push_back(auop);

      pub_keys = db_api.get_potential_signatures( trx );

      BOOST_CHECK( pub_keys.find( pub_key_active ) == pub_keys.end() ); // active key doesn't help in this case
      BOOST_CHECK( pub_keys.find( pub_key_owner ) != pub_keys.end() );

   } FC_LOG_AND_RETHROW()
}

/// Testing get_potential_signatures and get_required_signatures for non-immediate owner authority issue.
/// https://github.com/bitshares/bitshares-core/issues/584
BOOST_AUTO_TEST_CASE( get_signatures_non_immediate_owner )
{
   try {
      fc::ecc::private_key nathan_key1 = fc::ecc::private_key::regenerate(fc::digest("key1"));
      fc::ecc::private_key nathan_key2 = fc::ecc::private_key::regenerate(fc::digest("key2"));
      fc::ecc::private_key ashley_key1 = fc::ecc::private_key::regenerate(fc::digest("akey1"));
      fc::ecc::private_key ashley_key2 = fc::ecc::private_key::regenerate(fc::digest("akey2"));
      fc::ecc::private_key oliver_key1 = fc::ecc::private_key::regenerate(fc::digest("okey1"));
      fc::ecc::private_key oliver_key2 = fc::ecc::private_key::regenerate(fc::digest("okey2"));
      public_key_type pub_key_active( nathan_key1.get_public_key() );
      public_key_type pub_key_owner( nathan_key2.get_public_key() );
      public_key_type a_pub_key_active( ashley_key1.get_public_key() );
      public_key_type a_pub_key_owner( ashley_key2.get_public_key() );
      public_key_type o_pub_key_active( oliver_key1.get_public_key() );
      public_key_type o_pub_key_owner( oliver_key2.get_public_key() );
      const account_object& nathan = create_account("nathan", nathan_key1.get_public_key() );
      const account_object& ashley = create_account("ashley", ashley_key1.get_public_key() );
      const account_object& oliver = create_account("oliver", oliver_key1.get_public_key() );
      account_id_type nathan_id = nathan.id;
      account_id_type ashley_id = ashley.id;
      account_id_type oliver_id = oliver.id;

      try {
         account_update_operation op;
         op.account = nathan_id;
         op.active = authority(1, pub_key_active, 1, ashley_id, 1);
         op.owner = authority(1, pub_key_owner, 1, oliver_id, 1);
         trx.operations.push_back(op);
         sign(trx, nathan_key1);
         PUSH_TX( db, trx, database::skip_transaction_dupe_check );
         trx.clear();

         op.account = ashley_id;
         op.active = authority(1, a_pub_key_active, 1);
         op.owner = authority(1, a_pub_key_owner, 1);
         trx.operations.push_back(op);
         sign(trx, ashley_key1);
         PUSH_TX( db, trx, database::skip_transaction_dupe_check );
         trx.clear();

         op.account = oliver_id;
         op.active = authority(1, o_pub_key_active, 1);
         op.owner = authority(1, o_pub_key_owner, 1);
         trx.operations.push_back(op);
         sign(trx, oliver_key1);
         PUSH_TX( db, trx, database::skip_transaction_dupe_check );
         trx.clear();
      } FC_CAPTURE_AND_RETHROW ((nathan.active))

      // this transaction requires active
      signed_transaction trx_a;
      transfer_operation op;
      op.from = nathan_id;
      op.to = account_id_type();
      trx_a.operations.push_back(op);

      // get potential signatures
      graphene::app::database_api db_api(db);
      set<public_key_type> pub_keys = db_api.get_potential_signatures( trx_a );

      BOOST_CHECK( pub_keys.find( pub_key_active ) != pub_keys.end() );
      BOOST_CHECK( pub_keys.find( pub_key_owner ) != pub_keys.end() );
      BOOST_CHECK( pub_keys.find( a_pub_key_active ) != pub_keys.end() );
      // doesn't work due to https://github.com/bitshares/bitshares-core/issues/584
      BOOST_CHECK( pub_keys.find( a_pub_key_owner ) == pub_keys.end() );
      BOOST_CHECK( pub_keys.find( o_pub_key_active ) != pub_keys.end() );
      // doesn't work due to https://github.com/bitshares/bitshares-core/issues/584
      BOOST_CHECK( pub_keys.find( o_pub_key_owner ) == pub_keys.end() );

      // get required signatures
      pub_keys = db_api.get_required_signatures( trx_a, { a_pub_key_owner, o_pub_key_owner } );
      BOOST_CHECK( pub_keys.empty() );

      // this op requires owner
      signed_transaction trx_o;
      account_update_operation auop;
      auop.account = nathan_id;
      auop.owner = authority(1, pub_key_owner, 1);
      trx_o.operations.push_back(auop);

      // get potential signatures
      pub_keys = db_api.get_potential_signatures( trx_o );

      // active authorities doesn't help in this case
      BOOST_CHECK( pub_keys.find( pub_key_active ) == pub_keys.end() );
      BOOST_CHECK( pub_keys.find( a_pub_key_active ) == pub_keys.end() );
      BOOST_CHECK( pub_keys.find( a_pub_key_owner ) == pub_keys.end() );

      // owner authorities should be ok
      BOOST_CHECK( pub_keys.find( pub_key_owner ) != pub_keys.end() );
      BOOST_CHECK( pub_keys.find( o_pub_key_active ) != pub_keys.end() );
      // doesn't work due to https://github.com/bitshares/bitshares-core/issues/584
      BOOST_CHECK( pub_keys.find( o_pub_key_owner ) == pub_keys.end() );

      // get required signatures
      pub_keys = db_api.get_required_signatures( trx_o, { a_pub_key_owner, o_pub_key_owner } );
      BOOST_CHECK( pub_keys.empty() );

      // go beyond hard fork
      generate_blocks( HARDFORK_CORE_584_TIME, true );

      // for the transaction that requires active
      // get potential signatures
      pub_keys = db_api.get_potential_signatures( trx_a );

      // all authorities should be ok
      BOOST_CHECK( pub_keys.find( pub_key_active ) != pub_keys.end() );
      BOOST_CHECK( pub_keys.find( a_pub_key_active ) != pub_keys.end() );
      BOOST_CHECK( pub_keys.find( a_pub_key_owner ) != pub_keys.end() );
      BOOST_CHECK( pub_keys.find( pub_key_owner ) != pub_keys.end() );
      BOOST_CHECK( pub_keys.find( o_pub_key_active ) != pub_keys.end() );
      BOOST_CHECK( pub_keys.find( o_pub_key_owner ) != pub_keys.end() );

      // get required signatures
      pub_keys = db_api.get_required_signatures( trx_a, { a_pub_key_owner } );
      BOOST_CHECK( pub_keys.find( a_pub_key_owner ) != pub_keys.end() );
      pub_keys = db_api.get_required_signatures( trx_a, { o_pub_key_owner } );
      BOOST_CHECK( pub_keys.find( o_pub_key_owner ) != pub_keys.end() );

      // for the transaction that requires owner
      // get potential signatures
      pub_keys = db_api.get_potential_signatures( trx_o );

      // active authorities doesn't help in this case
      BOOST_CHECK( pub_keys.find( pub_key_active ) == pub_keys.end() );
      BOOST_CHECK( pub_keys.find( a_pub_key_active ) == pub_keys.end() );
      BOOST_CHECK( pub_keys.find( a_pub_key_owner ) == pub_keys.end() );

      // owner authorities should help
      BOOST_CHECK( pub_keys.find( pub_key_owner ) != pub_keys.end() );
      BOOST_CHECK( pub_keys.find( o_pub_key_active ) != pub_keys.end() );
      BOOST_CHECK( pub_keys.find( o_pub_key_owner ) != pub_keys.end() );

      // get required signatures
      pub_keys = db_api.get_required_signatures( trx_o, { a_pub_key_owner, o_pub_key_owner } );
      BOOST_CHECK( pub_keys.find( a_pub_key_owner ) == pub_keys.end() );
      BOOST_CHECK( pub_keys.find( o_pub_key_owner ) != pub_keys.end() );

   } FC_LOG_AND_RETHROW()
}

BOOST_AUTO_TEST_CASE( get_potential_signatures_other )
{
   try {
      fc::ecc::private_key priv_key1 = fc::ecc::private_key::regenerate(fc::digest("key1"));
      public_key_type pub_key1( priv_key1.get_public_key() );

      const account_object& nathan = create_account( "nathan" );

      balance_claim_operation op;
      op.deposit_to_account = nathan.id;
      op.balance_owner_key = pub_key1;
      trx.operations.push_back(op);

      graphene::app::database_api db_api(db);
      set<public_key_type> pub_keys = db_api.get_potential_signatures( trx );

      BOOST_CHECK( pub_keys.find( pub_key1 ) != pub_keys.end() );

   } FC_LOG_AND_RETHROW()
}

BOOST_AUTO_TEST_CASE( get_required_signatures_owner_or_active )
{
   try {
      fc::ecc::private_key nathan_key1 = fc::ecc::private_key::regenerate(fc::digest("key1"));
      fc::ecc::private_key nathan_key2 = fc::ecc::private_key::regenerate(fc::digest("key2"));
      public_key_type pub_key_active( nathan_key1.get_public_key() );
      public_key_type pub_key_owner( nathan_key2.get_public_key() );
      const account_object& nathan = create_account("nathan", nathan_key1.get_public_key() );

      try {
         account_update_operation op;
         op.account = nathan.id;
         op.active = authority(1, pub_key_active, 1);
         op.owner = authority(1, pub_key_owner, 1);
         trx.operations.push_back(op);
         sign(trx, nathan_key1);
         PUSH_TX( db, trx, database::skip_transaction_dupe_check );
         trx.clear();
      } FC_CAPTURE_AND_RETHROW ((nathan.active))

      graphene::app::database_api db_api(db);

      // prepare available keys sets
      flat_set<public_key_type> avail_keys1, avail_keys2, avail_keys3;
      avail_keys1.insert( pub_key_active );
      avail_keys2.insert( pub_key_owner );
      avail_keys3.insert( pub_key_active );
      avail_keys3.insert( pub_key_owner );

      set<public_key_type> pub_keys;

      // this op requires active
      transfer_operation op;
      op.from = nathan.id;
      op.to = account_id_type();
      trx.operations.push_back(op);

      // provides active, should be ok
      pub_keys = db_api.get_required_signatures( trx, avail_keys1 );
      BOOST_CHECK( pub_keys.find( pub_key_active ) != pub_keys.end() );

      // provides owner, should be ok
      pub_keys = db_api.get_required_signatures( trx, avail_keys2 );
      BOOST_CHECK( pub_keys.find( pub_key_owner ) != pub_keys.end() );

      // provides both active and owner, should return one of them
      pub_keys = db_api.get_required_signatures( trx, avail_keys3 );
      BOOST_CHECK( pub_keys.size() == 1 );
      BOOST_CHECK( pub_keys.find( pub_key_active ) != pub_keys.end() || pub_keys.find( pub_key_owner ) != pub_keys.end() );

      trx.operations.clear();

      // this op requires owner
      account_update_operation auop;
      auop.account = nathan.id;
      auop.owner = authority(1, pub_key_owner, 1);
      trx.operations.push_back(auop);

      // provides active, should return an empty set
      pub_keys = db_api.get_required_signatures( trx, avail_keys1 );
      BOOST_CHECK( pub_keys.size() == 0 );

      // provides owner, should return it
      pub_keys = db_api.get_required_signatures( trx, avail_keys2 );
      BOOST_CHECK( pub_keys.find( pub_key_owner ) != pub_keys.end() );

      // provides both active and owner, should return owner only
      pub_keys = db_api.get_required_signatures( trx, avail_keys3 );
      BOOST_CHECK( pub_keys.find( pub_key_active ) == pub_keys.end() );
      BOOST_CHECK( pub_keys.find( pub_key_owner ) != pub_keys.end() );

   } FC_LOG_AND_RETHROW()
}

BOOST_AUTO_TEST_CASE( get_required_signatures_partially_signed_or_not )
{
   try {
      fc::ecc::private_key morgan_key = fc::ecc::private_key::regenerate(fc::digest("morgan_key"));
      fc::ecc::private_key nathan_key = fc::ecc::private_key::regenerate(fc::digest("nathan_key"));
      fc::ecc::private_key oliver_key = fc::ecc::private_key::regenerate(fc::digest("oliver_key"));
      public_key_type pub_key_morgan( morgan_key.get_public_key() );
      public_key_type pub_key_nathan( nathan_key.get_public_key() );
      public_key_type pub_key_oliver( oliver_key.get_public_key() );
      const account_object& morgan = create_account("morgan", morgan_key.get_public_key() );
      const account_object& nathan = create_account("nathan", nathan_key.get_public_key() );
      const account_object& oliver = create_account("oliver", oliver_key.get_public_key() );

      graphene::app::database_api db_api(db);

      // prepare available keys sets
      flat_set<public_key_type> avail_keys_empty, avail_keys_m, avail_keys_n, avail_keys_o;
      flat_set<public_key_type> avail_keys_mn, avail_keys_mo, avail_keys_no, avail_keys_mno;
      avail_keys_m.insert( pub_key_morgan );
      avail_keys_mn.insert( pub_key_morgan );
      avail_keys_mo.insert( pub_key_morgan );
      avail_keys_mno.insert( pub_key_morgan );
      avail_keys_n.insert( pub_key_nathan );
      avail_keys_mn.insert( pub_key_nathan );
      avail_keys_no.insert( pub_key_nathan );
      avail_keys_mno.insert( pub_key_nathan );
      avail_keys_o.insert( pub_key_oliver );
      avail_keys_mo.insert( pub_key_oliver );
      avail_keys_no.insert( pub_key_oliver );
      avail_keys_mno.insert( pub_key_oliver );

      // result set
      set<public_key_type> pub_keys;

      // make a transaction that require 1 signature (m)
      transfer_operation op;
      op.from = morgan.id;
      op.to = oliver.id;
      trx.operations.push_back(op);

      // provides [], should return []
      pub_keys = db_api.get_required_signatures( trx, avail_keys_empty );
      BOOST_CHECK( pub_keys.size() == 0 );

     // provides [m], should return [m]
      pub_keys = db_api.get_required_signatures( trx, avail_keys_m );
      BOOST_CHECK( pub_keys.size() == 1 );
      BOOST_CHECK( pub_keys.find( pub_key_morgan ) != pub_keys.end() );

      // provides [n], should return []
      pub_keys = db_api.get_required_signatures( trx, avail_keys_n );
      BOOST_CHECK( pub_keys.size() == 0 );

       // provides [m,n], should return [m]
      pub_keys = db_api.get_required_signatures( trx, avail_keys_mn );
      BOOST_CHECK( pub_keys.size() == 1 );
      BOOST_CHECK( pub_keys.find( pub_key_morgan ) != pub_keys.end() );

      // sign with n, but actually need m
      sign(trx, nathan_key);

      // provides [], should return []
      pub_keys = db_api.get_required_signatures( trx, avail_keys_empty );
      BOOST_CHECK( pub_keys.size() == 0 );

      // provides [m], should return [m]
      pub_keys = db_api.get_required_signatures( trx, avail_keys_m );
      BOOST_CHECK( pub_keys.size() == 1 );
      BOOST_CHECK( pub_keys.find( pub_key_morgan ) != pub_keys.end() );

      // provides [n], should return []
      pub_keys = db_api.get_required_signatures( trx, avail_keys_n );
      BOOST_CHECK( pub_keys.size() == 0 );

      // provides [o], should return []
      pub_keys = db_api.get_required_signatures( trx, avail_keys_o );
      BOOST_CHECK( pub_keys.size() == 0 );

      // provides [m,n], should return [m]
      pub_keys = db_api.get_required_signatures( trx, avail_keys_mn );
      BOOST_CHECK( pub_keys.size() == 1 );
      BOOST_CHECK( pub_keys.find( pub_key_morgan ) != pub_keys.end() );

      // provides [m,o], should return [m]
      pub_keys = db_api.get_required_signatures( trx, avail_keys_mo );
      BOOST_CHECK( pub_keys.size() == 1 );
      BOOST_CHECK( pub_keys.find( pub_key_morgan ) != pub_keys.end() );

      // provides [n,o], should return []
      pub_keys = db_api.get_required_signatures( trx, avail_keys_no );
      BOOST_CHECK( pub_keys.size() == 0 );

      // provides [m,n,o], should return [m]
      pub_keys = db_api.get_required_signatures( trx, avail_keys_mno );
      BOOST_CHECK( pub_keys.size() == 1 );
      BOOST_CHECK( pub_keys.find( pub_key_morgan ) != pub_keys.end() );

      // sign with m, should be enough
      trx.clear_signatures();
      sign(trx, morgan_key);

      // provides [], should return []
      pub_keys = db_api.get_required_signatures( trx, avail_keys_empty );
      BOOST_CHECK( pub_keys.size() == 0 );

      // provides [m], should return []
      pub_keys = db_api.get_required_signatures( trx, avail_keys_m );
      BOOST_CHECK( pub_keys.size() == 0 );

      // provides [n], should return []
      pub_keys = db_api.get_required_signatures( trx, avail_keys_n );
      BOOST_CHECK( pub_keys.size() == 0 );

      // provides [m,n], should return []
      pub_keys = db_api.get_required_signatures( trx, avail_keys_mn );
      BOOST_CHECK( pub_keys.size() == 0 );

      // sign with m+n, although m only should be enough, this API won't complain
      sign(trx, nathan_key);

      // provides [], should return []
      pub_keys = db_api.get_required_signatures( trx, avail_keys_empty );
      BOOST_CHECK( pub_keys.size() == 0 );

      // provides [m], should return []
      pub_keys = db_api.get_required_signatures( trx, avail_keys_m );
      BOOST_CHECK( pub_keys.size() == 0 );

      // provides [n], should return []
      pub_keys = db_api.get_required_signatures( trx, avail_keys_n );
      BOOST_CHECK( pub_keys.size() == 0 );

      // provides [o], should return []
      pub_keys = db_api.get_required_signatures( trx, avail_keys_o );
      BOOST_CHECK( pub_keys.size() == 0 );

      // provides [m,n], should return []
      pub_keys = db_api.get_required_signatures( trx, avail_keys_mn );
      BOOST_CHECK( pub_keys.size() == 0 );

      // provides [m,o], should return []
      pub_keys = db_api.get_required_signatures( trx, avail_keys_mo );
      BOOST_CHECK( pub_keys.size() == 0 );

      // provides [n,o], should return []
      pub_keys = db_api.get_required_signatures( trx, avail_keys_no );
      BOOST_CHECK( pub_keys.size() == 0 );

      // provides [m,n,o], should return []
      pub_keys = db_api.get_required_signatures( trx, avail_keys_mno );
      BOOST_CHECK( pub_keys.size() == 0 );

      // make a transaction that require 2 signatures (m+n)
      trx.clear_signatures();
      op.from = nathan.id;
      trx.operations.push_back(op);

      // provides [], should return []
      pub_keys = db_api.get_required_signatures( trx, avail_keys_empty );
      BOOST_CHECK( pub_keys.size() == 0 );

      // provides [m], should return [m]
      pub_keys = db_api.get_required_signatures( trx, avail_keys_m );
      BOOST_CHECK( pub_keys.size() == 1 );
      BOOST_CHECK( pub_keys.find( pub_key_morgan ) != pub_keys.end() );

      // provides [n], should return [n]
      pub_keys = db_api.get_required_signatures( trx, avail_keys_n );
      BOOST_CHECK( pub_keys.size() == 1 );
      BOOST_CHECK( pub_keys.find( pub_key_nathan ) != pub_keys.end() );

      // provides [o], should return []
      pub_keys = db_api.get_required_signatures( trx, avail_keys_o );
      BOOST_CHECK( pub_keys.size() == 0 );

      // provides [m,n], should return [m,n]
      pub_keys = db_api.get_required_signatures( trx, avail_keys_mn );
      BOOST_CHECK( pub_keys.size() == 2 );
      BOOST_CHECK( pub_keys.find( pub_key_morgan ) != pub_keys.end() );
      BOOST_CHECK( pub_keys.find( pub_key_nathan ) != pub_keys.end() );

      // provides [m,o], should return [m]
      pub_keys = db_api.get_required_signatures( trx, avail_keys_mo );
      BOOST_CHECK( pub_keys.size() == 1 );
      BOOST_CHECK( pub_keys.find( pub_key_morgan ) != pub_keys.end() );

      // provides [n,o], should return [n]
      pub_keys = db_api.get_required_signatures( trx, avail_keys_no );
      BOOST_CHECK( pub_keys.size() == 1 );
      BOOST_CHECK( pub_keys.find( pub_key_nathan ) != pub_keys.end() );

      // provides [m,n,o], should return [m,n]
      pub_keys = db_api.get_required_signatures( trx, avail_keys_mno );
      BOOST_CHECK( pub_keys.size() == 2 );
      BOOST_CHECK( pub_keys.find( pub_key_morgan ) != pub_keys.end() );
      BOOST_CHECK( pub_keys.find( pub_key_nathan ) != pub_keys.end() );

      // sign with o, but actually need m+n
      sign(trx, oliver_key);

      // provides [], should return []
      pub_keys = db_api.get_required_signatures( trx, avail_keys_empty );
      BOOST_CHECK( pub_keys.size() == 0 );

      // provides [m], should return [m]
      pub_keys = db_api.get_required_signatures( trx, avail_keys_m );
      BOOST_CHECK( pub_keys.size() == 1 );
      BOOST_CHECK( pub_keys.find( pub_key_morgan ) != pub_keys.end() );

      // provides [n], should return [n]
      pub_keys = db_api.get_required_signatures( trx, avail_keys_n );
      BOOST_CHECK( pub_keys.size() == 1 );
      BOOST_CHECK( pub_keys.find( pub_key_nathan ) != pub_keys.end() );

      // provides [o], should return []
      pub_keys = db_api.get_required_signatures( trx, avail_keys_o );
      BOOST_CHECK( pub_keys.size() == 0 );

      // provides [m,n], should return [m,n]
      pub_keys = db_api.get_required_signatures( trx, avail_keys_mn );
      BOOST_CHECK( pub_keys.size() == 2 );
      BOOST_CHECK( pub_keys.find( pub_key_morgan ) != pub_keys.end() );
      BOOST_CHECK( pub_keys.find( pub_key_nathan ) != pub_keys.end() );

      // provides [m,o], should return [m]
      pub_keys = db_api.get_required_signatures( trx, avail_keys_mo );
      BOOST_CHECK( pub_keys.size() == 1 );
      BOOST_CHECK( pub_keys.find( pub_key_morgan ) != pub_keys.end() );

      // provides [n,o], should return [n]
      pub_keys = db_api.get_required_signatures( trx, avail_keys_no );
      BOOST_CHECK( pub_keys.size() == 1 );
      BOOST_CHECK( pub_keys.find( pub_key_nathan ) != pub_keys.end() );

      // provides [m,n,o], should return [m,n]
      pub_keys = db_api.get_required_signatures( trx, avail_keys_mno );
      BOOST_CHECK( pub_keys.size() == 2 );
      BOOST_CHECK( pub_keys.find( pub_key_morgan ) != pub_keys.end() );
      BOOST_CHECK( pub_keys.find( pub_key_nathan ) != pub_keys.end() );

      // sign with m+o, but actually need m+n
      sign(trx, morgan_key);

      // provides [], should return []
      pub_keys = db_api.get_required_signatures( trx, avail_keys_empty );
      BOOST_CHECK( pub_keys.size() == 0 );

      // provides [m], should return []
      pub_keys = db_api.get_required_signatures( trx, avail_keys_m );
      BOOST_CHECK( pub_keys.size() == 0 );

      // provides [n], should return [n]
      pub_keys = db_api.get_required_signatures( trx, avail_keys_n );
      BOOST_CHECK( pub_keys.size() == 1 );
      BOOST_CHECK( pub_keys.find( pub_key_nathan ) != pub_keys.end() );

      // provides [o], should return []
      pub_keys = db_api.get_required_signatures( trx, avail_keys_o );
      BOOST_CHECK( pub_keys.size() == 0 );

      // provides [m,n], should return [n]
      pub_keys = db_api.get_required_signatures( trx, avail_keys_mn );
      BOOST_CHECK( pub_keys.size() == 1 );
      BOOST_CHECK( pub_keys.find( pub_key_nathan ) != pub_keys.end() );

      // provides [m,o], should return []
      pub_keys = db_api.get_required_signatures( trx, avail_keys_mo );
      BOOST_CHECK( pub_keys.size() == 0 );

      // provides [n,o], should return [n]
      pub_keys = db_api.get_required_signatures( trx, avail_keys_no );
      BOOST_CHECK( pub_keys.size() == 1 );
      BOOST_CHECK( pub_keys.find( pub_key_nathan ) != pub_keys.end() );

      // provides [m,n,o], should return [n]
      pub_keys = db_api.get_required_signatures( trx, avail_keys_mno );
      BOOST_CHECK( pub_keys.size() == 1 );
      BOOST_CHECK( pub_keys.find( pub_key_nathan ) != pub_keys.end() );

      // sign with m, but actually need m+n
      trx.clear_signatures();
      sign(trx, morgan_key);

      // provides [], should return []
      pub_keys = db_api.get_required_signatures( trx, avail_keys_empty );
      BOOST_CHECK( pub_keys.size() == 0 );

      // provides [m], should return []
      pub_keys = db_api.get_required_signatures( trx, avail_keys_m );
      BOOST_CHECK( pub_keys.size() == 0 );

      // provides [n], should return [n]
      pub_keys = db_api.get_required_signatures( trx, avail_keys_n );
      BOOST_CHECK( pub_keys.size() == 1 );
      BOOST_CHECK( pub_keys.find( pub_key_nathan ) != pub_keys.end() );

      // provides [o], should return []
      pub_keys = db_api.get_required_signatures( trx, avail_keys_o );
      BOOST_CHECK( pub_keys.size() == 0 );

      // provides [m,n], should return [n]
      pub_keys = db_api.get_required_signatures( trx, avail_keys_mn );
      BOOST_CHECK( pub_keys.size() == 1 );
      BOOST_CHECK( pub_keys.find( pub_key_nathan ) != pub_keys.end() );

      // provides [m,o], should return []
      pub_keys = db_api.get_required_signatures( trx, avail_keys_mo );
      BOOST_CHECK( pub_keys.size() == 0 );

      // provides [n,o], should return [n]
      pub_keys = db_api.get_required_signatures( trx, avail_keys_no );
      BOOST_CHECK( pub_keys.size() == 1 );
      BOOST_CHECK( pub_keys.find( pub_key_nathan ) != pub_keys.end() );

      // provides [m,n,o], should return [n]
      pub_keys = db_api.get_required_signatures( trx, avail_keys_mno );
      BOOST_CHECK( pub_keys.size() == 1 );
      BOOST_CHECK( pub_keys.find( pub_key_nathan ) != pub_keys.end() );

      // sign with m+n, should be enough
      sign(trx, nathan_key);

      // provides [], should return []
      pub_keys = db_api.get_required_signatures( trx, avail_keys_empty );
      BOOST_CHECK( pub_keys.size() == 0 );

      // provides [m], should return []
      pub_keys = db_api.get_required_signatures( trx, avail_keys_m );
      BOOST_CHECK( pub_keys.size() == 0 );

      // provides [n], should return []
      pub_keys = db_api.get_required_signatures( trx, avail_keys_n );
      BOOST_CHECK( pub_keys.size() == 0 );

      // provides [o], should return []
      pub_keys = db_api.get_required_signatures( trx, avail_keys_o );
      BOOST_CHECK( pub_keys.size() == 0 );

      // provides [m,n], should return []
      pub_keys = db_api.get_required_signatures( trx, avail_keys_mn );
      BOOST_CHECK( pub_keys.size() == 0 );

      // provides [m,o], should return []
      pub_keys = db_api.get_required_signatures( trx, avail_keys_mo );
      BOOST_CHECK( pub_keys.size() == 0 );

      // provides [n,o], should return []
      pub_keys = db_api.get_required_signatures( trx, avail_keys_no );
      BOOST_CHECK( pub_keys.size() == 0 );

      // provides [m,n,o], should return []
      pub_keys = db_api.get_required_signatures( trx, avail_keys_mno );
      BOOST_CHECK( pub_keys.size() == 0 );

      // sign with m+n+o, should be enough as well
      sign(trx, oliver_key);

      // provides [], should return []
      pub_keys = db_api.get_required_signatures( trx, avail_keys_empty );
      BOOST_CHECK( pub_keys.size() == 0 );

      // provides [m], should return []
      pub_keys = db_api.get_required_signatures( trx, avail_keys_m );
      BOOST_CHECK( pub_keys.size() == 0 );

      // provides [n], should return []
      pub_keys = db_api.get_required_signatures( trx, avail_keys_n );
      BOOST_CHECK( pub_keys.size() == 0 );

      // provides [o], should return []
      pub_keys = db_api.get_required_signatures( trx, avail_keys_o );
      BOOST_CHECK( pub_keys.size() == 0 );

      // provides [m,n], should return []
      pub_keys = db_api.get_required_signatures( trx, avail_keys_mn );
      BOOST_CHECK( pub_keys.size() == 0 );

      // provides [m,o], should return []
      pub_keys = db_api.get_required_signatures( trx, avail_keys_mo );
      BOOST_CHECK( pub_keys.size() == 0 );

      // provides [n,o], should return []
      pub_keys = db_api.get_required_signatures( trx, avail_keys_no );
      BOOST_CHECK( pub_keys.size() == 0 );

      // provides [m,n,o], should return []
      pub_keys = db_api.get_required_signatures( trx, avail_keys_mno );
      BOOST_CHECK( pub_keys.size() == 0 );

   } FC_LOG_AND_RETHROW()
}

BOOST_AUTO_TEST_CASE( subscription_key_collision_test )
{
   object_id_type uia_object_id = create_user_issued_asset( "UIATEST" ).get_id();

   uint32_t objects_changed = 0;
   auto callback = [&]( const variant& v )
   {
      ++objects_changed;
   };

   graphene::app::database_api db_api(db);
   db_api.set_subscribe_callback( callback, false );

   // subscribe to an account which has same instance ID as UIATEST
   vector<string> collision_ids;
   collision_ids.push_back( string( object_id_type( account_id_type( uia_object_id ) ) ) );
   db_api.get_accounts( collision_ids );

   generate_block();
   fc::usleep(fc::milliseconds(200)); // sleep a while to execute callback in another thread

   BOOST_CHECK_EQUAL( objects_changed, 0 ); // did not subscribe to UIATEST, so no notification

   vector<string> asset_names;
   asset_names.push_back( "UIATEST" );
   db_api.get_assets( asset_names );

   generate_block();
   fc::usleep(fc::milliseconds(200)); // sleep a while to execute callback in another thread

   BOOST_CHECK_EQUAL( objects_changed, 0 ); // UIATEST did not change in this block, so no notification
}

BOOST_AUTO_TEST_CASE( subscription_notification_test )
{
   try {

      generate_blocks(HARDFORK_CORE_1468_TIME);
      set_expiration( db, trx );
      set_htlc_committee_parameters();
      generate_block();
      set_expiration( db, trx );

      ACTORS( (alice)(bob) );

      create_user_issued_asset( "UIATEST" );

      // prepare data for get_htlc
      {
         int64_t init_balance(100 * GRAPHENE_BLOCKCHAIN_PRECISION);
         transfer( committee_account, alice_id, graphene::chain::asset(init_balance) );

         uint16_t preimage_size = 256;
         std::vector<char> pre_image(256);
         std::independent_bits_engine<std::default_random_engine, sizeof(unsigned), unsigned int> rbe;
         std::generate(begin(pre_image), end(pre_image), std::ref(rbe));

         // alice puts a htlc contract to bob
         graphene::chain::htlc_create_operation create_operation;
         BOOST_TEST_MESSAGE("Alice, who has 100 coins, is transferring 3 coins to Bob");
         create_operation.amount = graphene::chain::asset( 3 * GRAPHENE_BLOCKCHAIN_PRECISION );
         create_operation.to = bob_id;
         create_operation.claim_period_seconds = 60;
         create_operation.preimage_hash = hash_it<fc::sha256>( pre_image );
         create_operation.preimage_size = preimage_size;
         create_operation.from = alice_id;
         create_operation.fee = db.get_global_properties().parameters.current_fees->calculate_fee(create_operation);
         trx.operations.push_back(create_operation);
         sign(trx, alice_private_key);
         PUSH_TX(db, trx, ~0);
         trx.clear();
      }

// declare db_api1 ~ db_api60
#define SUB_NOTIF_TEST_NUM_CALLBACKS_PLUS_ONE 61
// db_api1  ~ db_api30 auto-subscribe in the beginning,
// db_api31 ~ db_api60 don't auto-subscribe
#define SUB_NOTIF_TEST_START_ID_DISABLE_AUTO_SUB 31

// create callback functions
#define SUB_NOTIF_TEST_INIT_CALLBACKS(z, i, data) \
      uint32_t objects_changed ## i = 0; \
      auto callback ## i = [&]( const variant& v ) \
      { \
         idump((i)(v)); \
         ++objects_changed ## i; \
      }; \
      uint32_t expected_objects_changed ## i = 0;

// create function to check results
#define SUB_NOTIF_TEST_CHECK(z, i, data) \
      if( expected_objects_changed ## i > 0 ) { \
         BOOST_CHECK_LE( expected_objects_changed ## i, objects_changed ## i ); \
      } else { \
         BOOST_CHECK_EQUAL( expected_objects_changed ## i, objects_changed ## i ); \
      } \
      expected_objects_changed ## i = 0; \
      objects_changed ## i = 0;

      BOOST_PP_REPEAT_FROM_TO( 1, SUB_NOTIF_TEST_NUM_CALLBACKS_PLUS_ONE, SUB_NOTIF_TEST_INIT_CALLBACKS, unused );

      auto check_results = [&]()
      {
         BOOST_PP_REPEAT_FROM_TO( 1, SUB_NOTIF_TEST_NUM_CALLBACKS_PLUS_ONE, SUB_NOTIF_TEST_CHECK, unused );
      };

#undef SUB_NOTIF_TEST_CHECK
#undef SUB_NOTIF_TEST_INIT_CALLBACKS

      graphene::app::database_api db_api1(db);

      // subscribing to all should fail
      BOOST_CHECK_THROW( db_api1.set_subscribe_callback( callback1, true ), fc::exception );

      db_api1.set_subscribe_callback( callback1, false );

      graphene::app::application_options opt;
      opt.enable_subscribe_to_all = true;

      graphene::app::database_api db_api2( db, &opt );
      db_api2.set_subscribe_callback( callback2, true ); // subscribing to all should succeed

// declare the rest of API callers and initialize callbacks
#define SUB_NOTIF_TEST_INIT_APIS(z, i, data) \
      graphene::app::database_api db_api ## i( db, &opt ); \
      db_api ## i.set_subscribe_callback( callback ## i, false );

      BOOST_PP_REPEAT_FROM_TO( 3, SUB_NOTIF_TEST_NUM_CALLBACKS_PLUS_ONE, SUB_NOTIF_TEST_INIT_APIS, unused );

#undef SUB_NOTIF_TEST_INIT_APIS

// disable auto-subscription for some API callers
#define SUB_NOTIF_TEST_DISABLE_AUTO_SUB(z, i, data) \
      db_api ## i.set_auto_subscription( false );

      BOOST_PP_REPEAT_FROM_TO( SUB_NOTIF_TEST_START_ID_DISABLE_AUTO_SUB, SUB_NOTIF_TEST_NUM_CALLBACKS_PLUS_ONE,
                               SUB_NOTIF_TEST_DISABLE_AUTO_SUB, unused );

#undef SUB_NOTIF_TEST_DISABLE_AUTO_SUB
#undef SUB_NOTIF_TEST_NUM_CALLBACKS_PLUS_ONE
#undef SUB_NOTIF_TEST_START_ID_DISABLE_AUTO_SUB

      vector<object_id_type> account_ids;
      account_ids.push_back( alice_id );
      db_api1.get_objects( account_ids );         // db_api1  subscribe to Alice
      db_api11.get_objects( account_ids, true );  // db_api11 subscribe to Alice
      db_api21.get_objects( account_ids, false ); // db_api21 doesn't subscribe to Alice
      db_api31.get_objects( account_ids );        // db_api31 doesn't subscribe to Alice
      db_api41.get_objects( account_ids, true );  // db_api41 subscribe to Alice
      db_api51.get_objects( account_ids, false ); // db_api51 doesn't subscribe to Alice

      vector<string> account_names;
      account_names.push_back( "alice" );
      db_api4.get_accounts( account_names );         // db_api4  subscribe to Alice
      db_api14.get_accounts( account_names, true );  // db_api14 subscribe to Alice
      db_api24.get_accounts( account_names, false ); // db_api24 doesn't subscribe to Alice
      db_api34.get_accounts( account_names );        // db_api34 doesn't subscribe to Alice
      db_api44.get_accounts( account_names, true );  // db_api44 subscribe to Alice
      db_api54.get_accounts( account_names, false ); // db_api54 doesn't subscribe to Alice

      db_api5.lookup_accounts( "ali", 1 );         // db_api5  subscribe to Alice
      db_api15.lookup_accounts( "ali", 1, true );  // db_api15 subscribe to Alice
      db_api25.lookup_accounts( "ali", 1, false ); // db_api25 doesn't subscribe to Alice
      db_api35.lookup_accounts( "ali", 1 );        // db_api35 doesn't subscribe to Alice
      db_api45.lookup_accounts( "ali", 1, true );  // db_api45 subscribe to Alice
      db_api55.lookup_accounts( "ali", 1, false ); // db_api55 doesn't subscribe to Alice

      db_api6.lookup_accounts( "alice", 3 );         // db_api6  does not subscribe to Alice
      db_api16.lookup_accounts( "alice", 3, true );  // db_api16 does not subscribe to Alice
      db_api26.lookup_accounts( "alice", 3, false ); // db_api26 does not subscribe to Alice
      db_api36.lookup_accounts( "alice", 3 );        // db_api36 does not subscribe to Alice
      db_api46.lookup_accounts( "alice", 3, true );  // db_api46 does not subscribe to Alice
      db_api56.lookup_accounts( "alice", 3, false ); // db_api56 does not subscribe to Alice

      vector<string> asset_names;
      asset_names.push_back( "UIATEST" );
      db_api7.get_assets( asset_names );         // db_api7  subscribe to UIA
      db_api17.get_assets( asset_names, true );  // db_api17 subscribe to UIA
      db_api27.get_assets( asset_names, false ); // db_api27 doesn't subscribe to UIA
      db_api37.get_assets( asset_names );        // db_api37 doesn't subscribe to UIA
      db_api47.get_assets( asset_names, true );  // db_api47 subscribe to UIA
      db_api57.get_assets( asset_names, false ); // db_api57 doesn't subscribe to UIA

      graphene::chain::htlc_id_type alice_htlc_id_bob; // assuming ID of the first htlc object is 0
      db_api8.get_htlc( alice_htlc_id_bob );         // db_api8  subscribe to the HTLC object
      db_api18.get_htlc( alice_htlc_id_bob, true );  // db_api18 subscribe to the HTLC object
      db_api28.get_htlc( alice_htlc_id_bob, false ); // db_api28 doesn't subscribe to the HTLC object
      db_api38.get_htlc( alice_htlc_id_bob );        // db_api38 doesn't subscribe to the HTLC object
      db_api48.get_htlc( alice_htlc_id_bob, true );  // db_api48 subscribe to the HTLC object
      db_api58.get_htlc( alice_htlc_id_bob, false ); // db_api58 doesn't subscribe to the HTLC object

      generate_block();
      ++expected_objects_changed1; // db_api1 subscribed to Alice, notify Alice account creation
      ++expected_objects_changed11; // db_api11 subscribed to Alice, notify Alice account creation
      ++expected_objects_changed41; // db_api41 subscribed to Alice, notify Alice account creation
      ++expected_objects_changed2; // db_api2 subscribed to all, notify new objects
      // db_api3 didn't subscribe to anything, nothing would be notified
      ++expected_objects_changed4; // db_api4 subscribed to Alice, notify Alice account creation
      ++expected_objects_changed14; // db_api14 subscribed to Alice, notify Alice account creation
      ++expected_objects_changed44; // db_api44 subscribed to Alice, notify Alice account creation
      ++expected_objects_changed5; // db_api5 subscribed to Alice, notify Alice account creation
      ++expected_objects_changed15; // db_api15 subscribed to Alice, notify Alice account creation
      ++expected_objects_changed45; // db_api45 subscribed to Alice, notify Alice account creation
      // db_api*6 didn't subscribe to anything, nothing would be notified
      ++expected_objects_changed7; // db_api7 subscribed to UIA, notify asset creation
      ++expected_objects_changed17; // db_api17 subscribed to UIA, notify asset creation
      ++expected_objects_changed47; // db_api47 subscribed to UIA, notify asset creation
      ++expected_objects_changed8; // db_api8 subscribed to HTLC object, notify object creation
      ++expected_objects_changed18; // db_api18 subscribed to HTLC object, notify object creation
      ++expected_objects_changed48; // db_api48 subscribed to HTLC object, notify object creation

      fc::usleep(fc::milliseconds(200)); // sleep a while to execute callback in another thread
      check_results();

      transfer( account_id_type(), alice_id, asset(1) );
      generate_block();
      // db_api1 didn't subscribe to Alice with get_full_accounts but only subscribed to the account object,
      //   nothing would be notified
      ++expected_objects_changed2; // db_api2 subscribed to all, notify new balance object and etc
      // db_api3 didn't subscribe to anything, nothing would be notified
      // db_api4 only subscribed to the account object of Alice, nothing notified
      // db_api5 only subscribed to the account object of Alice, nothing notified
      // db_api6 didn't subscribe to anything, nothing would be notified
      // db_api7: no change on UIA, nothing would be notified

      fc::usleep(fc::milliseconds(200)); // sleep a while to execute callback in another thread
      check_results();

      vector<object_id_type> obj_ids;
      obj_ids.push_back( db.get_dynamic_global_properties().id );
      db_api3.get_objects( obj_ids ); // db_api3 subscribe to dynamic global properties

      db_api4.get_full_accounts( account_names, true );   // db_api4 subscribe to Alice with get_full_accounts
      db_api14.get_full_accounts( account_names, false ); // db_api14 doesn't subscribe
      db_api24.get_full_accounts( account_names );        // db_api24 subscribe to Alice with get_full_accounts
      db_api34.get_full_accounts( account_names, true );  // db_api34 subscribe to Alice with get_full_accounts
      db_api44.get_full_accounts( account_names, false ); // db_api44 doesn't subscribe
      db_api54.get_full_accounts( account_names );        // db_api54 doesn't subscribe

      db_api5.get_full_accounts( account_names, false ); // db_api5 doesn't subscribe

      transfer( account_id_type(), alice_id, asset(1) );
      generate_block();
      // db_api1 didn't subscribe to Alice with get_full_accounts but only subscribed to the account object,
      //   nothing would be notified
      ++expected_objects_changed2; // db_api2 subscribed to all, notify new history records and etc
      ++expected_objects_changed3; // db_api3 subscribed to dynamic global properties, would be notified
      ++expected_objects_changed4; // db_api4 subscribed to full account data of Alice, would be notified
      ++expected_objects_changed24; // db_api24 subscribed to full account data of Alice, would be notified
      ++expected_objects_changed34; // db_api34 subscribed to full account data of Alice, would be notified
      // db_api5 only subscribed to the account object of Alice, nothing notified
      // db_api6 didn't subscribe to anything, nothing would be notified
      // db_api7: no change on UIA, nothing would be notified

      fc::usleep(fc::milliseconds(200)); // sleep a while to execute callback in another thread
      check_results();

      db_api6.set_auto_subscription( false );
      db_api6.get_objects( obj_ids ); // db_api6 doesn't auto-subscribe to dynamic global properties

      generate_block();
      // db_api1 only subscribed to the account object of Alice, nothing notified
      // db_api2 subscribed to all, but no object is created or removed in this block, so nothing notified
      ++expected_objects_changed3; // db_api3 subscribed to dynamic global properties, would be notified
      // db_api4 subscribed to full account data of Alice, nothing would be notified
      // db_api5 only subscribed to the account object of Alice, nothing notified
      // db_api6 didn't subscribe to anything, nothing would be notified
      // db_api7: no change on UIA, nothing would be notified

      fc::usleep(fc::milliseconds(200)); // sleep a while to execute callback in another thread
      check_results();

      account_names.clear();
      account_names.push_back( "bob" );
      db_api5.set_auto_subscription( false );
      db_api5.get_full_accounts( account_names, true ); // db_api5 subscribe to full account data of Bob

      db_api6.get_full_accounts( account_names, false ); // db_api6 doesn't subscribe

      transfer( account_id_type(), bob_id, asset(1) );

      generate_block();
      // db_api1 only subscribed to the account object of Alice, nothing notified
      ++expected_objects_changed2; // db_api2 subscribed to all, notify new history records and etc
      ++expected_objects_changed3; // db_api3 subscribed to dynamic global properties, would be notified
      // db_api4 subscribed to full account data of Alice, nothing would be notified
      ++expected_objects_changed5; // db_api5 subscribed to full account data of Bob, would be notified
      // db_api6 didn't subscribe to anything, nothing would be notified
      // db_api7: no change on UIA, nothing would be notified

      fc::usleep(fc::milliseconds(200)); // sleep a while to execute callback in another thread
      check_results();

      db_api6.set_auto_subscription( true );
      db_api6.get_objects( obj_ids ); // db_api6 auto-subscribe to dynamic global properties

      generate_block();
      // db_api1 only subscribed to the account object of Alice, nothing notified
      // db_api2 subscribed to all, but no object is created or removed in this block, so nothing notified
      ++expected_objects_changed3; // db_api3 subscribed to dynamic global properties, would be notified
      // db_api4 subscribed to full account data of Alice, nothing would be notified
      // db_api5 subscribed to full account data of Bob, nothing notified
      ++expected_objects_changed6; // db_api6 subscribed to dynamic global properties, would be notified
      // db_api7: no change on UIA, nothing would be notified

      fc::usleep(fc::milliseconds(200)); // sleep a while to execute callback in another thread
      check_results();

      db_api5.set_subscribe_callback( callback5, false ); // reset subscription

      db_api6.cancel_all_subscriptions();
      db_api6.get_objects( obj_ids ); // db_api6 doesn't auto-subscribe to dynamic global properties

      transfer( alice_id, bob_id, asset(1) );

      generate_block();
      // db_api1 only subscribed to the account object of Alice, nothing notified
      ++expected_objects_changed2; // db_api2 subscribed to all, notify new history records and etc
      ++expected_objects_changed3; // db_api3 subscribed to dynamic global properties, would be notified
      ++expected_objects_changed4; // db_api4 subscribed to full account data of Alice, would be notified
      ++expected_objects_changed24; // db_api24 subscribed to full account data of Alice, would be notified
      ++expected_objects_changed34; // db_api34 subscribed to full account data of Alice, would be notified
      // db_api5 subscribed to anything, nothing notified
      // db_api6 subscribed to anything, nothing notified
      // db_api7: no change on UIA, nothing would be notified

      fc::usleep(fc::milliseconds(200)); // sleep a while to execute callback in another thread
      check_results();

   } FC_LOG_AND_RETHROW()
}

BOOST_AUTO_TEST_CASE( get_all_workers )
{ try {
   graphene::app::database_api db_api( db, &( app.get_options() ));
   ACTORS( (connie)(whitney)(wolverine) );

   fund(connie);
   upgrade_to_lifetime_member(connie);
   fund(whitney);
   upgrade_to_lifetime_member(whitney);
   fund(wolverine);
   upgrade_to_lifetime_member(wolverine);

   vector<worker_object> results;

   const auto& worker1 = create_worker( connie_id, 1000, fc::days(10) );
   worker_id_type worker1_id = worker1.id;

   BOOST_REQUIRE_EQUAL( db_api.get_all_workers().size(), 1 );
   BOOST_REQUIRE_EQUAL( db_api.get_all_workers(true).size(), 0 );
   BOOST_REQUIRE_EQUAL( db_api.get_all_workers(false).size(), 1 );
   BOOST_CHECK( db_api.get_all_workers().front().id == worker1_id );
   BOOST_CHECK( db_api.get_all_workers(false).front().id == worker1_id );

   generate_blocks( db.head_block_time() + fc::days(11) );
   set_expiration( db, trx );

   BOOST_REQUIRE_EQUAL( db_api.get_all_workers().size(), 1 );
   BOOST_REQUIRE_EQUAL( db_api.get_all_workers(true).size(), 1 );
   BOOST_REQUIRE_EQUAL( db_api.get_all_workers(false).size(), 0 );
   BOOST_CHECK( db_api.get_all_workers().front().id == worker1_id );
   BOOST_CHECK( db_api.get_all_workers(true).front().id == worker1_id );

   const auto& worker2 = create_worker( whitney_id, 1000, fc::days(50) );
   worker_id_type worker2_id = worker2.id;

   BOOST_REQUIRE_EQUAL( db_api.get_all_workers().size(), 2 );
   BOOST_REQUIRE_EQUAL( db_api.get_all_workers(true).size(), 1 );
   BOOST_REQUIRE_EQUAL( db_api.get_all_workers(false).size(), 1 );
   BOOST_CHECK( db_api.get_all_workers().front().id == worker1_id );
   BOOST_CHECK( db_api.get_all_workers().back().id == worker2_id );
   BOOST_CHECK( db_api.get_all_workers(true).front().id == worker1_id );
   BOOST_CHECK( db_api.get_all_workers(false).front().id == worker2_id );

   const auto& worker3 = create_worker( wolverine_id, 1000, fc::days(100) );
   worker_id_type worker3_id = worker3.id;

   BOOST_REQUIRE_EQUAL( db_api.get_all_workers().size(), 3 );
   BOOST_REQUIRE_EQUAL( db_api.get_all_workers(true).size(), 1 );
   BOOST_REQUIRE_EQUAL( db_api.get_all_workers(false).size(), 2 );
   BOOST_CHECK( db_api.get_all_workers().front().id == worker1_id );
   BOOST_CHECK( db_api.get_all_workers().back().id == worker3_id );
   BOOST_CHECK( db_api.get_all_workers(true).front().id == worker1_id );
   BOOST_CHECK( db_api.get_all_workers(false).front().id == worker2_id );
   BOOST_CHECK( db_api.get_all_workers(false).back().id == worker3_id );

   generate_blocks( db.head_block_time() + fc::days(55) );
   set_expiration( db, trx );

   BOOST_REQUIRE_EQUAL( db_api.get_all_workers().size(), 3 );
   BOOST_REQUIRE_EQUAL( db_api.get_all_workers(true).size(), 2 );
   BOOST_REQUIRE_EQUAL( db_api.get_all_workers(false).size(), 1 );
   BOOST_CHECK( db_api.get_all_workers().front().id == worker1_id );
   BOOST_CHECK( db_api.get_all_workers().back().id == worker3_id );
   BOOST_CHECK( db_api.get_all_workers(true).front().id == worker1_id );
   BOOST_CHECK( db_api.get_all_workers(true).back().id == worker2_id );
   BOOST_CHECK( db_api.get_all_workers(false).front().id == worker3_id );

   generate_blocks( db.head_block_time() + fc::days(55) );
   set_expiration( db, trx );

   BOOST_REQUIRE_EQUAL( db_api.get_all_workers().size(), 3 );
   BOOST_REQUIRE_EQUAL( db_api.get_all_workers(true).size(), 3 );
   BOOST_REQUIRE_EQUAL( db_api.get_all_workers(false).size(), 0 );
   BOOST_CHECK( db_api.get_all_workers().front().id == worker1_id );
   BOOST_CHECK( db_api.get_all_workers().back().id == worker3_id );
   BOOST_CHECK( db_api.get_all_workers(true).front().id == worker1_id );
   BOOST_CHECK( db_api.get_all_workers(true).back().id == worker3_id );

} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE( get_workers_by_account )
{ try {
   graphene::app::database_api db_api( db, &( app.get_options() ));
   ACTORS( (connie)(whitney)(wolverine) );

   fund(connie);
   upgrade_to_lifetime_member(connie);
   fund(whitney);
   upgrade_to_lifetime_member(whitney);
   fund(wolverine);
   upgrade_to_lifetime_member(wolverine);

   vector<worker_object> results;

   const auto& worker1 = create_worker( connie_id );
   worker_id_type worker1_id = worker1.id;

   const auto& worker2 = create_worker( whitney_id, 1000, fc::days(50) );
   worker_id_type worker2_id = worker2.id;

   const auto& worker3 = create_worker( whitney_id, 1000, fc::days(100) );
   worker_id_type worker3_id = worker3.id;

   BOOST_REQUIRE_EQUAL( db_api.get_workers_by_account("connie").size(), 1 );
   BOOST_CHECK( db_api.get_workers_by_account("connie").front().id == worker1_id );

   BOOST_REQUIRE_EQUAL( db_api.get_workers_by_account(string(whitney.id)).size(), 2 );
   BOOST_CHECK( db_api.get_workers_by_account(string(whitney.id)).front().id == worker2_id );
   BOOST_CHECK( db_api.get_workers_by_account(string(whitney.id)).back().id == worker3_id );

   BOOST_REQUIRE_EQUAL( db_api.get_workers_by_account("wolverine").size(), 0 );

   BOOST_REQUIRE_THROW( db_api.get_workers_by_account("not-a-user"), fc::exception );

} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE( lookup_vote_ids )
{ try {
   graphene::app::database_api db_api( db, &( app.get_options() ));
   ACTORS( (connie)(whitney)(wolverine) );

   fund(connie);
   upgrade_to_lifetime_member(connie);
   fund(whitney);
   upgrade_to_lifetime_member(whitney);
   fund(wolverine);
   upgrade_to_lifetime_member(wolverine);

   const auto& committee = create_committee_member( connie );
   const auto& witness = create_witness( whitney );
   const auto& worker = create_worker( wolverine_id );

   std::vector<vote_id_type> votes;
   votes.push_back( committee.vote_id );
   votes.push_back( witness.vote_id );
   votes.push_back( worker.vote_for );

   const auto results = db_api.lookup_vote_ids( votes );

} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE(get_limit_orders_by_account)
{ try {
   graphene::app::database_api db_api( db, &( app.get_options() ));
   ACTORS((seller)(buyer)(watcher));

   const auto& bitcny = create_user_issued_asset("CNY");
   const auto& core   = asset_id_type()(db);

   int64_t init_balance(10000000);
   transfer( committee_account, seller_id, asset(init_balance) );
   issue_uia( buyer_id, bitcny.amount(init_balance) );
   BOOST_CHECK_EQUAL( 10000000, get_balance(seller, core) );
   BOOST_CHECK_EQUAL( 10000000, get_balance(buyer, bitcny) );

   std::vector<limit_order_object> results, results2;
   limit_order_object o;

   // limit too large
   BOOST_CHECK_THROW( db_api.get_limit_orders_by_account( seller.name, 102 ), fc::exception );

   // The order book is empty
   results = db_api.get_limit_orders_by_account( seller.name );
   BOOST_CHECK_EQUAL( results.size(), 0 );

   // Seller create 50 orders
   for (size_t i = 0 ; i < 50 ; ++i)
   {
      BOOST_CHECK(create_sell_order(seller, core.amount(100), bitcny.amount(250)));
   }

   // Get all orders
   results = db_api.get_limit_orders_by_account( seller.name );
   BOOST_CHECK_EQUAL( results.size(), 50 );

   // Seller create 200 orders
   for (size_t i = 1 ; i < 101 ; ++i)
   {
      BOOST_CHECK(create_sell_order(seller, core.amount(100), bitcny.amount(250 + i)));
      BOOST_CHECK(create_sell_order(seller, core.amount(100), bitcny.amount(250 - i)));
   }

   // Buyer create 20 orders
   for (size_t i = 0 ; i < 70 ; ++i)
   {
      BOOST_CHECK(create_sell_order(buyer, bitcny.amount(100), core.amount(5000 + i)));
   }

   // Get the first 101 orders
   results = db_api.get_limit_orders_by_account( seller.name );
   BOOST_CHECK_EQUAL( results.size(), 101 );
   for (size_t i = 0 ; i < results.size() - 1 ; ++i)
   {
      BOOST_CHECK(results[i].id < results[i+1].id);
   }
   BOOST_CHECK(results.front().sell_price == price(core.amount(100), bitcny.amount(250)));
   BOOST_CHECK(results.back().sell_price == price(core.amount(100), bitcny.amount(276)));
   o = results.back();

   // Get the No. 101-201 orders
   results = db_api.get_limit_orders_by_account( seller.name, {}, o.id );
   BOOST_CHECK_EQUAL( results.size(), 101 );
   for (size_t i = 0 ; i < results.size() - 1 ; ++i)
   {
      BOOST_CHECK(results[i].id < results[i+1].id);
   }
   BOOST_CHECK(results.front().sell_price == price(core.amount(100), bitcny.amount(276)));
   BOOST_CHECK(results.back().sell_price == price(core.amount(100), bitcny.amount(326)));
   o = results.back();

   // Get the No. 201- orders
   results = db_api.get_limit_orders_by_account( seller.name, {}, o.id );
   BOOST_CHECK_EQUAL( results.size(), 50 );
   for (size_t i = 0 ; i < results.size() - 1 ; ++i)
   {
      BOOST_CHECK(results[i].id < results[i+1].id);
   }
   BOOST_CHECK(results.front().sell_price == price(core.amount(100), bitcny.amount(326)));
   BOOST_CHECK(results.back().sell_price == price(core.amount(100), bitcny.amount(150)));

   // Get the No. 201-210 orders
   results2 = db_api.get_limit_orders_by_account( seller.name, 10, o.id );
   BOOST_CHECK_EQUAL( results2.size(), 10 );
   for (size_t i = 0 ; i < results2.size() - 1 ; ++i)
   {
      BOOST_CHECK(results2[i].id < results2[i+1].id);
      BOOST_CHECK(results[i].id == results2[i].id);
   }
   BOOST_CHECK(results2.front().sell_price == price(core.amount(100), bitcny.amount(326)));
   BOOST_CHECK(results2.back().sell_price == price(core.amount(100), bitcny.amount(170)));

   // Buyer has 70 orders, all IDs are greater than sellers
   results = db_api.get_limit_orders_by_account( buyer.name, 90, o.id );
   BOOST_CHECK_EQUAL( results.size(), 70 );
   o = results.back();

   // All seller's order IDs are smaller, so querying with a buyer's ID will get nothing
   results = db_api.get_limit_orders_by_account( seller.name, 90, o.id );
   BOOST_CHECK_EQUAL( results.size(), 0 );

   // Watcher has no order
   results = db_api.get_limit_orders_by_account( watcher.name );
   BOOST_CHECK_EQUAL( results.size(), 0 );

   // unregistered account, throws exception
   BOOST_CHECK_THROW( db_api.get_limit_orders_by_account( "not-a-user", 10, limit_order_id_type() ),
                      fc::exception );

} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE(get_account_limit_orders)
{ try {
   graphene::app::database_api db_api( db, &( app.get_options() ));
   ACTORS((seller));

   const auto& bitcny = create_bitasset("CNY");
   const auto& core   = asset_id_type()(db);

   int64_t init_balance(10000000);
   transfer(committee_account, seller_id, asset(init_balance));
   BOOST_CHECK_EQUAL( 10000000, get_balance(seller, core) );

   /// Create 250 versatile orders
   for (size_t i = 0 ; i < 50 ; ++i)
   {
      BOOST_CHECK(create_sell_order(seller, core.amount(100), bitcny.amount(250)));
   }

   for (size_t i = 1 ; i < 101 ; ++i)
   {
      BOOST_CHECK(create_sell_order(seller, core.amount(100), bitcny.amount(250 + i)));
      BOOST_CHECK(create_sell_order(seller, core.amount(100), bitcny.amount(250 - i)));
   }

   std::vector<limit_order_object> results;
   limit_order_object o;

   // query with no constraint, expected:
   // 1. up to 101 orders returned
   // 2. orders were sorted by price desendingly
   results = db_api.get_account_limit_orders(seller.name, GRAPHENE_SYMBOL, "CNY");
   BOOST_CHECK(results.size() == 101);
   for (size_t i = 0 ; i < results.size() - 1 ; ++i)
   {
      BOOST_CHECK(results[i].sell_price >= results[i+1].sell_price);
   }
   BOOST_CHECK(results.front().sell_price == price(core.amount(100), bitcny.amount(150)));
   BOOST_CHECK(results.back().sell_price == price(core.amount(100), bitcny.amount(250)));
   results.clear();

   // query with specified limit, expected:
   // 1. up to specified amount of orders returned
   // 2. orders were sorted by price desendingly
   results = db_api.get_account_limit_orders(seller.name, GRAPHENE_SYMBOL, "CNY", 50);
   BOOST_CHECK(results.size() == 50);
   for (size_t i = 0 ; i < results.size() - 1 ; ++i)
   {
      BOOST_CHECK(results[i].sell_price >= results[i+1].sell_price);
   }
   BOOST_CHECK(results.front().sell_price == price(core.amount(100), bitcny.amount(150)));
   BOOST_CHECK(results.back().sell_price == price(core.amount(100), bitcny.amount(199)));

   o = results.back();
   results.clear();

   // query with specified order id and limit, expected:
   // same as before, but also the first order's id equal to specified
   results = db_api.get_account_limit_orders(seller.name, GRAPHENE_SYMBOL, "CNY", 80,
       limit_order_id_type(o.id));
   BOOST_CHECK(results.size() == 80);
   BOOST_CHECK(results.front().id == o.id);
   for (size_t i = 0 ; i < results.size() - 1 ; ++i)
   {
      BOOST_CHECK(results[i].sell_price >= results[i+1].sell_price);
   }
   BOOST_CHECK(results.front().sell_price == price(core.amount(100), bitcny.amount(199)));
   BOOST_CHECK(results.back().sell_price == price(core.amount(100), bitcny.amount(250)));

   o = results.back();
   results.clear();

   // query with specified price and an not exists order id, expected:
   // 1. the canceled order should not exists in returned orders and first order's
   //    id should greater than specified
   // 2. returned orders sorted by price desendingly
   // 3. the first order's sell price equal to specified
   cancel_limit_order(o); // NOTE 1: this canceled order was in scope of the
                          // first created 50 orders, so with price 2.5 BTS/CNY
   results = db_api.get_account_limit_orders(seller.name, GRAPHENE_SYMBOL, "CNY", 50,
       limit_order_id_type(o.id), o.sell_price);
   BOOST_CHECK(results.size() == 50);
   BOOST_CHECK(results.front().id > o.id);
   // NOTE 2: because of NOTE 1, here should be equal
   BOOST_CHECK(results.front().sell_price == o.sell_price);
   for (size_t i = 0 ; i < results.size() - 1 ; ++i)
   {
      BOOST_CHECK(results[i].sell_price >= results[i+1].sell_price);
   }
   BOOST_CHECK(results.front().sell_price == price(core.amount(100), bitcny.amount(250)));
   BOOST_CHECK(results.back().sell_price == price(core.amount(100), bitcny.amount(279)));

   o = results.back();
   results.clear();

   cancel_limit_order(o); // NOTE 3: this time the canceled order was in scope
                          // of the lowest price 150 orders
   results = db_api.get_account_limit_orders(seller.name, GRAPHENE_SYMBOL, "CNY", 101,
       limit_order_id_type(o.id), o.sell_price);
   BOOST_CHECK(results.size() == 71);
   BOOST_CHECK(results.front().id > o.id);
   // NOTE 3: because of NOTE 1, here should be little than
   BOOST_CHECK(results.front().sell_price < o.sell_price);
   for (size_t i = 0 ; i < results.size() - 1 ; ++i)
   {
      BOOST_CHECK(results[i].sell_price >= results[i+1].sell_price);
   }
   BOOST_CHECK(results.front().sell_price == price(core.amount(100), bitcny.amount(280)));
   BOOST_CHECK(results.back().sell_price == price(core.amount(100), bitcny.amount(350)));

   BOOST_CHECK_THROW(db_api.get_account_limit_orders(seller.name, GRAPHENE_SYMBOL, "CNY", 101,
               limit_order_id_type(o.id)), fc::exception);

} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE( get_transaction_hex )
{ try {
   graphene::app::database_api db_api(db);
   auto test_private_key = generate_private_key("testaccount");
   public_key_type test_public = test_private_key.get_public_key();

   trx.operations.push_back(make_account("testaccount", test_public));
   trx.validate();

   // case1: not signed, get hex
   std::string hex_str = fc::to_hex( fc::raw::pack( trx ) );

   BOOST_CHECK( db_api.get_transaction_hex( trx ) == hex_str );
   BOOST_CHECK( db_api.get_transaction_hex_without_sig( trx ) + "00" == hex_str );

   // case2: signed, get hex
   sign( trx, test_private_key );
   hex_str = fc::to_hex( fc::raw::pack( trx ) );

   BOOST_CHECK( db_api.get_transaction_hex( trx ) == hex_str );
   BOOST_CHECK( db_api.get_transaction_hex_without_sig( trx ) +
                   fc::to_hex( fc::raw::pack( trx.signatures ) ) == hex_str );

} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE(verify_account_authority)
{
      try {

         ACTORS( (nathan) );
         graphene::app::database_api db_api(db);

         // good keys
         flat_set<public_key_type> public_keys;
         public_keys.emplace(nathan_public_key);
         BOOST_CHECK(db_api.verify_account_authority( "nathan", public_keys));

         // bad keys
         flat_set<public_key_type> bad_public_keys;
         bad_public_keys.emplace(public_key_type("BTS6MkMxwBjFWmcDjXRoJ4mW9Hd4LCSPwtv9tKG1qYW5Kgu4AhoZy"));
         BOOST_CHECK(!db_api.verify_account_authority( "nathan", bad_public_keys));

      } FC_LOG_AND_RETHROW()
}

BOOST_AUTO_TEST_CASE( any_two_of_three )
{
   try {
      fc::ecc::private_key nathan_key1 = fc::ecc::private_key::regenerate(fc::digest("key1"));
      fc::ecc::private_key nathan_key2 = fc::ecc::private_key::regenerate(fc::digest("key2"));
      fc::ecc::private_key nathan_key3 = fc::ecc::private_key::regenerate(fc::digest("key3"));
      const account_object& nathan = create_account("nathan", nathan_key1.get_public_key() );
      fund(nathan);
      graphene::app::database_api db_api(db);

      try {
         account_update_operation op;
         op.account = nathan.id;
         op.active = authority(2, public_key_type(nathan_key1.get_public_key()), 1,
               public_key_type(nathan_key2.get_public_key()), 1, public_key_type(nathan_key3.get_public_key()), 1);
         op.owner = *op.active;
         trx.operations.push_back(op);
         sign(trx, nathan_key1);
         PUSH_TX( db, trx, database::skip_transaction_dupe_check );
         trx.clear();
      } FC_CAPTURE_AND_RETHROW ((nathan.active))

      // two keys should work
      {
      	flat_set<public_key_type> public_keys;
      	public_keys.emplace(nathan_key1.get_public_key());
      	public_keys.emplace(nathan_key2.get_public_key());
      	BOOST_CHECK(db_api.verify_account_authority("nathan", public_keys));
      }

      // the other two keys should work
      {
     	   flat_set<public_key_type> public_keys;
      	public_keys.emplace(nathan_key2.get_public_key());
      	public_keys.emplace(nathan_key3.get_public_key());
     	   BOOST_CHECK(db_api.verify_account_authority("nathan", public_keys));
      }

      // just one key should not work
      {
     	   flat_set<public_key_type> public_keys;
         public_keys.emplace(nathan_key1.get_public_key());
     	   BOOST_CHECK(!db_api.verify_account_authority("nathan", public_keys));
      }
   } catch (fc::exception& e) {
      edump((e.to_detail_string()));
      throw;
   }
}

BOOST_AUTO_TEST_CASE( verify_authority_multiple_accounts )
{
   try {
      ACTORS( (nathan) (alice) (bob) );

      graphene::app::database_api db_api(db);

      try {
         account_update_operation op;
         op.account = nathan.id;
         op.active = authority(3, nathan_public_key, 1, alice.id, 1, bob.id, 1);
         op.owner = *op.active;
         trx.operations.push_back(op);
         sign(trx, nathan_private_key);
         PUSH_TX( db, trx, database::skip_transaction_dupe_check );
         trx.clear();
      } FC_CAPTURE_AND_RETHROW ((nathan.active))

      // requires 3 signatures
      {
      	flat_set<public_key_type> public_keys;
      	public_keys.emplace(nathan_public_key);
      	public_keys.emplace(alice_public_key);
      	public_keys.emplace(bob_public_key);
      	BOOST_CHECK(db_api.verify_account_authority("nathan", public_keys));
      }

      // only 2 signatures given
      {
      	flat_set<public_key_type> public_keys;
      	public_keys.emplace(nathan_public_key);
      	public_keys.emplace(bob_public_key);
      	BOOST_CHECK(!db_api.verify_account_authority("nathan", public_keys));
      }
   } catch (fc::exception& e) {
      edump((e.to_detail_string()));
      throw;
   }
}

BOOST_AUTO_TEST_CASE( get_assets_by_issuer ) {
   try {
      graphene::app::database_api db_api(db, &(this->app.get_options()));

      create_bitasset("CNY");
      create_bitasset("EUR");
      create_bitasset("USD");

      generate_block();

      auto assets = db_api.get_assets_by_issuer("witness-account", asset_id_type(), 10);

      BOOST_CHECK(assets.size() == 3);
      BOOST_CHECK(assets[0].symbol == "CNY");
      BOOST_CHECK(assets[1].symbol == "EUR");
      BOOST_CHECK(assets[2].symbol == "USD");

      assets = db_api.get_assets_by_issuer("witness-account", asset_id_type(200), 100);
      BOOST_CHECK(assets.size() == 0);

      GRAPHENE_CHECK_THROW(db_api.get_assets_by_issuer("nosuchaccount", asset_id_type(), 100), fc::exception);

   } catch (fc::exception& e) {
      edump((e.to_detail_string()));
      throw;
   }
}

BOOST_AUTO_TEST_CASE( get_call_orders_by_account ) {

   try {
      ACTORS((caller)(feedproducer));

      graphene::app::database_api db_api(db, &(this->app.get_options()));

      const auto &usd = create_bitasset("USD", feedproducer_id);
      const auto &cny = create_bitasset("CNY", feedproducer_id);
      const auto &core = asset_id_type()(db);

      int64_t init_balance(1000000);
      transfer(committee_account, caller_id, asset(init_balance));

      update_feed_producers(usd, {feedproducer.id});
      update_feed_producers(cny, {feedproducer.id});

      price_feed current_feed;
      current_feed.maintenance_collateral_ratio = 1750;
      current_feed.maximum_short_squeeze_ratio = 1100;
      current_feed.settlement_price = usd.amount(1) / core.amount(5);
      publish_feed(usd, feedproducer, current_feed);

      current_feed.maintenance_collateral_ratio = 1750;
      current_feed.maximum_short_squeeze_ratio = 1100;
      current_feed.settlement_price = cny.amount(1) / core.amount(5);
      publish_feed(cny, feedproducer, current_feed);

      auto call1 = borrow(caller, usd.amount(1000), asset(15000));
      auto call2 = borrow(caller, cny.amount(1000), asset(15000));

      auto calls = db_api.get_call_orders_by_account("caller", asset_id_type(), 100);

      BOOST_CHECK(calls.size() == 2);
      BOOST_CHECK(calls[0].id == call1->id);
      BOOST_CHECK(calls[1].id == call2->id);

      GRAPHENE_CHECK_THROW(db_api.get_call_orders_by_account("nosuchaccount", asset_id_type(), 100), fc::exception);

   } catch (fc::exception& e) {
      edump((e.to_detail_string()));
      throw;
   }
}

BOOST_AUTO_TEST_CASE( get_settle_orders_by_account ) {
   try {
      ACTORS((creator)(settler)(caller)(feedproducer));

      graphene::app::database_api db_api(db, &(this->app.get_options()));

      const auto &usd = create_bitasset("USD", creator_id);
      const auto &core = asset_id_type()(db);
      asset_id_type usd_id = usd.id;

      int64_t init_balance(1000000);
      transfer(committee_account, settler_id, asset(init_balance));
      transfer(committee_account, caller_id, asset(init_balance));

      update_feed_producers(usd, {feedproducer.id});

      price_feed current_feed;
      current_feed.maintenance_collateral_ratio = 1750;
      current_feed.maximum_short_squeeze_ratio = 1100;
      current_feed.settlement_price = usd.amount(1) / core.amount(5);
      publish_feed(usd, feedproducer, current_feed);

      borrow(caller, usd.amount(1000), asset(15000));
      generate_block();

      transfer(caller.id, settler.id, asset(200, usd_id));

      auto result = force_settle( settler, usd_id(db).amount(4));
      generate_block();

      auto settlements = db_api.get_settle_orders_by_account("settler", force_settlement_id_type(), 100);

      BOOST_CHECK(settlements.size() == 1);
      BOOST_CHECK(settlements[0].id == result.get<object_id_type>());

      GRAPHENE_CHECK_THROW(db_api.get_settle_orders_by_account("nosuchaccount", force_settlement_id_type(), 100), fc::exception);

   } catch (fc::exception& e) {
      edump((e.to_detail_string()));
      throw;
   }
}

BOOST_AUTO_TEST_CASE( asset_in_collateral )
{ try {
   ACTORS( (dan)(nathan) );
   fund( nathan );
   fund( dan );

   graphene::app::database_api db_api( db, &( app.get_options() ) );

   auto oassets = db_api.get_assets( { GRAPHENE_SYMBOL } );
   BOOST_REQUIRE( !oassets.empty() );
   BOOST_REQUIRE( oassets[0].valid() );
   BOOST_REQUIRE( oassets[0]->total_in_collateral.valid() );
   BOOST_CHECK_EQUAL( 0, oassets[0]->total_in_collateral->value );
   BOOST_CHECK( !oassets[0]->total_backing_collateral.valid() );

   asset_id_type bitusd_id = create_bitasset( "USDBIT", nathan_id, 100, charge_market_fee ).id;
   update_feed_producers( bitusd_id, { nathan_id } );
   asset_id_type bitdan_id = create_bitasset( "DANBIT", dan_id, 100, charge_market_fee ).id;
   update_feed_producers( bitdan_id, { nathan_id } );
   asset_id_type btc_id = create_bitasset( "BTC", nathan_id, 100, charge_market_fee, 8, bitusd_id ).id;
   update_feed_producers( btc_id, { nathan_id } );

   oassets = db_api.get_assets( { GRAPHENE_SYMBOL, "USDBIT", "DANBIT", "BTC" } );
   BOOST_REQUIRE_EQUAL( 4, oassets.size() );
   BOOST_REQUIRE( oassets[0].valid() );
   BOOST_REQUIRE( oassets[0]->total_in_collateral.valid() );
   BOOST_CHECK( !oassets[0]->total_backing_collateral.valid() );
   BOOST_REQUIRE( oassets[1].valid() );
   BOOST_REQUIRE( oassets[1]->total_in_collateral.valid() );
   BOOST_REQUIRE( oassets[1]->total_backing_collateral.valid() );
   BOOST_REQUIRE( oassets[2].valid() );
   BOOST_REQUIRE( oassets[2]->total_in_collateral.valid() );
   BOOST_REQUIRE( oassets[2]->total_backing_collateral.valid() );
   BOOST_REQUIRE( oassets[3].valid() );
   BOOST_REQUIRE( oassets[3]->total_in_collateral.valid() );
   BOOST_REQUIRE( oassets[3]->total_backing_collateral.valid() );
   BOOST_CHECK_EQUAL( 0, oassets[0]->total_in_collateral->value );
   BOOST_CHECK_EQUAL( 0, oassets[1]->total_in_collateral->value );
   BOOST_CHECK_EQUAL( 0, oassets[1]->total_backing_collateral->value );
   BOOST_CHECK_EQUAL( 0, oassets[2]->total_in_collateral->value );
   BOOST_CHECK_EQUAL( 0, oassets[2]->total_backing_collateral->value );
   BOOST_CHECK_EQUAL( 0, oassets[3]->total_in_collateral->value );
   BOOST_CHECK_EQUAL( 0, oassets[3]->total_backing_collateral->value );

   generate_block();
   fc::usleep(fc::milliseconds(100));

   const auto& bitusd = bitusd_id( db );
   const auto& bitdan = bitdan_id( db );
   const auto& btc = btc_id( db );

   {
      const auto& core = asset_id_type()( db );
      price_feed current_feed;
      current_feed.maintenance_collateral_ratio = 1750;
      current_feed.maximum_short_squeeze_ratio = 1100;
      current_feed.settlement_price = bitusd.amount(1) / core.amount(5);
      publish_feed( bitusd_id, nathan_id, current_feed );
      current_feed.settlement_price = bitdan.amount(1) / core.amount(5);
      publish_feed( bitdan_id, nathan_id, current_feed );
      current_feed.settlement_price = btc.amount(1) / bitusd.amount(100);
      publish_feed( btc_id, nathan_id, current_feed );
   }

   borrow( nathan_id, bitusd.amount(1000), asset(15000) );
   borrow( dan_id, bitusd.amount(100), asset(2000) );

   oassets = db_api.get_assets( { GRAPHENE_SYMBOL, "USDBIT", "DANBIT", "BTC" } );
   BOOST_REQUIRE_EQUAL( 4, oassets.size() );
   BOOST_REQUIRE( oassets[0].valid() );
   BOOST_REQUIRE( oassets[0]->total_in_collateral.valid() );
   BOOST_CHECK( !oassets[0]->total_backing_collateral.valid() );
   BOOST_REQUIRE( oassets[1].valid() );
   BOOST_REQUIRE( oassets[1]->total_in_collateral.valid() );
   BOOST_REQUIRE( oassets[1]->total_backing_collateral.valid() );
   BOOST_REQUIRE( oassets[2].valid() );
   BOOST_REQUIRE( oassets[2]->total_in_collateral.valid() );
   BOOST_REQUIRE( oassets[2]->total_backing_collateral.valid() );
   BOOST_REQUIRE( oassets[3].valid() );
   BOOST_REQUIRE( oassets[3]->total_in_collateral.valid() );
   BOOST_REQUIRE( oassets[3]->total_backing_collateral.valid() );
   BOOST_CHECK_EQUAL( 17000, oassets[0]->total_in_collateral->value );
   BOOST_CHECK_EQUAL( 0, oassets[1]->total_in_collateral->value );
   BOOST_CHECK_EQUAL( 17000, oassets[1]->total_backing_collateral->value );
   BOOST_CHECK_EQUAL( 0, oassets[2]->total_in_collateral->value );
   BOOST_CHECK_EQUAL( 0, oassets[2]->total_backing_collateral->value );
   BOOST_CHECK_EQUAL( 0, oassets[3]->total_in_collateral->value );
   BOOST_CHECK_EQUAL( 0, oassets[3]->total_backing_collateral->value );

   borrow( nathan_id, bitdan.amount(1000), asset(15000) );
   borrow( nathan_id, btc.amount(5), bitusd.amount(1000) );

   oassets = db_api.lookup_asset_symbols( { GRAPHENE_SYMBOL, "USDBIT", "DANBIT", "BTC" } );
   BOOST_REQUIRE_EQUAL( 4, oassets.size() );
   BOOST_REQUIRE( oassets[0].valid() );
   BOOST_REQUIRE( oassets[0]->total_in_collateral.valid() );
   BOOST_CHECK( !oassets[0]->total_backing_collateral.valid() );
   BOOST_REQUIRE( oassets[1].valid() );
   BOOST_REQUIRE( oassets[1]->total_in_collateral.valid() );
   BOOST_REQUIRE( oassets[1]->total_backing_collateral.valid() );
   BOOST_REQUIRE( oassets[2].valid() );
   BOOST_REQUIRE( oassets[2]->total_in_collateral.valid() );
   BOOST_REQUIRE( oassets[2]->total_backing_collateral.valid() );
   BOOST_REQUIRE( oassets[3].valid() );
   BOOST_REQUIRE( oassets[3]->total_in_collateral.valid() );
   BOOST_REQUIRE( oassets[3]->total_backing_collateral.valid() );
   BOOST_CHECK_EQUAL( 32000, oassets[0]->total_in_collateral->value );
   BOOST_CHECK_EQUAL( 1000, oassets[1]->total_in_collateral->value );
   BOOST_CHECK_EQUAL( 17000, oassets[1]->total_backing_collateral->value );
   BOOST_CHECK_EQUAL( 0, oassets[2]->total_in_collateral->value );
   BOOST_CHECK_EQUAL( 15000, oassets[2]->total_backing_collateral->value );
   BOOST_CHECK_EQUAL( 0, oassets[3]->total_in_collateral->value );
   BOOST_CHECK_EQUAL( 1000, oassets[3]->total_backing_collateral->value );

   force_settle( dan_id(db), bitusd.amount(100) ); // settles against nathan, receives 500 CORE collateral
   generate_blocks( db.head_block_time() + fc::days(2) );
   fc::usleep(fc::milliseconds(100));

   auto assets = db_api.list_assets( GRAPHENE_SYMBOL, 1 );
   BOOST_REQUIRE( !assets.empty() );
   BOOST_REQUIRE( assets[0].total_in_collateral.valid() );
   BOOST_CHECK( !assets[0].total_backing_collateral.valid() );
   BOOST_CHECK_EQUAL( 31500, assets[0].total_in_collateral->value );

   assets = db_api.get_assets_by_issuer( "nathan", asset_id_type(1), 2 );
   BOOST_REQUIRE_EQUAL( 2, assets.size() );
   BOOST_REQUIRE( assets[0].total_in_collateral.valid() );
   BOOST_REQUIRE( assets[0].total_backing_collateral.valid() );
   BOOST_REQUIRE( assets[1].total_in_collateral.valid() );
   BOOST_REQUIRE( assets[1].total_backing_collateral.valid() );
   BOOST_CHECK_EQUAL( 1000, assets[0].total_in_collateral->value );
   BOOST_CHECK_EQUAL( 16500, assets[0].total_backing_collateral->value );
   BOOST_CHECK_EQUAL( 0, assets[1].total_in_collateral->value );
   BOOST_CHECK_EQUAL( 1000, assets[1].total_backing_collateral->value );

} FC_LOG_AND_RETHROW() }


BOOST_AUTO_TEST_CASE( get_trade_history )
{ try {

   app.enable_plugin("market_history");
   graphene::app::application_options opt=app.get_options();
   opt.has_market_history_plugin = true;
   graphene::app::database_api db_api( db, &opt);

   ACTORS((bob)(alice));

   const auto& eur = create_user_issued_asset("EUR");
   const auto& usd = create_user_issued_asset("USD");

   issue_uia( bob_id, usd.amount(1000000) );
   issue_uia( alice_id, eur.amount(1000000) );

   // maker create an order
   create_sell_order(bob, usd.amount(200), eur.amount(210));

   // taker match it
   create_sell_order(alice, eur.amount(210), usd.amount(200));

   generate_block();

   // taker is selling
   auto history = db_api.get_trade_history( "EUR", "USD", db.head_block_time(), db.head_block_time() - fc::days(1) );
   BOOST_REQUIRE_EQUAL( 1, history.size() );
   BOOST_CHECK_EQUAL( "1.05", history[0].price );
   BOOST_CHECK_EQUAL( "2", history[0].amount );
   BOOST_CHECK_EQUAL( "2.10", history[0].value );
   BOOST_CHECK_EQUAL( "sell", history[0].type );
   BOOST_CHECK_EQUAL( bob_id.instance.value, history[0].side1_account_id.instance.value );
   BOOST_CHECK_EQUAL( alice_id.instance.value, history[0].side2_account_id.instance.value );

   // opposite side, taker is buying
   history = db_api.get_trade_history( "USD", "EUR", db.head_block_time(), db.head_block_time() - fc::days(1) );
   BOOST_REQUIRE_EQUAL( 1, history.size() );
   BOOST_CHECK_EQUAL( "0.9523809523809523809", history[0].price );
   BOOST_CHECK_EQUAL( "2.10", history[0].amount );
   BOOST_CHECK_EQUAL( "2", history[0].value );
   BOOST_CHECK_EQUAL( "buy", history[0].type );
   BOOST_CHECK_EQUAL( bob_id.instance.value, history[0].side1_account_id.instance.value );
   BOOST_CHECK_EQUAL( alice_id.instance.value, history[0].side2_account_id.instance.value );

   // by sequence
   history = db_api.get_trade_history_by_sequence( "EUR", "USD", 2, db.head_block_time() - fc::days(1) );
   BOOST_CHECK_EQUAL( "1.05", history[0].price );
   BOOST_CHECK_EQUAL( "2", history[0].amount );
   BOOST_CHECK_EQUAL( "2.10", history[0].value );
   BOOST_CHECK_EQUAL( "sell", history[0].type );
   BOOST_CHECK_EQUAL( bob_id.instance.value, history[0].side1_account_id.instance.value );
   BOOST_CHECK_EQUAL( alice_id.instance.value, history[0].side2_account_id.instance.value );

   // opposite side
   history = db_api.get_trade_history_by_sequence( "USD", "EUR", 2, db.head_block_time() - fc::days(1) );
   BOOST_REQUIRE_EQUAL( 1, history.size() );
   BOOST_CHECK_EQUAL( "0.9523809523809523809", history[0].price );
   BOOST_CHECK_EQUAL( "2.10", history[0].amount );
   BOOST_CHECK_EQUAL( "2", history[0].value );
   BOOST_CHECK_EQUAL( "buy", history[0].type );
   BOOST_CHECK_EQUAL( bob_id.instance.value, history[0].side1_account_id.instance.value );
   BOOST_CHECK_EQUAL( alice_id.instance.value, history[0].side2_account_id.instance.value );

} FC_LOG_AND_RETHROW() }


BOOST_AUTO_TEST_SUITE_END()
