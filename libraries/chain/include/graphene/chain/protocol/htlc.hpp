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
#pragma once
#include <fc/time.hpp>
#include <boost/container/flat_set.hpp>
#include <graphene/chain/protocol/base.hpp>
#include <graphene/chain/protocol/types.hpp>
#include <algorithm> // std::max

namespace graphene { 
   namespace chain {

      enum htlc_hash_algorithm {
         unknown = 0x00,
         ripemd160 = 0x01,
         sha256 = 0x02,
         sha1 = 0x03
      };

      struct htlc_create_operation : public base_operation 
      {
         struct fee_parameters_type {
            uint64_t fee = 1 * GRAPHENE_BLOCKCHAIN_PRECISION;
            uint64_t fee_per_day = 1 * GRAPHENE_BLOCKCHAIN_PRECISION;
         };
         // paid to network
    	   asset fee; 
         // where the held monies are to come from
         account_id_type from;
         // where the held monies will go if the preimage is provided
    	   account_id_type to; 
         // the amount to hold
    	   asset amount;
         // hash algorithm used to create preimage_hash
         fc::enum_type<uint8_t, htlc_hash_algorithm> hash_type 
               = htlc_hash_algorithm::unknown;
         // the hash of the preimage
    	   std::vector<uint8_t> preimage_hash;
         // the size of the preimage
    	   uint16_t preimage_size;
         // The time the funds will be returned to the source if not claimed
    	   uint32_t claim_period_seconds;
         // for future expansion
    	   extensions_type extensions; 

         /***
          * @brief Does simple validation of this object
          */
    	   void validate()const;
         
         /**
          * @brief who will pay the fee
          */
         account_id_type fee_payer()const { return from; }

         /****
          * @brief calculates the fee to be paid for this operation
          */
         share_type calculate_fee(const fee_parameters_type& fee_params)const;
      };

      struct htlc_redeem_operation : public base_operation
      {
         struct fee_parameters_type {
            uint64_t fee = 1 * GRAPHENE_BLOCKCHAIN_PRECISION;
            uint64_t fee_per_kb = 1 * GRAPHENE_BLOCKCHAIN_PRECISION;
         };
         
         // paid to network
         asset fee;
         // the object we are attempting to update
         htlc_id_type htlc_id;
         // who is attempting to update the transaction
    	   account_id_type redeemer;
         // the preimage (not used if after epoch timeout)
    	   std::vector<uint8_t> preimage;
         // for future expansion
         extensions_type extensions; 

         /***
          * @brief Perform obvious checks to validate this object
          */
    	   void validate()const;
         
         /**
          * @brief Who is to pay the fee
          */
         account_id_type fee_payer()const { return redeemer; }

         /****
          * @brief calculates the fee to be paid for this operation
          */
         share_type calculate_fee(const fee_parameters_type& fee_params)const;
      };

      struct htlc_extend_operation : public base_operation
      {
         struct fee_parameters_type {
            uint64_t fee = 1 * GRAPHENE_BLOCKCHAIN_PRECISION;
            uint64_t fee_per_day = 1 * GRAPHENE_BLOCKCHAIN_PRECISION;
         };
         
         // paid to network
         asset fee;
         // the object we are attempting to update
         htlc_id_type htlc_id;
         // who is attempting to update the transaction
         account_id_type update_issuer;
         // how much to add
         uint32_t seconds_to_add;
         // for future expansion
         extensions_type extensions; 

         /***
          * @brief Perform obvious checks to validate this object
          */
         void validate()const;
         
         /**
          * @brief Who is to pay the fee
          */
         account_id_type fee_payer()const { return update_issuer; }

         /****
          * @brief calculates the fee to be paid for this operation
          */
         share_type calculate_fee(const fee_parameters_type& fee_params)const;
      };

      struct htlc_refund_operation : public base_operation
      {
         struct fee_parameters_type {};

         htlc_refund_operation(){}
         htlc_refund_operation( const htlc_id_type& htlc_id, const account_id_type& to ) 
         : htlc_id(htlc_id), to(to) {}

         account_id_type fee_payer()const { return to; }
         void            validate()const { FC_ASSERT( !"virtual operation" ); }

         /// This is a virtual operation; there is no fee
         share_type      calculate_fee(const fee_parameters_type& k)const { return 0; }

         htlc_id_type htlc_id;
         account_id_type to;
         asset fee;
      };
   } 
}

FC_REFLECT_ENUM( graphene::chain::htlc_hash_algorithm, (unknown)(ripemd160)(sha256)(sha1));

FC_REFLECT( graphene::chain::htlc_create_operation::fee_parameters_type, (fee) (fee_per_day) )
FC_REFLECT( graphene::chain::htlc_redeem_operation::fee_parameters_type, (fee) (fee_per_kb) )
FC_REFLECT( graphene::chain::htlc_extend_operation::fee_parameters_type, (fee) (fee_per_day))
FC_REFLECT( graphene::chain::htlc_refund_operation::fee_parameters_type, ) // VIRTUAL

FC_REFLECT( graphene::chain::htlc_create_operation, 
      (fee)(from)(to)(amount)(preimage_hash)(preimage_size)(claim_period_seconds)(extensions)(hash_type))
FC_REFLECT( graphene::chain::htlc_redeem_operation, (fee)(htlc_id)(redeemer)(preimage)(extensions))
FC_REFLECT( graphene::chain::htlc_extend_operation, (fee)(htlc_id)(update_issuer)(seconds_to_add)(extensions))
FC_REFLECT( graphene::chain::htlc_refund_operation, (fee)(htlc_id)(to))
