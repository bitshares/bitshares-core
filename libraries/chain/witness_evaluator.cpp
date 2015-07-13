/*
 * Copyright (c) 2015, Cryptonomex, Inc.
 * All rights reserved.
 *
 * This source code is provided for evaluation in private test networks only, until September 8, 2015. After this date, this license expires and
 * the code may not be used, modified or distributed for any purpose. Redistribution and use in source and binary forms, with or without modification,
 * are permitted until September 8, 2015, provided that the following conditions are met:
 *
 * 1. The code and/or derivative works are used only for private test networks consisting of no more than 10 P2P nodes.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#include <graphene/chain/witness_evaluator.hpp>
#include <graphene/chain/witness_object.hpp>
#include <graphene/chain/committee_member_object.hpp>
#include <graphene/chain/account_object.hpp>
#include <graphene/chain/database.hpp>

namespace graphene { namespace chain {

void_result witness_create_evaluator::do_evaluate( const witness_create_operation& op )
{ try {
   FC_ASSERT(db().get(op.witness_account).is_lifetime_member());
   return void_result();
} FC_CAPTURE_AND_RETHROW( (op) ) }

object_id_type witness_create_evaluator::do_apply( const witness_create_operation& op )
{ try {
   vote_id_type vote_id;
   db().modify(db().get_global_properties(), [&vote_id](global_property_object& p) {
      vote_id = p.get_next_vote_id(vote_id_type::witness);
   });

   const auto& new_witness_object = db().create<witness_object>( [&]( witness_object& obj ){
         obj.witness_account  = op.witness_account;
         obj.signing_key      = op.block_signing_key;
         obj.next_secret_hash = op.initial_secret;
         obj.vote_id          = vote_id;
         obj.url              = op.url;
   });
   return new_witness_object.id;
} FC_CAPTURE_AND_RETHROW( (op) ) }

void_result witness_withdraw_pay_evaluator::do_evaluate(const witness_withdraw_pay_evaluator::operation_type& o)
{ try {
   database& d = db();

   witness = &d.get(o.from_witness);
   FC_ASSERT( o.to_account == witness->witness_account );
   FC_ASSERT( o.amount <= witness->accumulated_income, "Attempting to withdraw ${w}, but witness has only earned ${e}.",
              ("w", o.amount)("e", witness->accumulated_income) );
   to_account = &d.get(o.to_account);

   return void_result();
} FC_CAPTURE_AND_RETHROW( (o) ) }

void_result witness_withdraw_pay_evaluator::do_apply(const witness_withdraw_pay_evaluator::operation_type& o)
{ try {
   database& d = db();

   d.adjust_balance(o.to_account, asset(o.amount));

   d.modify(*witness, [&o](witness_object& w) {
      w.accumulated_income -= o.amount;
   });

   return void_result();
} FC_CAPTURE_AND_RETHROW( (o) ) }

} } // graphene::chain
