/*
 * Copyright (c) 2018 Lubos Ilcik, and contributors.
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

#include <graphene/app/config_util.hpp>
#include <graphene/chain/config.hpp>

#include <fc/reflect/variant.hpp>
#include <fc/string.hpp>
#include <fc/exception/exception.hpp>
#include <fc/log/console_appender.hpp>
#include <fc/log/file_appender.hpp>
#include <fc/log/logger_config.hpp>

#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/ini_parser.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string.hpp>

#include <fstream>

namespace bpo = boost::program_options;

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

// Currently, you can only specify the filenames and logging levels, which
// are all most users would want to change.  At a later time, options can
// be added to control rotation intervals, compression, and other seldom-
// used features
static void write_default_logging_config_to_stream(std::ostream& out)
{
   out << "# declare an appender named \"stderr\" that writes messages to the console\n"
          "[log.console_appender.stderr]\n"
          "stream=std_error\n\n"
          "# declare an appender named \"default\" that writes messages to default.log\n"
          "[log.file_appender.default]\n"
          "# filename can be absolute or relative to this config file\n"
          "filename=logs/default/default.log\n"
          "# Rotate log every ? minutes, if leave out default to 60\n"
          "rotation_interval=60\n"
          "# how long will logs be kept (in days), if leave out default to 1\n"
          "rotation_limit=7\n\n"
          "# declare an appender named \"p2p\" that writes messages to p2p.log\n"
          "[log.file_appender.p2p]\n"
          "# filename can be absolute or relative to this config file\n"
          "filename=logs/p2p/p2p.log\n"
          "# Rotate log every ? minutes, if leave out default to 60\n"
          "rotation_interval=60\n"
          "# how long will logs be kept (in days), if leave out default to 1\n"
          "rotation_limit=7\n\n"
          "# declare an appender named \"rpc\" that writes messages to rpc.log\n"
          "[log.file_appender.rpc]\n"
          "# filename can be absolute or relative to this config file\n"
          "filename=logs/rpc/rpc.log\n"
          "# Rotate log every ? minutes, if leave out default to 60\n"
          "rotation_interval=60\n"
          "# how long will logs be kept (in days), if leave out default to 1\n"
          "rotation_limit=7\n\n"
          "# route any messages logged to the default logger to the \"stderr\" appender and\n"
          "# \"default\" appender we declared above, if they are info level or higher\n"
          "[logger.default]\n"
          "level=info\n"
          "appenders=stderr,default\n\n"
          "# route messages sent to the \"p2p\" logger to the \"p2p\" appender declared above\n"
          "[logger.p2p]\n"
          "level=warn\n"
          "appenders=p2p\n\n"
          "# route messages sent to the \"rpc\" logger to the \"rpc\" appender declared above\n"
          "[logger.rpc]\n"
          "level=error\n"
          "appenders=rpc\n\n";
}

// logging config is too complicated to be parsed by boost::program_options,
// so we do it by hand
static fc::optional<fc::logging_config> load_logging_config_from_ini_file(const fc::path& config_ini_filename)
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
            int limit = section_tree.get_optional<int>("rotation_limit").get_value_or(1);

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

static const boost::shared_ptr<bpo::option_description> new_option_description( const std::string& name, const bpo::value_semantic* value, const std::string& description )
{
   bpo::options_description helper("");
   helper.add_options()( name.c_str(), value, description.c_str() );
   return helper.options()[0];
}


static void load_config_file(const fc::path& config_ini_path, const bpo::options_description& cfg_options,
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
}

static bool load_logging_config_file(const fc::path& config_ini_path)
{
   // try to get logging options from the config file.
   try
   {
      fc::optional<fc::logging_config> logging_config = load_logging_config_from_ini_file(config_ini_path);
      if (logging_config)
      {
         fc::configure_logging(*logging_config);
         return true;
      }
   }
   catch (const fc::exception& ex)
   {
      wlog("Error parsing logging config from logging config file ${config}, using default config", ("config", config_ini_path.preferred_string()));
   }
   return false;
}

static void create_new_config_file(const fc::path& config_ini_path, const fc::path& data_dir,
                                   const bpo::options_description& cfg_options )
{
   ilog("Writing new config file at ${path}", ("path", config_ini_path));
   if( !fc::exists(data_dir) )
      fc::create_directories(data_dir);

   auto modify_option_defaults = [](const boost::shared_ptr<bpo::option_description>& o) -> const boost::shared_ptr<bpo::option_description> {
       const std::string& name = o->long_name();
       if( name == "partial-operations" )
          return new_option_description(name, bpo::value<bool>()->default_value(true), o->description() );
       if( name == "max-ops-per-account" )
          return new_option_description(name, bpo::value<int>()->default_value(100), o->description() );
       return o;
   };
   deduplicator dedup(modify_option_defaults);
   std::ofstream out_cfg(config_ini_path.preferred_string());
   std::string plugin_header_surrounding( 78, '=' );
   for( const boost::shared_ptr<bpo::option_description> opt : cfg_options.options() )
   {
      const boost::shared_ptr<bpo::option_description> od = dedup.next(opt);
      if( !od ) continue;

      if( od->long_name().find("plugin-cfg-header-") == 0 ) // it's a plugin header
      {
         out_cfg << "\n";
         out_cfg << "# " << plugin_header_surrounding << "\n";
         out_cfg << "# " << od->description() << "\n";
         out_cfg << "# " << plugin_header_surrounding << "\n";
         out_cfg << "\n";
         continue;
      }

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

   out_cfg << "\n"
           << "# " << plugin_header_surrounding << "\n"
           << "# logging options\n"
           << "# " << plugin_header_surrounding << "\n"
           << "#\n"
           << "# Logging configuration is loaded from logging.ini by default.\n"
           << "# If logging.ini exists, logging configuration added in this file will be ignored.\n";
   out_cfg.close();
}

static void create_logging_config_file(const fc::path& config_ini_path, const fc::path& data_dir)
{
   ilog("Writing new config file at ${path}", ("path", config_ini_path));
   if (!exists(data_dir))
   {
      create_directories(data_dir);
   }

   std::ofstream out_cfg(config_ini_path.preferred_string());
   write_default_logging_config_to_stream(out_cfg);
   out_cfg.close();
}

namespace graphene { namespace app {

   void load_configuration_options(const fc::path& data_dir, const bpo::options_description& cfg_options, bpo::variables_map& options)
   {
      const auto config_ini_path = data_dir / "config.ini";
      const auto logging_ini_path = data_dir / "logging.ini";

      if(!exists(config_ini_path) && fc::exists(logging_ini_path))
      {
         // this is an uncommon case
         create_new_config_file(config_ini_path, data_dir, cfg_options);
      }
      else if(!exists(config_ini_path))
      {
         // create default config.ini and logging.ini
         create_new_config_file(config_ini_path, data_dir, cfg_options);
         create_logging_config_file(logging_ini_path, data_dir);
      }

      // load witness node configuration
      load_config_file(config_ini_path, cfg_options, options);

      // load logging configuration
      if (fc::exists(logging_ini_path))
      {
         load_logging_config_file(logging_ini_path);
      }
      else
      {
         // this is the legacy config.ini case
         load_logging_config_file(config_ini_path);
      }
   }

} } // graphene::app
