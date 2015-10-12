/*
 * Copyright (c) 2015 Cryptonomex, Inc., and contributors.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:
 *
 * 1. Any modified source or binaries are used only with the BitShares network.
 *
 * 2. Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
 *
 * 3. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */
#pragma once

#include <graphene/app/api_access.hpp>
#include <graphene/net/node.hpp>
#include <graphene/chain/database.hpp>

#include <boost/program_options.hpp>

namespace graphene { namespace app {
   namespace detail { class application_impl; }
   using std::string;

   class abstract_plugin;

   class application
   {
      public:
         application();
         ~application();

         void set_program_options( boost::program_options::options_description& command_line_options,
                                   boost::program_options::options_description& configuration_file_options )const;
         void initialize(const fc::path& data_dir, const boost::program_options::variables_map&options);
         void initialize_plugins( const boost::program_options::variables_map& options );
         void startup();
         void shutdown();
         void startup_plugins();
         void shutdown_plugins();

         template<typename PluginType>
         std::shared_ptr<PluginType> register_plugin()
         {
            auto plug = std::make_shared<PluginType>();
            plug->plugin_set_app(this);

            boost::program_options::options_description plugin_cli_options("Options for plugin " + plug->plugin_name()), plugin_cfg_options;
            plug->plugin_set_program_options(plugin_cli_options, plugin_cfg_options);
            if( !plugin_cli_options.options().empty() )
               _cli_options.add(plugin_cli_options);
            if( !plugin_cfg_options.options().empty() )
               _cfg_options.add(plugin_cfg_options);

            add_plugin( plug->plugin_name(), plug );
            return plug;
         }
         std::shared_ptr<abstract_plugin> get_plugin( const string& name )const;

         template<typename PluginType>
         std::shared_ptr<PluginType> get_plugin( const string& name ) const
         {
            std::shared_ptr<abstract_plugin> abs_plugin = get_plugin( name );
            std::shared_ptr<PluginType> result = std::dynamic_pointer_cast<PluginType>( abs_plugin );
            FC_ASSERT( result != std::shared_ptr<PluginType>() );
            return result;
         }

         net::node_ptr                    p2p_node();
         std::shared_ptr<chain::database> chain_database()const;

         void set_block_production(bool producing_blocks);
         fc::optional< api_access_info > get_api_access_info( const string& username )const;
         void set_api_access_info(const string& username, api_access_info&& permissions);

         bool is_finished_syncing()const;
         /// Emitted when syncing finishes (is_finished_syncing will return true)
         boost::signals2::signal<void()> syncing_finished;

      private:
         void add_plugin( const string& name, std::shared_ptr<abstract_plugin> p );
         std::shared_ptr<detail::application_impl> my;

         boost::program_options::options_description _cli_options;
         boost::program_options::options_description _cfg_options;
   };

} }
