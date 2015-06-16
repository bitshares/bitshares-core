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
#pragma once
#include <graphene/db/generic_index.hpp>
#include <graphene/chain/types.hpp>
#include <graphene/chain/authority.hpp>
#include <graphene/chain/asset.hpp>
#include <boost/multi_index/composite_key.hpp>
#include <fc/uint128.hpp>

namespace graphene { namespace chain {
   using namespace graphene::db;

  /**
   * @class call_order_object
   * @brief tracks debt and call price information
   *
   * There should only be one call_order_object per asset pair per account and
   * they will all have the same call price.
   */
  class call_order_object : public abstract_object<call_order_object>
  {
     public:
        static const uint8_t space_id = protocol_ids;
        static const uint8_t type_id  = call_order_object_type;

        asset get_collateral()const { return asset( collateral, call_price.base.asset_id ); }
        asset get_debt()const { return asset( debt, debt_type() ); }
        asset amount_to_receive()const { return get_debt(); }
        asset_id_type debt_type()const { return call_price.quote.asset_id; }
        price collateralization()const { return get_collateral() / get_debt(); }

        void update_call_price() { call_price = price::call_price(get_debt(), get_collateral(), maintenance_collateral_ratio); }

        account_id_type  borrower;
        share_type       collateral;  ///< call_price.base.asset_id, access via get_collateral
        share_type       debt;        ///< call_price.quote.asset_id, access via get_collateral
        price            call_price;
        uint16_t         maintenance_collateral_ratio;
  };

  /**
   *  @brief tracks bitassets scheduled for force settlement at some point in the future.
   *
   *  On the @ref settlement_date the @ref balance will be converted to the collateral asset
   *  and paid to @ref owner and then this object will be deleted.
   */
  class force_settlement_object : public graphene::db::annotated_object<force_settlement_object>
  {
     public:
        static const uint8_t space_id = protocol_ids;
        static const uint8_t type_id  = force_settlement_object_type;

        account_id_type   owner;
        asset             balance;
        time_point_sec    settlement_date;

        asset_id_type settlement_asset_id()const
        { return balance.asset_id; }
  };

   struct by_collateral;
   struct by_account;
   struct by_price;
   typedef multi_index_container<
      call_order_object,
      indexed_by<
         hashed_unique< tag<by_id>,
            member< object, object_id_type, &object::id > >,
         ordered_unique< tag<by_price>,
            composite_key< call_order_object,
               member< call_order_object, price, &call_order_object::call_price>,
               member< object, object_id_type, &object::id>
            >,
            composite_key_compare< std::less<price>, std::less<object_id_type> >
         >,
         ordered_unique< tag<by_account>,
            composite_key< call_order_object,
               member< call_order_object, account_id_type, &call_order_object::borrower >,
               const_mem_fun< call_order_object, asset_id_type, &call_order_object::debt_type>
            >
         >,
         ordered_unique< tag<by_collateral>,
            composite_key< call_order_object,
               const_mem_fun< call_order_object, price, &call_order_object::collateralization >,
               member< object, object_id_type, &object::id >
            >
         >
      >
   > call_order_multi_index_type;

   struct by_expiration;
   typedef multi_index_container<
      force_settlement_object,
      indexed_by<
         hashed_unique< tag<by_id>, member< object, object_id_type, &object::id > >,
         ordered_non_unique< tag<by_account>,
            member<force_settlement_object, account_id_type, &force_settlement_object::owner>
         >,
         ordered_non_unique< tag<by_expiration>,
            composite_key< force_settlement_object,
               const_mem_fun<force_settlement_object, asset_id_type, &force_settlement_object::settlement_asset_id>,
               member<force_settlement_object, time_point_sec, &force_settlement_object::settlement_date>
            >
         >
      >
   > force_settlement_object_multi_index_type;


  typedef generic_index<call_order_object, call_order_multi_index_type>                      call_order_index;
  typedef generic_index<force_settlement_object, force_settlement_object_multi_index_type>   force_settlement_index;
} } // graphene::chain


FC_REFLECT_DERIVED( graphene::chain::call_order_object, (graphene::db::object),
                    (borrower)(collateral)(debt)(call_price)(maintenance_collateral_ratio) )

FC_REFLECT( graphene::chain::force_settlement_object, (owner)(balance)(settlement_date) )
