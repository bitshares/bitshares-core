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
    * @brief Create a new SameT Fund object
    * @ingroup operations
    *
    * A SameT Fund is a fund which can be used by a borrower and have to be repaid in the same transaction.
    */
   struct samet_fund_create_operation : public base_operation
   {
      struct fee_parameters_type { uint64_t fee = 1 * GRAPHENE_BLOCKCHAIN_PRECISION; };

      asset           fee;                   ///< Operation fee
      account_id_type owner_account;         ///< Owner of the fund
      asset_id_type   asset_type;            ///< Asset type in the fund
      share_type      balance;               ///< Usable amount in the fund
      uint32_t        fee_rate = 0;          ///< Fee rate, the demominator is GRAPHENE_FEE_RATE_DENOM

      extensions_type extensions;  ///< Unused. Reserved for future use.

      account_id_type fee_payer()const { return owner_account; }
      void            validate()const override;
   };

   /**
    * @brief Delete a SameT Fund object
    * @ingroup operations
    */
   struct samet_fund_delete_operation : public base_operation
   {
      struct fee_parameters_type { uint64_t fee = 0; };

      asset                fee;                ///< Operation fee
      account_id_type      owner_account;      ///< The account who owns the SameT Fund object
      samet_fund_id_type   fund_id;            ///< ID of the SameT Fund object

      extensions_type extensions;  ///< Unused. Reserved for future use.

      account_id_type fee_payer()const { return owner_account; }
      void            validate()const override;
   };

   /**
    * @brief Update a SameT Fund object
    * @ingroup operations
    */
   struct samet_fund_update_operation : public base_operation
   {
      struct fee_parameters_type { uint64_t fee = 1 * GRAPHENE_BLOCKCHAIN_PRECISION; };

      asset                fee;                   ///< Operation fee
      account_id_type      owner_account;         ///< Owner of the fund
      samet_fund_id_type   fund_id;               ///< ID of the SameT Fund object
      optional<asset>      delta_amount;          ///< Delta amount, optional
      optional<uint32_t>   new_fee_rate = 0;   ///< New fee rate, optional

      extensions_type extensions;  ///< Unused. Reserved for future use.

      account_id_type fee_payer()const { return owner_account; }
      void            validate()const override;
   };

   /**
    * @brief Borrow from a SameT Fund
    * @ingroup operations
    */
   struct samet_fund_borrow_operation : public base_operation
   {
      struct fee_parameters_type { uint64_t fee = 1 * GRAPHENE_BLOCKCHAIN_PRECISION; };

      asset                    fee;                ///< Operation fee
      account_id_type          borrower;           ///< The account who borrows from the fund
      samet_fund_id_type       fund_id;            ///< ID of the SameT Fund
      asset                    borrow_amount;      ///< The amount to borrow

      extensions_type extensions;  ///< Unused. Reserved for future use.

      account_id_type fee_payer()const { return borrower; }
      void            validate()const override;
   };

   /**
    * @brief Repay to a SameT Fund
    * @ingroup operations
    */
   struct samet_fund_repay_operation : public base_operation
   {
      struct fee_parameters_type { uint64_t fee = 1 * GRAPHENE_BLOCKCHAIN_PRECISION; };

      asset                    fee;                ///< Operation fee
      account_id_type          account;            ///< The account who repays to the SameT Fund
      samet_fund_id_type       fund_id;            ///< ID of the SameT Fund
      asset                    repay_amount;       ///< The amount to repay
      asset                    fund_fee;           ///< Fee for using the fund

      extensions_type extensions;  ///< Unused. Reserved for future use.

      account_id_type fee_payer()const { return account; }
      void            validate()const override;
   };

} } // graphene::protocol

FC_REFLECT( graphene::protocol::samet_fund_create_operation::fee_parameters_type, (fee) )
FC_REFLECT( graphene::protocol::samet_fund_delete_operation::fee_parameters_type, (fee) )
FC_REFLECT( graphene::protocol::samet_fund_update_operation::fee_parameters_type, (fee) )
FC_REFLECT( graphene::protocol::samet_fund_borrow_operation::fee_parameters_type, (fee) )
FC_REFLECT( graphene::protocol::samet_fund_repay_operation::fee_parameters_type, (fee) )

FC_REFLECT( graphene::protocol::samet_fund_create_operation,
            (fee)(owner_account)(asset_type)(balance)(fee_rate)(extensions) )
FC_REFLECT( graphene::protocol::samet_fund_delete_operation,
            (fee)(owner_account)(fund_id)(extensions) )
FC_REFLECT( graphene::protocol::samet_fund_update_operation,
            (fee)(owner_account)(fund_id)(delta_amount)(new_fee_rate)(extensions) )
FC_REFLECT( graphene::protocol::samet_fund_borrow_operation,
            (fee)(borrower)(fund_id)(borrow_amount)(extensions) )
FC_REFLECT( graphene::protocol::samet_fund_repay_operation,
            (fee)(account)(fund_id)(repay_amount)(fund_fee)(extensions) )

GRAPHENE_DECLARE_EXTERNAL_SERIALIZATION( graphene::protocol::samet_fund_create_operation::fee_parameters_type )
GRAPHENE_DECLARE_EXTERNAL_SERIALIZATION( graphene::protocol::samet_fund_delete_operation::fee_parameters_type )
GRAPHENE_DECLARE_EXTERNAL_SERIALIZATION( graphene::protocol::samet_fund_update_operation::fee_parameters_type )
GRAPHENE_DECLARE_EXTERNAL_SERIALIZATION( graphene::protocol::samet_fund_borrow_operation::fee_parameters_type )
GRAPHENE_DECLARE_EXTERNAL_SERIALIZATION( graphene::protocol::samet_fund_repay_operation::fee_parameters_type )

GRAPHENE_DECLARE_EXTERNAL_SERIALIZATION( graphene::protocol::samet_fund_create_operation )
GRAPHENE_DECLARE_EXTERNAL_SERIALIZATION( graphene::protocol::samet_fund_delete_operation )
GRAPHENE_DECLARE_EXTERNAL_SERIALIZATION( graphene::protocol::samet_fund_update_operation )
GRAPHENE_DECLARE_EXTERNAL_SERIALIZATION( graphene::protocol::samet_fund_borrow_operation )
GRAPHENE_DECLARE_EXTERNAL_SERIALIZATION( graphene::protocol::samet_fund_repay_operation )
