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
/* Copyright (C) Cryptonomex, Inc - All Rights Reserved
 *
 *  All modifications become property of Cryptonomex, Inc.
 *
 **/
#pragma once

#include <graphene/chain/types.hpp>
#include <graphene/chain/asset.hpp>
#include <graphene/chain/authority.hpp>
#include <graphene/chain/asset_object.hpp>
#include <graphene/chain/worker_object.hpp>
#include <graphene/chain/account_object.hpp>

#include <fc/static_variant.hpp>
#include <fc/uint128.hpp>

namespace graphene { namespace chain {

   bool is_valid_symbol( const string& symbol );
   bool is_valid_name( const string& s );
   bool is_premium_name( const string& n );
   bool is_cheap_name( const string& n );

   struct void_result{};
   typedef fc::static_variant<void_result,object_id_type,asset> operation_result;

   struct balance_accumulator
   {
      void  adjust( account_id_type account, const asset& delta )
      {
         balance[ std::make_pair(account, delta.asset_id) ] += delta.amount;
      }
      flat_map< pair<account_id_type, asset_id_type>, share_type > balance;
   };

   /**
    *  @defgroup operations Operations
    *  @ingroup transactions Transactions
    *  @brief A set of valid comands for mutating the globally shared state.
    *
    *  An operation can be thought of like a function that will modify the global
    *  shared state of the blockchain.  The members of each struct are like function
    *  arguments and each operation can potentially generate a return value.
    *
    *  Operations can be grouped into transactions (@ref transaction) to ensure that they occur
    *  in a particular order and that all operations apply successfully or
    *  no operations apply.
    *
    *  Each operation is a fully defined state transition and can exist in a transaction on its own.
    *
    *  @section operation_design_principles Design Principles
    *
    *  Operations have been carefully designed to include all of the information necessary to
    *  interpret them outside the context of the blockchain.   This means that information about
    *  current chain state is included in the operation even though it could be inferred from
    *  a subset of the data.   This makes the expected outcome of each operation well defined and
    *  easily understood without access to chain state.
    *
    *  @subsection balance_calculation Balance Calculation Principle
    *
    *    We have stipulated that the current account balance may be entirely calculated from
    *    just the subset of operations that are relevant to that account.  There should be
    *    no need to process the entire blockchain inorder to know your account's balance.
    *
    *  @subsection fee_calculation Explicit Fee Principle
    *
    *    Blockchain fees can change from time to time and it is important that a signed
    *    transaction explicitly agree to the fees it will be paying.  This aids with account
    *    balance updates and ensures that the sender agreed to the fee prior to making the
    *    transaction.
    *
    *  @subsection defined_authority Explicit Authority
    *
    *    Each operation shall contain enough information to know which accounts must authorize
    *    the operation.  This principle enables authority verification to occur in a centralized,
    *    optimized, and parallel manner.
    *
    *  @subsection relevancy_principle Explicit Relevant Accounts
    *
    *    Each operation contains enough information to enumerate all accounts for which the
    *    operation should apear in its account history.  This principle enables us to easily
    *    define and enforce the @balance_calculation. This is superset of the @ref defined_authority
    *
    *  @{
    */

   /**
    *  @brief assert that some conditions are true.
    *  @ingroup operations
    *
    *  This operation performs no changes to the database state, but can but used to verify 
    *  pre or post conditions for other operations.  
    *
    */
   struct assert_operation
   {
      asset                     fee;
      account_id_type           fee_paying_account;
      vector< vector< char > >  predicates;
      flat_set<account_id_type> required_auths;

      account_id_type fee_payer()const { return fee_paying_account; }
      void            get_required_auth(flat_set<account_id_type>& active_auth_set, flat_set<account_id_type>&)const;
      share_type      calculate_fee( const fee_schedule_type& k )const;
      void            validate()const;

      void get_balance_delta( balance_accumulator& acc, const operation_result& result = asset())const { acc.adjust( fee_payer(), -fee ); }
   };

   /**
    *  @brief reserves a new ID to refer to a particular key or address.
    *  @ingroup operations
    */
   struct key_create_operation
   {
      asset            fee;
      account_id_type  fee_paying_account;
      static_variant<address,public_key_type> key_data;

      account_id_type fee_payer()const { return fee_paying_account; }
      void            get_required_auth(flat_set<account_id_type>& active_auth_set , flat_set<account_id_type>&)const;
      share_type      calculate_fee( const fee_schedule_type& k )const{ return k.key_create_fee; }
      void            validate()const;

      void get_balance_delta( balance_accumulator& acc, const operation_result& result = asset())const { acc.adjust( fee_payer(), -fee ); }
   };

   /**
    *  @ingroup operations
    */
   struct account_create_operation
   {
      asset           fee;
      /// This account pays the fee. Must be a lifetime member.
      account_id_type registrar;

      /// This account receives a portion of the fee split between registrar and referrer. Must be a member.
      account_id_type referrer;
      /// Of the fee split between registrar and referrer, this percentage goes to the referrer. The rest goes to the
      /// registrar.
      uint16_t        referrer_percent = 0;

      string          name;
      authority       owner;
      authority       active;

      account_object::options_type options;

      account_id_type fee_payer()const { return registrar; }
      void       get_required_auth(flat_set<account_id_type>& active_auth_set , flat_set<account_id_type>&)const;
      void       validate()const;
      share_type calculate_fee( const fee_schedule_type& k )const;

      void get_balance_delta( balance_accumulator& acc, const operation_result& result = asset())const { acc.adjust( fee_payer(), -fee ); }
   };

   /**
    * @brief This operation is used to whitelist and blacklist accounts, primarily for transacting in whitelisted assets
    * @ingroup operations
    *
    * Accounts can freely specify opinions about other accounts, in the form of either whitelisting or blacklisting
    * them. This information is used in chain validation only to determine whether an account is authorized to transact
    * in an asset type which enforces a whitelist, but third parties can use this information for other uses as well,
    * as long as it does not conflict with the use of whitelisted assets.
    *
    * An asset which enforces a whitelist specifies a list of accounts to maintain its whitelist, and a list of
    * accounts to maintain its blacklist. In order for a given account A to hold and transact in a whitelisted asset S,
    * A must be whitelisted by at least one of S's whitelist_authorities and blacklisted by none of S's
    * blacklist_authorities. If A receives a balance of S, and is later removed from the whitelist(s) which allowed it
    * to hold S, or added to any blacklist S specifies as authoritative, A's balance of S will be frozen until A's
    * authorization is reinstated.
    *
    * This operation requires authorizing_account's signature, but not account_to_list's. The fee is paid by
    * authorizing_account.
    */
   struct account_whitelist_operation
   {
      enum account_listing {
         no_listing = 0x0, ///< No opinion is specified about this account
         white_listed = 0x1, ///< This account is whitelisted, but not blacklisted
         black_listed = 0x2, ///< This account is blacklisted, but not whitelisted
         white_and_black_listed = white_listed | black_listed ///< This account is both whitelisted and blacklisted
      };

      /// Paid by authorizing_account
      asset           fee;
      /// The account which is specifying an opinion of another account
      account_id_type authorizing_account;
      /// The account being opined about
      account_id_type account_to_list;
      /// The new white and blacklist status of account_to_list, as determined by authorizing_account
      /// This is a bitfield using values defined in the account_listing enum
      uint8_t new_listing;

      account_id_type fee_payer()const { return authorizing_account; }
      void get_required_auth(flat_set<account_id_type>& active_auth_set, flat_set<account_id_type>&)const;
      void validate()const { FC_ASSERT( fee.amount >= 0 ); FC_ASSERT(new_listing < 0x4); }
      share_type calculate_fee(const fee_schedule_type& k)const { return k.account_whitelist_fee; }

      void get_balance_delta( balance_accumulator& acc, const operation_result& result = asset())const { acc.adjust( fee_payer(), -fee ); }
   };

   /**
    * @ingroup operations
    */
   struct account_update_operation
   {
      asset fee;
      /// The account to update
      account_id_type account;

