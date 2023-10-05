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

#include <graphene/chain/hardfork.hpp>
#include <graphene/app/api.hpp>
#include <graphene/utilities/tempdir.hpp>
#include <fc/crypto/digest.hpp>

#include <graphene/utilities/elasticsearch.hpp>
#include <graphene/elasticsearch/elasticsearch_plugin.hpp>

#include "../common/init_unit_test_suite.hpp"
#include "../common/database_fixture.hpp"
#include "../common/elasticsearch.hpp"
#include "../common/utils.hpp"

#define ES_WAIT_TIME (fc::milliseconds(10000))

using namespace graphene::chain;
using namespace graphene::chain::test;
using namespace graphene::app;

extern std::string GRAPHENE_TESTING_ES_URL;

BOOST_FIXTURE_TEST_SUITE( elasticsearch_tests, database_fixture )

BOOST_AUTO_TEST_CASE(elasticsearch_account_history) {
   try {

      CURL *curl; // curl handler
      curl = curl_easy_init();
      curl_easy_setopt(curl, CURLOPT_SSLVERSION, CURL_SSLVERSION_TLSv1_2);

      graphene::utilities::ES es;
      es.curl = curl;
      es.elasticsearch_url = GRAPHENE_TESTING_ES_URL;
      es.index_prefix = es_index_prefix;

      // delete all first
      auto delete_account_history = graphene::utilities::deleteAll(es);

      BOOST_REQUIRE(delete_account_history); // require successful deletion
      if(delete_account_history) { // all records deleted

         //account_id_type() do 3 ops
         create_bitasset("USD", account_id_type());
         auto dan = create_account("dan");
         auto bob = create_account("bob");

         generate_block();

         string query = "{ \"query\" : { \"bool\" : { \"must\" : [{\"match_all\": {}}] } } }";
         es.endpoint = es.index_prefix + "*/_count";
         es.query = query;

         string res;
         variant j;
         string total;

         fc::wait_for( ES_WAIT_TIME,  [&]() {
            res = graphene::utilities::simpleQuery(es);
            j = fc::json::from_string(res);
            total = j["count"].as_string();
            return (total == "5");
         });

         es.endpoint = es.index_prefix + "*/_search";
         res = graphene::utilities::simpleQuery(es);
         j = fc::json::from_string(res);
         auto first_id = j["hits"]["hits"][size_t(0)]["_id"].as_string();
         BOOST_CHECK_EQUAL(first_id, "2.9.0");

         generate_block();
         auto willie = create_account("willie");
         generate_block();

         es.endpoint = es.index_prefix + "*/_count";

         fc::wait_for( ES_WAIT_TIME,  [&]() {
            res = graphene::utilities::simpleQuery(es);
            j = fc::json::from_string(res);
            total = j["count"].as_string();
            return (total == "7");
         });

         // do some transfers in 1 block
         transfer(account_id_type()(db), bob, asset(100));
         transfer(account_id_type()(db), bob, asset(200));
         transfer(account_id_type()(db), bob, asset(300));

         generate_block();

         fc::wait_for( ES_WAIT_TIME,  [&]() {
            res = graphene::utilities::simpleQuery(es);
            j = fc::json::from_string(res);
            total = j["count"].as_string();
            return (total == "13");
         });

         // check the visitor data
         auto block_date = db.head_block_time();
         std::string index_name = es_index_prefix + block_date.to_iso_string().substr( 0, 7 ); // yyyy-MM

         es.endpoint = index_name + "/_doc/2.9.12"; // we know last op is a transfer of amount 300
         res = graphene::utilities::getEndPoint(es);
         j = fc::json::from_string(res);
         auto last_transfer_amount = j["_source"]["operation_history"]["op_object"]["amount_"]["amount"].as_string();
         BOOST_CHECK_EQUAL(last_transfer_amount, "300");
         auto last_transfer_payer = j["_source"]["operation_history"]["fee_payer"].as_string();
         BOOST_CHECK_EQUAL(last_transfer_payer, "1.2.0");
         auto is_virtual = j["_source"]["operation_history"]["is_virtual"].as_bool();
         BOOST_CHECK( !is_virtual );

         // To test credit offers
         generate_blocks( HARDFORK_CORE_2362_TIME );
         set_expiration( db, trx );

         ACTORS((sam)(ted)(por));

         auto init_amount = 10000000 * GRAPHENE_BLOCKCHAIN_PRECISION;
         fund( sam, asset(init_amount) );
         fund( ted, asset(init_amount) );

         const asset_object& core = asset_id_type()(db);
         asset_id_type core_id;

         const asset_object& usd = create_user_issued_asset( "MYUSD" );
         asset_id_type usd_id = usd.get_id();
         issue_uia( sam, usd.amount(init_amount) );
         issue_uia( ted, usd.amount(init_amount) );

         const asset_object& eur = create_user_issued_asset( "MYEUR", sam, white_list );
         asset_id_type eur_id = eur.get_id();
         issue_uia( sam, eur.amount(init_amount) );
         issue_uia( ted, eur.amount(init_amount) );

         // propose
         {
            flat_map<asset_id_type, price> collateral_map;
            collateral_map[usd_id] = price( asset(1), asset(1, usd_id) );

            credit_offer_create_operation cop = make_credit_offer_create_op( sam_id, core.get_id(), 10000, 100, 3600,
                                                   0, false, db.head_block_time() + fc::days(1), collateral_map, {} );
            propose( cop );
         }

         // create credit offers
         // 1.
         auto disable_time1 = db.head_block_time() - fc::minutes(1); // a time in the past

         flat_map<asset_id_type, price> collateral_map1;
         collateral_map1[usd_id] = price( asset(1), asset(2, usd_id) );

         const credit_offer_object& coo1 = create_credit_offer( sam_id, core.get_id(), 10000, 100, 3600, 0, false,
                                              disable_time1, collateral_map1, {} );

         BOOST_CHECK( coo1.owner_account == sam_id );
         BOOST_CHECK( coo1.current_balance == 10000 );

         // 2.
         auto duration2 = GRAPHENE_MAX_CREDIT_DEAL_SECS;
         auto disable_time2 = db.head_block_time() + fc::days(GRAPHENE_MAX_CREDIT_OFFER_DAYS);

         flat_map<asset_id_type, price> collateral_map2;
         collateral_map2[core_id] = price( asset(2, usd_id), asset(3) );
         collateral_map2[eur_id] = price( asset(3, usd_id), asset(4, eur_id) );

         flat_map<account_id_type, share_type> borrower_map2;
         borrower_map2[account_id_type()] = 0;
         borrower_map2[sam_id] = 1;
         borrower_map2[ted_id] = GRAPHENE_MAX_SHARE_SUPPLY;

         const credit_offer_object& coo2 = create_credit_offer( ted_id, usd_id, 1, 10000000u, duration2, 10000, true,
                                              disable_time2, collateral_map2, borrower_map2 );
         BOOST_CHECK( coo2.owner_account == ted_id );
         BOOST_CHECK( coo2.asset_type == usd_id );
         BOOST_CHECK( coo2.total_balance == 1 );

         generate_block();

         es.endpoint = es.index_prefix + "*/_count";
         fc::wait_for( ES_WAIT_TIME,  [&]() {
            res = graphene::utilities::simpleQuery(es);
            j = fc::json::from_string(res);
            total = j["count"].as_string();
            return (std::stoi(total) > 13);
         });

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
      curl_easy_setopt(curl, CURLOPT_SSLVERSION, CURL_SSLVERSION_TLSv1_2);

      graphene::utilities::ES es;
      es.curl = curl;
      es.elasticsearch_url = GRAPHENE_TESTING_ES_URL;
      es.index_prefix = es_obj_index_prefix;

      // The head block number is 1
      BOOST_CHECK_EQUAL( db.head_block_num(), 1u );

      generate_blocks( HARDFORK_CORE_2535_TIME ); // For Order-Sends-Take-Profit-Order
      generate_block();
      set_expiration( db, trx );

      // delete all first, this will delete genesis data and data inserted at block 1
      auto delete_objects = graphene::utilities::deleteAll(es);
      BOOST_REQUIRE(delete_objects); // require successful deletion

      generate_block();

      if(delete_objects) { // all records deleted

         // asset and bitasset
         asset_id_type usd_id = create_bitasset("USD", account_id_type()).get_id();
         generate_block();

         string query = "{ \"query\" : { \"bool\" : { \"must\" : [{\"match_all\": {}}] } } }";
         es.endpoint = es.index_prefix + "*/_count";
         es.query = query;

         string res;
         variant j;
         string total;

         fc::wait_for( ES_WAIT_TIME,  [&]() {
            res = graphene::utilities::simpleQuery(es);
            j = fc::json::from_string(res);
            total = j["count"].as_string();
            return (total == "2");
         });

         es.endpoint = es.index_prefix + "asset/_search";
         res = graphene::utilities::simpleQuery(es);
         j = fc::json::from_string(res);
         auto first_id = j["hits"]["hits"][size_t(0)]["_source"]["symbol"].as_string();
         BOOST_CHECK_EQUAL(first_id, "USD");

         auto bitasset_data_id = j["hits"]["hits"][size_t(0)]["_source"]["bitasset_data_id"].as_string();
         es.endpoint = es.index_prefix + "bitasset/_search";
         es.query = "{ \"query\" : { \"bool\": { \"must\" : [{ \"term\": { \"object_id\": \""
                  + bitasset_data_id + "\"}}] } } }";
         res = graphene::utilities::simpleQuery(es);
         j = fc::json::from_string(res);
         auto bitasset_object_id = j["hits"]["hits"][size_t(0)]["_source"]["object_id"].as_string();
         BOOST_CHECK_EQUAL(bitasset_object_id, bitasset_data_id);

         //                                           fee_asset, spread,  size,   expiration, repeat
         create_take_profit_order_action tpa1 { asset_id_type(),    300,  9900,        86400, true };
         vector<limit_order_auto_action> on_fill_1 { tpa1 };
         // create a limit order that expires at the next maintenance time
         create_sell_order( account_id_type(), asset(1), asset(1, usd_id),
                            db.get_dynamic_global_properties().next_maintenance_time,
                            price::unit_price(), on_fill_1 );
         generate_block();

         es.endpoint = es.index_prefix + "limitorder/_count";
         es.query = "";
         fc::wait_for( ES_WAIT_TIME,  [&]() {
            res = graphene::utilities::getEndPoint(es);
            j = fc::json::from_string(res);
            if( !j.is_object() )
               return false;
            const auto& obj = j.get_object();
            if( obj.find("count") == obj.end() )
               return false;
            total = obj["count"].as_string();
            return (total == "1");
         });

         // maintenance, for budget records
         generate_blocks( db.get_dynamic_global_properties().next_maintenance_time );
         generate_block();

         es.endpoint = es.index_prefix + "budget/_count";
         es.query = "";
         fc::wait_for( ES_WAIT_TIME,  [&]() {
            res = graphene::utilities::getEndPoint(es);
            j = fc::json::from_string(res);
            if( !j.is_object() )
               return false;
            const auto& obj = j.get_object();
            if( obj.find("count") == obj.end() )
               return false;
            total = obj["count"].as_string();
            return (total == "1"); // new record inserted at the first maintenance block
         });

         es.endpoint = es.index_prefix + "limitorder/_count";
         es.query = "";
         fc::wait_for( ES_WAIT_TIME,  [&]() {
            res = graphene::utilities::getEndPoint(es);
            j = fc::json::from_string(res);
            if( !j.is_object() )
               return false;
            const auto& obj = j.get_object();
            if( obj.find("count") == obj.end() )
               return false;
            total = obj["count"].as_string();
            return (total == "0"); // the limit order expired, so the object is removed
         });

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
      curl_easy_setopt(curl, CURLOPT_SSLVERSION, CURL_SSLVERSION_TLSv1_2);

      graphene::utilities::ES es;
      es.curl = curl;
      es.elasticsearch_url = GRAPHENE_TESTING_ES_URL;
      es.index_prefix = es_index_prefix;

      generate_blocks( HARDFORK_CORE_2535_TIME ); // For Order-Sends-Take-Profit-Order
      generate_block();
      set_expiration( db, trx );

      auto delete_account_history = graphene::utilities::deleteAll(es);
      BOOST_REQUIRE(delete_account_history); // require successful deletion

      generate_block();

      if(delete_account_history) {

         create_bitasset("USD", account_id_type()); // create op 0
         const account_object& dan = create_account("dan"); // create op 1
         create_bitasset("CNY", dan.get_id()); // create op 2
         create_bitasset("BTC", account_id_type()); // create op 3
         create_bitasset("XMR", dan.get_id()); // create op 4
         create_bitasset("EUR", account_id_type()); // create op 5
         create_bitasset("OIL", dan.get_id()); // create op 6

         generate_block();

         // Test history APIs
         graphene::app::history_api hist_api(app);

         // f(A, 0, 4, 9) = { 5, 3, 1, 0 }
         auto histories = hist_api.get_account_history(
               "1.2.0", operation_history_id_type(), 4, operation_history_id_type(9));

         fc::wait_for( ES_WAIT_TIME,  [&]() {
            histories = hist_api.get_account_history(
                  "1.2.0", operation_history_id_type(), 4, operation_history_id_type(9));
            return (histories.size() == 4u);
         });
         BOOST_REQUIRE_EQUAL(histories.size(), 4u);
         BOOST_CHECK_EQUAL(histories[0].id.instance(), 5u);
         BOOST_CHECK_EQUAL(histories[1].id.instance(), 3u);
         BOOST_CHECK_EQUAL(histories[2].id.instance(), 1u);
         BOOST_CHECK_EQUAL(histories[3].id.instance(), 0u);

         BOOST_CHECK( !histories[0].is_virtual );
         BOOST_CHECK( histories[0].block_time == db.head_block_time() );

         // f(A, 0, 4, 6) = { 5, 3, 1, 0 }
         histories = hist_api.get_account_history("1.2.0", operation_history_id_type(), 4, operation_history_id_type(6));
         BOOST_REQUIRE_EQUAL(histories.size(), 4u);
         BOOST_CHECK_EQUAL(histories[0].id.instance(), 5u);
         BOOST_CHECK_EQUAL(histories[1].id.instance(), 3u);
         BOOST_CHECK_EQUAL(histories[2].id.instance(), 1u);
         BOOST_CHECK_EQUAL(histories[3].id.instance(), 0u);

         // f(A, 0, 4, 5) = { 5, 3, 1, 0 }
         histories = hist_api.get_account_history("1.2.0", operation_history_id_type(), 4, operation_history_id_type(5));
         BOOST_REQUIRE_EQUAL(histories.size(), 4u);
         BOOST_CHECK_EQUAL(histories[0].id.instance(), 5u);
         BOOST_CHECK_EQUAL(histories[1].id.instance(), 3u);
         BOOST_CHECK_EQUAL(histories[2].id.instance(), 1u);
         BOOST_CHECK_EQUAL(histories[3].id.instance(), 0u);

         // f(A, 0, 4, 4) = { 3, 1, 0 }
         histories = hist_api.get_account_history("1.2.0", operation_history_id_type(), 4, operation_history_id_type(4));
         BOOST_REQUIRE_EQUAL(histories.size(), 3u);
         BOOST_CHECK_EQUAL(histories[0].id.instance(), 3u);
         BOOST_CHECK_EQUAL(histories[1].id.instance(), 1u);
         BOOST_CHECK_EQUAL(histories[2].id.instance(), 0u);

         // f(A, 0, 4, 3) = { 3, 1, 0 }
         histories = hist_api.get_account_history("1.2.0", operation_history_id_type(), 4, operation_history_id_type(3));
         BOOST_REQUIRE_EQUAL(histories.size(), 3u);
         BOOST_CHECK_EQUAL(histories[0].id.instance(), 3u);
         BOOST_CHECK_EQUAL(histories[1].id.instance(), 1u);
         BOOST_CHECK_EQUAL(histories[2].id.instance(), 0u);

         // f(A, 0, 4, 2) = { 1, 0 }
         histories = hist_api.get_account_history("1.2.0", operation_history_id_type(), 4, operation_history_id_type(2));
         BOOST_REQUIRE_EQUAL(histories.size(), 2u);
         BOOST_CHECK_EQUAL(histories[0].id.instance(), 1u);
         BOOST_CHECK_EQUAL(histories[1].id.instance(), 0u);

         // f(A, 0, 4, 1) = { 1, 0 }
         histories = hist_api.get_account_history("1.2.0", operation_history_id_type(), 4, operation_history_id_type(1));
         BOOST_REQUIRE_EQUAL(histories.size(), 2u);
         BOOST_CHECK_EQUAL(histories[0].id.instance(), 1u);
         BOOST_CHECK_EQUAL(histories[1].id.instance(), 0u);

         // f(A, 0, 4, 0) = { 5, 3, 1, 0 }
         histories = hist_api.get_account_history("1.2.0", operation_history_id_type(), 4, operation_history_id_type());
         BOOST_REQUIRE_EQUAL(histories.size(), 4u);
         BOOST_CHECK_EQUAL(histories[0].id.instance(), 5u);
         BOOST_CHECK_EQUAL(histories[1].id.instance(), 3u);
         BOOST_CHECK_EQUAL(histories[2].id.instance(), 1u);
         BOOST_CHECK_EQUAL(histories[3].id.instance(), 0u);

         // f(A, 1, 5, 9) = { 5, 3 }
         histories = hist_api.get_account_history("1.2.0", operation_history_id_type(1), 5, operation_history_id_type(9));
         BOOST_REQUIRE_EQUAL(histories.size(), 2u);
         BOOST_CHECK_EQUAL(histories[0].id.instance(), 5u);
         BOOST_CHECK_EQUAL(histories[1].id.instance(), 3u);

         // f(A, 1, 5, 6) = { 5, 3 }
         histories = hist_api.get_account_history("1.2.0", operation_history_id_type(1), 5, operation_history_id_type(6));
         BOOST_REQUIRE_EQUAL(histories.size(), 2u);
         BOOST_CHECK_EQUAL(histories[0].id.instance(), 5u);
         BOOST_CHECK_EQUAL(histories[1].id.instance(), 3u);

         // f(A, 1, 5, 5) = { 5, 3 }
         histories = hist_api.get_account_history("1.2.0", operation_history_id_type(1), 5, operation_history_id_type(5));
         BOOST_REQUIRE_EQUAL(histories.size(), 2u);
         BOOST_CHECK_EQUAL(histories[0].id.instance(), 5u);
         BOOST_CHECK_EQUAL(histories[1].id.instance(), 3u);

         // f(A, 1, 5, 4) = { 3 }
         histories = hist_api.get_account_history("1.2.0", operation_history_id_type(1), 5, operation_history_id_type(4));
         BOOST_REQUIRE_EQUAL(histories.size(), 1u);
         BOOST_CHECK_EQUAL(histories[0].id.instance(), 3u);

         // f(A, 1, 5, 3) = { 3 }
         histories = hist_api.get_account_history("1.2.0", operation_history_id_type(1), 5, operation_history_id_type(3));
         BOOST_REQUIRE_EQUAL(histories.size(), 1u);
         BOOST_CHECK_EQUAL(histories[0].id.instance(), 3u);

         // f(A, 1, 5, 2) = { }
         histories = hist_api.get_account_history("1.2.0", operation_history_id_type(1), 5, operation_history_id_type(2));
         BOOST_CHECK_EQUAL(histories.size(), 0u);

         // f(A, 1, 5, 1) = { }
         histories = hist_api.get_account_history("1.2.0", operation_history_id_type(1), 5, operation_history_id_type(1));
         BOOST_CHECK_EQUAL(histories.size(), 0u);

         // f(A, 1, 5, 0) = { 5, 3 }
         histories = hist_api.get_account_history("1.2.0", operation_history_id_type(1), 5, operation_history_id_type(0));
         BOOST_REQUIRE_EQUAL(histories.size(), 2u);
         BOOST_CHECK_EQUAL(histories[0].id.instance(), 5u);
         BOOST_CHECK_EQUAL(histories[1].id.instance(), 3u);

         // f(A, 0, 3, 9) = { 5, 3, 1 }
         histories = hist_api.get_account_history("1.2.0", operation_history_id_type(), 3, operation_history_id_type(9));
         BOOST_REQUIRE_EQUAL(histories.size(), 3u);
         BOOST_CHECK_EQUAL(histories[0].id.instance(), 5u);
         BOOST_CHECK_EQUAL(histories[1].id.instance(), 3u);
         BOOST_CHECK_EQUAL(histories[2].id.instance(), 1u);

         // f(A, 0, 3, 6) = { 5, 3, 1 }
         histories = hist_api.get_account_history("1.2.0", operation_history_id_type(), 3, operation_history_id_type(6));
         BOOST_REQUIRE_EQUAL(histories.size(), 3u);
         BOOST_CHECK_EQUAL(histories[0].id.instance(), 5u);
         BOOST_CHECK_EQUAL(histories[1].id.instance(), 3u);
         BOOST_CHECK_EQUAL(histories[2].id.instance(), 1u);

         // f(A, 0, 3, 5) = { 5, 3, 1 }
         histories = hist_api.get_account_history("1.2.0", operation_history_id_type(), 3, operation_history_id_type(5));
         BOOST_REQUIRE_EQUAL(histories.size(), 3u);
         BOOST_CHECK_EQUAL(histories[0].id.instance(), 5u);
         BOOST_CHECK_EQUAL(histories[1].id.instance(), 3u);
         BOOST_CHECK_EQUAL(histories[2].id.instance(), 1u);

         // f(A, 0, 3, 4) = { 3, 1, 0 }
         histories = hist_api.get_account_history("1.2.0", operation_history_id_type(), 3, operation_history_id_type(4));
         BOOST_REQUIRE_EQUAL(histories.size(), 3u);
         BOOST_CHECK_EQUAL(histories[0].id.instance(), 3u);
         BOOST_CHECK_EQUAL(histories[1].id.instance(), 1u);
         BOOST_CHECK_EQUAL(histories[2].id.instance(), 0u);

         // f(A, 0, 3, 3) = { 3, 1, 0 }
         histories = hist_api.get_account_history("1.2.0", operation_history_id_type(), 3, operation_history_id_type(3));
         BOOST_REQUIRE_EQUAL(histories.size(), 3u);
         BOOST_CHECK_EQUAL(histories[0].id.instance(), 3u);
         BOOST_CHECK_EQUAL(histories[1].id.instance(), 1u);
         BOOST_CHECK_EQUAL(histories[2].id.instance(), 0u);

         // f(A, 0, 3, 2) = { 1, 0 }
         histories = hist_api.get_account_history("1.2.0", operation_history_id_type(), 3, operation_history_id_type(2));
         BOOST_REQUIRE_EQUAL(histories.size(), 2u);
         BOOST_CHECK_EQUAL(histories[0].id.instance(), 1u);
         BOOST_CHECK_EQUAL(histories[1].id.instance(), 0u);

         // f(A, 0, 3, 1) = { 1, 0 }
         histories = hist_api.get_account_history("1.2.0", operation_history_id_type(), 3, operation_history_id_type(1));
         BOOST_REQUIRE_EQUAL(histories.size(), 2u);
         BOOST_CHECK_EQUAL(histories[0].id.instance(), 1u);
         BOOST_CHECK_EQUAL(histories[1].id.instance(), 0u);

         // f(A, 0, 3, 0) = { 5, 3, 1 }
         histories = hist_api.get_account_history("1.2.0", operation_history_id_type(), 3, operation_history_id_type());
         BOOST_REQUIRE_EQUAL(histories.size(), 3u);
         BOOST_CHECK_EQUAL(histories[0].id.instance(), 5u);
         BOOST_CHECK_EQUAL(histories[1].id.instance(), 3u);
         BOOST_CHECK_EQUAL(histories[2].id.instance(), 1u);

         // f(B, 0, 4, 9) = { 6, 4, 2, 1 }
         histories = hist_api.get_account_history("dan", operation_history_id_type(), 4, operation_history_id_type(9));
         BOOST_REQUIRE_EQUAL(histories.size(), 4u);
         BOOST_CHECK_EQUAL(histories[0].id.instance(), 6u);
         BOOST_CHECK_EQUAL(histories[1].id.instance(), 4u);
         BOOST_CHECK_EQUAL(histories[2].id.instance(), 2u);
         BOOST_CHECK_EQUAL(histories[3].id.instance(), 1u);

         // f(B, 0, 4, 6) = { 6, 4, 2, 1 }
         histories = hist_api.get_account_history("dan", operation_history_id_type(), 4, operation_history_id_type(6));
         BOOST_REQUIRE_EQUAL(histories.size(), 4u);
         BOOST_CHECK_EQUAL(histories[0].id.instance(), 6u);
         BOOST_CHECK_EQUAL(histories[1].id.instance(), 4u);
         BOOST_CHECK_EQUAL(histories[2].id.instance(), 2u);
         BOOST_CHECK_EQUAL(histories[3].id.instance(), 1u);

         // f(B, 0, 4, 5) = { 4, 2, 1 }
         histories = hist_api.get_account_history("dan", operation_history_id_type(), 4, operation_history_id_type(5));
         BOOST_REQUIRE_EQUAL(histories.size(), 3u);
         BOOST_CHECK_EQUAL(histories[0].id.instance(), 4u);
         BOOST_CHECK_EQUAL(histories[1].id.instance(), 2u);
         BOOST_CHECK_EQUAL(histories[2].id.instance(), 1u);

         // f(B, 0, 4, 4) = { 4, 2, 1 }
         histories = hist_api.get_account_history("dan", operation_history_id_type(), 4, operation_history_id_type(4));
         BOOST_REQUIRE_EQUAL(histories.size(), 3u);
         BOOST_CHECK_EQUAL(histories[0].id.instance(), 4u);
         BOOST_CHECK_EQUAL(histories[1].id.instance(), 2u);
         BOOST_CHECK_EQUAL(histories[2].id.instance(), 1u);

         // f(B, 0, 4, 3) = { 2, 1 }
         histories = hist_api.get_account_history("dan", operation_history_id_type(), 4, operation_history_id_type(3));
         BOOST_REQUIRE_EQUAL(histories.size(), 2u);
         BOOST_CHECK_EQUAL(histories[0].id.instance(), 2u);
         BOOST_CHECK_EQUAL(histories[1].id.instance(), 1u);

         // f(B, 0, 4, 2) = { 2, 1 }
         histories = hist_api.get_account_history("dan", operation_history_id_type(), 4, operation_history_id_type(2));
         BOOST_REQUIRE_EQUAL(histories.size(), 2u);
         BOOST_CHECK_EQUAL(histories[0].id.instance(), 2u);
         BOOST_CHECK_EQUAL(histories[1].id.instance(), 1u);

         // f(B, 0, 4, 1) = { 1 }
         histories = hist_api.get_account_history("dan", operation_history_id_type(), 4, operation_history_id_type(1));
         BOOST_REQUIRE_EQUAL(histories.size(), 1u);
         BOOST_CHECK_EQUAL(histories[0].id.instance(), 1u);

         // f(B, 0, 4, 0) = { 6, 4, 2, 1 }
         histories = hist_api.get_account_history("dan", operation_history_id_type(), 4, operation_history_id_type());
         BOOST_REQUIRE_EQUAL(histories.size(), 4u);
         BOOST_CHECK_EQUAL(histories[0].id.instance(), 6u);
         BOOST_CHECK_EQUAL(histories[1].id.instance(), 4u);
         BOOST_CHECK_EQUAL(histories[2].id.instance(), 2u);
         BOOST_CHECK_EQUAL(histories[3].id.instance(), 1u);

         // f(B, 2, 4, 9) = { 6, 4 }
         histories = hist_api.get_account_history("dan", operation_history_id_type(2), 4, operation_history_id_type(9));
         BOOST_REQUIRE_EQUAL(histories.size(), 2u);
         BOOST_CHECK_EQUAL(histories[0].id.instance(), 6u);
         BOOST_CHECK_EQUAL(histories[1].id.instance(), 4u);

         // f(B, 2, 4, 6) = { 6, 4 }
         histories = hist_api.get_account_history("dan", operation_history_id_type(2), 4, operation_history_id_type(6));
         BOOST_REQUIRE_EQUAL(histories.size(), 2u);
         BOOST_CHECK_EQUAL(histories[0].id.instance(), 6u);
         BOOST_CHECK_EQUAL(histories[1].id.instance(), 4u);

         // f(B, 2, 4, 5) = { 4 }
         histories = hist_api.get_account_history("dan", operation_history_id_type(2), 4, operation_history_id_type(5));
         BOOST_REQUIRE_EQUAL(histories.size(), 1u);
         BOOST_CHECK_EQUAL(histories[0].id.instance(), 4u);

         // f(B, 2, 4, 4) = { 4 }
         histories = hist_api.get_account_history("dan", operation_history_id_type(2), 4, operation_history_id_type(4));
         BOOST_REQUIRE_EQUAL(histories.size(), 1u);
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
         BOOST_REQUIRE_EQUAL(histories.size(), 2u);
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
         auto alice = create_account("alice");
         account_id_type alice_id = alice.get_id();

         generate_block();

         // f(C, 0, 4, 10) = { 7 }
         fc::wait_for( ES_WAIT_TIME,  [&]() {
            histories = hist_api.get_account_history(
                  "alice", operation_history_id_type(0), 4, operation_history_id_type(10));
            return (histories.size() == 1u);
         });
         BOOST_REQUIRE_EQUAL(histories.size(), 1u);
         BOOST_CHECK_EQUAL(histories[0].id.instance(), 7u);

         // f(C, 8, 4, 10) = { }
         histories = hist_api.get_account_history("alice", operation_history_id_type(8), 4, operation_history_id_type(10));
         BOOST_CHECK_EQUAL(histories.size(), 0u);

         // f(A, 0, 10, 0) = { 7, 5, 3, 1, 0 }
         histories = hist_api.get_account_history("1.2.0", operation_history_id_type(0), 10, operation_history_id_type(0));
         BOOST_REQUIRE_EQUAL(histories.size(), 5u);
         BOOST_CHECK_EQUAL(histories[0].id.instance(), 7u);
         BOOST_CHECK_EQUAL(histories[1].id.instance(), 5u);
         BOOST_CHECK_EQUAL(histories[2].id.instance(), 3u);
         BOOST_CHECK_EQUAL(histories[3].id.instance(), 1u);
         BOOST_CHECK_EQUAL(histories[4].id.instance(), 0u);

         // Ugly test to cover elasticsearch_plugin::get_operation_by_id()
         if( !app.elasticsearch_thread )
            app.elasticsearch_thread = std::make_shared<fc::thread>("elasticsearch");
         auto es_plugin = app.get_plugin< graphene::elasticsearch::elasticsearch_plugin >("elasticsearch");
         auto his_obj7 = app.elasticsearch_thread->async([&es_plugin]() {
            return es_plugin->get_operation_by_id( operation_history_id_type(7) );
         }, "thread invoke for method " BOOST_PP_STRINGIZE(method_name)).wait();
         BOOST_REQUIRE( his_obj7.op.is_type<account_create_operation>() );
         BOOST_CHECK_EQUAL( his_obj7.op.get<account_create_operation>().name, "alice" );

         // Test virtual operation

         // Prepare funds
         transfer( account_id_type()(db), alice_id(db), asset(100) );
         //                                           fee_asset, spread,  size,   expiration, repeat
         create_take_profit_order_action tpa1 { asset_id_type(),    100, 10000,        86400, false };
         vector<limit_order_auto_action> on_fill_1 { tpa1 };
         // Create a limit order that expires in 300 seconds
         create_sell_order( alice_id, asset(1), asset(1, asset_id_type(1)), db.head_block_time() + 300,
                            price::unit_price(), on_fill_1 );

         generate_block();

         // f(C, 0, 4, 0) = { 9, 8, 7 }
         fc::wait_for( ES_WAIT_TIME,  [&]() {
            histories = hist_api.get_account_history(
                  "alice", operation_history_id_type(0), 4, operation_history_id_type(0));
            return (histories.size() == 3u);
         });
         BOOST_REQUIRE_EQUAL(histories.size(), 3u);
         BOOST_CHECK( histories[0].op.is_type<limit_order_create_operation>() );
         BOOST_CHECK( !histories[0].is_virtual );
         BOOST_CHECK( histories[0].block_time == db.head_block_time() );
         BOOST_CHECK( histories[1].op.is_type<transfer_operation>() );
         BOOST_CHECK( !histories[1].is_virtual );

         // Let the limit order expire
         generate_blocks( db.head_block_time() + 300 );
         generate_block();

         // f(C, 0, 4, 0) = { 10, 9, 8, 7 }
         fc::wait_for( ES_WAIT_TIME,  [&]() {
            histories = hist_api.get_account_history(
                  "alice", operation_history_id_type(0), 4, operation_history_id_type(0));
            return (histories.size() == 4u);
         });
         BOOST_REQUIRE_EQUAL(histories.size(), 4u);
         BOOST_CHECK( histories[0].op.is_type<limit_order_cancel_operation>() );
         BOOST_CHECK( histories[0].is_virtual );
         BOOST_CHECK( histories[1].op.is_type<limit_order_create_operation>() );
         BOOST_CHECK( !histories[1].is_virtual );
         BOOST_CHECK( histories[2].op.is_type<transfer_operation>() );
         BOOST_CHECK( !histories[2].is_virtual );

      }
   }
   catch (fc::exception &e) {
      edump((e.to_detail_string()));
      throw;
   }
}
BOOST_AUTO_TEST_SUITE_END()
