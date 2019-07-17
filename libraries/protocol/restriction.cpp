/*
 * Copyright (c) 2018 Abit More, and contributors.
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
#include <graphene/protocol/restriction.hpp>
#include <graphene/protocol/operations.hpp>
#include <graphene/protocol/fee_schedule.hpp>
#include <graphene/protocol/restriction_predicate.hpp>

#include <boost/preprocessor/variadic/to_seq.hpp>
#include <boost/preprocessor/seq/for_each_i.hpp>

#include <type_traits>

namespace graphene { namespace protocol {

template <bool B>
using bool_const = std::integral_constant<bool, B>;

struct restriction_validation_visitor;

struct argument_get_units_visitor
{
   typedef uint64_t result_type;

   template<typename T>
   inline result_type operator()( const T& t )
   {
      return 1;
   }

   inline result_type operator()( const fc::sha256& t )
   {
      return 4;
   }

   inline result_type operator()( const public_key_type& t )
   {
      return 4;
   }

   inline result_type operator()( const string& t )
   {
      return ( t.size() + 7 ) / 8;
   }

   template<typename T>
   inline result_type operator()( const flat_set<T>& t )
   {
      return t.size() * (*this)( *((const T*)nullptr) );
   }

   template<typename T>
   inline result_type operator()( const flat_set<string>& t )
   {
      result_type result = 0;
      for( const auto& s : t )
      {
         result += ( s.size() + 7 ) / 8;
      }
      return result;
   }

   result_type operator()( const vector<restriction>& t )
   {
      result_type result = 0;
      for( const auto& r : t )
      {
         result += r.argument.visit(*this);
      }
      return result;
   }
};

uint64_t restriction::get_units()const
{
   argument_get_units_visitor vtor;
   return argument.visit( vtor );
}

void restriction::validate( unsigned_int op_type )const
{
}

} } // graphene::protocol
