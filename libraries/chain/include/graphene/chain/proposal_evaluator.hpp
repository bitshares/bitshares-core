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
#include <graphene/chain/evaluator.hpp>
#include <graphene/chain/database.hpp>
#include <graphene/chain/transaction_evaluation_state.hpp>

namespace graphene { namespace chain {

   class proposal_create_evaluator : public evaluator<proposal_create_evaluator>
   {
      public:
         typedef proposal_create_operation operation_type;

         void_result do_evaluate( const proposal_create_operation& o );
         object_id_type do_apply( const proposal_create_operation& o );

         transaction _proposed_trx;
   };

   class proposal_update_evaluator : public evaluator<proposal_update_evaluator>
   {
      public:
         typedef proposal_update_operation operation_type;

         void_result do_evaluate( const proposal_update_operation& o );
         void_result do_apply( const proposal_update_operation& o );

         const proposal_object* _proposal = nullptr;
         processed_transaction _processed_transaction;
         bool _executed_proposal = false;
         bool _proposal_failed = false;
   };

   class proposal_delete_evaluator : public evaluator<proposal_delete_evaluator>
   {
      public:
         typedef proposal_delete_operation operation_type;

         void_result do_evaluate( const proposal_delete_operation& o );
         void_result do_apply(const proposal_delete_operation&);

         const proposal_object* _proposal = nullptr;
   };

} } // graphene::chain
