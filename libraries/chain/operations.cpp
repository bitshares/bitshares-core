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
#include <graphene/chain/operations.hpp>
#include <graphene/chain/predicate.hpp>
#include <fc/crypto/aes.hpp>

namespace graphene { namespace chain {

/**
 *  Valid symbols can contain [A, Z], and '.'
 *  They must start with [A, Z]
 *  They must end with [A, Z]
 *  They can contain a maximum of one '.'
 */
bool is_valid_symbol( const string& symbol )
{
    if( symbol.size() < GRAPHENE_MIN_ASSET_SYMBOL_LENGTH )
        return false;

    if( symbol.size() > GRAPHENE_MAX_ASSET_SYMBOL_LENGTH )
        return false;

    if( !isalpha( symbol.front() ) )
        return false;

    if( !isalpha( symbol.back() ) )
        return false;

    bool dot_already_present = false;
    for( const auto c : symbol )
    {
        if( isalpha( c ) && isupper( c ) )
            continue;

        if( c == '.' )
        {
            if( dot_already_present )
                return false;

            dot_already_present = true;
            continue;
        }

        return false;
    }

    return true;
}

/**
 *  Valid names can contain [a, z], [0, 9], '.', and '-'
 *  They must start with [a, z]
 *  They must end with [a, z] or [0, 9]
 *  '.' must be followed by [a, z]
 *  '-' must be followed by [a, z] or [0, 9]
 */
bool is_valid_name( const string& name )
{
    if( name.size() < GRAPHENE_MIN_ACCOUNT_NAME_LENGTH )
        return false;

    if( name.size() > GRAPHENE_MAX_ACCOUNT_NAME_LENGTH )
        return false;

    if( !isalpha( name.front() ) )
        return false;

    if( !isalpha( name.back() ) && !isdigit( name.back() ) )
        return false;

    for( size_t i = 0; i < name.size(); ++i )
    {
        const auto c = name.at( i );

        if( isalpha( c ) && islower( c ) )
            continue;

        if( isdigit( c ) )
            continue;

        if( c == '.' )
        {
            const auto next = name.at( i + 1 );
            if( !isalpha( next ) )
                return false;

            continue;
        }

        if( c == '-' )
        {
            const auto next = name.at( i + 1 );
            if( !isalpha( next ) && !isdigit( next ) )
                return false;

            continue;
        }

        return false;
    }

    return true;
}

bool is_cheap_name( const string& n )
{
   bool v = false;
   for( auto c : n )
   {
      if( c >= '0' && c <= '9' ) return true;
      if( c == '.' || c == '-' || c == '/' ) return true;
      switch( c )
      {
         case 'a':
         case 'e':
         case 'i':
         case 'o':
         case 'u':
         case 'y':
            v = true;
      }
   }
   if( !v )
      return true;
   return false;
}

share_type account_create_operation::calculate_fee( const fee_schedule_type& schedule )const
{
   auto core_fee_required = schedule.account_create_fee;

   uint32_t s = name.size();
   if( is_cheap_name(name) )
      s = 63;

   FC_ASSERT( s >= 2 );

   if( s >= 8 && s < 63 )
     core_fee_required = schedule.account_len8up_fee;
   else if( s == 7 )
     core_fee_required = schedule.account_len7_fee;
   else if( s == 6 )
     core_fee_required = schedule.account_len6_fee;
   else if( s == 5 )
     core_fee_required = schedule.account_len5_fee;
   else if( s == 4 )
     core_fee_required = schedule.account_len4_fee;
   else if( s == 3 )
     core_fee_required = schedule.account_len3_fee;
   else if( s == 2 )
      core_fee_required = schedule.account_len2_fee;

   // Authorities and vote lists can be arbitrarily large, so charge a data fee for big ones
   core_fee_required += schedule.total_data_fee(active, owner, options.votes);

   return core_fee_required;
}
share_type account_update_operation::calculate_fee( const fee_schedule_type& schedule )const
{
   auto core_fee_required = schedule.account_update_fee + schedule.total_data_fee(owner, active);
   if( new_options )
      core_fee_required += schedule.total_data_fee(new_options->votes);
   return core_fee_required;
}
void account_update_operation::get_required_auth(flat_set<account_id_type>& active_auth_set,
                                                 flat_set<account_id_type>& owner_auth_set) const
{
   if( owner || active )
      owner_auth_set.insert(account);
   else
      active_auth_set.insert(account);
}

void account_update_operation::validate()const
{
   FC_ASSERT( fee.amount >= 0 );
   FC_ASSERT( account != account_id_type() );
   FC_ASSERT( owner || active || new_options );

   if( new_options )
      new_options->validate();
}

share_type asset_create_operation::calculate_fee(const fee_schedule_type& schedule)const
{
   auto core_fee_required = schedule.asset_create_fee;

   switch(symbol.size()) {
   case 3: core_fee_required += schedule.asset_len3_fee;
       break;
   case 4: core_fee_required += schedule.asset_len4_fee;
       break;
   case 5: core_fee_required += schedule.asset_len5_fee;
       break;
   case 6: core_fee_required += schedule.asset_len6_fee;
       break;
   default: core_fee_required += schedule.asset_len7up_fee;
   }

   // common_options contains several lists and a string. Charge fees for its size
   core_fee_required += schedule.total_data_fee(common_options);

   return core_fee_required;
}

share_type transfer_operation::calculate_fee( const fee_schedule_type& schedule )const
{
   share_type core_fee_required = schedule.transfer_fee;
   if( memo )
      core_fee_required += schedule.total_data_fee(memo->message);
   return core_fee_required;
}

struct key_data_validate
{
   typedef void result_type;
   void operator()( const address& a )const { FC_ASSERT( a != address() ); }
   void operator()( const public_key_type& a )const { FC_ASSERT( a != public_key_type() ); }
};
void key_create_operation::get_required_auth(flat_set<account_id_type>& active_auth_set,
                                             flat_set<account_id_type>&) const
{
   active_auth_set.insert(fee_paying_account);
}

void key_create_operation::validate()const
{
   FC_ASSERT( fee.amount >= 0 );
   key_data.visit( key_data_validate() );
}

void account_create_operation::get_required_auth(flat_set<account_id_type>& active_auth_set,
                                                 flat_set<account_id_type>&) const
{
   active_auth_set.insert(registrar);
}

void account_create_operation::validate()const
{
   FC_ASSERT( fee.amount >= 0 );
   FC_ASSERT( is_valid_name( name ) );
   FC_ASSERT( referrer_percent <= GRAPHENE_100_PERCENT );
   FC_ASSERT( !owner.auths.empty() );
   auto pos = name.find( '/' );
   if( pos != string::npos )
   {
      FC_ASSERT( owner.weight_threshold == 1 );
      FC_ASSERT( owner.auths.size() == 1 );
   }
   options.validate();
}


share_type asset_publish_feed_operation::calculate_fee( const fee_schedule_type& schedule )const
{
   return schedule.publish_feed_fee;
}

void asset_publish_feed_operation::validate()const
{
   FC_ASSERT( fee.amount >= 0 );
   feed.validate();
}

void transfer_operation::get_required_auth(flat_set<account_id_type>& active_auth_set,
                                           flat_set<account_id_type>&) const
{
   active_auth_set.insert( from );
}

void transfer_operation::validate()const
{
   FC_ASSERT( fee.amount >= 0 );
   FC_ASSERT( from != to );
   FC_ASSERT( amount.amount > 0 );
}

void asset_create_operation::get_required_auth(flat_set<account_id_type>& active_auth_set, flat_set<account_id_type>&) const
{
   active_auth_set.insert(issuer);
}

void  asset_create_operation::validate()const
{
   FC_ASSERT( fee.amount >= 0 );
   FC_ASSERT( is_valid_symbol(symbol) );
   common_options.validate();
   if( common_options.issuer_permissions & (disable_force_settle|global_settle) )
      FC_ASSERT( bitasset_options.valid() );
   if( is_prediction_market )
   {
      FC_ASSERT( bitasset_options.valid(), "Cannot have a User-Issued Asset implement a prediction market." );
      FC_ASSERT( common_options.issuer_permissions & global_settle );
   }
   if( bitasset_options ) bitasset_options->validate();

   asset dummy = asset(1) * common_options.core_exchange_rate;
   FC_ASSERT(dummy.asset_id == asset_id_type(1));
   FC_ASSERT(precision <= 12);
}

asset_update_operation::asset_update_operation(const asset_object& old)
{
   issuer = old.issuer;
   asset_to_update = old.get_id();
   new_options = old.options;
}

void asset_update_operation::get_required_auth(flat_set<account_id_type>& active_auth_set, flat_set<account_id_type>&) const
{
   active_auth_set.insert(issuer);
}

void asset_update_operation::validate()const
{
   FC_ASSERT( fee.amount >= 0 );
   if( new_issuer )
      FC_ASSERT(issuer != *new_issuer);
   new_options.validate();

   asset dummy = asset(1, asset_to_update) * new_options.core_exchange_rate;
   FC_ASSERT(dummy.asset_id == asset_id_type());
}

share_type asset_update_operation::calculate_fee(const fee_schedule_type& k)const
{
   return k.asset_update_fee + k.total_data_fee(new_options);
}

void asset_reserve_operation::get_required_auth(flat_set<account_id_type>& active_auth_set, flat_set<account_id_type>&) const
{
   active_auth_set.insert(payer);
}

void asset_reserve_operation::validate()const
{
   FC_ASSERT( fee.amount >= 0 );
   FC_ASSERT( amount_to_reserve.amount.value <= GRAPHENE_MAX_SHARE_SUPPLY );
   FC_ASSERT( amount_to_reserve.amount.value > 0 );
}

share_type asset_reserve_operation::calculate_fee(const fee_schedule_type& k)const
{
   return k.asset_reserve_fee;
}

void asset_issue_operation::get_required_auth(flat_set<account_id_type>& active_auth_set, flat_set<account_id_type>&) const
{
   active_auth_set.insert(issuer);
}

void asset_issue_operation::validate()const
{
   FC_ASSERT( fee.amount >= 0 );
   FC_ASSERT( asset_to_issue.amount.value <= GRAPHENE_MAX_SHARE_SUPPLY );
   FC_ASSERT( asset_to_issue.amount.value > 0 );
   FC_ASSERT( asset_to_issue.asset_id != 0 );
}

share_type asset_issue_operation::calculate_fee( const fee_schedule_type& k )const
{
   return k.asset_issue_fee;
}

share_type delegate_create_operation::calculate_fee( const fee_schedule_type& k )const
{
   return k.delegate_create_fee + k.total_data_fee(url);
}

void delegate_create_operation::get_required_auth(flat_set<account_id_type>& active_auth_set, flat_set<account_id_type>&) const
{
   active_auth_set.insert(delegate_account);
}

void delegate_create_operation::validate()const
{
   FC_ASSERT( fee.amount >= 0 );
   FC_ASSERT(url.size() < GRAPHENE_MAX_URL_LENGTH );
}

void asset_fund_fee_pool_operation::get_required_auth(flat_set<account_id_type>& active_auth_set, flat_set<account_id_type>&) const
{
   active_auth_set.insert(from_account);
}

void asset_fund_fee_pool_operation::validate() const
{
   FC_ASSERT( fee.amount >= 0 );
   FC_ASSERT( fee.asset_id == asset_id_type() );
   FC_ASSERT( amount > 0 );
}

share_type asset_fund_fee_pool_operation::calculate_fee(const fee_schedule_type& k) const
{
   return k.asset_fund_fee_pool_fee;
}

void limit_order_create_operation::get_required_auth(flat_set<account_id_type>& active_auth_set, flat_set<account_id_type>&) const
{
   active_auth_set.insert(seller);
}

void limit_order_create_operation::validate()const
{
   FC_ASSERT( amount_to_sell.asset_id != min_to_receive.asset_id );
   FC_ASSERT( fee.amount >= 0 );
   FC_ASSERT( amount_to_sell.amount > 0 );
   FC_ASSERT( min_to_receive.amount > 0 );
}

share_type limit_order_create_operation::calculate_fee(const fee_schedule_type& k) const
{
   return k.limit_order_create_fee;
}

void limit_order_cancel_operation::get_required_auth(flat_set<account_id_type>& active_auth_set, flat_set<account_id_type>&) const
{
   active_auth_set.insert(fee_paying_account);
}

void limit_order_cancel_operation::validate()const
{
   FC_ASSERT( fee.amount >= 0 );
}

share_type limit_order_cancel_operation::calculate_fee(const fee_schedule_type& k) const
{
   return k.limit_order_create_fee;
}

void call_order_update_operation::get_required_auth(flat_set<account_id_type>& active_auth_set, flat_set<account_id_type>&) const
{
   active_auth_set.insert(funding_account);
}

void call_order_update_operation::validate()const
{ try {
   FC_ASSERT( fee.amount >= 0 );
   FC_ASSERT( delta_collateral.asset_id != delta_debt.asset_id );
   FC_ASSERT( delta_collateral.amount != 0 || delta_debt.amount != 0 );
} FC_CAPTURE_AND_RETHROW((*this)) }

share_type call_order_update_operation::calculate_fee(const fee_schedule_type& k) const
{
   return k.call_order_fee;
}

proposal_create_operation proposal_create_operation::genesis_proposal(const database& db)
{
   auto global_params = db.get_global_properties().parameters;
   proposal_create_operation op = {account_id_type(), asset(), {},
                                   db.head_block_time() + global_params.maximum_proposal_lifetime,
                                   global_params.genesis_proposal_review_period};
   op.fee = op.calculate_fee(global_params.current_fees);
   return op;
}

void proposal_create_operation::get_required_auth(flat_set<account_id_type>& active_auth_set, flat_set<account_id_type>&) const
{
   active_auth_set.insert(fee_paying_account);
}

void proposal_create_operation::validate() const
{
   FC_ASSERT( !proposed_ops.empty() );
   for( const auto& op : proposed_ops ) op.validate();
}

share_type proposal_create_operation::calculate_fee(const fee_schedule_type& k) const
{
   return k.proposal_create_fee + k.total_data_fee(proposed_ops);
}

void proposal_update_operation::get_required_auth(flat_set<account_id_type>& active_auth_set,
                                                  flat_set<account_id_type>& owner_auth_set) const
{
   active_auth_set.insert(fee_paying_account);
   for( auto id : active_approvals_to_add )
      active_auth_set.insert(id);
   for( auto id : active_approvals_to_remove )
      active_auth_set.insert(id);
   for( auto id : owner_approvals_to_add )
      owner_auth_set.insert(id);
   for( auto id : owner_approvals_to_remove )
      owner_auth_set.insert(id);
}

void proposal_update_operation::validate() const
{
   FC_ASSERT(fee.amount >= 0);
   FC_ASSERT(!(active_approvals_to_add.empty() && active_approvals_to_remove.empty() &&
               owner_approvals_to_add.empty() && owner_approvals_to_remove.empty() &&
               key_approvals_to_add.empty() && key_approvals_to_remove.empty()));
   for( auto a : active_approvals_to_add )
   {
      FC_ASSERT(active_approvals_to_remove.find(a) == active_approvals_to_remove.end(),
                "Cannot add and remove approval at the same time.");
   }
   for( auto a : owner_approvals_to_add )
   {
      FC_ASSERT(owner_approvals_to_remove.find(a) == owner_approvals_to_remove.end(),
                "Cannot add and remove approval at the same time.");
   }
   for( auto a : key_approvals_to_add )
   {
      FC_ASSERT(key_approvals_to_remove.find(a) == key_approvals_to_remove.end(),
                "Cannot add and remove approval at the same time.");
   }
}

share_type proposal_update_operation::calculate_fee(const fee_schedule_type& k) const
{
   return k.proposal_create_fee + k.total_data_fee(active_approvals_to_add,
                                                   active_approvals_to_remove,
                                                   owner_approvals_to_add,
                                                   owner_approvals_to_remove,
                                                   key_approvals_to_add,
                                                   key_approvals_to_remove);
}

void proposal_delete_operation::get_required_auth(flat_set<account_id_type>& active_auth_set,
                                                  flat_set<account_id_type>& owner_auth_set) const
{
    if( using_owner_authority )
        owner_auth_set.insert(fee_paying_account);
    else
        active_auth_set.insert(fee_paying_account);
}

void account_transfer_operation::validate()const
{
   FC_ASSERT( fee.amount >= 0 );
}

void account_transfer_operation::get_required_auth(flat_set<account_id_type>& active_auth_set, flat_set<account_id_type>&)const
{
   active_auth_set.insert( account_id );
}

share_type  account_transfer_operation::calculate_fee( const fee_schedule_type& k )const
{
   return k.account_transfer_fee;
}


void proposal_delete_operation::validate() const
{
   FC_ASSERT( fee.amount >= 0 );
}

void witness_withdraw_pay_operation::get_required_auth(flat_set<account_id_type>& active_auth_set, flat_set<account_id_type>&) const
{
   active_auth_set.insert(to_account);
}

void witness_withdraw_pay_operation::validate() const
{
   FC_ASSERT( fee.amount >= 0 );
   FC_ASSERT( amount >= 0 );
}

share_type witness_withdraw_pay_operation::calculate_fee(const fee_schedule_type& k) const
{
   return k.witness_withdraw_pay_fee;
}

void account_whitelist_operation::get_required_auth(flat_set<account_id_type>& active_auth_set, flat_set<account_id_type>&) const
{
   active_auth_set.insert(authorizing_account);
}

void global_parameters_update_operation::validate() const
{
   FC_ASSERT( fee.amount >= 0 );
   new_parameters.validate();
}

share_type global_parameters_update_operation::calculate_fee(const fee_schedule_type& k) const
{
   return k.global_parameters_update_fee;
}

void witness_create_operation::get_required_auth(flat_set<graphene::chain::account_id_type>& active_auth_set, flat_set<graphene::chain::account_id_type>&) const
{
   active_auth_set.insert(witness_account);
}

void witness_create_operation::validate() const
{
   FC_ASSERT(fee.amount >= 0);
   FC_ASSERT(url.size() < GRAPHENE_MAX_URL_LENGTH );
}

share_type witness_create_operation::calculate_fee(const fee_schedule_type& k) const
{
   return k.witness_create_fee + k.total_data_fee(url);
}

void withdraw_permission_update_operation::get_required_auth(flat_set<account_id_type>& active_auth_set, flat_set<account_id_type>&)const
{
   active_auth_set.insert( withdraw_from_account );
}

void withdraw_permission_update_operation::validate()const
{
   FC_ASSERT( withdrawal_limit.amount > 0 );
   FC_ASSERT( fee.amount >= 0 );
   FC_ASSERT( withdrawal_period_sec > 0 );
   FC_ASSERT( withdraw_from_account != authorized_account );
   FC_ASSERT( periods_until_expiration > 0 );
}

share_type withdraw_permission_update_operation::calculate_fee( const fee_schedule_type& schedule )const
{
   return schedule.withdraw_permission_update_fee;
}

void withdraw_permission_claim_operation::get_required_auth(flat_set<account_id_type>& active_auth_set, flat_set<account_id_type>&)const
{
   active_auth_set.insert( withdraw_to_account );
}

void withdraw_permission_claim_operation::validate()const
{
   FC_ASSERT( withdraw_to_account != withdraw_from_account );
   FC_ASSERT( amount_to_withdraw.amount > 0 );
   FC_ASSERT( fee.amount >= 0 );
}

share_type withdraw_permission_claim_operation::calculate_fee(const fee_schedule_type& k)const
{
   share_type core_fee_required = k.withdraw_permission_claim_fee;
   if( memo )
      core_fee_required += k.total_data_fee(memo->message);
   return core_fee_required;
}

void withdraw_permission_delete_operation::get_required_auth(flat_set<account_id_type>& active_auth_set, flat_set<account_id_type>&) const
{
   active_auth_set.insert(withdraw_from_account);
}

void withdraw_permission_delete_operation::validate() const
{
   FC_ASSERT( fee.amount >= 0 );
   FC_ASSERT( withdraw_from_account != authorized_account );
}

share_type withdraw_permission_delete_operation::calculate_fee(const fee_schedule_type& k) const
{
   return k.withdraw_permission_delete_fee;
}

void withdraw_permission_create_operation::get_required_auth(flat_set<account_id_type>& active_auth_set, flat_set<account_id_type>&) const
{
   active_auth_set.insert(withdraw_from_account);
}

void withdraw_permission_create_operation::validate() const
{
   FC_ASSERT( fee.amount >= 0 );
   FC_ASSERT( withdraw_from_account != authorized_account );
   FC_ASSERT( withdrawal_limit.amount > 0 );
   //TODO: better bounds checking on these values
   FC_ASSERT( withdrawal_period_sec > 0 );
   FC_ASSERT( periods_until_expiration > 0 );
}

share_type withdraw_permission_create_operation::calculate_fee(const fee_schedule_type& k) const
{
   return k.withdraw_permission_create_fee;
}


void        asset_global_settle_operation::get_required_auth(flat_set<account_id_type>& active_auth_set, flat_set<account_id_type>&)const
{
   active_auth_set.insert( fee_payer() );

}

void        asset_global_settle_operation::validate()const
{
   FC_ASSERT( fee.amount >= 0 );
   FC_ASSERT( asset_to_settle == settle_price.base.asset_id );
}

share_type  asset_global_settle_operation::calculate_fee(const fee_schedule_type& k)const
{
   return k.asset_global_settle_fee;
}

void asset_settle_operation::get_required_auth(flat_set<account_id_type>& active_auth_set, flat_set<account_id_type>&) const
{
   active_auth_set.insert( account );
}

void asset_settle_operation::validate() const
{
   FC_ASSERT( fee.amount >= 0 );
   FC_ASSERT( amount.amount >= 0 );
}

share_type asset_settle_operation::calculate_fee(const fee_schedule_type& k) const
{
   return k.asset_settle_fee;
}


void graphene::chain::asset_publish_feed_operation::get_required_auth(flat_set<account_id_type>& active_auth_set, flat_set<account_id_type>&) const
{
   active_auth_set.insert(publisher);
}

void asset_update_bitasset_operation::get_required_auth(flat_set<account_id_type>& active_auth_set, flat_set<account_id_type>&) const
{
   active_auth_set.insert(issuer);
}

void asset_update_bitasset_operation::validate() const
{
   FC_ASSERT( fee.amount >= 0 );
   new_options.validate();
}

share_type asset_update_bitasset_operation::calculate_fee(const fee_schedule_type& k) const
{
   return k.asset_update_fee;
}

void asset_update_feed_producers_operation::validate() const
{
   FC_ASSERT( fee.amount >= 0 );
}

share_type asset_update_feed_producers_operation::calculate_fee(const fee_schedule_type &k) const
{
   return k.asset_update_fee + k.total_data_fee(new_feed_producers);
}

void vesting_balance_create_operation::get_required_auth(flat_set<account_id_type>& active_auth_set, flat_set<account_id_type>&)const
{
    // owner's authorization isn't needed since this is effectively a transfer of value TO the owner
   active_auth_set.insert(creator);
}

share_type vesting_balance_create_operation::calculate_fee(const fee_schedule_type& k)const
{
   // We don't want to have custom inspection for each policy type; instead, charge a data fee for big ones
   return k.vesting_balance_create_fee + k.total_data_fee(policy);
}

void vesting_balance_create_operation::validate()const
{
   FC_ASSERT( fee.amount >= 0 );
   FC_ASSERT( amount.amount > 0 );
}

void vesting_balance_withdraw_operation::get_required_auth(flat_set<account_id_type>& active_auth_set, flat_set<account_id_type>&)const
{
   active_auth_set.insert(owner);
}

void vesting_balance_withdraw_operation::validate()const
{
   FC_ASSERT( fee.amount >= 0 );
   FC_ASSERT( amount.amount > 0 );
}

share_type vesting_balance_withdraw_operation::calculate_fee(const fee_schedule_type& k)const
{
   return k.vesting_balance_withdraw_fee;
}

void memo_data::set_message( const fc::ecc::private_key& priv,
                             const fc::ecc::public_key& pub, const string& msg )
{
   if( from )
   {
      uint64_t entropy = fc::sha224::hash(fc::ecc::private_key::generate())._hash[0];
      entropy <<= 32;
      entropy                                                     &= 0xff00000000000000;
      nonce = (fc::time_point::now().time_since_epoch().count()   &  0x00ffffffffffffff) | entropy;
      auto secret = priv.get_shared_secret(pub);
      auto nonce_plus_secret = fc::sha512::hash(fc::to_string(nonce) + secret.str());
      string text = memo_message(digest_type::hash(msg)._hash[0], msg).serialize();
      message = fc::aes_encrypt( nonce_plus_secret, vector<char>(text.begin(), text.end()) );
   }
   else
   {
      auto text = memo_message( 0, msg ).serialize();
      message = vector<char>(text.begin(), text.end());
   }
}

string memo_data::get_message( const fc::ecc::private_key& priv,
                               const fc::ecc::public_key& pub )const
{
   if( from )
   {
      auto secret = priv.get_shared_secret(pub);
      auto nonce_plus_secret = fc::sha512::hash(fc::to_string(nonce) + secret.str());
      auto plain_text = fc::aes_decrypt( nonce_plus_secret, message );
      auto result = memo_message::deserialize(string(plain_text.begin(), plain_text.end()));
      FC_ASSERT( result.checksum == uint32_t(digest_type::hash(result.text)._hash[0]) );
      return result.text;
   }
   else
   {
      return memo_message::deserialize(string(message.begin(), message.end())).text;
   }
}

void custom_operation::get_required_auth(flat_set<account_id_type>& active_auth_set, flat_set<account_id_type>&)const
{
   active_auth_set.insert(required_auths.begin(), required_auths.end());
}
void custom_operation::validate()const
{
   FC_ASSERT( fee.amount > 0 );
}
share_type custom_operation::calculate_fee(const fee_schedule_type& k)const
{
   return k.custom_operation_fee + k.total_data_fee(required_auths, data);
}

void worker_create_operation::get_required_auth(flat_set<account_id_type>& active_auth_set, flat_set<account_id_type>&) const
{
   active_auth_set.insert(owner);
}

void worker_create_operation::validate() const
{
   FC_ASSERT(fee.amount >= 0);
   FC_ASSERT(work_end_date > work_begin_date);
   FC_ASSERT(daily_pay > 0);
   FC_ASSERT(daily_pay < GRAPHENE_MAX_SHARE_SUPPLY);
   FC_ASSERT(name.size() < GRAPHENE_MAX_WORKER_NAME_LENGTH );
   FC_ASSERT(url.size() < GRAPHENE_MAX_URL_LENGTH );
}

share_type worker_create_operation::calculate_fee(const fee_schedule_type& k) const
{
   // Charge data fees for excessively long name, URL, or large initializers
   return k.worker_create_fee + k.total_data_fee(name, url, initializer);
}

string memo_message::serialize() const
{
   auto serial_checksum = string(sizeof(checksum), ' ');
   (uint32_t&)(*serial_checksum.data()) = htonl(checksum);
   return serial_checksum + text;
}

memo_message memo_message::deserialize(const string& serial)
{
   memo_message result;
   FC_ASSERT( serial.size() >= sizeof(result.checksum) );
   result.checksum = ntohl((uint32_t&)(*serial.data()));
   result.text = serial.substr(sizeof(result.checksum));
   return result;
}

void account_upgrade_operation::validate() const
{
   FC_ASSERT( fee.amount >= 0 );
}

share_type account_upgrade_operation::calculate_fee(const fee_schedule_type& k) const
{
   if( upgrade_to_lifetime_member )
      return k.membership_lifetime_fee;
   return k.membership_annual_fee;
}

struct predicate_validator
{
   typedef void result_type;