      /// New owner authority. If set, this operation requires owner authority to execute.
      optional<authority> owner;
      /// New active authority. If set, this operation requires owner authority to execute.
      optional<authority> active;

      /// New account options
      optional<account_object::options_type> new_options;

      account_id_type fee_payer()const { return account; }
      void       get_required_auth(flat_set<account_id_type>& active_auth_set , flat_set<account_id_type>& owner_auth_set)const;
      void       validate()const;
      share_type calculate_fee( const fee_schedule_type& k )const;
      void get_balance_delta( balance_accumulator& acc, const operation_result& result = asset())const { acc.adjust( fee_payer(), -fee ); }
   };

   /**
    * @brief Manage an account's membership status
    * @ingroup operations
    *
    * This operation is used to upgrade an account to a member, or renew its subscription. If an account which is an
    * unexpired annual subscription member publishes this operation with @ref upgrade_to_lifetime_member set to false,
    * the account's membership expiration date will be pushed backward one year. If a basic account publishes it with
    * @ref upgrade_to_lifetime_member set to false, the account will be upgraded to a subscription member with an
    * expiration date one year after the processing time of this operation.
    *
    * Any account may use this operation to become a lifetime member by setting @ref upgrade_to_lifetime_member to
    * true. Once an account has become a lifetime member, it may not use this operation anymore.
    */
   struct account_upgrade_operation
   {
      asset             fee;
      /// The account to upgrade; must not already be a lifetime member
      account_id_type   account_to_upgrade;
      /// If true, the account will be upgraded to a lifetime member; otherwise, it will add a year to the subscription
      bool              upgrade_to_lifetime_member = false;

      account_id_type fee_payer()const { return account_to_upgrade; }
      void       get_required_auth(flat_set<account_id_type>& active_auth_set , flat_set<account_id_type>&)const
      { active_auth_set.insert(account_to_upgrade); }
      void       validate()const;
      share_type calculate_fee( const fee_schedule_type& k )const;
      void get_balance_delta( balance_accumulator& acc, const operation_result& = asset())const { acc.adjust( fee_payer(), -fee ); }
   };

   /**
    * @brief transfers the account to another account while clearing the white list
    * @ingroup operations
    *
    * In theory an account can be transferred by simply updating the authorities, but that kind
    * of transfer lacks semantic meaning and is more often done to rotate keys without transferring
    * ownership.   This operation is used to indicate the legal transfer of title to this account and
    * a break in the operation history.
    *
    * The account_id's owner/active/voting/memo authority should be set to new_owner
    *
    * This operation will clear the account's whitelist statuses, but not the blacklist statuses.
    */
   struct account_transfer_operation
   {
      asset           fee;
      account_id_type account_id;
      account_id_type new_owner;

      account_id_type fee_payer()const { return account_id; }
      void        get_required_auth(flat_set<account_id_type>& active_auth_set, flat_set<account_id_type>&)const;
      void        validate()const;
      share_type  calculate_fee( const fee_schedule_type& k )const;

      void get_balance_delta( balance_accumulator& acc, const operation_result& result = asset())const { acc.adjust( fee_payer(), -fee ); }
   };

   /**
    * @brief Create a delegate object, as a bid to hold a delegate seat on the network.
    * @ingroup operations
    *
    * Accounts which wish to become delegates may use this operation to create a delegate object which stakeholders may
    * vote on to approve its position as a delegate.
    */
   struct delegate_create_operation
   {
      asset                                 fee;
      /// The account which owns the delegate. This account pays the fee for this operation.
      account_id_type                       delegate_account;

      account_id_type fee_payer()const { return delegate_account; }
      void get_required_auth(flat_set<account_id_type>& active_auth_set, flat_set<account_id_type>&)const;
      void validate()const;
      share_type calculate_fee( const fee_schedule_type& k )const;
      void get_balance_delta( balance_accumulator& acc, const operation_result& result = asset())const { acc.adjust( fee_payer(), -fee ); }
   };

  /**
    * @brief Create a witness object, as a bid to hold a witness position on the network.
    * @ingroup operations
    *
    * Accounts which wish to become witnesses may use this operation to create a witness object which stakeholders may
    * vote on to approve its position as a witness.
    */
   struct witness_create_operation
   {
      asset             fee;
      /// The account which owns the delegate. This account pays the fee for this operation.
      account_id_type   witness_account;
      key_id_type       block_signing_key;
      secret_hash_type  initial_secret;

      account_id_type fee_payer()const { return witness_account; }
      void get_required_auth(flat_set<account_id_type>& active_auth_set, flat_set<account_id_type>&)const;
      void validate()const;
      share_type calculate_fee( const fee_schedule_type& k )const;

      void get_balance_delta( balance_accumulator& acc, const operation_result& result = asset())const { acc.adjust( fee_payer(), -fee ); }
   };

   /**
    * @ingroup operations
    *  Used to move witness pay from accumulated_income to their account balance.
    */
   struct witness_withdraw_pay_operation
   {
      asset            fee;
      /// The account to pay. Must match from_witness->witness_account. This account pays the fee for this operation.
      account_id_type  to_account;
      witness_id_type  from_witness;
      share_type       amount;

      account_id_type fee_payer()const { return to_account; }
      void       get_required_auth(flat_set<account_id_type>& active_auth_set, flat_set<account_id_type>&)const;
      void       validate()const;
      share_type calculate_fee( const fee_schedule_type& k )const;

      void get_balance_delta( balance_accumulator& acc, const operation_result& result = asset())const
      {
         acc.adjust( fee_payer(), -fee );
         acc.adjust( to_account, amount );
      }
   };

   /**
    * @brief Used by delegates to update the global parameters of the blockchain.
    * @ingroup operations
    *
    * This operation allows the delegates to update the global parameters on the blockchain. These control various
    * tunable aspects of the chain, including block and maintenance intervals, maximum data sizes, the fees charged by
    * the network, etc.
    *
    * This operation may only be used in a proposed transaction, and a proposed transaction which contains this
    * operation must have a review period specified in the current global parameters before it may be accepted.
    */
   struct global_parameters_update_operation
   {
      asset fee;
      chain_parameters new_parameters;

      account_id_type fee_payer()const { return account_id_type(); }
      void get_required_auth(flat_set<account_id_type>& active_auth_set, flat_set<account_id_type>&)const
      { active_auth_set.insert(account_id_type()); }
      void validate()const;
      share_type calculate_fee( const fee_schedule_type& k )const;
      void get_balance_delta( balance_accumulator& acc, const operation_result& result = asset())const { acc.adjust( fee_payer(), -fee ); }
   };

   /**
    * @brief defines a message and checksum to enable validation of successful decryption
    *
    * When encrypting/decrypting a checksum is required to determine whether or not
    * decryption was successful.
    */
   struct memo_message
   {
      memo_message(){}
      memo_message( uint32_t checksum, const std::string& text )
      :checksum(checksum),text(text){}

      uint32_t    checksum = 0;
      std::string text;

      string serialize() const;
      static memo_message deserialize(const string& serial);
   };

   /**
    *  @brief defines the keys used to derive the shared secret
    *
    *  Because account authorities and keys can change at any time, each memo must
    *  capture the specific keys used to derive the shared secret.  In order to read
    *  the cipher message you will need one of the two private keys.
    *
    *  If @ref from == @ref to and @ref from == 0 then no encryption is used, the memo is public.
    *  If @ref from == @ref to and @ref from != 0 then invalid memo data
    *
    */
   struct memo_data
   {
      key_id_type from;
      key_id_type to;
      /**
       * 64 bit nonce format:
       * [  8 bits | 56 bits   ]
       * [ entropy | timestamp ]
       * Timestamp is number of microseconds since the epoch
       * Entropy is a byte taken from the hash of a new private key
       *
       * This format is not mandated or verified; it is chosen to ensure uniqueness of key-IV pairs only. This should
       * be unique with high probability as long as the generating host has a high-resolution clock OR a strong source
       * of entropy for generating private keys.
       */
      uint64_t    nonce;
      /**
       * This field contains the AES encrypted packed @ref memo_message
       */
      vector<char> message;

