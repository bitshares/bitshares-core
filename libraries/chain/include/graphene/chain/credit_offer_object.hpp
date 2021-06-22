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

#include <graphene/chain/types.hpp>
#include <graphene/db/generic_index.hpp>

#include <boost/multi_index/composite_key.hpp>

namespace graphene { namespace chain {

/**
 *  @brief A credit offer is a fund that can be used by other accounts who provide certain collateral.
 *  @ingroup object
 *  @ingroup protocol
 *
 */
class credit_offer_object : public abstract_object<credit_offer_object>
{
   public:
      static constexpr uint8_t space_id = protocol_ids;
      static constexpr uint8_t type_id  = credit_offer_object_type;

      account_id_type owner_account;            ///< Owner of the fund
      asset_id_type   asset_type;               ///< Asset type in the fund
      share_type      total_balance;            ///< Total size of the fund
      share_type      current_balance;          ///< Usable amount in the fund
      uint32_t        fee_rate = 0;             ///< Fee rate, the demominator is GRAPHENE_FEE_RATE_DENOM
      uint32_t        max_duration_seconds = 0; ///< The time limit that borrowed funds should be repaid
      share_type      min_deal_amount;          ///< Minimum amount to borrow for each new deal
      bool            enabled = false;          ///< Whether this offer is available
      time_point_sec  auto_disable_time;        ///< The time when this offer will be disabled automatically

      /// Types and rates of acceptable collateral
      flat_map<asset_id_type, price>          acceptable_collateral;

      /// Allowed borrowers and their maximum amounts to borrow. No limitation if empty.
      flat_map<account_id_type, share_type>   acceptable_borrowers;
};

struct by_auto_disable_time; // for protocol
struct by_owner;             // for API
struct by_asset_type;        // for API

/**
* @ingroup object_index
*/
using credit_offer_multi_index_type = multi_index_container<
   credit_offer_object,
   indexed_by<
      ordered_unique< tag<by_id>, member< object, object_id_type, &object::id > >,
      ordered_unique< tag<by_auto_disable_time>,
         composite_key< credit_offer_object,
            member< credit_offer_object, bool, &credit_offer_object::enabled >,
            member< credit_offer_object, time_point_sec, &credit_offer_object::auto_disable_time >,
            member< object, object_id_type, &object::id>
         >
      >,
      ordered_unique< tag<by_owner>,
         composite_key< credit_offer_object,
            member< credit_offer_object, account_id_type, &credit_offer_object::owner_account >,
            member< object, object_id_type, &object::id>
         >
      >,
      ordered_unique< tag<by_asset_type>,
         composite_key< credit_offer_object,
            member< credit_offer_object, asset_id_type, &credit_offer_object::asset_type >,
            member< object, object_id_type, &object::id>
         >
      >
   >
>;

/**
* @ingroup object_index
*/
using credit_offer_index = generic_index<credit_offer_object, credit_offer_multi_index_type>;


/**
 *  @brief A credit deal describes the details of a borrower's borrowing of funds from a credit offer.
 *  @ingroup object
 *  @ingroup protocol
 *
 */
class credit_deal_object : public abstract_object<credit_deal_object>
{
   public:
      static constexpr uint8_t space_id = protocol_ids;
      static constexpr uint8_t type_id  = credit_deal_object_type;

