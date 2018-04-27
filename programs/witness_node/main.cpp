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

#include <graphene/witness/witness.hpp>
#include <graphene/debug_witness/debug_witness.hpp>
#include <graphene/account_history/account_history_plugin.hpp>
#include <graphene/elasticsearch/elasticsearch_plugin.hpp>
#include <graphene/market_history/market_history_plugin.hpp>
#include <graphene/delayed_node/delayed_node_plugin.hpp>
#include <graphene/snapshot/snapshot.hpp>
#include <graphene/es_objects/es_objects.hpp>
#include <graphene/grouped_orders/grouped_orders_plugin.hpp>

#include <fc/exception/exception.hpp>
#include <fc/thread/thread.hpp>
#include <fc/interprocess/signals.hpp>
#include <fc/log/console_appender.hpp>
#include <fc/log/file_appender.hpp>
#include <fc/log/logger.hpp>
#include <fc/log/logger_config.hpp>

#include <boost/filesystem.hpp>

#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/ini_parser.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/container/flat_set.hpp>

#include <graphene/utilities/git_revision.hpp>
#include <boost/version.hpp>
#include <boost/algorithm/string/replace.hpp>
#include <websocketpp/version.hpp>

#include <iostream>
#include <fstream>

#ifdef WIN32
# include <signal.h> 
#else
# include <csignal>
#endif

using namespace graphene;
namespace bpo = boost::program_options;
         
void write_default_logging_config_to_stream(std::ostream& out);
fc::optional<fc::logging_config> load_logging_config_from_ini_file(const fc::path& config_ini_filename);

class deduplicator 
{
   public:
      deduplicator() : modifier(nullptr) {}

      deduplicator(const boost::shared_ptr<bpo::option_description> (*mod_fn)(const boost::shared_ptr<bpo::option_description>&))
              : modifier(mod_fn) {}

      const boost::shared_ptr<bpo::option_description> next(const boost::shared_ptr<bpo::option_description>& o)
      {
         const std::string name = o->long_name();
         if( seen.find( name ) != seen.end() )
            return nullptr;
         seen.insert(name);
         return modifier ? modifier(o) : o;
      }

   private:
      boost::container::flat_set<std::string> seen;
      const boost::shared_ptr<bpo::option_description> (*modifier)(const boost::shared_ptr<bpo::option_description>&);
};

static void load_config_file( const fc::path& config_ini_path, const bpo::options_description& cfg_options,
                              bpo::variables_map& options )
{
   deduplicator dedup;
   bpo::options_description unique_options("Graphene Witness Node");
   for( const boost::shared_ptr<bpo::option_description> opt : cfg_options.options() )
   {
      const boost::shared_ptr<bpo::option_description> od = dedup.next(opt);
      if( !od ) continue;
      unique_options.add( od );
   }

   // get the basic options
   bpo::store(bpo::parse_config_file<char>(config_ini_path.preferred_string().c_str(),
              unique_options, true), options);

   // try to get logging options from the config file.
   try
   {
      fc::optional<fc::logging_config> logging_config = load_logging_config_from_ini_file(config_ini_path);
      if (logging_config)
         fc::configure_logging(*logging_config);
   }
   catch (const fc::exception&)
   {
      wlog("Error parsing logging config from config file ${config}, using default config", ("config", config_ini_path.preferred_string()));
   }
}

const boost::shared_ptr<bpo::option_description> new_option_description( const std::string& name, const bpo::value_semantic* value, const std::string& description )
{
    bpo::options_description helper("");
    helper.add_options()( name.c_str(), value, description.c_str() );
    return helper.options()[0];
}

static void create_new_config_file( const fc::path& config_ini_path, const fc::path& data_dir,
                                    const bpo::options_description& cfg_options )
{
   ilog("Writing new config file at ${path}", ("path", config_ini_path));
   if( !fc::exists(data_dir) )
      fc::create_directories(data_dir);

   auto modify_option_defaults = [](const boost::shared_ptr<bpo::option_description>& o) -> const boost::shared_ptr<bpo::option_description> {
       const std::string& name = o->long_name();
       if( name == "partial-operations" )
          return new_option_description( name, bpo::value<bool>()->default_value(true), o->description() );
       if( name == "max-ops-per-account" )
          return new_option_description( name, bpo::value<int>()->default_value(1000), o->description() );
       return o;
   };
   deduplicator dedup(modify_option_defaults);
   std::ofstream out_cfg(config_ini_path.preferred_string());
   for( const boost::shared_ptr<bpo::option_description> opt : cfg_options.options() )
   {
      const boost::shared_ptr<bpo::option_description> od = dedup.next(opt);
      if( !od ) continue;

      if( !od->description().empty() )
         out_cfg << "# " << od->description() << "\n";
      boost::any store;
      if( !od->semantic()->apply_default(store) )
         out_cfg << "# " << od->long_name() << " = \n";
      else {
         auto example = od->format_parameter();
         if( example.empty() )
            // This is a boolean switch
            out_cfg << od->long_name() << " = " << "false\n";
         else {
            // The string is formatted "arg (=<interesting part>)"
            example.erase(0, 6);
            example.erase(example.length()-1);
            out_cfg << od->long_name() << " = " << example << "\n";
         }
      }
      out_cfg << "\n";
   }
   write_default_logging_config_to_stream(out_cfg);
   out_cfg.close();
   // read the default logging config we just wrote out to the file and start using it
   fc::optional<fc::logging_config> logging_config = load_logging_config_from_ini_file(config_ini_path);
   if (logging_config)
      fc::configure_logging(*logging_config);
}

