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
#include <graphene/chain/protocol/withdraw_permission.hpp>

namespace graphene { namespace chain {

void withdraw_permission_update_operation::validate()const
{
   FC_ASSERT( withdrawal_limit.amount > 0 );
   FC_ASSERT( fee.amount >= 0 );
   FC_ASSERT( withdrawal_period_sec > 0 );
   FC_ASSERT( withdraw_from_account != authorized_account );
   FC_ASSERT( periods_until_expiration > 0 );
}

void withdraw_permission_claim_operation::validate()const
{
   FC_ASSERT( withdraw_to_account != withdraw_from_account );
   FC_ASSERT( amount_to_withdraw.amount > 0 );
   FC_ASSERT( fee.amount >= 0 );
}

share_type withdraw_permission_claim_operation::calculate_fee(const fee_parameters_type& k)const
{
   share_type core_fee_required = k.fee;
   if( memo )
      core_fee_required += calculate_data_fee( fc::raw::pack_size(memo), k.price_per_kbyte );
   return core_fee_required;
}

void withdraw_permission_create_operation::validate() const
{
   FC_ASSERT( fee.amount >= 0 );
   FC_ASSERT( withdraw_from_account != authorized_account );
   FC_ASSERT( withdrawal_limit.amount > 0 );
   //TODO: better bounds checking on these values
   FC_ASSERT( withdrawal_period_sec > 0 );
   FC_ASSERT( periods_until_expiration > 0 );
}

void withdraw_permission_delete_operation::validate() const
{
   FC_ASSERT( fee.amount >= 0 );
   FC_ASSERT( withdraw_from_account != authorized_account );
}


} } // graphene::chain

