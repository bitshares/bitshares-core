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

#include <algorithm>
#include <iomanip>
#include <iostream>
#include <iterator>

#include <fc/io/json.hpp>
#include <fc/io/stdio.hpp>
#include <fc/network/http/websocket.hpp>
#include <fc/rpc/cli.hpp>
#include <fc/rpc/websocket_api.hpp>

#include <graphene/app/api.hpp>
#include <graphene/chain/config.hpp>
#include <graphene/egenesis/egenesis.hpp>
#include <graphene/utilities/key_conversion.hpp>
#include <graphene/wallet/wallet.hpp>

#include <fc/interprocess/signals.hpp>
#include <boost/program_options.hpp>

#include <fc/log/console_appender.hpp>
#include <fc/log/file_appender.hpp>
#include <fc/log/logger.hpp>
#include <fc/log/logger_config.hpp>

#include <graphene/utilities/git_revision.hpp>
#include <boost/version.hpp>
#include <boost/algorithm/string/replace.hpp>
#include <websocketpp/version.hpp>

#ifdef WIN32
# include <signal.h>
#else
# include <csignal>
#endif

using namespace graphene::app;
using namespace graphene::chain;
using namespace graphene::utilities;
using namespace graphene::wallet;
using namespace std;
namespace bpo = boost::program_options;

fc::log_level string_to_level(string level)
{
   fc::log_level result;
   if(level == "info")
      result = fc::log_level::info;
   else if(level == "debug")
      result = fc::log_level::debug;
   else if(level == "warn")
      result = fc::log_level::warn;
   else if(level == "error")
      result = fc::log_level::error;
   else if(level == "all")
      result = fc::log_level::all;
   else
      FC_THROW("Log level not allowed. Allowed levels are info, debug, warn, error and all.");

   return result;
}

void setup_logging(string console_level, bool file_logger, string file_level, string file_name)
{
   fc::logging_config cfg;

   // console logger
   fc::console_appender::config console_appender_config;
   console_appender_config.level_colors.emplace_back(
         fc::console_appender::level_color(fc::log_level::debug,
         fc::console_appender::color::green));
   console_appender_config.level_colors.emplace_back(
         fc::console_appender::level_color(fc::log_level::warn,
         fc::console_appender::color::brown));
   console_appender_config.level_colors.emplace_back(
         fc::console_appender::level_color(fc::log_level::error,
         fc::console_appender::color::red));
   cfg.appenders.push_back(fc::appender_config( "default", "console", fc::variant(console_appender_config, 20)));
   cfg.loggers = { fc::logger_config("default"), fc::logger_config( "rpc") };
   cfg.loggers.front().level = string_to_level(console_level);
   cfg.loggers.front().appenders = {"default"};

   // file logger
   if(file_logger) {
      fc::path data_dir;
      fc::path log_dir = data_dir / "cli_wallet_logs";
      fc::file_appender::config ac;
      ac.filename             = log_dir / file_name;
      ac.flush                = true;
      ac.rotate               = true;
      ac.rotation_interval    = fc::hours( 1 );
      ac.rotation_limit       = fc::days( 1 );
      cfg.appenders.push_back(fc::appender_config( "rpc", "file", fc::variant(ac, 5)));
      cfg.loggers.back().level = string_to_level(file_level);
      cfg.loggers.back().appenders = {"rpc"};
      fc::configure_logging( cfg );
      ilog ("Logging RPC to file: " + (ac.filename).preferred_string());
   }
}

