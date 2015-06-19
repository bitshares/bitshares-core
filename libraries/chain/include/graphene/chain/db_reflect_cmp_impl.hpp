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

#pragma once

#include <fc/io/raw.hpp>

#include <cstdint>
#include <vector>

//
// This file implements the comparison used by the assert op.
// The entry point for comparison is _cmp(), it can be specialized for
// different types.  The default implementation defers to _ser_eq_cmp()
// which only allows equality comparisons, and asserts
//

namespace graphene { namespace chain { namespace impl {

// useful for types which have all comparison ops implemented
template< typename T >
static bool _full_cmp( const T& a, const T& b, uint8_t opc )
{
   switch( opc )
   {
      case opc_equal_to:
         return (a == b);
      case opc_not_equal_to:
         return (a != b);
      case opc_greater:
         return (a >  b);
      case opc_less:
         return (a <  b);
      case opc_greater_equal:
         return (a >= b);
      case opc_less_equal:
         return (a <= b);
      default:
         FC_ASSERT( false, "unknown comparison operator" );
   }
}

// useful for types which have operator== implemented
template< typename T >
static bool _eq_cmp( const T& a, const T& b, uint8_t opc )
{
   switch( opc )
   {
      case opc_equal_to:
         return (a == b);
      case opc_not_equal_to:
         return !(a == b);
      default:
         FC_ASSERT( false, "unknown comparison operator" );
   }
}

// works for every serializable type
template< typename T >
static bool _ser_eq_cmp( const T& a, const std::vector<char>& b, uint8_t opc )
{
   std::vector< char > _a = fc::raw::pack( a );
   return _eq_cmp( _a, b, opc );
}

/*
static bool _cmp( const fc::sha224& a, const fc::sha224& b, uint8_t opc )
{
   assert( a.data_size() == b.data_size() );
   int result = memcmp( a.data(), b.data(), a.data_size() );
   switch( opc )
   {
      case opc_equal_to:
         return (result == 0);
      case opc_not_equal_to:
         return (result != 0);
      case opc_greater:
         return (result >  0);
      case opc_less:
         return (result <  0);
      case opc_greater_equal:
         return (result >= 0);
      case opc_less_equal:
         return (result <= 0);
      default:
         FC_ASSERT( false, "unknown comparison operator" );
   }
}
*/

// _cmp needs to be specialized for types which don't have overloads
// for all comparison operators

template< typename T >
static bool _cmp( const T& a, const std::vector<char>& b, uint8_t opc )
{
   return _ser_eq_cmp( a, b, opc );
}

} } } // graphene::chain::detail
