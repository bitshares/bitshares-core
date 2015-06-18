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
#include <fc/crypto/aes.hpp>

namespace graphene { namespace chain {

/**
 *  Valid symbols have between 3 and 17 upper case characters
 *  with at most a single "." that is not the first or last character.
 */
bool is_valid_symbol( const string& symbol )
{
   if( symbol.size() > 17 ) return false;
   if( symbol.size() < 3  ) return false;
   int dot_count = 0;
   for( auto c : symbol )
   {
      if( c == '.' ) ++dot_count;
      else if( c < 'A' || c > 'Z' ) return false;
   }
   if( symbol[0] == '.' || symbol[symbol.size()-1] == '.' )
      return false;
   return dot_count <= 1;
}

/**
 *  Valid names are all lower case, start with [a-z] and may
 *  have "." or "-" in the name along with a single '/'.  The
 *  next character after a "/", "." or "-" cannot be [0-9] or
 *  another '.', '-'.
 *
 */
bool is_valid_name( const string& s )
{
   if( s.size() <  2  ) return false;
   if( s.size() >= 64 ) return false;

   int num_slash = 0;
   char prev = ' ';
   for( auto c : s )
   {
      if( c >= 'a' && c <= 'z' ){}
      else if( c >= '0' && c <= '9' )
      {
         if( prev == ' ' || prev == '.' ||  prev == '/' ) return false;
      }
      else switch( c )
      {
            case '/':
               if( ++num_slash > 1 ) return false;
            case '.':
            case '-':
               if( prev == ' ' || prev == '/' || prev == '.' || prev == '-' ) return false;
              break;
            default:
              return false;
      }
      prev = c;
   }
   switch( s.back() )
   {
      case '/': case '-': case '.':
         return false;
      default:
         return true;
   }
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
   if( is_cheap_name( name ) ) s = 63;

   FC_ASSERT( s >= 2 );

   if( s == 8 )
     core_fee_required = schedule.account_len8_fee;
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

   return core_fee_required;
}
share_type account_update_operation::calculate_fee( const fee_schedule_type& schedule )const
{
   return schedule.account_create_fee;
}
void account_update_operation::get_required_auth(flat_set<account_id_type>& active_auth_set,
                                                 flat_set<account_id_type>& owner_auth_set) const
{
   if( owner || active )
      owner_auth_set.insert( account );
   else
      active_auth_set.insert( account );
}

void account_update_operation::validate()const
{
   FC_ASSERT( fee.amount >= 0 );
   FC_ASSERT( account != account_id_type() );
   FC_ASSERT( owner || active || new_options );

   if( new_options )
      new_options->validate();
}

share_type asset_create_operation::calculate_fee( const fee_schedule_type& schedule )const
{
   auto core_fee_required = schedule.asset_create_fee;

   uint32_t s = symbol.size();
   while( s <= 6 ) {  core_fee_required *= 30; ++s; }

   return core_fee_required;
}

share_type transfer_operation::calculate_fee( const fee_schedule_type& schedule )const
{
   share_type core_fee_required = schedule.transfer_fee;
   if( memo )
   {
      core_fee_required += share_type((memo->message.size() * schedule.data_fee)/1024);
   }
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
   FC_ASSERT( is_valid_symbol( symbol ) );
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

share_type asset_update_operation::calculate_fee( const fee_schedule_type& k )const
{
   return k.asset_update_fee;
}

void asset_burn_operation::get_required_auth(flat_set<account_id_type>& active_auth_set, flat_set<account_id_type>&) const
{
   active_auth_set.insert(payer);
}

void asset_burn_operation::validate()const
{
   FC_ASSERT( fee.amount >= 0 );
   FC_ASSERT( amount_to_burn.amount.value <= GRAPHENE_BLOCKCHAIN_MAX_SHARES );
   FC_ASSERT( amount_to_burn.amount.value > 0 );
}

share_type asset_burn_operation::calculate_fee( const fee_schedule_type& k )const
{
   return k.asset_issue_fee;
}

void asset_issue_operation::get_required_auth(flat_set<account_id_type>& active_auth_set, flat_set<account_id_type>&) const
{
   active_auth_set.insert(issuer);
}

void asset_issue_operation::validate()const
{
   FC_ASSERT( fee.amount >= 0 );
   FC_ASSERT( asset_to_issue.amount.value <= GRAPHENE_BLOCKCHAIN_MAX_SHARES );
   FC_ASSERT( asset_to_issue.amount.value > 0 );
   FC_ASSERT( asset_to_issue.asset_id != 0 );
}

share_type asset_issue_operation::calculate_fee( const fee_schedule_type& k )const
{
   return k.asset_issue_fee;
}

share_type delegate_create_operation::calculate_fee( const fee_schedule_type& k )const
{
   return k.delegate_create_fee ;
}

void delegate_create_operation::get_required_auth(flat_set<account_id_type>& active_auth_set, flat_set<account_id_type>&) const
{
   active_auth_set.insert(delegate_account);
}

void delegate_create_operation::validate()const
{
   FC_ASSERT( fee.amount >= 0 );
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
   return k.limit_order_fee;
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
   return k.limit_order_fee;
}


void call_order_update_operation::get_required_auth(flat_set<account_id_type>& active_auth_set, flat_set<account_id_type>&) const
{
   active_auth_set.insert(funding_account);
}

void call_order_update_operation::validate()const
{ try {
   FC_ASSERT( fee.amount >= 0 );
   FC_ASSERT( delta_collateral.asset_id != delta_debt.asset_id );
   FC_ASSERT( delta_debt.asset_id       == call_price.base.asset_id );
   FC_ASSERT( delta_collateral.asset_id == call_price.quote.asset_id );
   call_price.validate();
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
   return k.transfer_fee;
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
}

share_type witness_create_operation::calculate_fee(const fee_schedule_type& k) const
{
   return k.delegate_create_fee;
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

share_type withdraw_permission_claim_operation::calculate_fee( const fee_schedule_type& schedule )const
{
   share_type core_fee_required = schedule.transfer_fee;
   if( memo )
      core_fee_required += share_type((memo->message.size() * schedule.data_fee)/1024);
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
   return k.withdraw_permission_update_fee;
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
   return k.withdraw_permission_update_fee;
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

share_type  asset_global_settle_operation::calculate_fee( const fee_schedule_type& k )const
{
   return k.global_settle_fee;
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

void vesting_balance_create_operation::get_required_auth(flat_set<account_id_type>& active_auth_set, flat_set<account_id_type>&)const
{
   // owner's authorization isn't needed since this is effectively a transfer of value TO the owner
   active_auth_set.insert( creator );
}

share_type vesting_balance_create_operation::calculate_fee( const fee_schedule_type& k )const
{
   return k.vesting_balance_create_fee;
}

void vesting_balance_create_operation::validate()const
{
   FC_ASSERT( fee.amount >= 0 );
   FC_ASSERT( amount.amount > 0 );
   FC_ASSERT( vesting_seconds > 0 );
}

void vesting_balance_withdraw_operation::get_required_auth(flat_set<account_id_type>& active_auth_set, flat_set<account_id_type>&)const
{
   active_auth_set.insert( owner );
}

void vesting_balance_withdraw_operation::validate()const
{
   FC_ASSERT( fee.amount >= 0 );
   FC_ASSERT( amount.amount > 0 );
}

share_type vesting_balance_withdraw_operation::calculate_fee( const fee_schedule_type& k )const
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
share_type custom_operation::calculate_fee( const fee_schedule_type& k )const
{
   return (data.size() * k.data_fee)/1024;
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
   FC_ASSERT(daily_pay < GRAPHENE_BLOCKCHAIN_MAX_SHARES);
}

share_type worker_create_operation::calculate_fee(const fee_schedule_type& k) const
{
   return k.worker_create_fee;
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

} } // namespace graphene::chain
