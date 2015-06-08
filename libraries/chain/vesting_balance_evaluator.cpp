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

#include <graphene/chain/account_object.hpp>
#include <graphene/chain/database.hpp>
#include <graphene/chain/vesting_balance_evaluator.hpp>
#include <graphene/chain/vesting_balance_object.hpp>

namespace graphene { namespace chain {

object_id_type vesting_balance_create_evaluator::do_evaluate( const vesting_balance_create_operation& op )
{
   const database& d = db();

   const account_object& creator_account = op.creator( d );
   /* const account_object& owner_account = */ op.owner( d );

   // TODO: Check asset authorizations and withdrawals

   FC_ASSERT( op.amount.amount > 0 );
   FC_ASSERT( d.get_balance( creator_account.id, op.amount.asset_id ) >= op.amount );
   FC_ASSERT( !op.amount.asset_id(d).is_transfer_restricted() );

   /** we cannot create vesting balances that are market issued due to black swans */
   FC_ASSERT( !op.amount.asset_id(d).is_market_issued() );

   return object_id_type();
}

object_id_type vesting_balance_create_evaluator::do_apply( const vesting_balance_create_operation& op )
{
   database& d = db();
   const time_point_sec now = d.head_block_time();

   d.adjust_balance( op.creator, -op.amount );

   const vesting_balance_object& vbo = d.create< vesting_balance_object >( [&]( vesting_balance_object& obj )
   {
      //WARNING: The logic to create a vesting balance object is replicated in vesting_balance_worker_type::initializer::init.
      // If making changes to this logic, check if those changes should also be made there as well.
      obj.owner = op.owner;
      obj.balance = op.amount;

      cdd_vesting_policy policy;
      policy.vesting_seconds = op.vesting_seconds;
      policy.coin_seconds_earned = 0;
      policy.coin_seconds_earned_last_update = now;

      obj.policy = policy;
   } );

   FC_ASSERT( d.get_balance( op.creator, op.amount.asset_id ) >= op.amount );

   return vbo.id;
}

object_id_type vesting_balance_withdraw_evaluator::do_evaluate( const vesting_balance_withdraw_operation& op )
{
   const database& d = db();
   const time_point_sec now = d.head_block_time();

   const vesting_balance_object& vbo = op.vesting_balance( d );
   FC_ASSERT( op.owner == vbo.owner );
   FC_ASSERT( vbo.is_withdraw_allowed( now, op.amount ) );
   assert( op.amount <= vbo.balance );      // is_withdraw_allowed should fail before this check is reached

   /* const account_object& owner_account = */ op.owner( d );
   // TODO: Check asset authorizations and withdrawals
   return object_id_type();
}

object_id_type vesting_balance_withdraw_evaluator::do_apply( const vesting_balance_withdraw_operation& op )
{
   database& d = db();
   const time_point_sec now = d.head_block_time();

   const vesting_balance_object& vbo = op.vesting_balance( d );

   // Allow zero balance objects to stick around, (1) to comply
   // with the chain's "objects live forever" design principle, (2)
   // if it's cashback or worker, it'll be filled up again.

   d.modify( vbo, [&]( vesting_balance_object& vbo )
   {
      vbo.withdraw( now, op.amount );
   } );

   d.adjust_balance( op.owner, op.amount );

   // TODO: Check asset authorizations and withdrawals
   return object_id_type();
}

} } // graphene::chain
