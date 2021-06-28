/*
 * Copyright (c) 2021 Abit More, and contributors.
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
    * @brief Create a new credit offer
    * @ingroup operations
    *
    * A credit offer is a fund that can be used by other accounts who provide certain collateral.
    */
   struct credit_offer_create_operation : public base_operation
   {
      struct fee_parameters_type {
         uint64_t fee             = 1 * GRAPHENE_BLOCKCHAIN_PRECISION;
         uint32_t price_per_kbyte = 1 * GRAPHENE_BLOCKCHAIN_PRECISION;
      };

      asset           fee;                   ///< Operation fee
      account_id_type owner_account;         ///< Owner of the credit offer
      asset_id_type   asset_type;            ///< Asset type in the credit offer
      share_type      balance;               ///< Usable amount in the credit offer
      uint32_t        fee_rate = 0;          ///< Fee rate, the demominator is GRAPHENE_FEE_RATE_DENOM
      uint32_t        max_duration_seconds = 0; ///< The time limit that borrowed funds should be repaid
      share_type      min_deal_amount;          ///< Minimum amount to borrow for each new deal
      bool            enabled = false;          ///< Whether this offer is available
      time_point_sec  auto_disable_time;        ///< The time when this offer will be disabled automatically

      /// Types and rates of acceptable collateral
      flat_map<asset_id_type, price>          acceptable_collateral;

      /// Allowed borrowers and their maximum amounts to borrow. No limitation if empty.
      flat_map<account_id_type, share_type>   acceptable_borrowers;

      extensions_type extensions;  ///< Unused. Reserved for future use.

      account_id_type fee_payer()const { return owner_account; }
      void            validate()const override;
      share_type      calculate_fee(const fee_parameters_type& k)const;
   };

   /**
    * @brief Delete a credit offer
    * @ingroup operations
    */
   struct credit_offer_delete_operation : public base_operation
   {
      struct fee_parameters_type { uint64_t fee = 0; };

      asset                fee;                ///< Operation fee
      account_id_type      owner_account;      ///< The account who owns the credit offer
      credit_offer_id_type offer_id;           ///< ID of the credit offer

      extensions_type extensions;  ///< Unused. Reserved for future use.

      account_id_type fee_payer()const { return owner_account; }
      void            validate()const override;
   };

   /**
    * @brief Update a credit offer
    * @ingroup operations
    */
   struct credit_offer_update_operation : public base_operation
   {
      struct fee_parameters_type {
         uint64_t fee             = 1 * GRAPHENE_BLOCKCHAIN_PRECISION;
         uint32_t price_per_kbyte = 1 * GRAPHENE_BLOCKCHAIN_PRECISION;
      };

      asset                    fee;                   ///< Operation fee
      account_id_type          owner_account;         ///< Owner of the credit offer
      credit_offer_id_type     offer_id;              ///< ID of the credit offer
      optional<asset>          delta_amount;          ///< Delta amount, optional
      optional<uint32_t>       fee_rate;              ///< New fee rate, optional
      optional<uint32_t>       max_duration_seconds;  ///< New repayment time limit, optional
      optional<share_type>     min_deal_amount;       ///< Minimum amount to borrow for each new deal, optional
      optional<bool>           enabled;               ///< Whether this offer is available, optional
      optional<time_point_sec> auto_disable_time;     ///< New time to disable automatically, optional

      /// New types and rates of acceptable collateral, optional
      optional<flat_map<asset_id_type, price>>          acceptable_collateral;

      /// New allowed borrowers and their maximum amounts to borrow, optional
      optional<flat_map<account_id_type, share_type>>   acceptable_borrowers;

      extensions_type extensions;  ///< Unused. Reserved for future use.

      account_id_type fee_payer()const { return owner_account; }
      void            validate()const override;
      share_type      calculate_fee(const fee_parameters_type& k)const;
   };

   /**
    * @brief Accept a creadit offer and create a credit deal
    * @ingroup operations
    */
   struct credit_offer_accept_operation : public base_operation
   {
      struct fee_parameters_type { uint64_t fee = 1 * GRAPHENE_BLOCKCHAIN_PRECISION; };

      asset                    fee;                ///< Operation fee
      account_id_type          borrower;           ///< The account who accepts the offer
      credit_offer_id_type     offer_id;           ///< ID of the credit offer
      asset                    borrow_amount;      ///< The amount to borrow
      asset                    collateral;         ///< The collateral

      extensions_type extensions;  ///< Unused. Reserved for future use.

      account_id_type fee_payer()const { return borrower; }
      void            validate()const override;
   };

   /**
    * @brief Repay a credit deal
    * @ingroup operations
    */
   struct credit_deal_repay_operation : public base_operation
   {
      struct fee_parameters_type { uint64_t fee = 1 * GRAPHENE_BLOCKCHAIN_PRECISION; };

      asset                    fee;                ///< Operation fee
      account_id_type          account;            ///< The account who repays to the credit offer
      credit_deal_id_type      deal_id;            ///< ID of the credit deal
      asset                    repay_amount;       ///< The amount to repay
      asset                    credit_fee;         ///< The credit fee relative to the amount to repay

      extensions_type extensions;  ///< Unused. Reserved for future use.

      account_id_type fee_payer()const { return account; }
      void            validate()const override;
   };

   /**
    * @brief A credit deal expired without being fully repaid
    * @ingroup operations
    * @note This is a virtual operation.
    */
   struct credit_deal_expired_operation : public base_operation
   {
      struct fee_parameters_type {};

      credit_deal_expired_operation() = default;

      credit_deal_expired_operation( const credit_deal_id_type& did, const credit_offer_id_type& oid,
            const account_id_type& o, const account_id_type& b, const asset& u, const asset& c, const uint32_t fr)
      : deal_id(did), offer_id(oid), offer_owner(o), borrower(b), unpaid_amount(u), collateral(c), fee_rate(fr)
      { /* Nothing to do */ }

      asset                    fee;                ///< Only for compatibility, unused
      credit_deal_id_type      deal_id;            ///< ID of the credit deal
      credit_offer_id_type     offer_id;           ///< ID of the credit offer
      account_id_type          offer_owner;        ///< Owner of the credit offer
      account_id_type          borrower;           ///< The account who repays to the credit offer
      asset                    unpaid_amount;      ///< The amount that is unpaid
      asset                    collateral;         ///< The collateral liquidated
      uint32_t                 fee_rate = 0;       ///< Fee rate, the demominator is GRAPHENE_FEE_RATE_DENOM

      account_id_type fee_payer()const { return borrower; }
      void            validate()const override { FC_ASSERT( !"virtual operation" ); }

      /// This is a virtual operation; there is no fee
      share_type      calculate_fee(const fee_parameters_type&)const { return 0; }
   };

} } // graphene::protocol

