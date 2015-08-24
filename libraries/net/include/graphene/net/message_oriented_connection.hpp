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
#include <fc/network/tcp_socket.hpp>
#include <graphene/net/message.hpp>

namespace graphene { namespace net {

  namespace detail { class message_oriented_connection_impl; }

  class message_oriented_connection;

  /** receives incoming messages from a message_oriented_connection object */
  class message_oriented_connection_delegate 
  {
  public:
    virtual void on_message(message_oriented_connection* originating_connection, const message& received_message) = 0;
    virtual void on_connection_closed(message_oriented_connection* originating_connection) = 0;
  };

  /** uses a secure socket to create a connection that reads and writes a stream of `fc::net::message` objects */
  class message_oriented_connection
  {
     public:
       message_oriented_connection(message_oriented_connection_delegate* delegate = nullptr);
       ~message_oriented_connection();
       fc::tcp_socket& get_socket();

       void accept();
       void bind(const fc::ip::endpoint& local_endpoint);
       void connect_to(const fc::ip::endpoint& remote_endpoint);

       void send_message(const message& message_to_send);
       void close_connection();
       void destroy_connection();

       uint64_t       get_total_bytes_sent() const;
       uint64_t       get_total_bytes_received() const;
       fc::time_point get_last_message_sent_time() const;
       fc::time_point get_last_message_received_time() const;
       fc::time_point get_connection_time() const;
       fc::sha512     get_shared_secret() const;
     private:
       std::unique_ptr<detail::message_oriented_connection_impl> my;
  };
  typedef std::shared_ptr<message_oriented_connection> message_oriented_connection_ptr;

} } // graphene::net
