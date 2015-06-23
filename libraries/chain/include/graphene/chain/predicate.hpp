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

#include <graphene/chain/types.hpp>

namespace graphene { namespace chain {

class database;

namespace pred {

/**
 *  Used to verify that account_id->name is equal to the given string literal.
 */
struct account_name_eq_lit
{
   account_id_type account_id;
   string          name;

   /**
    *  Perform state-independent checks.  Verify
    *  account_name is a valid account name.
    */
   bool validate()const;

   /**
    * Evaluate the predicate.
    */
   bool evaluate( const database& db )const;
};

/**
 *  Used to verify that asset_id->symbol is equal to the given string literal.
 */
struct asset_symbol_eq_lit
{
   asset_id_type   asset_id;
   string          symbol;

   /**
    *  Perform state independent checks.  Verify symbol is a
    *  valid asset symbol.
    */
   bool validate()const;

   /**
    * Evaluate the predicate.
    */
   bool evaluate( const database& db )const;
};

}

/**
 *  When defining predicates do not make the protocol dependent upon
 *  implementation details.
 */
typedef static_variant<
   pred::account_name_eq_lit,
   pred::asset_symbol_eq_lit
  > predicate;

} }

FC_REFLECT( graphene::chain::pred::account_name_eq_lit, (account_id)(name) )
FC_REFLECT( graphene::chain::pred::asset_symbol_eq_lit, (asset_id)(symbol) )
FC_REFLECT_TYPENAME( graphene::chain::predicate )
