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

#include <graphene/protocol/samet_fund.hpp>

namespace graphene { namespace chain {

   class samet_fund_object;

   class samet_fund_create_evaluator : public evaluator<samet_fund_create_evaluator>
   {
      public:
         using operation_type = samet_fund_create_operation;

         void_result do_evaluate( const samet_fund_create_operation& op ) const;
         object_id_type do_apply( const samet_fund_create_operation& op ) const;
   };

   class samet_fund_delete_evaluator : public evaluator<samet_fund_delete_evaluator>
   {
      public:
         using operation_type = samet_fund_delete_operation;

         void_result do_evaluate( const samet_fund_delete_operation& op );
         asset do_apply( const samet_fund_delete_operation& op ) const;

         const samet_fund_object* _fund = nullptr;
   };

   class samet_fund_update_evaluator : public evaluator<samet_fund_update_evaluator>
   {
      public:
         using operation_type = samet_fund_update_operation;

         void_result do_evaluate( const samet_fund_update_operation& op );
         void_result do_apply( const samet_fund_update_operation& op ) const;

         const samet_fund_object* _fund = nullptr;
   };

   class samet_fund_borrow_evaluator : public evaluator<samet_fund_borrow_evaluator>
   {
      public:
         using operation_type = samet_fund_borrow_operation;

         void_result do_evaluate( const samet_fund_borrow_operation& op );
         extendable_operation_result do_apply( const samet_fund_borrow_operation& op ) const;

         const samet_fund_object* _fund = nullptr;
   };

   class samet_fund_repay_evaluator : public evaluator<samet_fund_repay_evaluator>
   {
      public:
         using operation_type = samet_fund_repay_operation;

         void_result do_evaluate( const samet_fund_repay_operation& op );
         extendable_operation_result do_apply( const samet_fund_repay_operation& op ) const;

         const samet_fund_object* _fund = nullptr;
   };

} } // graphene::chain
