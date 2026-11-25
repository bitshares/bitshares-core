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

namespace {

void enforce_output_confidential_rules( const database& d, const vector<blind_output>& outputs )
{
   const auto when = d.head_block_time();
   if( HARDFORK_BSIP_CT2_PASSED( when ) )
   {
      FC_ASSERT( outputs.size() <= GRAPHENE_RING_MAX_OUTPUTS );
      verify_bulletproof_plus_outputs( outputs );
   }
   else if( SOFTFORK_20261119_PASSED( when ) )
      verify_blind_output_range_proofs( outputs );
   if( HARDFORK_BSIP_CT1_PASSED( when ) )
   {
      for( const auto& out : outputs )
         check_required_stealth_memo( out.stealth_memo );
   }
}

fc::ecc::public_key_data single_spend_key( const authority& owner )
{
   FC_ASSERT( owner.weight_threshold == 1, "ring member owner must be a single key" );
   FC_ASSERT( owner.account_auths.empty() && owner.address_auths.empty(),
              "ring member owner must be a single key" );
   FC_ASSERT( owner.key_auths.size() == 1 && owner.key_auths.begin()->second == 1,
              "ring member owner must be a single key of weight 1" );
   return owner.key_auths.begin()->first.key_data;
}

vector<fc::ecc::public_key_data> load_ring_spend_keys( const database& d, asset_id_type asset,
                                                      const vector<ring_input>& inputs )
{
   const auto& balances = d.get_index_type<blinded_balance_index>().indices().get<by_commitment>();
   const auto& images = d.get_index_type<key_image_index>().indices().get<by_key_image>();
   vector<fc::ecc::public_key_data> keys;
   keys.reserve( inputs.size() * GRAPHENE_RING_SIZE );
   for( const auto& in : inputs )
   {
      FC_ASSERT( images.find( in.key_image ) == images.end(), "key image has already been spent" );
      for( const auto& member : in.members )
      {
         auto itr = balances.find( member );
         FC_ASSERT( itr != balances.end(), "ring member commitment does not exist" );
         FC_ASSERT( itr->asset_id == asset, "ring member belongs to a different asset" );
         keys.push_back( single_spend_key( itr->owner ) );
      }
   }
   return keys;
}

void require_single_key_output( const ring_output& out )
{
   single_spend_key( out.owner );
}

} // namespace

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
   enforce_output_confidential_rules( d, o.outputs );
   return void_result();
} FC_CAPTURE_AND_RETHROW( (o) ) }


void_result transfer_to_blind_evaluator::do_apply( const transfer_to_blind_operation& o ) 
{ try {
   db().adjust_balance( o.from, -o.amount ); 

   const auto& add = o.amount.asset_id(db()).dynamic_asset_data_id(db());  // verify fee is a legit asset 
   db().modify( add, [&]( asset_dynamic_data_object& obj ){
      obj.confidential_supply += o.amount.amount;
      FC_ASSERT( obj.confidential_supply >= 0 );
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
   FC_ASSERT( !HARDFORK_BSIP_CT3_PASSED( d.head_block_time() ),
              "transfer_from_blind is closed; use ring_transfer_from_blind" );
   o.fee.asset_id(d);  // verify fee is a legit asset 
   if( SOFTFORK_20261119_PASSED( d.head_block_time() ) )
      o.to(d); // verify the "to" account exists
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
   db().adjust_balance( o.fee_payer(), o.fee ); 
   db().adjust_balance( o.to, o.amount ); 
   const auto& bbi = db().get_index_type<blinded_balance_index>();
   const auto& cidx = bbi.indices().get<by_commitment>();
   for( const auto& in : o.inputs )
   {
      auto itr = cidx.find( in.commitment );
      FC_ASSERT( itr != cidx.end() );
      db().remove( *itr );
   }
   const auto& add = o.amount.asset_id(db()).dynamic_asset_data_id(db());  // verify fee is a legit asset 
   db().modify( add, [&]( asset_dynamic_data_object& obj ){
      obj.confidential_supply -= o.amount.amount + o.fee.amount;
      FC_ASSERT( obj.confidential_supply >= 0 );
   });
   return void_result();
} FC_CAPTURE_AND_RETHROW( (o) ) }

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
   FC_ASSERT( !HARDFORK_BSIP_CT3_PASSED( d.head_block_time() ),
              "blind_transfer is closed; use ring_blind_transfer" );
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
   enforce_output_confidential_rules( d, o.outputs );
   return void_result();
} FC_CAPTURE_AND_RETHROW( (o) ) }

void_result blind_transfer_evaluator::do_apply( const blind_transfer_operation& o ) 
{ try {
   db().adjust_balance( o.fee_payer(), o.fee ); // deposit the fee to the temp account
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
   db().modify( add, [&]( asset_dynamic_data_object& obj ){
      obj.confidential_supply -= o.fee.amount;
      FC_ASSERT( obj.confidential_supply >= 0 );
   });

   return void_result();
} FC_CAPTURE_AND_RETHROW( (o) ) }

