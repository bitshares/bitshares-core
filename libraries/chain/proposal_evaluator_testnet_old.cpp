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
#include <graphene/chain/database.hpp>
#include <graphene/chain/account_object.hpp>
#include <graphene/chain/proposal_object.hpp>

/** This file is mostly a copy from proposal_object.cpp and protocol/transaction.cpp
 *  It reverts this harmless little change:
 *  https://github.com/bitshares/bitshares-core/commit/23b8843d2f4e23280e3a5e1db480297758c8c872
 */
namespace graphene { namespace chain {

struct _testnet_old_sign_state
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

      optional<map<address,public_key_type>> available_address_sigs;
      optional<map<address,public_key_type>> provided_address_sigs;

      bool signed_by( const address& a ) {
         if( !available_address_sigs ) {
            available_address_sigs = std::map<address,public_key_type>();
            provided_address_sigs = std::map<address,public_key_type>();
            for( auto& item : available_keys ) {
             (*available_address_sigs)[ address(pts_address(item, false, 56) ) ] = item;
             (*available_address_sigs)[ address(pts_address(item, true, 56) ) ] = item;
             (*available_address_sigs)[ address(pts_address(item, false, 0) ) ] = item;
             (*available_address_sigs)[ address(pts_address(item, true, 0) ) ] = item;
             (*available_address_sigs)[ address(item) ] = item;
            }
            for( auto& item : provided_signatures ) {
             (*provided_address_sigs)[ address(pts_address(item.first, false, 56) ) ] = item.first;
             (*provided_address_sigs)[ address(pts_address(item.first, true, 56) ) ] = item.first;
             (*provided_address_sigs)[ address(pts_address(item.first, false, 0) ) ] = item.first;
             (*provided_address_sigs)[ address(pts_address(item.first, true, 0) ) ] = item.first;
             (*provided_address_sigs)[ address(item.first) ] = item.first;
            }
         }
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
         }
         return provided_signatures[itr->second] = true;
      }

      bool check_authority( account_id_type id )
      {
         if( approved_by.find(id) != approved_by.end() ) return true;
         return check_authority( get_active(id) );
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
         for( const auto& sig : provided_signatures )
            if( !sig.second ) remove_sigs.push_back( sig.first );

         for( auto& sig : remove_sigs )
            provided_signatures.erase(sig);

         return remove_sigs.size() != 0;
      }

      _testnet_old_sign_state( const flat_set<public_key_type>& sigs,
                  const std::function<const authority*(account_id_type)>& a,
                  const flat_set<public_key_type>& keys = flat_set<public_key_type>() )
      :get_active(a),available_keys(keys)
      {
         for( const auto& key : sigs )
            provided_signatures[ key ] = false;
         approved_by.insert( GRAPHENE_TEMP_ACCOUNT  );
      }

      const std::function<const authority*(account_id_type)>& get_active;
      const flat_set<public_key_type>&                        available_keys;

      flat_map<public_key_type,bool>   provided_signatures;
      flat_set<account_id_type>        approved_by;
      uint32_t                         max_recursion = GRAPHENE_MAX_SIG_CHECK_DEPTH;
};


void _testnet_old_verify_authority( const vector<operation>& ops, const flat_set<public_key_type>& sigs,
                       const std::function<const authority*(account_id_type)>& get_active,
                       const std::function<const authority*(account_id_type)>& get_owner,
                       uint32_t max_recursion_depth,
                       bool  allow_committe,
                       const flat_set<account_id_type>& active_aprovals,
                       const flat_set<account_id_type>& owner_approvals )
{ try {
   flat_set<account_id_type> required_active;
   flat_set<account_id_type> required_owner;
   vector<authority> other;

   for( const auto& op : ops )
      operation_get_required_authorities( op, required_active, required_owner, other );

   if( !allow_committe )
      GRAPHENE_ASSERT( required_active.find(GRAPHENE_COMMITTEE_ACCOUNT) == required_active.end(),
                       invalid_committee_approval, "Committee account may only propose transactions" );

   _testnet_old_sign_state s(sigs,get_active);
   s.max_recursion = max_recursion_depth;
   for( auto& id : active_aprovals )
      s.approved_by.insert( id );
   for( auto& id : owner_approvals )
      s.approved_by.insert( id );

   for( const auto& auth : other )
   {
      GRAPHENE_ASSERT( s.check_authority(&auth), tx_missing_other_auth, "Missing Authority", ("auth",auth)("sigs",sigs) );
   }

   // fetch all of the top level authorities
   for( auto id : required_active )
   {
      GRAPHENE_ASSERT( s.check_authority(id) ||
                       s.check_authority(get_owner(id)),
                       tx_missing_active_auth, "Missing Active Authority ${id}", ("id",id)("auth",*get_active(id))("owner",*get_owner(id)) );
   }

   for( auto id : required_owner )
   {
      GRAPHENE_ASSERT( owner_approvals.find(id) != owner_approvals.end() ||
                       s.check_authority(get_owner(id)),
                       tx_missing_owner_auth, "Missing Owner Authority ${id}", ("id",id)("auth",*get_owner(id)) );
   }

   GRAPHENE_ASSERT(
      !s.remove_unused_signatures(),
      tx_irrelevant_sig,
      "Unnecessary signature(s) detected"
      );
} FC_CAPTURE_AND_RETHROW( (ops)(sigs) ) }

bool _testnet_old_is_authorized(const proposal_object& proposal, database& db)
{
   transaction_evaluation_state dry_run_eval(&db);

   try {
      _testnet_old_verify_authority( proposal.proposed_transaction.operations,
                        proposal.available_key_approvals,
                        [&]( account_id_type id ){ return &id(db).active; },
                        [&]( account_id_type id ){ return &id(db).owner;  },
                        db.get_global_properties().parameters.max_authority_depth,
                        true, /* allow committeee */
                        proposal.available_active_approvals,
                        proposal.available_owner_approvals );
   }
   catch ( const fc::exception& e )
   {
      //idump((available_active_approvals));
      //wlog((e.to_detail_string()));
      return false;
   }
   return true;
}

} } // graphene::chain
