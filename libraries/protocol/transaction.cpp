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

#include <graphene/protocol/transaction.hpp>
#include <graphene/protocol/block.hpp>
#include <graphene/protocol/exceptions.hpp>
#include <graphene/protocol/fee_schedule.hpp>
#include <graphene/protocol/pts_address.hpp>
#include <graphene/protocol/restriction_predicate.hpp>

#include <fc/io/raw.hpp>

namespace graphene { namespace protocol {

digest_type processed_transaction::merkle_digest()const
{
   // Deliberately no scoped_pq_format override here -- see declaration comment.
   digest_type::encoder enc;
   fc::raw::pack( enc, *this );
   return enc.result();
}

digest_type transaction::digest( fc::raw::pq_format fmt )const
{
   fc::raw::scoped_pq_format f( fmt );
   digest_type::encoder enc;
   fc::raw::pack( enc, *this );
   return enc.result();
}

digest_type transaction::sig_digest( const chain_id_type& chain_id, fc::raw::pq_format fmt )const
{
   fc::raw::scoped_pq_format f( fmt );
   digest_type::encoder enc;
   fc::raw::pack( enc, chain_id );
   fc::raw::pack( enc, *this );
   return enc.result();
}

void transaction::validate() const
{
   FC_ASSERT( operations.size() > 0, "A transaction must have at least one operation", ("trx",*this) );
   for( const auto& op : operations )
      operation_validate(op);
}

uint64_t transaction::get_packed_size() const
{
   return fc::raw::pack_size(*this);
}

const transaction_id_type& transaction::id() const
{
   // Explicitly pass the ambient format rather than calling digest() with its own default:
   // digest()'s default parameter value is a fixed compile-time constant, so calling it with
   // no argument would silently ignore whatever fc::raw::scoped_pq_format the caller (e.g.
   // database::_apply_transaction / _precompute_parallel) has already set up from chain
   // state -- exactly the bug class described in transaction::sig_digest()'s callers. Without
   // this, two transactions differing only in an embedded authority's pq_key_auths content
   // could silently hash to the same id() once legacy format is in play, since legacy format
   // omits pq_key_auths from the packed bytes entirely.
   auto h = digest( fc::raw::get_pq_format() );
   memcpy(_tx_id_buffer._hash, h._hash, std::min(sizeof(_tx_id_buffer), sizeof(h)));
   return _tx_id_buffer;
}

const signature_type& graphene::protocol::signed_transaction::sign(const private_key_type& key, const chain_id_type& chain_id,
                                                                      fc::raw::pq_format fmt)
{
   digest_type h = sig_digest( chain_id, fmt );
   signatures.push_back(key.sign_compact(h));
   return signatures.back();
}

signature_type graphene::protocol::signed_transaction::sign(const private_key_type& key, const chain_id_type& chain_id,
                                                              fc::raw::pq_format fmt)const
{
   fc::raw::scoped_pq_format f( fmt );
   digest_type::encoder enc;
   fc::raw::pack( enc, chain_id );
   fc::raw::pack( enc, *this );
   return key.sign_compact(enc.result());
}


const pq_signature& graphene::protocol::signed_transaction::sign_pq(const fc::pq_private_key& key, const chain_id_type& chain_id,
                                                                      fc::raw::pq_format fmt)
{
   pq_signatures.push_back( static_cast<const signed_transaction&>(*this).sign_pq( key, chain_id, fmt ) );
   return pq_signatures.back();
}

pq_signature graphene::protocol::signed_transaction::sign_pq(const fc::pq_private_key& key, const chain_id_type& chain_id,
                                                               fc::raw::pq_format fmt)const
{
   pq_signature result;
   result.key = pq_public_key_type( key.get_public_key() );
   result.signature = key.sign( sig_digest( chain_id, fmt ) );
   return result;
}

void transaction::set_expiration( fc::time_point_sec expiration_time )
{
    expiration = expiration_time;
}

void transaction::set_reference_block( const block_id_type& reference_block )
{
   ref_block_num = boost::endian::endian_reverse(reference_block._hash[0].value());
   ref_block_prefix = reference_block._hash[1].value();
}

void transaction::get_required_authorities( flat_set<account_id_type>& active,
                                            flat_set<account_id_type>& owner,
                                            vector<authority>& other,
                                            bool ignore_custom_operation_required_auths )const
{
   for( const auto& op : operations )
      operation_get_required_authorities( op, active, owner, other, ignore_custom_operation_required_auths );
   for( const auto& account : owner )
      active.erase( account );
}


const flat_set<public_key_type> empty_keyset;
const flat_set<pq_public_key_type> empty_pq_keyset;

struct sign_state
{
      /** returns true if we have a signature for this key or can
       * produce a signature for this key, else returns false.
       */
      bool signed_by( const public_key_type& k )
      {
         auto itr = provided_signatures.find(k);
         if( itr == provided_signatures.end() )
         {
            auto pk = available_keys.find(k);
            if( pk  != available_keys.end() )
               return provided_signatures[k] = true;
            return false;
         }
         return itr->second = true;
      }

