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
#include <graphene/chain/protocol/operations.hpp>
#include <graphene/db/object.hpp>
#include <boost/multi_index/composite_key.hpp>

namespace graphene { namespace chain {

   /**
    * @brief tracks the history of all logical operations on blockchain state
    * @ingroup object
    * @ingroup implementation
    *
    *  All operations and virtual operations result in the creation of an
    *  operation_history_object that is maintained on disk as a stack.  Each
    *  real or virtual operation is assigned a unique ID / sequence number that
    *  it can be referenced by.
    *
    *  @note  by default these objects are not tracked, the account_history_plugin must
    *  be loaded fore these objects to be maintained.
    *
    *  @note  this object is READ ONLY it can never be modified
    */
   class operation_history_object : public abstract_object<operation_history_object>
   {
      public:
         static const uint8_t space_id = protocol_ids;
         static const uint8_t type_id  = operation_history_object_type;

         operation_history_object( const operation& o ):op(o){}
         operation_history_object(){}

         operation         op;
         operation_result  result;
         /** the block that caused this operation */
         uint32_t          block_num = 0;
         /** the transaction in the block */
         uint16_t          trx_in_block = 0;
         /** the operation within the transaction */
         uint16_t          op_in_trx = 0;
         /** any virtual operations implied by operation in block */
         uint16_t          virtual_op = 0;
   };

   /**
    *  @brief a node in a linked list of operation_history_objects
    *  @ingroup implementation
    *  @ingroup object
    *
    *  Account history is important for users and wallets even though it is
    *  not part of "core validation".   Account history is maintained as
    *  a linked list stored on disk in a stack.  Each account will point to the
    *  most recent account history object by ID.  When a new operation relativent
    *  to that account is processed a new account history object is allcoated at
    *  the end of the stack and intialized to point to the prior object.
    *
    *  This data is never accessed as part of chain validation and therefore
    *  can be kept on disk as a memory mapped file.  Using a memory mapped file
    *  will help the operating system better manage / cache / page files and
    *  also accelerates load time.
    *
    *  When the transaction history for a particular account is requested the
    *  linked list can be traversed with relatively effecient disk access because
    *  of the use of a memory mapped stack.
    */
   class account_transaction_history_object :  public abstract_object<account_transaction_history_object>
   {
      public:
         static const uint8_t space_id = implementation_ids;
         static const uint8_t type_id  = impl_account_transaction_history_object_type;
         account_id_type                      account; /// the account this operation applies to
         operation_history_id_type            operation_id;
         uint32_t                             sequence = 0; /// the operation position within the given account
         account_transaction_history_id_type  next;

         //std::pair<account_id_type,operation_history_id_type>  account_op()const  { return std::tie( account, operation_id ); }
         //std::pair<account_id_type,uint32_t>                   account_seq()const { return std::tie( account, sequence );     }
   };
   
   struct by_id;
struct by_seq;
struct by_op;
typedef multi_index_container<
   account_transaction_history_object,
   indexed_by<
      ordered_unique< tag<by_id>, member< object, object_id_type, &object::id > >,
      ordered_unique< tag<by_seq>,
         composite_key< account_transaction_history_object,
            member< account_transaction_history_object, account_id_type, &account_transaction_history_object::account>,
            member< account_transaction_history_object, uint32_t, &account_transaction_history_object::sequence>
         >
      >,
      ordered_unique< tag<by_op>,
         composite_key< account_transaction_history_object,
            member< account_transaction_history_object, account_id_type, &account_transaction_history_object::account>,
            member< account_transaction_history_object, operation_history_id_type, &account_transaction_history_object::operation_id>
         >
      >
   >
> account_transaction_history_multi_index_type;

typedef generic_index<account_transaction_history_object, account_transaction_history_multi_index_type> account_transaction_history_index;

   
} } // graphene::chain

FC_REFLECT_DERIVED( graphene::chain::operation_history_object, (graphene::chain::object),
                    (op)(result)(block_num)(trx_in_block)(op_in_trx)(virtual_op) )

FC_REFLECT_DERIVED( graphene::chain::account_transaction_history_object, (graphene::chain::object),
                    (account)(operation_id)(sequence)(next) )
