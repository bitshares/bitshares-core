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
   database& d = db();
   FC_ASSERT( d.find_object(op.options.voting_account) );
   FC_ASSERT( is_relative(op.options.memo_key) || d.find_object(op.options.memo_key) );
   FC_ASSERT( fee_paying_account->is_lifetime_member() );
   FC_ASSERT( op.referrer(d).is_member(d.head_block_time()) );

   const auto& global_props = d.get_global_properties();
   const auto& chain_params = global_props.parameters;

   FC_ASSERT( op.owner.auths.size() <= chain_params.maximum_authority_membership );
   FC_ASSERT( op.active.auths.size() <= chain_params.maximum_authority_membership );
   check_relative_ids(op.owner);
   check_relative_ids(op.active);
   FC_ASSERT( d.find(key_id_type(get_relative_id(op.options.memo_key))) );
   for( auto id : op.owner.auths )
   {
      FC_ASSERT( is_relative(id.first) || d.find_object(id.first) );
   }
   for( auto id : op.active.auths )
   {
      FC_ASSERT( is_relative(id.first) || d.find_object(id.first) );
   }

   uint32_t max_vote_id = global_props.next_available_vote_id;
   FC_ASSERT( op.options.num_witness <= chain_params.maximum_witness_count );
   FC_ASSERT( op.options.num_committee <= chain_params.maximum_committee_count );
   safe<uint32_t> counts[vote_id_type::VOTE_TYPE_COUNT];
   for( auto id : op.options.votes )
   {
      FC_ASSERT( id < max_vote_id );
      counts[id.type()]++;
   }
   FC_ASSERT(counts[vote_id_type::witness] <= op.options.num_witness,
             "",
             ("count", counts[vote_id_type::witness])("num", op.options.num_witness));
   FC_ASSERT(counts[vote_id_type::committee] <= op.options.num_committee,
             "",
             ("count", counts[vote_id_type::committee])("num", op.options.num_committee));

   auto& acnt_indx = d.get_index_type<account_index>();
   if( op.name.size() )
   {
      auto current_account_itr = acnt_indx.indices().get<by_name>().find( op.name );
      FC_ASSERT( current_account_itr == acnt_indx.indices().get<by_name>().end() );
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
   const auto& stats_obj = db().create<account_statistics_object>( [&]( account_statistics_object& ){
   });

   const auto& new_acnt_object = db().create<account_object>( [&]( account_object& obj ){
         obj.registrar = o.registrar;
         obj.referrer = o.referrer;
         obj.lifetime_referrer = o.referrer(db()).lifetime_referrer;

         auto& params = db().get_global_properties().parameters;
         obj.network_fee_percentage = params.network_percent_of_fee;
         obj.lifetime_referrer_fee_percentage = params.lifetime_referrer_percent_of_fee;
         obj.referrer_rewards_percentage = o.referrer_percent;

         obj.name             = o.name;
         obj.owner            = resolve_relative_ids(o.owner);
         obj.active           = resolve_relative_ids(o.active);
         obj.statistics       = stats_obj.id;
         obj.options          = o.options;
         obj.options.memo_key = get_relative_id(obj.options.memo_key);
   });

   return new_acnt_object.id;
} FC_CAPTURE_AND_RETHROW((o)) }


void_result account_update_evaluator::do_evaluate( const account_update_operation& o )
{
   database& d = db();

   const auto& chain_params = db().get_global_properties().parameters;

   if( o.owner )
   {
      FC_ASSERT( o.owner->auths.size() <= chain_params.maximum_authority_membership );
      check_relative_ids(*o.owner);
      for( auto id : o.owner->auths )
      {
         FC_ASSERT( is_relative(id.first) || db().find<object>(id.first) );
      }
   }
   if( o.active )
   {
      FC_ASSERT( o.active->auths.size() <= chain_params.maximum_authority_membership );
      check_relative_ids(*o.active);
      for( auto id : o.active->auths )
      {
         FC_ASSERT( is_relative(id.first) || db().find<object>(id.first) );
      }
   }

   acnt = &o.account(d);

   if( o.new_options )
   {
      FC_ASSERT( d.find(key_id_type(get_relative_id(o.new_options->memo_key))) );
      FC_ASSERT( o.new_options->num_witness <= chain_params.maximum_witness_count );
      FC_ASSERT( o.new_options->num_committee <= chain_params.maximum_committee_count );
      uint32_t max_vote_id = d.get_global_properties().next_available_vote_id;
      for( auto id : o.new_options->votes )
      {
         FC_ASSERT( id < max_vote_id );
      }
   }

   return void_result();
}
void_result account_update_evaluator::do_apply( const account_update_operation& o )
{
   db().modify( *acnt, [&](account_object& a){
      if( o.owner ) a.owner = resolve_relative_ids(*o.owner);
      if( o.active ) a.active = resolve_relative_ids(*o.active);
      if( o.new_options )
      {
         a.options = *o.new_options;
         a.options.memo_key = get_relative_id(a.options.memo_key);
      }
   });
   return void_result();
}

void_result account_whitelist_evaluator::do_evaluate(const account_whitelist_operation& o)
{ try {
   database& d = db();

   listed_account = &o.account_to_list(d);
   if( !d.get_global_properties().parameters.allow_non_member_whitelists )
      FC_ASSERT(o.authorizing_account(d).is_lifetime_member());

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

void_result account_upgrade_evaluator::do_evaluate(const account_upgrade_evaluator::operation_type& o)
{
   database& d = db();

   account = &d.get(o.account_to_upgrade);
   FC_ASSERT(!account->is_lifetime_member());

   return {};
}

void_result account_upgrade_evaluator::do_apply(const account_upgrade_evaluator::operation_type& o)
{
   database& d = db();

   d.modify(*account, [&](account_object& a) {
      if( o.upgrade_to_lifetime_member )
      {
         // Upgrade to lifetime member. I don't care what the account was before.
         a.statistics(d).process_fees(a, d);
         a.membership_expiration_date = time_point_sec::maximum();
         a.referrer = a.registrar = a.lifetime_referrer = a.get_id();
         a.lifetime_referrer_fee_percentage = GRAPHENE_100_PERCENT - a.network_fee_percentage;
      } else if( a.is_annual_member(d.head_block_time()) ) {
         // Renew an annual subscription that's still in effect.
         FC_ASSERT(a.membership_expiration_date - d.head_block_time() < fc::days(3650),
                   "May not extend annual membership more than a decade into the future.");
         a.membership_expiration_date += fc::days(365);
      } else {
         // Upgrade from basic account.
         a.statistics(d).process_fees(a, d);
         assert(a.is_basic_account(d.head_block_time()));
         a.membership_expiration_date = d.head_block_time() + fc::days(365);
      }
   });

   return {};
}

} } // graphene::chain
