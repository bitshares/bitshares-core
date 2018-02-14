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

#include <graphene/chain/balance_object.hpp>
#include <graphene/chain/protocol/transaction.hpp>

#include <graphene/utilities/tempdir.hpp>

#include <graphene/account_history/account_history_plugin.hpp>
#include <graphene/witness/witness.hpp>
#include <graphene/market_history/market_history_plugin.hpp>
#include <graphene/egenesis/egenesis.hpp>
#include <graphene/wallet/wallet.hpp>

#include <fc/thread/thread.hpp>
#include <fc/smart_ref_impl.hpp>
#include <fc/network/http/websocket.hpp>
#include <fc/rpc/websocket_api.hpp>
#include <fc/rpc/cli.hpp>

#include <boost/filesystem/path.hpp>

#define BOOST_TEST_MODULE Test Application
#include <boost/test/included/unit_test.hpp>

using namespace graphene;

// hack:  import create_example_genesis() even though it's a way, way
// specific internal detail
namespace graphene { namespace app { namespace detail {
	graphene::chain::genesis_state_type create_example_genesis();
} } } // graphene::app::detail

boost::filesystem::path create_genesis_file(fc::temp_directory& directory) {
	boost::filesystem::path genesis_path = boost::filesystem::path{directory.path().generic_string()} / "genesis.json";
    fc::path genesis_out = genesis_path;
    graphene::chain::genesis_state_type genesis_state = graphene::app::detail::create_example_genesis();
    std::cerr << "Creating example genesis state in file " << genesis_out.generic_string() << "\n";
    fc::json::save_to_file(genesis_state, genesis_out);
    return genesis_path;
}

/**
 * Start a server and connect using the same calls as the CLI
 */
BOOST_AUTO_TEST_CASE( cli_connect )
{
   using namespace graphene::chain;
   using namespace graphene::app;
   try {
      BOOST_TEST_MESSAGE( "Creating temporary files" );

      fc::temp_directory app_dir( graphene::utilities::temp_directory_path() );

      BOOST_TEST_MESSAGE( "Creating and initializing app1" );

      graphene::app::application app1;
      app1.register_plugin<graphene::account_history::account_history_plugin>();
      app1.register_plugin< graphene::market_history::market_history_plugin >();
      app1.register_plugin< graphene::witness_plugin::witness_plugin >();
      app1.startup_plugins();
      boost::program_options::variables_map cfg;
      cfg.emplace("rpc-endpoint", boost::program_options::variable_value(string("127.0.0.1:8090"), false));
      cfg.emplace("genesis-json", boost::program_options::variable_value(create_genesis_file(app_dir), false));
      app1.initialize(app_dir.path(), cfg);

      BOOST_TEST_MESSAGE( "Starting app1 and waiting 500 ms" );
      app1.startup();
      fc::usleep(fc::milliseconds(500));

      // connect to the server
      graphene::wallet::wallet_data wdata;
      wdata.chain_id = app1.chain_database()->get_chain_id();
      wdata.ws_server = "ws://127.0.0.1:8090";
      wdata.ws_user = "";
      wdata.ws_password = "";
      fc::http::websocket_client client;
      auto con  = client.connect( wdata.ws_server );

      auto apic = std::make_shared<fc::rpc::websocket_api_connection>(*con);

      auto remote_api = apic->get_remote_api< login_api >(1);
      BOOST_CHECK(remote_api->login( wdata.ws_user, wdata.ws_password ) );

      fc::mutable_variant_object settings = remote_api->get_server_information();
      std::cout << "Server Version: " << settings["server_version"].as<std::string>() << std::endl;
      std::cout << "Server SHA Version: " << settings["server_sha_version"].as<std::string>() << std::endl;
      std::cout << "Server Version Timestamp: " << settings["server_version_timestamp"].as<std::string>() << std::endl;
      std::cout << "SSL Version: " << settings["ssl_version"].as<std::string>() << std::endl;
      std::cout << "Boost Version: " << settings["boost_version"].as<std::string>() << std::endl;
      std::cout << "Websocket++ Version: " << settings["websocket_version"].as<std::string>() << std::endl;
      BOOST_CHECK(settings["server_version"].as<std::string>() != "");
      BOOST_CHECK(settings["server_sha_version"].as<std::string>() != "");
      BOOST_CHECK(settings["server_version_timestamp"].as<std::string>() != "");
      BOOST_CHECK(settings["ssl_version"].as<std::string>() != "");
      BOOST_CHECK(settings["boost_version"].as<std::string>() != "");
      BOOST_CHECK(settings["websocket_version"].as<std::string>() != "");
   } catch( fc::exception& e ) {
      edump((e.to_detail_string()));
      throw;
   }
}