int main(int argc, char** argv) {
   app::application* node = new app::application();
   fc::oexception unhandled_exception;
   try {
      bpo::options_description app_options("Graphene Witness Node");
      bpo::options_description cfg_options("Graphene Witness Node");
      app_options.add_options()
            ("help,h", "Print this help message and exit.")
            ("data-dir,d", bpo::value<boost::filesystem::path>()->default_value("witness_node_data_dir"), "Directory containing databases, configuration file, etc.")
            ("version,v", "Display version information")
            ;

      bpo::variables_map options;

      auto witness_plug = node->register_plugin<witness_plugin::witness_plugin>();
      auto debug_witness_plug = node->register_plugin<debug_witness_plugin::debug_witness_plugin>();
      auto history_plug = node->register_plugin<account_history::account_history_plugin>();
      auto elasticsearch_plug = node->register_plugin<elasticsearch::elasticsearch_plugin>();
      auto market_history_plug = node->register_plugin<market_history::market_history_plugin>();
      auto delayed_plug = node->register_plugin<delayed_node::delayed_node_plugin>();
      auto snapshot_plug = node->register_plugin<snapshot_plugin::snapshot_plugin>();
      auto es_objects_plug = node->register_plugin<es_objects::es_objects_plugin>();
      auto grouped_orders_plug = node->register_plugin<grouped_orders::grouped_orders_plugin>();

      try
      {
         bpo::options_description cli, cfg;
         node->set_program_options(cli, cfg);
         app_options.add(cli);
         cfg_options.add(cfg);
         bpo::store(bpo::parse_command_line(argc, argv, app_options), options);
      }
      catch (const boost::program_options::error& e)
      {
        std::cerr << "Error parsing command line: " << e.what() << "\n";
        return 1;
      }

      if( options.count("help") )
      {
         std::cout << app_options << "\n";
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

      fc::path data_dir;
      if( options.count("data-dir") )
      {
         data_dir = options["data-dir"].as<boost::filesystem::path>();
         if( data_dir.is_relative() )
            data_dir = fc::current_path() / data_dir;
      }

      fc::path config_ini_path = data_dir / "config.ini";
      if( !fc::exists(config_ini_path) )
         create_new_config_file( config_ini_path, data_dir, cfg_options );
      load_config_file( config_ini_path, cfg_options, options );

      bpo::notify(options);
      node->initialize(data_dir, options);
      node->initialize_plugins( options );

      node->startup();
      node->startup_plugins();

      fc::promise<int>::ptr exit_promise = new fc::promise<int>("UNIX Signal Handler");

      fc::set_signal_handler([&exit_promise](int signal) {
         elog( "Caught SIGINT attempting to exit cleanly" );
         exit_promise->set_value(signal);
      }, SIGINT);

      fc::set_signal_handler([&exit_promise](int signal) {
         elog( "Caught SIGTERM attempting to exit cleanly" );
         exit_promise->set_value(signal);
      }, SIGTERM);

      ilog("Started BitShares node on a chain with ${h} blocks.", ("h", node->chain_database()->head_block_num()));
      ilog("Chain ID is ${id}", ("id", node->chain_database()->get_chain_id()) );

      int signal = exit_promise->wait();
      ilog("Exiting from signal ${n}", ("n", signal));
      node->shutdown_plugins();
      node->shutdown();
      delete node;
      return 0;
   } catch( const fc::exception& e ) {
      // deleting the node can yield, so do this outside the exception handler
      unhandled_exception = e;
   }

   if (unhandled_exception)
   {
      elog("Exiting with error:\n${e}", ("e", unhandled_exception->to_detail_string()));
      node->shutdown();
      delete node;
      return 1;
   }
}

// logging config is too complicated to be parsed by boost::program_options, 
// so we do it by hand
//
// Currently, you can only specify the filenames and logging levels, which
// are all most users would want to change.  At a later time, options can
// be added to control rotation intervals, compression, and other seldom-
// used features
void write_default_logging_config_to_stream(std::ostream& out)
{
   out << "# declare an appender named \"stderr\" that writes messages to the console\n"
          "[log.console_appender.stderr]\n"
          "stream=std_error\n\n"
          "# declare an appender named \"p2p\" that writes messages to p2p.log\n"
          "[log.file_appender.p2p]\n"
          "# filename can be absolute or relative to this config file\n"
          "filename=logs/p2p/p2p.log\n"
          "# Rotate log every ? minutes, if leave out default to 60\n"
          "rotation_interval=60\n"
          "# how long will logs be kept (in days), if leave out default to 7\n"
          "rotation_limit=7\n\n"
          "# route any messages logged to the default logger to the \"stderr\" logger we\n"
          "# declared above, if they are info level are higher\n"
          "[logger.default]\n"
          "level=info\n"
          "appenders=stderr\n\n"
          "# route messages sent to the \"p2p\" logger to the p2p appender declared above\n"
          "[logger.p2p]\n"
          "level=info\n"
          "appenders=p2p\n\n";
}

fc::optional<fc::logging_config> load_logging_config_from_ini_file(const fc::path& config_ini_filename)
{
   try
   {
      fc::logging_config logging_config;
      bool found_logging_config = false;

      boost::property_tree::ptree config_ini_tree;
      boost::property_tree::ini_parser::read_ini(config_ini_filename.preferred_string().c_str(), config_ini_tree);
      for (const auto& section : config_ini_tree)
      {
         const std::string& section_name = section.first;
         const boost::property_tree::ptree& section_tree = section.second;

         const std::string console_appender_section_prefix = "log.console_appender.";
         const std::string file_appender_section_prefix = "log.file_appender.";
         const std::string logger_section_prefix = "logger.";

         if (boost::starts_with(section_name, console_appender_section_prefix))
         {
            std::string console_appender_name = section_name.substr(console_appender_section_prefix.length());
            std::string stream_name = section_tree.get<std::string>("stream");

            // construct a default console appender config here
            // stdout/stderr will be taken from ini file, everything else hard-coded here
            fc::console_appender::config console_appender_config;
            console_appender_config.level_colors.emplace_back(
               fc::console_appender::level_color(fc::log_level::debug, 
                                                 fc::console_appender::color::green));
            console_appender_config.level_colors.emplace_back(
               fc::console_appender::level_color(fc::log_level::warn, 
                                                 fc::console_appender::color::brown));
            console_appender_config.level_colors.emplace_back(
               fc::console_appender::level_color(fc::log_level::error, 
                                                 fc::console_appender::color::cyan));
            console_appender_config.stream = fc::variant(stream_name).as<fc::console_appender::stream::type>(GRAPHENE_MAX_NESTED_OBJECTS);
            logging_config.appenders.push_back(fc::appender_config(console_appender_name, "console", fc::variant(console_appender_config, GRAPHENE_MAX_NESTED_OBJECTS)));
            found_logging_config = true;
         }
         else if (boost::starts_with(section_name, file_appender_section_prefix))
         {
            std::string file_appender_name = section_name.substr(file_appender_section_prefix.length());
            fc::path file_name = section_tree.get<std::string>("filename");
            if (file_name.is_relative())
               file_name = fc::absolute(config_ini_filename).parent_path() / file_name;
            int interval = section_tree.get_optional<int>("rotation_interval").get_value_or(60);
            int limit = section_tree.get_optional<int>("rotation_limit").get_value_or(7);

            // construct a default file appender config here
            // filename will be taken from ini file, everything else hard-coded here
            fc::file_appender::config file_appender_config;
            file_appender_config.filename = file_name;
            file_appender_config.flush = true;
            file_appender_config.rotate = true;
            file_appender_config.rotation_interval = fc::minutes(interval);
            file_appender_config.rotation_limit = fc::days(limit);
            logging_config.appenders.push_back(fc::appender_config(file_appender_name, "file", fc::variant(file_appender_config, GRAPHENE_MAX_NESTED_OBJECTS)));
            found_logging_config = true;
         }
         else if (boost::starts_with(section_name, logger_section_prefix))
         {
            std::string logger_name = section_name.substr(logger_section_prefix.length());
            std::string level_string = section_tree.get<std::string>("level");
            std::string appenders_string = section_tree.get<std::string>("appenders");
            fc::logger_config logger_config(logger_name);
            logger_config.level = fc::variant(level_string).as<fc::log_level>(5);
            boost::split(logger_config.appenders, appenders_string, 
                         boost::is_any_of(" ,"), 
                         boost::token_compress_on);
            logging_config.loggers.push_back(logger_config);
            found_logging_config = true;
         }
      }
      if (found_logging_config)
         return logging_config;
      else
         return fc::optional<fc::logging_config>();
   }
   FC_RETHROW_EXCEPTIONS(warn, "")
}
