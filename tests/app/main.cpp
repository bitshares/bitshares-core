/*
 * Copyright (c) 2015 Cryptonomex, Inc., and contributors.
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
#include <graphene/app/application.hpp>
#include <graphene/app/plugin.hpp>
#include <graphene/app/config_util.hpp>

#include <graphene/chain/balance_object.hpp>

#include <graphene/utilities/tempdir.hpp>

#include <graphene/account_history/account_history_plugin.hpp>
#include <graphene/market_history/market_history_plugin.hpp>
#include <graphene/witness/witness.hpp>
#include <graphene/grouped_orders/grouped_orders_plugin.hpp>

#include <fc/thread/thread.hpp>
#include <fc/log/appender.hpp>
#include <fc/log/logger.hpp>
#include <thread>

#include <boost/filesystem/path.hpp>

#include "../../libraries/app/application_impl.hxx"

#define BOOST_TEST_MODULE Test Application
#include <boost/test/included/unit_test.hpp>

#include "../common/genesis_file_util.hpp"

#ifdef _WIN32
   #ifndef _WIN32_WINNT
      #define _WIN32_WINNT 0x0501
   #endif
   #include <winsock2.h>
   #include <WS2tcpip.h>
   int sockInit(void)
   {
      WSADATA wsa_data;
      return WSAStartup(MAKEWORD(1,1), &wsa_data);
   }
   int sockQuit(void)
   {
      return WSACleanup();
   }
#else
   #include <sys/socket.h>
   #include <netinet/ip.h>
   #include <sys/types.h>
#endif

/********
 * @brief attempt to find an available port on localhost
 * @returns an available port number, or -1 on error
 */
int get_available_port()
{
   struct sockaddr_in sin;
   int socket_fd = socket(AF_INET, SOCK_STREAM, 0);
   if (socket_fd == -1)
      return -1;
   sin.sin_family = AF_INET;
   sin.sin_port = 0;
   sin.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
   if (::bind(socket_fd, (struct sockaddr*)&sin, sizeof(struct sockaddr_in)) == -1)
      return -1;
   socklen_t len = sizeof(sin);
   if (getsockname(socket_fd, (struct sockaddr *)&sin, &len) == -1)
      return -1;
#ifdef _WIN32
   closesocket(socket_fd);
#else
   close(socket_fd);
#endif
   return ntohs(sin.sin_port);
}

using namespace graphene;
namespace bpo = boost::program_options;

namespace fc {
   extern std::unordered_map<std::string, logger> &get_logger_map();
   extern std::unordered_map<std::string, appender::ptr> &get_appender_map();
}

BOOST_AUTO_TEST_CASE(load_configuration_options_test_config_logging_files_created)
{
   fc::temp_directory app_dir(graphene::utilities::temp_directory_path());
   auto dir = app_dir.path();
   auto config_ini_file = dir / "config.ini";
   auto logging_ini_file = dir / "logging.ini";

   /// create default config options
   auto node = new app::application();
   bpo::options_description cli, cfg;
   node->set_program_options(cli, cfg);
   bpo::options_description cfg_options("BitShares Witness Node");
   cfg_options.add(cfg);

   /// check preconditions
   BOOST_CHECK(!fc::exists(config_ini_file));
   BOOST_CHECK(!fc::exists(logging_ini_file));

   bpo::variables_map options;
   app::load_configuration_options(dir, cfg_options, options);

   /// check post-conditions
   BOOST_CHECK(fc::exists(config_ini_file));
   BOOST_CHECK(fc::exists(logging_ini_file));
   BOOST_CHECK_GT(fc::file_size(config_ini_file), 0u);
   BOOST_CHECK_GT(fc::file_size(logging_ini_file), 0u);
}

BOOST_AUTO_TEST_CASE(load_configuration_options_test_config_ini_options)
{
   fc::temp_directory app_dir(graphene::utilities::temp_directory_path());
   auto dir = app_dir.path();
   auto config_ini_file = dir / "config.ini";
   auto logging_ini_file = dir / "logging.ini";

   /// create config.ini
   bpo::options_description cfg_options("config.ini options");
   cfg_options.add_options()
   ("option1", bpo::value<std::string>(), "")
   ("option2", bpo::value<int>(), "")
   ;
   std::ofstream out(config_ini_file.preferred_string());
   out << "option1=is present\n"
          "option2=1\n\n";
   out.close();

   /// check preconditions
   BOOST_CHECK(fc::exists(config_ini_file));
   BOOST_CHECK(!fc::exists(logging_ini_file));

   bpo::variables_map options;
   app::load_configuration_options(dir, cfg_options, options);

   /// check the options values are parsed into the output map
   BOOST_CHECK(!options.empty());
   BOOST_CHECK_EQUAL(options.count("option1"), 1u);
   BOOST_CHECK_EQUAL(options.count("option2"), 1u);
   BOOST_CHECK_EQUAL(options["option1"].as<std::string>(), "is present");
   BOOST_CHECK_EQUAL(options["option2"].as<int>(), 1);

   /// when the config.ini exists and doesn't contain logging configuration while the logging.ini doesn't exist
   /// the logging.ini is not created
   BOOST_CHECK(!fc::exists(logging_ini_file));
}

