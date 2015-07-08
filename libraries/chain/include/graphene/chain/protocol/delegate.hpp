#pragma once
#include <graphene/chain/protocol/base.hpp>
#include <graphene/chain/protocol/chain_parameters.hpp>

namespace graphene { namespace chain { 

   /**
    * @brief Create a delegate object, as a bid to hold a delegate seat on the network.
    * @ingroup operations
    *
    * Accounts which wish to become delegates may use this operation to create a delegate object which stakeholders may
    * vote on to approve its position as a delegate.
    */
   struct delegate_create_operation : public base_operation
   {
      struct fee_parameters_type { uint64_t fee = 5000 * GRAPHENE_BLOCKCHAIN_PRECISION; };

      asset                                 fee;
      /// The account which owns the delegate. This account pays the fee for this operation.
      account_id_type                       delegate_account;
      string                                url;

      account_id_type fee_payer()const { return delegate_account; }
      void            validate()const;
   };

   /**
    * @brief Used by delegates to update the global parameters of the blockchain.
    * @ingroup operations
    *
    * This operation allows the delegates to update the global parameters on the blockchain. These control various
    * tunable aspects of the chain, including block and maintenance intervals, maximum data sizes, the fees charged by
    * the network, etc.
    *
    * This operation may only be used in a proposed transaction, and a proposed transaction which contains this
    * operation must have a review period specified in the current global parameters before it may be accepted.
    */
   struct delegate_update_global_parameters_operation : public base_operation
   {
      struct fee_parameters_type { uint64_t fee = GRAPHENE_BLOCKCHAIN_PRECISION; };

      asset             fee;
      chain_parameters  new_parameters;

      account_id_type fee_payer()const { return account_id_type(); }
      void            validate()const;
   };

   /// TODO: delegate_resign_operation : public base_operation

} } // graphene::chain
FC_REFLECT( graphene::chain::delegate_create_operation::fee_parameters_type, (fee) )
FC_REFLECT( graphene::chain::delegate_update_global_parameters_operation::fee_parameters_type, (fee) )


FC_REFLECT( graphene::chain::delegate_create_operation,
            (fee)(delegate_account)(url) )

FC_REFLECT( graphene::chain::delegate_update_global_parameters_operation, (fee)(new_parameters) );
