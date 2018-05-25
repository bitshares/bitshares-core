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
#include <graphene/chain/database.hpp>
#include <graphene/chain/protocol/base.hpp>

namespace graphene { namespace chain {

   class crowdfund_object : public abstract_object<crowdfund_object>
   {
      public:
         static const uint8_t space_id = protocol_ids;
         static const uint8_t type_id  = crowdfund_object_type;


         account_id_type owner;
         asset_id_type   asset_id;
         time_point_sec  begin;
         uint32_t        t;
         uint32_t        u;
         share_type      V;

         double  p(uint32_t s) const;
         asset_id_type asset_type()const { return asset_id; }
   };

   struct by_owner;

   /**
    * @ingroup object_index
    */
   using crowdfund_multi_index_type = multi_index_container<
      crowdfund_object,
      indexed_by<
         ordered_unique< tag<by_id>, member< object, object_id_type, &object::id > >,
         ordered_non_unique< tag<by_owner>, composite_key<
            crowdfund_object,
            member<crowdfund_object, account_id_type, &crowdfund_object::owner>,
            const_mem_fun<crowdfund_object, asset_id_type, &crowdfund_object::asset_type>
         > >
      >
   >;

   /**
    * @ingroup object_index
    */
   using crowdfund_index = generic_index<crowdfund_object, crowdfund_multi_index_type>;
} }

FC_REFLECT_DERIVED( graphene::chain::crowdfund_object, (graphene::db::object),
                    (owner)(asset_id)(begin)(t) (u)(V))
