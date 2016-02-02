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
#include <graphene/chain/protocol/confidential.hpp>
#include <graphene/chain/confidential_evaluator.hpp>
#include <graphene/chain/database.hpp>

#include <fc/crypto/base58.hpp>
#include <fc/io/raw.hpp>
#include <fc/crypto/elliptic.hpp>

namespace graphene { namespace chain {

void transfer_to_blind_operation::validate()const
{
   FC_ASSERT( fee.amount >= 0 );
   FC_ASSERT( amount.amount > 0 );

   vector<commitment_type> in;
   vector<commitment_type> out(outputs.size());
   int64_t                 net_public = amount.amount.value;
   for( uint32_t i = 0; i < out.size(); ++i )
   {
      out[i] = outputs[i].commitment;
      /// require all outputs to be sorted prevents duplicates AND prevents implementations
      /// from accidentally leaking information by how they arrange commitments.
      if( i > 0 ) FC_ASSERT( out[i-1] < out[i], "all outputs must be sorted by commitment id" );
      FC_ASSERT( !outputs[i].owner.is_impossible() );
   }
   FC_ASSERT( out.size(), "there must be at least one output" );

   auto public_c = fc::ecc::blind(blinding_factor,net_public);

   FC_ASSERT( fc::ecc::verify_sum( {public_c}, out, 0 ), "", ("net_public",net_public) );

   if( outputs.size() > 1 )
   {
      for( auto out : outputs )
      {
         auto info = fc::ecc::range_get_info( out.range_proof );
         FC_ASSERT( info.max_value <= GRAPHENE_MAX_SHARE_SUPPLY );
      }
   }
}

share_type transfer_to_blind_operation::calculate_fee( const fee_parameters_type& k )const
{
    return k.fee + outputs.size() * k.price_per_output;
}


void transfer_from_blind_operation::validate()const
{
   FC_ASSERT( amount.amount > 0 );
   FC_ASSERT( fee.amount >= 0 );
   FC_ASSERT( inputs.size() > 0 );
   FC_ASSERT( amount.asset_id == fee.asset_id );


   vector<commitment_type> in(inputs.size());
   vector<commitment_type> out;
   int64_t                 net_public = fee.amount.value + amount.amount.value;
   out.push_back( fc::ecc::blind( blinding_factor, net_public ) );
   for( uint32_t i = 0; i < in.size(); ++i )
   {
      in[i] = inputs[i].commitment;
      /// by requiring all inputs to be sorted we also prevent duplicate commitments on the input
      if( i > 0 ) FC_ASSERT( in[i-1] < in[i], "all inputs must be sorted by commitment id" );
   }
   FC_ASSERT( in.size(), "there must be at least one input" );
   FC_ASSERT( fc::ecc::verify_sum( in, out, 0 ) );
}


/**
 *  If fee_payer = temp_account_id, then the fee is paid by the surplus balance of inputs-outputs and
 *  100% of the fee goes to the network.
 */
account_id_type blind_transfer_operation::fee_payer()const
{
   return GRAPHENE_TEMP_ACCOUNT;
}


/**
 *  This method can be computationally intensive because it verifies that input commitments - output commitments add up to 0
 */
void blind_transfer_operation::validate()const
{ try {
   vector<commitment_type> in(inputs.size());
   vector<commitment_type> out(outputs.size());
   int64_t                 net_public = fee.amount.value;//from_amount.value - to_amount.value;
   for( uint32_t i = 0; i < in.size(); ++i )
   {
      in[i] = inputs[i].commitment;
      /// by requiring all inputs to be sorted we also prevent duplicate commitments on the input
      if( i > 0 ) FC_ASSERT( in[i-1] < in[i] );
   }
   for( uint32_t i = 0; i < out.size(); ++i )
   {
      out[i] = outputs[i].commitment;
      if( i > 0 ) FC_ASSERT( out[i-1] < out[i] );
      FC_ASSERT( !outputs[i].owner.is_impossible() );
   }
   FC_ASSERT( in.size(), "there must be at least one input" );
   FC_ASSERT( fc::ecc::verify_sum( in, out, net_public ), "", ("net_public", net_public) );

   if( outputs.size() > 1 )
   {
      for( auto out : outputs )
      {
         auto info = fc::ecc::range_get_info( out.range_proof );
         FC_ASSERT( info.max_value <= GRAPHENE_MAX_SHARE_SUPPLY );
      }
   }
   FC_ASSERT( fc::ecc::verify_sum( in, out, net_public ), "", ("net_public", net_public) );
} FC_CAPTURE_AND_RETHROW( (*this) ) }

share_type blind_transfer_operation::calculate_fee( const fee_parameters_type& k )const
{
    return k.fee + outputs.size() * k.price_per_output;
}




/**
 *  Packs *this then encodes as base58 encoded string.
 */
stealth_confirmation::operator string()const
{
   return fc::to_base58( fc::raw::pack( *this ) );
}
/**
 * Unpacks from a base58 string
 */
stealth_confirmation::stealth_confirmation( const std::string& base58 )
{
   *this = fc::raw::unpack<stealth_confirmation>( fc::from_base58( base58 ) );
}



} } // graphene::chain
