/*
 * Copyright (c) 2018 Abit More, and contributors.
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
#include <graphene/chain/protocol/authority.hpp>
#include <graphene/chain/protocol/custom_authority.hpp>
#include <graphene/db/generic_index.hpp>
#include <boost/multi_index/composite_key.hpp>

namespace graphene { namespace chain {

   /**
    * @brief Tracks account custom authorities
    * @ingroup object
    *
    */
   class custom_authority_object : public abstract_object<custom_authority_object>
   {
      public:
         static const uint8_t space_id = implementation_ids;
         static const uint8_t type_id  = impl_custom_authority_object_type;

         account_id_type                 account;
         uint32_t                        custom_id;
         bool                            enabled;
         time_point_sec                  valid_from;
         time_point_sec                  valid_to;
         unsigned_int                    operation_type;
         authority                       auth;
         vector<restriction>             restrictions;
   };

   struct by_account_custom;

   /**
    * @ingroup object_index
    */
   typedef multi_index_container<
      custom_authority_object,
      indexed_by<
         ordered_unique< tag<by_id>, member< object, object_id_type, &object::id > >,
         ordered_unique< tag<by_account_custom>,
            composite_key<
               custom_authority_object,
               member<custom_authority_object, account_id_type, &custom_authority_object::account>,
               member<custom_authority_object, uint32_t, &custom_authority_object::custom_id>
            >
         >
      >
   > custom_authority_multi_index_type;

   /**
    * @ingroup object_index
    */
   typedef generic_index<custom_authority_object, custom_authority_multi_index_type> custom_authority_index;

} } // graphene::chain

FC_REFLECT_DERIVED( graphene::chain::custom_authority_object,
                    (graphene::db::object),
                    (account)
                    (custom_id)
                    (enabled)
                    (valid_from)
                    (valid_to)
                    (operation_type)
                    (auth)
                    (restrictions)
                  )
