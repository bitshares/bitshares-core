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

namespace graphene { namespace chain {

   class asset_create_evaluator : public evaluator<asset_create_evaluator>
   {
      public:
         typedef asset_create_operation operation_type;

         void_result do_evaluate( const asset_create_operation& o );
         object_id_type do_apply( const asset_create_operation& o );
   };

   class asset_issue_evaluator : public evaluator<asset_issue_evaluator>
   {
      public:
         typedef asset_issue_operation operation_type;
         void_result do_evaluate( const asset_issue_operation& o );
         void_result do_apply( const asset_issue_operation& o );

         const asset_dynamic_data_object* asset_dyn_data = nullptr;
         const account_object*            to_account = nullptr;
   };

   class asset_reserve_evaluator : public evaluator<asset_reserve_evaluator>
   {
      public:
         typedef asset_reserve_operation operation_type;
         void_result do_evaluate( const asset_reserve_operation& o );
         void_result do_apply( const asset_reserve_operation& o );

         const asset_dynamic_data_object* asset_dyn_data = nullptr;
         const account_object*            from_account = nullptr;
   };


   class asset_update_evaluator : public evaluator<asset_update_evaluator>
   {
      public:
         typedef asset_update_operation operation_type;

         void_result do_evaluate( const asset_update_operation& o );
         void_result do_apply( const asset_update_operation& o );

         const asset_object* asset_to_update = nullptr;
   };

   class asset_update_bitasset_evaluator : public evaluator<asset_update_bitasset_evaluator>
   {
      public:
         typedef asset_update_bitasset_operation operation_type;

         void_result do_evaluate( const asset_update_bitasset_operation& o );
         void_result do_apply( const asset_update_bitasset_operation& o );

         const asset_bitasset_data_object* bitasset_to_update = nullptr;
   };

   class asset_update_feed_producers_evaluator : public evaluator<asset_update_feed_producers_evaluator>
   {
      public:
         typedef asset_update_feed_producers_operation operation_type;

         void_result do_evaluate( const operation_type& o );
         void_result do_apply( const operation_type& o );

         const asset_bitasset_data_object* bitasset_to_update = nullptr;
   };

   class asset_fund_fee_pool_evaluator : public evaluator<asset_fund_fee_pool_evaluator>
   {
      public:
         typedef asset_fund_fee_pool_operation operation_type;

         void_result do_evaluate(const asset_fund_fee_pool_operation& op);
         void_result do_apply(const asset_fund_fee_pool_operation& op);

         const asset_dynamic_data_object* asset_dyn_data = nullptr;
   };

   class asset_global_settle_evaluator : public evaluator<asset_global_settle_evaluator>
   {
      public:
         typedef asset_global_settle_operation operation_type;

         void_result do_evaluate(const operation_type& op);
         void_result do_apply(const operation_type& op);

         const asset_object* asset_to_settle = nullptr;
   };
   class asset_settle_evaluator : public evaluator<asset_settle_evaluator>
   {
      public:
         typedef asset_settle_operation operation_type;

         void_result do_evaluate(const operation_type& op);
         operation_result do_apply(const operation_type& op);

         const asset_object* asset_to_settle = nullptr;
   };

   class asset_publish_feeds_evaluator : public evaluator<asset_publish_feeds_evaluator>
   {
      public:
         typedef asset_publish_feed_operation operation_type;

         void_result do_evaluate( const asset_publish_feed_operation& o );
         void_result do_apply( const asset_publish_feed_operation& o );

         std::map<std::pair<asset_id_type,asset_id_type>,price_feed> median_feed_values;
   };

   class asset_claim_fees_evaluator : public evaluator<asset_claim_fees_evaluator>
   {
      public:
         typedef asset_claim_fees_operation operation_type;

         void_result do_evaluate( const asset_claim_fees_operation& o );
         void_result do_apply( const asset_claim_fees_operation& o );
   };

} } // graphene::chain
