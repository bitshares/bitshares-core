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
#pragma once
#include <graphene/chain/protocol/base.hpp>
#include <graphene/chain/protocol/memo.hpp>

namespace graphene { namespace chain { 

   struct additional_asset_options
   {
      fc::optional<uint16_t>                  reward_percent;
      fc::optional<flat_set<account_id_type>> whitelist_market_fee_sharing;
   };
   typedef extension<additional_asset_options> additional_asset_options_t;

   bool is_valid_symbol( const string& symbol );

   /**
    * @brief The asset_options struct contains options available on all assets in the network
    *
    * @note Changes to this struct will break protocol compatibility
    */
   struct asset_options {
      /// The initial maximum supply of this asset which may exist at any given time.
      share_type initial_max_supply = GRAPHENE_INITIAL_MAX_SHARE_SUPPLY;
      /// When this asset is traded on the markets, this percentage of the total traded will be exacted and paid
      /// to the issuer. This is a fixed point value, representing hundredths of a percent, i.e. a value of 100
      /// in this field means a 1% fee is charged on market trades of this asset.
      uint16_t market_fee_percent = 0;
      /// Market fees calculated as @ref market_fee_percent of the traded volume are capped to this value
      share_type max_market_fee = GRAPHENE_INITIAL_MAX_SHARE_SUPPLY;

      /// The flags which the issuer has permission to update. See @ref asset_issuer_permission_flags
      uint16_t issuer_permissions = UIA_ASSET_ISSUER_PERMISSION_MASK;
      /// The currently active flags on this permission. See @ref asset_issuer_permission_flags
      uint16_t flags = 0;

      /// When a non-core asset is used to pay a fee, the blockchain must convert that asset to core asset in
      /// order to accept the fee. If this asset's fee pool is funded, the chain will automatically deposite fees
      /// in this asset to its accumulated fees, and withdraw from the fee pool the same amount as converted at
      /// the core exchange rate.
      price core_exchange_rate = price(asset(), asset(0, asset_id_type(1)));

      /// A set of accounts which maintain whitelists to consult for this asset. If whitelist_authorities
      /// is non-empty, then only accounts in whitelist_authorities are allowed to hold, use, or transfer the asset.
      flat_set<account_id_type> whitelist_authorities;
      /// A set of accounts which maintain blacklists to consult for this asset. If flags & white_list is set,
      /// an account may only send, receive, trade, etc. in this asset if none of these accounts appears in
      /// its account_object::blacklisting_accounts field. If the account is blacklisted, it may not transact in
      /// this asset even if it is also whitelisted.
      flat_set<account_id_type> blacklist_authorities;

      /** defines the assets that this asset may be traded against in the market */
      flat_set<asset_id_type>   whitelist_markets;
      /** defines the assets that this asset may not be traded against in the market, must not overlap whitelist */
      flat_set<asset_id_type>   blacklist_markets;

      /**
       * data that describes the meaning/purpose of this asset, fee will be charged proportional to
       * size of description.
       */
      string description;
      additional_asset_options_t extensions;

      /// Perform internal consistency checks.
      /// @throws fc::exception if any check fails
      void validate()const;
   };

   /**
    * @ingroup operations
    */
   struct asset_fund_fee_pool_operation : public base_operation
   {
      struct fee_parameters_type { uint64_t fee =  GRAPHENE_BLOCKCHAIN_PRECISION; };

      asset           fee; ///< core asset
      account_id_type from_account;
      asset_id_type   asset_id;
      share_type      amount; ///< core asset
      extensions_type extensions;

      account_id_type fee_payer()const { return from_account; }
      void       validate()const;
   };

   /**
    * @brief Update options common to all assets
    * @ingroup operations
    *
    * There are a number of options which all assets in the network use. These options are enumerated in the @ref
    * asset_options struct. This operation is used to update these options for an existing asset.
    *
    * @note This operation cannot be used to update BitAsset-specific options. For these options, use @ref
    * asset_update_bitasset_operation instead.
    *
    * @pre @ref issuer SHALL be an existing account and MUST match asset_object::issuer on @ref asset_to_update
    * @pre @ref fee SHALL be nonnegative, and @ref issuer MUST have a sufficient balance to pay it
    * @pre @ref new_options SHALL be internally consistent, as verified by @ref validate()
    * @post @ref asset_to_update will have options matching those of new_options
    */
   struct asset_update_operation : public base_operation
   {
      struct fee_parameters_type { 
         uint64_t fee            = 500 * GRAPHENE_BLOCKCHAIN_PRECISION;
         uint32_t price_per_kbyte = 10;
      };

