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
#include <graphene/chain/transaction_evaluation_state.hpp>
#include <graphene/chain/account_object.hpp>
#include <graphene/chain/asset_object.hpp>
#include <graphene/chain/delegate_object.hpp>
#include <graphene/chain/database.hpp>
#include <graphene/chain/exceptions.hpp>

namespace graphene { namespace chain {
   bool transaction_evaluation_state::check_authority( const account_object& account, authority::classification auth_class, int depth )
   {
      if( (!_is_proposed_trx) && (_db->get_node_properties().skip_flags & database::skip_authority_check)  )
         return true;
      if( (!_is_proposed_trx) && (_db->get_node_properties().skip_flags & database::skip_transaction_signatures)  )
         return true;
      if( account.get_id() == GRAPHENE_TEMP_ACCOUNT ||
          approved_by.find(make_pair(account.id, auth_class)) != approved_by.end() )
         return true;

      FC_ASSERT( account.id.instance() != 0 || _is_proposed_trx, "", ("account",account)("is_proposed",_is_proposed_trx) );

      bool valid = false;
      switch( auth_class )
      {
         case authority::owner:
            valid = check_authority( account.owner, auth_class, depth );
            break;
         case authority::active:
            valid = check_authority( account.active, auth_class, depth );
            break;
         default:
            FC_ASSERT( false, "Invalid Account Auth Class" );
      };
      if( valid )
         approved_by.insert( std::make_pair(account.id, auth_class) );
      return valid;
   }

   bool transaction_evaluation_state::check_authority( const authority& au, authority::classification auth_class, int depth )
   { try {

      if( (!_is_proposed_trx) && (_db->get_node_properties().skip_flags & database::skip_authority_check)  )
         return true;
      if( (!_is_proposed_trx) && (_db->get_node_properties().skip_flags & database::skip_transaction_signatures)  )
         return true;

      uint32_t total_weight = 0;
      for( const auto& auth : au.account_auths )
      {
         if( approved_by.find( std::make_pair(auth.first,auth_class) ) != approved_by.end() )
            total_weight += auth.second;
         else
         {
            if( depth == GRAPHENE_MAX_SIG_CHECK_DEPTH )
            {
               //elog("Failing authority verification due to recursion depth.");
               return false;
            }
            const account_object& acnt = auth.first(*_db);
            if( check_authority( acnt, auth_class, depth + 1 ) )
            {
               approved_by.insert( std::make_pair(acnt.id,auth_class) );
               total_weight += auth.second;
            }
         }
      }
      for( const auto& key : au.key_auths )
      {
         if( signed_by( key.first ) )
            total_weight += key.second;
      }
      for( const auto& key : au.address_auths )
      {
         if( signed_by( key.first ) )
            total_weight += key.second;
      }

      return total_weight >= au.weight_threshold;
   } FC_CAPTURE_AND_RETHROW( (au)(auth_class)(depth) ) }

   bool transaction_evaluation_state::signed_by(const public_key_type& k)
   {
      auto itr = _sigs.find(k);
      return itr != _sigs.end() && (itr->second = true);
   }

   bool transaction_evaluation_state::signed_by(const address& k)
   {
      for( auto itr = _sigs.begin(); itr != _sigs.end(); ++itr )
         if( itr->first == k ) return itr->second = true;
      return false;
   }

} } // namespace graphene::chain
