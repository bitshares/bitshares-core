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

#include "../common/database_fixture.hpp"
#include "../../libraries/chain/account_evaluator.cpp"

#define BOOST_TEST_MODULE Elastic Search Database Tests
#include <boost/test/included/unit_test.hpp>

using namespace graphene::chain;
using namespace graphene::chain::test;
using namespace graphene::app;

#include <boost/filesystem.hpp>
#include <fstream>
#include <streambuf>
#include <map>
#include <memory>

namespace fs = boost::filesystem;

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
         auto last_transfer_amount = j["_source"]["additional_data"]["transfer_data"]["amount"].as_string();
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

struct locked_account_finder
{
   const database& db;
   std::unique_ptr<CURL, void(*)(CURL*)> curl;
   const std::string potentially_locked_file = "/potentially_locked.json";
   fs::path current_path;

   mutable std::map<account_id_type, account_object> accounts;

   locked_account_finder(const database& d)
   : db(d)
   , curl(curl_easy_init(), curl_easy_cleanup)
   {
      current_path = fs::current_path();
   }

   time_point_sec head_block_time() const
   {
      return HARDFORK_CYCLED_ACCOUNTS_TIME;
   }

   const global_property_object& get_global_properties() const
   {
      return db.get_global_properties();
   }

   const account_object& get( account_id_type id ) const
   {
      auto it = accounts.find(id);
      if (accounts.end() != it)
      {
         return it->second;
      }

      auto account = get_from_bts(id);
      auto res = accounts.insert(std::make_pair(account.get_id(), account));
      return res.first->second;
   }

   account_object get_from_bts(account_id_type account_id) const
   {
      variant vo;
      to_variant(account_id, vo);
      auto id_str = vo.as_string();

      graphene::utilities::CurlRequest curl_request;
      curl_request.handler = curl.get();
      curl_request.url = "http://127.0.0.1:8092/rpc";
      curl_request.type = "POST";
      curl_request.query = R"({"jsonrpc":"2.0","method":"get_account","params":[")" + id_str + R"("],"id":1})";
      auto response = doCurl(curl_request);

      variant variant_response = fc::json::from_string(response);
      optional<account_object> account;
      fc::from_variant(variant_response["result"], account, FC_PACK_MAX_DEPTH);
      return *account;
   }

   void store_potentially_locked_accounts(const fs::path& file_path) const
   {
      constexpr auto ES_URL = "http://bselastic.dev.aetsoft.by/";
      graphene::utilities::ES es;
      es.curl = curl.get();
      es.elasticsearch_url = ES_URL;
      es.index_prefix = "objects-account";
      es.endpoint = es.index_prefix + "/_search?size=0&pretty=true";
      es.query = R"({"_source": ["object_id"])";
      es.query.append(R"(,"query":{"bool":{"should":[{"bool":{"must_not":{"term":{"active_account_auths.keyword":"[]"}}}})");
      es.query.append(R"(,{"bool":{"must_not":{"term":{"owner_account_auths.keyword":"[]"}}}}]}}})");
      auto res = graphene::utilities::simpleQuery(es);
      variant json_result = fc::json::from_string(res);
      auto count = json_result["hits"]["total"].as_string();
      std::cout << "total:" << count << std::endl;

      es.endpoint = es.index_prefix + "/_search?size=" + count +"&pretty=true";
      res = graphene::utilities::simpleQuery(es);

      // save accounts to file
      std::ofstream file(file_path.string(), std::ofstream::out);
      std::copy(res.begin(),res.end(), std::ostreambuf_iterator<char>(file));
   }

   variant get_potentially_locked(const fs::path& file_path) const
   {
      std::ifstream file(file_path.string());
      std::string data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
      return fc::json::from_string(data);
   }

   void run() const
   {
      auto potentially_locked_path = current_path;
      potentially_locked_path += potentially_locked_file;
      if ( !fs::exists( fs::status(potentially_locked_path) ) )
      {
         store_potentially_locked_accounts(potentially_locked_path);
      }
      auto potentially_locked_accounts = get_potentially_locked(potentially_locked_path);

      const auto total_count =  potentially_locked_accounts["hits"]["total"].as_int64();

      auto locked_path = current_path;
      locked_path += "/locked.txt";
      std::ofstream locked_file(locked_path.string(), std::ofstream::out);

      for(auto i = 0; i < total_count; ++i)
      {
         account_id_type account_id;
         const auto v_account_id = potentially_locked_accounts["hits"]["hits"][size_t(i)]["_source"]["object_id"];
         from_variant(v_account_id, account_id);

         auto account = get(account_id);
         std::cout << "id: " << v_account_id.as_string() << " checked: " << i << " from: " << total_count << std::endl;
         try
         {
            graphene::chain::detail::check_account_authorities(account.get_id(), *this, account.active, account.owner);
         }
         catch(const tx_missing_active_auth &)
         {
            locked_file << v_account_id.as_string() << std::endl;
         }
      }
   }
};

BOOST_AUTO_TEST_CASE(find_locked_accounts) {
   try {

      locked_account_finder finder(db);
      finder.run();
   }
   FC_LOG_AND_RETHROW()
}

BOOST_AUTO_TEST_SUITE_END()
