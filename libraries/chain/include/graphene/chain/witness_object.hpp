/*
 * Copyright (c) 2015, Cryptonomex, Inc.
 * All rights reserved.
 *
 * This source code is provided for evaluation in private test networks only, until September 8, 2015. After this date, this license expires and
 * the code may not be used, modified or distributed for any purpose. Redistribution and use in source and binary forms, with or without modification,
 * are permitted until September 8, 2015, provided that the following conditions are met:
 *
 * 1. The code and/or derivative works are used only for private test networks consisting of no more than 10 P2P nodes.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#pragma once
#include <graphene/chain/protocol/asset.hpp>
#include <graphene/db/object.hpp>
#include <graphene/db/generic_index.hpp>

namespace graphene { namespace chain {
   using namespace graphene::db;

   class witness_object;

   class witness_object : public abstract_object<witness_object>
   {
      public:
         static const uint8_t space_id = protocol_ids;
         static const uint8_t type_id = witness_object_type;

         account_id_type  witness_account;
         uint64_t         last_aslot = 0;
         public_key_type  signing_key;
         optional< vesting_balance_id_type > pay_vb;
         vote_id_type     vote_id;
         uint64_t         total_votes = 0;
         string           url;

         witness_object() : vote_id(vote_id_type::witness) {}
   };

   struct by_account;
   struct by_vote_id;
   using witness_multi_index_type = multi_index_container<
      witness_object,
      indexed_by<
         hashed_unique< tag<by_id>,
            member<object, object_id_type, &object::id>
         >,
         hashed_unique< tag<by_account>,
            member<witness_object, account_id_type, &witness_object::witness_account>
         >,
         hashed_unique< tag<by_vote_id>,
            member<witness_object, vote_id_type, &witness_object::vote_id>
         >
      >
   >;
   using witness_index = generic_index<witness_object, witness_multi_index_type>;
} } // graphene::chain

FC_REFLECT_DERIVED( graphene::chain::witness_object, (graphene::db::object),
                    (witness_account)
                    (last_aslot)
                    (signing_key)
                    (pay_vb)
                    (vote_id)
                    (total_votes)
                    (url) )