int main( int argc, char** argv )
{
   try {

      boost::program_options::options_description opts;
         opts.add_options()
         ("help,h", "Print this help message and exit.")
         ("server-rpc-endpoint,s", bpo::value<string>()->implicit_value("ws://127.0.0.1:8090"), "Server websocket RPC endpoint")
         ("server-rpc-user,u", bpo::value<string>(), "Server Username")
         ("server-rpc-password,p", bpo::value<string>(), "Server Password")
         ("rpc-endpoint,r", bpo::value<string>()->implicit_value("127.0.0.1:8091"),
               "Endpoint for wallet websocket RPC to listen on (DEPRECATED, use rpc-http-endpoint instead)")
         ("rpc-tls-endpoint,t", bpo::value<string>()->implicit_value("127.0.0.1:8092"),
               "Endpoint for wallet websocket TLS RPC to listen on")
         ("rpc-tls-certificate,c", bpo::value<string>()->implicit_value("server.pem"),
               "PEM certificate for wallet websocket TLS RPC")
         ("rpc-http-endpoint,H", bpo::value<string>()->implicit_value("127.0.0.1:8093"),
               "Endpoint for wallet HTTP and websocket RPC to listen on")
         ("daemon,d", "Run the wallet in daemon mode" )
         ("wallet-file,w", bpo::value<string>()->implicit_value("wallet.json"), "wallet to load")
         ("chain-id", bpo::value<string>(), "chain ID to connect to")
         ("suggest-brain-key", "Suggest a safe brain key to use for creating your account")
         ("logs-rpc-console-level", bpo::value<string>()->default_value("info"),
               "Level of console logging. Allowed levels: info, debug, warn, error, all")
         ("logs-rpc-file", bpo::value<bool>()->default_value(false), "Turn on/off file logging")
         ("logs-rpc-file-level", bpo::value<string>()->default_value("debug"),
               "Level of file logging. Allowed levels: info, debug, warn, error, all")
         ("logs-rpc-file-name", bpo::value<string>()->default_value("rpc.log"), "File name for file rpc logs")
         ("version,v", "Display version information");

      bpo::variables_map options;

      bpo::store( bpo::parse_command_line(argc, argv, opts), options );

      if( options.count("help") )
      {
         std::cout << opts << "\n";
         return 0;
      }
      if( options.count("version") )
      {
         std::cout << "Version: " << graphene::utilities::git_revision_description << "\n";
         std::cout << "SHA: " << graphene::utilities::git_revision_sha << "\n";
         std::cout << "Timestamp: " << fc::get_approximate_relative_time_string(fc::time_point_sec(graphene::utilities::git_revision_unix_timestamp)) << "\n";
         std::cout << "SSL: " << OPENSSL_VERSION_TEXT << "\n";
         std::cout << "Boost: " << boost::replace_all_copy(std::string(BOOST_LIB_VERSION), "_", ".") << "\n";
         std::cout << "Websocket++: " << websocketpp::major_version << "." << websocketpp::minor_version << "." << websocketpp::patch_version << "\n";
         return 0;
      }
      if( options.count("suggest-brain-key") )
      {
         auto keyinfo = graphene::wallet::utility::suggest_brain_key();
         string data = fc::json::to_pretty_string( keyinfo );
         std::cout << data.c_str() << std::endl;
         return 0;
      }

      setup_logging(options.at("logs-rpc-console-level").as<string>(),options.at("logs-rpc-file").as<bool>(),
            options.at("logs-rpc-file-level").as<string>(), options.at("logs-rpc-file-name").as<string>());

      // TODO:  We read wallet_data twice, once in main() to grab the
      //    socket info, again in wallet_api when we do
      //    load_wallet_file().  Seems like this could be better
      //    designed.
      //
      wallet_data wdata;

      fc::path wallet_file( options.count("wallet-file") ? options.at("wallet-file").as<string>() : "wallet.json");
      if( fc::exists( wallet_file ) )
      {
         wdata = fc::json::from_file( wallet_file ).as<wallet_data>( GRAPHENE_MAX_NESTED_OBJECTS );
         if( options.count("chain-id") )
         {
            // the --chain-id on the CLI must match the chain ID embedded in the wallet file
            if( chain_id_type(options.at("chain-id").as<std::string>()) != wdata.chain_id )
            {
               std::cout << "Chain ID in wallet file does not match specified chain ID\n";
               return 1;
            }
         }
      }
      else
      {
         if( options.count("chain-id") )
         {
            wdata.chain_id = chain_id_type(options.at("chain-id").as<std::string>());
            std::cout << "Starting a new wallet with chain ID " << wdata.chain_id.str() << " (from CLI)\n";
         }
         else
         {
            wdata.chain_id = graphene::egenesis::get_egenesis_chain_id();
            std::cout << "Starting a new wallet with chain ID " << wdata.chain_id.str() << " (from egenesis)\n";
         }
      }

      // but allow CLI to override
      if( options.count("server-rpc-endpoint") )
         wdata.ws_server = options.at("server-rpc-endpoint").as<std::string>();
      if( options.count("server-rpc-user") )
         wdata.ws_user = options.at("server-rpc-user").as<std::string>();
      if( options.count("server-rpc-password") )
         wdata.ws_password = options.at("server-rpc-password").as<std::string>();

      fc::http::websocket_client client;
      idump((wdata.ws_server));
      auto con  = client.connect( wdata.ws_server );
      auto apic = std::make_shared<fc::rpc::websocket_api_connection>(con, GRAPHENE_MAX_NESTED_OBJECTS);

      auto remote_api = apic->get_remote_api< login_api >(1);
      edump((wdata.ws_user)(wdata.ws_password) );
      FC_ASSERT( remote_api->login( wdata.ws_user, wdata.ws_password ), "Failed to log in to API server" );

      auto wapiptr = std::make_shared<wallet_api>( wdata, remote_api );
      wapiptr->set_wallet_filename( wallet_file.generic_string() );
      wapiptr->load_wallet_file();

      fc::api<wallet_api> wapi(wapiptr);

      std::shared_ptr<fc::http::websocket_server> _websocket_server;
      if( options.count("rpc-endpoint") )
      {
         _websocket_server = std::make_shared<fc::http::websocket_server>();
         _websocket_server->on_connection([&wapi]( const fc::http::websocket_connection_ptr& c ){
            auto wsc = std::make_shared<fc::rpc::websocket_api_connection>(c, GRAPHENE_MAX_NESTED_OBJECTS);
            wsc->register_api(wapi);
            c->set_session_data( wsc );
         });
         ilog( "Listening for incoming HTTP and WS RPC requests on ${p}", ("p", options.at("rpc-endpoint").as<string>() ));
         _websocket_server->listen( fc::ip::endpoint::from_string(options.at("rpc-endpoint").as<string>()) );
         _websocket_server->start_accept();
      }

      string cert_pem = "server.pem";
      if( options.count( "rpc-tls-certificate" ) )
         cert_pem = options.at("rpc-tls-certificate").as<string>();

      std::shared_ptr<fc::http::websocket_tls_server> _websocket_tls_server;
      if( options.count("rpc-tls-endpoint") )
      {
         _websocket_tls_server = std::make_shared<fc::http::websocket_tls_server>(cert_pem);
         _websocket_tls_server->on_connection([&wapi]( const fc::http::websocket_connection_ptr& c ){
            auto wsc = std::make_shared<fc::rpc::websocket_api_connection>(c, GRAPHENE_MAX_NESTED_OBJECTS);
            wsc->register_api(wapi);
            c->set_session_data( wsc );
         });
         ilog( "Listening for incoming HTTPS and WSS RPC requests on ${p}",
               ("p", options.at("rpc-tls-endpoint").as<string>()) );
         _websocket_tls_server->listen( fc::ip::endpoint::from_string(options.at("rpc-tls-endpoint").as<string>()) );
         _websocket_tls_server->start_accept();
      }

      std::shared_ptr<fc::http::websocket_server> _http_ws_server;
      if( options.count("rpc-http-endpoint" ) )
      {
         _http_ws_server = std::make_shared<fc::http::websocket_server>();
         ilog( "Listening for incoming HTTP and WS RPC requests on ${p}",
               ("p", options.at("rpc-http-endpoint").as<string>()) );
         _http_ws_server->on_connection([&wapi]( const fc::http::websocket_connection_ptr& c ){
            auto wsc = std::make_shared<fc::rpc::websocket_api_connection>(c, GRAPHENE_MAX_NESTED_OBJECTS);
            wsc->register_api(wapi);
            c->set_session_data( wsc );
         });
         _http_ws_server->listen( fc::ip::endpoint::from_string(options.at("rpc-http-endpoint").as<string>()) );
         _http_ws_server->start_accept();
      }

      if( !options.count( "daemon" ) )
      {
         auto wallet_cli = std::make_shared<fc::rpc::cli>( GRAPHENE_MAX_NESTED_OBJECTS );
         
         wallet_cli->set_regex_secret("\\s*(unlock|set_password)\\s*");

         for( auto& name_formatter : wapiptr->get_result_formatters() )
            wallet_cli->format_result( name_formatter.first, name_formatter.second );

         std::cout << "\nType \"help\" for a list of available commands.\n";
         std::cout << "Type \"gethelp <command>\" for info about individual commands.\n\n";
         if( wapiptr->is_new() )
         {
            std::cout << "Please use the \"set_password\" method to initialize a new wallet before continuing\n";
            wallet_cli->set_prompt( "new >>> " );
         }
         else
            wallet_cli->set_prompt( "locked >>> " );

         boost::signals2::scoped_connection locked_connection( wapiptr->lock_changed.connect(
            [wallet_cli](bool locked) {
               wallet_cli->set_prompt( locked ? "locked >>> " : "unlocked >>> " );
            }));

         auto sig_set = fc::set_signal_handler( [wallet_cli](int signal) {
            ilog( "Captured SIGINT not in daemon mode, exiting" );
            fc::set_signal_handler( [](int sig) {}, SIGINT ); // reinstall an empty SIGINT handler
            wallet_cli->cancel();
         }, SIGINT );

         fc::set_signal_handler( [wallet_cli,sig_set](int signal) {
            ilog( "Captured SIGTERM not in daemon mode, exiting" );
            sig_set->cancel();
            fc::set_signal_handler( [](int sig) {}, SIGINT ); // reinstall an empty SIGINT handler
            wallet_cli->cancel();
         }, SIGTERM );
#ifdef SIGQUIT
         fc::set_signal_handler( [wallet_cli,sig_set](int signal) {
            ilog( "Captured SIGQUIT not in daemon mode, exiting" );
            sig_set->cancel();
            fc::set_signal_handler( [](int sig) {}, SIGINT ); // reinstall an empty SIGINT handler
            wallet_cli->cancel();
         }, SIGQUIT );
#endif
         boost::signals2::scoped_connection closed_connection( con->closed.connect( [wallet_cli,sig_set] {
            elog( "Server has disconnected us." );
            sig_set->cancel();
            fc::set_signal_handler( [](int sig) {}, SIGINT ); // reinstall an empty SIGINT handler
            wallet_cli->cancel();
         }));

         wallet_cli->register_api( wapi );
         wallet_cli->start();
         wallet_cli->wait();

         locked_connection.disconnect();
         closed_connection.disconnect();
      }
      else
      {
         fc::promise<int>::ptr exit_promise = fc::promise<int>::create("UNIX Signal Handler");

         fc::set_signal_handler( [&exit_promise](int signal) {
            ilog( "Captured SIGINT in daemon mode, exiting" );
            exit_promise->set_value(signal);
         }, SIGINT );

         fc::set_signal_handler( [&exit_promise](int signal) {
            ilog( "Captured SIGTERM in daemon mode, exiting" );
            exit_promise->set_value(signal);
         }, SIGTERM );
#ifdef SIGQUIT
         fc::set_signal_handler( [&exit_promise](int signal) {
            ilog( "Captured SIGQUIT in daemon mode, exiting" );
            exit_promise->set_value(signal);
         }, SIGQUIT );
#endif
         boost::signals2::scoped_connection closed_connection( con->closed.connect( [&exit_promise] {
            elog( "Server has disconnected us." );
            exit_promise->set_value(0);
         }));

         ilog( "Entering Daemon Mode, ^C to exit" );
         exit_promise->wait();

         closed_connection.disconnect();
      }

      wapi->save_wallet_file(wallet_file.generic_string());
   }
   catch ( const fc::exception& e )
   {
      std::cerr << e.to_detail_string() << "\n";
      return -1;
   }
   return 0;
}
