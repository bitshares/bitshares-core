/*
 * Copyright (c) 2018 oxarbitrage and contributors.
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

namespace graphene { namespace chain {

      class escrow_transfer_evaluator : public evaluator<escrow_transfer_evaluator>
      {
      public:
         typedef escrow_transfer_operation operation_type;

         void_result do_evaluate( const escrow_transfer_operation& o );
         object_id_type do_apply( const escrow_transfer_operation& o );
      };

      class escrow_dispute_evaluator : public evaluator<escrow_dispute_evaluator>
      {
      public:
         typedef escrow_dispute_operation operation_type;

         void_result do_evaluate( const escrow_dispute_operation& o );
         object_id_type do_apply( const escrow_dispute_operation& o );
      };

      class escrow_release_evaluator : public evaluator<escrow_release_evaluator>
      {
      public:
         typedef escrow_release_operation operation_type;

         void_result do_evaluate( const escrow_release_operation& o );
         object_id_type do_apply( const escrow_release_operation& o );
      };


   } } // graphene::chain
