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

#include <memory>
#include <graphene/protocol/base.hpp>

namespace graphene { namespace protocol {
   struct fee_schedule;

   struct htlc_options
   {
      uint32_t max_timeout_secs;
      uint32_t max_preimage_size;
   };

   struct custom_authority_options_type
   {
      uint32_t max_custom_authority_lifetime_seconds = GRAPHENE_DEFAULT_MAX_CUSTOM_AUTHORITY_LIFETIME_SECONDS;
      uint32_t max_custom_authorities_per_account = GRAPHENE_DEFAULT_MAX_CUSTOM_AUTHORITIES_PER_ACCOUNT;
      uint32_t max_custom_authorities_per_account_op = GRAPHENE_DEFAULT_MAX_CUSTOM_AUTHORITIES_PER_ACCOUNT_OP;
      uint32_t max_custom_authority_restrictions = GRAPHENE_DEFAULT_MAX_CUSTOM_AUTHORITY_RESTRICTIONS;
   };

   struct chain_parameters
   {
      /** using a shared_ptr breaks the circular dependency created between operations and the fee schedule */
      std::shared_ptr<const fee_schedule> current_fees;                  ///< current schedule of fees
      const fee_schedule& get_current_fees() const { FC_ASSERT(current_fees); return *current_fees; }
      fee_schedule& get_mutable_fees() { FC_ASSERT(current_fees); return const_cast<fee_schedule&>(*current_fees); }

      uint8_t                 block_interval                      = GRAPHENE_DEFAULT_BLOCK_INTERVAL; ///< interval in seconds between blocks
      uint32_t                maintenance_interval                = GRAPHENE_DEFAULT_MAINTENANCE_INTERVAL; ///< interval in sections between blockchain maintenance events
      uint8_t                 maintenance_skip_slots              = GRAPHENE_DEFAULT_MAINTENANCE_SKIP_SLOTS; ///< number of block_intervals to skip at maintenance time
      uint32_t                committee_proposal_review_period    = GRAPHENE_DEFAULT_COMMITTEE_PROPOSAL_REVIEW_PERIOD_SEC; ///< minimum time in seconds that a proposed transaction requiring committee authority may not be signed, prior to expiration
      uint32_t                maximum_transaction_size            = GRAPHENE_DEFAULT_MAX_TRANSACTION_SIZE; ///< maximum allowable size in bytes for a transaction
      uint32_t                maximum_block_size                  = GRAPHENE_DEFAULT_MAX_BLOCK_SIZE; ///< maximum allowable size in bytes for a block
      uint32_t                maximum_time_until_expiration       = GRAPHENE_DEFAULT_MAX_TIME_UNTIL_EXPIRATION; ///< maximum lifetime in seconds for transactions to be valid, before expiring
      uint32_t                maximum_proposal_lifetime           = GRAPHENE_DEFAULT_MAX_PROPOSAL_LIFETIME_SEC; ///< maximum lifetime in seconds for proposed transactions to be kept, before expiring
      uint8_t                 maximum_asset_whitelist_authorities = GRAPHENE_DEFAULT_MAX_ASSET_WHITELIST_AUTHORITIES; ///< maximum number of accounts which an asset may list as authorities for its whitelist OR blacklist
      uint8_t                 maximum_asset_feed_publishers       = GRAPHENE_DEFAULT_MAX_ASSET_FEED_PUBLISHERS; ///< the maximum number of feed publishers for a given asset
      uint16_t                maximum_witness_count               = GRAPHENE_DEFAULT_MAX_WITNESSES; ///< maximum number of active witnesses
      uint16_t                maximum_committee_count             = GRAPHENE_DEFAULT_MAX_COMMITTEE; ///< maximum number of active committee_members
      uint16_t                maximum_authority_membership        = GRAPHENE_DEFAULT_MAX_AUTHORITY_MEMBERSHIP; ///< largest number of keys/accounts an authority can have
      uint16_t                reserve_percent_of_fee              = GRAPHENE_DEFAULT_BURN_PERCENT_OF_FEE; ///< the percentage of the network's allocation of a fee that is taken out of circulation
      uint16_t                network_percent_of_fee              = GRAPHENE_DEFAULT_NETWORK_PERCENT_OF_FEE; ///< percent of transaction fees paid to network
      uint16_t                lifetime_referrer_percent_of_fee    = GRAPHENE_DEFAULT_LIFETIME_REFERRER_PERCENT_OF_FEE; ///< percent of transaction fees paid to network
      uint32_t                cashback_vesting_period_seconds     = GRAPHENE_DEFAULT_CASHBACK_VESTING_PERIOD_SEC; ///< time after cashback rewards are accrued before they become liquid
      share_type              cashback_vesting_threshold          = GRAPHENE_DEFAULT_CASHBACK_VESTING_THRESHOLD; ///< the maximum cashback that can be received without vesting
      bool                    count_non_member_votes              = true; ///< set to false to restrict voting privlegages to member accounts
      bool                    allow_non_member_whitelists         = false; ///< true if non-member accounts may set whitelists and blacklists; false otherwise
      share_type              witness_pay_per_block               = GRAPHENE_DEFAULT_WITNESS_PAY_PER_BLOCK; ///< CORE to be allocated to witnesses (per block)
      uint32_t                witness_pay_vesting_seconds         = GRAPHENE_DEFAULT_WITNESS_PAY_VESTING_SECONDS; ///< vesting_seconds parameter for witness VBO's
      share_type              worker_budget_per_day               = GRAPHENE_DEFAULT_WORKER_BUDGET_PER_DAY; ///< CORE to be allocated to workers (per day)
      uint16_t                max_predicate_opcode                = GRAPHENE_DEFAULT_MAX_ASSERT_OPCODE; ///< predicate_opcode must be less than this number
      share_type              fee_liquidation_threshold           = GRAPHENE_DEFAULT_FEE_LIQUIDATION_THRESHOLD; ///< value in CORE at which accumulated fees in blockchain-issued market assets should be liquidated
      uint16_t                accounts_per_fee_scale              = GRAPHENE_DEFAULT_ACCOUNTS_PER_FEE_SCALE; ///< number of accounts between fee scalings
      uint8_t                 account_fee_scale_bitshifts         = GRAPHENE_DEFAULT_ACCOUNT_FEE_SCALE_BITSHIFTS; ///< number of times to left bitshift account registration fee at each scaling
      uint8_t                 max_authority_depth                 = GRAPHENE_MAX_SIG_CHECK_DEPTH;

