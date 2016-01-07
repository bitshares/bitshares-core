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
