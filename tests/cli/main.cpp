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

/*********************
 * Helper Methods
 *********************/

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

/***
 * @brief Start the application, listening on port 8090
 * @param app_dir the temporary directory to use
 * @returns the application object
 */
std::shared_ptr<graphene::app::application> start_application(fc::temp_directory& app_dir) {
	std::shared_ptr<graphene::app::application> app1(new graphene::app::application{});

    app1->register_plugin<graphene::account_history::account_history_plugin>();
    app1->register_plugin< graphene::market_history::market_history_plugin >();
    app1->register_plugin< graphene::witness_plugin::witness_plugin >();
    app1->startup_plugins();
    boost::program_options::variables_map cfg;
    cfg.emplace("rpc-endpoint", boost::program_options::variable_value(string("127.0.0.1:8090"), false));
    cfg.emplace("genesis-json", boost::program_options::variable_value(create_genesis_file(app_dir), false));
    cfg.emplace("seed-nodes", boost::program_options::variable_value(string("[]"), false));
    app1->initialize(app_dir.path(), cfg);

    app1->startup();
    fc::usleep(fc::milliseconds(500));
	return app1;
}

/****
 * Send a block to the db
 * @param app the application
 * @returns true on success
 */
bool generate_block(graphene::app::application& app) {
	try {
	    fc::ecc::private_key committee_key = fc::ecc::private_key::regenerate(fc::sha256::hash(string("nathan")));
	    auto db = app.chain_database();
	    auto block_1 = db->generate_block(
	       db->get_slot_time(1),
	       db->get_scheduled_witness(1),
	       committee_key,
	       database::skip_nothing);
	    return true;
	} catch (exception &e) {
		return false;
	}
}

/****************************
 * Tests
 ****************************/

/**
 * Start a server and connect using the same calls as the CLI
 */
BOOST_AUTO_TEST_CASE( cli_connect )
{
   using namespace graphene::chain;
   using namespace graphene::app;
   std::shared_ptr<graphene::app::application> app1;
   try {
	   fc::temp_directory app_dir ( graphene::utilities::temp_directory_path() );

	   app1 = start_application(app_dir);

      // connect to the server
      graphene::wallet::wallet_data wdata;
      wdata.chain_id = app1->chain_database()->get_chain_id();
      wdata.ws_server = "ws://127.0.0.1:8090";
      wdata.ws_user = "";
      wdata.ws_password = "";
      fc::http::websocket_client client;

      auto apic = std::make_shared<fc::rpc::websocket_api_connection>(*(client.connect(wdata.ws_server)));

      auto remote_api = apic->get_remote_api< login_api >(1);
      BOOST_CHECK(remote_api->login( wdata.ws_user, wdata.ws_password ) );

   } catch( fc::exception& e ) {
      edump((e.to_detail_string()));
      throw;
   }
   app1->shutdown();
}

/**
 * Start a server and connect using the same calls as the CLI
 */
