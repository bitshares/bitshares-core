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

   struct initiate_crowdfund_operation : public base_operation
   {
     struct fee_parameters_type { 
         uint64_t fee = GRAPHENE_BLOCKCHAIN_PRECISION; 
         uint32_t price_per_kbyte = 10;
      };

      asset                     fee;
      account_id_type           owner;
      asset_id_type             asset_id;
      uint64_t                  t;
      uint64_t                  u;

      account_id_type   fee_payer()const { return owner; }
      void              validate()const;
     // share_type        calculate_fee(const fee_parameters_type& k)const;
   };
   struct participate_crowdfund_operation : public base_operation
   {
      struct fee_parameters_type { 
         uint64_t fee = GRAPHENE_BLOCKCHAIN_PRECISION; 
         uint32_t price_per_kbyte = 10;
      };

      asset                     fee;
      account_id_type           buyer;
      int64_t                   valuation;
      int64_t                   cap;
      crowdfund_id_type         crowdfund;
      //address                   pubkey;

      account_id_type   fee_payer()const { return buyer; }
      void              validate()const;
    //  share_type        calculate_fee(const fee_parameters_type& k)const;
   };
   struct withdraw_crowdfund_operation : public base_operation
   {
      struct fee_parameters_type { 
         uint64_t fee = GRAPHENE_BLOCKCHAIN_PRECISION; 
         uint32_t price_per_kbyte = 10;
      };

      asset                     fee;
      account_id_type           buyer;
      crowdfund_contract_id_type            crowdfund_contract;

      account_id_type   fee_payer()const { return buyer; }
      void              validate()const;
      //share_type        calculate_fee(const fee_parameters_type& k)const;
    };

} } // namespace graphene::chain

FC_REFLECT( graphene::chain::initiate_crowdfund_operation::fee_parameters_type, (fee)(price_per_kbyte) )
FC_REFLECT( graphene::chain::initiate_crowdfund_operation, (fee)(owner)(asset_id )(t)(u) )
FC_REFLECT( graphene::chain::participate_crowdfund_operation::fee_parameters_type, (fee)(price_per_kbyte) )
FC_REFLECT( graphene::chain::participate_crowdfund_operation, (fee)(buyer)(valuation)(cap)(crowdfund) )
FC_REFLECT( graphene::chain::withdraw_crowdfund_operation::fee_parameters_type, (fee)(price_per_kbyte) )
FC_REFLECT( graphene::chain::withdraw_crowdfund_operation, (fee)(buyer)(crowdfund_contract) )

#define db_notify_crowdfund                             \
   void operator()( const withdraw_crowdfund_operation& op ) \
   {                                                     \
      _impacted.insert( op.buyer );                      \
   }                                                     \
   void operator()( const participate_crowdfund_operation& op ) \
   {                                                     \
      _impacted.insert( op.buyer );                      \
   }                                                     \
   void operator()( const initiate_crowdfund_operation& op ) \
   {                                                     \
      _impacted.insert( op.owner );                      \
   }                                                     \

#define impact_visit_crowdfund db_notify_crowdfund

#define crowdfund_object_type_to_accounts                 \
        case crowdfund_object_type:{                      \
           const auto& aobj = dynamic_cast<const crowdfund_object*>(obj);\
           assert( aobj != nullptr );                     \
           accounts.insert( aobj->owner );                \
           break;                                         \
        } case crowdfund_contract_object_type:{           \
           const auto& aobj = dynamic_cast<const crowdfund_contract_object*>(obj);\
           assert( aobj != nullptr );                     \
           accounts.insert( aobj->owner );                \
           break;                                         \
        }                                                 \

