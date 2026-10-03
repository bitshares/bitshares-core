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
#include <graphene/protocol/witness.hpp>

#include <fc/io/raw.hpp>

namespace graphene { namespace protocol {

void witness_create_operation::validate() const
{
   FC_ASSERT(fee.amount >= 0);
   FC_ASSERT(url.size() < GRAPHENE_MAX_URL_LENGTH );
}

void witness_update_operation::validate() const
{
   FC_ASSERT(fee.amount >= 0);
   if( new_url.valid() )
       FC_ASSERT(new_url->size() < GRAPHENE_MAX_URL_LENGTH );
}

} } // graphene::protocol

namespace fc { namespace raw {

namespace detail {

// Post-quantum: the signing-key fields these two operations gained must not appear on the
// wire under the legacy format. They were added straight into FC_REFLECT, whose field order
// is the wire order, which appended an optional to operations that have existed since
// genesis. Reading a historical block then went looking for a field that was never written,
// took a byte from whatever followed, and the transaction's `extensions` field -- the next
// thing along -- came apart on a nonsense variant tag. Replay of the existing chain broke
// immediately, with no hardfork involved.
//
// Gate them the way authority gates pq_key_auths, and append rather than insert, so legacy
// bytes are byte-identical to what a pre-PQ node produces.

template<typename Stream>
void pack_witness_create_impl( Stream& s, const graphene::protocol::witness_create_operation& v,
                               uint32_t _max_depth )
{
   FC_ASSERT( _max_depth > 0 );
   --_max_depth;
   fc::raw::pack( s, v.fee, _max_depth );
   fc::raw::pack( s, v.witness_account, _max_depth );
   fc::raw::pack( s, v.url, _max_depth );
   fc::raw::pack( s, v.block_signing_key, _max_depth );
   fc::raw::pack( s, v.block_pq_signing_key, _max_depth );   // gates itself; see fc::pq_gated
}

template<typename Stream>
void unpack_witness_create_impl( Stream& s, graphene::protocol::witness_create_operation& v,
                                 uint32_t _max_depth )
{ try {
   FC_ASSERT( _max_depth > 0 );
   --_max_depth;
   fc::raw::unpack( s, v.fee, _max_depth );
   fc::raw::unpack( s, v.witness_account, _max_depth );
   fc::raw::unpack( s, v.url, _max_depth );
   fc::raw::unpack( s, v.block_signing_key, _max_depth );
   fc::raw::unpack( s, v.block_pq_signing_key, _max_depth );
} FC_RETHROW_EXCEPTIONS( warn, "error unpacking witness_create_operation" ) }

template<typename Stream>
void pack_witness_update_impl( Stream& s, const graphene::protocol::witness_update_operation& v,
                               uint32_t _max_depth )
{
   FC_ASSERT( _max_depth > 0 );
   --_max_depth;
   fc::raw::pack( s, v.fee, _max_depth );
   fc::raw::pack( s, v.witness, _max_depth );
   fc::raw::pack( s, v.witness_account, _max_depth );
   fc::raw::pack( s, v.new_url, _max_depth );
   fc::raw::pack( s, v.new_signing_key, _max_depth );
   fc::raw::pack( s, v.new_pq_signing_key, _max_depth );   // gates itself; see fc::pq_gated
}

template<typename Stream>
void unpack_witness_update_impl( Stream& s, graphene::protocol::witness_update_operation& v,
                                 uint32_t _max_depth )
{ try {
   FC_ASSERT( _max_depth > 0 );
   --_max_depth;
   fc::raw::unpack( s, v.fee, _max_depth );
   fc::raw::unpack( s, v.witness, _max_depth );
   fc::raw::unpack( s, v.witness_account, _max_depth );
   fc::raw::unpack( s, v.new_url, _max_depth );
   fc::raw::unpack( s, v.new_signing_key, _max_depth );
   fc::raw::unpack( s, v.new_pq_signing_key, _max_depth );
} FC_RETHROW_EXCEPTIONS( warn, "error unpacking witness_update_operation" ) }

} // namespace detail

void pack( datastream<size_t>& s, const graphene::protocol::witness_create_operation& v, uint32_t d )
   { detail::pack_witness_create_impl( s, v, d ); }
void pack( sha256::encoder& s, const graphene::protocol::witness_create_operation& v, uint32_t d )
   { detail::pack_witness_create_impl( s, v, d ); }
void pack( datastream<char*>& s, const graphene::protocol::witness_create_operation& v, uint32_t d )
   { detail::pack_witness_create_impl( s, v, d ); }
void unpack( datastream<const char*>& s, graphene::protocol::witness_create_operation& v, uint32_t d )
   { detail::unpack_witness_create_impl( s, v, d ); }

void pack( datastream<size_t>& s, const graphene::protocol::witness_update_operation& v, uint32_t d )
   { detail::pack_witness_update_impl( s, v, d ); }
void pack( sha256::encoder& s, const graphene::protocol::witness_update_operation& v, uint32_t d )
   { detail::pack_witness_update_impl( s, v, d ); }
void pack( datastream<char*>& s, const graphene::protocol::witness_update_operation& v, uint32_t d )
   { detail::pack_witness_update_impl( s, v, d ); }
void unpack( datastream<const char*>& s, graphene::protocol::witness_update_operation& v, uint32_t d )
   { detail::unpack_witness_update_impl( s, v, d ); }

} } // namespace fc::raw

GRAPHENE_IMPLEMENT_EXTERNAL_SERIALIZATION( graphene::protocol::witness_create_operation::fee_params_t )
GRAPHENE_IMPLEMENT_EXTERNAL_SERIALIZATION( graphene::protocol::witness_update_operation::fee_params_t )
GRAPHENE_IMPLEMENT_EXTERNAL_SERIALIZATION( graphene::protocol::witness_create_operation )
GRAPHENE_IMPLEMENT_EXTERNAL_SERIALIZATION( graphene::protocol::witness_update_operation )
