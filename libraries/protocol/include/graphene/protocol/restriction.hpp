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
#include <graphene/protocol/base.hpp>
#include <graphene/protocol/types.hpp>

namespace graphene { namespace protocol {

/**
  * Defines the set of valid operation restritions as a discriminated union type.
  */
struct restriction {
   enum function_type {
      func_eq,
      func_ne,
      func_lt,
      func_le,
      func_gt,
      func_ge,
      func_in,
      func_not_in,
      func_has_all,
      func_has_none,
      func_attr,
      func_logical_or,
      func_variant_assert,
      FUNCTION_TYPE_COUNT ///< Sentry value which contains the number of different types
   };

   // A variant assertion argument is a pair of the tag expected to be in the variant, and the restrictions to apply
   // to the value
   using variant_assert_argument_type = pair<int64_t, vector<restriction>>;

#define GRAPHENE_OP_RESTRICTION_ARGUMENTS_VARIADIC \
   /*  0 */ void_t, \
   /*  1 */ bool, \
   /*  2 */ int64_t, \
   /*  3 */ string, \
   /*  4 */ time_point_sec, \
   /*  5 */ public_key_type, \
   /*  6 */ fc::sha256, \
   /*  7 */ account_id_type, \
   /*  8 */ asset_id_type, \
   /*  9 */ force_settlement_id_type, \
   /* 10 */ committee_member_id_type, \
   /* 11 */ witness_id_type, \
   /* 12 */ limit_order_id_type, \
   /* 13 */ call_order_id_type, \
   /* 14 */ custom_id_type, \
   /* 15 */ proposal_id_type, \
   /* 16 */ withdraw_permission_id_type, \
   /* 17 */ vesting_balance_id_type, \
   /* 18 */ worker_id_type, \
   /* 19 */ balance_id_type, \
   /* 20 */ flat_set<bool>, \
   /* 21 */ flat_set<int64_t>, \
   /* 22 */ flat_set<string>, \
   /* 23 */ flat_set<time_point_sec>, \
   /* 24 */ flat_set<public_key_type>, \
   /* 25 */ flat_set<fc::sha256>, \
   /* 26 */ flat_set<account_id_type>, \
   /* 27 */ flat_set<asset_id_type>, \
   /* 28 */ flat_set<force_settlement_id_type>, \
   /* 29 */ flat_set<committee_member_id_type>, \
   /* 30 */ flat_set<witness_id_type>, \
   /* 31 */ flat_set<limit_order_id_type>, \
   /* 32 */ flat_set<call_order_id_type>, \
   /* 33 */ flat_set<custom_id_type>, \
   /* 34 */ flat_set<proposal_id_type>, \
   /* 35 */ flat_set<withdraw_permission_id_type>, \
   /* 36 */ flat_set<vesting_balance_id_type>, \
   /* 37 */ flat_set<worker_id_type>, \
   /* 38 */ flat_set<balance_id_type>, \
   /* 39 */ vector<restriction>, \
   /* 40 */ vector<vector<restriction>>, \
   /* 41 */ variant_assert_argument_type

   using argument_type = fc::static_variant<GRAPHENE_OP_RESTRICTION_ARGUMENTS_VARIADIC>;

   unsigned_int member_index;
   unsigned_int restriction_type;
   argument_type argument;

   extensions_type extensions;

   restriction() = default;
   restriction(const unsigned_int& member_index, function_type type, const argument_type& argument)
      : member_index(member_index), restriction_type(type), argument(argument) {}

   static size_t restriction_count(const vector<restriction>& restrictions);
   size_t restriction_count() const;
};

} } // graphene::protocol

FC_REFLECT_ENUM(graphene::protocol::restriction::function_type,
                (func_eq)
                (func_ne)
                (func_lt)
                (func_le)
                (func_gt)
                (func_ge)
                (func_in)
                (func_not_in)
                (func_has_all)
                (func_has_none)
                (func_attr)
                (func_logical_or)
                (func_variant_assert)
                (FUNCTION_TYPE_COUNT))

FC_REFLECT(graphene::protocol::restriction,
           (member_index)
           (restriction_type)
           (argument)
           (extensions))


