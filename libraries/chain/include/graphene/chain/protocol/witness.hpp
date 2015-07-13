#pragma once
#include <graphene/chain/protocol/base.hpp>

namespace graphene { namespace chain { 

  /**
    * @brief Create a witness object, as a bid to hold a witness position on the network.
    * @ingroup operations
    *
    * Accounts which wish to become witnesses may use this operation to create a witness object which stakeholders may
    * vote on to approve its position as a witness.
    */
   struct witness_create_operation : public base_operation
   {
      struct fee_parameters_type { uint64_t fee = 5000 * GRAPHENE_BLOCKCHAIN_PRECISION; };

      asset             fee;
      /// The account which owns the witness. This account pays the fee for this operation.
      account_id_type   witness_account;
      string            url;
      public_key_type   block_signing_key;
      secret_hash_type  initial_secret;

      account_id_type fee_payer()const { return witness_account; }
      void            validate()const;
   };


   /**
    * @ingroup operations
    *  Used to move witness pay from accumulated_income to their account balance.
    *
    * TODO: remove this operation, send witness pay into a vesting balance object and
    * have the witness claim the funds from there.
    */
   struct witness_withdraw_pay_operation : public base_operation
   {
      struct fee_parameters_type { uint64_t fee = 20 * GRAPHENE_BLOCKCHAIN_PRECISION; };

      asset            fee;
      /// The account to pay. Must match from_witness->witness_account. This account pays the fee for this operation.
      account_id_type  to_account;
      witness_id_type  from_witness;
      share_type       amount;

      account_id_type fee_payer()const { return to_account; }
      void            validate()const;
   };


   /// TODO: witness_resign_operation : public base_operation

} } // graphene::chain

FC_REFLECT( graphene::chain::witness_create_operation::fee_parameters_type, (fee) )
FC_REFLECT( graphene::chain::witness_withdraw_pay_operation::fee_parameters_type, (fee) )

FC_REFLECT( graphene::chain::witness_create_operation, (fee)(witness_account)(url)(block_signing_key)(initial_secret) )
FC_REFLECT( graphene::chain::witness_withdraw_pay_operation, (fee)(from_witness)(to_account)(amount) )