      void        set_message( const fc::ecc::private_key& priv,
                               const fc::ecc::public_key& pub, const string& msg );

      std::string get_message( const fc::ecc::private_key& priv,
                               const fc::ecc::public_key& pub )const;
   };

   /**
    * @ingroup operations
    *
    * @brief Transfers an amount of one asset from one account to another
    *
    *  Fees are paid by the "from" account
    *
    *  @pre amount.amount > 0
    *  @pre fee.amount >= 0
    *  @pre from != to
    *  @post from account's balance will be reduced by fee and amount
    *  @post to account's balance will be increased by amount
    *  @return n/a
    */
   struct transfer_operation
   {
      /** paid by the from account, may be of any asset for which there is a funded fee pool
       **/
      asset           fee;
      account_id_type from;
      account_id_type to;
      /** the amount and asset type that will be withdrawn from account "from" and added to account "to"
       *
       **/
      asset           amount;

      /** user provided data encrypted to the memo key of the "to" account */
      optional<memo_data>  memo;

      account_id_type fee_payer()const { return from; }
      void       get_required_auth(flat_set<account_id_type>& active_auth_set, flat_set<account_id_type>&)const;
      void       validate()const;
      share_type calculate_fee( const fee_schedule_type& k )const;
      void get_balance_delta( balance_accumulator& acc, const operation_result& result = asset())const
      {
         acc.adjust( fee_payer(), -fee );
         acc.adjust( from, -amount );
         acc.adjust( to, amount );
      }
   };

   /**
    * @ingroup operations
    */
   struct asset_create_operation
   {
      asset                   fee;
      /// This account must sign and pay the fee for this operation. Later, this account may update the asset
      account_id_type         issuer;
      /// The ticker symbol of this asset
      string                  symbol;
      /// Number of digits to the right of decimal point, must be less than or equal to 12
      uint8_t                 precision = 0;

      /// Options common to all assets.
      ///
      /// @note common_options.core_exchange_rate technically needs to store the asset ID of this new asset. Since this
      /// ID is not known at the time this operation is created, create this price as though the new asset has instance
      /// ID 1, and the chain will overwrite it with the new asset's ID.
      asset_object::asset_options common_options;
      /// Options only available for BitAssets. MUST be non-null if and only if the @ref market_issued flag is set in
      /// common_options.flags
      fc::optional<asset_object::bitasset_options> bitasset_options;
      /// For BitAssets, set this to true if the asset implements a @ref prediction_market; false otherwise
      bool is_prediction_market = false;

      account_id_type fee_payer()const { return issuer; }
      void            get_required_auth(flat_set<account_id_type>& active_auth_set, flat_set<account_id_type>&)const;
      void            validate()const;
      share_type      calculate_fee( const fee_schedule_type& k )const;
      void            get_balance_delta( balance_accumulator& acc, const operation_result& result = asset())const { acc.adjust( fee_payer(), -fee ); }
   };

   /**
    *  @brief allows global settling of bitassets (black swan or prediction markets)
    *
    *  In order to use this operation, @ref asset_to_settle must have the global_settle flag set
    *
    *  When this operation is executed all balances are converted into the backing asset at the
    *  settle_price and all open margin positions are called at the settle price.  If this asset is
    *  used as backing for other bitassets, those bitassets will be force settled at their current
    *  feed price.
    */
   struct asset_global_settle_operation
   {
      asset           fee;
      account_id_type issuer; ///< must equal @ref asset_to_settle->issuer
      asset_id_type   asset_to_settle;
      price           settle_price;

      account_id_type fee_payer()const { return issuer; }
      void            get_required_auth(flat_set<account_id_type>& active_auth_set, flat_set<account_id_type>&)const;
      void            validate()const;
      share_type      calculate_fee( const fee_schedule_type& k )const;
      void            get_balance_delta( balance_accumulator& acc, const operation_result& result = asset())const { acc.adjust( fee_payer(), -fee ); }
   };

   /**
    * @brief Schedules a market-issued asset for automatic settlement
    * @ingroup operations
    *
    * Holders of market-issued assests may request a forced settlement for some amount of their asset. This means that
    * the specified sum will be locked by the chain and held for the settlement period, after which time the chain will
    * choose a margin posision holder and buy the settled asset using the margin's collateral. The price of this sale
    * will be based on the feed price for the market-issued asset being settled. The exact settlement price will be the
    * feed price at the time of settlement with an offset in favor of the margin position, where the offset is a
    * blockchain parameter set in the global_property_object.
    *
    * The fee is paid by @ref account, and @ref account must authorize this operation
    */
   struct asset_settle_operation
   {
      asset           fee;
      /// Account requesting the force settlement. This account pays the fee
      account_id_type account;
      /// Amount of asset to force settle. This must be a market-issued asset
      asset           amount;

      account_id_type fee_payer()const { return account; }
      void            get_required_auth(flat_set<account_id_type>& active_auth_set, flat_set<account_id_type>&)const;
      void            validate()const;
      share_type      calculate_fee( const fee_schedule_type& k )const;
      void get_balance_delta( balance_accumulator& acc, const operation_result& result = asset())const
      {
         acc.adjust( fee_payer(), -fee );
         acc.adjust( account, -amount );
      }
   };

   /**
    * @ingroup operations
    */
   struct asset_fund_fee_pool_operation
   {
      asset           fee; ///< core asset
      account_id_type from_account;
      asset_id_type   asset_id;
      share_type      amount; ///< core asset

      account_id_type fee_payer()const { return from_account; }
      void       get_required_auth(flat_set<account_id_type>& active_auth_set, flat_set<account_id_type>&)const;
      void       validate()const;
      share_type calculate_fee( const fee_schedule_type& k )const;
      void       get_balance_delta( balance_accumulator& acc, const operation_result& result = asset())const
      {
         acc.adjust( fee_payer(), -fee );
         acc.adjust( fee_payer(), -amount );
      }
   };

   /**
    * @brief Update options common to all assets
    * @ingroup operations
    *
    * There are a number of options which all assets in the network use. These options are enumerated in the @ref
    * asset_object::asset_options struct. This operation is used to update these options for an existing asset.
    *
    * @note This operation cannot be used to update BitAsset-specific options. For these options, use @ref
    * asset_update_bitasset_operation instead.
    *
    * @pre @ref issuer SHALL be an existing account and MUST match asset_object::issuer on @ref asset_to_update
    * @pre @ref fee SHALL be nonnegative, and @ref issuer MUST have a sufficient balance to pay it
    * @pre @ref new_options SHALL be internally consistent, as verified by @ref validate()
    * @post @ref asset_to_update will have options matching those of new_options
    */
   struct asset_update_operation
   {
      asset_update_operation(){}
      /// Initializes the operation to apply changes to the provided asset, and copies old.options into @ref new_options
      asset_update_operation(const asset_object& old);

      asset           fee;
      account_id_type issuer;
      asset_id_type   asset_to_update;

      /// If the asset is to be given a new issuer, specify his ID here.
      optional<account_id_type>   new_issuer;
      asset_object::asset_options new_options;

      account_id_type fee_payer()const { return issuer; }
      void            get_required_auth(flat_set<account_id_type>& active_auth_set, flat_set<account_id_type>&)const;
      void            validate()const;
      share_type      calculate_fee( const fee_schedule_type& k )const;
      void            get_balance_delta( balance_accumulator& acc, const operation_result& result = asset())const { acc.adjust( fee_payer(), -fee ); }
   };

   /**
    * @brief Update options specific to BitAssets
    * @ingroup operations
    *
    * BitAssets have some options which are not relevant to other asset types. This operation is used to update those
    * options an an existing BitAsset.
    *
    * @pre @ref issuer MUST be an existing account and MUST match asset_object::issuer on @ref asset_to_update
    * @pre @ref asset_to_update MUST be a BitAsset, i.e. @ref asset_object::is_market_issued() returns true
    * @pre @ref fee MUST be nonnegative, and @ref issuer MUST have a sufficient balance to pay it
    * @pre @ref new_options SHALL be internally consistent, as verified by @ref validate()
    * @post @ref asset_to_update will have BitAsset-specific options matching those of new_options
    */
   struct asset_update_bitasset_operation
   {
      asset           fee;
      account_id_type issuer;
      asset_id_type   asset_to_update;

