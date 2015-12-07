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
#include <graphene/chain/protocol/operations.hpp>
#include <graphene/chain/evaluator.hpp>
#include <graphene/chain/database.hpp>

namespace graphene { namespace chain {

  using namespace graphene::db;

  /**
   *  @brief an offer to sell a amount of a asset at a specified exchange rate by a certain time
   *  @ingroup object
   *  @ingroup protocol
   *  @ingroup market
   *
   *  This limit_order_objects are indexed by @ref expiration and is automatically deleted on the first block after expiration.
   */
  class limit_order_object : public abstract_object<limit_order_object>
  {
     public:
        static const uint8_t space_id = protocol_ids;
        static const uint8_t type_id  = limit_order_object_type;

        time_point_sec   expiration;
        account_id_type  seller;
        share_type       for_sale; ///< asset id is sell_price.base.asset_id
        price            sell_price;
        share_type       deferred_fee;

        pair<asset_id_type,asset_id_type> get_market()const
        {
           auto tmp = std::make_pair( sell_price.base.asset_id, sell_price.quote.asset_id );
           if( tmp.first > tmp.second ) std::swap( tmp.first, tmp.second );
           return tmp;
        }

        asset amount_for_sale()const   { return asset( for_sale, sell_price.base.asset_id ); }
        asset amount_to_receive()const { return amount_for_sale() * sell_price; }
  };

  struct by_id;
  struct by_price;
  struct by_expiration;
  struct by_account;
  typedef multi_index_container<
     limit_order_object,
     indexed_by<
        ordered_unique< tag<by_id>,
           member< object, object_id_type, &object::id > >,
        ordered_non_unique< tag<by_expiration>, member< limit_order_object, time_point_sec, &limit_order_object::expiration> >,
        ordered_unique< tag<by_price>,
           composite_key< limit_order_object,
              member< limit_order_object, price, &limit_order_object::sell_price>,
              member< object, object_id_type, &object::id>
           >,
           composite_key_compare< std::greater<price>, std::less<object_id_type> >
        >,
        ordered_non_unique< tag<by_account>, member<limit_order_object, account_id_type, &limit_order_object::seller>>
     >
  > limit_order_multi_index_type;

  typedef generic_index<limit_order_object, limit_order_multi_index_type> limit_order_index;

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

        account_id_type  borrower;
        share_type       collateral;  ///< call_price.base.asset_id, access via get_collateral
        share_type       debt;        ///< call_price.quote.asset_id, access via get_collateral
        price            call_price;  ///< Debt / Collateral
  };

  /**
   *  @brief tracks bitassets scheduled for force settlement at some point in the future.
   *
   *  On the @ref settlement_date the @ref balance will be converted to the collateral asset
   *  and paid to @ref owner and then this object will be deleted.
   */
  class force_settlement_object : public abstract_object<force_settlement_object>
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
         ordered_unique< tag<by_id>,
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
         ordered_unique< tag<by_id>, member< object, object_id_type, &object::id > >,
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




   class limit_order_create_evaluator : public evaluator<limit_order_create_evaluator>
   {
      public:
         typedef limit_order_create_operation operation_type;

         void_result do_evaluate( const limit_order_create_operation& o );
         object_id_type do_apply( const limit_order_create_operation& o );

         asset calculate_market_fee( const asset_object* aobj, const asset& trade_amount );

         /** override the default behavior defined by generic_evalautor which is to
          * post the fee to fee_paying_account_stats.pending_fees
          */
         virtual void pay_fee() override;

         share_type                          _deferred_fee  = 0;
         const limit_order_create_operation* _op            = nullptr;
         const account_object*               _seller        = nullptr;
         const asset_object*                 _sell_asset    = nullptr;
         const asset_object*                 _receive_asset = nullptr;
   };

   class limit_order_cancel_evaluator : public evaluator<limit_order_cancel_evaluator>
   {
      public:
         typedef limit_order_cancel_operation operation_type;

         void_result do_evaluate( const limit_order_cancel_operation& o );
         asset do_apply( const limit_order_cancel_operation& o );

         const limit_order_object* _order;
   };

   class call_order_update_evaluator : public evaluator<call_order_update_evaluator>
   {
      public:
         typedef call_order_update_operation operation_type;

         void_result do_evaluate( const call_order_update_operation& o );
         void_result do_apply( const call_order_update_operation& o );

         bool _closing_order = false;
         const asset_object* _debt_asset = nullptr;
         const account_object* _paying_account = nullptr;
         const call_order_object* _order = nullptr;
         const asset_bitasset_data_object* _bitasset_data = nullptr;
   };

} } // graphene::chain

FC_REFLECT_DERIVED( graphene::chain::limit_order_object,
                    (graphene::db::object),
                    (expiration)(seller)(for_sale)(sell_price)(deferred_fee)
                  )

FC_REFLECT_DERIVED( graphene::chain::call_order_object, (graphene::db::object),
                    (borrower)(collateral)(debt)(call_price) )

FC_REFLECT( graphene::chain::force_settlement_object, (owner)(balance)(settlement_date) )
