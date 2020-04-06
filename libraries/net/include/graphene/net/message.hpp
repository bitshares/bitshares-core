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
#include <boost/endian/buffers.hpp>

#include <graphene/protocol/types.hpp>

#include <fc/io/varint.hpp>
#include <fc/network/ip.hpp>
#include <fc/io/raw_fwd.hpp>
#include <fc/crypto/ripemd160.hpp>
#include <fc/reflect/typename.hpp>

namespace graphene { namespace net {

  /**
   *  Defines an 8 byte header that is always present because the minimum encrypted packet
   *  size is 8 bytes (blowfish).  The maximum message size is defined in config.hpp. The channel,
   *  and message type is also included because almost every channel will have a message type
   *  field and we might as well include it in the 8 byte header to save space.
   */
  struct message_header
  {
     boost::endian::little_uint32_buf_t size;   // number of bytes in message, capped at MAX_MESSAGE_SIZE
     boost::endian::little_uint32_buf_t msg_type;  // every channel gets a 16 bit message type specifier
     message_header()
     {
        size = 0;
        msg_type = 0;
     }
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
          FC_ASSERT( msg_type.value() == T::type );
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
              ("msg_type", msg_type.value())
              );
     }
  };

} } // graphene::net

FC_REFLECT_TYPENAME( graphene::net::message_header )
FC_REFLECT_TYPENAME( graphene::net::message )

GRAPHENE_DECLARE_EXTERNAL_SERIALIZATION( graphene::net::message_header)
GRAPHENE_DECLARE_EXTERNAL_SERIALIZATION( graphene::net::message)
