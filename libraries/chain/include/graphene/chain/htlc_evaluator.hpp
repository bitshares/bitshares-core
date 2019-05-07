/*
 * Copyright (c) 2018 jmjatlanta and contributors.
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
#include <graphene/chain/htlc_object.hpp>

namespace graphene { 
   namespace chain {

      class htlc_create_evaluator : public evaluator<htlc_create_evaluator>
      {
         public:
    	      typedef htlc_create_operation operation_type;

    	      void_result do_evaluate( const htlc_create_operation& o);
    	      object_id_type do_apply( const htlc_create_operation& o);
      };

      class htlc_redeem_evaluator : public evaluator<htlc_redeem_evaluator>
      {
         public:
    	      typedef htlc_redeem_operation operation_type;

    	      void_result do_evaluate( const htlc_redeem_operation& o);
    	      void_result do_apply( const htlc_redeem_operation& o);
    	      const htlc_object* htlc_obj = nullptr;
      };

      class htlc_extend_evaluator : public evaluator<htlc_extend_evaluator>
      {
         public:
    	      typedef htlc_extend_operation operation_type;

    	      void_result do_evaluate( const htlc_extend_operation& o);
    	      void_result do_apply( const htlc_extend_operation& o);
    	      const htlc_object* htlc_obj = nullptr;
      };
   } // namespace graphene
} // namespace graphene
