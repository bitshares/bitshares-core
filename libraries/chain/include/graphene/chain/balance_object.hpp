#pragma once

namespace graphene { namespace chain {

   class balance_object : public abstract_object<balance_object>
   {
      public:
         static const uint8_t space_id = protocol_ids;
         static const uint8_t type_id  = balance_object_type;

         address owner;
         asset   balance;
         asset_id_type asset_type()const { return balance.asset_id; }
   };

   struct by_owner;

   /**
    * @ingroup object_index
    */
   typedef multi_index_container<
      balance_object,
      indexed_by<
         hashed_unique< tag<by_id>, member< object, object_id_type, &object::id > >,
         ordered_non_unique< tag<by_owner>, composite_key<
            balance_object,
            member<balance_object, address, &balance_object::owner>,
            const_mem_fun<balance_object, asset_id_type, &balance_object::asset_type> 
         > >
      >
   > balance_multi_index_type;

   /**
    * @ingroup object_index
    */
   typedef generic_index<balance_object, balance_multi_index_type> balance_index;
} }

FC_REFLECT_DERIVED( graphene::chain::balance_object, (graphene::db::object), (owner)(balance) )
