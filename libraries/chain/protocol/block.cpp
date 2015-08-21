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
#include <graphene/chain/protocol/block.hpp>
#include <fc/io/raw.hpp>
#include <fc/bitutil.hpp>
#include <algorithm>

namespace graphene { namespace chain {
   digest_type block_header::digest()const
   {
      return digest_type::hash(*this);
   }

   uint32_t block_header::num_from_id(const block_id_type& id)
   {
      return fc::endian_reverse_u32(id._hash[0]);
   }

   block_id_type signed_block_header::id()const
   {
      auto tmp = fc::sha224::hash( *this );
      tmp._hash[0] = fc::endian_reverse_u32(block_num()); // store the block num in the ID, 160 bits is plenty for the hash
      static_assert( sizeof(tmp._hash[0]) == 4, "should be 4 bytes" );
      block_id_type result;
      memcpy(result._hash, tmp._hash, std::min(sizeof(result), sizeof(tmp)));
      return result;
   }

   fc::ecc::public_key signed_block_header::signee()const
   {
      return fc::ecc::public_key( witness_signature, digest(), true/*enforce canonical*/ );
   }

   void signed_block_header::sign( const fc::ecc::private_key& signer )
   {
      witness_signature = signer.sign_compact( digest() );
   }

   bool signed_block_header::validate_signee( const fc::ecc::public_key& expected_signee )const
   {
      return signee() == expected_signee;
   }

   checksum_type signed_block::calculate_merkle_root()const
   {
      if( transactions.size() == 0 ) 
         return checksum_type();

      vector<digest_type>  ids;
      ids.resize( ((transactions.size() + 1)/2)*2 );
      for( uint32_t i = 0; i < transactions.size(); ++i )
         ids[i] = transactions[i].merkle_digest();

      vector<digest_type>::size_type current_number_of_hashes = ids.size();
      while( true )
      {
#define AUG_20_TESTNET_COMPATIBLE
#ifdef AUG_20_TESTNET_COMPATIBLE
         for( uint32_t i = 0; i < transactions.size(); i += 2 )
#else 
         for( uint32_t i = 0; i < current_number_of_hashes; i += 2 )
#endif
           ids[i/2] = digest_type::hash( std::make_pair( ids[i], ids[i+1] ) );
         // since we're processing hashes in pairs, we need to ensure that we always
         // have an even number of hashes in the ids list.  If we would end up with
         // an odd number, add a default-initialized hash to compensate
         current_number_of_hashes /= 2;
#ifdef AUG_20_TESTNET_COMPATIBLE
         if (current_number_of_hashes <= 1)
            break;
#else
         if (current_number_of_hashes == 1)
            break;
         if (current_number_of_hashes % 2)
         {
            ++current_number_of_hashes;
            // TODO: HARD FORK: we should probably enable the next line the next time we fire
            // up a new testnet; it will change the merkle roots we generate, but will 
            // give us a better-defined algorithm for calculating them
            //
            ids[current_number_of_hashes - 1] = digest_type();
         }
#endif
      }
      return checksum_type::hash( ids[0] );
   }

} }
