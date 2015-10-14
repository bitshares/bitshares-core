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

#include <graphene/chain/account_object.hpp>
#include <graphene/chain/asset_object.hpp>
#include <graphene/chain/committee_member_object.hpp>
#include <graphene/chain/witness_object.hpp>
#include <graphene/chain/market_evaluator.hpp>
#include <graphene/chain/proposal_object.hpp>
#include <graphene/chain/operation_history_object.hpp>
#include <graphene/chain/withdraw_permission_object.hpp>

using namespace fc;
using namespace graphene::chain;

namespace graphene { namespace wallet {

template< typename ObjectType >
object* create_object_of_type( const variant& v )
{
   return new ObjectType( v.as<ObjectType>() );
}

object* create_object( const variant& v )
{
   const variant_object& obj = v.get_object();
   object_id_type obj_id = obj["id"].as< object_id_type >();

   FC_ASSERT( obj_id.type() == protocol_ids );

   //
   // Sufficiently clever template metaprogramming might
   // be able to convince the compiler to emit this switch
   // instead of creating it explicitly.
   //
   switch( obj_id.space() )
   {
      /*
      case null_object_type:
         return nullptr;
      case base_object_type:
         return create_object_of_type< base_object >( v );
      */
      case account_object_type:
         return create_object_of_type< account_object >( v );
      case asset_object_type:
         return create_object_of_type< asset_object >( v );
      case force_settlement_object_type:
         return create_object_of_type< force_settlement_object >( v );
      case committee_member_object_type:
         return create_object_of_type< committee_member_object >( v );
      case witness_object_type:
         return create_object_of_type< witness_object >( v );
      case limit_order_object_type:
         return create_object_of_type< limit_order_object >( v );
      case call_order_object_type:
         return create_object_of_type< call_order_object >( v );
      /*
      case custom_object_type:
         return create_object_of_type< custom_object >( v );
      */
      case proposal_object_type:
         return create_object_of_type< proposal_object >( v );
      case operation_history_object_type:
         return create_object_of_type< operation_history_object >( v );
      case withdraw_permission_object_type:
         return create_object_of_type< withdraw_permission_object >( v );
      default:
         ;
   }
   FC_ASSERT( false, "unknown type_id" );
}

} }
