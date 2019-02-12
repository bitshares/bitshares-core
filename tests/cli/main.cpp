/*
 * Copyright (c) 2018 John Jones, and contributors.
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
#include <fc/network/http/websocket.hpp>
#include <fc/rpc/websocket_api.hpp>
#include <fc/rpc/cli.hpp>
#include <fc/crypto/base58.hpp>

#ifdef _WIN32
   #ifndef _WIN32_WINNT
      #define _WIN32_WINNT 0x0501
   #endif
   #include <winsock2.h>
   #include <WS2tcpip.h>
#else
   #include <sys/socket.h>
   #include <netinet/ip.h>
   #include <sys/types.h>
#endif
#include <thread>

#include <boost/filesystem/path.hpp>

#define BOOST_TEST_MODULE Test Application
#include <boost/test/included/unit_test.hpp>

/*****
 * Global Initialization for Windows
 * ( sets up Winsock stuf )
 */
#ifdef _WIN32
int sockInit(void)
{
   WSADATA wsa_data;
   return WSAStartup(MAKEWORD(1,1), &wsa_data);
}
int sockQuit(void)
{
   return WSACleanup();
}
#endif

/*********************
 * Helper Methods
 *********************/

#include "../common/genesis_file_util.hpp"

#define INVOKE(test) ((struct test*)this)->test_method();

//////
/// @brief attempt to find an available port on localhost
/// @returns an available port number, or -1 on error
/////
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

///////////
/// @brief Start the application
/// @param app_dir the temporary directory to use
/// @param server_port_number to be filled with the rpc endpoint port number
/// @returns the application object
//////////
std::shared_ptr<graphene::app::application> start_application(fc::temp_directory& app_dir, int& server_port_number) {
   std::shared_ptr<graphene::app::application> app1(new graphene::app::application{});

   app1->register_plugin<graphene::account_history::account_history_plugin>(true);
   app1->register_plugin< graphene::market_history::market_history_plugin >(true);
   app1->register_plugin< graphene::witness_plugin::witness_plugin >(true);
   app1->register_plugin< graphene::grouped_orders::grouped_orders_plugin>(true);
   app1->startup_plugins();
   boost::program_options::variables_map cfg;
#ifdef _WIN32
   sockInit();
#endif
   server_port_number = get_available_port();
   cfg.emplace(
      "rpc-endpoint", 
      boost::program_options::variable_value(string("127.0.0.1:" + std::to_string(server_port_number)), false)
   );
   cfg.emplace("genesis-json", boost::program_options::variable_value(create_genesis_file(app_dir), false));
   cfg.emplace("seed-nodes", boost::program_options::variable_value(string("[]"), false));
   app1->initialize(app_dir.path(), cfg);

   app1->initialize_plugins(cfg);
   app1->startup_plugins();

   app1->startup();
   fc::usleep(fc::milliseconds(500));
   return app1;
}

///////////
/// Send a block to the db
/// @param app the application
/// @param returned_block the signed block
/// @returns true on success
///////////
bool generate_block(std::shared_ptr<graphene::app::application> app, graphene::chain::signed_block& returned_block) 
{
   try {
      fc::ecc::private_key committee_key = fc::ecc::private_key::regenerate(fc::sha256::hash(string("nathan")));
      auto db = app->chain_database();
      returned_block = db->generate_block( db->get_slot_time(1),
                                         db->get_scheduled_witness(1),
                                         committee_key,
                                         database::skip_nothing );
      return true;
   } catch (exception &e) {
      return false;
   }
}

bool generate_block(std::shared_ptr<graphene::app::application> app) 
{
   graphene::chain::signed_block returned_block;
   return generate_block(app, returned_block);
}

///////////
/// @brief Skip intermediate blocks, and generate a maintenance block
/// @param app the application
/// @returns true on success
///////////
bool generate_maintenance_block(std::shared_ptr<graphene::app::application> app) {
   try {
      fc::ecc::private_key committee_key = fc::ecc::private_key::regenerate(fc::sha256::hash(string("nathan")));
      uint32_t skip = ~0;
      auto db = app->chain_database();
      auto maint_time = db->get_dynamic_global_properties().next_maintenance_time;
      auto slots_to_miss = db->get_slot_at_time(maint_time);
      db->generate_block(db->get_slot_time(slots_to_miss),
            db->get_scheduled_witness(slots_to_miss),
            committee_key,
            skip);
      return true;
   } catch (exception& e)
   {
      return false;
   }
}

