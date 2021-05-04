/*
 * Copyright (c) 2015-2017 Cryptonomex, Inc., and contributors.
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
#include <graphene/app/config_util.hpp>

#include <graphene/witness/witness.hpp>
#include <graphene/debug_witness/debug_witness.hpp>
#include <graphene/account_history/account_history_plugin.hpp>
#include <graphene/elasticsearch/elasticsearch_plugin.hpp>
#include <graphene/market_history/market_history_plugin.hpp>
#include <graphene/delayed_node/delayed_node_plugin.hpp>
#include <graphene/snapshot/snapshot.hpp>
#include <graphene/es_objects/es_objects.hpp>
#include <graphene/grouped_orders/grouped_orders_plugin.hpp>
#include <graphene/api_helper_indexes/api_helper_indexes.hpp>
#include <graphene/custom_operations/custom_operations_plugin.hpp>

#include <fc/thread/thread.hpp>
#include <fc/interprocess/signals.hpp>
#include <fc/stacktrace.hpp>
#include <fc/log/console_appender.hpp>
#include <fc/log/logger_config.hpp>

#include <boost/filesystem.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/container/flat_set.hpp>
#include <boost/algorithm/string.hpp>

#include <graphene/utilities/git_revision.hpp>
#include <boost/algorithm/string/replace.hpp>
#include <websocketpp/version.hpp>

#include <sstream>

#ifdef WIN32
# include <signal.h>
#else
# include <csignal>
#endif

namespace bpo = boost::program_options;

/// Disable default logging
void disable_default_logging()
{
   fc::configure_logging( fc::logging_config() );
}

/// Hack to log messages to console with default color and no format via fc::console_appender
// TODO fix console_appender and use ilog() or etc instead: 1) stream is always stderr, 2) format can not change
void my_log( const std::string& s )
{
   static fc::console_appender::config my_console_config;
   static fc::console_appender my_appender( my_console_config );
   my_appender.print(s);
   my_appender.print("\n"); // This is needed, otherwise the next message will cover it
}

/// The main program
int main(int argc, char** argv) {
   fc::print_stacktrace_on_segfault();
   auto node = std::make_unique<graphene::app::application>();
   fc::oexception unhandled_exception;
   try {
      bpo::options_description app_options("BitShares Witness Node");
      bpo::options_description cfg_options("BitShares Witness Node");
      std::string default_plugins = "witness account_history market_history grouped_orders "
                                    "api_helper_indexes custom_operations";
      app_options.add_options()
            ("help,h", "Print this help message and exit.")
            ("data-dir,d", bpo::value<boost::filesystem::path>()->default_value("witness_node_data_dir"),
                    "Directory containing databases, configuration file, etc.")
            ("version,v", "Display version information")
            ("plugins", bpo::value<std::string>()->default_value(default_plugins),
                    "Space-separated list of plugins to activate")
            ("ignore-api-helper-indexes-warning", "Do not exit if api_helper_indexes plugin is not enabled.");

      auto sharable_options = std::make_shared<bpo::variables_map>();
      auto& options = *sharable_options;

      bpo::options_description cli;
      bpo::options_description cfg;
      node->set_program_options(cli, cfg);
      cfg_options.add(cfg);

      cfg_options.add_options()
            ("plugins", bpo::value<std::string>()->default_value(default_plugins),
                    "Space-separated list of plugins to activate")
            ("ignore-api-helper-indexes-warning", "Do not exit if api_helper_indexes plugin is not enabled.");

      node->register_plugin<graphene::witness_plugin::witness_plugin>();
      node->register_plugin<graphene::debug_witness_plugin::debug_witness_plugin>();
      node->register_plugin<graphene::account_history::account_history_plugin>();
      node->register_plugin<graphene::elasticsearch::elasticsearch_plugin>();
      node->register_plugin<graphene::market_history::market_history_plugin>();
      node->register_plugin<graphene::delayed_node::delayed_node_plugin>();
      node->register_plugin<graphene::snapshot_plugin::snapshot_plugin>();
      node->register_plugin<graphene::es_objects::es_objects_plugin>();
      node->register_plugin<graphene::grouped_orders::grouped_orders_plugin>();
      node->register_plugin<graphene::api_helper_indexes::api_helper_indexes>();
      node->register_plugin<graphene::custom_operations::custom_operations_plugin>();

      // add plugin options to config
      try
      {
         bpo::options_description tmp_cli;
         bpo::options_description tmp_cfg;
         node->set_program_options(tmp_cli, tmp_cfg);
         app_options.add(tmp_cli);
         cfg_options.add(tmp_cfg);
         bpo::store(bpo::parse_command_line(argc, argv, app_options), options);
      }
      catch (const boost::program_options::error& e)
      {
         disable_default_logging();
         std::stringstream ss;
         ss << "Error parsing command line: " << e.what();
         my_log( ss.str() );
         return EXIT_FAILURE;
      }

      if( options.count("version") > 0 )
      {
         disable_default_logging();
         std::stringstream ss;
         ss << "Version: " << graphene::utilities::git_revision_description << "\n";
         ss << "SHA: " << graphene::utilities::git_revision_sha << "\n";
         ss << "Timestamp: " << fc::get_approximate_relative_time_string(fc::time_point_sec(
                                             graphene::utilities::git_revision_unix_timestamp)) << "\n";
         ss << "SSL: " << OPENSSL_VERSION_TEXT << "\n";
         ss << "Boost: " << boost::replace_all_copy(std::string(BOOST_LIB_VERSION), "_", ".") << "\n";
         ss << "Websocket++: " << websocketpp::major_version << "." << websocketpp::minor_version
                                      << "." << websocketpp::patch_version; // No end of line in the end
         my_log( ss.str() );
         return EXIT_SUCCESS;
      }
      if( options.count("help") > 0 )
      {
         disable_default_logging();
         std::stringstream ss;
         ss << app_options << "\n";
         my_log( ss.str() );
         return EXIT_SUCCESS;
      }

      fc::path data_dir;
      if( options.count("data-dir") > 0 )
      {
         data_dir = options["data-dir"].as<boost::filesystem::path>();
         if( data_dir.is_relative() )
            data_dir = fc::current_path() / data_dir;
      }
      graphene::app::load_configuration_options(data_dir, cfg_options, options);

      std::set<std::string> plugins;
      boost::split(plugins, options.at("plugins").as<std::string>(), [](char c){return c == ' ';});

      if( plugins.count("account_history") > 0 && plugins.count("elasticsearch") > 0 ) {
         disable_default_logging();
         std::stringstream ss;
         ss << "Plugin conflict: Cannot load both account_history plugin and elasticsearch plugin";
         my_log( ss.str() );
         return EXIT_FAILURE;
      }

      if( plugins.count("api_helper_indexes") == 0 && options.count("ignore-api-helper-indexes-warning") == 0
          && ( options.count("rpc-endpoint") > 0 || options.count("rpc-tls-endpoint") > 0 ) )
      {
         disable_default_logging();
         std::stringstream ss;
         ss << "\nIf this is an API node, please enable api_helper_indexes plugin."
               "\nIf this is not an API node, please start with \"--ignore-api-helper-indexes-warning\""
               " or enable it in config.ini file.\n";
         my_log( ss.str() );
         return EXIT_FAILURE;
      }

      std::for_each(plugins.begin(), plugins.end(), [&node](const std::string& plug) mutable {
         if (!plug.empty()) {
            node->enable_plugin(plug);
         }
      });

      bpo::notify(options);

      node->initialize(data_dir, sharable_options);

      node->startup();

      fc::promise<int>::ptr exit_promise = fc::promise<int>::create("UNIX Signal Handler");

      fc::set_signal_handler([&exit_promise](int the_signal) {
         wlog( "Caught SIGINT, attempting to exit cleanly" );
         exit_promise->set_value(the_signal);
      }, SIGINT);

      fc::set_signal_handler([&exit_promise](int the_signal) {
         wlog( "Caught SIGTERM, attempting to exit cleanly" );
         exit_promise->set_value(the_signal);
      }, SIGTERM);

#ifdef SIGQUIT
      fc::set_signal_handler( [&exit_promise](int the_signal) {
         wlog( "Caught SIGQUIT, attempting to exit cleanly" );
         exit_promise->set_value(the_signal);
      }, SIGQUIT );
#endif

      ilog("Started BitShares node on a chain with ${h} blocks.", ("h", node->chain_database()->head_block_num()));
      ilog("Chain ID is ${id}", ("id", node->chain_database()->get_chain_id()) );

      auto caught_signal = exit_promise->wait();
      ilog("Exiting from signal ${n}", ("n", caught_signal));
      return EXIT_SUCCESS;
   } catch( const fc::exception& e ) {
      // deleting the node can yield, so do this outside the exception handler
      unhandled_exception = e;
   }

   if (unhandled_exception)
   {
      elog("Exiting with error:\n${e}", ("e", unhandled_exception->to_detail_string()));
      return EXIT_FAILURE;
   }
}

