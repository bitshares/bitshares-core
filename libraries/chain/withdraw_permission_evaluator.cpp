/*
 * Copyright (c) 2015 Cryptonomex, Inc., and contributors.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:
 *
 * 1. Any modified source or binaries are used only with the BitShares network.
 *
 * 2. Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
 *
 * 3. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */
#include <graphene/chain/withdraw_permission_evaluator.hpp>
#include <graphene/chain/withdraw_permission_object.hpp>
#include <graphene/chain/account_object.hpp>
#include <graphene/chain/database.hpp>
#include <graphene/chain/exceptions.hpp>
#include <graphene/chain/hardfork.hpp>

namespace graphene { namespace chain {

void_result withdraw_permission_create_evaluator::do_evaluate(const operation_type& op)
{ try {
   database& d = db();
   FC_ASSERT(d.find_object(op.withdraw_from_account));
   FC_ASSERT(d.find_object(op.authorized_account));
   FC_ASSERT(d.find_object(op.withdrawal_limit.asset_id));
   FC_ASSERT(op.period_start_time > d.head_block_time());
   FC_ASSERT(op.period_start_time + op.periods_until_expiration * op.withdrawal_period_sec > d.head_block_time());
   FC_ASSERT(op.withdrawal_period_sec >= d.get_global_properties().parameters.block_interval);

   return void_result();
} FC_CAPTURE_AND_RETHROW( (op) ) }

object_id_type withdraw_permission_create_evaluator::do_apply(const operation_type& op)
{ try {
   return db().create<withdraw_permission_object>([&op](withdraw_permission_object& p) {
      p.withdraw_from_account = op.withdraw_from_account;
      p.authorized_account = op.authorized_account;
      p.withdrawal_limit = op.withdrawal_limit;
      p.withdrawal_period_sec = op.withdrawal_period_sec;
      p.expiration = op.period_start_time + op.periods_until_expiration * op.withdrawal_period_sec;
      p.period_start_time = op.period_start_time;
   }).id;
} FC_CAPTURE_AND_RETHROW( (op) ) }

void_result withdraw_permission_claim_evaluator::do_evaluate(const withdraw_permission_claim_evaluator::operation_type& op)
{ try {
   const database& d = db();

   const withdraw_permission_object& permit = op.withdraw_permission(d);
   FC_ASSERT(permit.expiration > d.head_block_time() );
   FC_ASSERT(permit.authorized_account == op.withdraw_to_account);
   FC_ASSERT(permit.withdraw_from_account == op.withdraw_from_account);
   FC_ASSERT(op.amount_to_withdraw <= permit.available_this_period( d.head_block_time() ) );
   FC_ASSERT(d.get_balance(op.withdraw_from_account, op.amount_to_withdraw.asset_id) >= op.amount_to_withdraw);

   const asset_object& _asset = op.amount_to_withdraw.asset_id(d);
   if( _asset.is_transfer_restricted() ) FC_ASSERT( _asset.issuer == permit.authorized_account || _asset.issuer == permit.withdraw_from_account );

   if( d.head_block_time() <= HARDFORK_416_TIME )
   {
      if( _asset.options.flags & white_list )
      {
         const account_object& from  = op.withdraw_to_account(d);
         const account_object& to    = permit.authorized_account(d);
         FC_ASSERT( to.is_authorized_asset( _asset, d ) );
         FC_ASSERT( from.is_authorized_asset( _asset, d ) );
      }
   }
   else
   {
      const account_object& from  = op.withdraw_to_account(d);
      const account_object& to    = permit.authorized_account(d);
      FC_ASSERT( to.is_authorized_asset( _asset, d ) );
      FC_ASSERT( from.is_authorized_asset( _asset, d ) );
   }

   return void_result();
} FC_CAPTURE_AND_RETHROW( (op) ) }

void_result withdraw_permission_claim_evaluator::do_apply(const withdraw_permission_claim_evaluator::operation_type& op)
{ try {
   database& d = db();

   const withdraw_permission_object& permit = d.get(op.withdraw_permission);
   d.modify(permit, [&](withdraw_permission_object& p) {
      auto periods = (d.head_block_time() - p.period_start_time).to_seconds() / p.withdrawal_period_sec;
      p.period_start_time += periods * p.withdrawal_period_sec;
      if( periods == 0 )
         p.claimed_this_period += op.amount_to_withdraw.amount;
      else
         p.claimed_this_period = op.amount_to_withdraw.amount;
   });

   d.adjust_balance(op.withdraw_from_account, -op.amount_to_withdraw);
   d.adjust_balance(op.withdraw_to_account, op.amount_to_withdraw);

   return void_result();
} FC_CAPTURE_AND_RETHROW( (op) ) }

void_result withdraw_permission_update_evaluator::do_evaluate(const withdraw_permission_update_evaluator::operation_type& op)
{ try {
   database& d = db();

   const withdraw_permission_object& permit = op.permission_to_update(d);
   FC_ASSERT(permit.authorized_account == op.authorized_account);
   FC_ASSERT(permit.withdraw_from_account == op.withdraw_from_account);
   FC_ASSERT(d.find_object(op.withdrawal_limit.asset_id));
   FC_ASSERT(op.period_start_time >= d.head_block_time());
   FC_ASSERT(op.period_start_time + op.periods_until_expiration * op.withdrawal_period_sec > d.head_block_time());
   FC_ASSERT(op.withdrawal_period_sec >= d.get_global_properties().parameters.block_interval);

   return void_result();
} FC_CAPTURE_AND_RETHROW( (op) ) }

void_result withdraw_permission_update_evaluator::do_apply(const withdraw_permission_update_evaluator::operation_type& op)
{ try {
   database& d = db();

   d.modify(op.permission_to_update(d), [&op](withdraw_permission_object& p) {
      p.period_start_time = op.period_start_time;
      p.expiration = op.period_start_time + op.periods_until_expiration * op.withdrawal_period_sec;
      p.withdrawal_limit = op.withdrawal_limit;
      p.withdrawal_period_sec = op.withdrawal_period_sec;
   });

   return void_result();
} FC_CAPTURE_AND_RETHROW( (op) ) }

void_result withdraw_permission_delete_evaluator::do_evaluate(const withdraw_permission_delete_evaluator::operation_type& op)
{ try {
   database& d = db();

   const withdraw_permission_object& permit = op.withdrawal_permission(d);
   FC_ASSERT(permit.authorized_account == op.authorized_account);
   FC_ASSERT(permit.withdraw_from_account == op.withdraw_from_account);

   return void_result();
} FC_CAPTURE_AND_RETHROW( (op) ) }

void_result withdraw_permission_delete_evaluator::do_apply(const withdraw_permission_delete_evaluator::operation_type& op)
{ try {
   db().remove(db().get(op.withdrawal_permission));
   return void_result();
} FC_CAPTURE_AND_RETHROW( (op) ) }

} } // graphene::chain