///////////
/// @brief a class to make connecting to the application server easier
///////////
class client_connection
{
public:
   /////////
   // constructor
   /////////
   client_connection(
      std::shared_ptr<graphene::app::application> app, 
      const fc::temp_directory& data_dir, 
      const int server_port_number
   )
   {
      wallet_data.chain_id = app->chain_database()->get_chain_id();
      wallet_data.ws_server = "ws://127.0.0.1:" + std::to_string(server_port_number);
      wallet_data.ws_user = "";
      wallet_data.ws_password = "";
      websocket_connection  = websocket_client.connect( wallet_data.ws_server );

      api_connection = std::make_shared<fc::rpc::websocket_api_connection>(*websocket_connection, GRAPHENE_MAX_NESTED_OBJECTS);

      remote_login_api = api_connection->get_remote_api< graphene::app::login_api >(1);
      BOOST_CHECK(remote_login_api->login( wallet_data.ws_user, wallet_data.ws_password ) );

      wallet_api_ptr = std::make_shared<graphene::wallet::wallet_api>(wallet_data, remote_login_api);
      wallet_filename = data_dir.path().generic_string() + "/wallet.json";
      wallet_api_ptr->set_wallet_filename(wallet_filename);

      wallet_api = fc::api<graphene::wallet::wallet_api>(wallet_api_ptr);

      wallet_cli = std::make_shared<fc::rpc::cli>(GRAPHENE_MAX_NESTED_OBJECTS);
      for( auto& name_formatter : wallet_api_ptr->get_result_formatters() )
         wallet_cli->format_result( name_formatter.first, name_formatter.second );

      boost::signals2::scoped_connection closed_connection(websocket_connection->closed.connect([=]{
         cerr << "Server has disconnected us.\n";
         wallet_cli->stop();
      }));
      (void)(closed_connection);
   }
public:
   fc::http::websocket_client websocket_client;
   graphene::wallet::wallet_data wallet_data;
   fc::http::websocket_connection_ptr websocket_connection;
   std::shared_ptr<fc::rpc::websocket_api_connection> api_connection;
   fc::api<login_api> remote_login_api;
   std::shared_ptr<graphene::wallet::wallet_api> wallet_api_ptr;
   fc::api<graphene::wallet::wallet_api> wallet_api;
   std::shared_ptr<fc::rpc::cli> wallet_cli;
   std::string wallet_filename;
};

///////////////////////////////
// Cli Wallet Fixture
///////////////////////////////

struct cli_fixture
{
   int server_port_number;
   fc::temp_directory app_dir;
   std::shared_ptr<graphene::app::application> app1;
   client_connection con;
   std::vector<std::string> nathan_keys;

   cli_fixture() : 
      server_port_number(0),
      app_dir( graphene::utilities::temp_directory_path() ),
      app1( start_application(app_dir, server_port_number) ), 
      con( app1, app_dir, server_port_number ),
      nathan_keys( {"5KQwrPbwdL6PhXujxW37FSSQZ1JiwsST4cqQzDeyXtP79zkvFD3"} )
   {
      BOOST_TEST_MESSAGE("Setup cli_wallet::boost_fixture_test_case");

      using namespace graphene::chain;
      using namespace graphene::app;

      try 
      {
         BOOST_TEST_MESSAGE("Setting wallet password");
         con.wallet_api_ptr->set_password("supersecret");
         con.wallet_api_ptr->unlock("supersecret");

         // import Nathan account
         BOOST_TEST_MESSAGE("Importing nathan key");
         BOOST_CHECK_EQUAL(nathan_keys[0], "5KQwrPbwdL6PhXujxW37FSSQZ1JiwsST4cqQzDeyXtP79zkvFD3");
         BOOST_CHECK(con.wallet_api_ptr->import_key("nathan", nathan_keys[0]));
      } catch( fc::exception& e ) {
         edump((e.to_detail_string()));
         throw;
      }
   }

   ~cli_fixture()
   {
      BOOST_TEST_MESSAGE("Cleanup cli_wallet::boost_fixture_test_case");

      // wait for everything to finish up
      fc::usleep(fc::seconds(1));
        
      app1->shutdown();
#ifdef _WIN32
      sockQuit();
#endif
   }
};

///////////////////////////////
// Tests
///////////////////////////////

