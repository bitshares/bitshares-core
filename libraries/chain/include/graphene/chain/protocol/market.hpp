/*
 * Copyright (c) 2015 Cryptonomex, Inc., and contributors.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:
 *
 * 1. Any modified source or binaries are used only with the BitShares network.
 *
 * 2. Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
 *
 * 3. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */
#pragma once
#include <graphene/chain/protocol/base.hpp>

namespace graphene { namespace chain { 

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
   struct limit_order_create_operation : public base_operation
   {
      struct fee_parameters_type { uint64_t fee = 5 * GRAPHENE_BLOCKCHAIN_PRECISION; };

      asset           fee;
      account_id_type seller;
      asset           amount_to_sell;
      asset           min_to_receive;

      /// The order will be removed from the books if not filled by expiration
      /// Upon expiration, all unsold asset will be returned to seller
      time_point_sec expiration = time_point_sec::maximum();

      /// If this flag is set the entire order must be filled or the operation is rejected
      bool fill_or_kill = false;
      extensions_type   extensions;

      pair<asset_id_type,asset_id_type> get_market()const
      {
         return amount_to_sell.asset_id < min_to_receive.asset_id ?
                std::make_pair(amount_to_sell.asset_id, min_to_receive.asset_id) :
                std::make_pair(min_to_receive.asset_id, amount_to_sell.asset_id);
      }
      account_id_type fee_payer()const { return seller; }
      void            validate()const;
      price           get_price()const { return amount_to_sell / min_to_receive; }
   };


   /**
    *  @ingroup operations
    *  Used to cancel an existing limit order. Both fee_pay_account and the
    *  account to receive the proceeds must be the same as order->seller.
    *
    *  @return the amount actually refunded
    */
   struct limit_order_cancel_operation : public base_operation
   {
      struct fee_parameters_type { uint64_t fee = 0; };

      asset               fee;
      limit_order_id_type order;
      /** must be order->seller */
      account_id_type     fee_paying_account;
      extensions_type   extensions;

      account_id_type fee_payer()const { return fee_paying_account; }
      void            validate()const;
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
   struct call_order_update_operation : public base_operation
   {
      /** this is slightly more expensive than limit orders, this pricing impacts prediction markets */
      struct fee_parameters_type { uint64_t fee = 20 * GRAPHENE_BLOCKCHAIN_PRECISION; };

      asset               fee;
      account_id_type     funding_account; ///< pays fee, collateral, and cover
      asset               delta_collateral; ///< the amount of collateral to add to the margin position
      asset               delta_debt; ///< the amount of the debt to be paid off, may be negative to issue new debt
      extensions_type     extensions;

      account_id_type fee_payer()const { return funding_account; }
      void            validate()const;
   };

   /**
    * @ingroup operations
    *
    * @note This is a virtual operation that is created while matching orders and
    * emitted for the purpose of accurately tracking account history, accelerating
    * a reindex.
    */
   struct fill_order_operation : public base_operation
   {
      struct fee_parameters_type {};

      fill_order_operation(){}
      fill_order_operation( object_id_type o, account_id_type a, asset p, asset r, asset f )
         :order_id(o),account_id(a),pays(p),receives(r),fee(f){}

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
      void            validate()const { FC_ASSERT( !"virtual operation" ); }

      /// This is a virtual operation; there is no fee
      share_type      calculate_fee(const fee_parameters_type& k)const { return 0; }
   };

} } // graphene::chain

FC_REFLECT( graphene::chain::limit_order_create_operation::fee_parameters_type, (fee) )
FC_REFLECT( graphene::chain::limit_order_cancel_operation::fee_parameters_type, (fee) )
FC_REFLECT( graphene::chain::call_order_update_operation::fee_parameters_type, (fee) )
/// THIS IS THE ONLY VIRTUAL OPERATION THUS FAR... 
FC_REFLECT( graphene::chain::fill_order_operation::fee_parameters_type,  )


FC_REFLECT( graphene::chain::limit_order_create_operation,(fee)(seller)(amount_to_sell)(min_to_receive)(expiration)(fill_or_kill)(extensions))
FC_REFLECT( graphene::chain::limit_order_cancel_operation,(fee)(fee_paying_account)(order)(extensions) )
FC_REFLECT( graphene::chain::call_order_update_operation, (fee)(funding_account)(delta_collateral)(delta_debt)(extensions) )
FC_REFLECT( graphene::chain::fill_order_operation, (fee)(order_id)(account_id)(pays)(receives) )
