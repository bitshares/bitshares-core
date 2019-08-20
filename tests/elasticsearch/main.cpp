/*
 * Copyright (c) 2018 oxarbitrage and contributors.
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

#include <graphene/utilities/elasticsearch.hpp>
#include <graphene/elasticsearch/elasticsearch_plugin.hpp>

#include "../common/database_fixture.hpp"

#define BOOST_TEST_MODULE Elastic Search Database Tests
#include <boost/test/included/unit_test.hpp>

using namespace graphene::chain;
using namespace graphene::chain::test;
using namespace graphene::app;

BOOST_FIXTURE_TEST_SUITE( elasticsearch_tests, database_fixture )

BOOST_AUTO_TEST_CASE(elasticsearch_account_history) {
   try {

      CURL *curl; // curl handler
      curl = curl_easy_init();

      graphene::utilities::ES es;
      es.curl = curl;
      es.elasticsearch_url = "http://localhost:9200/";
      es.index_prefix = "bitshares-";
      //es.auth = "elastic:changeme";

      // delete all first
      auto delete_account_history = graphene::utilities::deleteAll(es);
      fc::usleep(fc::milliseconds(1000)); // this is because index.refresh_interval, nothing to worry

      if(delete_account_history) { // all records deleted

         //account_id_type() do 3 ops
         create_bitasset("USD", account_id_type());
         auto dan = create_account("dan");
         auto bob = create_account("bob");

         generate_block();
         fc::usleep(fc::milliseconds(1000));

         // for later use
         //int asset_create_op_id = operation::tag<asset_create_operation>::value;
         //int account_create_op_id = operation::tag<account_create_operation>::value;

         string query = "{ \"query\" : { \"bool\" : { \"must\" : [{\"match_all\": {}}] } } }";
         es.endpoint = es.index_prefix + "*/data/_count";
         es.query = query;

         auto res = graphene::utilities::simpleQuery(es);
         variant j = fc::json::from_string(res);
         auto total = j["count"].as_string();
         BOOST_CHECK_EQUAL(total, "5");

         es.endpoint = es.index_prefix + "*/data/_search";
         res = graphene::utilities::simpleQuery(es);
         j = fc::json::from_string(res);
         auto first_id = j["hits"]["hits"][size_t(0)]["_id"].as_string();
         BOOST_CHECK_EQUAL(first_id, "2.9.1"); // this should be 0? are they inserted in the right order?

         generate_block();
         auto willie = create_account("willie");
         generate_block();

         fc::usleep(fc::milliseconds(1000)); // index.refresh_interval

         es.endpoint = es.index_prefix + "*/data/_count";
         res = graphene::utilities::simpleQuery(es);
         j = fc::json::from_string(res);

         total = j["count"].as_string();
         BOOST_CHECK_EQUAL(total, "7");

         // do some transfers in 1 block
         transfer(account_id_type()(db), bob, asset(100));
         transfer(account_id_type()(db), bob, asset(200));
         transfer(account_id_type()(db), bob, asset(300));

         generate_block();
         fc::usleep(fc::milliseconds(1000)); // index.refresh_interval

         res = graphene::utilities::simpleQuery(es);
         j = fc::json::from_string(res);

         total = j["count"].as_string();
         BOOST_CHECK_EQUAL(total, "13");

         // check the visitor data
         auto block_date = db.head_block_time();
         std::string index_name = graphene::utilities::generateIndexName(block_date, "bitshares-");

         es.endpoint = index_name + "/data/2.9.12"; // we know last op is a transfer of amount 300
         res = graphene::utilities::getEndPoint(es);
         j = fc::json::from_string(res);
         auto last_transfer_amount = j["_source"]["operation_history"]["op_object"]["amount_"]["amount"].as_string();
         BOOST_CHECK_EQUAL(last_transfer_amount, "300");
      }
   }
   catch (fc::exception &e) {
      edump((e.to_detail_string()));
      throw;
   }
}

