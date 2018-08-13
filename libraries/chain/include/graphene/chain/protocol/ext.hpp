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
#pragma once

#include <fc/io/varint.hpp>
#include <fc/reflect/reflect.hpp>

namespace graphene { namespace chain {

using fc::unsigned_int;

template< typename T >
struct extension
{
   extension() {}

   T value;
};

template< typename T >
struct graphene_extension_pack_count_visitor
{
   graphene_extension_pack_count_visitor( const T& v ) : value(v) {}

   template<typename Member, class Class, Member (Class::*member)>
   void operator()( const char* name )const
   {
      count += ((value.*member).valid()) ? 1 : 0;
   }

   const T& value;
   mutable uint32_t count = 0;
};

template< typename Stream, typename T >
struct graphene_extension_pack_read_visitor
{
   graphene_extension_pack_read_visitor( Stream& s, const T& v, uint32_t _max_depth )
   : stream(s), value(v), max_depth(_max_depth - 1)
   {
      FC_ASSERT( _max_depth > 0 );
   }

   template<typename Member, class Class, Member (Class::*member)>
   void operator()( const char* name )const
   {
      if( (value.*member).valid() )
      {
         fc::raw::pack( stream, unsigned_int( which ), max_depth );
         fc::raw::pack( stream, *(value.*member), max_depth );
      }
      ++which;
   }

   Stream& stream;
   const T& value;
   mutable uint32_t which = 0;
   const uint32_t max_depth;
};


template< typename Stream, typename T >
struct graphene_extension_unpack_visitor
{
   graphene_extension_unpack_visitor( Stream& s, T& v, uint32_t _max_depth )
   : stream(s), value(v), max_depth(_max_depth - 1)
   {
      FC_ASSERT( _max_depth > 0 );
      unsigned_int c;
      fc::raw::unpack( stream, c, max_depth );
      count_left = c.value;
      maybe_read_next_which();
   }

   void maybe_read_next_which()const
   {
      if( count_left > 0 )
      {
         unsigned_int w;
         fc::raw::unpack( stream, w, max_depth );
         next_which = w.value;
      }
   }

   template< typename Member, class Class, Member (Class::*member)>
   void operator()( const char* name )const
   {
      if( (count_left > 0) && (which == next_which) )
      {
         typename Member::value_type temp;
         fc::raw::unpack( stream, temp, max_depth );
         (value.*member) = temp;
         --count_left;
         maybe_read_next_which();
      }
      else
         (value.*member).reset();
      ++which;
   }

   mutable uint32_t      which = 0;
   mutable uint32_t next_which = 0;
   mutable uint32_t count_left = 0;

   Stream& stream;
   T& value;
   const uint32_t max_depth;
};

} } // graphene::chain

namespace fc {

template< typename T >
struct graphene_extension_from_variant_visitor
{
   graphene_extension_from_variant_visitor( const variant_object& v, T& val, uint32_t max_depth )
      : vo( v ), value( val ), _max_depth(max_depth - 1)
   {
      FC_ASSERT( max_depth > 0, "Recursion depth exceeded!" );
      count_left = vo.size();
   }

   template<typename Member, class Class, Member (Class::*member)>
   void operator()( const char* name )const
   {
      auto it = vo.find(name);
      if( it != vo.end() )
      {
         from_variant( it->value(), (value.*member), _max_depth );
         assert( count_left > 0 );    // x.find(k) returns true for n distinct values of k only if x.size() >= n
         --count_left;
      }
   }

   const variant_object& vo;
   T& value;
   const uint32_t _max_depth;
   mutable uint32_t count_left = 0;
};

template< typename T >
void from_variant( const fc::variant& var, graphene::chain::extension<T>& value, uint32_t max_depth )
{
   value = graphene::chain::extension<T>();
   if( var.is_null() )
      return;
   if( var.is_array() )
   {
      FC_ASSERT( var.size() == 0 );
      return;
   }

   graphene_extension_from_variant_visitor<T> vtor( var.get_object(), value.value, max_depth );
   fc::reflector<T>::visit( vtor );
   FC_ASSERT( vtor.count_left == 0 );    // unrecognized extension throws here
}

template< typename T >
struct graphene_extension_to_variant_visitor
{
   graphene_extension_to_variant_visitor( const T& v, uint32_t max_depth ) : value(v), mvo(max_depth) {}

   template<typename Member, class Class, Member (Class::*member)>
   void operator()( const char* name )const
   {
      if( (value.*member).valid() )
         mvo( name, value.*member );
   }

   const T& value;
   mutable limited_mutable_variant_object mvo;
};

template< typename T >
void to_variant( const graphene::chain::extension<T>& value, fc::variant& var, uint32_t max_depth )
{
   graphene_extension_to_variant_visitor<T> vtor( value.value, max_depth );
   fc::reflector<T>::visit( vtor );
   var = vtor.mvo;
}

namespace raw {

template< typename Stream, typename T >
void pack( Stream& stream, const graphene::chain::extension<T>& value, uint32_t _max_depth=FC_PACK_MAX_DEPTH )
{
   FC_ASSERT( _max_depth > 0 );
   --_max_depth;
   graphene::chain::graphene_extension_pack_count_visitor<T> count_vtor( value.value );
   fc::reflector<T>::visit( count_vtor );
   fc::raw::pack( stream, unsigned_int( count_vtor.count ), _max_depth );
   graphene::chain::graphene_extension_pack_read_visitor<Stream,T> read_vtor( stream, value.value, _max_depth );
   fc::reflector<T>::visit( read_vtor );
}


template< typename Stream, typename T >
void unpack( Stream& s, graphene::chain::extension<T>& value, uint32_t _max_depth=FC_PACK_MAX_DEPTH )
{
   FC_ASSERT( _max_depth > 0 );
   --_max_depth;
   value = graphene::chain::extension<T>();
   graphene::chain::graphene_extension_unpack_visitor<Stream, T> vtor( s, value.value, _max_depth );
   fc::reflector<T>::visit( vtor );
   FC_ASSERT( vtor.count_left == 0 ); // unrecognized extension throws here
}

} // fc::raw

template<typename T> struct get_typename< graphene::chain::extension<T> >
{ 
   static const char* name()
   { 
      static std::string n = std::string("graphene::chain::extension<") 
         + fc::get_typename<T>::name() + std::string(">");
      return n.c_str();
   } 
};


} // fc
