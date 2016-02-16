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
#include <graphene/chain/database.hpp>
#include <graphene/p2p/peer_connection.hpp>



namespace graphene { namespace p2p {
   using namespace graphene::chain;

   struct node_config
   {
      fc::ip::endpoint         server_endpoint;
      bool                     wait_if_not_available = true;
      uint32_t                 desired_peers;
      uint32_t                 max_peers;
      /** receive, but don't rebroadcast data */
      bool                     subscribe_only = false;
      public_key_type          node_id;
      vector<fc::ip::endpoint> seed_nodes;
   };

   struct by_remote_endpoint;
   struct by_peer_id;
 
   /**
    * @ingroup object_index
    */
   typedef multi_index_container<
      peer_connection_ptr,
      indexed_by<
         ordered_unique< tag<by_remote_endpoint>, 
                         const_mem_fun< peer_connection, fc::ip::endpoint, &peer_connection::get_remote_endpoint > >,
         ordered_unique< tag<by_peer_id>, member< peer_connection, public_key_type, &peer_connection::node_id > >
      >
   > peer_connection_index;


   class node : public std::enable_shared_from_this<node>
   {
      public:
         server( chain_database& db );

         void add_peer( const fc::ip::endpoint& ep );
         void configure( const node_config& cfg );

         void on_incomming_connection( peer_connection_ptr new_peer );
         void on_hello( peer_connection_ptr new_peer, hello_message m );
         void on_transaction( peer_connection_ptr from_peer, transaction_message m );
         void on_block( peer_connection_ptr from_peer, block_message m );
         void on_peers( peer_connection_ptr from_peer, peers_message  m );
         void on_error( peer_connection_ptr from_peer, error_message  m );
         void on_full_block( peer_connection_ptr from_peer, full_block_message m );
         void on_update_connections();

      private:
         /**
          *  Specifies the network interface and port upon which incoming
          *  connections should be accepted.
          */
         void      listen_on_endpoint( fc::ip::endpoint ep, bool wait_if_not_available );
         void      accept_loop();
     
         graphene::chain::database& _db;
     
         fc::tcp_server         _tcp_server;
         fc::ip::endpoint       _actual_listening_endpoint;
         fc::future<void>       _accept_loop_complete;
         peer_connection_index  _peers;

   };

} } /// graphene::p2p