BOOST_AUTO_TEST_CASE( cli_vote_for_2_witnesses )
{
   using namespace graphene::chain;
   using namespace graphene::app;
   std::shared_ptr<graphene::app::application> app1;
   try {

      fc::temp_directory app_dir( graphene::utilities::temp_directory_path() );

      app1 = start_application(app_dir);

      // connect to the server
      graphene::wallet::wallet_data wdata;
      wdata.chain_id = app1->chain_database()->get_chain_id();
      wdata.ws_server = "ws://127.0.0.1:8090";
      wdata.ws_user = "";
      wdata.ws_password = "";
      fc::http::websocket_client client;
      auto con  = client.connect( wdata.ws_server );

      auto apic = std::make_shared<fc::rpc::websocket_api_connection>(*con);

      auto remote_api = apic->get_remote_api< login_api >(1);
      BOOST_CHECK(remote_api->login( wdata.ws_user, wdata.ws_password ) );

      auto wapiptr = std::make_shared<graphene::wallet::wallet_api>(wdata, remote_api);
      std::stringstream wallet_filename;
      wallet_filename << app_dir.path().generic_string() << "/wallet.json";
      wapiptr->set_wallet_filename(wallet_filename.str());

      fc::api<graphene::wallet::wallet_api> wapi(wapiptr);

      auto wallet_cli = std::make_shared<fc::rpc::cli>();
      for( auto& name_formatter : wapiptr->get_result_formatters() )
         wallet_cli->format_result( name_formatter.first, name_formatter.second );

      boost::signals2::scoped_connection closed_connection(con->closed.connect([=]{
         cerr << "Server has disconnected us.\n";
         wallet_cli->stop();
      }));
      (void)(closed_connection);

      BOOST_TEST_MESSAGE("Setting wallet password");
      wapiptr->set_password("supersecret");
      wapiptr->unlock("supersecret");

      // import Nathan account
      BOOST_TEST_MESSAGE("Importing nathan key");
      std::vector<std::string> nathan_keys{"5KQwrPbwdL6PhXujxW37FSSQZ1JiwsST4cqQzDeyXtP79zkvFD3"};
      BOOST_CHECK_EQUAL(nathan_keys[0], "5KQwrPbwdL6PhXujxW37FSSQZ1JiwsST4cqQzDeyXtP79zkvFD3");
      BOOST_CHECK(wapiptr->import_key("nathan", nathan_keys[0]));

      BOOST_TEST_MESSAGE("Importing nathan's balance");
      std::vector<signed_transaction> import_txs = wapiptr->import_balance("nathan", nathan_keys, true);
      account_object nathan_acct_before_upgrade = wapiptr->get_account("nathan");

      // upgrade nathan
      BOOST_TEST_MESSAGE("Upgrading Nathan to LTM");
      signed_transaction upgrade_tx = wapiptr->upgrade_account("nathan", true);
      account_object nathan_acct_after_upgrade = wapiptr->get_account("nathan");

      // verify that the upgrade was successful
      BOOST_CHECK_PREDICATE( std::not_equal_to<uint32_t>(), (nathan_acct_before_upgrade.membership_expiration_date.sec_since_epoch())(nathan_acct_after_upgrade.membership_expiration_date.sec_since_epoch()) );
      BOOST_CHECK(nathan_acct_after_upgrade.is_lifetime_member());

      // create a new account
      graphene::wallet::brain_key_info bki = wapiptr->suggest_brain_key();
      BOOST_CHECK(!bki.brain_priv_key.empty());
      signed_transaction create_acct_tx = wapiptr->create_account_with_brain_key(bki.brain_priv_key, "jmjatlanta", "nathan", "nathan", true);
      // save the private key for this new account in the wallet file
   	  BOOST_CHECK(wapiptr->import_key("jmjatlanta", bki.wif_priv_key));
      wapiptr->save_wallet_file(wallet_filename.str());

      // attempt to give jmjatlanta some bitsahres
      BOOST_TEST_MESSAGE("Transferring bitshares from Nathan to jmjatlanta");
      signed_transaction transfer_tx = wapiptr->transfer("nathan", "jmjatlanta", "10000", "BTS", "Here are some BTS for your new account", true);

      // get the details for init1
      witness_object init1_obj = wapiptr->get_witness("init1");
      int init1_start_votes = init1_obj.total_votes;
      // Vote for a witness
      signed_transaction vote_witness1_tx = wapiptr->vote_for_witness("jmjatlanta", "init1", true, true);

      // wait for a maintenance interval
      // NOTE: For this to work consistently, your maintenance interval must be less than 20 seconds
      // see libraries/chain/include/graphene/chain/config.hpp:L50 GRAPHENE_DEFAULT_MAINTENANCE_INTERVAL (seconds)
      BOOST_CHECK(GRAPHENE_DEFAULT_MAINTENANCE_INTERVAL <= 20);
      fc::usleep(fc::seconds(20));
      // send a block to trigger maintenance interval
      BOOST_CHECK(generate_block(*app1.get()));

      // Verify that the vote is there
      init1_obj = wapiptr->get_witness("init1");
      witness_object init2_obj = wapiptr->get_witness("init2");
      int init1_middle_votes = init1_obj.total_votes;
      BOOST_CHECK(init1_middle_votes > init1_start_votes);

      // Vote for a 2nd witness
      int init2_start_votes = init2_obj.total_votes;
      signed_transaction vote_witness2_tx = wapiptr->vote_for_witness("jmjatlanta", "init2", true, true);

      // send another block to trigger maintenance interval
      fc::usleep(fc::seconds(20));
      BOOST_CHECK(generate_block(*app1.get()));

      // Verify that both the first vote and the 2nd are there
      init2_obj = wapiptr->get_witness("init2");
      init1_obj = wapiptr->get_witness("init1");

      int init2_middle_votes = init2_obj.total_votes;
      BOOST_CHECK(init2_middle_votes > init2_start_votes);
      int init1_last_votes = init1_obj.total_votes;
      BOOST_CHECK(init1_last_votes > init1_start_votes);

      // wait for everything to finish up
      fc::usleep(fc::seconds(1));
   } catch( fc::exception& e ) {
      edump((e.to_detail_string()));
      throw;
   }
   app1->shutdown();
}

/**
 * Start a server and connect using the same calls as the CLI
 */
