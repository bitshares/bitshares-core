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
