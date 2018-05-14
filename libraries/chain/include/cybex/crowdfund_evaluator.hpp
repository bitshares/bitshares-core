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
#include <graphene/chain/evaluator.hpp>
#include <graphene/chain/database.hpp>
#include <cybex/crowdfund_ops.hpp>

namespace graphene { namespace chain {

   class initiate_crowdfund_evaluator : public evaluator<initiate_crowdfund_evaluator>
   {
      public:
         typedef initiate_crowdfund_operation operation_type;

         void_result do_evaluate( const initiate_crowdfund_operation& o );
         object_id_type do_apply( const initiate_crowdfund_operation& o );
   };

   class participate_crowdfund_evaluator : public evaluator<participate_crowdfund_evaluator>
   {
      public:
         typedef participate_crowdfund_operation operation_type;
         void_result do_evaluate( const participate_crowdfund_operation& o );
         object_id_type do_apply( const participate_crowdfund_operation& o );

   };

   class withdraw_crowdfund_evaluator : public evaluator<withdraw_crowdfund_evaluator>
   {
      public:
         typedef withdraw_crowdfund_operation operation_type;

         void_result do_evaluate( const withdraw_crowdfund_operation& o );
         void_result do_apply( const withdraw_crowdfund_operation& o );
   };

} } // graphene::chain
