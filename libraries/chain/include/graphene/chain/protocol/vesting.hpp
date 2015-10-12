/*
 * Copyright (c) 2015 Cryptonomex, Inc., and contributors.  All rights reserved.
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

   struct linear_vesting_policy_initializer
   {
      /** while vesting begins on begin_timestamp, none may be claimed before vesting_cliff_seconds have passed */
      fc::time_point_sec begin_timestamp;
      uint32_t           vesting_cliff_seconds = 0;
      uint32_t           vesting_duration_seconds = 0;
   };

   struct cdd_vesting_policy_initializer
   {
      /** while coindays may accrue over time, none may be claimed before the start_claim time */
      fc::time_point_sec start_claim;
      uint32_t           vesting_seconds = 0;
      cdd_vesting_policy_initializer( uint32_t vest_sec = 0, fc::time_point_sec sc = fc::time_point_sec() ):start_claim(sc),vesting_seconds(vest_sec){}
   };

   typedef fc::static_variant<linear_vesting_policy_initializer, cdd_vesting_policy_initializer> vesting_policy_initializer;



   /**
    * @brief Create a vesting balance.
    * @ingroup operations
    *
    *  The chain allows a user to create a vesting balance.
    *  Normally, vesting balances are created automatically as part
    *  of cashback and worker operations.  This operation allows
    *  vesting balances to be created manually as well.
    *
    *  Manual creation of vesting balances can be used by a stakeholder
    *  to publicly demonstrate that they are committed to the chain.
    *  It can also be used as a building block to create transactions
    *  that function like public debt.  Finally, it is useful for
    *  testing vesting balance functionality.
    *
    * @return ID of newly created vesting_balance_object
    */
   struct vesting_balance_create_operation : public base_operation
   {
      struct fee_parameters_type { uint64_t fee = GRAPHENE_BLOCKCHAIN_PRECISION; };

      asset                       fee;
      account_id_type             creator; ///< Who provides funds initially
      account_id_type             owner; ///< Who is able to withdraw the balance
      asset                       amount;
      vesting_policy_initializer  policy;

      account_id_type   fee_payer()const { return creator; }
      void              validate()const
      {
         FC_ASSERT( fee.amount >= 0 );
         FC_ASSERT( amount.amount > 0 );
      }
   };

   /**
    * @brief Withdraw from a vesting balance.
    * @ingroup operations
    *
    * Withdrawal from a not-completely-mature vesting balance
    * will result in paying fees.
    *
    * @return Nothing
    */
   struct vesting_balance_withdraw_operation : public base_operation
   {
      struct fee_parameters_type { uint64_t fee = 20*GRAPHENE_BLOCKCHAIN_PRECISION; };

      asset                   fee;
      vesting_balance_id_type vesting_balance;
      account_id_type         owner; ///< Must be vesting_balance.owner
      asset                   amount;

      account_id_type   fee_payer()const { return owner; }
      void              validate()const
      {
         FC_ASSERT( fee.amount >= 0 );
         FC_ASSERT( amount.amount > 0 );
      }
   };

} } // graphene::chain

FC_REFLECT( graphene::chain::vesting_balance_create_operation::fee_parameters_type, (fee) )
FC_REFLECT( graphene::chain::vesting_balance_withdraw_operation::fee_parameters_type, (fee) )

FC_REFLECT( graphene::chain::vesting_balance_create_operation, (fee)(creator)(owner)(amount)(policy) )
FC_REFLECT( graphene::chain::vesting_balance_withdraw_operation, (fee)(vesting_balance)(owner)(amount) )

FC_REFLECT(graphene::chain::linear_vesting_policy_initializer, (begin_timestamp)(vesting_cliff_seconds)(vesting_duration_seconds) )
FC_REFLECT(graphene::chain::cdd_vesting_policy_initializer, (start_claim)(vesting_seconds) )
FC_REFLECT_TYPENAME( graphene::chain::vesting_policy_initializer )