      asset_object::bitasset_options new_options;

      account_id_type fee_payer()const { return issuer; }
      void            get_required_auth(flat_set<account_id_type>& active_auth_set, flat_set<account_id_type>&)const;
      void            validate()const;
      share_type      calculate_fee( const fee_schedule_type& k )const;
      void            get_balance_delta( balance_accumulator& acc, const operation_result& result = asset())const { acc.adjust( fee_payer(), -fee ); }
   };

   /**
    * @brief Update the set of feed-producing accounts for a BitAsset
    * @ingroup operations
    *
    * BitAssets have price feeds selected by taking the median values of recommendations from a set of feed producers.
    * This operation is used to specify which accounts may produce feeds for a given BitAsset.
    *
    * @pre @ref issuer MUST be an existing account, and MUST match asset_object::issuer on @ref asset_to_update
    * @pre @ref issuer MUST NOT be the genesis account
    * @pre @ref asset_to_update MUST be a BitAsset, i.e. @ref asset_object::is_market_issued() returns true
    * @pre @ref fee MUST be nonnegative, and @ref issuer MUST have a sufficient balance to pay it
    * @pre Cardinality of @ref new_feed_producers MUST NOT exceed @ref chain_parameters::maximum_asset_feed_publishers
    * @post @ref asset_to_update will have a set of feed producers matching @ref new_feed_producers
    * @post All valid feeds supplied by feed producers in @ref new_feed_producers, which were already feed producers
    * prior to execution of this operation, will be preserved
    */
   struct asset_update_feed_producers_operation
   {
      asset           fee;
      account_id_type issuer;
      asset_id_type   asset_to_update;

      flat_set<account_id_type> new_feed_producers;

      account_id_type fee_payer()const { return issuer; }
      void            get_required_auth(flat_set<account_id_type>& active_auth_set, flat_set<account_id_type>&)const
      { active_auth_set.insert(fee_payer()); }
      void            validate()const;
      share_type      calculate_fee( const fee_schedule_type& k )const
      { return k.asset_update_fee; }
      void            get_balance_delta( balance_accumulator& acc, const operation_result& result = asset())const
      { acc.adjust( fee_payer(), -fee ); }
   };

   /**
    * @brief Publish price feeds for market-issued assets
    * @ingroup operations
    *
    * Price feed providers use this operation to publish their price feeds for market-issued assets. A price feed is
    * used to tune the market for a particular market-issued asset. For each value in the feed, the median across all
    * delegate feeds for that asset is calculated and the market for the asset is configured with the median of that
    * value.
    *
    * The feed in the operation contains three prices: a call price limit, a short price limit, and a settlement price.
    * The call limit price is structured as (collateral asset) / (debt asset) and the short limit price is structured
    * as (asset for sale) / (collateral asset). Note that the asset IDs are opposite to eachother, so if we're
    * publishing a feed for USD, the call limit price will be CORE/USD and the short limit price will be USD/CORE. The
    * settlement price may be flipped either direction, as long as it is a ratio between the market-issued asset and
    * its collateral.
    */
   struct asset_publish_feed_operation
   {
      asset                  fee; ///< paid for by publisher
      account_id_type        publisher;
      asset_id_type          asset_id; ///< asset for which the feed is published
      price_feed             feed;

      account_id_type fee_payer()const { return publisher; }
      void       get_required_auth( flat_set<account_id_type>& active_auth_set, flat_set<account_id_type>& )const;
      void       validate()const;
      share_type calculate_fee( const fee_schedule_type& k )const;
      void get_balance_delta( balance_accumulator& acc, const operation_result& result = asset() )const { acc.adjust( fee_payer(), -fee ); }
   };

   /**
    * @ingroup operations
    */
   struct asset_issue_operation
   {
      asset            fee;
      account_id_type  issuer; ///< Must be asset_to_issue->asset_id->issuer
      asset            asset_to_issue;
      account_id_type  issue_to_account;


      /** user provided data encrypted to the memo key of the "to" account */
      optional<memo_data>  memo;

      account_id_type fee_payer()const { return issuer; }
      void            get_required_auth(flat_set<account_id_type>& active_auth_set, flat_set<account_id_type>&)const;
      void            validate()const;
      share_type      calculate_fee( const fee_schedule_type& k )const;
      void get_balance_delta( balance_accumulator& acc, const operation_result& result = asset())const
      { acc.adjust( fee_payer(), -fee ); acc.adjust(issue_to_account, asset_to_issue); }
   };

   /**
    * @brief used to take an asset out of circulation
    * @ingroup operations
    *
    * @note You cannot burn market-issued assets.
    */
   struct asset_burn_operation
   {
      asset             fee;
      account_id_type   payer;
      asset             amount_to_burn;

      account_id_type fee_payer()const { return payer; }
      void            get_required_auth(flat_set<account_id_type>& active_auth_set, flat_set<account_id_type>&)const;
      void            validate()const;
      share_type      calculate_fee( const fee_schedule_type& k )const;
      void get_balance_delta( balance_accumulator& acc, const operation_result& result = asset())const
      {
         acc.adjust( fee_payer(), -fee );
         acc.adjust( fee_payer(), -amount_to_burn );
      }
   };

   /**
    *  @class limit_order_create_operation
    *  @brief instructs the blockchain to attempt to sell one asset for another
    *  @ingroup operations
    *
    *  The blockchain will atempt to sell amount_to_sell.asset_id for as
    *  much min_to_receive.asset_id as possible.  The fee will be paid by
    *  the seller's account.  Market fees will apply as specified by the
    *  issuer of both the selling asset and the receiving asset as
    *  a percentage of the amount exchanged.
    *
    *  If either the selling asset or the receiving asset is white list
    *  restricted, the order will only be created if the seller is on
    *  the white list of the restricted asset type.
    *
    *  Market orders are matched in the order they are included
    *  in the block chain.
    */
   struct limit_order_create_operation
   {
      asset           fee;
      account_id_type seller;
      asset           amount_to_sell;
      asset           min_to_receive;
      /**
       *  This order should expire if not filled by expiration
       */
      time_point_sec  expiration = time_point_sec::maximum();

      /** if this flag is set the entire order must be filled or
       * the operation is rejected.
       */
      bool            fill_or_kill = false;

      pair<asset_id_type,asset_id_type> get_market()const
      {
         return amount_to_sell.asset_id < min_to_receive.asset_id ?
                std::make_pair( amount_to_sell.asset_id, min_to_receive.asset_id ) :
                std::make_pair( min_to_receive.asset_id, amount_to_sell.asset_id );
      }
      account_id_type fee_payer()const { return seller; }
      void            get_required_auth(flat_set<account_id_type>& active_auth_set, flat_set<account_id_type>&)const;
      void            validate()const;
      share_type      calculate_fee( const fee_schedule_type& k )const;
      price           get_price()const { return amount_to_sell / min_to_receive; }
      void            get_balance_delta( balance_accumulator& acc, const operation_result& result = asset())const
      {
         acc.adjust( fee_payer(), -fee );
         acc.adjust( seller, -amount_to_sell );
      }
   };


   /**
    *  @ingroup operations
    *  Used to cancel an existing limit order. Both fee_pay_account and the
    *  account to receive the proceeds must be the same as order->seller.
    *
    *  @return the amount actually refunded
    */
   struct limit_order_cancel_operation
   {
      asset               fee;
      limit_order_id_type order;
      /** must be order->seller */
      account_id_type     fee_paying_account;

      account_id_type fee_payer()const { return fee_paying_account; }
      void            get_required_auth(flat_set<account_id_type>& active_auth_set, flat_set<account_id_type>&)const;
      void            validate()const;
      share_type      calculate_fee( const fee_schedule_type& k )const;

