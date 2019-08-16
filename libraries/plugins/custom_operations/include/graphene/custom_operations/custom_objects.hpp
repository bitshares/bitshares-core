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

enum types {
   account_contact = 0,
   create_htlc = 1,
   take_htlc = 2
};
enum blockchains {
   eos = 0,
   bitcoin = 1,
   ripple = 2,
   ethereum = 3
};

struct account_contact_object : public abstract_object<account_contact_object>
{
   static const uint8_t space_id = CUSTOM_OPERATIONS_SPACE_ID;
   static const uint8_t type_id  = account_contact;

   account_id_type account;
   optional<string> name;
   optional<string> email;
   optional<string> phone;
   optional<string> address;
   optional<string> company;
   optional<string> url;
};

struct htlc_order_object : public abstract_object<htlc_order_object>
{
   static const uint8_t space_id = CUSTOM_OPERATIONS_SPACE_ID;
   static const uint8_t type_id  = create_htlc;

   account_id_type bitshares_account;
   asset bitshares_amount;
   blockchains blockchain;
   string blockchain_account;
   string blockchain_asset;
   string blockchain_amount;
   fc::time_point_sec expiration;
   fc::time_point_sec order_time;
   bool active;

   optional<uint32_t> blockchain_asset_precision;
   optional<string> token_contract;
   optional<string> tag;
   optional<account_id_type> taker_bitshares_account;
   optional<string> taker_blockchain_account;
   optional<fc::time_point_sec> close_time;
};

struct by_custom_id;
struct by_custom_account;
typedef multi_index_container<
      account_contact_object,
      indexed_by<
            ordered_non_unique< tag<by_custom_id>, member< object, object_id_type, &object::id > >,
            ordered_unique< tag<by_custom_account>,
                  member< account_contact_object, account_id_type, &account_contact_object::account > >
      >
> account_contact_multi_index_type;

typedef generic_index<account_contact_object, account_contact_multi_index_type> account_contact_index;

struct by_bitshares_account;
struct by_active;
typedef multi_index_container<
      htlc_order_object,
      indexed_by<
            ordered_non_unique< tag<by_custom_id>, member< object, object_id_type, &object::id > >,
            ordered_unique< tag<by_bitshares_account>,
                  composite_key< htlc_order_object,
                        member< htlc_order_object, account_id_type, &htlc_order_object::bitshares_account >,
                        member< object, object_id_type, &object::id >
                  >
            >,
            ordered_unique< tag<by_active>,
                  composite_key< htlc_order_object,
                        member< htlc_order_object, bool, &htlc_order_object::active >,
                        member< htlc_order_object, fc::time_point_sec, &htlc_order_object::expiration >,
                        member< object, object_id_type, &object::id >
                  >
            >
      >
> htlc_orderbook_multi_index_type;

typedef generic_index<htlc_order_object, htlc_orderbook_multi_index_type> htlc_orderbook_index;

using account_contact_id_type = object_id<CUSTOM_OPERATIONS_SPACE_ID, account_contact>;
using htlc_order_id_type = object_id<CUSTOM_OPERATIONS_SPACE_ID, create_htlc>;

} } //graphene::custom_operations


FC_REFLECT_DERIVED( graphene::custom_operations::account_contact_object, (graphene::db::object),
                    (account)(name)(email)(phone)(address)(company)(url))
FC_REFLECT_DERIVED( graphene::custom_operations::htlc_order_object, (graphene::db::object),
                    (bitshares_account)(bitshares_amount)(blockchain)(blockchain_account)(blockchain_asset)
                    (blockchain_amount)(expiration)(order_time)(active)
                    (blockchain_asset_precision)(token_contract)(tag)(taker_bitshares_account)
                    (taker_blockchain_account)(close_time))
FC_REFLECT_ENUM( graphene::custom_operations::types, (account_contact)(create_htlc)(take_htlc) )
FC_REFLECT_ENUM( graphene::custom_operations::blockchains, (eos)(bitcoin)(ripple)(ethereum) )
