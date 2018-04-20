/*
 * Copyright (c) 2015 Cryptonomex, Inc., and contributors.
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

#pragma once
#include <graphene/chain/protocol/base.hpp>

namespace graphene { namespace chain { 

   struct cancel_vesting_operation : public base_operation
   {
     struct fee_parameters_type { 
         uint64_t fee = GRAPHENE_BLOCKCHAIN_PRECISION; 
      };

      asset                     fee;
      account_id_type           sender;
      balance_id_type           balance_object;

      account_id_type   fee_payer()const { return sender; }
      void              validate()const;
   };

} } // namespace graphene::chain

FC_REFLECT( graphene::chain::cancel_vesting_operation::fee_parameters_type, (fee) )
FC_REFLECT( graphene::chain::cancel_vesting_operation, (fee)(sender)(balance_object) )


#define   db_notify_cancel_vesting                       \
   void operator()( const cancel_vesting_operation& op ) \
   {                                                     \
      _impacted.insert( op.sender );                     \
   }                                                     \

#define impact_visit_cancel_vesting db_notify_cancel_vesting   