      bool signed_by( const pq_public_key_type& k )
      {
         auto itr = provided_pq_signatures.find(k);
         if( itr == provided_pq_signatures.end() )
            return false;
         return itr->second = true;
      }

      optional<map<address,public_key_type>> available_address_sigs;
      /// Addresses of the PQ signatures actually provided. Only ever populated when PQ
      /// signatures are allowed at all, which is what keeps pre-hardfork behaviour identical.
      optional<map<address,pq_public_key_type>> provided_pq_address_sigs;
      optional<map<address,public_key_type>> provided_address_sigs;

      bool signed_by( const address& a ) {
         if( !available_address_sigs ) {
            available_address_sigs = std::map<address,public_key_type>();
            provided_address_sigs = std::map<address,public_key_type>();
            for( auto& item : available_keys ) {
             (*available_address_sigs)[ address(pts_address(item, false) ) ] = item; // verison = 56 (default)
             (*available_address_sigs)[ address(pts_address(item, true) ) ] = item; // verison = 56 (default)
             (*available_address_sigs)[ address(pts_address(item, false, 0) ) ] = item;
             (*available_address_sigs)[ address(pts_address(item, true, 0) ) ] = item;
             (*available_address_sigs)[ address(item) ] = item;
            }
            for( auto& item : provided_signatures ) {
             (*provided_address_sigs)[ address(pts_address(item.first, false) ) ] = item.first; //verison 56 (default)
             (*provided_address_sigs)[ address(pts_address(item.first, true) ) ] = item.first; // verison 56 (default)
             (*provided_address_sigs)[ address(pts_address(item.first, false, 0) ) ] = item.first;
             (*provided_address_sigs)[ address(pts_address(item.first, true, 0) ) ] = item.first;
             (*provided_address_sigs)[ address(item.first) ] = item.first;
            }

            // An address auth names a hash, not a key type. A PQ key hashing to that address
            // satisfies it exactly as a classic key does -- otherwise an account protected by
            // address_auths could never be moved to post-quantum keys at all, and would stay
            // quantum-vulnerable no matter what else it did.
            //
            // Only the pts_address variants are omitted: those encode a secp256k1-specific
            // legacy format from the PTS import and have no meaning for an ML-DSA key.
            provided_pq_address_sigs = std::map<address,pq_public_key_type>();
            for( auto& item : provided_pq_signatures )
             (*provided_pq_address_sigs)[ address(item.first) ] = item.first;
         }
         // A PQ signature satisfying this address counts before anything else is considered:
         // it is a signature that was actually provided, which is the strongest evidence here.
         auto pq_itr = provided_pq_address_sigs->find(a);
         if( pq_itr != provided_pq_address_sigs->end() )
            return provided_pq_signatures[pq_itr->second] = true;

         auto itr = provided_address_sigs->find(a);
         if( itr == provided_address_sigs->end() )
         {
            auto aitr = available_address_sigs->find(a);
            if( aitr != available_address_sigs->end() ) {
               auto pk = available_keys.find(aitr->second);
               if( pk != available_keys.end() )
                  return provided_signatures[aitr->second] = true;
               return false;
            }
            // Nothing provided and nothing available: the address is simply not signed for.
            // Upstream falls through to the line below and dereferences an end() iterator,
            // which is undefined behaviour that happens to yield `true` on libstdc++ -- i.e.
            // it would report an unsigned address auth as satisfied. Unreachable today,
            // because account_create/account_update::validate() reject address_auths outright
            // so no account can carry one, but it is on the path this test exercises.
            return false;
         }
         return provided_signatures[itr->second] = true;
      }

