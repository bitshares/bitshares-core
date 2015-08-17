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
#include <graphene/app/api.hpp>

#include <fc/network/http/websocket.hpp>
#include <fc/rpc/websocket_api.hpp>
#include <fc/api.hpp>

namespace graphene { namespace delayed_node {
namespace bpo = boost::program_options;

namespace detail {
struct delayed_node_plugin_impl {
   std::string remote_endpoint;
   int delay_blocks;
   std::shared_ptr<fc::rpc::websocket_api_connection> client_connection;
   fc::api<graphene::app::database_api> database_api;
   boost::signals2::scoped_connection client_connection_closed;
};
}

delayed_node_plugin::delayed_node_plugin()
   : my(new detail::delayed_node_plugin_impl)
{}

void delayed_node_plugin::plugin_set_program_options(bpo::options_description&, bpo::options_description& cfg)
{
   cfg.add_options()
         ("trusted_node", boost::program_options::value<std::string>()->required(), "RPC endpoint of a trusted validating node")
         ("delay_block_count", boost::program_options::value<int>()->required(), "Number of blocks to delay before advancing chain state")
         ;

}

void delayed_node_plugin::connect()
{
   fc::http::websocket_client client;
   my->client_connection = std::make_shared<fc::rpc::websocket_api_connection>(*client.connect(my->remote_endpoint));
   my->database_api = my->client_connection->get_remote_api<graphene::app::database_api>(0);
   my->client_connection_closed = my->client_connection->closed.connect([this] {
      connection_failed();
   });
}

void delayed_node_plugin::plugin_initialize(const boost::program_options::variables_map& options)
{
   my->remote_endpoint = options.at("trusted_node").as<std::string>();
   my->delay_blocks = options.at("delay_block_count").as<int>();

   try {
      connect();
   } catch (const fc::exception& e) {
      elog("Error during connection: ${e}", ("e", e.to_detail_string()));
      connection_failed();
   }
}

void delayed_node_plugin::connection_failed()
{
   elog("Connection to trusted node failed; retrying in 5 seconds...");
   fc::usleep(fc::seconds(5));

   try {
      connect();
   } catch (const fc::exception& e) {
      elog("Error during connection: ${e}", ("e", e.to_detail_string()));
      fc::async([this]{connection_failed();});
   }
}

} }
