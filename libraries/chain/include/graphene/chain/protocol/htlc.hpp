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
#include <graphene/chain/htlc_object.hpp>
#include <graphene/chain/protocol/base.hpp>
#include <graphene/chain/protocol/types.hpp>

namespace graphene { 
   namespace chain {

      /**
       * Convert the hash algorithm to a string
       * @param algo the enum to convert
       * @returns a string (lower case)
       */
      std::string hash_algorithm_to_string(fc::enum_type<uint8_t, hash_algorithm> algo);

      /**
       * Convert a string to the enum that matches the hash algorithm
       * @param incoing the string (case insensitive)
       * @returns the matching enum
       */
      fc::enum_type<uint8_t, hash_algorithm> string_to_hash_algorithm(std::string incoming);
   

      struct htlc_create_operation : public base_operation 
      {
         struct fee_parameters_type {
            uint64_t fee = 1 * GRAPHENE_BLOCKCHAIN_PRECISION;
            uint64_t fee_per_day = 1 * GRAPHENE_BLOCKCHAIN_PRECISION;
         };
         // paid to network
    	   asset fee; 
         // where the held monies are to come from
         account_id_type source;
         // where the held monies will go if the preimage is provided
    	   account_id_type destination; 
         // the amount to hold
    	   asset amount;
         // hash algorithm used to create key_hash
         fc::enum_type<uint8_t, graphene::chain::hash_algorithm> hash_type 
               = graphene::chain::hash_algorithm::unknown;
         // the hash of the preimage
    	   std::vector<unsigned char> key_hash;
         // the size of the preimage
    	   uint16_t key_size;
         // The time the funds will be returned to the source if not claimed
    	   uint32_t seconds_in_force;
         // for future expansion
    	   extensions_type extensions; 

         /***
          * @brief Does simple validation of this object
          */
    	   void validate()const;
         
         /**
          * @brief Determines who is required to sign
          */
         void get_required_active_authorities( boost::container::flat_set<account_id_type>& a )const
         { 
            a.insert(source); 
         }

         /**
          * @brief who will pay the fee
          */
         account_id_type fee_payer()const { return source; }

         /****
          * @brief calculates the fee to be paid for this operation
          */
         share_type calculate_fee(const fee_parameters_type& fee_params)const
         {
            uint32_t days = seconds_in_force / (60 * 60 * 24);
            return fee_params.fee + (fee_params.fee_per_day * days);
         }

      };

      struct htlc_redeem_operation : public base_operation
      {
         struct fee_parameters_type {
            uint64_t fee = 1 * GRAPHENE_BLOCKCHAIN_PRECISION;
         };
         
         // paid to network
         asset fee;
         // the object we are attempting to update
         htlc_id_type htlc_id;
         // who is attempting to update the transaction
    	   account_id_type update_issuer;
         // the preimage (not used if after epoch timeout)
    	   std::vector<unsigned char> preimage;
         // for future expansion
         extensions_type extensions; 

         /***
          * @brief Perform obvious checks to validate this object
          */
    	   void validate()const;
         
         /***
          * @determines who should have signed this object
          */
         void get_required_active_authorities( boost::container::flat_set<account_id_type>& a )const
         { 
            a.insert(update_issuer); 
         }

         /**
          * @brief Who is to pay the fee
          */
         account_id_type fee_payer()const { return update_issuer; }

         /****
          * @brief calculates the fee to be paid for this operation
          */
         share_type calculate_fee(const fee_parameters_type& fee_params)const
         {
            return fee_params.fee;
         }
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
         
         /***
          * @determines who should have signed this object
          */
         void get_required_active_authorities( boost::container::flat_set<account_id_type>& a )const
         { 
            a.insert(update_issuer); 
         }

         /**
          * @brief Who is to pay the fee
          */
         account_id_type fee_payer()const { return update_issuer; }

         /****
          * @brief calculates the fee to be paid for this operation
          */
         share_type calculate_fee(const fee_parameters_type& fee_params)const
         {
            uint32_t days = seconds_to_add / (60 * 60 * 24);
            return fee_params.fee + (fee_params.fee_per_day * days);
         }
      };
   } 
}

FC_REFLECT( graphene::chain::htlc_create_operation::fee_parameters_type, (fee) (fee_per_day) )
FC_REFLECT( graphene::chain::htlc_redeem_operation::fee_parameters_type, (fee) )
FC_REFLECT( graphene::chain::htlc_extend_operation::fee_parameters_type, (fee) (fee_per_day))

FC_REFLECT( graphene::chain::htlc_create_operation, 
      (fee)(source)(destination)(amount)(key_hash)(key_size)(seconds_in_force)(extensions)(hash_type))
FC_REFLECT( graphene::chain::htlc_redeem_operation, (fee)(htlc_id)(update_issuer)(preimage)(extensions))
FC_REFLECT( graphene::chain::htlc_extend_operation, (fee)(htlc_id)(update_issuer)(seconds_to_add)(extensions))