      void            get_balance_delta( balance_accumulator& acc, const operation_result& result = asset())const
      {
         acc.adjust( fee_payer(), -fee );
         acc.adjust( fee_payer(), result.get<asset>() );
      }
   };


   /**
    *  @ingroup operations
    *
    *  This operation can be used to add collateral, cover, and adjust the margin call price for a particular user.
    *
    *  For prediction markets the collateral and debt must always be equal.
    *
    *  This operation will fail if it would trigger a margin call that couldn't be filled.  If the margin call hits
    *  the call price limit then it will fail if the call price is above the settlement price.
    *
    *  @note this operation can be used to force a market order using the collateral without requiring outside funds.
    */
   struct call_order_update_operation
   {
      asset               fee; ///< paid by funding_account
      account_id_type     funding_account; ///< pays fee, collateral, and cover
      asset               delta_collateral; ///< the amount of collateral to add to the margin position
      asset               delta_debt; ///< the amount of the debt to be paid off, may be negative to issue new debt

      account_id_type fee_payer()const { return funding_account; }
      void            get_required_auth(flat_set<account_id_type>& active_auth_set, flat_set<account_id_type>&)const;
      void            validate()const;
      share_type      calculate_fee( const fee_schedule_type& k )const;
      void            get_balance_delta( balance_accumulator& acc, const operation_result& result = asset())const
      {
         acc.adjust( fee_payer(), -fee );
         acc.adjust( funding_account, -delta_collateral );
         acc.adjust( funding_account, delta_debt );
      }
   };

   /**
     * @defgroup proposed_transactions  The Graphene Transaction Proposal Protocol
     * @ingroup operations
     *
     * Graphene allows users to propose a transaction which requires approval of multiple accounts in order to execute.
     * The user proposes a transaction using proposal_create_operation, then signatory accounts use
     * proposal_update_operations to add or remove their approvals from this operation. When a sufficient number of
     * approvals have been granted, the operations in the proposal are used to create a virtual transaction which is
     * subsequently evaluated. Even if the transaction fails, the proposal will be kept until the expiration time, at
     * which point, if sufficient approval is granted, the transaction will be evaluated a final time. This allows
     * transactions which will not execute successfully until a given time to still be executed through the proposal
     * mechanism. The first time the proposed transaction succeeds, the proposal will be regarded as resolved, and all
     * future updates will be invalid.
     *
     * The proposal system allows for arbitrarily complex or recursively nested authorities. If a recursive authority
     * (i.e. an authority which requires approval of 'nested' authorities on other accounts) is required for a
     * proposal, then a second proposal can be used to grant the nested authority's approval. That is, a second
     * proposal can be created which, when sufficiently approved, adds the approval of a nested authority to the first
     * proposal. This multiple-proposal scheme can be used to acquire approval for an arbitrarily deep authority tree.
     *
     * Note that at any time, a proposal can be approved in a single transaction if sufficient signatures are available
     * on the proposal_update_operation, as long as the authority tree to approve the proposal does not exceed the
     * maximum recursion depth. In practice, however, it is easier to use proposals to acquire all approvals, as this
     * leverages on-chain notification of all relevant parties that their approval is required. Off-chain
     * multi-signature approval requires some off-chain mechanism for acquiring several signatures on a single
     * transaction. This off-chain synchronization can be avoided using proposals.
     * @{
     */
   /**
    * op_wrapper is used to get around the circular definition of operation and proposals that contain them.
    */
   struct op_wrapper;
   /**
    * @brief The proposal_create_operation creates a transaction proposal, for use in multi-sig scenarios
    * @ingroup operations
    *
    * Creates a transaction proposal. The operations which compose the transaction are listed in order in proposed_ops,
    * and expiration_time specifies the time by which the proposal must be accepted or it will fail permanently. The
    * expiration_time cannot be farther in the future than the maximum expiration time set in the global properties
    * object.
    */
   struct proposal_create_operation
   {
       account_id_type    fee_paying_account;
       asset              fee;
       vector<op_wrapper> proposed_ops;
       time_point_sec     expiration_time;
       optional<uint32_t> review_period_seconds;

       /// Constructs a proposal_create_operation suitable for genesis proposals, with fee, expiration time and review
       /// period set appropriately.
       static proposal_create_operation genesis_proposal(const class database& db);

      account_id_type fee_payer()const { return fee_paying_account; }
      void       get_required_auth( flat_set<account_id_type>& active_auth_set, flat_set<account_id_type>& )const;
      void       validate()const;
      share_type calculate_fee( const fee_schedule_type& k )const { return 0; }

      void            get_balance_delta( balance_accumulator& acc, const operation_result& result = asset())const { acc.adjust( fee_payer(), -fee ); }
   };

   /**
    * @brief The proposal_update_operation updates an existing transaction proposal
    * @ingroup operations
    *
    * This operation allows accounts to add or revoke approval of a proposed transaction. Signatures sufficient to
    * satisfy the authority of each account in approvals are required on the transaction containing this operation.
    *
    * If an account with a multi-signature authority is listed in approvals_to_add or approvals_to_remove, either all
    * required signatures to satisfy that account's authority must be provided in the transaction containing this
    * operation, or a secondary proposal must be created which contains this operation.
    *
    * NOTE: If the proposal requires only an account's active authority, the account must not update adding its owner
    * authority's approval. This is considered an error. An owner approval may only be added if the proposal requires
    * the owner's authority.
    *
    * If an account's owner and active authority are both required, only the owner authority may approve. An attempt to
    * add or remove active authority approval to such a proposal will fail.
    */
   struct proposal_update_operation
   {
      account_id_type            fee_paying_account;
      asset                      fee;
      proposal_id_type           proposal;
      flat_set<account_id_type>  active_approvals_to_add;
      flat_set<account_id_type>  active_approvals_to_remove;
      flat_set<account_id_type>  owner_approvals_to_add;
      flat_set<account_id_type>  owner_approvals_to_remove;
      flat_set<key_id_type>      key_approvals_to_add;
      flat_set<key_id_type>      key_approvals_to_remove;

      account_id_type fee_payer()const { return fee_paying_account; }
      void       get_required_auth(flat_set<account_id_type>& active_auth_set, flat_set<account_id_type>& owner_auth_set)const;
      void       validate()const;
      share_type calculate_fee( const fee_schedule_type& k )const { return 0; }
      void       get_balance_delta( balance_accumulator& acc, const operation_result& result = asset())const { acc.adjust( fee_payer(), -fee ); }
   };

   /**
    * @brief The proposal_delete_operation deletes an existing transaction proposal
    * @ingroup operations
    *
    * This operation allows the early veto of a proposed transaction. It may be used by any account which is a required
    * authority on the proposed transaction, when that account's holder feels the proposal is ill-advised and he decides
    * he will never approve of it and wishes to put an end to all discussion of the issue. Because he is a required
    * authority, he could simply refuse to add his approval, but this would leave the topic open for debate until the
    * proposal expires. Using this operation, he can prevent any further breath from being wasted on such an absurd
    * proposal.
    */
   struct proposal_delete_operation
   {
      account_id_type   fee_paying_account;
      bool              using_owner_authority = false;
      asset             fee;
      proposal_id_type  proposal;

      account_id_type fee_payer()const { return fee_paying_account; }
      void       get_required_auth(flat_set<account_id_type>& active_auth_set, flat_set<account_id_type>& owner_auth_set)const;
      void       validate()const;
      share_type calculate_fee( const fee_schedule_type& k )const { return 0; }
      void       get_balance_delta( balance_accumulator& acc, const operation_result& result = asset())const { acc.adjust( fee_payer(), -fee ); }
   };
   ///@}

   /**
    * @ingroup operations
    *
    * @note This is a virtual operation that is created while matching orders and
    * emitted for the purpose of accurately tracking account history, accelerating
    * a reindex.
    */
   struct fill_order_operation
   {
      object_id_type      order_id;
      account_id_type     account_id;
      asset               pays;
      asset               receives;
      asset               fee; // paid by receiving account


