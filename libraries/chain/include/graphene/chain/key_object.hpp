/*
 * Copyright (c) 2015, Cryptonomex, Inc.
 * All rights reserved.
 */
#pragma once
#include <graphene/db/object.hpp>
#include <graphene/chain/address.hpp>
#include <fc/static_variant.hpp>
#include <graphene/chain/types.hpp>

namespace graphene { namespace chain {
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

         static_variant<address,public_key_type> key_data;
   };
} }

FC_REFLECT_DERIVED( graphene::chain::key_object, (graphene::db::object), (key_data) )