      bool check_authority( account_id_type id )
      {
         if( approved_by.find(id) != approved_by.end() ) return true;
         return check_authority( get_active(id) ) || ( allow_non_immediate_owner && check_authority( get_owner(id) ) );
      }

      /**
       *  Checks to see if we have signatures of the active authorites of
       *  the accounts specified in authority or the keys specified.
       */
      bool check_authority( const authority* au, uint32_t depth = 0 )
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

         for( const auto& k : auth.pq_key_auths )
            if( signed_by( k.first ) )
            {
               total_weight += k.second;
               if( total_weight >= auth.weight_threshold )
                  return true;
            }

         for( const auto& k : auth.address_auths )
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
               if( depth == max_recursion )
                  continue;
               if( check_authority( get_active( a.first ), depth+1 )
                     || ( allow_non_immediate_owner && check_authority( get_owner( a.first ), depth+1 ) ) )
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
         for( const auto& sig : provided_signatures )
            if( !sig.second ) remove_sigs.push_back( sig.first );

         for( auto& sig : remove_sigs )
            provided_signatures.erase(sig);

         // Mirror the same "unused signature is irrelevant" check for PQ signatures. Without
         // this, a transaction could carry an extra PQ signature from a key not required by
         // any authority and verify_authority() would never flag it via tx_irrelevant_sig,
         // unlike the equivalent classical-key case just above.
         vector<pq_public_key_type> remove_pq_sigs;
         for( const auto& sig : provided_pq_signatures )
            if( !sig.second ) remove_pq_sigs.push_back( sig.first );

         for( auto& sig : remove_pq_sigs )
            provided_pq_signatures.erase(sig);

         return remove_sigs.size() != 0 || remove_pq_sigs.size() != 0;
      }

      sign_state( const flat_set<public_key_type>& sigs,
                  const std::function<const authority*(account_id_type)>& active,
                  const std::function<const authority*(account_id_type)>& owner,
                  bool allow_owner,
                  uint32_t max_recursion_depth = GRAPHENE_MAX_SIG_CHECK_DEPTH,
                  const flat_set<public_key_type>& keys = empty_keyset,
                  const flat_set<pq_public_key_type>& pq_sigs = empty_pq_keyset )
      :  get_active(active),
         get_owner(owner),
         allow_non_immediate_owner(allow_owner),
         max_recursion(max_recursion_depth),
         available_keys(keys)
      {
         for( const auto& key : sigs )
            provided_signatures[ key ] = false;
         for( const auto& key : pq_sigs )
            provided_pq_signatures[ key ] = false;
         approved_by.insert( GRAPHENE_TEMP_ACCOUNT  );
      }

      const std::function<const authority*(account_id_type)>& get_active;
      const std::function<const authority*(account_id_type)>& get_owner;

      const bool                       allow_non_immediate_owner;
      const uint32_t                   max_recursion;
      const flat_set<public_key_type>& available_keys;

