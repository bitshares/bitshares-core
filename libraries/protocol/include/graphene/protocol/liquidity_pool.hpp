/*
 * Copyright (c) 2020 Abit More, and contributors.
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
#include <graphene/protocol/base.hpp>
#include <graphene/protocol/asset.hpp>

namespace graphene { namespace protocol {

   /**
    * @brief The pricing curve a liquidity pool uses
    * @ingroup operations
    */
   enum class liquidity_pool_curve_type : uint8_t
   {
      /// Constant-product curve x*y=k (the original, default behaviour)
      constant_product = 0,
      /// Curve/StableSwap curve, for assets expected to trade near parity
      stable = 1,
      /// Total number of defined curve types
      LP_CURVE_TYPE_COUNT = 2
   };

   /**
    * @brief Create a new liquidity pool
    * @ingroup operations
    */
   struct liquidity_pool_create_operation : public base_operation
   {
      /// Options that were added after the original operation shipped. Carried inside the
      /// operation's typed @c extensions field so the binary format stays backward compatible.
      struct ext
      {
         /// Pricing curve for the pool. Absent => constant_product. Requires the StableSwap hardfork.
         fc::optional<uint8_t>  pool_type;
         /// Amplification coefficient A. Required for, and only valid with, a stable pool_type.
         fc::optional<uint64_t> amplification;
      };

      struct fee_params_t { uint64_t fee = 50 * GRAPHENE_BLOCKCHAIN_PRECISION; };

      asset           fee;                         ///< Operation fee
      account_id_type account;                     ///< The account who creates the liquidity pool
      asset_id_type   asset_a;                     ///< Type of the first asset in the pool
      asset_id_type   asset_b;                     ///< Type of the second asset in the pool
      asset_id_type   share_asset;                 ///< Type of the share asset aka the LP token
      uint16_t        taker_fee_percent = 0;       ///< Taker fee percent
      uint16_t        withdrawal_fee_percent = 0;  ///< Withdrawal fee percent

      extension<ext>  extensions;  ///< Extensions

      account_id_type fee_payer()const { return account; }
      void            validate()const;
   };

   /**
    * @brief Delete a liquidity pool
    * @ingroup operations
    */
   struct liquidity_pool_delete_operation : public base_operation
   {
      struct fee_params_t { uint64_t fee = 0; };

      asset                    fee;                ///< Operation fee
      account_id_type          account;            ///< The account who owns the liquidity pool
      liquidity_pool_id_type   pool;               ///< ID of the liquidity pool

      extensions_type extensions;  ///< Unused. Reserved for future use.

      account_id_type fee_payer()const { return account; }
      void            validate()const;
   };

   /**
    * @brief Update a liquidity pool
    * @ingroup operations
    */
   struct liquidity_pool_update_operation : public base_operation
   {
      struct fee_params_t { uint64_t fee = 1 * GRAPHENE_BLOCKCHAIN_PRECISION; };

      asset                    fee;                ///< Operation fee
      account_id_type          account;            ///< The account who owns the liquidity pool
      liquidity_pool_id_type   pool;               ///< ID of the liquidity pool
      optional<uint16_t>       taker_fee_percent;       ///< Taker fee percent
      optional<uint16_t>       withdrawal_fee_percent;  ///< Withdrawal fee percent

      extensions_type extensions;  ///< Unused. Reserved for future use.

      account_id_type fee_payer()const { return account; }
      void            validate()const;
   };

   /**
    * @brief Deposit to a liquidity pool
    * @ingroup operations
    */
   struct liquidity_pool_deposit_operation : public base_operation
   {
      struct fee_params_t { uint64_t fee = GRAPHENE_BLOCKCHAIN_PRECISION / 10; };

      asset                    fee;                ///< Operation fee
      account_id_type          account;            ///< The account who deposits to the liquidity pool
      liquidity_pool_id_type   pool;               ///< ID of the liquidity pool
      asset                    amount_a;           ///< The amount of the first asset to deposit
      asset                    amount_b;           ///< The amount of the second asset to deposit

      extensions_type extensions;  ///< Unused. Reserved for future use.

      account_id_type fee_payer()const { return account; }
      void            validate()const;
   };

   /**
    * @brief Withdraw from a liquidity pool
    * @ingroup operations
    */
   struct liquidity_pool_withdraw_operation : public base_operation
   {
      struct fee_params_t { uint64_t fee = 5 * GRAPHENE_BLOCKCHAIN_PRECISION; };

      asset                    fee;                ///< Operation fee
      account_id_type          account;            ///< The account who withdraws from the liquidity pool
      liquidity_pool_id_type   pool;               ///< ID of the liquidity pool
      asset                    share_amount;       ///< The amount of the share asset to use

      /// Options added after the operation shipped, carried in the typed extension so the
      /// binary format stays what it was for every withdrawal that does not use them.
      struct ext
      {
         /**
          * Take the whole withdrawal in ONE asset instead of both.
          *
          * Only meaningful for a stable pool: the invariant is what lets the pool quote a
          * single-sided exit at all. Withdrawing one side moves the pool away from balance,
          * so it pays the same imbalance fee a one-sided deposit does -- otherwise depositing
          * one side and withdrawing the other would be a swap that skipped the trading fee,
          * which is exactly what the deposit fee exists to prevent.
          */
         fc::optional<asset_id_type> withdraw_one_asset;
      };

      extension<ext> extensions;  ///< Unused. Reserved for future use.

      account_id_type fee_payer()const { return account; }
      void            validate()const;
   };

   /**
    * @brief Exchange with a liquidity pool
    * @ingroup operations
    * @note The result of this operation is a @ref generic_exchange_operation_result.
    *       There are 3 fees in the result, stored in this order:
    *       * maker market fee
    *       * taker market fee
    *       * liquidity pool taker fee
    */
   struct liquidity_pool_exchange_operation : public base_operation
   {
      struct fee_params_t { uint64_t fee = 1 * GRAPHENE_BLOCKCHAIN_PRECISION; };

      asset                    fee;                ///< Operation fee
      account_id_type          account;            ///< The account who exchanges with the liquidity pool
      liquidity_pool_id_type   pool;               ///< ID of the liquidity pool
      asset                    amount_to_sell;     ///< The amount of one asset type to sell
      asset                    min_to_receive;     ///< The minimum amount of the other asset type to receive

      extensions_type extensions;  ///< Unused. Reserved for future use.

      account_id_type fee_payer()const { return account; }
      void            validate()const;
   };

} } // graphene::protocol

