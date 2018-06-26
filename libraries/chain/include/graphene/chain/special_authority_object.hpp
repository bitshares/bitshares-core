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
#include <graphene/chain/protocol/types.hpp>
#include <graphene/db/object.hpp>
#include <graphene/db/generic_index.hpp>

namespace graphene { namespace chain {

/**
 * special_authority_object only exists to help with a specific indexing problem.
 * We want to be able to iterate over all accounts that contain a special authority.
 * However, accounts which have a special_authority are very rare.  So rather
 * than indexing account_object by the special_authority fields (requiring additional
 * bookkeeping for every account), we instead maintain a special_authority_object
 * pointing to each account which has special_authority (requiring additional
 * bookkeeping only for every account which has special_authority).
 *
 * This class is an implementation detail.
 */

class special_authority_object : public graphene::db::abstract_object<special_authority_object>
{
   public:
      static const uint8_t space_id = implementation_ids;
      static const uint8_t type_id = impl_special_authority_object_type;

      account_id_type account;
};

struct by_account;

typedef multi_index_container<
   special_authority_object,
   indexed_by<
      ordered_unique< tag<by_id>, member< object, object_id_type, &object::id > >,
      ordered_unique< tag<by_account>, member< special_authority_object, account_id_type, &special_authority_object::account> >
   >
> special_authority_multi_index_type;

typedef generic_index< special_authority_object, special_authority_multi_index_type > special_authority_index;


/**
 * account_special_balance_object only exists to help with a specific indexing problem.
 * We want to be able to maintain top n holders of special assets, which are specified
 * by accounts with a special_authority.
 * However, as of writing, accounts which have a special_authority are very rare.
 * So rather than indexing account_balance_object by the asset_type and balance fields
 * (requiring additional bookkeeping for every balance), we instead maintain a
 * account_special_balance_object which is a copy of account_balance_object but only
 * for those special assets (requiring additional bookkeeping only for assets which
 * specified by accounts with special_authority).
 *
 * Note: although special_authority is rarely used in the system as of writing, it's
 *       possible that it will become popular at some time point in the future,
 *       then we need to re-visit this implementation.
 *
 * This class is an implementation detail.
 */

class account_special_balance_object : public abstract_object<account_special_balance_object>
{
   public:
      static const uint8_t space_id = implementation_ids;
      static const uint8_t type_id  = impl_account_special_balance_object_type;

      account_id_type   owner;
      asset_id_type     asset_type;
      share_type        balance;

      asset get_balance()const { return asset(balance, asset_type); }
};

struct by_account_asset;
struct by_asset_balance;

typedef multi_index_container<
   account_special_balance_object,
   indexed_by<
      ordered_unique< tag<by_id>, member< object, object_id_type, &object::id > >,
      ordered_unique< tag<by_account_asset>,
         composite_key<
            account_special_balance_object,
            member<account_special_balance_object, account_id_type, &account_special_balance_object::owner>,
            member<account_special_balance_object, asset_id_type, &account_special_balance_object::asset_type>
         >
      >,
      ordered_unique< tag<by_asset_balance>,
         composite_key<
            account_special_balance_object,
            member<account_special_balance_object, asset_id_type, &account_special_balance_object::asset_type>,
            member<account_special_balance_object, share_type, &account_special_balance_object::balance>,
            member<account_special_balance_object, account_id_type, &account_special_balance_object::owner>
         >,
         composite_key_compare<
            std::less< asset_id_type >,
            std::greater< share_type >,
            std::less< account_id_type >
         >
      >
   >
> account_special_balance_object_multi_index_type;

typedef generic_index<account_special_balance_object, account_special_balance_object_multi_index_type>
           account_special_balance_index;


/**
 * @brief Special assets meta object
 * @ingroup object
 * @ingroup implementation
 *
 * Meta object that stores info related to all special assets which are specified
 * by accounts with a special_authority.
 *
 * Note: as of writing, there are very few special assets.
 *       If quantity of special assets become large, it would be better to
 *       redesign this object.
 *
 * This is an implementation detail.
 */
class special_assets_meta_object : public abstract_object<special_assets_meta_object>
{
   public:
      static const uint8_t space_id = implementation_ids;
      static const uint8_t type_id  = impl_special_assets_meta_object_type;

      flat_set<asset_id_type> special_assets;
      flat_set<asset_id_type> special_assets_added_this_interval;
      flat_set<asset_id_type> special_assets_removed_this_interval;
};

} } // graphene::chain

FC_REFLECT_DERIVED(
   graphene::chain::special_authority_object,
   (graphene::db::object),
   (account)
)

FC_REFLECT_DERIVED(
   graphene::chain::account_special_balance_object, (graphene::db::object),
   (owner)(asset_type)(balance)
)

FC_REFLECT_DERIVED(
   graphene::chain::special_assets_meta_object, (graphene::db::object),
   (special_assets)
   (special_assets_added_this_interval)
   (special_assets_removed_this_interval)
)
