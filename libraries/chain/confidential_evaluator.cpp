/*
 * Copyright (c) 2015 Cryptonomex, Inc., and contributors.
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
#include <graphene/chain/exceptions.hpp>
#include <graphene/protocol/confidential.hpp>
#include <graphene/chain/confidential_evaluator.hpp>
#include <graphene/chain/confidential_object.hpp>
#include <graphene/chain/database.hpp>
#include <graphene/chain/fba_accumulator_id.hpp>
#include <graphene/chain/hardfork.hpp>

namespace graphene { namespace chain {

void_result transfer_to_blind_evaluator::do_evaluate( const transfer_to_blind_operation& o )
{ try {
   const auto& d = db();

   const auto& atype = o.amount.asset_id(db()); 
   FC_ASSERT( atype.allow_confidential() );
   FC_ASSERT( !atype.is_transfer_restricted() );
   FC_ASSERT( !(atype.options.flags & white_list) );

   for( const auto& out : o.outputs )
   {
      for( const auto& a : out.owner.account_auths )
         a.first(d); // verify all accounts exist and are valid
   }
   return void_result();
} FC_CAPTURE_AND_RETHROW( (o) ) }


void_result transfer_to_blind_evaluator::do_apply( const transfer_to_blind_operation& o ) 
{ try {
   stored_value transport = db().reduce_balance( o.from, o.amount );

   const auto& add = o.amount.asset_id(db()).dynamic_asset_data_id(db());  // verify fee is a legit asset 
   db().modify( add, [&transport]( asset_dynamic_data_object& obj ){
      obj.confidential_supply += std::move(transport);
      FC_ASSERT( obj.confidential_supply.get_amount() >= 0 );
   });
   for( const auto& out : o.outputs )
   {
      db().create<blinded_balance_object>( [&]( blinded_balance_object& obj ){
          obj.asset_id   = o.amount.asset_id;
          obj.owner      = out.owner;
          obj.commitment = out.commitment;
      });
   }
   return void_result();
} FC_CAPTURE_AND_RETHROW( (o) ) }

void transfer_to_blind_evaluator::pay_fee()
{
   if( db().head_block_time() >= HARDFORK_563_TIME )
      pay_fba_fee( fba_accumulator_id_transfer_to_blind );
   else
      generic_evaluator::pay_fee();
}

void_result transfer_from_blind_evaluator::do_evaluate( const transfer_from_blind_operation& o )
{ try {
   const auto& d = db();
   o.fee.asset_id(d);  // verify fee is a legit asset 
   const auto& bbi = d.get_index_type<blinded_balance_index>();
   const auto& cidx = bbi.indices().get<by_commitment>();
   for( const auto& in : o.inputs )
   {
      auto itr = cidx.find( in.commitment );
      FC_ASSERT( itr != cidx.end() );
      FC_ASSERT( itr->asset_id == o.fee.asset_id );
      FC_ASSERT( itr->owner == in.owner );
   }
   return void_result();
} FC_CAPTURE_AND_RETHROW( (o) ) }

void_result transfer_from_blind_evaluator::do_apply( const transfer_from_blind_operation& o ) 
{ try {
   const auto& bbi = db().get_index_type<blinded_balance_index>();
   const auto& cidx = bbi.indices().get<by_commitment>();
   for( const auto& in : o.inputs )
   {
      auto itr = cidx.find( in.commitment );
      FC_ASSERT( itr != cidx.end() );
      db().remove( *itr );
   }
   const auto& add = o.amount.asset_id(db()).dynamic_asset_data_id(db());
   stored_value transport;
   db().modify( add, [&o,&transport]( asset_dynamic_data_object& obj ){
      transport = obj.confidential_supply.split( o.amount.amount + o.fee.amount );
   });
   tmp_fee.burn( transport.split(o.fee.amount) );
   db().add_balance( o.to, std::move(transport) );

   return void_result();
} FC_CAPTURE_AND_RETHROW( (o) ) }

void transfer_from_blind_evaluator::prepare_fee(account_id_type account_id, asset fee)
{
   const database& d = db();
   FC_ASSERT( fee.amount >= 0 );

   tmp_fee = stored_debt(fee.asset_id);                       // account_id borrows the fee temporarily,
   db().add_balance( account_id, tmp_fee.issue(fee.amount) ); // which is paid back from the blinded balance

   generic_evaluator::prepare_fee( account_id, fee );
}

void transfer_from_blind_evaluator::pay_fee()
{
   if( db().head_block_time() >= HARDFORK_563_TIME )
      pay_fba_fee( fba_accumulator_id_transfer_from_blind );
   else
      generic_evaluator::pay_fee();
}

void_result blind_transfer_evaluator::do_evaluate( const blind_transfer_operation& o )
{ try {
   const auto& d = db();
   o.fee.asset_id(db());  // verify fee is a legit asset 
   const auto& bbi = db().get_index_type<blinded_balance_index>();
   const auto& cidx = bbi.indices().get<by_commitment>();
   for( const auto& out : o.outputs )
   {
      for( const auto& a : out.owner.account_auths )
         a.first(d); // verify all accounts exist and are valid
   }
   for( const auto& in : o.inputs )
   {
      auto itr = cidx.find( in.commitment );
      GRAPHENE_ASSERT( itr != cidx.end(), blind_transfer_unknown_commitment, "", ("commitment",in.commitment) );
      FC_ASSERT( itr->asset_id == o.fee.asset_id );
      FC_ASSERT( itr->owner == in.owner );
   }
   return void_result();
} FC_CAPTURE_AND_RETHROW( (o) ) }

void_result blind_transfer_evaluator::do_apply( const blind_transfer_operation& o ) 
{ try {
   const auto& bbi = db().get_index_type<blinded_balance_index>();
   const auto& cidx = bbi.indices().get<by_commitment>();
   for( const auto& in : o.inputs )
   {
      auto itr = cidx.find( in.commitment );
      GRAPHENE_ASSERT( itr != cidx.end(), blind_transfer_unknown_commitment, "", ("commitment",in.commitment) );
      db().remove( *itr );
   }
   for( const auto& out : o.outputs )
   {
      db().create<blinded_balance_object>( [&]( blinded_balance_object& obj ){
          obj.asset_id   = o.fee.asset_id;
          obj.owner      = out.owner;
          obj.commitment = out.commitment;
      });
   }
   const auto& add = o.fee.asset_id(db()).dynamic_asset_data_id(db());  
   db().modify( add, [this,&o]( asset_dynamic_data_object& obj ){
      tmp_fee.burn( obj.confidential_supply.split(o.fee.amount) );
   });

   return void_result();
} FC_CAPTURE_AND_RETHROW( (o) ) }

void blind_transfer_evaluator::prepare_fee(account_id_type account_id, asset fee)
{
   const database& d = db();
   FC_ASSERT( fee.amount >= 0 );

   tmp_fee = stored_debt(fee.asset_id);                       // account_id borrows the fee temporarily,
   db().add_balance( account_id, tmp_fee.issue(fee.amount) ); // which is paid back from the blinded balance

   generic_evaluator::prepare_fee( account_id, fee );
}

void blind_transfer_evaluator::pay_fee()
{
   if( db().head_block_time() >= HARDFORK_563_TIME )
      pay_fba_fee( fba_accumulator_id_blind_transfer );
   else
      generic_evaluator::pay_fee();
}

} } // graphene::chain