      pair<asset_id_type,asset_id_type> get_market()const
      {
         return pays.asset_id < receives.asset_id ?
                std::make_pair( pays.asset_id, receives.asset_id ) :
                std::make_pair( receives.asset_id, pays.asset_id );
      }
      account_id_type fee_payer()const { return account_id; }
      void            get_required_auth(flat_set<account_id_type>& active_auth_set, flat_set<account_id_type>&)const
      { active_auth_set.insert(fee_payer()); }
      void            validate()const { FC_ASSERT( !"virtual operation" ); }
      share_type      calculate_fee( const fee_schedule_type& k )const { return share_type(); }
      void            get_balance_delta( balance_accumulator& acc, const operation_result& result = asset())const {
         // acc.adjust( fee_payer(), -fee );  fee never actually entered the account, this is a virtual operation
         acc.adjust( account_id, receives );
      }
   };

   /**
    * @brief Create a new withdrawal permission
    * @ingroup operations
    *
    * This operation creates a withdrawal permission, which allows some authorized account to withdraw from an
    * authorizing account. This operation is primarily useful for scheduling recurring payments.
    *
    * Withdrawal permissions define withdrawal periods, which is a span of time during which the authorized account may
    * make a withdrawal. Any number of withdrawals may be made so long as the total amount withdrawn per period does
    * not exceed the limit for any given period.
    *
    * Withdrawal permissions authorize only a specific pairing, i.e. a permission only authorizes one specified
    * authorized account to withdraw from one specified authorizing account. Withdrawals are limited and may not exceet
    * the withdrawal limit. The withdrawal must be made in the same asset as the limit; attempts with withdraw any
    * other asset type will be rejected.
    *
    * The fee for this operation is paid by withdraw_from_account, and this account is required to authorize this
    * operation.
    */
   struct withdraw_permission_create_operation
   {
      asset             fee;
      /// The account authorizing withdrawals from its balances
      account_id_type   withdraw_from_account;
      /// The account authorized to make withdrawals from withdraw_from_account
      account_id_type   authorized_account;
      /// The maximum amount authorized_account is allowed to withdraw in a given withdrawal period
      asset             withdrawal_limit;
      /// Length of the withdrawal period in seconds
      uint32_t          withdrawal_period_sec;
      /// The number of withdrawal periods this permission is valid for
      uint32_t          periods_until_expiration;
      /// Time at which the first withdrawal period begins; must be in the future
      time_point_sec    period_start_time;

      account_id_type fee_payer()const { return withdraw_from_account; }
      void            get_required_auth(flat_set<account_id_type>& active_auth_set, flat_set<account_id_type>&)const;
      void            validate()const;
      share_type      calculate_fee( const fee_schedule_type& k )const;
      void            get_balance_delta( balance_accumulator& acc, const operation_result& result = asset())const { acc.adjust( fee_payer(), -fee ); }
   };

   /**
    * @brief Update an existing withdraw permission
    * @ingroup operations
    *
    * This oeration is used to update the settings for an existing withdrawal permission. The accounts to withdraw to
    * and from may never be updated. The fields which may be updated are the withdrawal limit (both amount and asset
    * type may be updated), the withdrawal period length, the remaining number of periods until expiration, and the
    * starting time of the new period.
    *
    * Fee is paid by withdraw_from_account, which is required to authorize this operation
    */
   struct withdraw_permission_update_operation
   {
      asset                         fee;
      /// This account pays the fee. Must match permission_to_update->withdraw_from_account
      account_id_type               withdraw_from_account;
      /// The account authorized to make withdrawals. Must match permission_to_update->authorized_account
      account_id_type               authorized_account;
      /// ID of the permission which is being updated
      withdraw_permission_id_type   permission_to_update;
      /// New maximum amount the withdrawer is allowed to charge per withdrawal period
      asset                         withdrawal_limit;
      /// New length of the period between withdrawals
      uint32_t                      withdrawal_period_sec;
      /// New beginning of the next withdrawal period; must be in the future
      time_point_sec                period_start_time;
      /// The new number of withdrawal periods for which this permission will be valid
      uint32_t                      periods_until_expiration;

      account_id_type fee_payer()const { return withdraw_from_account; }
      void            get_required_auth(flat_set<account_id_type>& active_auth_set, flat_set<account_id_type>&)const;
      void            validate()const;
      share_type      calculate_fee( const fee_schedule_type& k )const;
      void            get_balance_delta( balance_accumulator& acc, const operation_result& result = asset())const { acc.adjust( fee_payer(), -fee ); }
   };

   /**
    * @brief Withdraw from an account which has published a withdrawal permission
    * @ingroup operations
    *
    * This operation is used to withdraw from an account which has authorized such a withdrawal. It may be executed at
    * most once per withdrawal period for the given permission. On execution, amount_to_withdraw is transferred from
    * withdraw_from_account to withdraw_to_account, assuming amount_to_withdraw is within the withdrawal limit. The
    * withdrawal permission will be updated to note that the withdrawal for the current period has occurred, and
    * further withdrawals will not be permitted until the next withdrawal period, assuming the permission has not
    * expired. This operation may be executed at any time within the current withdrawal period.
    *
    * Fee is paid by withdraw_to_account, which is required to authorize this operation
    */
   struct withdraw_permission_claim_operation
   {
      /// Paid by withdraw_to_account
      asset                       fee;
      /// ID of the permission authorizing this withdrawal
      withdraw_permission_id_type withdraw_permission;
      /// Must match withdraw_permission->withdraw_from_account
      account_id_type             withdraw_from_account;
      /// Must match withdraw_permision->authorized_account
      account_id_type             withdraw_to_account;
      /// Amount to withdraw. Must not exceed withdraw_permission->withdrawal_limit
      asset                       amount_to_withdraw;
      /// Memo for withdraw_from_account. Should generally be encrypted with withdraw_from_account->memo_key
      optional<memo_data>         memo;

      account_id_type fee_payer()const { return withdraw_to_account; }
      void            get_required_auth(flat_set<account_id_type>& active_auth_set, flat_set<account_id_type>&)const;
      void            validate()const;
      share_type      calculate_fee( const fee_schedule_type& k )const;
      void            get_balance_delta( balance_accumulator& acc, const operation_result& result = asset())const
      {
         acc.adjust( fee_payer(), -fee );
         acc.adjust( withdraw_to_account, amount_to_withdraw );
         acc.adjust( withdraw_from_account, -amount_to_withdraw );
      }
   };

   /**
    * @brief Delete an existing withdrawal permission
    * @ingroup operations
    *
    * This operation cancels a withdrawal permission, thus preventing any future withdrawals using that permission.
    *
    * Fee is paid by withdraw_from_account, which is required to authorize this operation
    */
   struct withdraw_permission_delete_operation
   {
      asset                         fee;
      /// Must match withdrawal_permission->withdraw_from_account. This account pays the fee.
      account_id_type               withdraw_from_account;
      /// The account previously authorized to make withdrawals. Must match withdrawal_permission->authorized_account
      account_id_type               authorized_account;
      /// ID of the permission to be revoked.
      withdraw_permission_id_type   withdrawal_permission;

      account_id_type fee_payer()const { return withdraw_from_account; }
      void            get_required_auth(flat_set<account_id_type>& active_auth_set, flat_set<account_id_type>&)const;
      void            validate()const;
      share_type      calculate_fee( const fee_schedule_type& k )const;
      void            get_balance_delta( balance_accumulator& acc, const operation_result& result = asset())const
      {
         acc.adjust( fee_payer(), -fee );
      }
   };

   /**
    * @brief Create a vesting balance.
    * @ingroup operations
    *
    *  The chain allows a user to create a vesting balance.
    *  Normally, vesting balances are created automatically as part
    *  of cashback and worker operations.  This operation allows
    *  vesting balances to be created manually as well.
    *
    *  Manual creation of vesting balances can be used by a stakeholder
    *  to publicly demonstrate that they are committed to the chain.
    *  It can also be used as a building block to create transactions
    *  that function like public debt.  Finally, it is useful for
    *  testing vesting balance functionality.
    *
    * @return ID of newly created vesting_balance_object
    */
   struct vesting_balance_create_operation
   {
      asset            fee;
      account_id_type  creator;         ///< Who provides funds initially
      account_id_type  owner;           ///< Who is able to withdraw the balance
      asset            amount;
      uint32_t         vesting_seconds;

