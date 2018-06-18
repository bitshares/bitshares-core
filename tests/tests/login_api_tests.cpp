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

#include <boost/test/unit_test.hpp>
#include <boost/filesystem/path.hpp>

#include <graphene/app/api.hpp>
#include <graphene/utilities/tempdir.hpp>
#include <graphene/account_history/account_history_plugin.hpp>
#include <graphene/market_history/market_history_plugin.hpp>
#include <graphene/witness/witness.hpp>

#include "../common/genesis_file_util.hpp"

#include <fc/crypto/digest.hpp>
#include <iostream>

BOOST_AUTO_TEST_SUITE( login_api_tests )

BOOST_AUTO_TEST_CASE(get_parameters_default) {

   BOOST_TEST_MESSAGE( "get_parameters_default" );

   fc::temp_directory app_dir( graphene::utilities::temp_directory_path() );

   graphene::app::application app1;
   app1.register_plugin< graphene::account_history::account_history_plugin>();
   app1.register_plugin< graphene::market_history::market_history_plugin >();
   app1.register_plugin< graphene::witness_plugin::witness_plugin >();
   app1.register_plugin< graphene::grouped_orders::grouped_orders_plugin>();
   app1.startup_plugins();
   boost::program_options::variables_map cfg;
   cfg.emplace("p2p-endpoint", boost::program_options::variable_value(std::string("127.0.0.1:3939"), false));
   cfg.emplace("genesis-json", boost::program_options::variable_value(create_genesis_file(app_dir), false));
   cfg.emplace("seed-nodes", boost::program_options::variable_value(std::string("[]"), false));
   app1.initialize(app_dir.path(), cfg);
   BOOST_TEST_MESSAGE( "Starting app and waiting 500 ms" );
   app1.startup();
   fc::usleep(fc::milliseconds(500));

   graphene::app::login_api login_api(app1);

   fc::mutable_variant_object server_info = login_api.get_server_information();
   BOOST_CHECK( server_info.size() == 0ul );

}

BOOST_AUTO_TEST_CASE(get_parameters_basic) {

   BOOST_TEST_MESSAGE( "get_parameters_basic" );

   fc::temp_directory app_dir( graphene::utilities::temp_directory_path() );

   graphene::app::application app1;
   app1.register_plugin< graphene::account_history::account_history_plugin>();
   app1.register_plugin< graphene::market_history::market_history_plugin >();
   app1.register_plugin< graphene::witness_plugin::witness_plugin >();
   app1.register_plugin< graphene::grouped_orders::grouped_orders_plugin>();
   app1.startup_plugins();
   boost::program_options::variables_map cfg;
   cfg.emplace("p2p-endpoint", boost::program_options::variable_value(std::string("127.0.0.1:3939"), false));
   cfg.emplace("genesis-json", boost::program_options::variable_value(create_genesis_file(app_dir), false));
   cfg.emplace("seed-nodes", boost::program_options::variable_value(std::string("[]"), false));
   cfg.emplace("share-version-info", boost::program_options::variable_value(std::string("true"), false));
   app1.initialize(app_dir.path(), cfg);
   BOOST_TEST_MESSAGE( "Starting app and waiting 500 ms" );
   app1.startup();
   fc::usleep(fc::milliseconds(500));

   graphene::app::login_api login_api(app1);

   fc::mutable_variant_object server_info = login_api.get_server_information();
   BOOST_CHECK( server_info.size() != 0ul );
   std::string result = fc::json::to_pretty_string( server_info );
   std::cout << result << std::endl;

}

BOOST_AUTO_TEST_CASE(get_parameters_plugins) {

   BOOST_TEST_MESSAGE( "get_parameters_plugins" );

   fc::temp_directory app_dir( graphene::utilities::temp_directory_path() );

   graphene::app::application app1;
   app1.register_plugin< graphene::account_history::account_history_plugin>();
   app1.register_plugin< graphene::market_history::market_history_plugin >();
   app1.register_plugin< graphene::witness_plugin::witness_plugin >();
   app1.register_plugin< graphene::grouped_orders::grouped_orders_plugin>();
   app1.startup_plugins();
   boost::program_options::variables_map cfg;
   cfg.emplace("p2p-endpoint", boost::program_options::variable_value(std::string("127.0.0.1:3939"), false));
   cfg.emplace("genesis-json", boost::program_options::variable_value(create_genesis_file(app_dir), false));
   cfg.emplace("seed-nodes", boost::program_options::variable_value(std::string("[]"), false));
   cfg.emplace("share-plugin-info", boost::program_options::variable_value(std::string("true"), false));
   app1.initialize(app_dir.path(), cfg);
   BOOST_TEST_MESSAGE( "Starting app and waiting 500 ms" );
   app1.startup();
   fc::usleep(fc::milliseconds(500));

   graphene::app::login_api login_api(app1);

   fc::mutable_variant_object server_info = login_api.get_server_information();
   BOOST_CHECK( server_info.size() != 0ul );
   std::string result = fc::json::to_pretty_string( server_info );
   std::cout << result << std::endl;

}

BOOST_AUTO_TEST_CASE(get_parameters_all) {

   BOOST_TEST_MESSAGE( "get_parameters_all" );

   fc::temp_directory app_dir( graphene::utilities::temp_directory_path() );

   graphene::app::application app1;
   app1.register_plugin< graphene::account_history::account_history_plugin>();
   app1.register_plugin< graphene::market_history::market_history_plugin >();
   app1.register_plugin< graphene::witness_plugin::witness_plugin >();
   app1.register_plugin< graphene::grouped_orders::grouped_orders_plugin>();
   app1.startup_plugins();
   boost::program_options::variables_map cfg;
   cfg.emplace("p2p-endpoint", boost::program_options::variable_value(std::string("127.0.0.1:3939"), false));
   cfg.emplace("genesis-json", boost::program_options::variable_value(create_genesis_file(app_dir), false));
   cfg.emplace("seed-nodes", boost::program_options::variable_value(std::string("[]"), false));
   cfg.emplace("share-plugin-info", boost::program_options::variable_value(std::string("true"), false));
   cfg.emplace("share-version-info", boost::program_options::variable_value(std::string("true"), false));
   app1.initialize(app_dir.path(), cfg);
   BOOST_TEST_MESSAGE( "Starting app and waiting 500 ms" );
   app1.startup();
   fc::usleep(fc::milliseconds(500));

   graphene::app::login_api login_api(app1);

   fc::mutable_variant_object server_info = login_api.get_server_information();
   BOOST_CHECK( server_info.size() != 0ul );
   std::string result = fc::json::to_pretty_string( server_info );
   std::cout << result << std::endl;

}

BOOST_AUTO_TEST_SUITE_END()