FC_REFLECT_ENUM( graphene::protocol::liquidity_pool_curve_type,
                 (constant_product)(stable)(LP_CURVE_TYPE_COUNT) )

FC_REFLECT( graphene::protocol::liquidity_pool_create_operation::ext,
            (pool_type)(amplification) )

FC_REFLECT( graphene::protocol::liquidity_pool_create_operation::fee_params_t, (fee) )
FC_REFLECT( graphene::protocol::liquidity_pool_delete_operation::fee_params_t, (fee) )
FC_REFLECT( graphene::protocol::liquidity_pool_update_operation::fee_params_t, (fee) )
FC_REFLECT( graphene::protocol::liquidity_pool_deposit_operation::fee_params_t, (fee) )
FC_REFLECT( graphene::protocol::liquidity_pool_withdraw_operation::fee_params_t, (fee) )
FC_REFLECT( graphene::protocol::liquidity_pool_exchange_operation::fee_params_t, (fee) )

FC_REFLECT( graphene::protocol::liquidity_pool_create_operation,
            (fee)(account)(asset_a)(asset_b)(share_asset)
            (taker_fee_percent)(withdrawal_fee_percent)(extensions) )
FC_REFLECT( graphene::protocol::liquidity_pool_delete_operation,
            (fee)(account)(pool)(extensions) )
FC_REFLECT( graphene::protocol::liquidity_pool_update_operation,
            (fee)(account)(pool)(taker_fee_percent)(withdrawal_fee_percent)(extensions) )
FC_REFLECT( graphene::protocol::liquidity_pool_deposit_operation,
            (fee)(account)(pool)(amount_a)(amount_b)(extensions) )
FC_REFLECT( graphene::protocol::liquidity_pool_withdraw_operation::ext, (withdraw_one_asset) )
FC_REFLECT_TYPENAME(
      graphene::protocol::extension<graphene::protocol::liquidity_pool_withdraw_operation::ext> )
FC_REFLECT( graphene::protocol::liquidity_pool_withdraw_operation,
            (fee)(account)(pool)(share_amount)(extensions) )
FC_REFLECT( graphene::protocol::liquidity_pool_exchange_operation,
            (fee)(account)(pool)(amount_to_sell)(min_to_receive)(extensions) )

GRAPHENE_DECLARE_EXTERNAL_SERIALIZATION( graphene::protocol::liquidity_pool_create_operation::fee_params_t )
GRAPHENE_DECLARE_EXTERNAL_SERIALIZATION( graphene::protocol::liquidity_pool_delete_operation::fee_params_t )
GRAPHENE_DECLARE_EXTERNAL_SERIALIZATION( graphene::protocol::liquidity_pool_update_operation::fee_params_t )
GRAPHENE_DECLARE_EXTERNAL_SERIALIZATION( graphene::protocol::liquidity_pool_deposit_operation::fee_params_t )
GRAPHENE_DECLARE_EXTERNAL_SERIALIZATION( graphene::protocol::liquidity_pool_withdraw_operation::fee_params_t )
GRAPHENE_DECLARE_EXTERNAL_SERIALIZATION( graphene::protocol::liquidity_pool_exchange_operation::fee_params_t )

GRAPHENE_DECLARE_EXTERNAL_SERIALIZATION( graphene::protocol::liquidity_pool_create_operation )
GRAPHENE_DECLARE_EXTERNAL_SERIALIZATION( graphene::protocol::liquidity_pool_delete_operation )
GRAPHENE_DECLARE_EXTERNAL_SERIALIZATION( graphene::protocol::liquidity_pool_update_operation )
GRAPHENE_DECLARE_EXTERNAL_SERIALIZATION( graphene::protocol::liquidity_pool_deposit_operation )
GRAPHENE_DECLARE_EXTERNAL_SERIALIZATION( graphene::protocol::liquidity_pool_withdraw_operation )
GRAPHENE_DECLARE_EXTERNAL_SERIALIZATION( graphene::protocol::liquidity_pool_exchange_operation )
