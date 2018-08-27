/*
 * Copyright (c) 2018 John Jones (jmjatlanta), and contributors.
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
#include <memory>
#include <thread>
#include <iostream>
#include <queue>
#include <cassert>
#include <mutex>
#include <random>
#include <condition_variable>
#include <csignal>

#include <sys/socket.h>
#include <netinet/ip.h>
#include <sys/types.h>

#include <graphene/app/application.hpp>
#include <graphene/app/api.hpp>
#include <graphene/account_history/account_history_plugin.hpp>
#include <graphene/market_history/market_history_plugin.hpp>
#include <graphene/witness/witness.hpp>
#include <graphene/grouped_orders/grouped_orders_plugin.hpp>
#include <graphene/utilities/tempdir.hpp>
#include <graphene/app/plugin.hpp>
#include <graphene/wallet/wallet.hpp>

#include <fc/thread/thread.hpp>
#include <fc/smart_ref_impl.hpp>
#include <fc/network/http/websocket.hpp>
#include <fc/rpc/websocket_api.hpp>
#include <fc/rpc/cli.hpp>
#include <fc/interprocess/signals.hpp>

#include "../common/genesis_file_util.hpp"

template <typename T>
class BlockingQueue
{
private:
    std::mutex              d_mutex;
    std::condition_variable d_condition;
    std::deque<T>           d_queue;
public:
    void push(T const& value) {
        if (d_queue.size() > 1000)
        {
           // silently skip this one and give the queue a rest
           std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
        else
        {
            std::unique_lock<std::mutex> lock(d_mutex);
            d_queue.push_front(value);
        }
        d_condition.notify_one();
    }
    T pop() {
      std::unique_lock<std::mutex> lock(d_mutex);
      d_condition.wait(lock, [=] { return !d_queue.empty(); });
      T rc(std::move(d_queue.back()));
      d_queue.pop_back();
      return rc;
    }
};

/******
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
   close(socket_fd);
   return ntohs(sin.sin_port);
}

///////////
/// Send a block to the db
/// @param app the application
/// @returns true on success
///////////
bool generate_block(std::shared_ptr<graphene::app::application> app) {
   try {
      fc::ecc::private_key committee_key = fc::ecc::private_key::regenerate(fc::sha256::hash(string("nathan")));
      auto db = app->chain_database();
      auto block_1 = db->generate_block( db->get_slot_time(1),
                                         db->get_scheduled_witness(1),
                                         committee_key,
                                         database::skip_nothing );
      return true;
   } catch (exception &e) {
      return false;
   }
}

std::shared_ptr<graphene::app::application> start_application(int p2p_port, int rpc_port, 
      const fc::temp_directory& app_dir, std::string seed_nodes = "[]")
{
   std::shared_ptr<graphene::app::application> app = std::make_shared<graphene::app::application>();
   app->register_plugin< graphene::account_history::account_history_plugin>();
   app->register_plugin< graphene::market_history::market_history_plugin >();
   app->register_plugin< graphene::witness_plugin::witness_plugin >();
   app->register_plugin< graphene::grouped_orders::grouped_orders_plugin>();
   app->startup_plugins();
   boost::program_options::variables_map cfg;
   cfg.emplace("rpc-endpoint", boost::program_options::variable_value("127.0.0.1:" + std::to_string(rpc_port), false));
   cfg.emplace("p2p-endpoint", boost::program_options::variable_value("127.0.0.1:" + std::to_string(p2p_port), false));
   cfg.emplace("genesis-json", boost::program_options::variable_value(create_genesis_file(app_dir), false));
   cfg.emplace("seed-nodes", boost::program_options::variable_value(seed_nodes, false));
   app->initialize(app_dir.path(), cfg);
   app->startup();
   fc::usleep(fc::milliseconds(500));
   return app;
}

/*****
 * Makes a client connecting to a node easer
 */
