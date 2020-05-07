/*
 * Copyright (c) 2018 Abit More, and contributors.
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
#include <graphene/protocol/custom_authority.hpp>
#include <graphene/protocol/operations.hpp>
#include <graphene/protocol/restriction_predicate.hpp>

#include <fc/io/raw.hpp>

namespace graphene { namespace protocol {

share_type custom_authority_create_operation::calculate_fee(const fee_parameters_type& k)const {
   share_type core_fee_required = k.basic_fee;
   // Note: practically the `*` won't cause an integer overflow, because k.price_per_byte is 32 bit
   //       and the results of pack_size() won't be too big
   core_fee_required += k.price_per_byte * (fc::raw::pack_size(restrictions) + fc::raw::pack_size(auth));
   return core_fee_required;
}

void custom_authority_create_operation::validate()const {
   FC_ASSERT(fee.amount >= 0, "Fee amount can not be negative");

   FC_ASSERT(account != GRAPHENE_TEMP_ACCOUNT
             && account != GRAPHENE_COMMITTEE_ACCOUNT
             && account != GRAPHENE_WITNESS_ACCOUNT
             && account != GRAPHENE_RELAXED_COMMITTEE_ACCOUNT,
             "Can not create custom authority for special accounts");

   FC_ASSERT(valid_from < valid_to, "valid_from must be earlier than valid_to");

   // Note: The authentication authority can be empty, but it cannot be impossible to satisify. Disable the authority
   // using the `enabled` boolean rather than setting an impossible authority.

   FC_ASSERT(auth.address_auths.size() == 0, "Address authorities are not supported");
   FC_ASSERT(!auth.is_impossible(), "Cannot use an imposible authority threshold");

   // Validate restrictions by constructing a predicate for them; this throws if restrictions aren't valid
   get_restriction_predicate(restrictions, operation_type);
}

share_type custom_authority_update_operation::calculate_fee(const fee_parameters_type& k)const {
   share_type core_fee_required = k.basic_fee;
   // Note: practically the `*` won't cause an integer overflow, because k.price_per_byte is 32 bit
   //       and the results of pack_size() won't be too big
   core_fee_required += k.price_per_byte * fc::raw::pack_size(restrictions_to_add);
   if (new_auth)
      core_fee_required += k.price_per_byte * fc::raw::pack_size(*new_auth);
   return core_fee_required;
}

void custom_authority_update_operation::validate()const {
   FC_ASSERT(fee.amount >= 0, "Fee amount can not be negative");

   FC_ASSERT(account != GRAPHENE_TEMP_ACCOUNT
             && account != GRAPHENE_COMMITTEE_ACCOUNT
             && account != GRAPHENE_WITNESS_ACCOUNT
             && account != GRAPHENE_RELAXED_COMMITTEE_ACCOUNT,
             "Can not create custom authority for special accounts");
   if (new_valid_from && new_valid_to)
      FC_ASSERT(*new_valid_from < *new_valid_to, "valid_from must be earlier than valid_to");
   if (new_auth) {
      FC_ASSERT(!new_auth->is_impossible(), "Cannot use an impossible authority threshold");
      FC_ASSERT(new_auth->address_auths.size() == 0, "Address auth is not supported");
   }
   FC_ASSERT( new_enabled.valid() || new_valid_from.valid() || new_valid_to.valid() || new_auth.valid()
              || !restrictions_to_remove.empty() || !restrictions_to_add.empty(),
              "Must update something" );
}

void custom_authority_delete_operation::validate()const {
   FC_ASSERT(fee.amount >= 0, "Fee amount can not be negative");

   FC_ASSERT(account != GRAPHENE_TEMP_ACCOUNT
             && account != GRAPHENE_COMMITTEE_ACCOUNT
             && account != GRAPHENE_WITNESS_ACCOUNT
             && account != GRAPHENE_RELAXED_COMMITTEE_ACCOUNT,
             "Can not delete custom authority for special accounts");
}

} } // graphene::protocol
