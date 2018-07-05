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
#include <fc/io/json.hpp>

#include <graphene/utilities/elasticsearch.hpp>

#include "../common/database_fixture.hpp"

#define BOOST_TEST_MODULE Elastic Search Database Tests
#include <boost/test/included/unit_test.hpp>

using namespace graphene::chain;
using namespace graphene::chain::test;
using namespace graphene::app;

BOOST_FIXTURE_TEST_SUITE( elasticsearch_tests, database_fixture )

BOOST_AUTO_TEST_CASE(es1) {
   try {

      CURL *curl; // curl handler
      curl = curl_easy_init();

      graphene::utilities::ES es;
      es.curl = curl;
      es.elasticsearch_url = "http://localhost:9200/";
      es.index_prefix = "bitshares-";
      //es.auth = "elastic:changeme";

      // delete all first
      auto dele = graphene::utilities::deleteAll(es);

      generate_block();
      fc::usleep(fc::milliseconds(1000)); // this is because index.refresh_interval, nothing to worry

      if(dele) { // all records deleted

         //account_id_type() do 3 ops
         create_bitasset("USD", account_id_type());
         auto dan = create_account("dan");
         auto bob = create_account("bob");

         generate_block();
         fc::usleep(fc::milliseconds(1000)); // this is because index.refresh_interval, nothing to worry

         // for later use
         //int asset_create_op_id = operation::tag<asset_create_operation>::value;
         //int account_create_op_id = operation::tag<account_create_operation>::value;

         curl = curl_easy_init();
         string query = "{ \"query\" : { \"bool\" : { \"must\" : [{\"match_all\": {}}] } } }";
         es.endpoint = es.index_prefix + "*/data/_count";
         es.query = query;

         auto res = graphene::utilities::simpleQuery(es);
         variant j = fc::json::from_string(res);
         auto total = j["count"].as_string();
         BOOST_CHECK_EQUAL(total, "5");

         curl = curl_easy_init();
         es.endpoint = es.index_prefix + "*/data/_search";
         res = graphene::utilities::simpleQuery(es);
         j = fc::json::from_string(res);
         auto first_id = j["hits"]["hits"][size_t(0)]["_id"].as_string();
         BOOST_CHECK_EQUAL(first_id, "2.9.1"); // this should be 0? are they inserted in the right order?

         generate_block();
         auto willie = create_account("willie");
         generate_block();

         fc::usleep(fc::milliseconds(1000)); // index.refresh_interval

         curl = curl_easy_init();
         es.endpoint = es.index_prefix + "*/data/_count";
         res = graphene::utilities::simpleQuery(es);
         j = fc::json::from_string(res);

         total = j["count"].as_string();
         BOOST_CHECK_EQUAL(total, "7");

         // do some transfers in 1 block
         transfer(account_id_type()(db), bob, asset(100));
         transfer(account_id_type()(db), bob, asset(100));
         transfer(account_id_type()(db), bob, asset(100));

         generate_block();
         fc::usleep(fc::milliseconds(1000)); // index.refresh_interval

         curl = curl_easy_init();
         res = graphene::utilities::simpleQuery(es);
         j = fc::json::from_string(res);

         total = j["count"].as_string();
         BOOST_CHECK_EQUAL(total, "13");

      }
   }
   catch (fc::exception &e) {
      edump((e.to_detail_string()));
      throw;
   }
}

BOOST_AUTO_TEST_SUITE_END()
