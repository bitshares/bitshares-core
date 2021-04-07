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

#include <fc/io/raw.hpp>
#include <fc/thread/thread.hpp>
#include <fc/thread/future.hpp>

#include "simulated_network.hpp"

#include <queue>

namespace graphene { namespace net {

  struct simulated_network::node_info
  {
    std::shared_ptr<node_delegate> delegate;
    fc::future<void> message_sender_task_done;
    std::queue<message> messages_to_deliver;
    explicit node_info(std::shared_ptr<node_delegate> del) : delegate(del) {}
  };

  simulated_network::~simulated_network()
  {
    for( auto network_node_info : network_nodes )
    {
      network_node_info->message_sender_task_done.cancel_and_wait("~simulated_network()");
    }
  }

  void simulated_network::message_sender(std::shared_ptr<node_info> destination_node)
  {
    while (!destination_node->messages_to_deliver.empty())
    {
      try
      {
        const message& message_to_deliver = destination_node->messages_to_deliver.front();
        if (message_to_deliver.msg_type.value() == trx_message_type)
          destination_node->delegate->handle_transaction(message_to_deliver.as<trx_message>());
        else if (message_to_deliver.msg_type.value() == block_message_type)
        {
          std::vector<message_hash_type> contained_transaction_msg_ids;
          destination_node->delegate->handle_block(message_to_deliver.as<block_message>(), false,
                                                   contained_transaction_msg_ids);
        }
        else
          destination_node->delegate->handle_message(message_to_deliver);
      }
      catch ( const fc::exception& e )
      {
        elog( "${r}", ("r",e) );
      }
      destination_node->messages_to_deliver.pop();
    }
  }

  void simulated_network::broadcast( const message& item_to_broadcast  )
  {
    for (auto network_node_info : network_nodes)
    {
      network_node_info->messages_to_deliver.emplace(item_to_broadcast);
      if (!network_node_info->message_sender_task_done.valid() || network_node_info->message_sender_task_done.ready())
        network_node_info->message_sender_task_done = fc::async([=](){ message_sender(network_node_info); },
                                                                "simulated_network_sender");
    }
  }

  void simulated_network::add_node_delegate( std::shared_ptr<node_delegate> node_delegate_to_add )
  {
    network_nodes.push_back(std::make_shared<node_info>(node_delegate_to_add));
  }

} } // graphene::net
