/*
 * Copyright (c) 2015, Cryptonomex, Inc.
 * All rights reserved.
 */
#pragma once

namespace graphene { namespace chain {

   /**
    * @class blinded_balance_object
    * @brief tracks a blinded balance commitment 
    * @ingroup object
    * @ingroup protocol
    */
   class blinded_balance_object : public graphene::db::abstract_object<blinded_balance_object>
   {
      public:
         static const uint8_t space_id = protocol_ids;
         static const uint8_t type_id  = blinded_balance_object_type;

         fc::ecc::commitment_type                commitment;
         asset_id_type                           asset_id;
         static_variant<address,account_id_type> owner;
   };

   struct by_asset;
   struct by_owner;
   struct by_commitment;

   /**
    * @ingroup object_index
    */
   typedef multi_index_container<
      blinded_balance_object,
      indexed_by<
         ordered_unique< tag<by_id>, member< object, object_id_type, &object::id > >,
         hashed_unique< tag<by_commitment>, member<blinded_balance_object, commitment_type, &blinded_balance_object::commitment> >
      >
   > blinded_balance_object_multi_index_type;
   typedef generic_index<blinded_balance_object, blinded_balance_object_multi_index_type> balance_index;


} } // graphene::chain

FC_REFLECT( graphene::chain::blinded_balance_object, (commitment)(asset_id)(last_update_block_num)(owner) )
