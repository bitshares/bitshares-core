/*
 * Copyright (c) 2019 GXChain and zhaoxiangfei、bijianing97 .
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
#include <graphene/db/object.hpp>
#include <graphene/protocol/types.hpp>
#include <boost/multi_index/composite_key.hpp>

namespace graphene { namespace chain {

   struct query_trx_info : public graphene::protocol::processed_transaction
   {
      query_trx_info( const signed_transaction& trx = signed_transaction())
         : processed_transaction(trx){}

      virtual ~query_trx_info() = default;

      uint64_t query_txid_block_number = 0;
      uint64_t query_txid_trx_in_block = 0;
   };
   #ifndef QUERY_TXID_SPACE_ID
   #define QUERY_TXID_SPACE_ID 8
   #endif
   enum query_txid_object_type
   {
      transaction_position_object_type = 0
   };
   class trx_entry_object : public abstract_object<trx_entry_object>
   {
      public:
         static const uint8_t space_id = QUERY_TXID_SPACE_ID;
         static const uint8_t type_id  = transaction_position_object_type;
         trx_entry_object(){}

         transaction_id_type    txid;
         uint32_t               block_num;
         uint32_t               trx_in_block;
   };

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
FC_REFLECT_DERIVED( graphene::chain::query_trx_info, (graphene::protocol::processed_transaction),
                    (query_txid_block_number)(query_txid_trx_in_block))
