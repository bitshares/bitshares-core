/*
 * Copyright (c) 2020 Abit More, and contributors.
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

#include <graphene/protocol/ticket.hpp>

namespace graphene { namespace chain {

   class ticket_object;

   class ticket_create_evaluator : public evaluator<ticket_create_evaluator>
   {
      public:
         typedef ticket_create_operation operation_type;

         void_result do_evaluate( const ticket_create_operation& op );
         object_id_type do_apply( const ticket_create_operation& op );
   };

   class ticket_update_evaluator : public evaluator<ticket_update_evaluator>
   {
      public:
         typedef ticket_update_operation operation_type;

         void_result do_evaluate( const ticket_update_operation& op );
         generic_operation_result do_apply( const ticket_update_operation& op );

         const ticket_object* _ticket = nullptr;
   };

} } // graphene::chain
