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
#include <graphene/chain/asset.hpp>
#include <graphene/db/object.hpp>

namespace graphene { namespace chain {
   using namespace graphene::db;

   class witness_object;

   class witness_object : public abstract_object<witness_object>
   {
      public:
         static const uint8_t space_id = protocol_ids;
         static const uint8_t type_id = witness_object_type;

         account_id_type                witness_account;
         key_id_type                    signing_key;
         secret_hash_type               next_secret;
         secret_hash_type               last_secret;
         share_type                     accumulated_income;
         vote_id_type                   vote_id;

         witness_object() : vote_id(vote_id_type::witness) {}
   };

} } // graphene::chain

FC_REFLECT_DERIVED( graphene::chain::witness_object, (graphene::db::object),
                    (witness_account)
                    (signing_key)
                    (next_secret)
                    (last_secret)
                    (accumulated_income)
                    (vote_id) )

