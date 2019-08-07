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

#include <graphene/chain/custom_authority_evaluator.hpp>
#include <graphene/chain/custom_authority_object.hpp>
#include <graphene/chain/account_object.hpp>
#include <graphene/chain/database.hpp>
#include <graphene/chain/exceptions.hpp>
#include <graphene/chain/hardfork.hpp>

namespace graphene { namespace chain {

void_result custom_authority_create_evaluator::do_evaluate(const custom_authority_create_operation& op)
{ try {
   const database& d = db();
   auto now = d.head_block_time();
   FC_ASSERT(HARDFORK_BSIP_40_PASSED(now), "Custom active authorities are not yet enabled");

   op.account(d);

   const auto& config = d.get_global_properties().parameters.extensions.value.custom_authority_options;
   FC_ASSERT(config.valid(), "Cannot use custom authorities yet: global configuration not set");
   FC_ASSERT(op.valid_to > now, "Custom authority expiration must be in the future");
   FC_ASSERT((op.valid_to - now).to_seconds() <= config->max_custom_authority_lifetime_seconds,
             "Custom authority lifetime exceeds maximum limit");

   FC_ASSERT(op.operation_type.value <= (size_t)config->max_operation_tag,
             "Cannot create custom authority for operation type which is not yet active");

   for (const auto& account_weight_pair : op.auth.account_auths)
      account_weight_pair.first(d);

   const auto& index = d.get_index_type<custom_authority_index>().indices().get<by_account_custom>();
   auto range = index.equal_range(op.account);
   FC_ASSERT(std::distance(range.first, range.second) < config->max_custom_authorities_per_account,
             "Cannot create custom authority for account: account already has maximum number");

   predicate = get_restriction_predicate(op.restrictions, op.operation_type);
   return void_result();
} FC_CAPTURE_AND_RETHROW((op)) }

object_id_type custom_authority_create_evaluator::do_apply(const custom_authority_create_operation& op)
{ try {
   database& d = db();

   return d.create<custom_authority_object>([&op, p=std::move(predicate)] (custom_authority_object& obj) mutable {
      obj.account = op.account;
      obj.enabled = op.enabled;
      obj.valid_from = op.valid_from;
      obj.valid_to = op.valid_to;
      obj.operation_type = op.operation_type;
      obj.auth = op.auth;
      std::for_each(op.restrictions.begin(), op.restrictions.end(), [&obj](const restriction& r) mutable {
         obj.restrictions.insert(std::make_pair(obj.restriction_counter++, r));
      });

      // Update the predicate cache
      obj.update_predicate_cache();
   }).id;
} FC_CAPTURE_AND_RETHROW((op)) }

void_result custom_authority_update_evaluator::do_evaluate(const custom_authority_update_operation& op)
{ try {
   const database& d = db();
   auto now = d.head_block_time();
   FC_ASSERT(HARDFORK_BSIP_40_PASSED(now), "Custom active authorities are not yet enabled");
   old_object = &op.authority_to_update(d);
   FC_ASSERT(old_object->account == op.account, "Cannot update a different account's custom authority");

   op.account(d);
   if (op.new_enabled)
      FC_ASSERT(*op.new_enabled != old_object->enabled,
                "Custom authority update specifies an enabled flag, but flag is not changed");

   const auto& config = d.get_global_properties().parameters.extensions.value.custom_authority_options;
   auto valid_from = old_object->valid_from;
   auto valid_to = old_object->valid_to;
   if (op.new_valid_from) {
      FC_ASSERT(*op.new_valid_from != old_object->valid_from,
                "Custom authority update specifies a new valid from date, but date is not changed");
      valid_from = *op.new_valid_from;
   }
   if (op.new_valid_to) {
      FC_ASSERT(*op.new_valid_to != old_object->valid_to,
                "Custom authority update specifies a new valid to date, but date is not changed");
      FC_ASSERT(*op.new_valid_to > now, "Custom authority expiration must be in the future");
      FC_ASSERT((*op.new_valid_to - now).to_seconds() <= config->max_custom_authority_lifetime_seconds,
                "Custom authority lifetime exceeds maximum limit");
      valid_to = *op.new_valid_to;
   }
   FC_ASSERT(valid_from < valid_to, "Custom authority validity begin date must be before expiration date");

   if (op.new_auth) {
      FC_ASSERT(*op.new_auth != old_object->auth,
                "Custom authority update specifies a new authentication authority, but authority is not changed");
      for (const auto& account_weight_pair : op.new_auth->account_auths)
         account_weight_pair.first(d);
   }

   std::for_each(op.restrictions_to_remove.begin(), op.restrictions_to_remove.end(), [this](uint16_t id) {
      FC_ASSERT(old_object->restrictions.count(id) == 1, "Cannot remove restriction ID ${I}: ID not found",
                ("I", id));
   });
   if (!op.restrictions_to_add.empty()) {
      // Sanity check
      if (!old_object->restrictions.empty())
         FC_ASSERT((--old_object->restrictions.end())->first < old_object->restriction_counter,
                   "LOGIC ERROR: Restriction counter overlaps restrictions. Please report this error.");
      FC_ASSERT(old_object->restriction_counter + op.restrictions_to_add.size() > old_object->restriction_counter,
                "Unable to add restrictions: causes wraparound of restriction IDs");
   }

   get_restriction_predicate(op.restrictions_to_add, old_object->operation_type);
   return void_result();
} FC_CAPTURE_AND_RETHROW((op)) }

void_result custom_authority_update_evaluator::do_apply(const custom_authority_update_operation& op)
{ try {
   database& d = db();

   d.modify(*old_object, [&op](custom_authority_object& obj) {
      if (op.new_enabled) obj.enabled = *op.new_enabled;
      if (op.new_valid_from) obj.valid_from = *op.new_valid_from;
      if (op.new_valid_to) obj.valid_to = *op.new_valid_to;
      if (op.new_auth) obj.auth = *op.new_auth;

      std::for_each(op.restrictions_to_remove.begin(), op.restrictions_to_remove.end(), [&obj](auto id) mutable {
         obj.restrictions.erase(id);
      });
      std::for_each(op.restrictions_to_add.begin(), op.restrictions_to_add.end(), [&obj](const auto& r) mutable {
         obj.restrictions.insert(std::make_pair(obj.restriction_counter++, r));
      });

      // Update the predicate cache
      obj.update_predicate_cache();
   });

   return void_result();
} FC_CAPTURE_AND_RETHROW((op)) }

void_result custom_authority_delete_evaluator::do_evaluate(const custom_authority_delete_operation& op)
{ try {
   const database& d = db();
   FC_ASSERT(HARDFORK_BSIP_40_PASSED(d.head_block_time()), "Custom active authorities are not yet enabled");

   op.account(d);
   old_object = &op.authority_to_delete(d);
   FC_ASSERT(old_object->account == op.account, "Cannot delete a different account's custom authority");

   return void_result();
} FC_CAPTURE_AND_RETHROW((op)) }

void_result custom_authority_delete_evaluator::do_apply(const custom_authority_delete_operation& op)
{ try {
   database& d = db();

   d.remove(*old_object);

   return void_result();
} FC_CAPTURE_AND_RETHROW((op)) }

} } // graphene::chain