////////////////
// Start a server and connect using the same calls as the CLI
////////////////
BOOST_FIXTURE_TEST_CASE( cli_connect, cli_fixture )
{
   BOOST_TEST_MESSAGE("Testing wallet connection.");
}

////////////////
// Start a server and connect using the same calls as the CLI
// Quit wallet and be sure that file was saved correctly
////////////////
BOOST_FIXTURE_TEST_CASE( cli_quit, cli_fixture )
{
   BOOST_TEST_MESSAGE("Testing wallet connection and quit command.");
   BOOST_CHECK_THROW( con.wallet_api_ptr->quit(), fc::canceled_exception );
}

BOOST_FIXTURE_TEST_CASE( upgrade_nathan_account, cli_fixture )
{
   try
   {
      BOOST_TEST_MESSAGE("Upgrade Nathan's account");

      account_object nathan_acct_before_upgrade, nathan_acct_after_upgrade;
      std::vector<signed_transaction> import_txs;
      signed_transaction upgrade_tx;

      BOOST_TEST_MESSAGE("Importing nathan's balance");
      import_txs = con.wallet_api_ptr->import_balance("nathan", nathan_keys, true);
      nathan_acct_before_upgrade = con.wallet_api_ptr->get_account("nathan");

      // upgrade nathan
      BOOST_TEST_MESSAGE("Upgrading Nathan to LTM");
      upgrade_tx = con.wallet_api_ptr->upgrade_account("nathan", true);
      nathan_acct_after_upgrade = con.wallet_api_ptr->get_account("nathan");

      // verify that the upgrade was successful
      BOOST_CHECK_PREDICATE( 
         std::not_equal_to<uint32_t>(), 
         (nathan_acct_before_upgrade.membership_expiration_date.sec_since_epoch())
         (nathan_acct_after_upgrade.membership_expiration_date.sec_since_epoch()) 
      );
      BOOST_CHECK(nathan_acct_after_upgrade.is_lifetime_member());
   } catch( fc::exception& e ) {
      edump((e.to_detail_string()));
      throw;
   }
}

BOOST_FIXTURE_TEST_CASE( create_new_account, cli_fixture )
{
   try
   {
      INVOKE(upgrade_nathan_account);

      // create a new account
      graphene::wallet::brain_key_info bki = con.wallet_api_ptr->suggest_brain_key();
      BOOST_CHECK(!bki.brain_priv_key.empty());
      signed_transaction create_acct_tx = con.wallet_api_ptr->create_account_with_brain_key(
         bki.brain_priv_key, "jmjatlanta", "nathan", "nathan", true
      );
      // save the private key for this new account in the wallet file
      BOOST_CHECK(con.wallet_api_ptr->import_key("jmjatlanta", bki.wif_priv_key));
      con.wallet_api_ptr->save_wallet_file(con.wallet_filename);

      // attempt to give jmjatlanta some bitsahres
      BOOST_TEST_MESSAGE("Transferring bitshares from Nathan to jmjatlanta");
      signed_transaction transfer_tx = con.wallet_api_ptr->transfer(
         "nathan", "jmjatlanta", "10000", "1.3.0", "Here are some CORE token for your new account", true
      );
   } catch( fc::exception& e ) {
      edump((e.to_detail_string()));
      throw;
   }
}

///////////////////////
// Start a server and connect using the same calls as the CLI
// Vote for two witnesses, and make sure they both stay there
// after a maintenance block
///////////////////////
BOOST_FIXTURE_TEST_CASE( cli_vote_for_2_witnesses, cli_fixture )
{
   try
   {
      BOOST_TEST_MESSAGE("Cli Vote Test for 2 Witnesses");

      INVOKE(create_new_account);

      // get the details for init1
      witness_object init1_obj = con.wallet_api_ptr->get_witness("init1");
      int init1_start_votes = init1_obj.total_votes;
      // Vote for a witness
      signed_transaction vote_witness1_tx = con.wallet_api_ptr->vote_for_witness("jmjatlanta", "init1", true, true);

      // generate a block to get things started
      BOOST_CHECK(generate_block(app1));
      // wait for a maintenance interval
      BOOST_CHECK(generate_maintenance_block(app1));

      // Verify that the vote is there
      init1_obj = con.wallet_api_ptr->get_witness("init1");
      witness_object init2_obj = con.wallet_api_ptr->get_witness("init2");
      int init1_middle_votes = init1_obj.total_votes;
      BOOST_CHECK(init1_middle_votes > init1_start_votes);

      // Vote for a 2nd witness
      int init2_start_votes = init2_obj.total_votes;
      signed_transaction vote_witness2_tx = con.wallet_api_ptr->vote_for_witness("jmjatlanta", "init2", true, true);

      // send another block to trigger maintenance interval
      BOOST_CHECK(generate_maintenance_block(app1));

      // Verify that both the first vote and the 2nd are there
      init2_obj = con.wallet_api_ptr->get_witness("init2");
      init1_obj = con.wallet_api_ptr->get_witness("init1");

      int init2_middle_votes = init2_obj.total_votes;
      BOOST_CHECK(init2_middle_votes > init2_start_votes);
      int init1_last_votes = init1_obj.total_votes;
      BOOST_CHECK(init1_last_votes > init1_start_votes);
   } catch( fc::exception& e ) {
      edump((e.to_detail_string()));
      throw;
   }
}

