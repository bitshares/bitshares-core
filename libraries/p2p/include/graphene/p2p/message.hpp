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
#include <fc/array.hpp>
#include <fc/io/varint.hpp>
#include <fc/network/ip.hpp>
#include <fc/io/raw.hpp>
#include <fc/crypto/ripemd160.hpp>
#include <fc/reflect/variant.hpp>

namespace graphene { namespace p2p {
  using namespace graphene::chain;

  struct message_header
  {
     uint32_t  size;   // number of bytes in message, capped at MAX_MESSAGE_SIZE
     uint32_t  msg_type;  
  };

  typedef fc::uint160_t message_hash_type;

  /**
   *  Abstracts the process of packing/unpacking a message for a 
   *  particular channel.
   */
  struct message : public message_header
  {
     std::vector<char> data;

     message(){}

     message( message&& m )
     :message_header(m),data( std::move(m.data) ){}

     message( const message& m )
     :message_header(m),data( m.data ){}

     /**
      *  Assumes that T::type specifies the message type
      */
     template<typename T>
     message( const T& m ) 
     {
        msg_type = T::type;
        data     = fc::raw::pack(m);
        size     = (uint32_t)data.size();
     }

     fc::uint160_t id()const
     {
        return fc::ripemd160::hash( data.data(), (uint32_t)data.size() );
     }

     /**
      *  Automatically checks the type and deserializes T in the
      *  opposite process from the constructor.
      */
     template<typename T>
     T as()const 
     {
         try {
          FC_ASSERT( msg_type == T::type );
          T tmp;
          if( data.size() )
          {
             fc::datastream<const char*> ds( data.data(), data.size() );
             fc::raw::unpack( ds, tmp );
          }
          else
          {
             // just to make sure that tmp shouldn't have any data
             fc::datastream<const char*> ds( nullptr, 0 );
             fc::raw::unpack( ds, tmp );
          }
          return tmp;
         } FC_RETHROW_EXCEPTIONS( warn, 
              "error unpacking network message as a '${type}'  ${x} !=? ${msg_type}", 
              ("type", fc::get_typename<T>::name() )
              ("x", T::type)
              ("msg_type", msg_type)
              );
     }
  };

  enum core_message_type_enum {
     hello_message_type       = 1000,
     transaction_message_type = 1001,
     block_message_type       = 1002,
     peer_message_type        = 1003,
     error_message_type       = 1004
  };

  struct hello_message
  {
      static const core_message_type_enum type;

      std::string         user_agent;
      uint16_t            version;
      fc::time_point      timestamp;
                          
      fc::ip::address     inbound_address;
      uint16_t            inbound_port;
      uint16_t            outbound_port;
      public_key_type     node_public_key;
      fc::sha256          chain_id;
      fc::variant_object  user_data;
      block_id_type       head_block;
  };

  struct hello_reply_message
  {
      static const core_message_type_enum type;

      fc::time_point   hello_timestamp;
      fc::time_point   reply_timestamp;
  };

  struct transaction_message 
  {
     static const core_message_type_enum type;
     signed_transaction trx;
  };

  struct block_summary_message
  {
     static const core_message_type_enum type;

     signed_block_header         header;
     vector<transaction_id_type> transaction_ids;
  };

  struct full_block_message
  {
     static const core_message_type_enum type;
     signed_block  block;
  };

  struct peers_message
  {
     static const core_message_type_enum type;

     vector<fc::ip::endpoint> peers;
  };

  struct error_message
  {
     static const core_message_type_enum type;
     string message;
  };


} } // graphene::p2p

FC_REFLECT( graphene::p2p::message_header, (size)(msg_type) )
FC_REFLECT_DERIVED( graphene::p2p::message, (graphene::p2p::message_header), (data) )
FC_REFLECT_ENUM( graphene::p2p::core_message_type_enum, 
       (hello_message_type)
       (transaction_message_type)
       (block_message_type)
       (peer_message_type)
       (error_message_type)
)
