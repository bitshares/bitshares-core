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
