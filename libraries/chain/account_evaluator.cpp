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
#include <graphene/chain/database.hpp>
#include <graphene/chain/account_evaluator.hpp>
#include <graphene/chain/key_object.hpp>
#include <algorithm>

namespace graphene { namespace chain {

void_result account_create_evaluator::do_evaluate( const account_create_operation& op )
{ try {
   FC_ASSERT( db().find_object(op.voting_account) );
   FC_ASSERT( is_relative(op.memo_key) || db().find_object(op.memo_key) );

   if( fee_paying_account->is_prime() )
   {
      FC_ASSERT( op.referrer(db()).is_prime() );
   }
   else
   {
      FC_ASSERT( op.referrer == fee_paying_account->referrer );
      FC_ASSERT( op.referrer_percent == fee_paying_account->referrer_percent, "",
                 ("op",op)
                 ("fee_paying_account->referral_percent",fee_paying_account->referrer_percent) );
   }

   const auto& global_props = db().get_global_properties();
   uint32_t max_vote_id = global_props.next_available_vote_id;
   const auto& chain_params = global_props.parameters;
   FC_ASSERT( op.num_witness <= chain_params.maximum_witness_count );
   FC_ASSERT( op.num_committee <= chain_params.maximum_committee_count );
   FC_ASSERT( op.owner.auths.size() <= chain_params.maximum_authority_membership );
   FC_ASSERT( op.active.auths.size() <= chain_params.maximum_authority_membership );
   for( auto id : op.owner.auths )
   {
      FC_ASSERT( is_relative(id.first) || db().find<object>(id.first) );
   }
   for( auto id : op.active.auths )
   {
      FC_ASSERT( is_relative(id.first) || db().find<object>(id.first) );
   }
   safe<uint32_t> counts[vote_id_type::VOTE_TYPE_COUNT];
   for( auto id : op.vote )
   {
      FC_ASSERT( id < max_vote_id );
      counts[id.type()]++;
   }
   FC_ASSERT(counts[vote_id_type::witness] <= op.num_witness,
             "",
             ("count", counts[vote_id_type::witness])("num", op.num_witness));
   FC_ASSERT(counts[vote_id_type::committee] <= op.num_committee,
             "",
             ("count", counts[vote_id_type::committee])("num", op.num_committee));

   auto& acnt_indx = db().get_index_type<account_index>();
   if( op.name.size() )
   {
      auto current_account_itr = acnt_indx.indices().get<by_name>().find( op.name );
      FC_ASSERT( current_account_itr == acnt_indx.indices().get<by_name>().end() );
   }

   // TODO: this check can be removed after GRAPHENE_LEGACY_NAME_IMPORT_PERIOD
   // legacy account check
   if( db().get_dynamic_global_properties().head_block_number < GRAPHENE_LEGACY_NAME_IMPORT_PERIOD )
   {
      auto legacy_account_itr = acnt_indx.indices().get<by_name>().find( "bts-"+op.name );
      if( legacy_account_itr != acnt_indx.indices().get<by_name>().end() )
      {
         FC_ASSERT( fee_paying_account->id == legacy_account_itr->id );
      }
   }

   // verify child account authority
   auto pos = op.name.find( '/' );
   if( pos != string::npos )
   {
      // TODO: lookup account by op.owner.auths[0] and verify the name
      // this should be a constant time lookup rather than log(N)
      auto parent_account_itr = acnt_indx.indices().get<by_name>().find( op.name.substr(0,pos) );
      FC_ASSERT( parent_account_itr != acnt_indx.indices().get<by_name>().end() );
      FC_ASSERT( verify_authority( *parent_account_itr, authority::owner ) );
      FC_ASSERT( op.owner.auths.find( parent_account_itr->id ) != op.owner.auths.end() );
   }

   return void_result();
} FC_CAPTURE_AND_RETHROW( (op) ) }

object_id_type account_create_evaluator::do_apply( const account_create_operation& o )
{ try {
   auto owner  = resolve_relative_ids( o.owner );
   auto active = resolve_relative_ids( o.active );

   const auto& stats_obj = db().create<account_statistics_object>( [&]( account_statistics_object& ){
   });

   const auto& new_acnt_object = db().create<account_object>( [&]( account_object& obj ){
         if( fee_paying_account->is_prime() )
         {
            obj.registrar        = o.registrar;
            obj.referrer         = o.referrer;
            obj.referrer_percent = o.referrer_percent;
         }
         else
         {
            obj.registrar         = fee_paying_account->registrar;
            obj.referrer          = fee_paying_account->referrer;
            obj.referrer_percent  = fee_paying_account->referrer_percent;
         }
         obj.name             = o.name;
         obj.owner            = owner;
         obj.active           = active;
         obj.statistics       = stats_obj.id;
         obj.memo_key         = get_relative_id(o.memo_key);
         obj.voting_account   = o.voting_account;
         obj.votes            = o.vote;
         obj.num_witness      = o.num_witness;
         obj.num_committee    = o.num_committee;
   });

   return new_acnt_object.id;
} FC_CAPTURE_AND_RETHROW((o)) }


void_result account_update_evaluator::do_evaluate( const account_update_operation& o )
{
   database&   d = db();

   FC_ASSERT( !o.memo_key || is_relative(*o.memo_key) || db().find_object(*o.memo_key) );

   const auto& chain_params = db().get_global_properties().parameters;
   FC_ASSERT( o.num_witness <= chain_params.maximum_witness_count );
   FC_ASSERT( o.num_committee <= chain_params.maximum_committee_count );
   if( o.owner )
   {
      FC_ASSERT( o.owner->auths.size() <= chain_params.maximum_authority_membership );
      for( auto id : o.owner->auths )
      {
         FC_ASSERT( is_relative(id.first) || db().find<object>(id.first) );
      }
   }
   if( o.active )
   {
      FC_ASSERT( o.active->auths.size() <= chain_params.maximum_authority_membership );
      for( auto id : o.active->auths )
      {
         FC_ASSERT( is_relative(id.first) || db().find<object>(id.first) );
      }
   }

   acnt = &o.account(d);
   if( o.upgrade_to_prime ) FC_ASSERT( !acnt->is_prime() );

   if( o.vote )
   {
      uint32_t max_vote_id = d.get_global_properties().next_available_vote_id;
      for( auto id : *o.vote )
      {
         FC_ASSERT( id < max_vote_id );
      }
   }

   return void_result();
}
void_result account_update_evaluator::do_apply( const account_update_operation& o )
{
   db().modify( *acnt, [&]( account_object& a  ){
          if( o.owner ) a.owner = *o.owner;
          if( o.active ) a.active = *o.active;
          if( o.voting_account ) a.voting_account = *o.voting_account;
          if( o.memo_key ) a.memo_key = *o.memo_key;
          if( o.vote ) a.votes = *o.vote;
          if( o.upgrade_to_prime )
          {
            a.referrer_percent = 100;
            a.referrer = a.id;
          }
          a.num_witness = o.num_witness;
          a.num_committee = o.num_committee;
      });
   return void_result();
}

void_result account_whitelist_evaluator::do_evaluate(const account_whitelist_operation& o)
{ try {
   database& d = db();

   listed_account = &o.account_to_list(d);
   if( !d.get_global_properties().parameters.allow_non_prime_whitelists )
      FC_ASSERT(listed_account->is_prime());

   return void_result();
} FC_CAPTURE_AND_RETHROW( (o) ) }

void_result account_whitelist_evaluator::do_apply(const account_whitelist_operation& o)
{
   database& d = db();

   d.modify(*listed_account, [&o](account_object& a) {
      if( o.new_listing & o.white_listed )
         a.whitelisting_accounts.insert(o.authorizing_account);
      else
         a.whitelisting_accounts.erase(o.authorizing_account);
      if( o.new_listing & o.black_listed )
         a.blacklisting_accounts.insert(o.authorizing_account);
      else
         a.blacklisting_accounts.erase(o.authorizing_account);
   });

   return void_result();
}

} } // graphene::chain
