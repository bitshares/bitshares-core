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

#include <graphene/chain/protocol/operations.hpp>
#include <graphene/chain/evaluator.hpp>
#include <graphene/chain/database.hpp>
#include <graphene/chain/transaction_evaluation_state.hpp>
#include <graphene/chain/asset_evaluator.hpp>
#include <graphene/chain/hardfork.hpp>

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

   namespace impl {

      class operation_hardfork_visitor {
      public:

         typedef void result_type;
         fc::time_point_sec block_time;
         operation_hardfork_visitor(fc::time_point_sec t) { block_time = t; }
         template<typename T>
         void operator()( const T& v )const {}

         // hf_620
         void operator()( const graphene::chain::asset_create_operation& v )const {
            if( block_time < HARDFORK_CORE_620_TIME ) {
               static const std::locale& loc = std::locale::classic();
               FC_ASSERT(isalpha(v.symbol.back(), loc), "Asset ${s} must end with alpha character before hardfork 620",
                         ("s", v.symbol));
            }
         }
         // hf_199
         void operator()( const graphene::chain::asset_update_issuer_operation& v )const {
            if ( block_time < HARDFORK_CORE_199_TIME) {
               FC_ASSERT(false, "Not allowed until hardfork 199");
            }
         }
         // hf_188
         void operator()( const graphene::chain::asset_claim_pool_operation& v )const {
            if ( block_time < HARDFORK_CORE_188_TIME) {
               FC_ASSERT(false, "Not allowed until hardfork 188");
            }
         }
         // hf_588
         // issue #588
         //
         // As a virtual operation which has no evaluator `asset_settle_cancel_operation`
         // originally won't be packed into blocks, yet its loose `validate()` method
         // make it able to slip into blocks.
         //
         // We need to forbid this operation being packed into blocks via proposal but
         // this will lead to a hardfork (this operation in proposal will denied by new
         // node while accept by old node), so a hardfork guard code needed and a
         // consensus upgrade over all nodes needed in future. And because the
         // `validate()` method not suitable to check database status, so we put the
         // code here.
         //
         // After the hardfork, all nodes will deny packing this operation into a block,
         // and then we will check whether exists a proposal containing this kind of
         // operation, if not exists, we can harden the `validate()` method to deny
         // it in a earlier stage.
         //
         void operator()( const graphene::chain::asset_settle_cancel_operation& v )const {
            if ( block_time > HARDFORK_CORE_588_TIME) {
               FC_ASSERT(!"Virtual operation");
            }
         }
         // loop and self visit in proposals
         void operator()( const graphene::chain::proposal_create_operation& v )const {
            for (const op_wrapper &op : v.proposed_ops)
               op.op.visit(*this);
         }
      };
   }

} } // graphene::chain
