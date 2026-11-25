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

#include <graphene/protocol/authority.hpp>
#include <graphene/protocol/types.hpp>
#include <graphene/chain/types.hpp>

#include <graphene/db/generic_index.hpp>

#include <boost/multi_index/composite_key.hpp>

#include <fc/crypto/elliptic.hpp>

namespace graphene { namespace chain {

/**
 * @class blinded_balance_object
 * @brief tracks a blinded balance commitment
 * @ingroup object
 * @ingroup protocol
 */
class blinded_balance_object : public graphene::db::abstract_object<blinded_balance_object,
                                         implementation_ids, impl_blinded_balance_object_type>
{
   public:
      fc::ecc::commitment_type                commitment;
      asset_id_type                           asset_id;
      authority                               owner;
};

struct by_commitment;
struct by_asset_commitment;

/**
 * @ingroup object_index
 */
using blinded_balance_obj_multi_idx = multi_index_container<
   blinded_balance_object,
   indexed_by<
      ordered_unique< tag<by_id>, member< object, object_id_type, &object::id > >,
      ordered_unique< tag<by_commitment>,
         member<blinded_balance_object, commitment_type, &blinded_balance_object::commitment> >,
      ordered_unique< tag<by_asset_commitment>,
         composite_key< blinded_balance_object,
            member< blinded_balance_object, asset_id_type, &blinded_balance_object::asset_id >,
            member< blinded_balance_object, commitment_type, &blinded_balance_object::commitment >
         >
      >
   >
>;
using blinded_balance_index = generic_index<blinded_balance_object, blinded_balance_obj_multi_idx>;

/**
 * A key image that has already been published by a ring spend.
 * The commitment itself stays in blinded_balance_index so it can be a decoy.
 */
class key_image_object : public graphene::db::abstract_object<key_image_object,
                                    implementation_ids, impl_key_image_object_type>
{
   public:
      fc::ecc::commitment_type   key_image;
      asset_id_type              asset_id;
};

struct by_key_image;

using key_image_multi_index_type = multi_index_container<
   key_image_object,
   indexed_by<
      ordered_unique< tag<by_id>, member< object, object_id_type, &object::id > >,
      ordered_unique< tag<by_key_image>,
         member< key_image_object, commitment_type, &key_image_object::key_image > >
   >
>;
using key_image_index = generic_index<key_image_object, key_image_multi_index_type>;

} } // graphene::chain

MAP_OBJECT_ID_TO_TYPE(graphene::chain::blinded_balance_object)
MAP_OBJECT_ID_TO_TYPE(graphene::chain::key_image_object)

FC_REFLECT_TYPENAME( graphene::chain::blinded_balance_object )
FC_REFLECT_TYPENAME( graphene::chain::key_image_object )

GRAPHENE_DECLARE_EXTERNAL_SERIALIZATION( graphene::chain::blinded_balance_object )
GRAPHENE_DECLARE_EXTERNAL_SERIALIZATION( graphene::chain::key_image_object )