      account_id_type   fee_payer()const { return creator; }
      void              get_required_auth(flat_set<account_id_type>& active_auth_set, flat_set<account_id_type>&)const;
      void              validate()const;
      share_type        calculate_fee( const fee_schedule_type& k )const;
      void              get_balance_delta( balance_accumulator& acc, const operation_result& result = asset())const
      {
         acc.adjust( fee_payer(), -fee );
         acc.adjust( creator, -amount );
      }
   };

   /**
    * @brief Withdraw from a vesting balance.
    * @ingroup operations
    *
    * Withdrawal from a not-completely-mature vesting balance
    * will result in paying fees.
    *
    * @return Nothing
    */
   struct vesting_balance_withdraw_operation
   {
      asset                   fee;
      vesting_balance_id_type vesting_balance;
      account_id_type         owner;              ///< Must be vesting_balance.owner
      asset                   amount;

      account_id_type   fee_payer()const { return owner; }
      void              get_required_auth(flat_set<account_id_type>& active_auth_set, flat_set<account_id_type>&)const;
      void              validate()const;
      share_type        calculate_fee( const fee_schedule_type& k )const;
      void              get_balance_delta( balance_accumulator& acc, const operation_result& result = asset())const
      {
         acc.adjust( fee_payer(), -fee );
         acc.adjust( owner, amount );
      }
   };

   /**
    * @defgroup workers The Blockchain Worker System
    * @ingroup operations
    *
    * Graphene blockchains allow the creation of special "workers" which are elected positions paid by the blockchain
    * for services they provide. There may be several types of workers, and the semantics of how and when they are paid
    * are defined by the @ref worker_type_enum enumeration. All workers are elected by core stakeholder approval, by
    * voting for or against them.
    *
    * Workers are paid from the blockchain's daily budget if their total approval (votes for - votes against) is
    * positive, ordered from most positive approval to least, until the budget is exhausted. Payments are processed at
    * the blockchain maintenance interval. If a worker does not have positive approval during payment processing, or if
    * the chain's budget is exhausted before the worker is paid, that worker is simply not paid at that interval.
    * Payment is not prorated based on percentage of the interval the worker was approved. If the chain attempts to pay
    * a worker, but the budget is insufficient to cover its entire pay, the worker is paid the remaining budget funds,
    * even though this does not fulfill his total pay. The worker will not receive extra pay to make up the difference
    * later. Worker pay is placed in a vesting balance and vests over the number of days specified at the worker's
    * creation.
    *
    * Once created, a worker is immutable and will be kept by the blockchain forever.
    *
    * @{
    */
   /**
    * @brief Create a new worker object
    * @ingroup operations
    */
   struct worker_create_operation
   {
      asset                fee;
      account_id_type      owner;
      time_point_sec       work_begin_date;
      time_point_sec       work_end_date;
      share_type           daily_pay;
      /// This should be set to the initializer appropriate for the type of worker to be created.
      worker_initializer   initializer;

      account_id_type   fee_payer()const { return owner; }
      void              get_required_auth(flat_set<account_id_type>& active_auth_set, flat_set<account_id_type>&)const;
      void              validate()const;
      share_type        calculate_fee( const fee_schedule_type& k )const;
      void              get_balance_delta( balance_accumulator& acc, const operation_result& result = asset())const
      {
         acc.adjust( fee_payer(), -fee );
      }

   };
   ///@}

   /**
    * @brief provides a generic way to add higher level protocols on top of witness consensus
    * @ingroup operations
    *
    * There is no validation for this operation other than that required auths are valid and a fee
    * is paid that is appropriate for the data contained.
    */
   struct custom_operation
   {
      asset                     fee;
      account_id_type           payer;
      flat_set<account_id_type> required_auths;
      uint16_t                  id;
      vector<char>              data;

      account_id_type   fee_payer()const { return payer; }
      void              get_required_auth(flat_set<account_id_type>& active_auth_set, flat_set<account_id_type>&)const;
      void              validate()const;
      share_type        calculate_fee( const fee_schedule_type& k )const;
      void              get_balance_delta( balance_accumulator& acc, const operation_result& result = asset())const
      {
         acc.adjust( fee_payer(), -fee );
      }
   };

   /**
    * @ingroup operations
    *
    * Defines the set of valid operations as a discriminated union type.
    */
   typedef fc::static_variant<
            transfer_operation,
            limit_order_create_operation,
            limit_order_cancel_operation,
            call_order_update_operation,
            key_create_operation,
            account_create_operation,
            account_update_operation,
            account_whitelist_operation,
            account_upgrade_operation,
            account_transfer_operation,
            asset_create_operation,
            asset_update_operation,
            asset_update_bitasset_operation,
            asset_update_feed_producers_operation,
            asset_issue_operation,
            asset_burn_operation,
            asset_fund_fee_pool_operation,
            asset_settle_operation,
            asset_global_settle_operation,
            asset_publish_feed_operation,
            delegate_create_operation,
            witness_create_operation,
            witness_withdraw_pay_operation,
            proposal_create_operation,
            proposal_update_operation,
            proposal_delete_operation,
            withdraw_permission_create_operation,
            withdraw_permission_update_operation,
            withdraw_permission_claim_operation,
            withdraw_permission_delete_operation,
            fill_order_operation,
            global_parameters_update_operation,
            vesting_balance_create_operation,
            vesting_balance_withdraw_operation,
            worker_create_operation,
            custom_operation,
            assert_operation
         > operation;

   /// @} // operations group

   /**
    *  Used to track the result of applying an operation and when it was applied.
    */
   struct applied_operation
   {
      operation        op;
      operation_result result;
      uint32_t         block_num;
      uint16_t         transaction_num;
      uint16_t         op_num;
   };

   /**
    * @brief Used to find accounts which must sign off on operations in a polymorphic manner
    */
   struct operation_get_required_auths
   {
      flat_set<account_id_type>& active_auth_set;
      flat_set<account_id_type>& owner_auth_set;
      operation_get_required_auths(flat_set<account_id_type>& active_auth_set,
                                   flat_set<account_id_type>& owner_auth_set)
         : active_auth_set(active_auth_set),
           owner_auth_set(owner_auth_set)
      {}
      typedef void result_type;
      template<typename T>
      void operator()(const T& v)const
      {
         v.get_required_auth(active_auth_set, owner_auth_set);
#ifndef NDEBUG
         if( !(active_auth_set.count(v.fee_payer()) || owner_auth_set.count(v.fee_payer())) )
            elog("Fee payer not in required auths on ${op}", ("op", fc::get_typename<T>::name()));
         assert(active_auth_set.count(v.fee_payer()) || owner_auth_set.count(v.fee_payer()));
#endif
      }
   };

   /**
    * @brief Used to validate operations in a polymorphic manner
    */
   struct operation_validator
   {
      typedef void result_type;
      template<typename T>
      void operator()( const T& v )const { v.validate(); }
   };

   /**
    * @brief Used to calculate fees in a polymorphic manner
    *
    * If you wish to pay fees in an asset other than CORE, use the core_exchange_rate argument to specify the rate of

    * exchange rate. It is up to the caller to ensure that the core_exchange_rate converts to an asset accepted by the
    * delegates at a rate which they will accept.
    */
   struct operation_calculate_fee
   {
      const fee_schedule_type& fees;
      const price& core_exchange_rate;
      operation_calculate_fee( const fee_schedule_type& f, const price& core_exchange_rate = price::unit_price() )
         : fees(f),
           core_exchange_rate(core_exchange_rate)
      {}
      typedef share_type result_type;
      template<typename T>
      share_type operator()( const T& v )const { return (v.calculate_fee(fees) * core_exchange_rate).amount; }
   };

