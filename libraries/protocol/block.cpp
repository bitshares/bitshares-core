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
#include <boost/endian/conversion.hpp>
#include <graphene/protocol/block.hpp>
#include <graphene/protocol/fee_schedule.hpp>
#include <fc/io/raw.hpp>
#include <algorithm>

namespace graphene { namespace protocol {
   digest_type block_header::digest()const
   {
      return digest_type::hash(*this);
   }

   uint32_t block_header::num_from_id(const block_id_type& id)
   {
      return boost::endian::endian_reverse(id._hash[0].value());
   }

   const block_id_type& signed_block_header::id()const
   {
      // Post-quantum: the id depends on the serialization format, because that format
      // decides whether witness_pq_signature is part of the hashed bytes. Caching on
      // "computed yet?" alone therefore freezes a block's id to whichever format happened
      // to be in effect the first time anyone asked for it.
      //
      // That is not hypothetical: a receiving node calls id() while logging the block, at
      // which point the block has been decoded but not yet applied, so chain state still
      // reports the pre-hardfork format. The legacy id was cached and then reused by
      // everything downstream -- including the block database -- so the producer and the
      // receiver ended up recording different ids for a byte-for-byte identical block, and
      // the receiver rejected the next block as unlinkable and stopped for good.
      //
      // Keep the format the cache was built under, and recompute when it differs.
      const auto current_fmt = fc::raw::get_pq_format();
      if( 0 == _block_id._hash[0].value() || _block_id_format != current_fmt )
      {
         _block_id_format = current_fmt;
         // Must use the encoder explicitly rather than the fc::sha224::hash<T> helper:
         // that helper calls a qualified fc::raw::pack() from inside fc's own template,
         // so the pq_format-aware overload declared in block.hpp is not in its candidate
         // set and it silently falls back to reflected packing -- which always emits
         // witness_pq_signature and so changes every block id relative to a pre-PQ node.
         fc::sha224::encoder enc;
         fc::raw::pack( enc, *this );
         auto tmp = enc.result();
         tmp._hash[0] = boost::endian::endian_reverse(block_num()); // store the block num in the ID, 160 bits is plenty for the hash
         static_assert( sizeof(tmp._hash[0]) == 4, "should be 4 bytes" );
         memcpy(_block_id._hash, tmp._hash, std::min(sizeof(_block_id), sizeof(tmp)));
      }
      return _block_id;
   }

   const fc::ecc::public_key& signed_block_header::signee()const
   {
      if( !_signee.valid() )
         _signee = fc::ecc::public_key( witness_signature, digest(), true/*enforce canonical*/ );
      return _signee;
   }

   void signed_block_header::sign( const fc::ecc::private_key& signer )
   {
      witness_signature = signer.sign_compact( digest() );
   }

   bool signed_block_header::validate_signee( const fc::ecc::public_key& expected_signee )const
   {
      return signee() == expected_signee;
   }

   void signed_block_header::sign_pq( const fc::pq_private_key& signer )
   {
      pq_signature sig;
      sig.key = pq_public_key_type( signer.get_public_key() );
      sig.signature = signer.sign( digest() );
      witness_pq_signature = sig;
   }

   bool signed_block_header::validate_signee_pq( const pq_public_key_type& expected_signee )const
   {
      return witness_pq_signature.valid()
             && witness_pq_signature->key == expected_signee
             && witness_pq_signature->key.to_pqc().verify( digest(), witness_pq_signature->signature );
   }

   const checksum_type& signed_block::calculate_merkle_root()const
   {
      static const checksum_type empty_checksum;
      if( transactions.size() == 0 ) 
         return empty_checksum;

      // Post-quantum: same hazard as signed_block_header::id(). The root is built from
      // merkle_digest(), which packs each transaction under the ambient format, so caching
      // on "computed yet?" alone would freeze the root to whichever format was in effect on
      // the first call. A block whose root was computed before it was applied -- while
      // chain state still reported the pre-hardfork format -- would then keep that root and
      // fail validation against the network's.
      //
      // This never showed up in devnet testing because every block produced there was
      // empty, and the size check above returns before the cache is ever consulted.
      const auto current_fmt = fc::raw::get_pq_format();
      if( 0 == _calculated_merkle_root._hash[0].value() || _merkle_root_format != current_fmt )
      {
         _merkle_root_format = current_fmt;
         vector<digest_type> ids;
         ids.resize( transactions.size() );
         for( uint32_t i = 0; i < transactions.size(); ++i )
            ids[i] = transactions[i].merkle_digest();

         vector<digest_type>::size_type current_number_of_hashes = ids.size();
         while( current_number_of_hashes > 1 )
         {
            // hash ID's in pairs
            uint32_t i_max = current_number_of_hashes - (current_number_of_hashes&1);
            uint32_t k = 0;

            for( uint32_t i = 0; i < i_max; i += 2 )
               ids[k++] = digest_type::hash( std::make_pair( ids[i], ids[i+1] ) );

            if( current_number_of_hashes&1 )
               ids[k++] = ids[i_max];
            current_number_of_hashes = k;
         }
         _calculated_merkle_root = checksum_type::hash( ids[0] );
      }
      return _calculated_merkle_root;
   }
} }