void blind_transfer_evaluator::pay_fee()
{
   if( db().head_block_time() >= HARDFORK_563_TIME )
      pay_fba_fee( fba_accumulator_id_blind_transfer );
   else
      generic_evaluator::pay_fee();
}

namespace {

void insert_key_images( database& d, asset_id_type asset, const vector<ring_input>& inputs )
{
   for( const auto& in : inputs )
   {
      d.create<key_image_object>( [&]( key_image_object& obj ) {
         obj.key_image = in.key_image;
         obj.asset_id = asset;
      });
   }
}

void insert_ring_outputs( database& d, asset_id_type asset, const vector<ring_output>& outputs )
{
   for( const auto& out : outputs )
   {
      d.create<blinded_balance_object>( [&]( blinded_balance_object& obj ) {
         obj.asset_id = asset;
         obj.owner = out.owner;
         obj.commitment = out.commitment;
      });
   }
}

} // namespace

void_result ring_blind_transfer_evaluator::do_evaluate( const ring_blind_transfer_operation& o )
{ try {
   const auto& d = db();
   FC_ASSERT( HARDFORK_BSIP_CT3_PASSED( d.head_block_time() ),
              "ring_blind_transfer is not enabled yet" );
   const auto& atype = o.fee.asset_id( d );
   FC_ASSERT( atype.allow_confidential() );
   FC_ASSERT( !atype.is_transfer_restricted() );
   FC_ASSERT( !(atype.options.flags & white_list) );
   for( const auto& out : o.outputs )
      require_single_key_output( out );
   auto keys = load_ring_spend_keys( d, o.fee.asset_id, o.inputs );
   vector<commitment_type> outs;
   for( const auto& out : o.outputs )
      outs.push_back( out.commitment );
   const auto digest = ring_signature_message( o.fee, asset(), account_id_type(),
                                               blind_factor_type(), o.inputs, outs, o.range_proof );
   verify_ring_signatures( digest, o.inputs, keys );
   return void_result();
} FC_CAPTURE_AND_RETHROW( (o) ) }

void_result ring_blind_transfer_evaluator::do_apply( const ring_blind_transfer_operation& o )
{ try {
   db().adjust_balance( o.fee_payer(), o.fee );
   insert_key_images( db(), o.fee.asset_id, o.inputs );
   insert_ring_outputs( db(), o.fee.asset_id, o.outputs );
   const auto& add = o.fee.asset_id( db() ).dynamic_asset_data_id( db() );
   db().modify( add, [&]( asset_dynamic_data_object& obj ) {
      obj.confidential_supply -= o.fee.amount;
      FC_ASSERT( obj.confidential_supply >= 0 );
   });
   return void_result();
} FC_CAPTURE_AND_RETHROW( (o) ) }

void ring_blind_transfer_evaluator::pay_fee()
{
   pay_fba_fee( fba_accumulator_id_blind_transfer );
}

void_result ring_transfer_from_blind_evaluator::do_evaluate( const ring_transfer_from_blind_operation& o )
{ try {
   const auto& d = db();
   FC_ASSERT( HARDFORK_BSIP_CT3_PASSED( d.head_block_time() ),
              "ring_transfer_from_blind is not enabled yet" );
   const auto& atype = o.fee.asset_id( d );
   FC_ASSERT( atype.allow_confidential() );
   o.to( d );
   for( const auto& out : o.changes )
      require_single_key_output( out );
   auto keys = load_ring_spend_keys( d, o.fee.asset_id, o.inputs );
   vector<commitment_type> outs;
   for( const auto& out : o.changes )
      outs.push_back( out.commitment );
   const auto digest = ring_signature_message( o.fee, o.amount, o.to, o.blinding_factor,
                                               o.inputs, outs, o.range_proof );
   verify_ring_signatures( digest, o.inputs, keys );
   return void_result();
} FC_CAPTURE_AND_RETHROW( (o) ) }

void_result ring_transfer_from_blind_evaluator::do_apply( const ring_transfer_from_blind_operation& o )
{ try {
   db().adjust_balance( o.fee_payer(), o.fee );
   db().adjust_balance( o.to, o.amount );
   insert_key_images( db(), o.fee.asset_id, o.inputs );
   insert_ring_outputs( db(), o.fee.asset_id, o.changes );
   const auto& add = o.amount.asset_id( db() ).dynamic_asset_data_id( db() );
   db().modify( add, [&]( asset_dynamic_data_object& obj ) {
      obj.confidential_supply -= o.amount.amount + o.fee.amount;
      FC_ASSERT( obj.confidential_supply >= 0 );
   });
   return void_result();
} FC_CAPTURE_AND_RETHROW( (o) ) }

void ring_transfer_from_blind_evaluator::pay_fee()
{
   pay_fba_fee( fba_accumulator_id_transfer_from_blind );
}

} } // graphene::chain
