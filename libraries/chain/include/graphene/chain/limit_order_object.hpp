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
#include <graphene/db/object.hpp>
#include <graphene/chain/authority.hpp>
#include <graphene/chain/asset.hpp>
#include <graphene/db/generic_index.hpp>
#include <boost/multi_index/composite_key.hpp>

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

        asset amount_for_sale()const   { return asset( for_sale, sell_price.base.asset_id ); }
        asset amount_to_receive()const { return amount_for_sale() * sell_price; }
  };

  struct by_id;
  struct by_price;
  struct by_expiration;
  typedef multi_index_container<
     limit_order_object,
     indexed_by<
        hashed_unique< tag<by_id>,
           member< object, object_id_type, &object::id > >,
        ordered_non_unique< tag<by_expiration>, member< limit_order_object, time_point_sec, &limit_order_object::expiration> >,
        ordered_unique< tag<by_price>,
           composite_key< limit_order_object,
              member< limit_order_object, price, &limit_order_object::sell_price>,
              member< object, object_id_type, &object::id>
           >,
           composite_key_compare< std::greater<price>, std::less<object_id_type> >
        >
     >
  > limit_order_multi_index_type;

  typedef generic_index<limit_order_object, limit_order_multi_index_type> limit_order_index;

} }

FC_REFLECT_DERIVED( graphene::chain::limit_order_object,
                    (graphene::db::object),
                    (expiration)(seller)(for_sale)(sell_price)
                  )

