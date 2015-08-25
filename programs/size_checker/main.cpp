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

#include <fc/io/json.hpp>
#include <fc/smart_ref_impl.hpp>
#include <fc/variant.hpp>
#include <fc/variant_object.hpp>

#include <graphene/chain/protocol/protocol.hpp>

#include <algorithm>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using namespace graphene::chain;

vector< fc::variant_object > g_op_types;

template< typename T >
uint64_t get_wire_size()
{
   T data;
   return fc::raw::pack( data ).size();
}

struct size_check_type_visitor
{
   typedef void result_type;

   int t = 0;
   size_check_type_visitor(int _t ):t(_t){}

   template<typename Type>
   result_type operator()( const Type& op )const
   {
      fc::mutable_variant_object vo;
      vo["name"] = fc::get_typename<Type>::name();
      vo["mem_size"] = sizeof( Type );
      vo["wire_size"] = get_wire_size<Type>();
      g_op_types.push_back( vo );
   }
};

int main( int argc, char** argv )
{
   try
   {
      graphene::chain::operation op;

      for( int32_t i = 0; i < op.count(); ++i )
      {
         op.set_which(i);
         op.visit( size_check_type_visitor(i) );
      }

      // sort them by mem size
      std::stable_sort( g_op_types.begin(), g_op_types.end(),
      [](const variant_object& oa, const variant_object& ob) {
      return oa["mem_size"].as_uint64() > ob["mem_size"].as_uint64();
      });
      std::cout << "[\n";
      for( size_t i=0; i<g_op_types.size(); i++ )
      {
         std::cout << "   " << fc::json::to_string( g_op_types[i] );
         if( i < g_op_types.size()-1 )
            std::cout << ",\n";
         else
            std::cout << "\n";
      }
      std::cout << "]\n";
      std::cerr << "Size of block header: " << sizeof( block_header ) << " " << fc::raw::pack_size( block_header() ) << "\n";
   }
   catch ( const fc::exception& e ){ edump((e.to_detail_string())); }
   idump((sizeof(signed_block)));
   idump((fc::raw::pack_size(signed_block())));
   return 0;
}
