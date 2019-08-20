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
#pragma once

#include <graphene/app/api_access.hpp>
#include <graphene/net/node.hpp>
#include <graphene/chain/database.hpp>

#include <boost/program_options.hpp>

namespace graphene { namespace app {
   namespace detail { class application_impl; }
   using std::string;

   class abstract_plugin;

   class application_options
   {
      public:
         bool enable_subscribe_to_all = false;
         bool has_market_history_plugin = false;
         uint64_t api_limit_get_account_history_operations = 100;
         uint64_t api_limit_get_account_history = 100;
         uint64_t api_limit_get_grouped_limit_orders = 101;
         uint64_t api_limit_get_relative_account_history = 100;
         uint64_t api_limit_get_account_history_by_operations = 100;
         uint64_t api_limit_get_asset_holders = 100;
         uint64_t api_limit_get_key_references = 100;
         uint64_t api_limit_get_htlc_by = 100;
         uint64_t api_limit_get_full_accounts = 10;
         uint64_t api_limit_get_full_accounts_lists = 100;
         uint64_t api_limit_get_call_orders = 300;
         uint64_t api_limit_get_settle_orders = 300;
         uint64_t api_limit_get_assets = 101;
         uint64_t api_limit_get_limit_orders = 300;
         uint64_t api_limit_get_order_book = 50;
         uint64_t api_limit_list_htlcs = 100;
   };

   class application
   {
      public:
         application();
         ~application();

         void set_program_options(boost::program_options::options_description& command_line_options,
                                  boost::program_options::options_description& configuration_file_options)const;
         void initialize(const fc::path& data_dir, const boost::program_options::variables_map& options);
         void initialize_plugins(const boost::program_options::variables_map& options);
         void startup();
         void shutdown();
         void startup_plugins();
         void shutdown_plugins();

         template<typename PluginType>
         std::shared_ptr<PluginType> register_plugin(bool auto_load = false) {
            auto plug = std::make_shared<PluginType>();
            plug->plugin_set_app(this);

            string cli_plugin_desc = plug->plugin_name() + " plugin. " + plug->plugin_description() + "\nOptions";
            boost::program_options::options_description plugin_cli_options( cli_plugin_desc ), plugin_cfg_options;
            plug->plugin_set_program_options(plugin_cli_options, plugin_cfg_options);

            if( !plugin_cli_options.options().empty() )
               _cli_options.add(plugin_cli_options);

            if( !plugin_cfg_options.options().empty() )
            {
               std::string header_name = "plugin-cfg-header-" + plug->plugin_name();
               std::string header_desc = plug->plugin_name() + " plugin options";
               _cfg_options.add_options()(header_name.c_str(), header_desc.c_str());
               _cfg_options.add(plugin_cfg_options);
            }

            add_available_plugin( plug );

            if (auto_load)
                enable_plugin(plug->plugin_name());

            return plug;
         }
         std::shared_ptr<abstract_plugin> get_plugin( const string& name )const;

         template<typename PluginType>
         std::shared_ptr<PluginType> get_plugin( const string& name ) const
         {
            std::shared_ptr<abstract_plugin> abs_plugin = get_plugin( name );
            std::shared_ptr<PluginType> result = std::dynamic_pointer_cast<PluginType>( abs_plugin );
            FC_ASSERT( result != std::shared_ptr<PluginType>(), "Unable to load plugin '${p}'", ("p",name) );
            return result;
         }

         net::node_ptr                    p2p_node();
         std::shared_ptr<chain::database> chain_database()const;
         void set_api_limit();
         void set_block_production(bool producing_blocks);
         fc::optional< api_access_info > get_api_access_info( const string& username )const;
         void set_api_access_info(const string& username, api_access_info&& permissions);

         bool is_finished_syncing()const;
         /// Emitted when syncing finishes (is_finished_syncing will return true)
         boost::signals2::signal<void()> syncing_finished;

         const application_options& get_options();

         void enable_plugin( const string& name );

         bool is_plugin_enabled(const string& name) const;

         std::shared_ptr<fc::thread> elasticsearch_thread;

   private:
         void add_available_plugin( std::shared_ptr<abstract_plugin> p );
         std::shared_ptr<detail::application_impl> my;

         boost::program_options::options_description _cli_options;
         boost::program_options::options_description _cfg_options;
   };

} }
