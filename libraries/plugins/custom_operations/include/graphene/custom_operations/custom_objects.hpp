/*
 * Copyright (c) 2019 oxarbitrage and contributors.
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

#include <boost/multi_index/composite_key.hpp>
#include <graphene/chain/database.hpp>

namespace graphene { namespace custom_operations {

using namespace chain;

#ifndef CUSTOM_OPERATIONS_SPACE_ID
#define CUSTOM_OPERATIONS_SPACE_ID 7
#endif

#define CUSTOM_OPERATIONS_MAX_KEY_SIZE (200)

enum types {
   account_map = 0
};

struct account_storage_object : public abstract_object<account_storage_object>
{
   static constexpr uint8_t space_id = CUSTOM_OPERATIONS_SPACE_ID;
   static constexpr uint8_t type_id  = account_map;

   account_id_type account;
   string catalog;
   string key;
   optional<variant> value;
};

struct by_account_catalog_key;

typedef multi_index_container<
      account_storage_object,
      indexed_by<
            ordered_unique< tag<by_id>, member< object, object_id_type, &object::id > >,
            ordered_unique< tag<by_account_catalog_key>,
                  composite_key< account_storage_object,
                        member< account_storage_object, account_id_type, &account_storage_object::account >,
                        member< account_storage_object, string, &account_storage_object::catalog >,
                        member< account_storage_object, string, &account_storage_object::key >
                  >
            >
      >
> account_storage_multi_index_type;

typedef generic_index<account_storage_object, account_storage_multi_index_type> account_storage_index;

using account_storage_id_type = object_id<CUSTOM_OPERATIONS_SPACE_ID, account_map>;

} } //graphene::custom_operations

FC_REFLECT_DERIVED( graphene::custom_operations::account_storage_object, (graphene::db::object),
                    (account)(catalog)(key)(value))
FC_REFLECT_ENUM( graphene::custom_operations::types, (account_map))
