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

#include <cassert>
#include <cstdint>
#include <string>

#include <fc/container/flat.hpp>
#include <fc/reflect/reflect.hpp>

namespace graphene { namespace chain {

/**
 * @brief An ID for some votable object
 *
 * This class stores an ID for a votable object. The ID is comprised of two fields: a type, and an instance. The
 * type field stores which kind of object is being voted on, and the instance stores which specific object of that
 * type is being referenced by this ID.
 *
 * A value of vote_id_type is implicitly convertible to an unsigned 32-bit integer containing only the instance. It
 * may also be implicitly assigned from a uint32_t, which will update the instance. It may not, however, be
 * implicitly constructed from a uint32_t, as in this case, the type would be unknown.
 *
 * On the wire, a vote_id_type is represented as a 32-bit integer with the type in the lower 8 bits and the instance
 * in the upper 24 bits. This means that types may never exceed 8 bits, and instances may never exceed 24 bits.
 *
 * In JSON, a vote_id_type is represented as a string "type:instance", i.e. "1:5" would be type 1 and instance 5.
 *
 * @note In the Graphene protocol, vote_id_type instances are unique across types; that is to say, if an object of
 * type 1 has instance 4, an object of type 0 may not also have instance 4. In other words, the type is not a
 * namespace for instances; it is only an informational field.
 */
struct vote_id_type
{
   /// Lower 8 bits are type; upper 24 bits are instance
   uint32_t content;

   enum vote_type
   {
      committee,
      witness,
      worker,
      VOTE_TYPE_COUNT
   };

   /// Default constructor. Sets type and instance to 0
   vote_id_type():content(0){}
   /// Construct this vote_id_type with provided type and instance
   vote_id_type(vote_type type, uint32_t instance = 0)
      : content(instance<<8 | type)
   {}
   /// Construct this vote_id_type from a serial string in the form "type:instance"
   explicit vote_id_type(const std::string& serial)
   {
      auto colon = serial.find(':');
      if( colon != std::string::npos )
         *this = vote_id_type(vote_type(std::stoul(serial.substr(0, colon))), std::stoul(serial.substr(colon+1)));
   }

   /// Set the type of this vote_id_type
   void set_type(vote_type type)
   {
      content &= 0xffffff00;
      content |= type & 0xff;
   }
   /// Get the type of this vote_id_type
   vote_type type()const
   {
      return vote_type(content & 0xff);
   }

   /// Set the instance of this vote_id_type
   void set_instance(uint32_t instance)
   {
      assert(instance < 0x01000000);
      content &= 0xff;
      content |= instance << 8;
   }
   /// Get the instance of this vote_id_type
   uint32_t instance()const
   {
      return content >> 8;
   }

   vote_id_type& operator =(vote_id_type other)
   {
      content = other.content;
      return *this;
   }
   /// Set the instance of this vote_id_type
   vote_id_type& operator =(uint32_t instance)
   {
      set_instance(instance);
      return *this;
   }
   /// Get the instance of this vote_id_type
   operator uint32_t()const
   {
      return instance();
   }

   /// Convert this vote_id_type to a serial string in the form "type:instance"
   explicit operator std::string()const
   {
      return std::to_string(type()) + ":" + std::to_string(instance());
   }
};

class global_property_object;

vote_id_type get_next_vote_id( global_property_object& gpo, vote_id_type::vote_type type );

} } // graphene::chain

namespace fc
{

class variant;

void to_variant( const graphene::chain::vote_id_type& var, fc::variant& vo );
void from_variant( const fc::variant& var, graphene::chain::vote_id_type& vo );

} // fc

FC_REFLECT_TYPENAME( graphene::chain::vote_id_type::vote_type )
FC_REFLECT_TYPENAME( fc::flat_set<graphene::chain::vote_id_type> )

FC_REFLECT_ENUM( graphene::chain::vote_id_type::vote_type, (witness)(committee)(worker)(VOTE_TYPE_COUNT) )
FC_REFLECT( graphene::chain::vote_id_type, (content) )