BOOST_AUTO_TEST_CASE(load_configuration_options_test_logging_ini_options)
{
   fc::temp_directory app_dir(graphene::utilities::temp_directory_path());
   auto dir = app_dir.path();
   auto config_ini_file = dir / "config.ini";
   auto logging_ini_file = dir / "logging.ini";

   /// create logging.ini
   /// configure exactly one logger and appender
   std::ofstream out(logging_ini_file.preferred_string());
   out << "[log.file_appender.default]\n"
          "filename=test.log\n\n"
          "[logger.default]\n"
          "level=info\n"
          "appenders=default\n\n"
          ;
   out.close();

   /// clear logger and appender state
   fc::get_logger_map().clear();
   fc::get_appender_map().clear();
   BOOST_CHECK(fc::get_logger_map().empty());
   BOOST_CHECK(fc::get_appender_map().empty());

   bpo::options_description cfg_options("empty");
   bpo::variables_map options;
   app::load_configuration_options(dir, cfg_options, options);

   /// check the options values are parsed into the output map
   /// this is a little bit tricky since load_configuration_options() doesn't provide output variable for logging_config
   auto logger_map = fc::get_logger_map();
   auto appender_map = fc::get_appender_map();
   BOOST_CHECK_EQUAL(logger_map.size(), 1u);
   BOOST_CHECK(logger_map.count("default"));
   BOOST_CHECK_EQUAL(appender_map.size(), 1u);
   BOOST_CHECK(appender_map.count("default"));
}

BOOST_AUTO_TEST_CASE(load_configuration_options_test_legacy_config_ini_options)
{
   fc::temp_directory app_dir(graphene::utilities::temp_directory_path());
   auto dir = app_dir.path();
   auto config_ini_file = dir / "config.ini";
   auto logging_ini_file = dir / "logging.ini";

   /// create config.ini
   bpo::options_description cfg_options("config.ini options");
   cfg_options.add_options()
   ("option1", bpo::value<std::string>(), "")
   ("option2", bpo::value<int>(), "")
   ;
   std::ofstream out(config_ini_file.preferred_string());
   out << "option1=is present\n"
          "option2=1\n\n"
          "[log.file_appender.default]\n"
          "filename=test.log\n\n"
          "[logger.default]\n"
          "level=info\n"
          "appenders=default\n\n"
          ;
   out.close();

   /// clear logger and appender state
   fc::get_logger_map().clear();
   fc::get_appender_map().clear();
   BOOST_CHECK(fc::get_logger_map().empty());
   BOOST_CHECK(fc::get_appender_map().empty());

   bpo::variables_map options;
   app::load_configuration_options(dir, cfg_options, options);

   /// check logging.ini not created
   BOOST_CHECK(!fc::exists(logging_ini_file));

   /// check the options values are parsed into the output map
   BOOST_CHECK(!options.empty());
   BOOST_CHECK_EQUAL(options.count("option1"), 1u);
   BOOST_CHECK_EQUAL(options.count("option2"), 1u);
   BOOST_CHECK_EQUAL(options["option1"].as<std::string>(), "is present");
   BOOST_CHECK_EQUAL(options["option2"].as<int>(), 1);

   auto logger_map = fc::get_logger_map();
   auto appender_map = fc::get_appender_map();
   BOOST_CHECK_EQUAL(logger_map.size(), 1u);
   BOOST_CHECK(logger_map.count("default"));
   BOOST_CHECK_EQUAL(appender_map.size(), 1u);
   BOOST_CHECK(appender_map.count("default"));
}

