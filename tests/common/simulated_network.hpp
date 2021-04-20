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

#include <graphene/net/node.hpp>

#include <list>

namespace graphene { namespace net {

class simulated_network : public node
{
public:
   ~simulated_network() override;
   explicit simulated_network(const std::string& user_agent) : node(user_agent) {}
   void      listen_to_p2p_network() override {}
   void      connect_to_p2p_network() override {}
   void      connect_to_endpoint(const fc::ip::endpoint& ep) override {}

   fc::ip::endpoint get_actual_listening_endpoint() const override { return fc::ip::endpoint(); }

   void      sync_from(const item_id& current_head_block, const std::vector<uint32_t>& hard_fork_block_numbers) override {}
   void      broadcast(const message& item_to_broadcast) override;
   void      add_node_delegate(std::shared_ptr<node_delegate> node_delegate_to_add);

   uint32_t get_connection_count() const override { return 8; }
private:
   struct node_info;
   void message_sender(std::shared_ptr<node_info> destination_node);
   std::list<std::shared_ptr<node_info>> network_nodes;
};

using simulated_network_ptr = std::shared_ptr<simulated_network>;

} } // graphene::net
