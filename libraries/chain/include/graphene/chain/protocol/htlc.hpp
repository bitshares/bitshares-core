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

      struct htlc_create_operation : public base_operation {
          struct fee_parameters_type {
             uint64_t fee            = 1 * GRAPHENE_BLOCKCHAIN_PRECISION;
          };
    	  asset fee; // paid to network
    	  account_id_type source; // where the held monies are to come from
    	  account_id_type destination; // where the held monies will go if the preimage is provided
    	  asset amount; // the amount to hold
    	  std::vector<unsigned char> key_hash; // the hash of the preimage
    	  uint16_t key_size; // the size of the preimage
    	  fc::time_point_sec epoch; // The time the funds will be returned to the source if not claimed
    	  extensions_type extensions; // for future expansion

    	  void validate()const;
          void get_required_active_authorities( boost::container::flat_set<account_id_type>& a )const{ a.insert(source); }
          account_id_type fee_payer()const { return source; }

      };

      struct htlc_update_operation : public base_operation
      {
          struct fee_parameters_type {
             uint64_t fee            = 1 * GRAPHENE_BLOCKCHAIN_PRECISION;
          };
    	  asset                       fee; // paid to network
    	  htlc_id_type                htlc_id; // the object we are attempting to update
    	  account_id_type             update_issuer; // who is attempting to update the transaction
    	  std::vector<unsigned char>  preimage; // the preimage (not used if after epoch timeout)
    	  extensions_type             extensions; // for future expansion

    	  void validate()const;
          void get_required_active_authorities( boost::container::flat_set<account_id_type>& a )const{ a.insert(update_issuer); }
          account_id_type fee_payer()const { return update_issuer; }
      };
   } 
}

FC_REFLECT( graphene::chain::htlc_create_operation::fee_parameters_type, (fee) )
FC_REFLECT( graphene::chain::htlc_update_operation::fee_parameters_type, (fee) )

FC_REFLECT( graphene::chain::htlc_create_operation, (fee)(source)(destination)(amount)(key_hash)(key_size)(epoch)(extensions))
FC_REFLECT( graphene::chain::htlc_update_operation, (fee)(update_issuer)(preimage)(extensions))