///////////////////
// Start a server and connect using the same calls as the CLI
// Set a voting proxy and be assured that it sticks
///////////////////
BOOST_FIXTURE_TEST_CASE( cli_set_voting_proxy, cli_fixture )
{
   try {
      INVOKE(create_new_account);

      // grab account for comparison
      account_object prior_voting_account = con.wallet_api_ptr->get_account("jmjatlanta");
      // set the voting proxy to nathan
      BOOST_TEST_MESSAGE("About to set voting proxy.");
      signed_transaction voting_tx = con.wallet_api_ptr->set_voting_proxy("jmjatlanta", "nathan", true);
      account_object after_voting_account = con.wallet_api_ptr->get_account("jmjatlanta");
      // see if it changed
      BOOST_CHECK(prior_voting_account.options.voting_account != after_voting_account.options.voting_account);
   } catch( fc::exception& e ) {
      edump((e.to_detail_string()));
      throw;
   }
}

///////////////////
// Test blind transactions and mantissa length of range proofs.
///////////////////
BOOST_FIXTURE_TEST_CASE( cli_confidential_tx_test, cli_fixture )
{
   using namespace graphene::wallet;
   try {
      std::vector<signed_transaction> import_txs;

      BOOST_TEST_MESSAGE("Importing nathan's balance");
      import_txs = con.wallet_api_ptr->import_balance("nathan", nathan_keys, true);

      unsigned int head_block = 0;
      auto & W = *con.wallet_api_ptr; // Wallet alias
      
      BOOST_TEST_MESSAGE("Creating blind accounts");
      graphene::wallet::brain_key_info bki_nathan = W.suggest_brain_key();
      graphene::wallet::brain_key_info bki_alice = W.suggest_brain_key();
      graphene::wallet::brain_key_info bki_bob = W.suggest_brain_key();
      W.create_blind_account("nathan", bki_nathan.brain_priv_key);
      W.create_blind_account("alice", bki_alice.brain_priv_key);
      W.create_blind_account("bob", bki_bob.brain_priv_key);
      BOOST_CHECK(W.get_blind_accounts().size() == 3);

      // ** Block 1: Import Nathan account:
      BOOST_TEST_MESSAGE("Importing nathan key and balance");
      std::vector<std::string> nathan_keys{"5KQwrPbwdL6PhXujxW37FSSQZ1JiwsST4cqQzDeyXtP79zkvFD3"};
      W.import_key("nathan", nathan_keys[0]);
      W.import_balance("nathan", nathan_keys, true);
      generate_block(app1); head_block++;

      // ** Block 2: Nathan will blind 100M CORE token:
      BOOST_TEST_MESSAGE("Blinding a large balance");
      W.transfer_to_blind("nathan", GRAPHENE_SYMBOL, {{"nathan","100000000"}}, true);
      BOOST_CHECK( W.get_blind_balances("nathan")[0].amount == 10000000000000 );
      generate_block(app1); head_block++;

      // ** Block 3: Nathan will send 1M CORE token to alice and 10K CORE token to bob. We
      // then confirm that balances are received, and then analyze the range
      // prooofs to make sure the mantissa length does not reveal approximate
      // balance (issue #480).
      std::map<std::string, share_type> to_list = {{"alice",100000000000},
                                                   {"bob",    1000000000}};
      vector<blind_confirmation> bconfs;
      asset_object core_asset = W.get_asset("1.3.0");
      BOOST_TEST_MESSAGE("Sending blind transactions to alice and bob");
      for (auto to : to_list) {
         string amount = core_asset.amount_to_string(to.second);
         bconfs.push_back(W.blind_transfer("nathan",to.first,amount,core_asset.symbol,true));
         BOOST_CHECK( W.get_blind_balances(to.first)[0].amount == to.second );
      }
      BOOST_TEST_MESSAGE("Inspecting range proof mantissa lengths");
      vector<int> rp_mantissabits;
      for (auto conf : bconfs) {
         for (auto out : conf.trx.operations[0].get<blind_transfer_operation>().outputs) {
            rp_mantissabits.push_back(1+out.range_proof[1]); // 2nd byte encodes mantissa length
         }
      }
      // We are checking the mantissa length of the range proofs for several Pedersen
      // commitments of varying magnitude.  We don't want the mantissa lengths to give
      // away magnitude.  Deprecated wallet behavior was to use "just enough" mantissa
      // bits to prove range, but this gives away value to within a factor of two. As a
      // naive test, we assume that if all mantissa lengths are equal, then they are not
      // revealing magnitude.  However, future more-sophisticated wallet behavior
      // *might* randomize mantissa length to achieve some space savings in the range
      // proof.  The following test will fail in that case and a more sophisticated test
      // will be needed.
      auto adjacent_unequal = std::adjacent_find(rp_mantissabits.begin(),
                                                 rp_mantissabits.end(),         // find unequal adjacent values
                                                 std::not_equal_to<int>());
      BOOST_CHECK(adjacent_unequal == rp_mantissabits.end());
      generate_block(app1); head_block++;

      // ** Check head block:
      BOOST_TEST_MESSAGE("Check that all expected blocks have processed");
      dynamic_global_property_object dgp = W.get_dynamic_global_properties();
      BOOST_CHECK(dgp.head_block_number == head_block);
   } catch( fc::exception& e ) {
      edump((e.to_detail_string()));
      throw;
   }
}

