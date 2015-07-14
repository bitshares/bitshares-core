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
#include <graphene/chain/protocol/transaction.hpp>
#include <graphene/chain/protocol/fee_schedule.hpp>
#include <fc/io/raw.hpp>
#include <fc/bitutil.hpp>
#include <algorithm>

namespace graphene { namespace chain {


digest_type processed_transaction::merkle_digest()const
{
   return digest_type::hash(*this);
}

digest_type transaction::digest()const
{
   digest_type::encoder enc;
   fc::raw::pack( enc, *this );
   return enc.result();
}
void transaction::validate() const
{
   for( const auto& op : operations )
      operation_validate(op); 
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

const signature_type& graphene::chain::signed_transaction::sign(const private_key_type& key)
{
   signatures.push_back(key.sign_compact(digest()));
   return signatures.back();
}
signature_type graphene::chain::signed_transaction::sign(const private_key_type& key)const
{
   return key.sign_compact(digest());
}

void transaction::set_expiration( fc::time_point_sec expiration_time )
{
    expiration = expiration_time;
}

void transaction::set_reference_block( const block_id_type& reference_block )
{
   ref_block_num = fc::endian_reverse_u32(reference_block._hash[0]);
   if( ref_block_num == 0 ) ref_block_prefix = 0;
   ref_block_prefix = reference_block._hash[1];
}

void transaction::get_required_authorities( flat_set<account_id_type>& active, flat_set<account_id_type>& owner, vector<authority>& other )const
{
   for( const auto& op : operations )
      operation_get_required_authorities( op, active, owner, other );
}

struct sign_state
{
      /** returns true if we have a signature for this key or can 
       * produce a signature for this key, else returns false. 
       */
      bool signed_by( const public_key_type& k )
      {
         auto itr = signatures.find(k);
         if( itr == signatures.end() )
         {
            auto pk = keys.find(k);
            if( pk  != keys.end() )
            {
               signatures[k]   = trx.sign(pk->second);
               checked_sigs.insert(k);
               return true;
            }
            return false;
         }
         checked_sigs.insert(k);
         return true;
      }

      /**
       *  Checks to see if we have signatures of the active authorites of
       *  the accounts specified in authority or the keys specified. 
       */
      bool check_authority( const authority* au, int depth = 0 )
      {
         if( au == nullptr ) return false;
         const authority& auth = *au;

         uint32_t total_weight = 0;
         for( const auto& k : auth.key_auths )
            if( signed_by( k.first ) )
            {
               total_weight += k.second;
               if( total_weight >= auth.weight_threshold )
                  return true;
            }

         for( const auto& a : auth.account_auths )
         {
            if( approved_by.find(a.first) == approved_by.end() )
            {
               if( depth == GRAPHENE_MAX_SIG_CHECK_DEPTH )
                  return false;
               if( check_authority( get_active( a.first ), depth+1 ) )
               {
                  approved_by.insert( a.first );
                  total_weight += a.second;
                  if( total_weight >= auth.weight_threshold )
                     return true;
               }
            }
            else
            {
               total_weight += a.second;
               if( total_weight >= auth.weight_threshold )
                  return true;
            }
         }
         return total_weight >= auth.weight_threshold;
      }

      bool remove_unused_signatures()
      {
         vector<public_key_type> remove_sigs;
         for( const auto& sig : signatures )
            if( checked_sigs.find(sig.first) == checked_sigs.end() )
               remove_sigs.push_back( sig.first );
         for( auto& sig : remove_sigs )
            signatures.erase(sig);
         return remove_sigs.size() != 0;
      }

      sign_state( const signed_transaction& t, const std::function<const authority*(account_id_type)>& a,
                  const vector<private_key_type>& kys = vector<private_key_type>() )
      :trx(t),get_active(a)
      {
         auto d = trx.digest();
         for( const auto& sig : trx.signatures )
            signatures[ fc::ecc::public_key( sig, d ) ] = sig;
      
         for( const auto& key : kys ) 
            keys[key.get_public_key()] = key;
      }

      const signed_transaction&                               trx;
      const std::function<const authority*(account_id_type)>& get_active;

      flat_map<public_key_type,private_key_type>         keys;
      flat_map<public_key_type,signature_type>  signatures;

      set<public_key_type>      checked_sigs;
      flat_set<account_id_type> approved_by;

};

/**
 *  Given a set of private keys sign this transaction with a minimial subset of required keys.
 */
void signed_transaction::sign( const vector<private_key_type>& keys, 
                               const std::function<const authority*(account_id_type)>& get_active,
                               const std::function<const authority*(account_id_type)>& get_owner  )
{
   flat_set<account_id_type> required_active;
   flat_set<account_id_type> required_owner;
   vector<authority> other;
   get_required_authorities( required_active, required_owner, other );

   sign_state s(*this,get_active,keys);

   for( const auto& auth : other )
      s.check_authority(&auth);
   for( auto id : required_active )
      s.check_authority(get_active(id));
   for( auto id : required_owner )
      s.check_authority(get_owner(id));
   
   s.remove_unused_signatures();

   signatures.clear();
   for( const auto& sig : s.signatures )
      signatures.push_back(sig.second);
}

bool signed_transaction::verify( const std::function<const authority*(account_id_type)>& get_active,
                                 const std::function<const authority*(account_id_type)>& get_owner  )const
{
   flat_set<account_id_type> required_active;
   flat_set<account_id_type> required_owner;
   vector<authority> other;
   get_required_authorities( required_active, required_owner, other );

   sign_state s(*this,get_active);

   for( const auto& auth : other )
      if( !s.check_authority(&auth) )
         return false;

   // fetch all of the top level authorities
   for( auto id : required_active )
      if( !s.check_authority(get_active(id)) )
         return false;
   for( auto id : required_owner )
      if( !s.check_authority(get_owner(id)) )
         return false;
   if( s.remove_unused_signatures() )
      return false;
   return true;
}

} } // graphene::chain
