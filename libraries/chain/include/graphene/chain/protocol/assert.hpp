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
#include <graphene/chain/protocol/base.hpp>

namespace graphene { namespace chain { 

   /**
    *  Used to verify that account_id->name is equal to the given string literal.
    */
   struct account_name_eq_lit_predicate
   {
      account_id_type account_id;
      string          name;

      /**
       *  Perform state-independent checks.  Verify
       *  account_name is a valid account name.
       */
      bool validate()const;
   };

   /**
    *  Used to verify that asset_id->symbol is equal to the given string literal.
    */
   struct asset_symbol_eq_lit_predicate
   {
      asset_id_type   asset_id;
      string          symbol;

      /**
       *  Perform state independent checks.  Verify symbol is a
       *  valid asset symbol.
       */
      bool validate()const;

   };

   /**
    * Used to verify that a specific block is part of the
    * blockchain history.  This helps protect some high-value
    * transactions to newly created IDs
    *
    * The block ID must be within the last 2^16 blocks.
    */
   struct block_id_predicate
   {
      block_id_type id;
      bool validate()const{ return true; }
   };

   /**
    *  When defining predicates do not make the protocol dependent upon
    *  implementation details.
    */
   typedef static_variant<
      account_name_eq_lit_predicate,
      asset_symbol_eq_lit_predicate,
      block_id_predicate
     > predicate;


   /**
    *  @brief assert that some conditions are true.
    *  @ingroup operations
    *
    *  This operation performs no changes to the database state, but can but used to verify
    *  pre or post conditions for other operations.
    */
   struct assert_operation : public base_operation
   {
      struct fee_parameters_type { uint64_t fee = GRAPHENE_BLOCKCHAIN_PRECISION; };

      asset                      fee;
      account_id_type            fee_paying_account;
      vector<predicate>          predicates;
      flat_set<account_id_type>  required_auths;
      extensions_type            extensions;

      account_id_type fee_payer()const { return fee_paying_account; }
      void            validate()const;
      share_type      calculate_fee(const fee_parameters_type& k)const;
   };

} } // graphene::chain

FC_REFLECT( graphene::chain::assert_operation::fee_parameters_type, (fee) )
FC_REFLECT( graphene::chain::account_name_eq_lit_predicate, (account_id)(name) )
FC_REFLECT( graphene::chain::asset_symbol_eq_lit_predicate, (asset_id)(symbol) )
FC_REFLECT( graphene::chain::block_id_predicate, (id) )
FC_REFLECT_TYPENAME( graphene::chain::predicate )
FC_REFLECT( graphene::chain::assert_operation, (fee)(fee_paying_account)(predicates)(required_auths)(extensions) )
 