      account_id_type      borrower;           ///< Borrower
      credit_offer_id_type offer_id;           ///< ID of the credit offer
      account_id_type      offer_owner;        ///< Owner of the credit offer, redundant info for ease of querying
      asset_id_type        debt_asset;         ///< Asset type of the debt, redundant info for ease of querying
      share_type           debt_amount;        ///< How much funds borrowed
      asset_id_type        collateral_asset;   ///< Asset type of the collateral
      share_type           collateral_amount;  ///< How much funds in collateral
      uint32_t             fee_rate = 0;       ///< Fee rate, the demominator is GRAPHENE_FEE_RATE_DENOM
      time_point_sec       latest_repay_time;  ///< The deadline when the debt should be repaid
};

struct by_latest_repay_time; // for protocol
struct by_offer_id;          // for API
struct by_offer_owner;       // for API
struct by_borrower;          // for API
struct by_debt_asset;        // for API
struct by_collateral_asset;  // for API

/**
* @ingroup object_index
*/
using credit_deal_multi_index_type = multi_index_container<
   credit_deal_object,
   indexed_by<
      ordered_unique< tag<by_id>, member< object, object_id_type, &object::id > >,
      ordered_unique< tag<by_latest_repay_time>,
         composite_key< credit_deal_object,
            member< credit_deal_object, time_point_sec, &credit_deal_object::latest_repay_time >,
            member< object, object_id_type, &object::id>
         >
      >,
      ordered_unique< tag<by_offer_id>,
         composite_key< credit_deal_object,
            member< credit_deal_object, credit_offer_id_type, &credit_deal_object::offer_id >,
            member< object, object_id_type, &object::id>
         >
      >,
      ordered_unique< tag<by_offer_owner>,
         composite_key< credit_deal_object,
            member< credit_deal_object, account_id_type, &credit_deal_object::offer_owner >,
            member< object, object_id_type, &object::id>
         >
      >,
      ordered_unique< tag<by_borrower>,
         composite_key< credit_deal_object,
            member< credit_deal_object, account_id_type, &credit_deal_object::borrower >,
            member< object, object_id_type, &object::id>
         >
      >,
      ordered_unique< tag<by_debt_asset>,
         composite_key< credit_deal_object,
            member< credit_deal_object, asset_id_type, &credit_deal_object::debt_asset >,
            member< object, object_id_type, &object::id>
         >
      >,
      ordered_unique< tag<by_collateral_asset>,
         composite_key< credit_deal_object,
            member< credit_deal_object, asset_id_type, &credit_deal_object::collateral_asset >,
            member< object, object_id_type, &object::id>
         >
      >
   >
>;

/**
* @ingroup object_index
*/
using credit_deal_index = generic_index<credit_deal_object, credit_deal_multi_index_type>;


/**
 *  @brief A credit deal summary describes the summary of a borrower's borrowing of funds from a credit offer.
 *  @ingroup object
 *  @ingroup implementation
 *
 */
class credit_deal_summary_object : public abstract_object<credit_deal_summary_object>
{
   public:
      static constexpr uint8_t space_id = implementation_ids;
      static constexpr uint8_t type_id  = impl_credit_deal_summary_object_type;

      account_id_type      borrower;           ///< Borrower
      credit_offer_id_type offer_id;           ///< ID of the credit offer
      account_id_type      offer_owner;        ///< Owner of the credit offer, redundant info for ease of querying
      asset_id_type        debt_asset;         ///< Asset type of the debt, redundant info for ease of querying
      share_type           total_debt_amount;  ///< How much funds borrowed
};

struct by_offer_borrower;    // for protocol

/**
* @ingroup object_index
*/
using credit_deal_summary_multi_index_type = multi_index_container<
   credit_deal_summary_object,
   indexed_by<
      ordered_unique< tag<by_id>, member< object, object_id_type, &object::id > >,
      ordered_unique< tag<by_offer_borrower>,
         composite_key< credit_deal_summary_object,
            member< credit_deal_summary_object, credit_offer_id_type, &credit_deal_summary_object::offer_id >,
            member< credit_deal_summary_object, account_id_type, &credit_deal_summary_object::borrower >
         >
      >
   >
>;

/**
* @ingroup object_index
*/
using credit_deal_summary_index = generic_index<credit_deal_summary_object, credit_deal_summary_multi_index_type>;

} } // graphene::chain

MAP_OBJECT_ID_TO_TYPE( graphene::chain::credit_offer_object )
MAP_OBJECT_ID_TO_TYPE( graphene::chain::credit_deal_object )
MAP_OBJECT_ID_TO_TYPE( graphene::chain::credit_deal_summary_object )

FC_REFLECT_TYPENAME( graphene::chain::credit_offer_object )
FC_REFLECT_TYPENAME( graphene::chain::credit_deal_object )
FC_REFLECT_TYPENAME( graphene::chain::credit_deal_summary_object )

GRAPHENE_DECLARE_EXTERNAL_SERIALIZATION( graphene::chain::credit_offer_object )
GRAPHENE_DECLARE_EXTERNAL_SERIALIZATION( graphene::chain::credit_deal_object )
GRAPHENE_DECLARE_EXTERNAL_SERIALIZATION( graphene::chain::credit_deal_summary_object )
