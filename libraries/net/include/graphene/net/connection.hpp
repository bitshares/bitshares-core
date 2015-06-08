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

namespace graphene { namespace net {

   namespace detail { class connection_impl; }

   class connection;
   struct message;
   struct message_header;

   typedef std::shared_ptr<connection> connection_ptr;

   /**
    * @brief defines callback interface for connections
    */
   class connection_delegate
   {
      public:
        /** Called when given network connection has completed receiving a message and it is ready
            for further processing.
        */
        virtual void on_connection_message( connection& c, const message& m ) = 0;
        /// Called when connection has been lost.
        virtual void on_connection_disconnected( connection& c ) = 0;

      protected:
        /// Only implementation is responsible for this object lifetime.
        virtual ~connection_delegate() {}
   };



   /**
    *  Manages a connection to a remote p2p node. A connection
    *  processes a stream of messages that have a common header
    *  and ensures everything is properly encrypted.
    *
    *  A connection also allows arbitrary data to be attached to it
    *  for use by other protocols built at higher levels.
    */
   class connection : public std::enable_shared_from_this<connection>
   {
      public:
        connection( const stcp_socket_ptr& c, connection_delegate* d);
        connection( connection_delegate* d );
        ~connection();

        stcp_socket_ptr  get_socket()const;
        fc::ip::endpoint remote_endpoint()const;

        void send( const message& m );

        void connect( const std::string& host_port );
        void connect( const fc::ip::endpoint& ep );
        void close();

        void exec_sync_loop();

      private:
        std::unique_ptr<detail::connection_impl> my;
   };


} } // graphene::net