/////////////
/// @brief create a 2 node network
/////////////
BOOST_AUTO_TEST_CASE( two_node_network )
{
   using namespace graphene::chain;
   using namespace graphene::app;
   try {
      BOOST_TEST_MESSAGE( "Creating and initializing app1" );

      fc::temp_directory app_dir( graphene::utilities::temp_directory_path() );

      graphene::app::application app1;
      std::string app1_ip_and_port = std::string("127.0.0.1:") + std::to_string(get_available_port());
      app1.register_plugin< graphene::account_history::account_history_plugin>();
      app1.register_plugin< graphene::market_history::market_history_plugin >();
      app1.register_plugin< graphene::witness_plugin::witness_plugin >();
      app1.register_plugin< graphene::grouped_orders::grouped_orders_plugin>();
      app1.startup_plugins();
      boost::program_options::variables_map cfg;
      cfg.emplace("p2p-endpoint", boost::program_options::variable_value(app1_ip_and_port, false));
      cfg.emplace("genesis-json", boost::program_options::variable_value(create_genesis_file(app_dir), false));
      cfg.emplace("seed-nodes", boost::program_options::variable_value(string("[]"), false));
      app1.initialize(app_dir.path(), cfg);
      BOOST_TEST_MESSAGE( std::string("Starting app1 at ") + app1_ip_and_port + " and waiting 500 ms" );
      app1.startup();
      fc::usleep(fc::milliseconds(500));

      BOOST_TEST_MESSAGE( "Creating and initializing app2" );

      fc::temp_directory app2_dir( graphene::utilities::temp_directory_path() );
      graphene::app::application app2;
      std::string app2_ip_and_port = std::string("127.0.0.1:") + std::to_string(get_available_port());
      app2.register_plugin<account_history::account_history_plugin>();
      app2.register_plugin< graphene::market_history::market_history_plugin >();
      app2.register_plugin< graphene::witness_plugin::witness_plugin >();
      app2.register_plugin< graphene::grouped_orders::grouped_orders_plugin>();
      app2.startup_plugins();
      auto cfg2 = cfg;
      cfg2.erase("p2p-endpoint");
      cfg2.emplace("p2p-endpoint", boost::program_options::variable_value(app2_ip_and_port, false));
      cfg2.emplace("genesis-json", boost::program_options::variable_value(create_genesis_file(app2_dir), false));
      cfg2.emplace("seed-node", boost::program_options::variable_value(vector<string>{app1_ip_and_port}, false));
      cfg2.emplace("seed-nodes", boost::program_options::variable_value(string("[]"), false));
      app2.initialize(app2_dir.path(), cfg2);

      BOOST_TEST_MESSAGE( std::string("Starting app2 on port ") + app2_ip_and_port + " and waiting 500 ms" );
      app2.startup();
      fc::usleep(fc::milliseconds(500));

      BOOST_REQUIRE_EQUAL(app1.p2p_node()->get_connection_count(), 1u);
      BOOST_CHECK_EQUAL(std::string(app1.p2p_node()->get_connected_peers().front().host.get_address()), "127.0.0.1");
      BOOST_TEST_MESSAGE( "app1 and app2 successfully connected" );

      std::shared_ptr<chain::database> db1 = app1.chain_database();
      std::shared_ptr<chain::database> db2 = app2.chain_database();

      BOOST_CHECK_EQUAL( db1->get_balance( GRAPHENE_NULL_ACCOUNT, asset_id_type() ).amount.value, 0 );
      BOOST_CHECK_EQUAL( db2->get_balance( GRAPHENE_NULL_ACCOUNT, asset_id_type() ).amount.value, 0 );

      BOOST_TEST_MESSAGE( "Creating transfer tx" );
      graphene::chain::precomputable_transaction trx;
      {
         account_id_type nathan_id = db2->get_index_type<account_index>().indices().get<by_name>().find( "nathan" )->id;
         fc::ecc::private_key nathan_key = fc::ecc::private_key::regenerate(fc::sha256::hash(string("nathan")));

         balance_claim_operation claim_op;
         balance_id_type bid = balance_id_type();
         claim_op.deposit_to_account = nathan_id;
         claim_op.balance_to_claim = bid;
         claim_op.balance_owner_key = nathan_key.get_public_key();
         claim_op.total_claimed = bid(*db1).balance;
         trx.operations.push_back( claim_op );
         db1->current_fee_schedule().set_fee( trx.operations.back() );

         transfer_operation xfer_op;
         xfer_op.from = nathan_id;
         xfer_op.to = GRAPHENE_NULL_ACCOUNT;
         xfer_op.amount = asset( 1000000 );
         trx.operations.push_back( xfer_op );
         db1->current_fee_schedule().set_fee( trx.operations.back() );

         trx.set_expiration( db1->get_slot_time( 10 ) );
         trx.sign( nathan_key, db1->get_chain_id() );
         trx.validate();
      }

      BOOST_TEST_MESSAGE( "Pushing tx locally on db1" );
      processed_transaction ptrx = db1->push_transaction(trx);

      BOOST_CHECK_EQUAL( db1->get_balance( GRAPHENE_NULL_ACCOUNT, asset_id_type() ).amount.value, 1000000 );
      BOOST_CHECK_EQUAL( db2->get_balance( GRAPHENE_NULL_ACCOUNT, asset_id_type() ).amount.value, 0 );

      BOOST_TEST_MESSAGE( "Broadcasting tx" );
      app1.p2p_node()->broadcast(graphene::net::trx_message(trx));

      fc::usleep(fc::milliseconds(500));

      BOOST_CHECK_EQUAL( db1->get_balance( GRAPHENE_NULL_ACCOUNT, asset_id_type() ).amount.value, 1000000 );
      BOOST_CHECK_EQUAL( db2->get_balance( GRAPHENE_NULL_ACCOUNT, asset_id_type() ).amount.value, 1000000 );

      BOOST_TEST_MESSAGE( "Generating block on db2" );
      fc::ecc::private_key committee_key = fc::ecc::private_key::regenerate(fc::sha256::hash(string("nathan")));

      auto block_1 = db2->generate_block(
         db2->get_slot_time(1),
         db2->get_scheduled_witness(1),
         committee_key,
         database::skip_nothing);

      BOOST_TEST_MESSAGE( "Broadcasting block" );
      app2.p2p_node()->broadcast(graphene::net::block_message( block_1 ));

      fc::usleep(fc::milliseconds(500));
      BOOST_TEST_MESSAGE( "Verifying nodes are still connected" );
      BOOST_CHECK_EQUAL(app1.p2p_node()->get_connection_count(), 1u);
      BOOST_CHECK_EQUAL(app1.chain_database()->head_block_num(), 1u);

      BOOST_TEST_MESSAGE( "Node 2 sends an unexpected hello message, which should have node1 disconnect him.");
      app2.p2p_node()->broadcast(graphene::net::hello_message() );
      BOOST_TEST_MESSAGE( "Hello message was broadcast. Giving the node some time.");
      std::this_thread::sleep_for(std::chrono::milliseconds(500));
      BOOST_TEST_MESSAGE("Checking to assure that node 2 is not connected to node 1 any longer");
      BOOST_CHECK_EQUAL(app1.p2p_node()->get_connection_count(), 0u);
      BOOST_TEST_MESSAGE("Creating a bunch of connections");

      // a bunch of nodes connect to node 1
      std::vector<graphene::app::application*> test_peers;
      size_t num_nodes = 100;
      for( size_t i = 0; i < num_nodes; ++i )
      {
         BOOST_TEST_MESSAGE( std::string("Starting up test peer ") + std::to_string(i) );
         int new_port = get_available_port();
         fc::temp_directory new_dir( graphene::utilities::temp_directory_path() );
         graphene::app::application* new_app = new graphene::app::application();
         test_peers.push_back(new_app);
         new_app->register_plugin< account_history::account_history_plugin>();
         new_app->register_plugin< graphene::market_history::market_history_plugin >();
         new_app->register_plugin< graphene::witness_plugin::witness_plugin >();
         new_app->register_plugin< graphene::grouped_orders::grouped_orders_plugin>();
         new_app->startup_plugins();
         auto cfg2 = cfg;
         cfg2.erase("p2p-endpoint");
         cfg2.emplace("p2p-endpoint", boost::program_options::variable_value(
               string("127.0.0.1:")+std::to_string(new_port), false));
         cfg2.emplace("genesis-json", boost::program_options::variable_value(create_genesis_file(new_dir), false));
         cfg2.emplace("seed-node", boost::program_options::variable_value(vector<string>{app1_ip_and_port}, false));
         cfg2.emplace("seed-nodes", boost::program_options::variable_value(string("[]"), false));
         new_app->initialize(new_dir.path(), cfg2);
         new_app->startup();
         std::this_thread::sleep_for( std::chrono::milliseconds(50) );
      }

      std::this_thread::sleep_for(std::chrono::milliseconds(100) );
      BOOST_CHECK_EQUAL(app1.p2p_node()->get_connection_count(), num_nodes);

      // now they all send an extra hello message, which should disconnect them
      for(size_t i = 0; i < num_nodes; ++i )
      {
         BOOST_TEST_MESSAGE( std::string("Sending hello_message from peer ") + std::to_string(i) );
         graphene::app::application* new_app = test_peers[i];
         new_app->p2p_node()->broadcast(graphene::net::hello_message() );
         std::this_thread::sleep_for( std::chrono::milliseconds(500) );
      }

      // wait for the dust to settle
      std::this_thread::sleep_for( std::chrono::milliseconds(500) );
      BOOST_CHECK_EQUAL(app1.p2p_node()->get_connection_count(), 0u);

      while(!test_peers.empty() ) delete test_peers.back(), test_peers.pop_back();

   } catch( fc::exception& e ) {
      edump((e.to_detail_string()));
      throw;
   }
}

// a contrived example to test the breaking out of application_impl to a header file

BOOST_AUTO_TEST_CASE(application_impl_breakout) {
   class test_impl : public graphene::app::detail::application_impl {
      // override the constructor, just to test that we can
   public:
      test_impl() : application_impl(nullptr) {}
      bool has_item(const net::item_id& id) override {
         return true;
      }
   };

   test_impl impl;
   graphene::net::item_id id;
   BOOST_CHECK(impl.has_item(id));
}