namespace fc { namespace raw {

namespace detail {

template<typename Stream>
void pack_signed_block_header_impl( Stream& s, const graphene::protocol::signed_block_header& v, uint32_t _max_depth )
{
   FC_ASSERT( _max_depth > 0 );
   --_max_depth;
   fc::raw::pack( s, static_cast<const graphene::protocol::block_header&>(v), _max_depth );
   fc::raw::pack( s, v.witness_signature, _max_depth );
   fc::raw::pack( s, v.witness_pq_signature, _max_depth );   // gates itself; see fc::pq_gated
}

template<typename Stream>
void unpack_signed_block_header_impl( Stream& s, graphene::protocol::signed_block_header& v, uint32_t _max_depth )
{ try {
   FC_ASSERT( _max_depth > 0 );
   --_max_depth;
   fc::raw::unpack( s, static_cast<graphene::protocol::block_header&>(v), _max_depth );
   fc::raw::unpack( s, v.witness_signature, _max_depth );
   fc::raw::unpack( s, v.witness_pq_signature, _max_depth );
} FC_RETHROW_EXCEPTIONS( warn, "error unpacking signed_block_header" ) }

template<typename Stream>
void pack_signed_block_impl( Stream& s, const graphene::protocol::signed_block& v, uint32_t _max_depth )
{
   FC_ASSERT( _max_depth > 0 );
   --_max_depth;
   detail::pack_signed_block_header_impl( s, static_cast<const graphene::protocol::signed_block_header&>(v), _max_depth );
   fc::raw::pack( s, v.transactions, _max_depth );
}

template<typename Stream>
void unpack_signed_block_impl( Stream& s, graphene::protocol::signed_block& v, uint32_t _max_depth )
{ try {
   FC_ASSERT( _max_depth > 0 );
   --_max_depth;
   detail::unpack_signed_block_header_impl( s, static_cast<graphene::protocol::signed_block_header&>(v), _max_depth );
   fc::raw::unpack( s, v.transactions, _max_depth );
} FC_RETHROW_EXCEPTIONS( warn, "error unpacking signed_block" ) }

} // namespace detail

void pack( datastream<size_t>& s, const graphene::protocol::signed_block_header& v, uint32_t _max_depth )
   { detail::pack_signed_block_header_impl( s, v, _max_depth ); }
void pack( sha256::encoder& s, const graphene::protocol::signed_block_header& v, uint32_t _max_depth )
   { detail::pack_signed_block_header_impl( s, v, _max_depth ); }
void pack( sha224::encoder& s, const graphene::protocol::signed_block_header& v, uint32_t _max_depth )
   { detail::pack_signed_block_header_impl( s, v, _max_depth ); }
void pack( datastream<char*>& s, const graphene::protocol::signed_block_header& v, uint32_t _max_depth )
   { detail::pack_signed_block_header_impl( s, v, _max_depth ); }
void unpack( datastream<const char*>& s, graphene::protocol::signed_block_header& v, uint32_t _max_depth )
   { detail::unpack_signed_block_header_impl( s, v, _max_depth ); }

void pack( datastream<size_t>& s, const graphene::protocol::signed_block& v, uint32_t _max_depth )
   { detail::pack_signed_block_impl( s, v, _max_depth ); }
void pack( sha256::encoder& s, const graphene::protocol::signed_block& v, uint32_t _max_depth )
   { detail::pack_signed_block_impl( s, v, _max_depth ); }
void pack( sha224::encoder& s, const graphene::protocol::signed_block& v, uint32_t _max_depth )
   { detail::pack_signed_block_impl( s, v, _max_depth ); }
void pack( datastream<char*>& s, const graphene::protocol::signed_block& v, uint32_t _max_depth )
   { detail::pack_signed_block_impl( s, v, _max_depth ); }
void unpack( datastream<const char*>& s, graphene::protocol::signed_block& v, uint32_t _max_depth )
   { detail::unpack_signed_block_impl( s, v, _max_depth ); }

template std::vector<char> pack( const graphene::protocol::signed_block_header& v, uint32_t _max_depth );
template std::vector<char> pack( const graphene::protocol::signed_block& v, uint32_t _max_depth );
template size_t pack_size( const graphene::protocol::signed_block_header& v );
template size_t pack_size( const graphene::protocol::signed_block& v );

} } // namespace fc::raw

GRAPHENE_IMPLEMENT_EXTERNAL_SERIALIZATION( graphene::protocol::block_header)
GRAPHENE_IMPLEMENT_EXTERNAL_SERIALIZATION( graphene::protocol::signed_block_header)
GRAPHENE_IMPLEMENT_EXTERNAL_SERIALIZATION( graphene::protocol::signed_block)