   template<typename T>
   void operator()( const T& p )const
   {
      p.validate();
   }
};

void assert_operation::validate()const
{
   FC_ASSERT( fee.amount >= 0 );
   for( const auto& item : predicates )
   {
      FC_ASSERT( item.size() > 0 );
      fc::datastream<const char*> ds( item.data(), item.size() );
      predicate p;
      try {
         fc::raw::unpack( ds, p );
      }
      catch ( const fc::exception& e )
      {
         continue;
      }
      p.visit( predicate_validator() );
   }
}
void assert_operation::get_required_auth(flat_set<account_id_type>& active_auth_set, flat_set<account_id_type>&)const
{
   active_auth_set.insert(fee_paying_account);
   active_auth_set.insert(required_auths.begin(), required_auths.end());
}

/**
 * The fee for assert operations is proportional to their size,
 * but cheaper than a data fee because they require no storage
 */
share_type  assert_operation::calculate_fee(const fee_schedule_type& k)const
{
   return std::max(size_t(1), fc::raw::pack_size(*this) / 1024) * k.assert_op_fee;
}

void  balance_claim_operation::get_required_auth(flat_set<account_id_type>& active_auth_set, flat_set<account_id_type>&)const
{
   active_auth_set.insert( fee_payer() );
}

void  balance_claim_operation::validate()const
{
   FC_ASSERT( fee == asset() );
}


} } // namespace graphene::chain
