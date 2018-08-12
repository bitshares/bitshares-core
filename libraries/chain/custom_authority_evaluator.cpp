/*
 * Copyright (c) 2018 Abit More, and contributors.
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
#include <graphene/chain/custom_authority_evaluator.hpp>
#include <graphene/chain/custom_authority_object.hpp>
#include <graphene/chain/account_object.hpp>
#include <graphene/chain/database.hpp>
#include <graphene/chain/exceptions.hpp>
#include <graphene/chain/hardfork.hpp>

namespace graphene { namespace chain {

void_result custom_authority_create_evaluator::do_evaluate(const custom_authority_create_evaluator::operation_type& op)
{ try {
   const database& d = db();

   return void_result();
} FC_CAPTURE_AND_RETHROW( (op) ) }

object_id_type custom_authority_create_evaluator::do_apply(const custom_authority_create_evaluator::operation_type& op)
{ try {
   database& d = db();

   return {};
} FC_CAPTURE_AND_RETHROW( (op) ) }

void_result custom_authority_update_evaluator::do_evaluate(const custom_authority_update_evaluator::operation_type& op)
{ try {
   const database& d = db();

   return void_result();
} FC_CAPTURE_AND_RETHROW( (op) ) }

void_result custom_authority_update_evaluator::do_apply(const custom_authority_update_evaluator::operation_type& op)
{ try {
   database& d = db();

   return void_result();
} FC_CAPTURE_AND_RETHROW( (op) ) }

void_result custom_authority_delete_evaluator::do_evaluate(const custom_authority_delete_evaluator::operation_type& op)
{ try {
   const database& d = db();

   return void_result();
} FC_CAPTURE_AND_RETHROW( (op) ) }

void_result custom_authority_delete_evaluator::do_apply(const custom_authority_delete_evaluator::operation_type& op)
{ try {
   database& d = db();

   return void_result();
} FC_CAPTURE_AND_RETHROW( (op) ) }

} } // graphene::chain
