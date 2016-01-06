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
#include <graphene/chain/evaluator.hpp>
#include <graphene/db/object.hpp>
#include <graphene/db/generic_index.hpp>

namespace graphene { namespace chain {

/**
 * @class blinded_balance_object
 * @brief tracks a blinded balance commitment
 * @ingroup object
 * @ingroup protocol
 */
class blinded_balance_object : public graphene::db::abstract_object<blinded_balance_object>
{
   public:
      static const uint8_t space_id = implementation_ids;
      static const uint8_t type_id  = impl_blinded_balance_object_type;

      fc::ecc::commitment_type                commitment;
      asset_id_type                           asset_id;
      authority                               owner;
};

struct by_asset;
struct by_owner;
struct by_commitment;

/**
 * @ingroup object_index
 */
typedef multi_index_container<
   blinded_balance_object,
   indexed_by<
      ordered_unique< tag<by_id>, member< object, object_id_type, &object::id > >,
      ordered_unique< tag<by_commitment>, member<blinded_balance_object, commitment_type, &blinded_balance_object::commitment> >
   >
> blinded_balance_object_multi_index_type;
typedef generic_index<blinded_balance_object, blinded_balance_object_multi_index_type> blinded_balance_index;


class transfer_to_blind_evaluator : public evaluator<transfer_to_blind_evaluator>
{
   public:
      typedef transfer_to_blind_operation operation_type;

      void_result do_evaluate( const transfer_to_blind_operation& o );
      void_result do_apply( const transfer_to_blind_operation& o ) ;
};

class transfer_from_blind_evaluator : public evaluator<transfer_from_blind_evaluator>
{
   public:
      typedef transfer_from_blind_operation operation_type;

      void_result do_evaluate( const transfer_from_blind_operation& o );
      void_result do_apply( const transfer_from_blind_operation& o ) ;
};

class blind_transfer_evaluator : public evaluator<blind_transfer_evaluator>
{
   public:
      typedef blind_transfer_operation operation_type;

      void_result do_evaluate( const blind_transfer_operation& o );
      void_result do_apply( const blind_transfer_operation& o ) ;
};

} } // namespace graphene::chain

FC_REFLECT_DERIVED( graphene::chain::blinded_balance_object, (graphene::db::object), (commitment)(asset_id)(owner) )