/******
 * Check account history pagination (see bitshares-core/issue/1176)
 */
BOOST_FIXTURE_TEST_CASE( account_history_pagination, cli_fixture )
{
   try
   {
      INVOKE(create_new_account);

      // attempt to give jmjatlanta some bitsahres
      BOOST_TEST_MESSAGE("Transferring bitshares from Nathan to jmjatlanta");
      for(int i = 1; i <= 199; i++)
      {
         signed_transaction transfer_tx = con.wallet_api_ptr->transfer("nathan", "jmjatlanta", std::to_string(i),
                                                "1.3.0", "Here are some CORE token for your new account", true);
      }

      BOOST_CHECK(generate_block(app1));

      // now get account history and make sure everything is there (and no duplicates)
      std::vector<graphene::wallet::operation_detail> history = con.wallet_api_ptr->get_account_history("jmjatlanta", 300);
      BOOST_CHECK_EQUAL(201u, history.size() );

      std::set<object_id_type> operation_ids;

      for(auto& op : history)
      {
         if( operation_ids.find(op.op.id) != operation_ids.end() )
         {
            BOOST_FAIL("Duplicate found");
         }
         operation_ids.insert(op.op.id);
      }
   } catch( fc::exception& e ) {
      edump((e.to_detail_string()));
      throw;
   }
}