FC_REFLECT( graphene::protocol::credit_offer_create_operation::fee_parameters_type, (fee)(price_per_kbyte) )
FC_REFLECT( graphene::protocol::credit_offer_delete_operation::fee_parameters_type, (fee) )
FC_REFLECT( graphene::protocol::credit_offer_update_operation::fee_parameters_type, (fee)(price_per_kbyte) )
FC_REFLECT( graphene::protocol::credit_offer_accept_operation::fee_parameters_type, (fee) )
FC_REFLECT( graphene::protocol::credit_deal_repay_operation::fee_parameters_type, (fee) )
FC_REFLECT( graphene::protocol::credit_deal_expired_operation::fee_parameters_type, ) // VIRTUAL

FC_REFLECT( graphene::protocol::credit_offer_create_operation,
            (fee)
            (owner_account)
            (asset_type)
            (balance)
            (fee_rate)
            (max_duration_seconds)
            (min_deal_amount)
            (enabled)
            (auto_disable_time)
            (acceptable_collateral)
            (acceptable_borrowers)
            (extensions)
          )

FC_REFLECT( graphene::protocol::credit_offer_delete_operation,
            (fee)
            (owner_account)
            (offer_id)
            (extensions)
          )

FC_REFLECT( graphene::protocol::credit_offer_update_operation,
            (fee)
            (owner_account)
            (offer_id)
            (delta_amount)
            (fee_rate)
            (max_duration_seconds)
            (min_deal_amount)
            (enabled)
            (auto_disable_time)
            (acceptable_collateral)
            (acceptable_borrowers)
            (extensions)
          )

FC_REFLECT( graphene::protocol::credit_offer_accept_operation,
            (fee)
            (borrower)
            (offer_id)
            (borrow_amount)
            (collateral)
            (extensions)
          )

FC_REFLECT( graphene::protocol::credit_deal_repay_operation,
            (fee)
            (account)
            (deal_id)
            (repay_amount)
            (credit_fee)
            (extensions)
          )

FC_REFLECT( graphene::protocol::credit_deal_expired_operation,
            (fee)
            (deal_id)
            (offer_id)
            (offer_owner)
            (borrower)
            (unpaid_amount)
            (collateral)
            (fee_rate)
          )

GRAPHENE_DECLARE_EXTERNAL_SERIALIZATION( graphene::protocol::credit_offer_create_operation::fee_parameters_type )
GRAPHENE_DECLARE_EXTERNAL_SERIALIZATION( graphene::protocol::credit_offer_delete_operation::fee_parameters_type )
GRAPHENE_DECLARE_EXTERNAL_SERIALIZATION( graphene::protocol::credit_offer_update_operation::fee_parameters_type )
GRAPHENE_DECLARE_EXTERNAL_SERIALIZATION( graphene::protocol::credit_offer_accept_operation::fee_parameters_type )
GRAPHENE_DECLARE_EXTERNAL_SERIALIZATION( graphene::protocol::credit_deal_repay_operation::fee_parameters_type )
// Note: credit_deal_expired_operation is virtual so no external serialization for its fee_parameters_type

GRAPHENE_DECLARE_EXTERNAL_SERIALIZATION( graphene::protocol::credit_offer_create_operation )
GRAPHENE_DECLARE_EXTERNAL_SERIALIZATION( graphene::protocol::credit_offer_delete_operation )
GRAPHENE_DECLARE_EXTERNAL_SERIALIZATION( graphene::protocol::credit_offer_update_operation )
GRAPHENE_DECLARE_EXTERNAL_SERIALIZATION( graphene::protocol::credit_offer_accept_operation )
GRAPHENE_DECLARE_EXTERNAL_SERIALIZATION( graphene::protocol::credit_deal_repay_operation )
GRAPHENE_DECLARE_EXTERNAL_SERIALIZATION( graphene::protocol::credit_deal_expired_operation )
