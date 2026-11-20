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
#include <graphene/protocol/transaction.hpp>

namespace graphene { namespace protocol {

   class block_header
   {
   public:
      digest_type                   digest()const;
      block_id_type                 previous;
      uint32_t                      block_num()const { return num_from_id(previous) + 1; }
      fc::time_point_sec            timestamp;
      witness_id_type               witness;
      checksum_type                 transaction_merkle_root;
      // Note: when we need to add data to `extensions`, remember to review `database::_generate_block()`.
      //       More info in https://github.com/bitshares/bitshares-core/issues/1136
      extensions_type               extensions;

      virtual ~block_header() = default;

      static uint32_t num_from_id(const block_id_type& id);
   };

   class signed_block_header : public block_header
   {
   public:
      const block_id_type&       id()const;
      const fc::ecc::public_key& signee()const;
      void                       sign( const fc::ecc::private_key& signer );
      bool                       validate_signee( const fc::ecc::public_key& expected_signee )const;

      signature_type             witness_signature;
      fc::pq_gated< fc::optional<pq_signature> >  witness_pq_signature;

      void                       sign_pq( const fc::pq_private_key& signer );
      bool                       validate_signee_pq( const pq_public_key_type& expected_signee )const;

      signed_block_header() = default;
      explicit signed_block_header( const block_header& header ) : block_header( header ) {}
   protected:
      mutable fc::ecc::public_key _signee;
      mutable block_id_type       _block_id;
      /// Serialization format the cached _block_id was computed under. A block's id depends
      /// on the format, so the cache is only valid while the format is unchanged -- see
      /// signed_block_header::id().
      mutable fc::raw::pq_format  _block_id_format = fc::raw::pq_format::legacy;
   };

   class signed_block : public signed_block_header
   {
   public:
      const checksum_type& calculate_merkle_root()const;
      vector<processed_transaction> transactions;
   protected:
      mutable checksum_type   _calculated_merkle_root;
      /// Serialization format the cached merkle root was computed under. The root is built
      /// from processed_transaction::merkle_digest(), which packs under the ambient format,
      /// so the cache is only valid while that format is unchanged -- see
      /// signed_block::calculate_merkle_root() and the same fix in signed_block_header::id().
      mutable fc::raw::pq_format _merkle_root_format = fc::raw::pq_format::legacy;
   };

} } // graphene::protocol

FC_REFLECT( graphene::protocol::block_header, (previous)(timestamp)(witness)(transaction_merkle_root)(extensions) )
FC_REFLECT_DERIVED( graphene::protocol::signed_block_header, (graphene::protocol::block_header), (witness_signature)(witness_pq_signature) )
FC_REFLECT_DERIVED( graphene::protocol::signed_block, (graphene::protocol::signed_block_header), (transactions) )


namespace fc { namespace raw {

void pack( datastream<size_t>& s, const graphene::protocol::signed_block_header& v, uint32_t _max_depth = FC_PACK_MAX_DEPTH );
void pack( sha256::encoder& s, const graphene::protocol::signed_block_header& v, uint32_t _max_depth = FC_PACK_MAX_DEPTH );
// sha224 is required because signed_block_header::id() hashes with fc::sha224, and without an
// overload for this exact encoder type the call falls back to fc's generic reflected pack.
//
// That fallback used to change the bytes: the reflected packer emitted witness_pq_signature
// unconditionally, so every block id differed from a pre-PQ node's, from block 1, before any
// hardfork. It no longer can -- the field is an fc::pq_gated and gates itself whichever packer
// runs -- but the overload stays, because relying on the reflected path for a block id would
// still be relying on which instantiation happens to win.
void pack( sha224::encoder& s, const graphene::protocol::signed_block_header& v, uint32_t _max_depth = FC_PACK_MAX_DEPTH );
void pack( datastream<char*>& s, const graphene::protocol::signed_block_header& v, uint32_t _max_depth = FC_PACK_MAX_DEPTH );
void unpack( datastream<const char*>& s, graphene::protocol::signed_block_header& v, uint32_t _max_depth = FC_PACK_MAX_DEPTH );

void pack( datastream<size_t>& s, const graphene::protocol::signed_block& v, uint32_t _max_depth = FC_PACK_MAX_DEPTH );
void pack( sha256::encoder& s, const graphene::protocol::signed_block& v, uint32_t _max_depth = FC_PACK_MAX_DEPTH );
void pack( sha224::encoder& s, const graphene::protocol::signed_block& v, uint32_t _max_depth = FC_PACK_MAX_DEPTH );
void pack( datastream<char*>& s, const graphene::protocol::signed_block& v, uint32_t _max_depth = FC_PACK_MAX_DEPTH );
void unpack( datastream<const char*>& s, graphene::protocol::signed_block& v, uint32_t _max_depth = FC_PACK_MAX_DEPTH );

extern template std::vector<char> pack( const graphene::protocol::signed_block_header& v, uint32_t _max_depth );
extern template std::vector<char> pack( const graphene::protocol::signed_block& v, uint32_t _max_depth );
extern template size_t pack_size( const graphene::protocol::signed_block_header& v );
extern template size_t pack_size( const graphene::protocol::signed_block& v );

} } // namespace fc::raw
GRAPHENE_DECLARE_EXTERNAL_SERIALIZATION( graphene::protocol::block_header)
GRAPHENE_DECLARE_EXTERNAL_SERIALIZATION( graphene::protocol::signed_block_header)
GRAPHENE_DECLARE_EXTERNAL_SERIALIZATION( graphene::protocol::signed_block)
