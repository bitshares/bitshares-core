/*
 * Copyright (c) 2019 oxarbitrage and contributors.
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

#include <graphene/app/api.hpp>
#include <graphene/utilities/tempdir.hpp>
#include <fc/crypto/digest.hpp>

#include <graphene/custom_operations/custom_operations_plugin.hpp>

#include "../common/database_fixture.hpp"

#define BOOST_TEST_MODULE Custom operations plugin tests
#include <boost/test/included/unit_test.hpp>

using namespace graphene::chain;
using namespace graphene::chain::test;
using namespace graphene::app;
using namespace graphene::custom_operations;

BOOST_FIXTURE_TEST_SUITE( custom_operation_tests, database_fixture )

BOOST_AUTO_TEST_CASE(custom_operations_account_contact_test)
{
try {
   ACTORS((nathan)(alice));

   app.enable_plugin("custom_operations");
   custom_operations_api custom_operations_api(app);

   generate_block();
   fc::usleep(fc::milliseconds(200));

   enable_fees();
   signed_transaction trx;
   set_expiration(db, trx);

   int64_t init_balance(10000 * GRAPHENE_BLOCKCHAIN_PRECISION);

   transfer(committee_account, nathan_id, asset(init_balance));
   transfer(committee_account, alice_id, asset(init_balance));

   // nathan adds account data via custom operation
   {
      custom_operation op;
      account_contact_operation contact;
      account_contact_operation::ext data;

      data.name = "Nathan";
      data.email = "nathan@nathan.com";
      data.phone = "+1 434343434343";
      data.address = "";
      data.company = "Bitshares";
      data.url = "http://nathan.com/";

      contact.extensions.value = data;

      auto packed = fc::raw::pack(contact);
      packed.insert(packed.begin(), types::account_contact);
      packed.insert(packed.begin(), 0xFF);

      op.payer = nathan_id;
      op.data = packed;
      op.fee = db.get_global_properties().parameters.current_fees->calculate_fee(op);
      trx.operations.push_back(op);
      sign(trx, nathan_private_key);
      PUSH_TX(db, trx, ~0);
      trx.clear();
   }

   // alice adds account data via custom operation
   {
      custom_operation op;
      account_contact_operation contact;

      account_contact_operation::ext data;
      data.name = "Alice";
      data.email = "alice@alice.com";
      data.phone = "";
      data.address = "Some Street 456, Somewhere";
      data.company = "";
      data.url = "http://alice.com/";

      contact.extensions.value = data;

      auto packed = fc::raw::pack(contact);
      packed.insert(packed.begin(), types::account_contact);
      packed.insert(packed.begin(), 0xFF);

      op.payer = alice_id;
      op.data = packed;
      op.fee = db.get_global_properties().parameters.current_fees->calculate_fee(op);
      trx.operations.push_back(op);
      sign(trx, alice_private_key);
      PUSH_TX(db, trx, ~0);
      trx.clear();
   }

   generate_block();
   fc::usleep(fc::milliseconds(200));

   // check nathan account data with the api
   account_contact_object contact_results_nathan = *custom_operations_api.get_contact_info("nathan");
   BOOST_CHECK_EQUAL(contact_results_nathan.account.instance.value, 16 );
   BOOST_CHECK_EQUAL(*contact_results_nathan.name, "Nathan");
   BOOST_CHECK_EQUAL(*contact_results_nathan.email, "nathan@nathan.com");
   BOOST_CHECK_EQUAL(*contact_results_nathan.phone, "+1 434343434343");
   BOOST_CHECK_EQUAL(*contact_results_nathan.address, "");
   BOOST_CHECK_EQUAL(*contact_results_nathan.company, "Bitshares");
   BOOST_CHECK_EQUAL(*contact_results_nathan.url, "http://nathan.com/");

   // check alice account data with the api
   account_contact_object contact_results_alice = *custom_operations_api.get_contact_info("alice");
   BOOST_CHECK_EQUAL(contact_results_alice.account.instance.value, 17 );
   BOOST_CHECK_EQUAL(*contact_results_alice.name, "Alice");
   BOOST_CHECK_EQUAL(*contact_results_alice.email, "alice@alice.com");
   BOOST_CHECK_EQUAL(*contact_results_alice.phone, "");
   BOOST_CHECK_EQUAL(*contact_results_alice.address, "Some Street 456, Somewhere");
   BOOST_CHECK_EQUAL(*contact_results_alice.company, "");
   BOOST_CHECK_EQUAL(*contact_results_alice.url, "http://alice.com/");

   // alice update her data
   {
      custom_operation op;
      account_contact_operation contact;

      account_contact_operation::ext data;
      data.name = "Alice Smith";
      data.email = "alicesmith@alice.com";
      data.phone = "+1 1111 11 1111";
      data.address = "Some Street 456, Somewhere";
      data.company = "";
      data.url = "http://alice.com/";

      contact.extensions.value = data;

      auto packed = fc::raw::pack(contact);
      packed.insert(packed.begin(), types::account_contact);
      packed.insert(packed.begin(), 0xFF);

      op.payer = alice_id;
      op.data = packed;
      op.fee = db.get_global_properties().parameters.current_fees->calculate_fee(op);
      trx.operations.push_back(op);
      sign(trx, alice_private_key);
      PUSH_TX(db, trx, ~0);
      trx.clear();
   }

   generate_block();
   fc::usleep(fc::milliseconds(200));

   // check alice account updates with the api
   contact_results_alice = *custom_operations_api.get_contact_info("alice");
   BOOST_CHECK_EQUAL(contact_results_alice.account.instance.value, 17 );
   BOOST_CHECK_EQUAL(*contact_results_alice.name, "Alice Smith");
   BOOST_CHECK_EQUAL(*contact_results_alice.email, "alicesmith@alice.com");
   BOOST_CHECK_EQUAL(*contact_results_alice.phone, "+1 1111 11 1111");
   BOOST_CHECK_EQUAL(*contact_results_alice.address, "Some Street 456, Somewhere");
   BOOST_CHECK_EQUAL(*contact_results_alice.company, "");
   BOOST_CHECK_EQUAL(*contact_results_alice.url, "http://alice.com/");

   // alice try to update nathan data
   {
      custom_operation op;
      account_contact_operation contact;

      account_contact_operation::ext data;
      data.name = "Not my account";
      data.phone = "Fake phone";
      data.email = "Fake email";
      data.address = "Fake address";
      data.company = "Fake company";
      data.url = "http://fake.com";

      contact.extensions.value = data;

      auto packed = fc::raw::pack(contact);
      packed.insert(packed.begin(), types::account_contact);
      packed.insert(packed.begin(), 0xFF);

      op.payer = alice_id;
      op.data = packed;
      op.fee = db.get_global_properties().parameters.current_fees->calculate_fee(op);
      trx.operations.push_back(op);
      sign(trx, alice_private_key);
      PUSH_TX(db, trx, ~0);
      trx.clear();
   }
   generate_block();
   fc::usleep(fc::milliseconds(200));

   // operation will pass but data will be unchanged, exception was produced in plug in
   contact_results_nathan = *custom_operations_api.get_contact_info("nathan");
   BOOST_CHECK(contact_results_nathan.account.instance.value == 16 );
   BOOST_CHECK(*contact_results_nathan.name != "Not my account");
   BOOST_CHECK(*contact_results_nathan.phone != "Fake phone");
   BOOST_CHECK(*contact_results_nathan.email != "Fake email");
}
catch (fc::exception &e) {
   edump((e.to_detail_string()));
   throw;
} }

BOOST_AUTO_TEST_CASE(custom_operations_htlc_bitshares_eos_test)
{ try {

   ACTORS((nathan)(alice)(bob)(carol));

   app.enable_plugin("custom_operations");
   custom_operations_api custom_operations_api(app);

   generate_block();
   fc::usleep(fc::milliseconds(200));

   enable_fees();
   signed_transaction trx;
   set_expiration(db, trx);

   int64_t init_balance(10000 * GRAPHENE_BLOCKCHAIN_PRECISION);

   transfer(committee_account, nathan_id, asset(init_balance));
   transfer(committee_account, alice_id, asset(init_balance));
   transfer(committee_account, bob_id, asset(init_balance));
   transfer(committee_account, carol_id, asset(init_balance));

   enable_fees();

   // alice creates an order
   {
      custom_operation op;
      create_htlc_order_operation htlc;

      create_htlc_order_operation::ext data;
      data.blockchain = blockchains::eos;
      data.blockchain_account = "alice";
      data.bitshares_amount = asset(10);
      data.blockchain_asset = "EOS";
      data.blockchain_amount = "10";
      data.expiration = db.head_block_time() + fc::seconds(7200);

      htlc.extensions.value = data;

      auto packed = fc::raw::pack(htlc);
      packed.insert(packed.begin(), types::create_htlc);
      packed.insert(packed.begin(), 0xFF);

      op.payer = alice_id;
      op.data = packed;
      op.fee = db.get_global_properties().parameters.current_fees->calculate_fee(op);
      trx.operations.push_back(op);
      sign(trx, alice_private_key);
      PUSH_TX(db, trx, ~0);
      trx.clear();
   }

   // bob creates an order
   {
      custom_operation op;
      create_htlc_order_operation htlc;

      create_htlc_order_operation::ext data;
      data.blockchain = blockchains::eos;
      data.blockchain_account = "bob";
      data.bitshares_amount = asset(100);
      data.blockchain_asset = "EOS";
      data.blockchain_amount = "100";
      data.expiration = db.head_block_time() + fc::seconds(7200);
      data.tag = "Some text, can be a memo";

      htlc.extensions.value = data;

      auto packed = fc::raw::pack(htlc);
      packed.insert(packed.begin(), types::create_htlc);
      packed.insert(packed.begin(), 0xFF);

      op.payer = bob_id;
      op.data = packed;
      op.fee = db.get_global_properties().parameters.current_fees->calculate_fee(op);
      trx.operations.push_back(op);
      sign(trx, bob_private_key);
      PUSH_TX(db, trx, ~0);
      trx.clear();
   }

   // carol creates an order with missing information (blockchain_amount), will fail in the validator
   {
      custom_operation op;
      create_htlc_order_operation htlc;

      create_htlc_order_operation::ext data;
      data.blockchain = blockchains::eos;
      data.blockchain_account = "carol";
      data.bitshares_amount = asset(10);
      data.blockchain_asset = "EOS";
      data.expiration = db.head_block_time() + fc::seconds(7200);

      htlc.extensions.value = data;

      auto packed = fc::raw::pack(htlc);
      packed.insert(packed.begin(), types::create_htlc);
      packed.insert(packed.begin(), 0xFF);

      op.payer = carol_id;
      op.data = packed;
      op.fee = db.get_global_properties().parameters.current_fees->calculate_fee(op);
      trx.operations.push_back(op);
      sign(trx, carol_private_key);
      PUSH_TX(db, trx, ~0);
      trx.clear();
   }

   generate_block();
   fc::usleep(fc::milliseconds(200));

   // test the get_account_htlc_offers api call for alice
   vector<htlc_order_object> htlc_offers_results_alice = custom_operations_api.get_account_htlc_offers("alice",
         htlc_order_id_type(0), 100);
   BOOST_CHECK_EQUAL(htlc_offers_results_alice.size(), 1);
   BOOST_CHECK_EQUAL(htlc_offers_results_alice[0].id.instance(), 0);
   BOOST_CHECK_EQUAL(htlc_offers_results_alice[0].bitshares_account.instance.value, 17);
   BOOST_CHECK_EQUAL(htlc_offers_results_alice[0].blockchain_account, "alice" );
   BOOST_CHECK_EQUAL(htlc_offers_results_alice[0].bitshares_amount.asset_id.instance.value, 0);
   BOOST_CHECK_EQUAL(htlc_offers_results_alice[0].bitshares_amount.amount.value, 10);
   BOOST_CHECK_EQUAL(htlc_offers_results_alice[0].blockchain_asset, "EOS");
   BOOST_CHECK_EQUAL(htlc_offers_results_alice[0].blockchain_amount, "10");
   BOOST_CHECK(htlc_offers_results_alice[0].active);

   // test the get_htlc_offer api call with alice order
   auto htlc_offer = custom_operations_api.get_htlc_offer(htlc_order_id_type(0));
   BOOST_CHECK_EQUAL(htlc_offer->id.instance(), 0);
   BOOST_CHECK_EQUAL(htlc_offer->bitshares_account.instance.value, 17);
   BOOST_CHECK_EQUAL(htlc_offer->blockchain_account, "alice" );
   BOOST_CHECK_EQUAL(htlc_offer->bitshares_amount.asset_id.instance.value, 0);
   BOOST_CHECK_EQUAL(htlc_offer->bitshares_amount.amount.value, 10);
   BOOST_CHECK_EQUAL(htlc_offer->blockchain_asset, "EOS");
   BOOST_CHECK_EQUAL(htlc_offer->blockchain_amount, "10");
   BOOST_CHECK(htlc_offer->active);

   // test the get_account_htlc_offers api call for bob
   vector<htlc_order_object> htlc_offers_results_bob = custom_operations_api.get_account_htlc_offers("bob",
         htlc_order_id_type(0), 100);

   BOOST_CHECK_EQUAL(htlc_offers_results_bob.size(), 1);
   BOOST_CHECK_EQUAL(htlc_offers_results_bob[0].id.instance(), 1);
   BOOST_CHECK_EQUAL(htlc_offers_results_bob[0].bitshares_account.instance.value, 18);
   BOOST_CHECK_EQUAL(htlc_offers_results_bob[0].blockchain_account, "bob" );
   BOOST_CHECK_EQUAL(htlc_offers_results_bob[0].bitshares_amount.asset_id.instance.value, 0);
   BOOST_CHECK_EQUAL(htlc_offers_results_bob[0].bitshares_amount.amount.value, 100);
   BOOST_CHECK_EQUAL(htlc_offers_results_bob[0].blockchain_asset, "EOS");
   BOOST_CHECK_EQUAL(htlc_offers_results_bob[0].blockchain_amount, "100");
   BOOST_CHECK(htlc_offers_results_bob[0].active);
   if(htlc_offers_results_bob[0].tag.valid())
      BOOST_CHECK_EQUAL(*htlc_offers_results_bob[0].tag, "Some text, can be a memo");

   // get all active offers
   vector<htlc_order_object> htlc_offers_results_active = custom_operations_api.get_active_htlc_offers(
         htlc_order_id_type(0), 100);

   BOOST_CHECK_EQUAL(htlc_offers_results_active.size(), 2);
   BOOST_CHECK_EQUAL(htlc_offers_results_active[0].id.instance(), 0);
   BOOST_CHECK_EQUAL(htlc_offers_results_active[0].bitshares_account.instance.value, 17);
   BOOST_CHECK_EQUAL(htlc_offers_results_active[0].blockchain_account, "alice" );
   BOOST_CHECK_EQUAL(htlc_offers_results_active[0].bitshares_amount.asset_id.instance.value, 0);
   BOOST_CHECK_EQUAL(htlc_offers_results_active[0].bitshares_amount.amount.value, 10);
   BOOST_CHECK_EQUAL(htlc_offers_results_active[0].blockchain_asset, "EOS");
   BOOST_CHECK_EQUAL(htlc_offers_results_active[0].blockchain_amount, "10");
   BOOST_CHECK(htlc_offers_results_active[0].active);

   BOOST_CHECK_EQUAL(htlc_offers_results_active[1].id.instance(), 1);
   BOOST_CHECK_EQUAL(htlc_offers_results_active[1].bitshares_account.instance.value, 18);
   BOOST_CHECK_EQUAL(htlc_offers_results_active[1].blockchain_account, "bob" );
   BOOST_CHECK_EQUAL(htlc_offers_results_active[1].bitshares_amount.asset_id.instance.value, 0);
   BOOST_CHECK_EQUAL(htlc_offers_results_active[1].bitshares_amount.amount.value, 100);
   BOOST_CHECK_EQUAL(htlc_offers_results_active[1].blockchain_asset, "EOS");
   BOOST_CHECK_EQUAL(htlc_offers_results_active[1].blockchain_amount, "100");
   BOOST_CHECK(htlc_offers_results_active[1].active);
   if(htlc_offers_results_active[0].tag.valid())
      BOOST_CHECK_EQUAL(*htlc_offers_results_active[0].tag, "Some text, can be a memo");

   // nathan takes alice order
   {
      custom_operation op;
      take_htlc_order_operation htlc;

      take_htlc_order_operation::ext data;
      data.htlc_order_id = htlc_offers_results_alice[0].id;
      data.blockchain_account = "nathan";

      htlc.extensions.value = data;

      auto packed = fc::raw::pack(htlc);
      packed.insert(packed.begin(), types::take_htlc);
      packed.insert(packed.begin(), 0xFF);

      op.payer = nathan_id;
      op.data = packed;
      op.fee = db.get_global_properties().parameters.current_fees->calculate_fee(op);
      trx.operations.push_back(op);
      sign(trx, nathan_private_key);
      PUSH_TX(db, trx, ~0);
      trx.clear();
   }
   generate_block();
   fc::usleep(fc::milliseconds(200));

   // check the taken object
   htlc_offer = custom_operations_api.get_htlc_offer(htlc_order_id_type(0));
   BOOST_CHECK_EQUAL(htlc_offer->id.instance(), 0);
   BOOST_CHECK_EQUAL(htlc_offer->bitshares_account.instance.value, 17);
   BOOST_CHECK_EQUAL(htlc_offer->blockchain_account, "alice" );
   BOOST_CHECK_EQUAL(htlc_offer->bitshares_amount.asset_id.instance.value, 0);
   BOOST_CHECK_EQUAL(htlc_offer->bitshares_amount.amount.value, 10);
   BOOST_CHECK_EQUAL(htlc_offer->blockchain_asset, "EOS");
   BOOST_CHECK_EQUAL(htlc_offer->blockchain_amount, "10");
   BOOST_CHECK(!htlc_offer->active);
   BOOST_CHECK_EQUAL(htlc_offer->taker_bitshares_account->instance.value, 16);
   BOOST_CHECK_EQUAL(*htlc_offer->taker_blockchain_account, "nathan");

   // alice order was taken, bob order still up for get_active_htlc_offers
   htlc_offers_results_active = custom_operations_api.get_active_htlc_offers(htlc_order_id_type(0), 100);
   BOOST_CHECK_EQUAL(htlc_offers_results_active.size(), 1);

   BOOST_CHECK_EQUAL(htlc_offers_results_active[0].id.instance(), 1);
   BOOST_CHECK_EQUAL(htlc_offers_results_active[0].bitshares_account.instance.value, 18);
   BOOST_CHECK_EQUAL(htlc_offers_results_active[0].blockchain_account, "bob");
   BOOST_CHECK_EQUAL(htlc_offers_results_active[0].bitshares_amount.asset_id.instance.value, 0);
   BOOST_CHECK_EQUAL(htlc_offers_results_active[0].bitshares_amount.amount.value, 100);
   BOOST_CHECK_EQUAL(htlc_offers_results_active[0].blockchain_asset, "EOS");
   BOOST_CHECK_EQUAL(htlc_offers_results_active[0].blockchain_amount, "100");
   BOOST_CHECK(htlc_offers_results_active[0].active);
   if(htlc_offers_results_active[0].tag.valid())
      BOOST_CHECK_EQUAL(*htlc_offers_results_active[0].tag, "Some text, can be a memo");

   // make bob order expire
   generate_blocks(7201);
   fc::usleep(fc::milliseconds(200));

   htlc_offers_results_active = custom_operations_api.get_active_htlc_offers(htlc_order_id_type(0), 100);
   BOOST_CHECK_EQUAL(htlc_offers_results_active.size(), 0);
}

catch (fc::exception &e) {
   edump((e.to_detail_string()));
   throw;
} }

BOOST_AUTO_TEST_CASE(custom_operations_account_storage_test)
{
try {
   ACTORS((nathan)(alice)(robert)(patty));

   app.enable_plugin("custom_operations");
   custom_operations_api custom_operations_api(app);

   generate_block();
   fc::usleep(fc::milliseconds(200));

   enable_fees();
   signed_transaction trx;
   set_expiration(db, trx);

   int64_t init_balance(10000 * GRAPHENE_BLOCKCHAIN_PRECISION);

   transfer(committee_account, nathan_id, asset(init_balance));
   transfer(committee_account, alice_id, asset(init_balance));

   // nathan adds arbitrary account data via custom operation, simulating some dapp settings in this case
   {
      custom_operation op;
      account_store_data store;
      account_store_data::ext data;

      flat_map<string, string> pairs;
      pairs["language"] = "en";
      pairs["image_url"] = "http://some.image.url/img.jpg";

      store.extensions.value.pairs = pairs;

      auto packed = fc::raw::pack(store);
      packed.insert(packed.begin(), types::account_store);
      packed.insert(packed.begin(), 0xFF);

      op.payer = nathan_id;
      op.data = packed;
      op.fee = db.get_global_properties().parameters.current_fees->calculate_fee(op);
      trx.operations.push_back(op);
      sign(trx, nathan_private_key);
      PUSH_TX(db, trx, ~0);
      trx.clear();
   }

   generate_block();
   fc::usleep(fc::milliseconds(200));

   // check nathan stored data with the api
   account_storage_object storage_results_nathan = *custom_operations_api.get_storage_info("nathan");

   BOOST_CHECK_EQUAL(storage_results_nathan.account.instance.value, 16 );
   auto row1 = storage_results_nathan.storage_map.find("language");
   auto row2 = storage_results_nathan.storage_map.find("image_url");

   BOOST_CHECK_EQUAL(row1->first, "language");
   BOOST_CHECK_EQUAL(row1->second, "en");
   BOOST_CHECK_EQUAL(row2->first, "image_url");
   BOOST_CHECK_EQUAL(row2->second, "http://some.image.url/img.jpg");

   // add accounts to account list storage
   {
      custom_operation op;
      account_list_data list;
      account_list_data::ext data;

      flat_set<account_id_type> accounts;
      accounts.insert(alice_id);
      accounts.insert(robert_id);

      list.extensions.value.accounts = accounts;

      auto packed = fc::raw::pack(list);
      packed.insert(packed.begin(), types::account_list);
      packed.insert(packed.begin(), 0xFF);

      op.payer = nathan_id;
      op.data = packed;
      op.fee = db.get_global_properties().parameters.current_fees->calculate_fee(op);
      trx.operations.push_back(op);
      sign(trx, nathan_private_key);
      PUSH_TX(db, trx, ~0);
      trx.clear();
   }

   generate_block();
   fc::usleep(fc::milliseconds(200));

   // get the account list for nathan, check alice and robert are there
   auto account_list_nathan = *custom_operations_api.get_storage_info("nathan");

   BOOST_CHECK_EQUAL(account_list_nathan.account.instance.value, 16 );
   BOOST_CHECK_EQUAL(account_list_nathan.account_list.size(), 2 );
   auto itr = account_list_nathan.account_list.begin();
   BOOST_CHECK_EQUAL(itr->instance.value, alice_id.instance.value);
   ++itr;
   BOOST_CHECK_EQUAL(itr->instance.value, robert_id.instance.value);

   // add a value into account list already there
   {
      custom_operation op;
      account_list_data list;
      account_list_data::ext data;

      flat_set<account_id_type> accounts;
      accounts.insert(alice_id);

      list.extensions.value.accounts = accounts;

      auto packed = fc::raw::pack(list);
      packed.insert(packed.begin(), types::account_list);
      packed.insert(packed.begin(), 0xFF);

      op.payer = nathan_id;
      op.data = packed;
      op.fee = db.get_global_properties().parameters.current_fees->calculate_fee(op);
      trx.operations.push_back(op);
      sign(trx, nathan_private_key);
      PUSH_TX(db, trx, ~0);
      trx.clear();
   }

   generate_block();
   fc::usleep(fc::milliseconds(200));

   // nothing changes
   account_list_nathan = *custom_operations_api.get_storage_info("nathan");
   BOOST_CHECK_EQUAL(account_list_nathan.account.instance.value, 16 );
   BOOST_CHECK_EQUAL(account_list_nathan.account_list.size(), 2 );
   itr = account_list_nathan.account_list.begin();
   BOOST_CHECK_EQUAL(itr->instance.value, alice_id.instance.value);
   ++itr;
   BOOST_CHECK_EQUAL(itr->instance.value, robert_id.instance.value);

   // delete alice from the list
   {
      custom_operation op;
      account_list_data list;
      account_list_data::ext data;

      flat_set<account_id_type> accounts;
      accounts.insert(alice_id);

      list.extensions.value.accounts = accounts;
      list.extensions.value.remove = true;

      auto packed = fc::raw::pack(list);
      packed.insert(packed.begin(), types::account_list);
      packed.insert(packed.begin(), 0xFF);

      op.payer = nathan_id;
      op.data = packed;
      op.fee = db.get_global_properties().parameters.current_fees->calculate_fee(op);
      trx.operations.push_back(op);
      sign(trx, nathan_private_key);
      PUSH_TX(db, trx, ~0);
      trx.clear();
   }

   generate_block();
   fc::usleep(fc::milliseconds(200));

   // alice gone
   account_list_nathan = *custom_operations_api.get_storage_info("nathan");
   BOOST_CHECK_EQUAL(account_list_nathan.account.instance.value, 16 );
   BOOST_CHECK_EQUAL(account_list_nathan.account_list.size(), 1 );
   itr = account_list_nathan.account_list.begin();
   BOOST_CHECK_EQUAL(itr->instance.value, robert_id.instance.value);

   // add and edit more stuff to the storage
   {
      custom_operation op;
      account_store_data store;
      account_store_data::ext data;

      flat_map<string, string> pairs;
      pairs["image_url"] = "http://new.image.url/newimg.jpg";
      pairs["theme"] = "dark";

      store.extensions.value.pairs = pairs;

      auto packed = fc::raw::pack(store);
      packed.insert(packed.begin(), types::account_store);
      packed.insert(packed.begin(), 0xFF);

      op.payer = nathan_id;
      op.data = packed;
      op.fee = db.get_global_properties().parameters.current_fees->calculate_fee(op);
      trx.operations.push_back(op);
      sign(trx, nathan_private_key);
      PUSH_TX(db, trx, ~0);
      trx.clear();
   }

   generate_block();
   fc::usleep(fc::milliseconds(200));

   // all good, image_url updated and theme added
   storage_results_nathan = *custom_operations_api.get_storage_info("nathan");
   BOOST_CHECK_EQUAL(storage_results_nathan.account.instance.value, 16 );
   row1 = storage_results_nathan.storage_map.find("language");
   row2 = storage_results_nathan.storage_map.find("image_url");
   auto row3 = storage_results_nathan.storage_map.find("theme");

   BOOST_CHECK_EQUAL(row1->first, "language");
   BOOST_CHECK_EQUAL(row1->second, "en");
   BOOST_CHECK_EQUAL(row2->first, "image_url");
   BOOST_CHECK_EQUAL(row2->second, "http://new.image.url/newimg.jpg");
   BOOST_CHECK_EQUAL(row3->first, "theme");
   BOOST_CHECK_EQUAL(row3->second, "dark");

   // delete stuff from the storage
   {
      custom_operation op;
      account_store_data store;
      account_store_data::ext data;

      flat_map<string, string> pairs;
      pairs["theme"] = "dark";

      store.extensions.value.pairs = pairs;
      store.extensions.value.remove = true;

      auto packed = fc::raw::pack(store);
      packed.insert(packed.begin(), types::account_store);
      packed.insert(packed.begin(), 0xFF);

      op.payer = nathan_id;
      op.data = packed;
      op.fee = db.get_global_properties().parameters.current_fees->calculate_fee(op);
      trx.operations.push_back(op);
      sign(trx, nathan_private_key);
      PUSH_TX(db, trx, ~0);
      trx.clear();
   }

   generate_block();
   fc::usleep(fc::milliseconds(200));

   // theme is removed from the storage
   storage_results_nathan = *custom_operations_api.get_storage_info("nathan");
   BOOST_CHECK_EQUAL(storage_results_nathan.account.instance.value, 16 );
   row1 = storage_results_nathan.storage_map.find("language");
   row2 = storage_results_nathan.storage_map.find("image_url");

   BOOST_CHECK_EQUAL(row1->first, "language");
   BOOST_CHECK_EQUAL(row1->second, "en");
   BOOST_CHECK_EQUAL(row2->first, "image_url");
   BOOST_CHECK_EQUAL(row2->second, "http://new.image.url/newimg.jpg");

   // delete stuff from that it is not there
   {
      custom_operation op;
      account_store_data store;
      account_store_data::ext data;

      flat_map<string, string> pairs;
      pairs["nothere"] = "nothere";

      store.extensions.value.pairs = pairs;
      store.extensions.value.remove = true;

      auto packed = fc::raw::pack(store);
      packed.insert(packed.begin(), types::account_store);
      packed.insert(packed.begin(), 0xFF);

      op.payer = nathan_id;
      op.data = packed;
      op.fee = db.get_global_properties().parameters.current_fees->calculate_fee(op);
      trx.operations.push_back(op);
      sign(trx, nathan_private_key);
      PUSH_TX(db, trx, ~0);
      trx.clear();
   }

   generate_block();
   fc::usleep(fc::milliseconds(200));

   // nothing changes
   storage_results_nathan = *custom_operations_api.get_storage_info("nathan");
   BOOST_CHECK_EQUAL(storage_results_nathan.account.instance.value, 16 );
   row1 = storage_results_nathan.storage_map.find("language");
   row2 = storage_results_nathan.storage_map.find("image_url");

   BOOST_CHECK_EQUAL(row1->first, "language");
   BOOST_CHECK_EQUAL(row1->second, "en");
   BOOST_CHECK_EQUAL(row2->first, "image_url");
   BOOST_CHECK_EQUAL(row2->second, "http://new.image.url/newimg.jpg");

   // add more than 10 storage items in 1 operation is not allowed
   {
      custom_operation op;
      account_store_data store;
      account_store_data::ext data;

      flat_map<string, string> pairs;
      pairs["key1"] = "value1";
      pairs["key2"] = "value2";
      pairs["key3"] = "value3";
      pairs["key4"] = "value4";
      pairs["key5"] = "value5";
      pairs["key6"] = "value6";
      pairs["key7"] = "value7";
      pairs["key8"] = "value8";
      pairs["key9"] = "value9";
      pairs["key10"] = "value10";
      pairs["key11"] = "value11";

      store.extensions.value.pairs = pairs;

      auto packed = fc::raw::pack(store);
      packed.insert(packed.begin(), types::account_store);
      packed.insert(packed.begin(), 0xFF);

      op.payer = nathan_id;
      op.data = packed;
      op.fee = db.get_global_properties().parameters.current_fees->calculate_fee(op);
      trx.operations.push_back(op);
      sign(trx, nathan_private_key);
      PUSH_TX(db, trx, ~0);
      trx.clear();
   }

   generate_block();
   fc::usleep(fc::milliseconds(200));

   // add more than 10 accounts to the list in 1 operation is not allowed
   {
      custom_operation op;
      account_list_data list;
      account_list_data::ext data;

      flat_set<account_id_type> accounts;
      accounts.insert(account_id_type(0));
      accounts.insert(account_id_type(1));
      accounts.insert(account_id_type(2));
      accounts.insert(account_id_type(3));
      accounts.insert(account_id_type(4));
      accounts.insert(account_id_type(5));
      accounts.insert(account_id_type(6));
      accounts.insert(account_id_type(7));
      accounts.insert(account_id_type(8));
      accounts.insert(account_id_type(9));
      accounts.insert(account_id_type(10));

      list.extensions.value.accounts = accounts;
      list.extensions.value.remove = true;

      auto packed = fc::raw::pack(list);
      packed.insert(packed.begin(), types::account_list);
      packed.insert(packed.begin(), 0xFF);

      op.payer = nathan_id;
      op.data = packed;
      op.fee = db.get_global_properties().parameters.current_fees->calculate_fee(op);
      trx.operations.push_back(op);
      sign(trx, nathan_private_key);
      PUSH_TX(db, trx, ~0);
      trx.clear();
   }

   generate_block();
   fc::usleep(fc::milliseconds(200));

   // alice, duplicated keys in storage, only second value will be added
   {
      custom_operation op;
      account_store_data store;
      account_store_data::ext data;

      flat_map<string, string> pairs;
      pairs["key1"] = "value1";
      pairs["key1"] = "value2";

      store.extensions.value.pairs = pairs;

      auto packed = fc::raw::pack(store);
      packed.insert(packed.begin(), types::account_store);
      packed.insert(packed.begin(), 0xFF);

      op.payer = alice_id;
      op.data = packed;
      op.fee = db.get_global_properties().parameters.current_fees->calculate_fee(op);
      trx.operations.push_back(op);
      sign(trx, alice_private_key);
      PUSH_TX(db, trx, ~0);
      trx.clear();
   }

   generate_block();
   fc::usleep(fc::milliseconds(200));

   auto storage_results_alice = *custom_operations_api.get_storage_info("alice");
   BOOST_CHECK_EQUAL(storage_results_alice.account.instance.value, 17 );
   row1 = storage_results_alice.storage_map.find("key1");

   BOOST_CHECK_EQUAL(row1->first, "key1");
   BOOST_CHECK_EQUAL(row1->second, "value2");

   // duplicated accounts in the list, only 1 will be inserted
   {
      custom_operation op;
      account_list_data list;
      account_list_data::ext data;

      flat_set<account_id_type> accounts;
      accounts.insert(robert_id);
      accounts.insert(robert_id);

      list.extensions.value.accounts = accounts;

      auto packed = fc::raw::pack(list);
      packed.insert(packed.begin(), types::account_list);
      packed.insert(packed.begin(), 0xFF);

      op.payer = alice_id;
      op.data = packed;
      op.fee = db.get_global_properties().parameters.current_fees->calculate_fee(op);
      trx.operations.push_back(op);
      sign(trx, alice_private_key);
      PUSH_TX(db, trx, ~0);
      trx.clear();
   }

   generate_block();
   fc::usleep(fc::milliseconds(200));

   auto account_list_alice = *custom_operations_api.get_storage_info("alice");
   BOOST_CHECK_EQUAL(account_list_alice.account.instance.value, 17 );
   BOOST_CHECK_EQUAL(account_list_alice.account_list.size(), 1 );
   itr = account_list_nathan.account_list.begin();
   BOOST_CHECK_EQUAL(itr->instance.value, robert_id.instance.value);

}
catch (fc::exception &e) {
   edump((e.to_detail_string()));
   throw;
} }


BOOST_AUTO_TEST_SUITE_END()
