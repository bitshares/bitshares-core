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
#include <graphene/chain/transfer_evaluator.hpp>
#include <graphene/chain/account_object.hpp>

namespace graphene { namespace chain {
void_result transfer_evaluator::do_evaluate( const transfer_operation& op )
{ try {
   database& d = db();

   const account_object& from_account    = op.from(d);
   const account_object& to_account      = op.to(d);
   const asset_object&   asset_type      = op.amount.asset_id(d);
   const asset_object&   fee_asset_type  = op.fee.asset_id(d);

   if( asset_type.options.flags & white_list )
   {
      FC_ASSERT( to_account.is_authorized_asset( asset_type ) );
      FC_ASSERT( from_account.is_authorized_asset( asset_type ) );
   }

   if( fee_asset_type.options.flags & white_list )
      FC_ASSERT( from_account.is_authorized_asset( asset_type ) );

   if( asset_type.is_transfer_restricted() )
      FC_ASSERT( from_account.id == asset_type.issuer || to_account.id == asset_type.issuer );

   FC_ASSERT( d.get_balance( &from_account, &asset_type ).amount >= op.amount.amount,
              "", ("total_transfer",op.amount)("balance",d.get_balance(&from_account, &asset_type).amount) );

   return void_result();
} FC_CAPTURE_AND_RETHROW( (op) ) }

void_result transfer_evaluator::do_apply( const transfer_operation& o )
{ try {
   db().adjust_balance( o.from, -o.amount );
   db().adjust_balance( o.to, o.amount );
   return void_result();
} FC_CAPTURE_AND_RETHROW( (o) ) }



void_result override_transfer_evaluator::do_evaluate( const override_transfer_operation& op )
{ try {
   database& d = db();

   const asset_object&   asset_type      = op.amount.asset_id(d);
   FC_ASSERT( asset_type.can_override() );
   FC_ASSERT( asset_type.issuer == op.issuer );

   const account_object& from_account    = op.from(d);
   const account_object& to_account      = op.to(d);
   const asset_object&   fee_asset_type  = op.fee.asset_id(d);

   if( asset_type.options.flags & white_list )
   {
      FC_ASSERT( to_account.is_authorized_asset( asset_type ) );
      FC_ASSERT( from_account.is_authorized_asset( asset_type ) );
   }

   if( fee_asset_type.options.flags & white_list )
      FC_ASSERT( from_account.is_authorized_asset( asset_type ) );

   FC_ASSERT( d.get_balance( &from_account, &asset_type ).amount >= op.amount.amount,
              "", ("total_transfer",op.amount)("balance",d.get_balance(&from_account, &asset_type).amount) );

   return void_result();
} FC_CAPTURE_AND_RETHROW( (op) ) }

void_result override_transfer_evaluator::do_apply( const override_transfer_operation& o )
{ try {
   db().adjust_balance( o.from, -o.amount );
   db().adjust_balance( o.to, o.amount );
   return void_result();
} FC_CAPTURE_AND_RETHROW( (o) ) }

} } // graphene::chain
