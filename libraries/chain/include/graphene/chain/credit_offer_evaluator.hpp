/*
 * Copyright (c) 2021 Abit More, and contributors.
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
#include <graphene/chain/evaluator.hpp>

#include <graphene/protocol/credit_offer.hpp>

namespace graphene { namespace chain {

   class credit_offer_object;

   class credit_offer_create_evaluator : public evaluator<credit_offer_create_evaluator>
   {
      public:
         using operation_type = credit_offer_create_operation;

         void_result do_evaluate( const credit_offer_create_operation& op ) const;
         object_id_type do_apply( const credit_offer_create_operation& op ) const;
   };

   class credit_offer_delete_evaluator : public evaluator<credit_offer_delete_evaluator>
   {
      public:
         using operation_type = credit_offer_delete_operation;

         void_result do_evaluate( const credit_offer_delete_operation& op );
         asset do_apply( const credit_offer_delete_operation& op ) const;

         const credit_offer_object* _offer = nullptr;
   };

   class credit_offer_update_evaluator : public evaluator<credit_offer_update_evaluator>
   {
      public:
         using operation_type = credit_offer_update_operation;

         void_result do_evaluate( const credit_offer_update_operation& op );
         void_result do_apply( const credit_offer_update_operation& op ) const;

         const credit_offer_object* _offer = nullptr;
   };

   class credit_offer_accept_evaluator : public evaluator<credit_offer_accept_evaluator>
   {
      public:
         using operation_type = credit_offer_accept_operation;

         void_result do_evaluate( const credit_offer_accept_operation& op );
         extendable_operation_result do_apply( const credit_offer_accept_operation& op ) const;

         const credit_offer_object* _offer = nullptr;
         const credit_deal_summary_object* _deal_summary = nullptr;
   };

   class credit_deal_repay_evaluator : public evaluator<credit_deal_repay_evaluator>
   {
      public:
         using operation_type = credit_deal_repay_operation;

         void_result do_evaluate( const credit_deal_repay_operation& op );
         extendable_operation_result do_apply( const credit_deal_repay_operation& op ) const;

         const credit_deal_object* _deal = nullptr;
   };

} } // graphene::chain
