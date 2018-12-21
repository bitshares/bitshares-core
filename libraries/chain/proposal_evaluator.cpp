/*
 * Copyright (c) 2015-2018 Cryptonomex, Inc., and contributors.
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
#include <graphene/chain/database.hpp>
#include <graphene/chain/proposal_evaluator.hpp>
#include <graphene/chain/proposal_object.hpp>
#include <graphene/chain/hardfork.hpp>

#include <fc/smart_ref_impl.hpp>

namespace graphene { namespace chain {


struct proposal_operation_hardfork_visitor
{
   typedef void result_type;
   const fc::time_point_sec block_time;
   const fc::time_point_sec next_maintenance_time;

   proposal_operation_hardfork_visitor( const fc::time_point_sec bt, const fc::time_point_sec nmt )
   : block_time(bt), next_maintenance_time(nmt) {}

   template<typename T>
   void operator()(const T &v) const {}

   // TODO review and cleanup code below after hard fork
   // hf_834
   void operator()(const graphene::chain::call_order_update_operation &v) const {
      if (next_maintenance_time <= HARDFORK_CORE_834_TIME) {
         FC_ASSERT( !v.extensions.value.target_collateral_ratio.valid(),
                    "Can not set target_collateral_ratio in call_order_update_operation before hardfork 834." );
      }
   }
   // hf_620
   void operator()(const graphene::chain::asset_create_operation &v) const {
      if (block_time < HARDFORK_CORE_620_TIME) {
         static const std::locale &loc = std::locale::classic();
         FC_ASSERT(isalpha(v.symbol.back(), loc), "Asset ${s} must end with alpha character before hardfork 620", ("s", v.symbol));
      }
   }
   // hf_199
   void operator()(const graphene::chain::asset_update_issuer_operation &v) const {
      if (block_time < HARDFORK_CORE_199_TIME) {
         FC_ASSERT(false, "Not allowed until hardfork 199");
      }
   }
   // hf_188
   void operator()(const graphene::chain::asset_claim_pool_operation &v) const {
      if (block_time < HARDFORK_CORE_188_TIME) {
         FC_ASSERT(false, "Not allowed until hardfork 188");
      }
   }
   // hf_588
   // issue #588
   //
   // As a virtual operation which has no evaluator `asset_settle_cancel_operation`
   // originally won't be packed into blocks, yet its loose `validate()` method
   // make it able to slip into blocks.
   //
   // We need to forbid this operation being packed into blocks via proposal but
   // this will lead to a hardfork (this operation in proposal will denied by new
   // node while accept by old node), so a hardfork guard code needed and a
   // consensus upgrade over all nodes needed in future. And because the
   // `validate()` method not suitable to check database status, so we put the
   // code here.
   //
   // After the hardfork, all nodes will deny packing this operation into a block,
   // and then we will check whether exists a proposal containing this kind of
   // operation, if not exists, we can harden the `validate()` method to deny
   // it in a earlier stage.
   //
   void operator()(const graphene::chain::asset_settle_cancel_operation &v) const {
      if (block_time > HARDFORK_CORE_588_TIME) {
         FC_ASSERT(!"Virtual operation");
      }
   }
   // loop and self visit in proposals
   void operator()(const graphene::chain::proposal_create_operation &v) const {
      for (const op_wrapper &op : v.proposed_ops)
         op.op.visit(*this);
   }
};

struct hardfork_visitor_214 // non-recursive proposal visitor
{
   typedef void result_type;

   template<typename T>
   void operator()(const T &v) const {}

   void operator()(const proposal_update_operation &v) const {
      FC_ASSERT(false, "Not allowed until hardfork 214");
   }
};

void hardfork_visitor_1479::operator()(const proposal_update_operation &v)
{
   if( nested_update_count == 0 || v.proposal.instance.value > max_update_instance )
      max_update_instance = v.proposal.instance.value;
   nested_update_count++;
}

void hardfork_visitor_1479::operator()(const proposal_delete_operation &v)
{
   if( nested_update_count == 0 || v.proposal.instance.value > max_update_instance )
      max_update_instance = v.proposal.instance.value;
   nested_update_count++;
}

// loop and self visit in proposals
void hardfork_visitor_1479::operator()(const graphene::chain::proposal_create_operation &v)
{
   for (const op_wrapper &op : v.proposed_ops)
      op.op.visit(*this);
}

void_result proposal_create_evaluator::do_evaluate(const proposal_create_operation& o)
{ try {
   const database& d = db();

   // Calling the proposal hardfork visitor
   const fc::time_point_sec block_time = d.head_block_time();
   const fc::time_point_sec next_maint_time = d.get_dynamic_global_properties().next_maintenance_time;
   proposal_operation_hardfork_visitor vtor( block_time, next_maint_time );
   vtor( o );
   if( block_time < HARDFORK_CORE_214_TIME )
   { // cannot be removed after hf, unfortunately
      hardfork_visitor_214 hf214;
      for (const op_wrapper &op : o.proposed_ops)
         op.op.visit( hf214 );
   }
   vtor_1479( o );

   const auto& global_parameters = d.get_global_properties().parameters;

   FC_ASSERT( o.expiration_time > block_time, "Proposal has already expired on creation." );
   FC_ASSERT( o.expiration_time <= block_time + global_parameters.maximum_proposal_lifetime,
              "Proposal expiration time is too far in the future.");
   FC_ASSERT( !o.review_period_seconds || fc::seconds(*o.review_period_seconds) < (o.expiration_time - block_time),
              "Proposal review period must be less than its overall lifetime." );

   {
      // If we're dealing with the committee authority, make sure this transaction has a sufficient review period.
      flat_set<account_id_type> auths;
      vector<authority> other;
      for( auto& op : o.proposed_ops )
      {
         operation_get_required_authorities(op.op, auths, auths, other);
      }

      FC_ASSERT( other.size() == 0 ); // TODO: what about other??? 

      if( auths.find(GRAPHENE_COMMITTEE_ACCOUNT) != auths.end() )
      {
         GRAPHENE_ASSERT(
            o.review_period_seconds.valid(),
            proposal_create_review_period_required,
            "Review period not given, but at least ${min} required",
            ("min", global_parameters.committee_proposal_review_period)
         );
         GRAPHENE_ASSERT(
            *o.review_period_seconds >= global_parameters.committee_proposal_review_period,
            proposal_create_review_period_insufficient,
            "Review period of ${t} specified, but at least ${min} required",
            ("t", *o.review_period_seconds)
            ("min", global_parameters.committee_proposal_review_period)
         );
      }
   }

   for( const op_wrapper& op : o.proposed_ops )
      _proposed_trx.operations.push_back(op.op);

   _proposed_trx.validate();

   return void_result();
} FC_CAPTURE_AND_RETHROW( (o) ) }

object_id_type proposal_create_evaluator::do_apply(const proposal_create_operation& o)
{ try {
   database& d = db();

   const proposal_object& proposal = d.create<proposal_object>([&](proposal_object& proposal) {
      _proposed_trx.expiration = o.expiration_time;
      proposal.proposed_transaction = _proposed_trx;
      proposal.expiration_time = o.expiration_time;
      proposal.proposer = o.fee_paying_account;
      if( o.review_period_seconds )
         proposal.review_period_time = o.expiration_time - *o.review_period_seconds;

      //Populate the required approval sets
      flat_set<account_id_type> required_active;
      vector<authority> other;
      
      // TODO: consider caching values from evaluate?
      for( auto& op : _proposed_trx.operations )
         operation_get_required_authorities(op, required_active, proposal.required_owner_approvals, other);

      //All accounts which must provide both owner and active authority should be omitted from the active authority set;
      //owner authority approval implies active authority approval.
      std::set_difference(required_active.begin(), required_active.end(),
                          proposal.required_owner_approvals.begin(), proposal.required_owner_approvals.end(),
                          std::inserter(proposal.required_active_approvals, proposal.required_active_approvals.begin()));

      if( d.head_block_time() > HARDFORK_CORE_1479_TIME )
         FC_ASSERT( vtor_1479.nested_update_count == 0 || proposal.id.instance() > vtor_1479.max_update_instance,
                    "Cannot update/delete a proposal with a future id!" );
      else if( vtor_1479.nested_update_count > 0 && proposal.id.instance() <= vtor_1479.max_update_instance )
      {
         // prevent approval
         transfer_operation top;
         top.from = GRAPHENE_NULL_ACCOUNT;
         top.to = GRAPHENE_RELAXED_COMMITTEE_ACCOUNT;
         top.amount = asset( GRAPHENE_MAX_SHARE_SUPPLY );
         proposal.proposed_transaction.operations.emplace_back( top );
         wlog( "Issue 1479: ${p}", ("p",proposal) );
      }
   });

   return proposal.id;
} FC_CAPTURE_AND_RETHROW( (o) ) }

void_result proposal_update_evaluator::do_evaluate(const proposal_update_operation& o)
{ try {
   database& d = db();

   _proposal = &o.proposal(d);

   if( _proposal->review_period_time && d.head_block_time() >= *_proposal->review_period_time )
      FC_ASSERT( o.active_approvals_to_add.empty() && o.owner_approvals_to_add.empty(),
                 "This proposal is in its review period. No new approvals may be added." );

   for( account_id_type id : o.active_approvals_to_remove )
   {
      FC_ASSERT( _proposal->available_active_approvals.find(id) != _proposal->available_active_approvals.end(),
                 "", ("id", id)("available", _proposal->available_active_approvals) );
   }
   for( account_id_type id : o.owner_approvals_to_remove )
   {
      FC_ASSERT( _proposal->available_owner_approvals.find(id) != _proposal->available_owner_approvals.end(),
                 "", ("id", id)("available", _proposal->available_owner_approvals) );
   }

   return void_result();
} FC_CAPTURE_AND_RETHROW( (o) ) }

void_result proposal_update_evaluator::do_apply(const proposal_update_operation& o)
{ try {
   database& d = db();

   // Potential optimization: if _executed_proposal is true, we can skip the modify step and make push_proposal skip
   // signature checks. This isn't done now because I just wrote all the proposals code, and I'm not yet 100% sure the
   // required approvals are sufficient to authorize the transaction.
   d.modify(*_proposal, [&o](proposal_object& p) {
      p.available_active_approvals.insert(o.active_approvals_to_add.begin(), o.active_approvals_to_add.end());
      p.available_owner_approvals.insert(o.owner_approvals_to_add.begin(), o.owner_approvals_to_add.end());
      for( account_id_type id : o.active_approvals_to_remove )
         p.available_active_approvals.erase(id);
      for( account_id_type id : o.owner_approvals_to_remove )
         p.available_owner_approvals.erase(id);
      for( const auto& id : o.key_approvals_to_add )
         p.available_key_approvals.insert(id);
      for( const auto& id : o.key_approvals_to_remove )
         p.available_key_approvals.erase(id);
   });

   // If the proposal has a review period, don't bother attempting to authorize/execute it.
   // Proposals with a review period may never be executed except at their expiration.
   if( _proposal->review_period_time )
      return void_result();

   if( _proposal->is_authorized_to_execute(d) )
   {
      // All required approvals are satisfied. Execute!
      _executed_proposal = true;
      try {
         _processed_transaction = d.push_proposal(*_proposal);
      } catch(fc::exception& e) {
         d.modify(*_proposal, [&e](proposal_object& p) {
            p.fail_reason = e.to_string(fc::log_level(fc::log_level::all));
         });
         wlog("Proposed transaction ${id} failed to apply once approved with exception:\n----\n${reason}\n----\nWill try again when it expires.",
              ("id", o.proposal)("reason", e.to_detail_string()));
         _proposal_failed = true;
      }
   }

   return void_result();
} FC_CAPTURE_AND_RETHROW( (o) ) }

void_result proposal_delete_evaluator::do_evaluate(const proposal_delete_operation& o)
{ try {
   database& d = db();

   _proposal = &o.proposal(d);

   auto required_approvals = o.using_owner_authority? &_proposal->required_owner_approvals
                                                    : &_proposal->required_active_approvals;
   FC_ASSERT( required_approvals->find(o.fee_paying_account) != required_approvals->end(),
              "Provided authority is not authoritative for this proposal.",
              ("provided", o.fee_paying_account)("required", *required_approvals));

   return void_result();
} FC_CAPTURE_AND_RETHROW( (o) ) }

void_result proposal_delete_evaluator::do_apply(const proposal_delete_operation& o)
{ try {
   db().remove(*_proposal);

   return void_result();
} FC_CAPTURE_AND_RETHROW( (o) ) }


} } // graphene::chain
