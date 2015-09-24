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

#include <graphene/delayed_node/delayed_node_plugin.hpp>
#include <graphene/chain/database.hpp>
#include <graphene/chain/worker_evaluator.hpp>
#include <graphene/app/api.hpp>

#include <fc/network/http/websocket.hpp>
#include <fc/rpc/websocket_api.hpp>
#include <fc/api.hpp>
#include <fc/smart_ref_impl.hpp>


namespace graphene { namespace delayed_node {
namespace bpo = boost::program_options;

namespace detail {
struct delayed_node_plugin_impl {
   std::string remote_endpoint;
   int delay_blocks;
   fc::http::websocket_client client;
   std::shared_ptr<fc::rpc::websocket_api_connection> client_connection;
   fc::api<graphene::app::database_api> database_api;
   boost::signals2::scoped_connection client_connection_closed;
   bool currently_fetching = false;
};
}

delayed_node_plugin::delayed_node_plugin()
   : my(new detail::delayed_node_plugin_impl)
{}

delayed_node_plugin::~delayed_node_plugin()
{}

void delayed_node_plugin::plugin_set_program_options(bpo::options_description& cli, bpo::options_description& cfg)
{
   cli.add_options()
         ("trusted-node", boost::program_options::value<std::string>()->required(), "RPC endpoint of a trusted validating node (required)")
         ("delay-block-count", boost::program_options::value<int>()->required(), "Number of blocks to delay before advancing chain state (required)")
         ;
   cfg.add(cli);
}

void delayed_node_plugin::connect()
{
   my->client_connection = std::make_shared<fc::rpc::websocket_api_connection>(*my->client.connect(my->remote_endpoint));
   my->database_api = my->client_connection->get_remote_api<graphene::app::database_api>(0);
   my->client_connection_closed = my->client_connection->closed.connect([this] {
      connection_failed();
   });
}

void delayed_node_plugin::plugin_initialize(const boost::program_options::variables_map& options)
{
   my->remote_endpoint = "ws://" + options.at("trusted-node").as<std::string>();
   my->delay_blocks = options.at("delay-block-count").as<int>();
}

void delayed_node_plugin::sync_with_trusted_node(uint32_t remote_head_block_num)
{
   struct raii {
      bool* target;
      ~raii() {
         *target = false;
      }
   };

   if (my->currently_fetching) return;
   raii releaser{&my->currently_fetching};
   my->currently_fetching = true;

   auto head_block = database().head_block_num();
   while (remote_head_block_num - head_block > my->delay_blocks) {
      fc::optional<graphene::chain::signed_block> block = my->database_api->get_block(++head_block);
      FC_ASSERT(block, "Trusted node claims it has blocks it doesn't actually have.");
      ilog("Pushing block #${n}", ("n", block->block_num()));
      database().push_block(*block);
   }
}

void delayed_node_plugin::plugin_startup()
{
   try {
      connect();

      my->database_api->set_subscribe_callback([this] (const fc::variant& v) {
         auto& updates = v.get_array();
         for( const auto& v : updates )
         {
            if( v.is_object() )
            {
               auto& obj = v.get_object();
               if( obj["id"].as<graphene::chain::object_id_type>() == graphene::chain::dynamic_global_property_id_type() )
               {
                  auto props = v.as<graphene::chain::dynamic_global_property_object>();
                  sync_with_trusted_node(props.head_block_number);
               }
            }
         }
      }, true);

      // Go ahead and get in sync now, before subscribing
      chain::dynamic_global_property_object props = my->database_api->get_dynamic_global_properties();
      sync_with_trusted_node(props.head_block_number);

      return;
   } catch (const fc::exception& e) {
      elog("Error during connection: ${e}", ("e", e.to_detail_string()));
   }
   fc::async([this]{connection_failed();});
}

void delayed_node_plugin::connection_failed()
{
   elog("Connection to trusted node failed; retrying in 5 seconds...");
   fc::schedule([this]{plugin_startup();}, fc::time_point::now() + fc::seconds(5));
}

} }
