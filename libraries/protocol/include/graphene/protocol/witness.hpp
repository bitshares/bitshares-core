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
#include <graphene/protocol/base.hpp>
#include <graphene/protocol/asset.hpp>

namespace graphene { namespace protocol { 

  /**
    * @brief Create a witness object, as a bid to hold a witness position on the network.
    * @ingroup operations
    *
    * Accounts which wish to become witnesses may use this operation to create a witness object which stakeholders may
    * vote on to approve its position as a witness.
    */
   struct witness_create_operation : public base_operation
   {
      struct fee_params_t { uint64_t fee = 5000 * GRAPHENE_BLOCKCHAIN_PRECISION; };

      asset             fee;
      /// The account which owns the witness. This account pays the fee for this operation.
      account_id_type   witness_account;
      string            url;
      public_key_type   block_signing_key;
      /// optional post-quantum (NIST FIPS 204 ML-DSA) block signing key
      fc::pq_gated< optional< pq_public_key_type > > block_pq_signing_key;

      account_id_type fee_payer()const { return witness_account; }
      void            validate()const;
   };

  /**
    * @brief Update a witness object's URL and block signing key.
    * @ingroup operations
    */
   struct witness_update_operation : public base_operation
   {
      struct fee_params_t
      {
         share_type fee = 20 * GRAPHENE_BLOCKCHAIN_PRECISION;
      };

      asset             fee;
      /// The witness object to update.
      witness_id_type   witness;
      /// The account which owns the witness. This account pays the fee for this operation.
      account_id_type   witness_account;
      /// The new URL.
      optional< string > new_url;
      /// The new block signing key.
      optional< public_key_type > new_signing_key;
      /// The new post-quantum (NIST FIPS 204 ML-DSA) block signing key.
      fc::pq_gated< optional< pq_public_key_type > > new_pq_signing_key;

      account_id_type fee_payer()const { return witness_account; }
      void            validate()const;
   };

   /// TODO: witness_resign_operation : public base_operation

} } // graphene::protocol

// The post-quantum signing keys are reflected -- so JSON, the API and the operation's
// field-name machinery all see them -- but they are NOT part of the reflected binary
// packing: fc::raw::pack/unpack for these two operations is written out by hand in
// witness.cpp so that the new field only appears on the wire under the post-quantum
// format, the same way authority gates pq_key_auths.
//
// Reflecting them for binary packing instead appends a field to the wire format of an
// operation that has existed since genesis, unconditionally. Every historical block
// containing a witness_create or witness_update operation then fails to deserialize,
// because the reader looks for an optional that was never written and consumes a byte
// belonging to the next field. That is not a post-activation problem: it breaks replay of
// the existing chain immediately, so a node could not sync mainnet from genesis at all.
FC_REFLECT( graphene::protocol::witness_create_operation::fee_params_t, (fee) )
FC_REFLECT( graphene::protocol::witness_create_operation, (fee)(witness_account)(url)(block_signing_key)(block_pq_signing_key) )

FC_REFLECT( graphene::protocol::witness_update_operation::fee_params_t, (fee) )
FC_REFLECT( graphene::protocol::witness_update_operation, (fee)(witness)(witness_account)(new_url)(new_signing_key)(new_pq_signing_key) )

namespace fc { namespace raw {
   void pack( datastream<size_t>& s, const graphene::protocol::witness_create_operation& v,
              uint32_t _max_depth = FC_PACK_MAX_DEPTH );
   void pack( sha256::encoder& s, const graphene::protocol::witness_create_operation& v,
              uint32_t _max_depth = FC_PACK_MAX_DEPTH );
   void pack( datastream<char*>& s, const graphene::protocol::witness_create_operation& v,
              uint32_t _max_depth = FC_PACK_MAX_DEPTH );
   void unpack( datastream<const char*>& s, graphene::protocol::witness_create_operation& v,
                uint32_t _max_depth = FC_PACK_MAX_DEPTH );

   void pack( datastream<size_t>& s, const graphene::protocol::witness_update_operation& v,
              uint32_t _max_depth = FC_PACK_MAX_DEPTH );
   void pack( sha256::encoder& s, const graphene::protocol::witness_update_operation& v,
              uint32_t _max_depth = FC_PACK_MAX_DEPTH );
   void pack( datastream<char*>& s, const graphene::protocol::witness_update_operation& v,
              uint32_t _max_depth = FC_PACK_MAX_DEPTH );
   void unpack( datastream<const char*>& s, graphene::protocol::witness_update_operation& v,
                uint32_t _max_depth = FC_PACK_MAX_DEPTH );
} } // namespace fc::raw

GRAPHENE_DECLARE_EXTERNAL_SERIALIZATION( graphene::protocol::witness_create_operation::fee_params_t )
GRAPHENE_DECLARE_EXTERNAL_SERIALIZATION( graphene::protocol::witness_update_operation::fee_params_t )
GRAPHENE_DECLARE_EXTERNAL_SERIALIZATION( graphene::protocol::witness_create_operation )
GRAPHENE_DECLARE_EXTERNAL_SERIALIZATION( graphene::protocol::witness_update_operation )