      struct ext
      {
         optional< htlc_options > updatable_htlc_options;
         optional< custom_authority_options_type > custom_authority_options;
         optional< uint16_t > market_fee_network_percent;
         optional< uint16_t > maker_fee_discount_percent;
      };

      extension<ext> extensions;

      void validate()const;
      
      chain_parameters();
      chain_parameters(const chain_parameters& other);
      chain_parameters(chain_parameters&& other);
      chain_parameters& operator=(const chain_parameters& other);
      chain_parameters& operator=(chain_parameters&& other);

      /// If @ref market_fee_network_percent is valid, return the value it contains, otherwise return 0
      uint16_t get_market_fee_network_percent() const;

      /// If @ref maker_fee_discount_percent is valid, return the value it contains, otherwise return 0
      uint16_t get_maker_fee_discount_percent() const;

      private:
      static void safe_copy(chain_parameters& to, const chain_parameters& from);
   };

} }  // graphene::protocol

FC_REFLECT( graphene::protocol::htlc_options,
      (max_timeout_secs)
      (max_preimage_size)
)

FC_REFLECT( graphene::protocol::custom_authority_options_type,
      (max_custom_authority_lifetime_seconds)
      (max_custom_authorities_per_account)
      (max_custom_authorities_per_account_op)
      (max_custom_authority_restrictions)
)

FC_REFLECT( graphene::protocol::chain_parameters::ext,
      (updatable_htlc_options)
      (custom_authority_options)
      (market_fee_network_percent)
      (maker_fee_discount_percent)
)

FC_REFLECT( graphene::protocol::chain_parameters,
            (current_fees)
            (block_interval)
            (maintenance_interval)
            (maintenance_skip_slots)
            (committee_proposal_review_period)
            (maximum_transaction_size)
            (maximum_block_size)
            (maximum_time_until_expiration)
            (maximum_proposal_lifetime)
            (maximum_asset_whitelist_authorities)
            (maximum_asset_feed_publishers)
            (maximum_witness_count)
            (maximum_committee_count)
            (maximum_authority_membership)
            (reserve_percent_of_fee)
            (network_percent_of_fee)
            (lifetime_referrer_percent_of_fee)
            (cashback_vesting_period_seconds)
            (cashback_vesting_threshold)
            (count_non_member_votes)
            (allow_non_member_whitelists)
            (witness_pay_per_block)
            (worker_budget_per_day)
            (max_predicate_opcode)
            (fee_liquidation_threshold)
            (accounts_per_fee_scale)
            (account_fee_scale_bitshifts)
            (max_authority_depth)
            (extensions)
          )

GRAPHENE_DECLARE_EXTERNAL_SERIALIZATION( graphene::protocol::chain_parameters )
