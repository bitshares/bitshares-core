#pragma once
#include <graphene/db/object.hpp>
//#include <graphene/chain/protocol/types.hpp>
//#include <graphene/db/object.hpp>
#include <graphene/protocol/types.hpp>
#include <boost/multi_index/composite_key.hpp>

namespace graphene { namespace chain {

   class trx_entry_object : public abstract_object<trx_entry_object>
   {
      public:
         static const uint8_t space_id = implementation_ids;
         static const uint8_t type_id  = impl_trx_entry_history_object_type;

         trx_entry_object(){}

         transaction_id_type    txid;
         uint32_t               block_num;
         uint32_t               trx_in_block;
   };

  // struct by_id;
   struct by_txid;
   struct by_blocknum;

   typedef multi_index_container<
      trx_entry_object,
      indexed_by<
         ordered_unique< tag<by_id>,           member< object, object_id_type, &object::id > >,
         ordered_unique< tag<by_txid>,         member< trx_entry_object, transaction_id_type, &trx_entry_object::txid > >,
         ordered_non_unique< tag<by_blocknum>, member< trx_entry_object, uint32_t, &trx_entry_object::block_num > >
      >
      
   > trx_entry_multi_index_type;

   typedef generic_index<trx_entry_object, trx_entry_multi_index_type> trx_entry_index;

} } // graphene::chain

FC_REFLECT_DERIVED( graphene::chain::trx_entry_object, (graphene::chain::object),
                    (txid)(block_num)(trx_in_block))