      asset_update_operation(){}

      asset           fee;
      account_id_type issuer;
      asset_id_type   asset_to_update;

      /// If the asset is to be given a new issuer, specify his ID here.
      optional<account_id_type>   new_issuer;
      asset_options               new_options;
      extensions_type             extensions;

      account_id_type fee_payer()const { return issuer; }
      void            validate()const;
      share_type      calculate_fee(const fee_parameters_type& k)const;
   };

   /**
    * @brief used to take an asset out of circulation, returning to the issuer
    * @ingroup operations
    *
    * @note You cannot use this operation on market-issued assets.
    */
   struct asset_reserve_operation : public base_operation
   {
      struct fee_parameters_type { uint64_t fee = 20 * GRAPHENE_BLOCKCHAIN_PRECISION; };

      asset             fee;
      account_id_type   payer;
      asset             amount_to_reserve;
      extensions_type   extensions;

      account_id_type fee_payer()const { return payer; }
      void            validate()const;
   };

   /**
    * @brief used to transfer accumulated fees back to the issuer's balance.
    */
   struct asset_claim_fees_operation : public base_operation
   {
      struct fee_parameters_type {
         uint64_t fee = 20 * GRAPHENE_BLOCKCHAIN_PRECISION;
      };

      asset           fee;
      account_id_type issuer;
      asset           amount_to_claim; /// amount_to_claim.asset_id->issuer must == issuer
      extensions_type extensions;

      account_id_type fee_payer()const { return issuer; }
      void            validate()const;
   };

   /**
    * @brief Transfers TUSC from the fee pool of a specified asset back to the issuer's balance

    * @param fee Payment for the operation execution
    * @param issuer Account which will be used for transfering TUSC
    * @param asset_id Id of the asset whose fee pool is going to be drained
    * @param amount_to_claim Amount of TUSC to claim from the fee pool
    * @param extensions Field for future expansion

    * @pre @ref fee must be paid in the asset other than the one whose pool is being drained
    * @pre @ref amount_to_claim should be specified in the core asset
    * @pre @ref amount_to_claim should be nonnegative
    */
   struct asset_claim_pool_operation : public base_operation
   {
      struct fee_parameters_type {
         uint64_t fee = 20 * GRAPHENE_BLOCKCHAIN_PRECISION;
      };

      asset           fee;
      account_id_type issuer;
      asset_id_type   asset_id;        /// fee.asset_id must != asset_id
      asset           amount_to_claim; /// core asset
      extensions_type extensions;

      account_id_type fee_payer()const { return issuer; }
      void            validate()const;
   };

} } // graphene::chain

FC_REFLECT( graphene::chain::asset_claim_fees_operation, (fee)(issuer)(amount_to_claim)(extensions) )
FC_REFLECT( graphene::chain::asset_claim_fees_operation::fee_parameters_type, (fee) )
FC_REFLECT( graphene::chain::asset_claim_pool_operation, (fee)(issuer)(asset_id)(amount_to_claim)(extensions) )
FC_REFLECT( graphene::chain::asset_claim_pool_operation::fee_parameters_type, (fee) )

FC_REFLECT( graphene::chain::asset_options,
            (initial_max_supply)
            (market_fee_percent)
            (max_market_fee)
            (issuer_permissions)
            (flags)
            (core_exchange_rate)
            (whitelist_authorities)
            (blacklist_authorities)
            (whitelist_markets)
            (blacklist_markets)
            (description)
            (extensions)
          )

FC_REFLECT( graphene::chain::additional_asset_options, (reward_percent)(whitelist_market_fee_sharing) )
FC_REFLECT( graphene::chain::asset_fund_fee_pool_operation::fee_parameters_type, (fee) )
FC_REFLECT( graphene::chain::asset_update_operation::fee_parameters_type, (fee)(price_per_kbyte) )
FC_REFLECT( graphene::chain::asset_reserve_operation::fee_parameters_type, (fee) )

FC_REFLECT( graphene::chain::asset_update_operation,
            (fee)
            (issuer)
            (asset_to_update)
            (new_issuer)
            (new_options)
            (extensions)
          )

FC_REFLECT( graphene::chain::asset_reserve_operation,
            (fee)(payer)(amount_to_reserve)(extensions) )

FC_REFLECT( graphene::chain::asset_fund_fee_pool_operation, (fee)(from_account)(asset_id)(amount)(extensions) );
