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
#include <graphene/chain/transaction.hpp>
#include <fc/io/raw.hpp>

namespace graphene { namespace chain {

digest_type transaction::digest(const block_id_type& ref_block_id) const
{
   digest_type::encoder enc;
   fc::raw::pack( enc, ref_block_id );
   fc::raw::pack( enc, *this );
   return enc.result();
}

digest_type processed_transaction::merkle_digest()const
{
   return digest_type::hash(*this);
}

digest_type transaction::digest()const
{
   //Only use this digest() for transactions with absolute expiration times.
   if( relative_expiration != 0 ) edump((*this));
   assert(relative_expiration == 0);
   digest_type::encoder enc;
   fc::raw::pack( enc, *this );
   return enc.result();
}
void transaction::validate() const
{
   if( relative_expiration == 0 )
      FC_ASSERT( ref_block_num == 0 && ref_block_prefix > 0 );

   for( const auto& op : operations )
      op.visit(operation_validator());
}

graphene::chain::transaction_id_type graphene::chain::transaction::id() const
{
   digest_type::encoder enc;
   fc::raw::pack(enc, *this);
   auto hash = enc.result();
   transaction_id_type result;
   memcpy(result._hash, hash._hash, std::min(sizeof(result), sizeof(hash)));
   return result;
}
void graphene::chain::signed_transaction::sign( key_id_type id, const private_key_type& key )
{
   if( relative_expiration != 0 )
   {
      if( !block_id_cache.valid() ) edump((*this));
      assert(block_id_cache.valid());
      signatures[id] =  key.sign_compact( digest(*block_id_cache) );
   } else {
      signatures[id] =  key.sign_compact( digest() );
   }
}
void graphene::chain::signed_transaction::sign( const address& addr, const private_key_type& key )
{
   if( relative_expiration != 0 )
   {
      if( !block_id_cache.valid() ) edump((*this));
      assert(block_id_cache.valid());
      extra_signatures[addr] =  key.sign_compact( digest(*block_id_cache) );
   } else {
      extra_signatures[addr] =  key.sign_compact( digest() );
   }
}

} } // graphene::chain
