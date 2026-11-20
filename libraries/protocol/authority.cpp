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

#include <graphene/protocol/authority.hpp>

#include <fc/io/raw.hpp>

namespace graphene { namespace protocol {

void add_authority_accounts(
   flat_set<account_id_type>& result,
   const authority& a
   )
{
   for( auto& item : a.account_auths )
      result.insert( item.first );
}

} } // graphene::protocol


namespace fc { namespace raw {

namespace detail {

template<typename Stream>
void pack_authority_impl( Stream& s, const graphene::protocol::authority& v, uint32_t _max_depth )
{
   FC_ASSERT( _max_depth > 0 );
   --_max_depth;
   fc::raw::pack( s, v.weight_threshold, _max_depth );
   fc::raw::pack( s, v.account_auths, _max_depth );
   fc::raw::pack( s, v.key_auths, _max_depth );
   if( fc::raw::get_pq_format() == fc::raw::pq_format::current )
      fc::raw::pack( s, v.pq_key_auths, _max_depth );
   fc::raw::pack( s, v.address_auths, _max_depth );
}

template<typename Stream>
void unpack_authority_impl( Stream& s, graphene::protocol::authority& v, uint32_t _max_depth )
{ try {
   FC_ASSERT( _max_depth > 0 );
   --_max_depth;
   fc::raw::unpack( s, v.weight_threshold, _max_depth );
   fc::raw::unpack( s, v.account_auths, _max_depth );
   fc::raw::unpack( s, v.key_auths, _max_depth );
   if( fc::raw::get_pq_format() == fc::raw::pq_format::current )
      fc::raw::unpack( s, v.pq_key_auths, _max_depth );
   else
      v.pq_key_auths.clear();
   fc::raw::unpack( s, v.address_auths, _max_depth );
} FC_RETHROW_EXCEPTIONS( warn, "error unpacking authority" ) }

} // namespace detail

void pack( datastream<size_t>& s, const graphene::protocol::authority& v, uint32_t _max_depth )
   { detail::pack_authority_impl( s, v, _max_depth ); }
void pack( sha256::encoder& s, const graphene::protocol::authority& v, uint32_t _max_depth )
   { detail::pack_authority_impl( s, v, _max_depth ); }
void pack( datastream<char*>& s, const graphene::protocol::authority& v, uint32_t _max_depth )
   { detail::pack_authority_impl( s, v, _max_depth ); }
void unpack( datastream<const char*>& s, graphene::protocol::authority& v, uint32_t _max_depth )
   { detail::unpack_authority_impl( s, v, _max_depth ); }

// Explicitly instantiate the 1-arg vector pack + pack_size so the extern
// template in other TUs resolve here.
template std::vector<char> pack( const graphene::protocol::authority& v, uint32_t _max_depth );
template size_t pack_size( const graphene::protocol::authority& v );

} } // namespace fc::raw

GRAPHENE_IMPLEMENT_EXTERNAL_SERIALIZATION( graphene::protocol::authority )