class client_connection
{
public:
   client_connection(std::shared_ptr<graphene::app::application> app, std::string data_dir, const int server_port_number, std::string wallet_file_name )
   {
      wallet_data.chain_id = app->chain_database()->get_chain_id();
      wallet_data.ws_server = "ws://127.0.0.1:" + std::to_string(server_port_number);
      wallet_data.ws_user = "";
      wallet_data.ws_password = "";
      websocket_connection  = websocket_client.connect( wallet_data.ws_server );

      api_connection = std::make_shared<fc::rpc::websocket_api_connection>(*websocket_connection, GRAPHENE_MAX_NESTED_OBJECTS);

      remote_login_api = api_connection->get_remote_api< graphene::app::login_api >(1);
      remote_login_api->login( wallet_data.ws_user, wallet_data.ws_password );

      wallet_api_ptr = std::make_shared<graphene::wallet::wallet_api>(wallet_data, remote_login_api);
      this->wallet_filename = data_dir + "/" + wallet_file_name;
      wallet_api_ptr->set_wallet_filename(this->wallet_filename);

      wallet_api = fc::api<graphene::wallet::wallet_api>(wallet_api_ptr);

      wallet_cli = std::make_shared<fc::rpc::cli>(GRAPHENE_MAX_NESTED_OBJECTS);
      for( auto& name_formatter : wallet_api_ptr->get_result_formatters() )
         wallet_cli->format_result( name_formatter.first, name_formatter.second );

      boost::signals2::scoped_connection closed_connection(websocket_connection->closed.connect([=]{
         std::cerr << "Server has disconnected us.\n";
         wallet_cli->stop();
      }));
      (void)(closed_connection);
   }
   void disconnect()
   {
      websocket_client.disconnect();
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


/***
 * An autonomous client that does typical client things
 */
class client{
public:
   client(std::shared_ptr<graphene::app::application> application, std::string app_dir, unsigned int id, std::string private_key, unsigned int port) 
      : application(application), app_dir(app_dir), my_id(id), private_key(private_key), port(port)
   {
      // connect to api node
      name = "client" + std::to_string(my_id);
   }
   ~client()
   {
      shutdown();
   }
   bool connect()
   {
      if (!shutting_down && !is_connected)
      {
         connection = std::make_shared<client_connection>(application, app_dir, port, name + ".json");
         // save wallet file
         connection->wallet_api_ptr->set_password("supersecret");
         connection->wallet_api_ptr->unlock("supersecret");
         std::vector<std::string> keys{private_key};
         std::vector<signed_transaction> import_txs = connection->wallet_api_ptr->import_balance(name, keys, true);
         connection->wallet_api_ptr->import_key(name, private_key);
         std::string wallet_filename = app_dir + "/" + name + ".json";
         connection->wallet_api_ptr->set_wallet_filename(wallet_filename);
         is_connected = true;
      }
      return is_connected;
   }
   void start_message_loop()
   {
      //start message loop
      message_loop_thread = std::thread(message_loop, this);
      pthread_setname_np(message_loop_thread.native_handle(), (name + "ml").c_str());
   }
   void do_random_action()
   {
      // put something in the queue
      queue.push( rand() % 3);
   }
   void do_random_actions_every(std::chrono::steady_clock::duration duration)
   {
      random_action_duration = duration;
      if (!random_loop_running)
      {
         random_actions_thread = std::thread(random_action_loop, this);
         pthread_setname_np(random_actions_thread.native_handle(), (name + "rl").c_str());
      }
   }

   void shutdown() 
   { 
      // shut down message loop first
      shutting_down = true; 
      if (message_loop_running)
      {
         // throw one more item in the queue, just to be sure
         do_random_action();
         if (message_loop_thread.joinable())
            message_loop_thread.join();
      }
      // now shut down random_actions thread
      if (random_loop_running)
      {  
         if (random_actions_thread.joinable())
            random_actions_thread.join();
      }
      connection->disconnect();
      is_connected = false;
   }
   void stop_random_actions() { random_loop_running = false; }
   void setDirectory(std::shared_ptr<std::vector<std::shared_ptr<client>>> c) { clients = c; }
   unsigned int my_id;
   std::string name;
   std::shared_ptr<client_connection> connection;
protected:
   std::thread message_loop_thread;
   std::thread random_actions_thread;
   volatile bool is_connected = false;
   volatile bool shutting_down = false;
   volatile bool random_loop_running = false;
   volatile bool message_loop_running = false;
   std::chrono::steady_clock::duration random_action_duration;
   BlockingQueue<int> queue;
   std::shared_ptr<std::vector<std::shared_ptr<client>>> clients;
   std::string private_key;
   std::shared_ptr<graphene::app::application> application;
   std::string app_dir;
   unsigned int port;

private:
   static void message_loop(client* c)
   {
      c->message_loop_running = true;
      while( !c->shutting_down)
      {
         if (c->is_connected)
         {
            int task = c->queue.pop();
            switch(task)
            {
               case (0):
                  c->request_block();
                  break;
               case (1):
                  c->transfer();
                  break;
               case (2):
                  c->disconnect_reconnect();
                  break;
            }
         }
      }
      c->message_loop_running = false;
   }
   static void random_action_loop(client* c)
   {
      c->random_loop_running = true;
      while ( !c->shutting_down )
      {
         c->do_random_action();
         std::this_thread::sleep_for(c->random_action_duration);
      }
      c->random_loop_running = false;
   }
   void request_block()
   {
      std::cout << "RequestBlock\n";
   }
   void disconnect_reconnect()
   {
      connection->disconnect();
      is_connected = false;
      connect();
      std::cout << "Disconnected/reconnected\n";
   }
   void transfer()
   {
      try
      {
         // we must have at least 2
         if (clients->size() < 2)
            return;
         // pick a random person that is not me
         unsigned int my_friend_no = my_id;
         while (my_friend_no == my_id)
            my_friend_no = rand() % clients->size();
         std::shared_ptr<client> my_friend = clients->at(my_friend_no);
         // give them some CORE
         connection->wallet_api_ptr->transfer(name, my_friend->name, "1000", "BTS", "", true);
         std::cout << "Transfer complete\n";
      } catch (fc::exception &ex) 
      {
         std::cerr << "Caught exception attempting to transfer. Error was: " + ex.to_detail_string(fc::log_level(fc::log_level::all)) + "\n";
      } catch (std::exception &ex)
      {
        std::cerr << "Caught std exception: " << ex.what() << "\n"; 
      } catch (...)
      {
        std::cerr << "Caught unknown exception.\n";
      }
   }
};

struct application_server
{
   std::shared_ptr<fc::temp_directory> app_dir;
   unsigned int p2p_port;
   unsigned int rpc_port;
   std::shared_ptr<graphene::app::application> app;
};

std::string calculate_seed_nodes(const std::vector<std::shared_ptr<application_server>>& servers)
{
   std::string retVal = "[";
   for(const auto& server : servers )
   {
      if (retVal.size() > 1)
         retVal += ", ";
      retVal += "\"127.0.0.1:" + std::to_string(server->p2p_port) + "\"";
   }
   retVal += "]";
   return retVal;
}

/****
 * Create a node and a few clients that do random things
 */
int main(int argc, char** argp)
{
   std::vector<std::shared_ptr<application_server>> servers;

   unsigned int num_clients = 10;
   unsigned int num_servers = 1;
   std::string seed_nodes = "[]";

   try
   {
      for(unsigned int i = 0; i < num_servers; ++i)
      {
         std::shared_ptr<application_server> current_server = std::make_shared<application_server>();
         current_server->p2p_port = get_available_port();
         current_server->rpc_port = get_available_port();
         current_server->app_dir = std::make_shared<fc::temp_directory>( graphene::utilities::temp_directory_path() );
         current_server->app = start_application(current_server->p2p_port, current_server->rpc_port, *current_server->app_dir, seed_nodes);
         servers.push_back( current_server );
         seed_nodes = calculate_seed_nodes(servers);
      }
      std::shared_ptr<application_server> main_server = servers.at(0);
      // have the nathan account so we can give new users some CORE
      client_connection con(main_server->app, main_server->app_dir->path().generic_string(), main_server->rpc_port, "nathan.json");
      con.wallet_api_ptr->set_password("supersecret");
      con.wallet_api_ptr->unlock("supersecret");
      std::vector<std::string> nathan_keys{"5KQwrPbwdL6PhXujxW37FSSQZ1JiwsST4cqQzDeyXtP79zkvFD3"};
      con.wallet_api_ptr->import_key("nathan", nathan_keys[0]);
      std::vector<signed_transaction> import_txs = con.wallet_api_ptr->import_balance("nathan", nathan_keys, true);
      signed_transaction upgrade_tx = con.wallet_api_ptr->upgrade_account("nathan", true);
      account_object nathan_acct = con.wallet_api_ptr->get_account("nathan");

      std::shared_ptr<std::vector<std::shared_ptr<client>>> clients = std::make_shared<std::vector< std::shared_ptr < client > > >();

      // create a number of clients, each with their own account, and that know each other
      for(unsigned int i = 0; i < num_clients; ++i)
      {
         // have nathan create an account
         std::string new_account_name = "client" + std::to_string(i);
         graphene::wallet::brain_key_info bki = con.wallet_api_ptr->suggest_brain_key();
         signed_transaction create_acct_tx = con.wallet_api_ptr->create_account_with_brain_key(bki.brain_priv_key, new_account_name, "nathan", "nathan", true);
         // transfer CORE from nathan to this new client
         con.wallet_api_ptr->transfer("nathan", new_account_name, "1000000", "BTS", "", true);
         unsigned int server_number = i % num_servers;
         const auto& current_server = servers.at(server_number);
         std::shared_ptr<client> current_client = std::make_shared<client>(current_server->app, current_server->app_dir->path().generic_string(),
               i, bki.wif_priv_key, current_server->rpc_port);
         clients->push_back( current_client );
      }
      // make sure all nodes have the new accounts
      std::this_thread::sleep_for(std::chrono::seconds(1));

      // have the clients become autonomous
      for( size_t i = 0; i < clients->size(); ++i )
      {
         std::shared_ptr<client> c = clients->at(i);
         c->connect();
         c->setDirectory(clients);
         c->start_message_loop();
         c->do_random_actions_every( std::chrono::milliseconds(1) );
      }
      // now wait until the user wants to stop
      fc::promise<int>::ptr exit_promise = new fc::promise<int>("UNIX Signal Handler");

      fc::set_signal_handler([&exit_promise](int signal) {
         exit_promise->set_value(signal);
      }, SIGINT);

      fc::set_signal_handler([&exit_promise](int signal) {
         exit_promise->set_value(signal);
      }, SIGTERM);

      int signal = exit_promise->wait();
      for (const auto& server : servers)
      {
         server->app->shutdown();
      }
   } 
   catch (fc::exception& ex)
   {
      std::cerr << "FC Exception thrown: " << ex.to_detail_string(fc::log_level(fc::log_level::all)) << "\n";
   }
   catch (...)
   {
      std::cerr << "Uncaught exception thrown.\n";
   }
   return 0;
}