      flat_map<public_key_type,bool>   provided_signatures;
      flat_map<pq_public_key_type,bool> provided_pq_signatures;
      flat_set<account_id_type>        approved_by;
};


void verify_authority( const vector<operation>& ops, const flat_set<public_key_type>& sigs,
                       const std::function<const authority*(account_id_type)>& get_active,
                       const std::function<const authority*(account_id_type)>& get_owner,
                       const custom_authority_lookup& get_custom,
                       bool allow_non_immediate_owner,
                       bool ignore_custom_operation_required_auths,
                       uint32_t max_recursion_depth,
                       bool  allow_committee,
                       bool  allow_pq,
                       const flat_set<account_id_type>& active_aprovals,
                       const flat_set<account_id_type>& owner_approvals,
                       const flat_set<pq_public_key_type>& pq_sigs )
{
   rejected_predicate_map rejected_custom_auths;
   try {
   flat_set<account_id_type> required_active;
   flat_set<account_id_type> required_owner;
   vector<authority> other;

   const flat_set<pq_public_key_type>& effective_pq_sigs = allow_pq ? pq_sigs : empty_pq_keyset;

   sign_state s( sigs, get_active, get_owner, allow_non_immediate_owner, max_recursion_depth, empty_keyset, effective_pq_sigs );
   for( auto& id : active_aprovals )
      s.approved_by.insert( id );
   for( auto& id : owner_approvals )
      s.approved_by.insert( id );

   auto approved_by_custom_authority = [&s, &rejected_custom_auths, get_custom = std::move(get_custom)](
           account_id_type account,
           operation op ) mutable {
      auto viable_custom_auths = get_custom( account, op, &rejected_custom_auths );
      for( const auto& auth : viable_custom_auths )
         if( s.check_authority( &auth ) ) return true;
      return false;
   };

   for( const auto& op : ops ) {
      flat_set<account_id_type> operation_required_active;
      operation_get_required_authorities( op, operation_required_active, required_owner, other,
                                          ignore_custom_operation_required_auths );

      auto itr = operation_required_active.begin();
      while ( itr != operation_required_active.end() ) {
         if ( approved_by_custom_authority( *itr, op ) )
            itr = operation_required_active.erase( itr );
         else
            ++itr;
      }

      required_active.insert( operation_required_active.begin(), operation_required_active.end() );
   }

   if( !allow_committee )
      GRAPHENE_ASSERT( required_active.find(GRAPHENE_COMMITTEE_ACCOUNT) == required_active.end(),
                       invalid_committee_approval, "Committee account may only propose transactions" );

   for( const auto& auth : other )
   {
      GRAPHENE_ASSERT( s.check_authority(&auth), tx_missing_other_auth, "Missing Authority", ("auth",auth)("sigs",sigs) );
   }

   // fetch all of the top level authorities
   for( auto id : required_owner )
   {
      GRAPHENE_ASSERT( owner_approvals.find(id) != owner_approvals.end() ||
                       s.check_authority(get_owner(id)),
                       tx_missing_owner_auth, "Missing Owner Authority ${id}", ("id",id)("auth",*get_owner(id)) );
   }

   for( auto id : required_active )
   {
      GRAPHENE_ASSERT( s.check_authority(id) ||
                       s.check_authority(get_owner(id)),
                       tx_missing_active_auth, "Missing Active Authority ${id}",
                       ("id",id)("auth",*get_active(id))("owner",*get_owner(id)) );
   }

   GRAPHENE_ASSERT(
      !s.remove_unused_signatures(),
      tx_irrelevant_sig,
      "Unnecessary signature(s) detected"
      );
} FC_CAPTURE_AND_RETHROW( (rejected_custom_auths)(ops)(sigs) ) }


const flat_set<public_key_type>& signed_transaction::get_signature_keys( const chain_id_type& chain_id,
                                                                         fc::raw::pq_format fmt )const
{ try {
   auto d = sig_digest( chain_id, fmt );
   flat_set<public_key_type> result;
   for( const auto&  sig : signatures )
   {
      GRAPHENE_ASSERT(
         result.insert( fc::ecc::public_key(sig,d) ).second,
            tx_duplicate_sig,
            "Duplicate Signature detected" );
   }
   _signees = std::move( result );
   return _signees;
} FC_CAPTURE_AND_RETHROW() }


set<public_key_type> signed_transaction::get_required_signatures( const chain_id_type& chain_id,
                                                                  const flat_set<public_key_type>& available_keys,
                                                                  const std::function<const authority*(account_id_type)>& get_active,
                                                                  const std::function<const authority*(account_id_type)>& get_owner,
                                                                  bool allow_non_immediate_owner,
                                                                  bool ignore_custom_operation_required_authorities,
                                                                  uint32_t max_recursion_depth )const
{
   flat_set<account_id_type> required_active;
   flat_set<account_id_type> required_owner;
   vector<authority> other;
   get_required_authorities( required_active, required_owner, other, ignore_custom_operation_required_authorities );

   const flat_set<public_key_type>& signature_keys = get_signature_keys(chain_id);
   sign_state s( signature_keys, get_active, get_owner, allow_non_immediate_owner, max_recursion_depth, available_keys );

   for( const auto& auth : other )
      s.check_authority( &auth );
   for( auto& owner : required_owner )
      s.check_authority( get_owner( owner ) );
   for( auto& active : required_active )
      s.check_authority( active ) || s.check_authority( get_owner( active ) );

   s.remove_unused_signatures();

   set<public_key_type> result;

   for( auto& provided_sig : s.provided_signatures )
      if( available_keys.find( provided_sig.first ) != available_keys.end()
            && signature_keys.find( provided_sig.first ) == signature_keys.end() )
         result.insert( provided_sig.first );

   return result;
}

set<public_key_type> signed_transaction::minimize_required_signatures(
         const chain_id_type& chain_id,
         const flat_set<public_key_type>& available_keys,
         const std::function<const authority*(account_id_type)>& get_active,
         const std::function<const authority*(account_id_type)>& get_owner,
         const custom_authority_lookup &get_custom,
         bool allow_non_immediate_owner,
         bool ignore_custom_operation_required_auths,
         uint32_t max_recursion )const
{
   set< public_key_type > s = get_required_signatures( chain_id, available_keys, get_active, get_owner,
                                                       allow_non_immediate_owner,
                                                       ignore_custom_operation_required_auths, max_recursion );
   flat_set< public_key_type > result( s.begin(), s.end() );

   for( const public_key_type& k : s )
   {
      result.erase( k );
      try
      {
         graphene::protocol::verify_authority( operations, result, get_active, get_owner, get_custom,
                                               allow_non_immediate_owner,ignore_custom_operation_required_auths,
                                               max_recursion );
         continue;  // element stays erased if verify_authority is ok
      }
      catch( const tx_missing_owner_auth& e ) {}
      catch( const tx_missing_active_auth& e ) {}
      catch( const tx_missing_other_auth& e ) {}
      result.insert( k );
   }
   return set<public_key_type>( result.begin(), result.end() );
}

const transaction_id_type& precomputable_transaction::id()const
{
   if( 0 == _tx_id_buffer._hash[0].value() )
      transaction::id();
   return _tx_id_buffer;
}

void precomputable_transaction::validate() const
{
   if( _validated ) return;
   transaction::validate();
   _validated = true;
}

uint64_t precomputable_transaction::get_packed_size()const
{
   // Post-quantum: the packed size counts the post-quantum fields only under the current
   // format, so a size cached under one format is wrong under the other. That matters
   // beyond bookkeeping -- this feeds fee calculation and the per-block size accounting, so
   // a stale value makes nodes disagree about what a transaction costs and whether a block
   // is over its limit. A transaction precomputed while it sat in the pending pool before
   // activation, then re-applied after it, would hit exactly that.
   const auto current_fmt = fc::raw::get_pq_format();
   if( _packed_size == 0 || _packed_size_format != current_fmt )
   {
      _packed_size_format = current_fmt;
      _packed_size = transaction::get_packed_size();
   }
   return _packed_size;
}

const flat_set<public_key_type>& precomputable_transaction::get_signature_keys( const chain_id_type& chain_id,
                                                                                 fc::raw::pq_format fmt )const
{
   // Strictly we should check whether the given chain ID is same as the one used to initialize the `signees` field.
   // However, we don't pass in another chain ID so far, for better performance, we skip the check.
   //
   // Post-quantum: the format, unlike the chain id, genuinely does vary between calls, and
   // it cannot be skipped. sig_digest() hashes different bytes under each format, so the
   // public keys recovered from the signatures differ too. Caching on "empty?" alone would
   // let a set recovered under the wrong format persist and be handed to the authority
   // checks, which is a wrong answer about who signed a transaction rather than merely a
   // stale one.
   if( _signees.empty() || _signees_format != fmt )
   {
      _signees_format = fmt;
      _signees.clear();
      signed_transaction::get_signature_keys( chain_id, fmt );
   }
   return _signees;
}

void signed_transaction::verify_authority( const chain_id_type& chain_id,
                                           const std::function<const authority*(account_id_type)>& get_active,
                                           const std::function<const authority*(account_id_type)>& get_owner,
                                           const custom_authority_lookup& get_custom,
                                           bool allow_non_immediate_owner,
                                           bool ignore_custom_operation_required_auths,
                                           uint32_t max_recursion )const
{ try {
   const auto fmt = fc::raw::get_pq_format();
   const digest_type d = sig_digest( chain_id, fmt );
   flat_set<pq_public_key_type> pq_keys;
   for( const auto& sig : pq_signatures )
   {
      GRAPHENE_ASSERT(
         sig.key.to_pqc().verify( d, sig.signature ),
         tx_invalid_pq_signature,
         "Invalid post-quantum signature" );
      GRAPHENE_ASSERT(
         pq_keys.insert( sig.key ).second,
         tx_duplicate_sig,
         "Duplicate post-quantum signature detected" );
   }
   graphene::protocol::verify_authority( operations, get_signature_keys( chain_id, fmt ), get_active, get_owner,
                                         get_custom, allow_non_immediate_owner,
                                         ignore_custom_operation_required_auths, max_recursion,
                                         false, true, flat_set<account_id_type>(), flat_set<account_id_type>(), pq_keys );
} FC_CAPTURE_AND_RETHROW( (*this) ) }

} } // graphene::protocol


