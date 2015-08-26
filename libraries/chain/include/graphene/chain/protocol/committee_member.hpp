#pragma once
#include <graphene/chain/protocol/base.hpp>
#include <graphene/chain/protocol/chain_parameters.hpp>

namespace graphene { namespace chain { 

   /**
    * @brief Create a committee_member object, as a bid to hold a committee_member seat on the network.
    * @ingroup operations
    *
    * Accounts which wish to become committee_members may use this operation to create a committee_member object which stakeholders may
    * vote on to approve its position as a committee_member.
    */
   struct committee_member_create_operation : public base_operation
   {
      struct fee_parameters_type { uint64_t fee = 5000 * GRAPHENE_BLOCKCHAIN_PRECISION; };

      asset                                 fee;
      /// The account which owns the committee_member. This account pays the fee for this operation.
      account_id_type                       committee_member_account;
      string                                url;

      account_id_type fee_payer()const { return committee_member_account; }
      void            validate()const;
   };

   /**
    * @brief Update a committee_member object.
    * @ingroup operations
    *
    * Currently the only field which can be updated is the `url`
    * field.
    */
   struct committee_member_update_operation : public base_operation
   {
      struct fee_parameters_type { uint64_t fee = 20 * GRAPHENE_BLOCKCHAIN_PRECISION; };

      asset                                 fee;
      /// The committee member to update.
      committee_member_id_type              committee_member;
      /// The account which owns the committee_member. This account pays the fee for this operation.
      account_id_type                       committee_member_account;
      optional< string >                    new_url;

      account_id_type fee_payer()const { return committee_member_account; }
      void            validate()const;
   };

   /**
    * @brief Used by committee_members to update the global parameters of the blockchain.
    * @ingroup operations
    *
    * This operation allows the committee_members to update the global parameters on the blockchain. These control various
    * tunable aspects of the chain, including block and maintenance intervals, maximum data sizes, the fees charged by
    * the network, etc.
    *
    * This operation may only be used in a proposed transaction, and a proposed transaction which contains this
    * operation must have a review period specified in the current global parameters before it may be accepted.
    */
   struct committee_member_update_global_parameters_operation : public base_operation
   {
      struct fee_parameters_type { uint64_t fee = GRAPHENE_BLOCKCHAIN_PRECISION; };

      asset             fee;
      chain_parameters  new_parameters;

      account_id_type fee_payer()const { return account_id_type(); }
      void            validate()const;
   };

   /// TODO: committee_member_resign_operation : public base_operation

} } // graphene::chain
FC_REFLECT( graphene::chain::committee_member_create_operation::fee_parameters_type, (fee) )
FC_REFLECT( graphene::chain::committee_member_update_operation::fee_parameters_type, (fee) )
FC_REFLECT( graphene::chain::committee_member_update_global_parameters_operation::fee_parameters_type, (fee) )


FC_REFLECT( graphene::chain::committee_member_create_operation,
            (fee)(committee_member_account)(url) )
FC_REFLECT( graphene::chain::committee_member_update_operation,
            (fee)(committee_member)(committee_member_account)(new_url) )
FC_REFLECT( graphene::chain::committee_member_update_global_parameters_operation, (fee)(new_parameters) );
