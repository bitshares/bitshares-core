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
 * Names must comply with the following grammar (RFC 1035):
 * <domain> ::= <subdomain> | " "
 * <subdomain> ::= <label> | <subdomain> "." <label>
 * <label> ::= <letter> [ [ <ldh-str> ] <let-dig> ]
 * <ldh-str> ::= <let-dig-hyp> | <let-dig-hyp> <ldh-str>
 * <let-dig-hyp> ::= <let-dig> | "-"
 * <let-dig> ::= <letter> | <digit>
 *
 * Which is equivalent to the following:
 *
 * <domain> ::= <subdomain> | " "
 * <subdomain> ::= <label> ("." <label>)*
 * <label> ::= <letter> [ [ <let-dig-hyp>+ ] <let-dig> ]
 * <let-dig-hyp> ::= <let-dig> | "-"
 * <let-dig> ::= <letter> | <digit>
 *
 * I.e. a valid name consists of a dot-separated sequence
 * of one or more labels consisting of the following rules:
 *
 * - Each label is three characters or more
 * - Each label begins with a letter
 * - Each label ends with a letter or digit
 * - Each label contains only letters, digits or hyphens
 *
 * In addition we require the following:
 *
 * - All letters are lowercase
 * - Length is between (inclusive) GRAPHENE_MIN_ACCOUNT_NAME_LENGTH and GRAPHENE_MAX_ACCOUNT_NAME_LENGTH
 */

