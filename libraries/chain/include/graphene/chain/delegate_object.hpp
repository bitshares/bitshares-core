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
#include <graphene/db/generic_index.hpp>

namespace graphene { namespace chain {
   using namespace graphene::db;

   class account_object;

   /**
    *  @brief tracks information about a delegate account.
    *  @ingroup object
    *
    *  A delegate is responsible for setting blockchain parameters and has
    *  dynamic multi-sig control over the genesis account.  The current set of
    *  active delegates has control.
    *
    *  Delegates were separated into a separate object to make iterating over
    *  the set of delegate easy.
    */
   class delegate_object : public abstract_object<delegate_object>
   {
      public:
         static const uint8_t space_id = protocol_ids;
         static const uint8_t type_id  = delegate_object_type;

         account_id_type  delegate_account;
         vote_id_type     vote_id;
         string           url;
   };

   struct by_account;
   using delegate_multi_index_type = multi_index_container<
      delegate_object,
      indexed_by<
         ordered_unique< tag<by_id>,
            member<object, object_id_type, &object::id>
         >,
         hashed_unique< tag<by_account>,
            member<delegate_object, account_id_type, &delegate_object::delegate_account>
         >
      >
   >;
   using delegate_index = generic_index<delegate_object, delegate_multi_index_type>;
} } // graphene::chain

FC_REFLECT_DERIVED( graphene::chain::delegate_object, (graphene::db::object),
                    (delegate_account)(vote_id)(url) )
