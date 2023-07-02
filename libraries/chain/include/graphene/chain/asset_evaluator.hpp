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
#include <graphene/protocol/operations.hpp>
#include <graphene/chain/evaluator.hpp>
#include <graphene/chain/database.hpp>

#include <graphene/chain/hardfork.hpp>
#include <locale>

namespace graphene { namespace chain {

   class asset_create_evaluator : public evaluator<asset_create_evaluator>
   {
      public:
         using operation_type = asset_create_operation;

         void_result do_evaluate( const asset_create_operation& o ) const;
         object_id_type do_apply( const asset_create_operation& o ) const;

         /** override the default behavior defined by generic_evalautor which is to
          * post the fee to fee_paying_account_stats.pending_fees
          */
         void pay_fee() override;
      private:
         bool fee_is_odd;
   };

   class asset_issue_evaluator : public evaluator<asset_issue_evaluator>
   {
      public:
         using operation_type = asset_issue_operation;
         void_result do_evaluate( const asset_issue_operation& o );
         void_result do_apply( const asset_issue_operation& o ) const;

      private:
         const asset_dynamic_data_object* asset_dyn_data = nullptr;
         const account_object*            to_account = nullptr;
   };

   class asset_reserve_evaluator : public evaluator<asset_reserve_evaluator>
   {
      public:
         using operation_type = asset_reserve_operation;
         void_result do_evaluate( const asset_reserve_operation& o );
         void_result do_apply( const asset_reserve_operation& o ) const;

      private:
         const asset_dynamic_data_object* asset_dyn_data = nullptr;
         const account_object*            from_account = nullptr;
   };


   class asset_update_evaluator : public evaluator<asset_update_evaluator>
   {
      public:
         using operation_type = asset_update_operation;

         void_result do_evaluate( const asset_update_operation& o );
         void_result do_apply( const asset_update_operation& o );

      private:
         const asset_object* asset_to_update = nullptr;
         const asset_bitasset_data_object* bitasset_data = nullptr;
   };

   class asset_update_issuer_evaluator : public evaluator<asset_update_issuer_evaluator>
   {
      public:
         using operation_type = asset_update_issuer_operation;

         void_result do_evaluate( const asset_update_issuer_operation& o );
         void_result do_apply( const asset_update_issuer_operation& o );

      private:
         const asset_object* asset_to_update = nullptr;
   };

   class asset_update_bitasset_evaluator : public evaluator<asset_update_bitasset_evaluator>
   {
      public:
         using operation_type = asset_update_bitasset_operation;

         void_result do_evaluate( const asset_update_bitasset_operation& o );
         void_result do_apply( const asset_update_bitasset_operation& o );

      private:
         const asset_bitasset_data_object* bitasset_to_update = nullptr;
         const asset_object* asset_to_update = nullptr;

         bool update_feeds_due_to_bsrm_change = false;
   };

   class asset_update_feed_producers_evaluator : public evaluator<asset_update_feed_producers_evaluator>
   {
      public:
         using operation_type = asset_update_feed_producers_operation;

         void_result do_evaluate( const operation_type& o );
         void_result do_apply( const operation_type& o ) const;

      private:
         const asset_object* asset_to_update = nullptr;
   };

   class asset_fund_fee_pool_evaluator : public evaluator<asset_fund_fee_pool_evaluator>
   {
      public:
         using operation_type = asset_fund_fee_pool_operation;

         void_result do_evaluate(const asset_fund_fee_pool_operation& op);
         void_result do_apply(const asset_fund_fee_pool_operation& op) const;

      private:
         const asset_dynamic_data_object* asset_dyn_data = nullptr;
   };

   class asset_global_settle_evaluator : public evaluator<asset_global_settle_evaluator>
   {
      public:
         using operation_type = asset_global_settle_operation;

         void_result do_evaluate(const operation_type& op);
         void_result do_apply(const operation_type& op);

      private:
         const asset_object* asset_to_settle = nullptr;
   };
   class asset_settle_evaluator : public evaluator<asset_settle_evaluator>
   {
      public:
         using operation_type = asset_settle_operation;

         void_result do_evaluate(const operation_type& op);
         operation_result do_apply(const operation_type& op);

      private:
         const asset_object* asset_to_settle = nullptr;
         const asset_bitasset_data_object* bitasset_ptr = nullptr;
   };

   class asset_publish_feeds_evaluator : public evaluator<asset_publish_feeds_evaluator>
   {
      public:
         using operation_type = asset_publish_feed_operation;

         void_result do_evaluate( const asset_publish_feed_operation& o );
         void_result do_apply( const asset_publish_feed_operation& o );

      private:
         const asset_object* asset_ptr = nullptr;
         const asset_bitasset_data_object* bitasset_ptr = nullptr;
   };

   class asset_claim_fees_evaluator : public evaluator<asset_claim_fees_evaluator>
   {
      public:
         using operation_type = asset_claim_fees_operation;

         void_result do_evaluate( const asset_claim_fees_operation& o );
         void_result do_apply( const asset_claim_fees_operation& o );

      private:
         const asset_object* container_asset = nullptr;
         const asset_dynamic_data_object* container_ddo = nullptr;
   };

   class asset_claim_pool_evaluator : public evaluator<asset_claim_pool_evaluator>
   {
      public:
         using operation_type = asset_claim_pool_operation;

         void_result do_evaluate( const asset_claim_pool_operation& o );
         void_result do_apply( const asset_claim_pool_operation& o );
   };

} } // graphene::chain