BOOST_AUTO_TEST_CASE(elasticsearch_objects) {
   try {

      CURL *curl; // curl handler
      curl = curl_easy_init();

      graphene::utilities::ES es;
      es.curl = curl;
      es.elasticsearch_url = "http://localhost:9200/";
      es.index_prefix = "objects-";
      //es.auth = "elastic:changeme";

      // delete all first
      auto delete_objects = graphene::utilities::deleteAll(es);

      generate_block();
      fc::usleep(fc::milliseconds(1000));

      if(delete_objects) { // all records deleted

         // asset and bitasset
         create_bitasset("USD", account_id_type());
         generate_block();
         fc::usleep(fc::milliseconds(1000));

         string query = "{ \"query\" : { \"bool\" : { \"must\" : [{\"match_all\": {}}] } } }";
         es.endpoint = es.index_prefix + "*/data/_count";
         es.query = query;

         auto res = graphene::utilities::simpleQuery(es);
         variant j = fc::json::from_string(res);
         auto total = j["count"].as_string();
         BOOST_CHECK_EQUAL(total, "2");

         es.endpoint = es.index_prefix + "asset/data/_search";
         res = graphene::utilities::simpleQuery(es);
         j = fc::json::from_string(res);
         auto first_id = j["hits"]["hits"][size_t(0)]["_source"]["symbol"].as_string();
         BOOST_CHECK_EQUAL(first_id, "USD");

         auto bitasset_data_id = j["hits"]["hits"][size_t(0)]["_source"]["bitasset_data_id"].as_string();
         es.endpoint = es.index_prefix + "bitasset/data/_search";
         es.query = "{ \"query\" : { \"bool\": { \"must\" : [{ \"term\": { \"object_id\": \""+bitasset_data_id+"\"}}] } } }";
         res = graphene::utilities::simpleQuery(es);
         j = fc::json::from_string(res);
         auto bitasset_object_id = j["hits"]["hits"][size_t(0)]["_source"]["object_id"].as_string();
         BOOST_CHECK_EQUAL(bitasset_object_id, bitasset_data_id);
      }
   }
   catch (fc::exception &e) {
      edump((e.to_detail_string()));
      throw;
   }
}

BOOST_AUTO_TEST_CASE(elasticsearch_suite) {
   try {

      CURL *curl; // curl handler
      curl = curl_easy_init();

      graphene::utilities::ES es;
      es.curl = curl;
      es.elasticsearch_url = "http://localhost:9200/";
      es.index_prefix = "bitshares-";
      auto delete_account_history = graphene::utilities::deleteAll(es);
      fc::usleep(fc::milliseconds(1000));
      es.index_prefix = "objects-";
      auto delete_objects = graphene::utilities::deleteAll(es);
      fc::usleep(fc::milliseconds(1000));

      if(delete_account_history && delete_objects) { // all records deleted


      }
   }
   catch (fc::exception &e) {
      edump((e.to_detail_string()));
      throw;
   }
}

