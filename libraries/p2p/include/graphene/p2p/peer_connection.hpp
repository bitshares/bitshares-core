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

#include <graphene/p2p/node.hpp>
#include <graphene/p2p/message.hpp>
#include <graphene/p2p/message_oriented_connection.hpp>
#include <graphene/p2p/stcp_socket.hpp>

#include <boost/tuple/tuple.hpp>

#include <boost/multi_index_container.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/mem_fun.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/random_access_index.hpp>
#include <boost/multi_index/tag.hpp>
#include <boost/multi_index/sequenced_index.hpp>
#include <boost/multi_index/hashed_index.hpp>

#include <queue>
#include <boost/container/deque.hpp>
#include <fc/thread/future.hpp>

namespace graphene { namespace p2p {

  class peer_connection;
  class peer_connection_delegate
  {
     public:
       virtual void on_message(peer_connection* originating_peer, const message& received_message) = 0;
       virtual void on_connection_closed(peer_connection* originating_peer) = 0;
  };

  class peer_connection;
  typedef std::shared_ptr<peer_connection> peer_connection_ptr;


  /**
   *   Each connection maintains its own queue of messages to be sent, when an item
   *   is first pushed to the queue it starts an async fiber that will sequentially write
   *   all items until there is nothing left to be sent.  
   *
   *   If a particular connection is unable to keep up with the real-time stream of 
   *   messages to be sent then it will be disconnected.  The backlog will be measured in
   *   seconds.
   *  
   *   A multi-index container that tracks the 
   */
  class peer_connection : public message_oriented_connection_delegate,
                          public std::enable_shared_from_this<peer_connection>
  {
     public:
        enum direction_type { inbound, outbound };
        enum connection_state {
           connecting = 0,
           syncing    = 1,
           synced     = 2
        };

        fc::time_point            connection_initiation_time;
        fc::time_point            connection_closed_time;
        fc::time_point            connection_terminated_time;
        direction_type            direction     = outbound;
        connection_state          state         = connecting;
        bool                      is_firewalled = true

        //connection_state state;
        fc::microseconds clock_offset;
        fc::microseconds round_trip_delay;
        
        /// data about the peer node
        /// @{
        
        /** the unique identifier we'll use to refer to the node with.  zero-initialized before
         * we receive the hello message, at which time it will be filled with either the "node_id"
         * from the user_data field of the hello, or if none is present it will be filled with a
         * copy of node_public_key */
        public_key_type                    node_id;
        uint32_t                           core_protocol_version;
        std::string                        user_agent;
                                           
        fc::optional<std::string>          graphene_git_revision_sha;
        fc::optional<fc::time_point_sec>   graphene_git_revision_unix_timestamp;
        fc::optional<std::string>          fc_git_revision_sha;
        fc::optional<fc::time_point_sec>   fc_git_revision_unix_timestamp;
        fc::optional<std::string>          platform;
        fc::optional<uint32_t>             bitness;
        
        // for inbound connections, these fields record what the peer sent us in
        // its hello message.  For outbound, they record what we sent the peer
        // in our hello message
        fc::ip::address                   inbound_address;
        uint16_t                          inbound_port;
        uint16_t                          outbound_port;
        /// @}
        
        void send( transaction_message_ptr msg )
        {
           // if not in sent_or_received then insert into _pending_send
           // if process_send_queue is invalid or complete then 
           //    async process_send_queue 
        }

        void received_transaction( const transaction_id_type& id )
        {
           _sent_or_received.insert(id);
        }

        void process_send_queue()
        {
           // while _pending_send.size() || _pending_blocks.size()
              // while there are pending blocks, then take the oldest
              //    for each transaction id, verify that it exists in _sent_or_received
              //       else find it in the _pending_send queue and send it
              // send one from _pending_send
        }

        
        std::unordered_map<transaction_id_type, transaction_message_ptr> _pending_send;
        /// todo: make multi-index that tracks how long items have been cached and removes them
        /// after a resasonable period of time (say 10 seconds)
        std::unordered_set<transaction_id_type>                          _sent_or_received;
        std::map<uint32_t,block_message_ptr>                             _pending_blocks;


        fc::ip::endpoint get_remote_endpoint()const 
        { return get_socket().get_remote_endpoint(); }
    
        void on_message(message_oriented_connection* originating_connection, 
                        const message& received_message) override
        {
           switch( core_message_type_enum( received_message.type ) )
           {
              case hello_message_type: 
                 _node->on_hello( shared_from_this(), 
                                  received_message.as<hello_message>() );
                 break;
              case transaction_message_type: 
                 _node->on_transaction( shared_from_this(), 
                                  received_message.as<transaction_message>() );
                 break; 
              case block_message_type: 
                 _node->on_block( shared_from_this(), 
                                  received_message.as<block_message>() );
                  break; 
              case peer_message_type: 
                 _node->on_peers( shared_from_this(), 
                                  received_message.as<peers_message>() );
                 break; 
           }
        }

        void on_connection_closed(message_oriented_connection* originating_connection) override
        {
           _node->on_close( shared_from_this() );
        }

        fc::tcp_socket& get_socket() { return _message_connection.get_socket(); }
        
     private:
       peer_connection_delegate*      _node;
       fc::optional<fc::ip::endpoint> _remote_endpoint;
       message_oriented_connection    _message_connection;

  };
  typedef std::shared_ptr<peer_connection> peer_connection_ptr;


 } } // end namespace graphene::p2p

// not sent over the wire, just reflected for logging
FC_REFLECT_ENUM(graphene::p2p::peer_connection::connection_state, (connecting)(syncing)(synced) )
FC_REFLECT_ENUM(graphene::p2p::peer_connection::direction_type, (inbound)(outbound) )
