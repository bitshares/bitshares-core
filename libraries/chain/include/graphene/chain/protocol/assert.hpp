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
    *  When defining predicates do not make the protocol dependent upon
    *  implementation details.
    */
   typedef static_variant<
      account_name_eq_lit_predicate,
      asset_symbol_eq_lit_predicate
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
FC_REFLECT_TYPENAME( graphene::chain::predicate )
FC_REFLECT( graphene::chain::assert_operation, (fee)(fee_paying_account)(predicates)(required_auths)(extensions) )
