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
#include <graphene/db/object.hpp>
#include <graphene/chain/address.hpp>
#include <fc/static_variant.hpp>
#include <graphene/chain/types.hpp>
#include <graphene/db/generic_index.hpp>
#include <boost/multi_index/composite_key.hpp>

namespace graphene { namespace chain {
   typedef  static_variant<address,public_key_type> address_or_key;

   /**
    * @class key_object
    * @brief maps an ID to a public key or address
    * @ingroup object 
    * @ingroup protocol
    */
   class key_object : public graphene::db::abstract_object<key_object>
   {
      public:
         static const uint8_t space_id = protocol_ids;
         static const uint8_t type_id  = key_object_type;

         key_id_type get_id()const  { return key_id_type( id.instance() ); }
         address key_address()const;
         const public_key_type& key()const { return key_data.get<public_key_type>(); }

         address_or_key key_data;
   };

   struct by_address;

   /**
    * @ingroup object_index
    */
   typedef multi_index_container<
      key_object,
      indexed_by<
         hashed_unique< tag<by_id>, member< object, object_id_type, &object::id > >,
         hashed_non_unique< tag<by_address>, const_mem_fun<key_object, address, &key_object::key_address> >
      >
   > key_multi_index_type;
   /**
    * @ingroup object_index
    */
   typedef generic_index<key_object, key_multi_index_type> key_index;
} }

FC_REFLECT_TYPENAME( graphene::chain::address_or_key )
FC_REFLECT_DERIVED( graphene::chain::key_object, (graphene::db::object), (key_data) )
