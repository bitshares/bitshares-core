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
#include <graphene/wallet/wallet.hpp>
#include <fc/crypto/digest.hpp>

#include <iostream>

#include "../common/database_fixture.hpp"

using namespace graphene::chain;
using namespace graphene::chain::test;
using namespace graphene::wallet;

BOOST_FIXTURE_TEST_SUITE(wallet_tests, database_fixture)

  /***
   * Check the basic behavior of deriving potential owner keys from a brain key
   */
  BOOST_AUTO_TEST_CASE(derive_owner_keys_from_brain_key) {
      try {
          /***
           * Act
           */
          unsigned int nbr_keys_desired = 3;
          vector<brain_key_info> derived_keys = graphene::wallet::utility::derive_owner_keys_from_brain_key("SOME WORDS GO HERE", nbr_keys_desired);


          /***
           * Assert: Check the number of derived keys
           */
          BOOST_CHECK_EQUAL(nbr_keys_desired, derived_keys.size());

          /***
           * Assert: Check that each derived key is unique
           */
          set<string> set_derived_public_keys;
          for (auto info : derived_keys) {
              string description = (string) info.pub_key;
              set_derived_public_keys.emplace(description);
          }
          BOOST_CHECK_EQUAL(nbr_keys_desired, set_derived_public_keys.size());

          /***
           * Assert: Check whether every public key begins with the expected prefix
           */
          string expected_prefix = GRAPHENE_ADDRESS_PREFIX;
          for (auto info : derived_keys) {
              string description = (string) info.pub_key;
              BOOST_CHECK_EQUAL(0u, description.find(expected_prefix));
          }

      } FC_LOG_AND_RETHROW()
  }

  BOOST_AUTO_TEST_CASE(verify_account_authority) {
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
         op.active = authority(2, public_key_type(nathan_key1.get_public_key()), 1, public_key_type(nathan_key2.get_public_key()), 1, public_key_type(nathan_key3.get_public_key()), 1);
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

BOOST_AUTO_TEST_SUITE_END()

