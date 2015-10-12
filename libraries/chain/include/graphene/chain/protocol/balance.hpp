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

   /**
    * @brief Claim a balance in a @ref balanc_object
    *
    * This operation is used to claim the balance in a given @ref balance_object. If the balance object contains a
    * vesting balance, @ref total_claimed must not exceed @ref balance_object::available at the time of evaluation. If
    * the object contains a non-vesting balance, @ref total_claimed must be the full balance of the object.
    */
   struct balance_claim_operation : public base_operation
   {
      struct fee_parameters_type {};

      asset             fee;
      account_id_type   deposit_to_account;
      balance_id_type   balance_to_claim;
      public_key_type   balance_owner_key;
      asset             total_claimed;

      account_id_type fee_payer()const { return deposit_to_account; }
      share_type      calculate_fee(const fee_parameters_type& )const { return 0; }
      void            validate()const;
      void            get_required_authorities( vector<authority>& a )const
      {
         a.push_back( authority( 1, balance_owner_key, 1 ) );
      }
   };

} } // graphene::chain

FC_REFLECT( graphene::chain::balance_claim_operation::fee_parameters_type,  )
FC_REFLECT( graphene::chain::balance_claim_operation,
            (fee)(deposit_to_account)(balance_to_claim)(balance_owner_key)(total_claimed) )
