/*
 * Copyright (c) 2018 jmjatlanta and contributors.
 *
 * The MIT License
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#include <graphene/protocol/htlc.hpp>

#include <fc/io/raw.hpp>

#define SECONDS_PER_DAY (60 * 60 * 24)

namespace graphene { namespace protocol {

   void htlc_create_operation::validate()const {
      FC_ASSERT( fee.amount >= 0, "Fee amount should not be negative" );
      FC_ASSERT( amount.amount > 0, "HTLC amount should be greater than zero" );
   }

   share_type htlc_create_operation::calculate_fee( const fee_parameters_type& fee_params )const
   {
      uint64_t days = ( claim_period_seconds + SECONDS_PER_DAY - 1 ) / SECONDS_PER_DAY;
      // multiply with overflow check
      uint64_t per_day_fee = fee_params.fee_per_day * days;
      FC_ASSERT( days == 0 || per_day_fee / days == fee_params.fee_per_day, "Fee calculation overflow" );
      return fee_params.fee + per_day_fee;
   }

   void htlc_redeem_operation::validate()const {
      FC_ASSERT( fee.amount >= 0, "Fee amount should not be negative" );
   }

   share_type htlc_redeem_operation::calculate_fee( const fee_parameters_type& fee_params )const
   {
      uint64_t kb = ( preimage.size() + 1023 ) / 1024;
      uint64_t product = kb * fee_params.fee_per_kb;
      FC_ASSERT( kb == 0 || product / kb == fee_params.fee_per_kb, "Fee calculation overflow");
      return fee_params.fee + product;
   }

   void htlc_extend_operation::validate()const {
      FC_ASSERT( fee.amount >= 0 , "Fee amount should not be negative");
   }

   share_type htlc_extend_operation::calculate_fee( const fee_parameters_type& fee_params )const
   {
      uint32_t days = ( seconds_to_add + SECONDS_PER_DAY - 1 ) / SECONDS_PER_DAY;
      uint64_t per_day_fee = fee_params.fee_per_day * days;
      FC_ASSERT( days == 0 || per_day_fee / days == fee_params.fee_per_day, "Fee calculation overflow" );
      return fee_params.fee + per_day_fee;
   }
} }

GRAPHENE_IMPLEMENT_EXTERNAL_SERIALIZATION( graphene::protocol::htlc_create_operation::fee_parameters_type )
GRAPHENE_IMPLEMENT_EXTERNAL_SERIALIZATION( graphene::protocol::htlc_redeem_operation::fee_parameters_type )
GRAPHENE_IMPLEMENT_EXTERNAL_SERIALIZATION( graphene::protocol::htlc_extend_operation::fee_parameters_type )
GRAPHENE_IMPLEMENT_EXTERNAL_SERIALIZATION( graphene::protocol::htlc_create_operation )
GRAPHENE_IMPLEMENT_EXTERNAL_SERIALIZATION( graphene::protocol::htlc_redeem_operation )
GRAPHENE_IMPLEMENT_EXTERNAL_SERIALIZATION( graphene::protocol::htlc_redeemed_operation )
GRAPHENE_IMPLEMENT_EXTERNAL_SERIALIZATION( graphene::protocol::htlc_extend_operation )
GRAPHENE_IMPLEMENT_EXTERNAL_SERIALIZATION( graphene::protocol::htlc_refund_operation )