///////////////////////
// Start a server and connect using the same calls as the CLI
// Create an HTLC
///////////////////////
BOOST_AUTO_TEST_CASE( cli_create_htlc )
{
   using namespace graphene::chain;
   using namespace graphene::app;
   std::shared_ptr<graphene::app::application> app1;
   try {
      fc::temp_directory app_dir( graphene::utilities::temp_directory_path() );

      int server_port_number = 0;
      app1 = start_application(app_dir, server_port_number);
      // set committee parameters
      app1->chain_database()->modify(app1->chain_database()->get_global_properties(), [](global_property_object& p) {
         graphene::chain::htlc_options params;
         params.max_preimage_size = 1024;
         params.max_timeout_secs = 60 * 60 * 24 * 28;
         p.parameters.extensions.value.updatable_htlc_options = params;
      });

      // connect to the server
      client_connection con(app1, app_dir, server_port_number);

      BOOST_TEST_MESSAGE("Setting wallet password");
      con.wallet_api_ptr->set_password("supersecret");
      con.wallet_api_ptr->unlock("supersecret");

      // import Nathan account
      BOOST_TEST_MESSAGE("Importing nathan key");
      std::vector<std::string> nathan_keys{"5KQwrPbwdL6PhXujxW37FSSQZ1JiwsST4cqQzDeyXtP79zkvFD3"};
      BOOST_CHECK_EQUAL(nathan_keys[0], "5KQwrPbwdL6PhXujxW37FSSQZ1JiwsST4cqQzDeyXtP79zkvFD3");
      BOOST_CHECK(con.wallet_api_ptr->import_key("nathan", nathan_keys[0]));

      BOOST_TEST_MESSAGE("Importing nathan's balance");
      std::vector<signed_transaction> import_txs = con.wallet_api_ptr->import_balance("nathan", nathan_keys, true);
      account_object nathan_acct_before_upgrade = con.wallet_api_ptr->get_account("nathan");

      // upgrade nathan
      BOOST_TEST_MESSAGE("Upgrading Nathan to LTM");
      signed_transaction upgrade_tx = con.wallet_api_ptr->upgrade_account("nathan", true);
      account_object nathan_acct_after_upgrade = con.wallet_api_ptr->get_account("nathan");

      // verify that the upgrade was successful
      BOOST_CHECK_PREDICATE( std::not_equal_to<uint32_t>(), (nathan_acct_before_upgrade.membership_expiration_date.sec_since_epoch())
            (nathan_acct_after_upgrade.membership_expiration_date.sec_since_epoch()) );
      BOOST_CHECK(nathan_acct_after_upgrade.is_lifetime_member());

      // Create new asset called BOBCOIN
      try 
      {
         graphene::chain::asset_options asset_ops;
         asset_ops.max_supply = 1000000;
         asset_ops.core_exchange_rate = price(asset(2),asset(1,asset_id_type(1)));
         fc::optional<graphene::chain::bitasset_options> bit_opts;
         con.wallet_api_ptr->create_asset("nathan", "BOBCOIN", 5, asset_ops, bit_opts, true);
      }
      catch(exception& e)
      {
         BOOST_FAIL(e.what());
      }
      catch(...)
      {
         BOOST_FAIL("Unknown exception creating BOBCOIN");
      }

      // create a new account for Alice
      {
         graphene::wallet::brain_key_info bki = con.wallet_api_ptr->suggest_brain_key();
         BOOST_CHECK(!bki.brain_priv_key.empty());
         signed_transaction create_acct_tx = con.wallet_api_ptr->create_account_with_brain_key(bki.brain_priv_key, "alice", 
               "nathan", "nathan", true);
         // save the private key for this new account in the wallet file
         BOOST_CHECK(con.wallet_api_ptr->import_key("alice", bki.wif_priv_key));
         con.wallet_api_ptr->save_wallet_file(con.wallet_filename);
         // attempt to give alice some bitsahres
         BOOST_TEST_MESSAGE("Transferring bitshares from Nathan to alice");
         signed_transaction transfer_tx = con.wallet_api_ptr->transfer("nathan", "alice", "10000", "1.3.0", 
               "Here are some CORE token for your new account", true);
      }

      // create a new account for Bob
      {
         graphene::wallet::brain_key_info bki = con.wallet_api_ptr->suggest_brain_key();
         BOOST_CHECK(!bki.brain_priv_key.empty());
         signed_transaction create_acct_tx = con.wallet_api_ptr->create_account_with_brain_key(bki.brain_priv_key, "bob", 
               "nathan", "nathan", true);
         // save the private key for this new account in the wallet file
         BOOST_CHECK(con.wallet_api_ptr->import_key("bob", bki.wif_priv_key));
         con.wallet_api_ptr->save_wallet_file(con.wallet_filename);
         // attempt to give bob some bitsahres
         BOOST_TEST_MESSAGE("Transferring bitshares from Nathan to Bob");
         signed_transaction transfer_tx = con.wallet_api_ptr->transfer("nathan", "bob", "10000", "1.3.0", 
               "Here are some CORE token for your new account", true);
         con.wallet_api_ptr->issue_asset("bob", "5", "BOBCOIN", "Here are your BOBCOINs", true);
      }


      BOOST_TEST_MESSAGE("Alice has agreed to buy 3 BOBCOIN from Bob for 3 BTS. Alice creates an HTLC");
      // create an HTLC
      std::string preimage_string = "My Secret";
      fc::sha256 preimage_md = fc::sha256::hash(preimage_string);
      std::stringstream ss;
      for(int i = 0; i < preimage_md.data_size(); i++)
      {
         char d = preimage_md.data()[i];
         unsigned char uc = static_cast<unsigned char>(d);
         ss << std::setfill('0') << std::setw(2) << std::hex << (int)uc;
      }
      std::string hash_str = ss.str();
      BOOST_TEST_MESSAGE("Secret is " + preimage_string + " and hash is " + hash_str);
      uint32_t timelock = fc::days(1).to_seconds();
      graphene::chain::signed_transaction result_tx 
            = con.wallet_api_ptr->htlc_create("alice", "bob", 
            "3", "1.3.0", "SHA256", hash_str, preimage_string.size(), timelock, true);

      // normally, a wallet would watch block production, and find the transaction. Here, we can cheat:
      std::string alice_htlc_id_as_string;
      {
         BOOST_TEST_MESSAGE("The system is generating a block");
         graphene::chain::signed_block result_block;
         BOOST_CHECK(generate_block(app1, result_block));

         // get the ID:
         htlc_id_type htlc_id = result_block.transactions[result_block.transactions.size()-1].operation_results[0].get<object_id_type>();
         alice_htlc_id_as_string = (std::string)(object_id_type)htlc_id;
         BOOST_TEST_MESSAGE("Alice shares the HTLC ID with Bob. The HTLC ID is: " + alice_htlc_id_as_string);
      }

      // Bob can now look over Alice's HTLC, to see if it is what was agreed to.
      BOOST_TEST_MESSAGE("Bob retrieves the HTLC Object by ID to examine it.");
      auto alice_htlc = con.wallet_api_ptr->get_htlc(alice_htlc_id_as_string);
      BOOST_TEST_MESSAGE("The HTLC Object is: " + fc::json::to_pretty_string(alice_htlc));

      // Bob likes what he sees, so he creates an HTLC, using the info he retrieved from Alice's HTLC
      con.wallet_api_ptr->htlc_create("bob", "alice",
            "3", "BOBCOIN", "SHA256", hash_str, preimage_string.size(), timelock, true);

      // normally, a wallet would watch block production, and find the transaction. Here, we can cheat:
      std::string bob_htlc_id_as_string;
      {
         BOOST_TEST_MESSAGE("The system is generating a block");
         graphene::chain::signed_block result_block;
         BOOST_CHECK(generate_block(app1, result_block));

         // get the ID:
         htlc_id_type htlc_id = result_block.transactions[result_block.transactions.size()-1].operation_results[0].get<object_id_type>();
         bob_htlc_id_as_string = (std::string)(object_id_type)htlc_id;
         BOOST_TEST_MESSAGE("Bob shares the HTLC ID with Alice. The HTLC ID is: " + bob_htlc_id_as_string);
      }

      // Alice can now look over Bob's HTLC, to see if it is what was agreed to:
      BOOST_TEST_MESSAGE("Alice retrieves the HTLC Object by ID to examine it.");
      auto bob_htlc = con.wallet_api_ptr->get_htlc(bob_htlc_id_as_string);
      BOOST_TEST_MESSAGE("The HTLC Object is: " + fc::json::to_pretty_string(bob_htlc));

      // Alice likes what she sees, so uses her preimage to get her BOBCOIN
      {
         BOOST_TEST_MESSAGE("Alice uses her preimage to retrieve the BOBCOIN");
         std::string secret = "My Secret";
         con.wallet_api_ptr->htlc_redeem(bob_htlc_id_as_string, "alice", secret, true);
         BOOST_TEST_MESSAGE("The system is generating a block");
         BOOST_CHECK(generate_block(app1));
      }

      // TODO: Bob can look at Alice's history to see her preimage
      // Bob can use the preimage to retrieve his BTS
      {
         BOOST_TEST_MESSAGE("Bob uses Alice's preimage to retrieve the BOBCOIN");
         std::string secret = "My Secret";
         con.wallet_api_ptr->htlc_redeem(alice_htlc_id_as_string, "bob", secret, true);
         BOOST_TEST_MESSAGE("The system is generating a block");
         BOOST_CHECK(generate_block(app1));
      }

      // wait for everything to finish up
      fc::usleep(fc::seconds(1));
   } catch( fc::exception& e ) {
      edump((e.to_detail_string()));
      throw;
   }
   app1->shutdown();
}
