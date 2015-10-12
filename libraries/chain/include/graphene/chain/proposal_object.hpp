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

#include <graphene/chain/protocol/transaction.hpp>
#include <graphene/chain/transaction_evaluation_state.hpp>

#include <graphene/db/generic_index.hpp>

namespace graphene { namespace chain {


/**
 *  @brief tracks the approval of a partially approved transaction 
 *  @ingroup object
 *  @ingroup protocol
 */
class proposal_object : public abstract_object<proposal_object>
{
   public:
      static const uint8_t space_id = protocol_ids;
      static const uint8_t type_id = proposal_object_type;

      time_point_sec                expiration_time;
      optional<time_point_sec>      review_period_time;
      transaction                   proposed_transaction;
      flat_set<account_id_type>     required_active_approvals;
      flat_set<account_id_type>     available_active_approvals;
      flat_set<account_id_type>     required_owner_approvals;
      flat_set<account_id_type>     available_owner_approvals;
      flat_set<public_key_type>     available_key_approvals;

      bool is_authorized_to_execute(database& db)const;
};

/**
 *  @brief tracks all of the proposal objects that requrie approval of
 *  an individual account.   
 *
 *  @ingroup object
 *  @ingroup protocol
 *
 *  This is a secondary index on the proposal_index
 *
 *  @note the set of required approvals is constant
 */
class required_approval_index : public secondary_index
{
   public:
      virtual void object_inserted( const object& obj ) override;
      virtual void object_removed( const object& obj ) override;
      virtual void about_to_modify( const object& before ) override{};
      virtual void object_modified( const object& after  ) override{};

      void remove( account_id_type a, proposal_id_type p );

      map<account_id_type, set<proposal_id_type> > _account_to_proposals;
};

struct by_expiration{};
typedef boost::multi_index_container<
   proposal_object,
   indexed_by<
      ordered_unique< tag< by_id >, member< object, object_id_type, &object::id > >,
      ordered_non_unique< tag< by_expiration >, member< proposal_object, time_point_sec, &proposal_object::expiration_time > >
   >
> proposal_multi_index_container;
typedef generic_index<proposal_object, proposal_multi_index_container> proposal_index;

} } // graphene::chain

FC_REFLECT_DERIVED( graphene::chain::proposal_object, (graphene::chain::object),
                    (expiration_time)(review_period_time)(proposed_transaction)(required_active_approvals)
                    (available_active_approvals)(required_owner_approvals)(available_owner_approvals)
                    (available_key_approvals) )