bool is_valid_name( const string& name )
{
    const size_t len = name.size();
    if( len < GRAPHENE_MIN_ACCOUNT_NAME_LENGTH )
        return false;

    if( len > GRAPHENE_MAX_ACCOUNT_NAME_LENGTH )
        return false;

    size_t begin = 0;
    while( true )
    {
       size_t end = name.find_first_of( '.', begin );
       if( end == std::string::npos )
          end = len;
       if( end - begin < 3 )
          return false;
       switch( name[begin] )
       {
          case 'a': case 'b': case 'c': case 'd': case 'e': case 'f': case 'g': case 'h':
          case 'i': case 'j': case 'k': case 'l': case 'm': case 'n': case 'o': case 'p':
          case 'q': case 'r': case 's': case 't': case 'u': case 'v': case 'w': case 'x':
          case 'y': case 'z':
             break;
          default:
             return false;
       }
       switch( name[end-1] )
       {
          case 'a': case 'b': case 'c': case 'd': case 'e': case 'f': case 'g': case 'h':
          case 'i': case 'j': case 'k': case 'l': case 'm': case 'n': case 'o': case 'p':
          case 'q': case 'r': case 's': case 't': case 'u': case 'v': case 'w': case 'x':
          case 'y': case 'z':
          case '0': case '1': case '2': case '3': case '4': case '5': case '6': case '7':
          case '8': case '9':
             break;
          default:
             return false;
       }
       for( size_t i=begin+1; i<end-1; i++ )
       {
          switch( name[i] )
          {
             case 'a': case 'b': case 'c': case 'd': case 'e': case 'f': case 'g': case 'h':
             case 'i': case 'j': case 'k': case 'l': case 'm': case 'n': case 'o': case 'p':
             case 'q': case 'r': case 's': case 't': case 'u': case 'v': case 'w': case 'x':
             case 'y': case 'z':
             case '0': case '1': case '2': case '3': case '4': case '5': case '6': case '7':
             case '8': case '9':
             case '-':
                break;
             default:
                return false;
          }
       }
       if( end == len )
          break;
       begin = end+1;
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
      s = 8;

   if( s >= 8 )
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
   else if( s <= 2 )
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

void account_update_operation::validate()const
{
   FC_ASSERT( fee.amount >= 0 );
   FC_ASSERT( account != account_id_type() );
   FC_ASSERT( owner || active || new_options );
   if( owner )
   {
      FC_ASSERT( owner->num_auths() != 0 );
      FC_ASSERT( owner->address_auths.size() == 0 );
   }
   if( active )
   {
      FC_ASSERT( active->num_auths() != 0 );
      FC_ASSERT( active->address_auths.size() == 0 );
   }

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

share_type override_transfer_operation::calculate_fee( const fee_schedule_type& schedule )const
{
   share_type core_fee_required = schedule.transfer_fee;
   if( memo )
      core_fee_required += schedule.total_data_fee(memo->message);
   return core_fee_required;
}



void account_create_operation::validate()const
{
   FC_ASSERT( fee.amount >= 0 );
   FC_ASSERT( is_valid_name( name ) );
   FC_ASSERT( referrer_percent <= GRAPHENE_100_PERCENT );
   FC_ASSERT( owner.num_auths() != 0 );
   FC_ASSERT( owner.address_auths.size() == 0 );
   // TODO: this asset causes many tests to fail, those tests should probably be updated
   //FC_ASSERT( active.num_auths() != 0 );
   FC_ASSERT( active.address_auths.size() == 0 );
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


void transfer_operation::validate()const
{
   FC_ASSERT( fee.amount >= 0 );
   FC_ASSERT( from != to );
   FC_ASSERT( amount.amount > 0 );
}

void override_transfer_operation::validate()const
{
   FC_ASSERT( fee.amount >= 0 );
   FC_ASSERT( from != to );
   FC_ASSERT( amount.amount > 0 );
   FC_ASSERT( issuer != from );
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


void delegate_create_operation::validate()const
{
   FC_ASSERT( fee.amount >= 0 );
   FC_ASSERT(url.size() < GRAPHENE_MAX_URL_LENGTH );
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


void limit_order_cancel_operation::validate()const
{
   FC_ASSERT( fee.amount >= 0 );
}

share_type limit_order_cancel_operation::calculate_fee(const fee_schedule_type& k) const
{
   return k.get_extended_fee(fee_schedule_type::limit_order_cancel_fee_id);
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


void proposal_create_operation::validate() const
{
   FC_ASSERT( !proposed_ops.empty() );
   for( const auto& op : proposed_ops ) op.validate();
}

share_type proposal_create_operation::calculate_fee(const fee_schedule_type& k) const
{
   return k.proposal_create_fee + k.total_data_fee(proposed_ops);
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

share_type proposal_delete_operation::calculate_fee(const fee_schedule_type& k)const
{ return k.get_extended_fee( fee_schedule_type::proposal_delete_fee_id ); }

void account_transfer_operation::validate()const
{
   FC_ASSERT( fee.amount >= 0 );
}


share_type  account_transfer_operation::calculate_fee( const fee_schedule_type& k )const
{
   return k.account_transfer_fee;
}


void proposal_delete_operation::validate() const
{
   FC_ASSERT( fee.amount >= 0 );
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


void global_parameters_update_operation::validate() const
{
   FC_ASSERT( fee.amount >= 0 );
   new_parameters.validate();
}

share_type global_parameters_update_operation::calculate_fee(const fee_schedule_type& k) const
{
   return k.global_parameters_update_fee;
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


void withdraw_permission_delete_operation::validate() const
{
   FC_ASSERT( fee.amount >= 0 );
   FC_ASSERT( withdraw_from_account != authorized_account );
}

share_type withdraw_permission_delete_operation::calculate_fee(const fee_schedule_type& k) const
{
   return k.get_extended_fee( fee_schedule_type::withdraw_permission_delete_fee_id );
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

void        asset_global_settle_operation::validate()const
{
   FC_ASSERT( fee.amount >= 0 );
   FC_ASSERT( asset_to_settle == settle_price.base.asset_id );
}

share_type  asset_global_settle_operation::calculate_fee(const fee_schedule_type& k)const
{
   return k.asset_global_settle_fee;
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

void vesting_balance_withdraw_operation::validate()const
{
   FC_ASSERT( fee.amount >= 0 );
   FC_ASSERT( amount.amount > 0 );
}

share_type vesting_balance_withdraw_operation::calculate_fee(const fee_schedule_type& k)const
{
   return k.vesting_balance_withdraw_fee;
}

void memo_data::set_message(const fc::ecc::private_key& priv, const fc::ecc::public_key& pub,
                            const string& msg, uint64_t custom_nonce)
{
   if( from != public_key_type() )
   {
      if( custom_nonce == 0 )
      {
         uint64_t entropy = fc::sha224::hash(fc::ecc::private_key::generate())._hash[0];
         entropy <<= 32;
         entropy                                                     &= 0xff00000000000000;
         nonce = (fc::time_point::now().time_since_epoch().count()   &  0x00ffffffffffffff) | entropy;
      } else
         nonce = custom_nonce;
      auto secret = priv.get_shared_secret(pub);
      auto nonce_plus_secret = fc::sha512::hash(fc::to_string(nonce) + secret.str());
      string text = memo_message(digest_type::hash(msg)._hash[0], msg).serialize();
      message = fc::aes_encrypt( nonce_plus_secret, vector<char>(text.begin(), text.end()) );
   }
   else
   {
      auto text = memo_message(0, msg).serialize();
      message = vector<char>(text.begin(), text.end());
   }
}

string memo_data::get_message(const fc::ecc::private_key& priv,
                              const fc::ecc::public_key& pub)const
{
   if( from != public_key_type()  )
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

void custom_operation::validate()const
{
   FC_ASSERT( fee.amount > 0 );
}
share_type custom_operation::calculate_fee(const fee_schedule_type& k)const
{
   return k.custom_operation_fee + k.total_data_fee(required_auths, data);
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
   (uint32_t&)(*serial_checksum.data()) = checksum;
   return serial_checksum + text;
}

memo_message memo_message::deserialize(const string& serial)
{
   memo_message result;
   FC_ASSERT( serial.size() >= sizeof(result.checksum) );
   result.checksum = ((uint32_t&)(*serial.data()));
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

/**
 * The fee for assert operations is proportional to their size,
 * but cheaper than a data fee because they require no storage
 */
share_type  assert_operation::calculate_fee(const fee_schedule_type& k)const
{
   return std::max(size_t(1), fc::raw::pack_size(*this) / 1024) * k.assert_op_fee;
}

void  balance_claim_operation::validate()const
{
   FC_ASSERT( fee == asset() );
   FC_ASSERT( balance_owner_key != public_key_type() );
}

struct required_auth_visitor
{
   typedef void result_type;

   vector<authority>& result;

   required_auth_visitor( vector<authority>& r ):result(r){}

   /** for most operations this is a no-op */
   template<typename T>
   void operator()(const T& )const {}
};

struct required_active_visitor
{
   typedef void result_type;

   flat_set<account_id_type>& result;

   required_active_visitor( flat_set<account_id_type>& r ):result(r){}

   /** for most operations this is just the fee payer */
   template<typename T>
   void operator()(const T& o)const 
   { 
      result.insert( o.fee_payer() );
   }
   void operator()(const account_update_operation& o)const 
   {
      /// if owner authority is required, no active authority is required
      if( !(o.owner || o.active) ) /// TODO: document why active cannot be updated by active?
         result.insert( o.fee_payer() );
   }
   void operator()( const proposal_delete_operation& o )const
   {
      if( !o.using_owner_authority )
         result.insert( o.fee_payer() );
   }

   void operator()( const proposal_update_operation& o )const
   {
      result.insert( o.fee_payer() );
      for( auto id : o.active_approvals_to_add )
         result.insert(id);
      for( auto id : o.active_approvals_to_remove )
         result.insert(id);
   }
   void operator()( const custom_operation& o )const
   {
      result.insert( o.required_auths.begin(), o.required_auths.end() );
   }
   void operator()( const assert_operation& o )const
   {
      result.insert( o.fee_payer() );
      result.insert( o.required_auths.begin(), o.required_auths.end() );
   }
};

struct required_owner_visitor
{
   typedef void result_type;

   flat_set<account_id_type>& result;

   required_owner_visitor( flat_set<account_id_type>& r ):result(r){}

   /** for most operations this is a no-op */
   template<typename T>
   void operator()(const T& o)const {}

   void operator()(const account_update_operation& o)const 
   {
      if( o.owner || o.active ) /// TODO: document why active cannot be updated by active?
         result.insert( o.account );
   }

   void operator()( const proposal_delete_operation& o )const
   {
      if( o.using_owner_authority )
         result.insert( o.fee_payer() );
   }

   void operator()( const proposal_update_operation& o )const
   {
      for( auto id : o.owner_approvals_to_add )
         result.insert(id);
      for( auto id : o.owner_approvals_to_remove )
         result.insert(id);
   }
};


void operation_get_required_authorities( const operation& op, vector<authority>& result )
{
   op.visit( required_auth_visitor( result ) );
}
void operation_get_required_active_authorities( const operation& op, flat_set<account_id_type>& result )
{
   op.visit( required_active_visitor( result ) );
}
void operation_get_required_owner_authorities( const operation& op, flat_set<account_id_type>& result )
{
   op.visit( required_owner_visitor( result ) );
}

/**
 * @brief Used to validate operations in a polymorphic manner
 */
struct operation_validator
{
   typedef void result_type;
   template<typename T>
   void operator()( const T& v )const { v.validate(); }
};

void operation_validate( const operation& op )
{
   op.visit( operation_validator() );
}

void op_wrapper::validate()const
{
   operation_validate(op);
}


} } // namespace graphene::chain