BOOST_AUTO_TEST_CASE(elasticsearch_history_api) {
   try {
      CURL *curl; // curl handler
      curl = curl_easy_init();

      graphene::utilities::ES es;
      es.curl = curl;
      es.elasticsearch_url = "http://localhost:9200/";
      es.index_prefix = "bitshares-";

      auto delete_account_history = graphene::utilities::deleteAll(es);

      generate_block();
      fc::usleep(fc::milliseconds(1000));

      if(delete_account_history) {

         create_bitasset("USD", account_id_type()); // create op 0
         const account_object& dan = create_account("dan"); // create op 1
         create_bitasset("CNY", dan.id); // create op 2
         create_bitasset("BTC", account_id_type()); // create op 3
         create_bitasset("XMR", dan.id); // create op 4
         create_bitasset("EUR", account_id_type()); // create op 5
         create_bitasset("OIL", dan.id); // create op 6

         generate_block();
         fc::usleep(fc::milliseconds(1000));

         graphene::app::history_api hist_api(app);
         app.enable_plugin("elasticsearch");

         // f(A, 0, 4, 9) = { 5, 3, 1, 0 }
         auto histories = hist_api.get_account_history("1.2.0", operation_history_id_type(), 4, operation_history_id_type(9));

         BOOST_CHECK_EQUAL(histories.size(), 4u);
         BOOST_CHECK_EQUAL(histories[0].id.instance(), 5u);
         BOOST_CHECK_EQUAL(histories[1].id.instance(), 3u);
         BOOST_CHECK_EQUAL(histories[2].id.instance(), 1u);
         BOOST_CHECK_EQUAL(histories[3].id.instance(), 0u);

         // f(A, 0, 4, 6) = { 5, 3, 1, 0 }
         histories = hist_api.get_account_history("1.2.0", operation_history_id_type(), 4, operation_history_id_type(6));
         BOOST_CHECK_EQUAL(histories.size(), 4u);
         BOOST_CHECK_EQUAL(histories[0].id.instance(), 5u);
         BOOST_CHECK_EQUAL(histories[1].id.instance(), 3u);
         BOOST_CHECK_EQUAL(histories[2].id.instance(), 1u);
         BOOST_CHECK_EQUAL(histories[3].id.instance(), 0u);

         // f(A, 0, 4, 5) = { 5, 3, 1, 0 }
         histories = hist_api.get_account_history("1.2.0", operation_history_id_type(), 4, operation_history_id_type(5));
         BOOST_CHECK_EQUAL(histories.size(), 4u);
         BOOST_CHECK_EQUAL(histories[0].id.instance(), 5u);
         BOOST_CHECK_EQUAL(histories[1].id.instance(), 3u);
         BOOST_CHECK_EQUAL(histories[2].id.instance(), 1u);
         BOOST_CHECK_EQUAL(histories[3].id.instance(), 0u);

         // f(A, 0, 4, 4) = { 3, 1, 0 }
         histories = hist_api.get_account_history("1.2.0", operation_history_id_type(), 4, operation_history_id_type(4));
         BOOST_CHECK_EQUAL(histories.size(), 3u);
         BOOST_CHECK_EQUAL(histories[0].id.instance(), 3u);
         BOOST_CHECK_EQUAL(histories[1].id.instance(), 1u);
         BOOST_CHECK_EQUAL(histories[2].id.instance(), 0u);

         // f(A, 0, 4, 3) = { 3, 1, 0 }
         histories = hist_api.get_account_history("1.2.0", operation_history_id_type(), 4, operation_history_id_type(3));
         BOOST_CHECK_EQUAL(histories.size(), 3u);
         BOOST_CHECK_EQUAL(histories[0].id.instance(), 3u);
         BOOST_CHECK_EQUAL(histories[1].id.instance(), 1u);
         BOOST_CHECK_EQUAL(histories[2].id.instance(), 0u);

         // f(A, 0, 4, 2) = { 1, 0 }
         histories = hist_api.get_account_history("1.2.0", operation_history_id_type(), 4, operation_history_id_type(2));
         BOOST_CHECK_EQUAL(histories.size(), 2u);
         BOOST_CHECK_EQUAL(histories[0].id.instance(), 1u);
         BOOST_CHECK_EQUAL(histories[1].id.instance(), 0u);

         // f(A, 0, 4, 1) = { 1, 0 }
         histories = hist_api.get_account_history("1.2.0", operation_history_id_type(), 4, operation_history_id_type(1));
         BOOST_CHECK_EQUAL(histories.size(), 2u);
         BOOST_CHECK_EQUAL(histories[0].id.instance(), 1u);
         BOOST_CHECK_EQUAL(histories[1].id.instance(), 0u);

         // f(A, 0, 4, 0) = { 5, 3, 1, 0 }
         histories = hist_api.get_account_history("1.2.0", operation_history_id_type(), 4, operation_history_id_type());
         BOOST_CHECK_EQUAL(histories.size(), 4u);
         BOOST_CHECK_EQUAL(histories[0].id.instance(), 5u);
         BOOST_CHECK_EQUAL(histories[1].id.instance(), 3u);
         BOOST_CHECK_EQUAL(histories[2].id.instance(), 1u);
         BOOST_CHECK_EQUAL(histories[3].id.instance(), 0u);

         // f(A, 1, 5, 9) = { 5, 3 }
         histories = hist_api.get_account_history("1.2.0", operation_history_id_type(1), 5, operation_history_id_type(9));
         BOOST_CHECK_EQUAL(histories.size(), 2u);
         BOOST_CHECK_EQUAL(histories[0].id.instance(), 5u);
         BOOST_CHECK_EQUAL(histories[1].id.instance(), 3u);

         // f(A, 1, 5, 6) = { 5, 3 }
         histories = hist_api.get_account_history("1.2.0", operation_history_id_type(1), 5, operation_history_id_type(6));
         BOOST_CHECK_EQUAL(histories.size(), 2u);
         BOOST_CHECK_EQUAL(histories[0].id.instance(), 5u);
         BOOST_CHECK_EQUAL(histories[1].id.instance(), 3u);

         // f(A, 1, 5, 5) = { 5, 3 }
         histories = hist_api.get_account_history("1.2.0", operation_history_id_type(1), 5, operation_history_id_type(5));
         BOOST_CHECK_EQUAL(histories.size(), 2u);
         BOOST_CHECK_EQUAL(histories[0].id.instance(), 5u);
         BOOST_CHECK_EQUAL(histories[1].id.instance(), 3u);

         // f(A, 1, 5, 4) = { 3 }
         histories = hist_api.get_account_history("1.2.0", operation_history_id_type(1), 5, operation_history_id_type(4));
         BOOST_CHECK_EQUAL(histories.size(), 1u);
         BOOST_CHECK_EQUAL(histories[0].id.instance(), 3u);

         // f(A, 1, 5, 3) = { 3 }
         histories = hist_api.get_account_history("1.2.0", operation_history_id_type(1), 5, operation_history_id_type(3));
         BOOST_CHECK_EQUAL(histories.size(), 1u);
         BOOST_CHECK_EQUAL(histories[0].id.instance(), 3u);

         // f(A, 1, 5, 2) = { }
         histories = hist_api.get_account_history("1.2.0", operation_history_id_type(1), 5, operation_history_id_type(2));
         BOOST_CHECK_EQUAL(histories.size(), 0u);

         // f(A, 1, 5, 1) = { }
         histories = hist_api.get_account_history("1.2.0", operation_history_id_type(1), 5, operation_history_id_type(1));
         BOOST_CHECK_EQUAL(histories.size(), 0u);

         // f(A, 1, 5, 0) = { 5, 3 }
         histories = hist_api.get_account_history("1.2.0", operation_history_id_type(1), 5, operation_history_id_type(0));
         BOOST_CHECK_EQUAL(histories.size(), 2u);
         BOOST_CHECK_EQUAL(histories[0].id.instance(), 5u);
         BOOST_CHECK_EQUAL(histories[1].id.instance(), 3u);

         // f(A, 0, 3, 9) = { 5, 3, 1 }
         histories = hist_api.get_account_history("1.2.0", operation_history_id_type(), 3, operation_history_id_type(9));
         BOOST_CHECK_EQUAL(histories.size(), 3u);
         BOOST_CHECK_EQUAL(histories[0].id.instance(), 5u);
         BOOST_CHECK_EQUAL(histories[1].id.instance(), 3u);
         BOOST_CHECK_EQUAL(histories[2].id.instance(), 1u);

         // f(A, 0, 3, 6) = { 5, 3, 1 }
         histories = hist_api.get_account_history("1.2.0", operation_history_id_type(), 3, operation_history_id_type(6));
         BOOST_CHECK_EQUAL(histories.size(), 3u);
         BOOST_CHECK_EQUAL(histories[0].id.instance(), 5u);
         BOOST_CHECK_EQUAL(histories[1].id.instance(), 3u);
         BOOST_CHECK_EQUAL(histories[2].id.instance(), 1u);

         // f(A, 0, 3, 5) = { 5, 3, 1 }
         histories = hist_api.get_account_history("1.2.0", operation_history_id_type(), 3, operation_history_id_type(5));
         BOOST_CHECK_EQUAL(histories.size(), 3u);
         BOOST_CHECK_EQUAL(histories[0].id.instance(), 5u);
         BOOST_CHECK_EQUAL(histories[1].id.instance(), 3u);
         BOOST_CHECK_EQUAL(histories[2].id.instance(), 1u);

         // f(A, 0, 3, 4) = { 3, 1, 0 }
         histories = hist_api.get_account_history("1.2.0", operation_history_id_type(), 3, operation_history_id_type(4));
         BOOST_CHECK_EQUAL(histories.size(), 3u);
         BOOST_CHECK_EQUAL(histories[0].id.instance(), 3u);
         BOOST_CHECK_EQUAL(histories[1].id.instance(), 1u);
         BOOST_CHECK_EQUAL(histories[2].id.instance(), 0u);

         // f(A, 0, 3, 3) = { 3, 1, 0 }
         histories = hist_api.get_account_history("1.2.0", operation_history_id_type(), 3, operation_history_id_type(3));
         BOOST_CHECK_EQUAL(histories.size(), 3u);
         BOOST_CHECK_EQUAL(histories[0].id.instance(), 3u);
         BOOST_CHECK_EQUAL(histories[1].id.instance(), 1u);
         BOOST_CHECK_EQUAL(histories[2].id.instance(), 0u);

         // f(A, 0, 3, 2) = { 1, 0 }
         histories = hist_api.get_account_history("1.2.0", operation_history_id_type(), 3, operation_history_id_type(2));
         BOOST_CHECK_EQUAL(histories.size(), 2u);
         BOOST_CHECK_EQUAL(histories[0].id.instance(), 1u);
         BOOST_CHECK_EQUAL(histories[1].id.instance(), 0u);

         // f(A, 0, 3, 1) = { 1, 0 }
         histories = hist_api.get_account_history("1.2.0", operation_history_id_type(), 3, operation_history_id_type(1));
         BOOST_CHECK_EQUAL(histories.size(), 2u);
         BOOST_CHECK_EQUAL(histories[0].id.instance(), 1u);
         BOOST_CHECK_EQUAL(histories[1].id.instance(), 0u);

         // f(A, 0, 3, 0) = { 5, 3, 1 }
         histories = hist_api.get_account_history("1.2.0", operation_history_id_type(), 3, operation_history_id_type());
         BOOST_CHECK_EQUAL(histories.size(), 3u);
         BOOST_CHECK_EQUAL(histories[0].id.instance(), 5u);
         BOOST_CHECK_EQUAL(histories[1].id.instance(), 3u);
         BOOST_CHECK_EQUAL(histories[2].id.instance(), 1u);

         // f(B, 0, 4, 9) = { 6, 4, 2, 1 }
         histories = hist_api.get_account_history("dan", operation_history_id_type(), 4, operation_history_id_type(9));
         BOOST_CHECK_EQUAL(histories.size(), 4u);
         BOOST_CHECK_EQUAL(histories[0].id.instance(), 6u);
         BOOST_CHECK_EQUAL(histories[1].id.instance(), 4u);
         BOOST_CHECK_EQUAL(histories[2].id.instance(), 2u);
         BOOST_CHECK_EQUAL(histories[3].id.instance(), 1u);

         // f(B, 0, 4, 6) = { 6, 4, 2, 1 }
         histories = hist_api.get_account_history("dan", operation_history_id_type(), 4, operation_history_id_type(6));
         BOOST_CHECK_EQUAL(histories.size(), 4u);
         BOOST_CHECK_EQUAL(histories[0].id.instance(), 6u);
         BOOST_CHECK_EQUAL(histories[1].id.instance(), 4u);
         BOOST_CHECK_EQUAL(histories[2].id.instance(), 2u);
         BOOST_CHECK_EQUAL(histories[3].id.instance(), 1u);

         // f(B, 0, 4, 5) = { 4, 2, 1 }
         histories = hist_api.get_account_history("dan", operation_history_id_type(), 4, operation_history_id_type(5));
         BOOST_CHECK_EQUAL(histories.size(), 3u);
         BOOST_CHECK_EQUAL(histories[0].id.instance(), 4u);
         BOOST_CHECK_EQUAL(histories[1].id.instance(), 2u);
         BOOST_CHECK_EQUAL(histories[2].id.instance(), 1u);

         // f(B, 0, 4, 4) = { 4, 2, 1 }
         histories = hist_api.get_account_history("dan", operation_history_id_type(), 4, operation_history_id_type(4));
         BOOST_CHECK_EQUAL(histories.size(), 3u);
         BOOST_CHECK_EQUAL(histories[0].id.instance(), 4u);
         BOOST_CHECK_EQUAL(histories[1].id.instance(), 2u);
         BOOST_CHECK_EQUAL(histories[2].id.instance(), 1u);

         // f(B, 0, 4, 3) = { 2, 1 }
         histories = hist_api.get_account_history("dan", operation_history_id_type(), 4, operation_history_id_type(3));
         BOOST_CHECK_EQUAL(histories.size(), 2u);
         BOOST_CHECK_EQUAL(histories[0].id.instance(), 2u);
         BOOST_CHECK_EQUAL(histories[1].id.instance(), 1u);

         // f(B, 0, 4, 2) = { 2, 1 }
         histories = hist_api.get_account_history("dan", operation_history_id_type(), 4, operation_history_id_type(2));
         BOOST_CHECK_EQUAL(histories.size(), 2u);
         BOOST_CHECK_EQUAL(histories[0].id.instance(), 2u);
         BOOST_CHECK_EQUAL(histories[1].id.instance(), 1u);

         // f(B, 0, 4, 1) = { 1 }
         histories = hist_api.get_account_history("dan", operation_history_id_type(), 4, operation_history_id_type(1));
         BOOST_CHECK_EQUAL(histories.size(), 1u);
         BOOST_CHECK_EQUAL(histories[0].id.instance(), 1u);

         // f(B, 0, 4, 0) = { 6, 4, 2, 1 }
         histories = hist_api.get_account_history("dan", operation_history_id_type(), 4, operation_history_id_type());
         BOOST_CHECK_EQUAL(histories.size(), 4u);
         BOOST_CHECK_EQUAL(histories[0].id.instance(), 6u);
         BOOST_CHECK_EQUAL(histories[1].id.instance(), 4u);
         BOOST_CHECK_EQUAL(histories[2].id.instance(), 2u);
         BOOST_CHECK_EQUAL(histories[3].id.instance(), 1u);

         // f(B, 2, 4, 9) = { 6, 4 }
         histories = hist_api.get_account_history("dan", operation_history_id_type(2), 4, operation_history_id_type(9));
         BOOST_CHECK_EQUAL(histories.size(), 2u);
         BOOST_CHECK_EQUAL(histories[0].id.instance(), 6u);
         BOOST_CHECK_EQUAL(histories[1].id.instance(), 4u);

         // f(B, 2, 4, 6) = { 6, 4 }
         histories = hist_api.get_account_history("dan", operation_history_id_type(2), 4, operation_history_id_type(6));
         BOOST_CHECK_EQUAL(histories.size(), 2u);
         BOOST_CHECK_EQUAL(histories[0].id.instance(), 6u);
         BOOST_CHECK_EQUAL(histories[1].id.instance(), 4u);

         // f(B, 2, 4, 5) = { 4 }
         histories = hist_api.get_account_history("dan", operation_history_id_type(2), 4, operation_history_id_type(5));
         BOOST_CHECK_EQUAL(histories.size(), 1u);
         BOOST_CHECK_EQUAL(histories[0].id.instance(), 4u);

         // f(B, 2, 4, 4) = { 4 }
         histories = hist_api.get_account_history("dan", operation_history_id_type(2), 4, operation_history_id_type(4));
         BOOST_CHECK_EQUAL(histories.size(), 1u);
         BOOST_CHECK_EQUAL(histories[0].id.instance(), 4u);

         // f(B, 2, 4, 3) = { }
         histories = hist_api.get_account_history("dan", operation_history_id_type(2), 4, operation_history_id_type(3));
         BOOST_CHECK_EQUAL(histories.size(), 0u);

         // f(B, 2, 4, 2) = { }
         histories = hist_api.get_account_history("dan", operation_history_id_type(2), 4, operation_history_id_type(2));
         BOOST_CHECK_EQUAL(histories.size(), 0u);

         // f(B, 2, 4, 1) = { }
         histories = hist_api.get_account_history("dan", operation_history_id_type(2), 4, operation_history_id_type(1));
         BOOST_CHECK_EQUAL(histories.size(), 0u);

         // f(B, 2, 4, 0) = { 6, 4 }
         histories = hist_api.get_account_history("dan", operation_history_id_type(2), 4, operation_history_id_type(0));
         BOOST_CHECK_EQUAL(histories.size(), 2u);
         BOOST_CHECK_EQUAL(histories[0].id.instance(), 6u);
         BOOST_CHECK_EQUAL(histories[1].id.instance(), 4u);

         // 0 limits
         histories = hist_api.get_account_history("dan", operation_history_id_type(0), 0, operation_history_id_type(0));
         BOOST_CHECK_EQUAL(histories.size(), 0u);
         histories = hist_api.get_account_history("1.2.0", operation_history_id_type(3), 0, operation_history_id_type(9));
         BOOST_CHECK_EQUAL(histories.size(), 0u);

         // non existent account
         histories = hist_api.get_account_history("1.2.18", operation_history_id_type(0), 4, operation_history_id_type(0));
         BOOST_CHECK_EQUAL(histories.size(), 0u);

         // create a new account C = alice { 7 }
         create_account("alice");

         generate_block();
         fc::usleep(fc::milliseconds(1000));

         // f(C, 0, 4, 10) = { 7 }
         histories = hist_api.get_account_history("alice", operation_history_id_type(0), 4, operation_history_id_type(10));
         BOOST_CHECK_EQUAL(histories.size(), 1u);
         BOOST_CHECK_EQUAL(histories[0].id.instance(), 7u);

         // f(C, 8, 4, 10) = { }
         histories = hist_api.get_account_history("alice", operation_history_id_type(8), 4, operation_history_id_type(10));
         BOOST_CHECK_EQUAL(histories.size(), 0u);

         // f(A, 0, 10, 0) = { 7, 5, 3, 1, 0 }
         histories = hist_api.get_account_history("1.2.0", operation_history_id_type(0), 10, operation_history_id_type(0));
         BOOST_CHECK_EQUAL(histories.size(), 5u);
         BOOST_CHECK_EQUAL(histories[0].id.instance(), 7u);
         BOOST_CHECK_EQUAL(histories[1].id.instance(), 5u);
         BOOST_CHECK_EQUAL(histories[2].id.instance(), 3u);
         BOOST_CHECK_EQUAL(histories[3].id.instance(), 1u);
         BOOST_CHECK_EQUAL(histories[4].id.instance(), 0u);
      }
   }
   catch (fc::exception &e) {
      edump((e.to_detail_string()));
      throw;
   }
}
BOOST_AUTO_TEST_SUITE_END()
