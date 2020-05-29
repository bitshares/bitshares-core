/*
 * Copyright (c) 2019 Contributors.
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
#include <graphene/protocol/base.hpp>
#include <graphene/protocol/asset.hpp>
#include <graphene/protocol/authority.hpp>
#include <graphene/protocol/restriction.hpp>

namespace graphene { namespace protocol {

   /**
    * @brief Create a new custom authority
    * @ingroup operations
    */
   struct custom_authority_create_operation : public base_operation {
      struct fee_parameters_type {
         uint64_t basic_fee = GRAPHENE_BLOCKCHAIN_PRECISION;
         uint32_t price_per_byte = GRAPHENE_BLOCKCHAIN_PRECISION / 10;
      };

      /// Operation fee
      asset fee;
      /// Account which is setting the custom authority; also pays the fee
      account_id_type account;
      /// Whether the custom authority is enabled or not
      bool enabled;
      /// Date when custom authority becomes active
      time_point_sec valid_from;
      /// Expiration date for custom authority
      time_point_sec valid_to;
      /// Tag of the operation this custom authority can authorize
      unsigned_int operation_type;
      /// Authentication requirements for the custom authority
      authority auth;
      /// Restrictions on operations this custom authority can authenticate
      vector<restriction> restrictions;

      extensions_type extensions;

      account_id_type fee_payer()const { return account; }
      void validate()const;
      share_type calculate_fee(const fee_parameters_type& k)const;
   };

   /**
    * @brief Update a custom authority
    * @ingroup operations
    */
   struct custom_authority_update_operation : public base_operation {
      struct fee_parameters_type {
         uint64_t basic_fee = GRAPHENE_BLOCKCHAIN_PRECISION;
         uint32_t price_per_byte = GRAPHENE_BLOCKCHAIN_PRECISION / 10;
      };

      /// Operation fee
      asset fee;
      /// Account which owns the custom authority to update; also pays the fee
      account_id_type account;
      /// ID of the custom authority to update
      custom_authority_id_type authority_to_update;
      /// Change to whether the custom authority is enabled or not
      optional<bool> new_enabled;
      /// Change to the custom authority begin date
      optional<time_point_sec> new_valid_from;
      /// Change to the custom authority expiration date
      optional<time_point_sec> new_valid_to;
      /// Change to the authentication for the custom authority
      optional<authority> new_auth;
      /// Set of IDs of restrictions to remove
      flat_set<uint16_t> restrictions_to_remove;
      /// Vector of new restrictions
      vector<restriction> restrictions_to_add;

      extensions_type extensions;

      account_id_type fee_payer()const { return account; }
      void validate()const;
      share_type calculate_fee(const fee_parameters_type& k)const;
   };


   /**
    * @brief Delete a custom authority
    * @ingroup operations
    */
   struct custom_authority_delete_operation : public base_operation {
      struct fee_parameters_type { uint64_t fee =  GRAPHENE_BLOCKCHAIN_PRECISION; };

      /// Operation fee
      asset fee;
      /// Account which owns the custom authority to update; also pays the fee
      account_id_type account;
      /// ID of the custom authority to delete
      custom_authority_id_type authority_to_delete;

      extensions_type extensions;

      account_id_type fee_payer()const { return account; }
      void validate()const;
      share_type calculate_fee(const fee_parameters_type& k)const { return k.fee; }
   };

} } // graphene::protocol

FC_REFLECT(graphene::protocol::custom_authority_create_operation::fee_parameters_type, (basic_fee)(price_per_byte))
FC_REFLECT(graphene::protocol::custom_authority_update_operation::fee_parameters_type, (basic_fee)(price_per_byte))
FC_REFLECT(graphene::protocol::custom_authority_delete_operation::fee_parameters_type, (fee))

FC_REFLECT(graphene::protocol::custom_authority_create_operation,
           (fee)(account)(enabled)(valid_from)(valid_to)(operation_type)(auth)(restrictions)(extensions))

FC_REFLECT(graphene::protocol::custom_authority_update_operation,
           (fee)(account)(authority_to_update)(new_enabled)(new_valid_from)
           (new_valid_to)(new_auth)(restrictions_to_remove)(restrictions_to_add)(extensions))
FC_REFLECT(graphene::protocol::custom_authority_delete_operation, (fee)(account)(authority_to_delete)(extensions))
