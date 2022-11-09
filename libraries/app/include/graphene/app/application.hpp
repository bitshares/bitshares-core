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

         bool has_api_helper_indexes_plugin = false;
         bool has_market_history_plugin = false;

         uint32_t api_limit_get_account_history = 100;
         uint32_t api_limit_get_account_history_operations = 100;
         uint32_t api_limit_get_account_history_by_operations = 100;
         uint32_t api_limit_get_relative_account_history = 100;
         uint32_t api_limit_get_market_history = 200;
         uint32_t api_limit_get_trade_history = 100;
         uint32_t api_limit_get_trade_history_by_sequence = 100;
         uint32_t api_limit_get_liquidity_pool_history = 101;
         uint32_t api_limit_get_top_markets = 100;
         uint32_t api_limit_get_assets = 101;
         uint32_t api_limit_get_asset_holders = 100;
         uint32_t api_limit_get_key_references = 100;
         uint32_t api_limit_get_full_accounts = 50;
         uint32_t api_limit_get_full_accounts_lists = 500;
         uint32_t api_limit_get_full_accounts_subscribe = 100;
         uint32_t api_limit_get_top_voters = 200;
         uint32_t api_limit_get_limit_orders = 300;
         uint32_t api_limit_get_limit_orders_by_account = 101;
         uint32_t api_limit_get_account_limit_orders = 101;
         uint32_t api_limit_get_grouped_limit_orders = 101;
         uint32_t api_limit_get_order_book = 50;
         uint32_t api_limit_get_call_orders = 300;
         uint32_t api_limit_get_settle_orders = 300;
         uint32_t api_limit_get_collateral_bids = 100;
         uint32_t api_limit_lookup_accounts = 1000;
         uint32_t api_limit_lookup_witness_accounts = 1000;
         uint32_t api_limit_lookup_committee_member_accounts = 1000;
         uint32_t api_limit_lookup_vote_ids = 1000;
         uint32_t api_limit_list_htlcs = 100;
         uint32_t api_limit_get_htlc_by = 100;
         uint32_t api_limit_get_withdraw_permissions_by_giver = 101;
         uint32_t api_limit_get_withdraw_permissions_by_recipient = 101;
         uint32_t api_limit_get_tickets = 101;
         uint32_t api_limit_get_liquidity_pools = 101;
         uint32_t api_limit_get_samet_funds = 101;
         uint32_t api_limit_get_credit_offers = 101;
         uint32_t api_limit_get_storage_info = 101;

         static constexpr application_options get_default()
         {
            constexpr application_options default_options;
            return default_options;
         }
   };

   class application
   {
      public:
         application();
         ~application();

         void set_program_options(boost::program_options::options_description& command_line_options,
                                  boost::program_options::options_description& configuration_file_options)const;
         void initialize(const fc::path& data_dir,
                         std::shared_ptr<boost::program_options::variables_map> options) const;
         void startup();

         template<typename PluginType>
         std::shared_ptr<PluginType> register_plugin(bool auto_load = false) {
            auto plug = std::make_shared<PluginType>(*this);

            string cli_plugin_desc = plug->plugin_name() + " plugin. " + plug->plugin_description() + "\nOptions";
            boost::program_options::options_description plugin_cli_options( cli_plugin_desc );
            boost::program_options::options_description plugin_cfg_options;
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

         const application_options& get_options() const;

         void enable_plugin( const string& name ) const;

         bool is_plugin_enabled(const string& name) const;

         std::shared_ptr<fc::thread> elasticsearch_thread;

         const string& get_node_info() const;

   private:
         /// Add an available plugin
         void add_available_plugin( std::shared_ptr<abstract_plugin> p ) const;

         std::shared_ptr<detail::application_impl> my;

         boost::program_options::options_description _cli_options;
         boost::program_options::options_description _cfg_options;
   };

} }

FC_REFLECT( graphene::app::application_options,
            ( enable_subscribe_to_all )
            ( has_api_helper_indexes_plugin )
            ( has_market_history_plugin )
            ( api_limit_get_account_history )
            ( api_limit_get_account_history_operations )
            ( api_limit_get_account_history_by_operations )
            ( api_limit_get_relative_account_history )
            ( api_limit_get_market_history )
            ( api_limit_get_trade_history )
            ( api_limit_get_trade_history_by_sequence )
            ( api_limit_get_liquidity_pool_history )
            ( api_limit_get_top_markets )
            ( api_limit_get_assets )
            ( api_limit_get_asset_holders )
            ( api_limit_get_key_references )
            ( api_limit_get_full_accounts )
            ( api_limit_get_full_accounts_lists )
            ( api_limit_get_full_accounts_subscribe )
            ( api_limit_get_top_voters )
            ( api_limit_get_limit_orders )
            ( api_limit_get_limit_orders_by_account )
            ( api_limit_get_account_limit_orders )
            ( api_limit_get_grouped_limit_orders )
            ( api_limit_get_order_book )
            ( api_limit_get_call_orders )
            ( api_limit_get_settle_orders )
            ( api_limit_get_collateral_bids )
            ( api_limit_lookup_accounts )
            ( api_limit_lookup_witness_accounts )
            ( api_limit_lookup_committee_member_accounts )
            ( api_limit_lookup_vote_ids )
            ( api_limit_list_htlcs )
            ( api_limit_get_htlc_by )
            ( api_limit_get_withdraw_permissions_by_giver )
            ( api_limit_get_withdraw_permissions_by_recipient )
            ( api_limit_get_tickets )
            ( api_limit_get_liquidity_pools )
            ( api_limit_get_samet_funds )
            ( api_limit_get_credit_offers )
            ( api_limit_get_storage_info )
          )

GRAPHENE_DECLARE_EXTERNAL_SERIALIZATION( graphene::app::application_options )
