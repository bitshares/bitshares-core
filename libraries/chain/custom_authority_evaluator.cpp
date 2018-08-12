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

void_result custom_authority_create_evaluator::do_evaluate(const custom_authority_create_operation& op)
{ try {
   const database& d = db();

   // account is fee payer so should be valid

   // custom_id should be unique per account

   // valid_from ?
   // valid_to should not be too far in the future

   // operation_type HF check

   // if there is an account in auth, need to be valid

   // restrictions size check? global limitation?

   // how many custom authorities per account?

/*
      asset                           fee;
      account_id_type                 account;
      uint32_t                        custom_id;
      bool                            enabled;
      time_point_sec                  valid_from;
      time_point_sec                  valid_to;
      unsigned_int                    operation_type;
      authority                       auth;
      vector<restriction>             restrictions;

      empty_extensions_type           extensions;
*/

   return void_result();
} FC_CAPTURE_AND_RETHROW( (op) ) }

object_id_type custom_authority_create_evaluator::do_apply(const custom_authority_create_operation& op)
{ try {
   database& d = db();

   const auto& new_object = d.create<custom_authority_object>( [&op]( custom_authority_object& obj ){
      obj.account        = op.account;
      obj.custom_id      = op.custom_id;
      obj.enabled        = op.enabled;
      obj.valid_from     = op.valid_from;
      obj.valid_to       = op.valid_to;
      obj.operation_type = op.operation_type;
      obj.auth           = op.auth;
      obj.restrictions   = op.restrictions;
   });

   return new_object.id;

} FC_CAPTURE_AND_RETHROW( (op) ) }

void_result custom_authority_update_evaluator::do_evaluate(const custom_authority_update_operation& op)
{ try {
   const database& d = db();

   return void_result();
} FC_CAPTURE_AND_RETHROW( (op) ) }

void_result custom_authority_update_evaluator::do_apply(const custom_authority_update_operation& op)
{ try {
   database& d = db();

   return void_result();
} FC_CAPTURE_AND_RETHROW( (op) ) }

void_result custom_authority_delete_evaluator::do_evaluate(const custom_authority_delete_operation& op)
{ try {
   const database& d = db();

   return void_result();
} FC_CAPTURE_AND_RETHROW( (op) ) }

void_result custom_authority_delete_evaluator::do_apply(const custom_authority_delete_operation& op)
{ try {
   database& d = db();

   return void_result();
} FC_CAPTURE_AND_RETHROW( (op) ) }

} } // graphene::chain
