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
#pragma once
#include <graphene/net/stcp_socket.hpp>
#include <graphene/net/message.hpp>
#include <fc/exception/exception.hpp>
#include <graphene/db/level_map.hpp>

using namespace graphene::blockchain;
using namespace graphene::net;

namespace graphene { namespace net {

   namespace detail { class chain_connection_impl; }

   class chain_connection;
   typedef std::shared_ptr<chain_connection> chain_connection_ptr;

   /**
    * @brief defines callback interface for chain_connections
    */
   class chain_connection_delegate
   {
      public:
        virtual ~chain_connection_delegate(){}
        virtual void on_connection_message( chain_connection& c, const message& m ){}
        virtual void on_connection_disconnected( chain_connection& c ){}
   };



   /**
    *  Manages a connection to a remote p2p node. A connection
    *  processes a stream of messages that have a common header
    *  and ensures everything is properly encrypted.
    *
    *  A connection also allows arbitrary data to be attached to it
    *  for use by other protocols built at higher levels.
    */
   class chain_connection : public std::enable_shared_from_this<chain_connection>
   {
      public:
        chain_connection( const stcp_socket_ptr& c, chain_connection_delegate* d);
        chain_connection( chain_connection_delegate* d );
        ~chain_connection();

        stcp_socket_ptr  get_socket()const;
        fc::ip::endpoint remote_endpoint()const;

        void send( const message& m );

        void connect( const std::string& host_port );
        void connect( const fc::ip::endpoint& ep );
        void close();

        graphene::blockchain::block_id_type get_last_block_id()const;
        void                           set_last_block_id( const graphene::blockchain::block_id_type& t );

        void exec_sync_loop();
        void set_database( graphene::blockchain::chain_database*  );

      private:
        std::unique_ptr<detail::chain_connection_impl> my;
   };

} } // graphene::net
