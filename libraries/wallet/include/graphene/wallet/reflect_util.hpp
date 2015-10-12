/*
 * Copyright (c) 2015 Cryptonomex, Inc., and contributors.  All rights reserved.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */
#pragma once

// This file contains various reflection methods that are used to
// support the wallet, e.g. allow specifying operations by name
// instead of ID.

namespace graphene { namespace wallet {

struct static_variant_map
{
   flat_map< string, int > name_to_which;
   vector< string > which_to_name;
};

namespace impl {

struct static_variant_map_visitor
{
   static_variant_map_visitor() {}

   typedef void result_type;

   template< typename T >
   result_type operator()( const T& dummy )
   {
      assert( which == m.which_to_name.size() );
      std::string name = js_name<T>::name();
      m.name_to_which[ name ] = which;
      m.which_to_name.push_back( name );
   }

   static_variant_map m;
   int which;
};

} // namespace impl

template< typename T >
T from_which_variant( int which, const variant& v )
{
   // Parse a variant for a known which()
   T result;
   result.set_which( which );
   from_variant( v, result );
   return result;
}

template<typename T>
static_variant_map create_static_variant_map()
{
   T dummy;
   int n = dummy.count();
   impl::static_variant_map_visitor vtor;
   for( int i=0; i<n; i++ )
   {
      dummy.set_which(i);
      vtor.which = i;
      dummy.visit( vtor );
   }
   return vtor.m;
}

} } // namespace graphene::wallet
