/*
 * Copyright (c) 2015, Cryptonomex, Inc.
 * All rights reserved.
 *
 * This source code is provided for evaluation in private test networks only, until September 8, 2015. After this date, this license expires and
 * the code may not be used, modified or distributed for any purpose. Redistribution and use in source and binary forms, with or without modification,
 * are permitted until September 8, 2015, provided that the following conditions are met:
 *
 * 1. The code and/or derivative works are used only for private test networks consisting of no more than 10 P2P nodes.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#pragma once

#include <graphene/app/application.hpp>

#include <boost/program_options.hpp>
#include <fc/io/json.hpp>

namespace graphene { namespace app {
namespace bpo = boost::program_options;

class abstract_plugin
{
   public:
      virtual ~abstract_plugin(){}
      virtual std::string plugin_name()const = 0;

      /**
       * @brief Perform early startup routines and register plugin indexes, callbacks, etc.
       *
       * Plugins MUST supply a method initialize() which will be called early in the application startup. This method
       * should contain early setup code such as initializing variables, adding indexes to the database, registering
       * callback methods from the database, adding APIs, etc., as well as applying any options in the @ref options map
       *
       * This method is called BEFORE the database is open, therefore any routines which require any chain state MUST
       * NOT be called by this method. These routines should be performed in startup() instead.
       *
       * @param options The options passed to the application, via configuration files or command line
       */
      virtual void plugin_initialize( const bpo::variables_map& options ) = 0;

      /**
       * @brief Begin normal runtime operations
       *
       * Plugins MUST supply a method startup() which will be called at the end of application startup. This method
       * should contain code which schedules any tasks, or requires chain state.
       */
      virtual void plugin_startup() = 0;

      /**
       * @brief Cleanly shut down the plugin.
       *
       * This is called to request a clean shutdown (e.g. due to SIGINT or SIGTERM).
       */
      virtual void plugin_shutdown() = 0;

      /**
       * @brief Register the application instance with the plugin.
       *
       * This is called by the framework to set the application.
       */
      virtual void plugin_set_app( application* a ) = 0;

      /**
       * @brief Fill in command line parameters used by the plugin.
       *
       * @param command_line_options All options this plugin supports taking on the command-line
       * @param config_file_options All options this plugin supports storing in a configuration file
       *
       * This method populates its arguments with any
       * command-line and configuration file options the plugin supports.
       * If a plugin does not need these options, it
       * may simply provide an empty implementation of this method.
       */
      virtual void plugin_set_program_options(
         bpo::options_description& command_line_options,
         bpo::options_description& config_file_options
         ) = 0;
};

/**
 * Provides basic default implementations of abstract_plugin functions.
 */

class plugin : public abstract_plugin
{
   public:
      plugin();
      virtual ~plugin() override;

      virtual std::string plugin_name()const override;
      virtual void plugin_initialize( const bpo::variables_map& options ) override;
      virtual void plugin_startup() override;
      virtual void plugin_shutdown() override;
      virtual void plugin_set_app( application* app ) override;
      virtual void plugin_set_program_options(
         bpo::options_description& command_line_options,
         bpo::options_description& config_file_options
         ) override;

      chain::database& database() { return *app().chain_database(); }
      application& app()const { assert(_app); return *_app; }
   protected:
      net::node& p2p_node() { return *app().p2p_node(); }

   private:
      application* _app = nullptr;
};

/// @group Some useful tools for boost::program_options arguments using vectors of JSON strings
/// @{
template<typename T>
T dejsonify(const string& s)
{
   return fc::json::from_string(s).as<T>();
}

#define DEFAULT_VALUE_VECTOR(value) default_value({fc::json::to_string(value)}, fc::json::to_string(value))
#define LOAD_VALUE_SET(options, name, container, type) \
if( options.count(name) ) { \
      const std::vector<std::string>& ops = options[name].as<std::vector<std::string>>(); \
      std::transform(ops.begin(), ops.end(), std::inserter(container, container.end()), &graphene::app::dejsonify<type>); \
}
/// @}

} } //graphene::app