namespace fc { namespace raw {

namespace detail {

template<typename Stream>
void pack_signed_transaction_impl( Stream& s, const graphene::protocol::signed_transaction& v, uint32_t _max_depth )
{
   FC_ASSERT( _max_depth > 0 );
   --_max_depth;
   fc::raw::pack( s, static_cast<const graphene::protocol::transaction&>(v), _max_depth );
   fc::raw::pack( s, v.signatures, _max_depth );
   // No format check here any more: pq_signatures is an fc::pq_gated, which gates itself on
   // every path. The check used to live here, and the reflected packer -- which knows nothing
   // about it -- disagreed.
   fc::raw::pack( s, v.pq_signatures, _max_depth );
}

template<typename Stream>
void unpack_signed_transaction_impl( Stream& s, graphene::protocol::signed_transaction& v, uint32_t _max_depth )
{ try {
   FC_ASSERT( _max_depth > 0 );
   --_max_depth;
   fc::raw::unpack( s, static_cast<graphene::protocol::transaction&>(v), _max_depth );
   fc::raw::unpack( s, v.signatures, _max_depth );
   fc::raw::unpack( s, v.pq_signatures, _max_depth );
} FC_RETHROW_EXCEPTIONS( warn, "error unpacking signed_transaction" ) }

template<typename Stream>
void pack_processed_transaction_impl( Stream& s, const graphene::protocol::processed_transaction& v, uint32_t _max_depth )
{
   FC_ASSERT( _max_depth > 0 );
   --_max_depth;
   detail::pack_signed_transaction_impl( s, static_cast<const graphene::protocol::signed_transaction&>(v), _max_depth );
   fc::raw::pack( s, v.operation_results, _max_depth );
}

template<typename Stream>
void unpack_processed_transaction_impl( Stream& s, graphene::protocol::processed_transaction& v, uint32_t _max_depth )
{ try {
   FC_ASSERT( _max_depth > 0 );
   --_max_depth;
   detail::unpack_signed_transaction_impl( s, static_cast<graphene::protocol::signed_transaction&>(v), _max_depth );
   fc::raw::unpack( s, v.operation_results, _max_depth );
} FC_RETHROW_EXCEPTIONS( warn, "error unpacking processed_transaction" ) }

} // namespace detail

void pack( datastream<size_t>& s, const graphene::protocol::signed_transaction& v, uint32_t _max_depth )
   { detail::pack_signed_transaction_impl( s, v, _max_depth ); }
void pack( sha256::encoder& s, const graphene::protocol::signed_transaction& v, uint32_t _max_depth )
   { detail::pack_signed_transaction_impl( s, v, _max_depth ); }
void pack( datastream<char*>& s, const graphene::protocol::signed_transaction& v, uint32_t _max_depth )
   { detail::pack_signed_transaction_impl( s, v, _max_depth ); }
void unpack( datastream<const char*>& s, graphene::protocol::signed_transaction& v, uint32_t _max_depth )
   { detail::unpack_signed_transaction_impl( s, v, _max_depth ); }

void pack( datastream<size_t>& s, const graphene::protocol::precomputable_transaction& v, uint32_t _max_depth )
   { detail::pack_signed_transaction_impl( s, static_cast<const graphene::protocol::signed_transaction&>(v), _max_depth ); }
void pack( sha256::encoder& s, const graphene::protocol::precomputable_transaction& v, uint32_t _max_depth )
   { detail::pack_signed_transaction_impl( s, static_cast<const graphene::protocol::signed_transaction&>(v), _max_depth ); }
void pack( datastream<char*>& s, const graphene::protocol::precomputable_transaction& v, uint32_t _max_depth )
   { detail::pack_signed_transaction_impl( s, static_cast<const graphene::protocol::signed_transaction&>(v), _max_depth ); }
void unpack( datastream<const char*>& s, graphene::protocol::precomputable_transaction& v, uint32_t _max_depth )
   { detail::unpack_signed_transaction_impl( s, static_cast<graphene::protocol::signed_transaction&>(v), _max_depth ); }

void pack( datastream<size_t>& s, const graphene::protocol::processed_transaction& v, uint32_t _max_depth )
   { detail::pack_processed_transaction_impl( s, v, _max_depth ); }
void pack( sha256::encoder& s, const graphene::protocol::processed_transaction& v, uint32_t _max_depth )
   { detail::pack_processed_transaction_impl( s, v, _max_depth ); }
void pack( datastream<char*>& s, const graphene::protocol::processed_transaction& v, uint32_t _max_depth )
   { detail::pack_processed_transaction_impl( s, v, _max_depth ); }
void unpack( datastream<const char*>& s, graphene::protocol::processed_transaction& v, uint32_t _max_depth )
   { detail::unpack_processed_transaction_impl( s, v, _max_depth ); }

template std::vector<char> pack( const graphene::protocol::signed_transaction& v, uint32_t _max_depth );
template std::vector<char> pack( const graphene::protocol::precomputable_transaction& v, uint32_t _max_depth );
template std::vector<char> pack( const graphene::protocol::processed_transaction& v, uint32_t _max_depth );
template size_t pack_size( const graphene::protocol::signed_transaction& v );
template size_t pack_size( const graphene::protocol::precomputable_transaction& v );
template size_t pack_size( const graphene::protocol::processed_transaction& v );

} } // namespace fc::raw

GRAPHENE_IMPLEMENT_EXTERNAL_SERIALIZATION( graphene::protocol::transaction)
GRAPHENE_IMPLEMENT_EXTERNAL_SERIALIZATION( graphene::protocol::signed_transaction)
GRAPHENE_IMPLEMENT_EXTERNAL_SERIALIZATION( graphene::protocol::precomputable_transaction)
GRAPHENE_IMPLEMENT_EXTERNAL_SERIALIZATION( graphene::protocol::processed_transaction)
