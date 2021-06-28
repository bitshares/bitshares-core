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
#include <graphene/chain/asset_object.hpp>
#include <graphene/chain/samet_fund_object.hpp>

#include <graphene/chain/samet_fund_evaluator.hpp>

#include <graphene/chain/database.hpp>
#include <graphene/chain/exceptions.hpp>
#include <graphene/chain/hardfork.hpp>
#include <graphene/chain/is_authorized_asset.hpp>

#include <graphene/protocol/samet_fund.hpp>

namespace graphene { namespace chain {

void_result samet_fund_create_evaluator::do_evaluate(const samet_fund_create_operation& op) const
{ try {
   const database& d = db();
   const auto block_time = d.head_block_time();

   FC_ASSERT( HARDFORK_CORE_2351_PASSED(block_time), "Not allowed until the core-2351 hardfork" );

   FC_ASSERT( is_authorized_asset( d, *fee_paying_account, op.asset_type(d) ),
              "The account is unauthorized by the asset" );

   return void_result();
} FC_CAPTURE_AND_RETHROW( (op) ) }

object_id_type samet_fund_create_evaluator::do_apply(const samet_fund_create_operation& op) const
{ try {
   database& d = db();

   d.adjust_balance( op.owner_account, -asset( op.balance, op.asset_type ) );

   const auto& new_samet_fund_object = d.create<samet_fund_object>([&op](samet_fund_object& obj){
      obj.owner_account = op.owner_account;
      obj.asset_type = op.asset_type;
      obj.balance = op.balance;
      obj.fee_rate = op.fee_rate;
      // unpaid amount is 0 by default
   });
   return new_samet_fund_object.id;
} FC_CAPTURE_AND_RETHROW( (op) ) }

void_result samet_fund_delete_evaluator::do_evaluate(const samet_fund_delete_operation& op)
{ try {
   const database& d = db();

   _fund = &op.fund_id(d);

   FC_ASSERT( _fund->owner_account == op.owner_account, "The account is not the owner of the SameT Fund" );

   FC_ASSERT( _fund->unpaid_amount == 0, "Can only delete a SameT Fund when the unpaid amount is zero" );

   // Note: no asset authorization check here, allow funds to be moved to account balance

   return void_result();
} FC_CAPTURE_AND_RETHROW( (op) ) }

asset samet_fund_delete_evaluator::do_apply(const samet_fund_delete_operation& op) const
{ try {
   database& d = db();

   asset released( _fund->balance, _fund->asset_type );

   d.adjust_balance( op.owner_account, released );

   d.remove( *_fund );

   return released;
} FC_CAPTURE_AND_RETHROW( (op) ) }

void_result samet_fund_update_evaluator::do_evaluate(const samet_fund_update_operation& op)
{ try {
   const database& d = db();

   _fund = &op.fund_id(d);

   FC_ASSERT( _fund->owner_account == op.owner_account, "The account is not the owner of the SameT Fund" );

   if( op.delta_amount.valid() )
   {
      FC_ASSERT( _fund->asset_type == op.delta_amount->asset_id, "Asset type mismatch" );
      FC_ASSERT( _fund->unpaid_amount == 0,
                 "Can only update the balance of a SameT Fund when the unpaid amount is zero" );

      if( op.delta_amount->amount > 0 )
      {
         // Check asset authorization only when moving funds from account balance to somewhere else
         FC_ASSERT( is_authorized_asset( d, *fee_paying_account, _fund->asset_type(d) ),
                    "The account is unauthorized by the asset" );
      }
      else
      {
         FC_ASSERT( _fund->balance > -op.delta_amount->amount, "Insufficient balance in the SameT Fund" );
      }
   }

   if( op.new_fee_rate.valid() )
   {
      FC_ASSERT( _fund->fee_rate != *op.new_fee_rate,
                 "New fee rate should not be the same as the original fee rate" );
   }

   return void_result();
} FC_CAPTURE_AND_RETHROW( (op) ) }

void_result samet_fund_update_evaluator::do_apply( const samet_fund_update_operation& op) const
{ try {
   database& d = db();

   if( op.delta_amount.valid() )
      d.adjust_balance( op.owner_account, -(*op.delta_amount) );

   d.modify( *_fund, [&op]( samet_fund_object& sfo ){
      if( op.delta_amount.valid() )
         sfo.balance += op.delta_amount->amount;
      if( op.new_fee_rate.valid() )
         sfo.fee_rate = *op.new_fee_rate;
   });

   // Defensive check
   FC_ASSERT( _fund->balance > 0, "Balance in the SameT Fund should be positive" );

   return void_result();
} FC_CAPTURE_AND_RETHROW( (op) ) }

void_result samet_fund_borrow_evaluator::do_evaluate(const samet_fund_borrow_operation& op)
{ try {
   const database& d = db();

   _fund = &op.fund_id(d);

   FC_ASSERT( _fund->asset_type == op.borrow_amount.asset_id, "Asset type mismatch" );

   FC_ASSERT( _fund->balance >= _fund->unpaid_amount + op.borrow_amount.amount,
              "Insufficient balance in the SameT Fund thus unable to borrow" );

   FC_ASSERT( is_authorized_asset( d, *fee_paying_account, _fund->asset_type(d) ),
              "The account is unauthorized by the asset" );

   return void_result();
} FC_CAPTURE_AND_RETHROW( (op) ) }

extendable_operation_result samet_fund_borrow_evaluator::do_apply( const samet_fund_borrow_operation& op) const
{ try {
   database& d = db();

   d.modify( *_fund, [&op]( samet_fund_object& sfo ){
      sfo.unpaid_amount += op.borrow_amount.amount;
   });

   d.adjust_balance( op.borrower, op.borrow_amount );

   // Defensive check
   FC_ASSERT( _fund->balance >= _fund->unpaid_amount, "Should not borrow more than available" );

   extendable_operation_result result;
   result.value.impacted_accounts = flat_set<account_id_type>({ _fund->owner_account });

   return result;
} FC_CAPTURE_AND_RETHROW( (op) ) }

void_result samet_fund_repay_evaluator::do_evaluate(const samet_fund_repay_operation& op)
{ try {
   const database& d = db();

   _fund = &op.fund_id(d);

   FC_ASSERT( _fund->asset_type == op.repay_amount.asset_id, "Asset type mismatch" );

   FC_ASSERT( is_authorized_asset( d, *fee_paying_account, _fund->asset_type(d) ),
              "The account is unauthorized by the asset" );

   FC_ASSERT( op.repay_amount.amount <= _fund->unpaid_amount,
              "Repay amount should not be greater than unpaid amount" );

   // Note: the result can be larger than 64 bit, but since we don't store it, it is allowed
   auto required_fee = ( ( ( fc::uint128_t( op.repay_amount.amount.value ) * _fund->fee_rate )
                         + GRAPHENE_FEE_RATE_DENOM ) - 1 ) / GRAPHENE_FEE_RATE_DENOM; // Round up

   FC_ASSERT( fc::uint128_t(op.fund_fee.amount.value) >= required_fee,
              "Insuffient fund fee, requires ${r}, offered ${p}",
              ("r", required_fee) ("p", op.fund_fee.amount) );

   return void_result();
} FC_CAPTURE_AND_RETHROW( (op) ) }

extendable_operation_result samet_fund_repay_evaluator::do_apply( const samet_fund_repay_operation& op) const
{ try {
   database& d = db();

   d.adjust_balance( op.account, -( op.repay_amount + op.fund_fee ) );

   d.modify( *_fund, [op]( samet_fund_object& sfo ){
      sfo.balance += op.fund_fee.amount;
      sfo.unpaid_amount -= op.repay_amount.amount;
   });

   extendable_operation_result result;
   result.value.impacted_accounts = flat_set<account_id_type>({ _fund->owner_account });

   return result;
} FC_CAPTURE_AND_RETHROW( (op) ) }

} } // graphene::chain