BOOST_AUTO_TEST_CASE( cli_set_voting_proxy )
{
   using namespace graphene::chain;
   using namespace graphene::app;
   std::shared_ptr<graphene::app::application> app1;
   try {

      fc::temp_directory app_dir( graphene::utilities::temp_directory_path() );

      app1 = start_application(app_dir);

      // connect to the server
      graphene::wallet::wallet_data wdata;
      wdata.chain_id = app1->chain_database()->get_chain_id();
      wdata.ws_server = "ws://127.0.0.1:8090";
      wdata.ws_user = "";
      wdata.ws_password = "";
      fc::http::websocket_client client;
      auto con  = client.connect( wdata.ws_server );

      auto apic = std::make_shared<fc::rpc::websocket_api_connection>(*con);

      auto remote_api = apic->get_remote_api< login_api >(1);
      BOOST_CHECK(remote_api->login( wdata.ws_user, wdata.ws_password ) );

      auto wapiptr = std::make_shared<graphene::wallet::wallet_api>(wdata, remote_api);
      std::stringstream wallet_filename;
      wallet_filename << app_dir.path().generic_string() << "/wallet.json";
      wapiptr->set_wallet_filename(wallet_filename.str());

      fc::api<graphene::wallet::wallet_api> wapi(wapiptr);

      auto wallet_cli = std::make_shared<fc::rpc::cli>();
      for( auto& name_formatter : wapiptr->get_result_formatters() )
         wallet_cli->format_result( name_formatter.first, name_formatter.second );

      boost::signals2::scoped_connection closed_connection(con->closed.connect([=]{
         cerr << "Server has disconnected us.\n";
         wallet_cli->stop();
      }));
      (void)(closed_connection);

      BOOST_TEST_MESSAGE("Setting wallet password");
      wapiptr->set_password("supersecret");
      wapiptr->unlock("supersecret");

      // import Nathan account
      BOOST_TEST_MESSAGE("Importing nathan key");
      std::vector<std::string> nathan_keys{"5KQwrPbwdL6PhXujxW37FSSQZ1JiwsST4cqQzDeyXtP79zkvFD3"};
      BOOST_CHECK_EQUAL(nathan_keys[0], "5KQwrPbwdL6PhXujxW37FSSQZ1JiwsST4cqQzDeyXtP79zkvFD3");
      BOOST_CHECK(wapiptr->import_key("nathan", nathan_keys[0]));

      BOOST_TEST_MESSAGE("Importing nathan's balance");
      std::vector<signed_transaction> import_txs = wapiptr->import_balance("nathan", nathan_keys, true);
      account_object nathan_acct_before_upgrade = wapiptr->get_account("nathan");

      // upgrade nathan
      BOOST_TEST_MESSAGE("Upgrading Nathan to LTM");
      signed_transaction upgrade_tx = wapiptr->upgrade_account("nathan", true);
      account_object nathan_acct_after_upgrade = wapiptr->get_account("nathan");

      // verify that the upgrade was successful
      BOOST_CHECK_PREDICATE( std::not_equal_to<uint32_t>(), (nathan_acct_before_upgrade.membership_expiration_date.sec_since_epoch())(nathan_acct_after_upgrade.membership_expiration_date.sec_since_epoch()) );
      BOOST_CHECK(nathan_acct_after_upgrade.is_lifetime_member());

      // create a new account
      graphene::wallet::brain_key_info bki = wapiptr->suggest_brain_key();
      BOOST_CHECK(!bki.brain_priv_key.empty());
      signed_transaction create_acct_tx = wapiptr->create_account_with_brain_key(bki.brain_priv_key, "jmjatlanta", "nathan", "nathan", true);
      // save the private key for this new account in the wallet file
   	  BOOST_CHECK(wapiptr->import_key("jmjatlanta", bki.wif_priv_key));
      wapiptr->save_wallet_file(wallet_filename.str());

      // attempt to give jmjatlanta some bitsahres
      BOOST_TEST_MESSAGE("Transferring bitshares from Nathan to jmjatlanta");
      signed_transaction transfer_tx = wapiptr->transfer("nathan", "jmjatlanta", "10000", "BTS", "Here are some BTS for your new account", true);

      // set the voting proxy to nathan
      BOOST_TEST_MESSAGE("About to set voting proxy.");
      signed_transaction voting_tx = wapiptr->set_voting_proxy("jmjatlanta", "nathan", true);

      // wait for everything to finish up
      fc::usleep(fc::seconds(1));
   } catch( fc::exception& e ) {
      edump((e.to_detail_string()));
      throw;
   }
   app1->shutdown();
}