   /**
    * @brief Used to set fees in a polymorphic manner
    *
    * If you wish to pay fees in an asset other than CORE, use the core_exchange_rate argument to specify the rate of
    * conversion you wish to use. The operation's fee will be set by multiplying the CORE fee by the provided exchange
    * rate. It is up to the caller to ensure that the core_exchange_rate converts to an asset accepted by the delegates
    * at a rate which they will accept.
    *
    * If total_fee is not nullptr, the total fee for all operations visited will be stored in the provided share_type.
    * The share_type will be set to zero when the visitor is constructed.
    */
   struct operation_set_fee
   {
      const fee_schedule_type& fees;
      const price& core_exchange_rate;
      share_type* total_fee;
      operation_set_fee( const fee_schedule_type& f,
                         const price& core_exchange_rate = price::unit_price(),
                         share_type* total_fee = nullptr )
         : fees(f),
           core_exchange_rate(core_exchange_rate),
           total_fee(total_fee)
      {
         if( total_fee )
            *total_fee = 0;
      }
      typedef asset result_type;
      template<typename T>
      asset operator()( T& v )const {
         asset fee = (v.calculate_fee(fees)) * core_exchange_rate;
         if( total_fee ) *total_fee += fee.amount;
         return v.fee = fee;
      }
   };

   /**
    *  @brief necessary to support nested operations inside the proposal_create_operation
    */
   struct op_wrapper
   {
      public:
      op_wrapper(const operation& op = operation()):op(op){}
      operation op;

      void       validate()const { op.visit( operation_validator() ); }
      void       get_required_auth(flat_set<account_id_type>& active, flat_set<account_id_type>& owner) {
         op.visit(operation_get_required_auths(active, owner));
      }
      asset      set_fee( const fee_schedule_type& k ) { return op.visit( operation_set_fee( k ) ); }
      share_type calculate_fee( const fee_schedule_type& k )const { return op.visit( operation_calculate_fee( k ) ); }
   };

} } // graphene::chain
FC_REFLECT( graphene::chain::op_wrapper, (op) )
FC_REFLECT( graphene::chain::memo_message, (checksum)(text) )
FC_REFLECT( graphene::chain::memo_data, (from)(to)(nonce)(message) )

FC_REFLECT( graphene::chain::key_create_operation,
            (fee)(fee_paying_account)
            (key_data)
          )

FC_REFLECT( graphene::chain::account_create_operation,
            (fee)(registrar)
            (referrer)(referrer_percent)
            (name)(owner)(active)(options)
          )

FC_REFLECT( graphene::chain::account_update_operation,
            (fee)(account)(owner)(active)(new_options)
          )
FC_REFLECT( graphene::chain::account_upgrade_operation, (fee)(account_to_upgrade)(upgrade_to_lifetime_member) )

FC_REFLECT_TYPENAME( graphene::chain::account_whitelist_operation::account_listing)
FC_REFLECT_ENUM( graphene::chain::account_whitelist_operation::account_listing,
                (no_listing)(white_listed)(black_listed)(white_and_black_listed))

FC_REFLECT( graphene::chain::account_whitelist_operation, (fee)(authorizing_account)(account_to_list)(new_listing))
FC_REFLECT( graphene::chain::account_transfer_operation, (fee)(account_id)(new_owner) )

FC_REFLECT( graphene::chain::delegate_create_operation,
            (fee)(delegate_account) )

FC_REFLECT( graphene::chain::witness_create_operation, (fee)(witness_account)(block_signing_key)(initial_secret) )
FC_REFLECT( graphene::chain::witness_withdraw_pay_operation, (fee)(from_witness)(to_account)(amount) )

FC_REFLECT( graphene::chain::limit_order_create_operation,
            (fee)(seller)(amount_to_sell)(min_to_receive)(expiration)(fill_or_kill)
          )
FC_REFLECT( graphene::chain::fill_order_operation, (fee)(order_id)(account_id)(pays)(receives) )
FC_REFLECT( graphene::chain::limit_order_cancel_operation,(fee)(fee_paying_account)(order) )
FC_REFLECT( graphene::chain::call_order_update_operation, (fee)(funding_account)(delta_collateral)(delta_debt) )

FC_REFLECT( graphene::chain::transfer_operation,
            (fee)(from)(to)(amount)(memo) )

FC_REFLECT( graphene::chain::asset_create_operation,
            (fee)
            (issuer)
            (symbol)
            (precision)
            (common_options)
            (bitasset_options)
            (is_prediction_market)
          )
FC_REFLECT( graphene::chain::asset_update_operation,
            (fee)
            (issuer)
            (asset_to_update)
            (new_issuer)
            (new_options)
          )
FC_REFLECT( graphene::chain::asset_update_bitasset_operation,
            (fee)
            (issuer)
            (asset_to_update)
            (new_options)
          )
FC_REFLECT( graphene::chain::asset_update_feed_producers_operation,
            (fee)(issuer)(asset_to_update)(new_feed_producers)
          )
FC_REFLECT( graphene::chain::asset_publish_feed_operation,
            (fee)(publisher)(asset_id)(feed) )
FC_REFLECT( graphene::chain::asset_settle_operation, (fee)(account)(amount) )
FC_REFLECT( graphene::chain::asset_global_settle_operation, (fee)(issuer)(asset_to_settle)(settle_price) )
FC_REFLECT( graphene::chain::asset_issue_operation,
            (fee)(issuer)(asset_to_issue)(issue_to_account)(memo) )
FC_REFLECT( graphene::chain::asset_burn_operation,
            (fee)(payer)(amount_to_burn) )

FC_REFLECT( graphene::chain::proposal_create_operation, (fee)(fee_paying_account)(expiration_time)
            (proposed_ops)(review_period_seconds) )
FC_REFLECT( graphene::chain::proposal_update_operation, (fee)(fee_paying_account)(proposal)
            (active_approvals_to_add)(active_approvals_to_remove)(owner_approvals_to_add)(owner_approvals_to_remove)
            (key_approvals_to_add)(key_approvals_to_remove) )
FC_REFLECT( graphene::chain::proposal_delete_operation, (fee)(fee_paying_account)(using_owner_authority)(proposal) )

FC_REFLECT( graphene::chain::asset_fund_fee_pool_operation, (fee)(from_account)(asset_id)(amount) );

FC_REFLECT( graphene::chain::global_parameters_update_operation, (fee)(new_parameters) );

FC_REFLECT( graphene::chain::withdraw_permission_create_operation, (fee)(withdraw_from_account)(authorized_account)
            (withdrawal_limit)(withdrawal_period_sec)(periods_until_expiration)(period_start_time) )
FC_REFLECT( graphene::chain::withdraw_permission_update_operation, (fee)(withdraw_from_account)(authorized_account)
            (permission_to_update)(withdrawal_limit)(withdrawal_period_sec)(period_start_time)(periods_until_expiration) )
FC_REFLECT( graphene::chain::withdraw_permission_claim_operation, (fee)(withdraw_permission)(withdraw_from_account)(withdraw_to_account)(amount_to_withdraw)(memo) );
FC_REFLECT( graphene::chain::withdraw_permission_delete_operation, (fee)(withdraw_from_account)(authorized_account)
            (withdrawal_permission) )

FC_REFLECT( graphene::chain::vesting_balance_create_operation, (fee)(creator)(owner)(amount)(vesting_seconds) )
FC_REFLECT( graphene::chain::vesting_balance_withdraw_operation, (fee)(vesting_balance)(owner)(amount) )

FC_REFLECT( graphene::chain::worker_create_operation,
            (fee)(owner)(work_begin_date)(work_end_date)(daily_pay)(initializer) )

FC_REFLECT( graphene::chain::custom_operation, (fee)(payer)(required_auths)(id)(data) )
FC_REFLECT( graphene::chain::assert_operation, (fee)(fee_paying_account)(predicates)(required_auths) )

FC_REFLECT( graphene::chain::void_result, )

FC_REFLECT_TYPENAME( graphene::chain::operation )
FC_REFLECT_TYPENAME( graphene::chain::operation_result )
FC_REFLECT_TYPENAME( fc::flat_set<graphene::chain::vote_id_type> )
