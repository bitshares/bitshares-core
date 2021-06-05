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

namespace graphene { namespace chain {

namespace detail {
   void check_asset_options_hf_1774(const fc::time_point_sec& block_time, const asset_options& options);

   void check_asset_options_hf_bsip_48_75(const fc::time_point_sec& block_time, const asset_options& options);
   void check_bitasset_options_hf_bsip_48_75(const fc::time_point_sec& block_time, const bitasset_options& options);
   void check_asset_update_extensions_hf_bsip_48_75( const fc::time_point_sec& block_time,
                                                     const asset_update_operation::ext& extensions );

   void check_asset_publish_feed_extensions_hf_bsip77( const fc::time_point_sec& block_time,
                                                       const asset_publish_feed_operation::ext& extensions );
   void check_bitasset_options_hf_bsip77(const fc::time_point_sec& block_time, const bitasset_options& options);

   void check_bitasset_options_hf_bsip74(const fc::time_point_sec& block_time,
                                         const bitasset_options& options); // HF_REMOVABLE

   void check_asset_options_hf_bsip81(const fc::time_point_sec& block_time, const asset_options& options);

   void check_bitasset_options_hf_bsip87(const fc::time_point_sec& block_time,
                                         const bitasset_options& options); // HF_REMOVABLE

   void check_asset_claim_fees_hardfork_87_74_collatfee(const fc::time_point_sec& block_time,
                                                        const asset_claim_fees_operation& op); // HF_REMOVABLE
}

struct proposal_operation_hardfork_visitor
{
   typedef void result_type;
   const database& db;
   const fc::time_point_sec block_time;
   const fc::time_point_sec next_maintenance_time;

   proposal_operation_hardfork_visitor( const database& _db, const fc::time_point_sec bt )
   : db( _db ), block_time(bt), next_maintenance_time( db.get_dynamic_global_properties().next_maintenance_time ) {}

   template<typename T>
   void operator()(const T &v) const {}

   void operator()(const graphene::chain::asset_create_operation &v) const {
      detail::check_asset_options_hf_1774(block_time, v.common_options);
      detail::check_asset_options_hf_bsip_48_75(block_time, v.common_options);
      detail::check_asset_options_hf_bsip81(block_time, v.common_options);
      if( v.bitasset_opts.valid() ) {
         detail::check_bitasset_options_hf_bsip_48_75( block_time, *v.bitasset_opts );
         detail::check_bitasset_options_hf_bsip74( block_time, *v.bitasset_opts ); // HF_REMOVABLE
         detail::check_bitasset_options_hf_bsip77( block_time, *v.bitasset_opts ); // HF_REMOVABLE
         detail::check_bitasset_options_hf_bsip87( block_time, *v.bitasset_opts ); // HF_REMOVABLE
      }

      // TODO move as many validations as possible to validate() if not triggered before hardfork
      if( HARDFORK_BSIP_48_75_PASSED( block_time ) )
      {
         v.common_options.validate_flags( v.bitasset_opts.valid() );
      }
   }

   void operator()(const graphene::chain::asset_update_operation &v) const {
      detail::check_asset_options_hf_1774(block_time, v.new_options);
      detail::check_asset_options_hf_bsip_48_75(block_time, v.new_options);
      detail::check_asset_options_hf_bsip81(block_time, v.new_options);

      detail::check_asset_update_extensions_hf_bsip_48_75( block_time, v.extensions.value );

      // TODO move as many validations as possible to validate() if not triggered before hardfork
      if( HARDFORK_BSIP_48_75_PASSED( block_time ) )
      {
         v.new_options.validate_flags( true );
      }

   }

   void operator()(const graphene::chain::asset_update_bitasset_operation &v) const {
      detail::check_bitasset_options_hf_bsip_48_75( block_time, v.new_options );
      detail::check_bitasset_options_hf_bsip74( block_time, v.new_options ); // HF_REMOVABLE
      detail::check_bitasset_options_hf_bsip77( block_time, v.new_options ); // HF_REMOVABLE
      detail::check_bitasset_options_hf_bsip87( block_time, v.new_options ); // HF_REMOVABLE
   }

   void operator()(const graphene::chain::asset_claim_fees_operation &v) const {
      detail::check_asset_claim_fees_hardfork_87_74_collatfee(block_time, v); // HF_REMOVABLE
   }

   void operator()(const graphene::chain::asset_publish_feed_operation &v) const {

      detail::check_asset_publish_feed_extensions_hf_bsip77( block_time, v.extensions.value );

   }

   void operator()(const graphene::chain::committee_member_update_global_parameters_operation &op) const {
      if (block_time < HARDFORK_CORE_1468_TIME) {
         FC_ASSERT(!op.new_parameters.extensions.value.updatable_htlc_options.valid(), 
               "Unable to set HTLC options before hardfork 1468");
         FC_ASSERT(!op.new_parameters.current_fees->exists<htlc_create_operation>());
         FC_ASSERT(!op.new_parameters.current_fees->exists<htlc_redeem_operation>());
         FC_ASSERT(!op.new_parameters.current_fees->exists<htlc_extend_operation>());
      }
      if (!HARDFORK_BSIP_40_PASSED(block_time)) {
         FC_ASSERT(!op.new_parameters.extensions.value.custom_authority_options.valid(),
                   "Unable to set Custom Authority Options before hardfork BSIP 40");
         FC_ASSERT(!op.new_parameters.current_fees->exists<custom_authority_create_operation>(),
                   "Unable to define fees for custom authority operations prior to hardfork BSIP 40");
         FC_ASSERT(!op.new_parameters.current_fees->exists<custom_authority_update_operation>(),
                   "Unable to define fees for custom authority operations prior to hardfork BSIP 40");
         FC_ASSERT(!op.new_parameters.current_fees->exists<custom_authority_delete_operation>(),
                   "Unable to define fees for custom authority operations prior to hardfork BSIP 40");
      }
      if (!HARDFORK_BSIP_85_PASSED(block_time)) {
         FC_ASSERT(!op.new_parameters.extensions.value.maker_fee_discount_percent.valid(),
                   "Unable to set maker_fee_discount_percent before hardfork BSIP 85");
      }
      if (!HARDFORK_BSIP_86_PASSED(block_time)) {
         FC_ASSERT(!op.new_parameters.extensions.value.market_fee_network_percent.valid(),
                   "Unable to set market_fee_network_percent before hardfork BSIP 86");
      }
      if (!HARDFORK_CORE_2103_PASSED(block_time)) {
         FC_ASSERT(!op.new_parameters.current_fees->exists<ticket_create_operation>(),
                   "Unable to define fees for ticket operations prior to hardfork 2103");
         FC_ASSERT(!op.new_parameters.current_fees->exists<ticket_update_operation>(),
                   "Unable to define fees for ticket operations prior to hardfork 2103");
      }
      if (!HARDFORK_LIQUIDITY_POOL_PASSED(block_time)) {
         FC_ASSERT(!op.new_parameters.current_fees->exists<liquidity_pool_create_operation>(),
                   "Unable to define fees for liquidity pool operations prior to the LP hardfork");
         FC_ASSERT(!op.new_parameters.current_fees->exists<liquidity_pool_delete_operation>(),
                   "Unable to define fees for liquidity pool operations prior to the LP hardfork");
         FC_ASSERT(!op.new_parameters.current_fees->exists<liquidity_pool_deposit_operation>(),
                   "Unable to define fees for liquidity pool operations prior to the LP hardfork");
         FC_ASSERT(!op.new_parameters.current_fees->exists<liquidity_pool_withdraw_operation>(),
                   "Unable to define fees for liquidity pool operations prior to the LP hardfork");
         FC_ASSERT(!op.new_parameters.current_fees->exists<liquidity_pool_exchange_operation>(),
                   "Unable to define fees for liquidity pool operations prior to the LP hardfork");
      }
      if (!HARDFORK_CORE_2351_PASSED(block_time)) {
         FC_ASSERT(!op.new_parameters.current_fees->exists<samet_fund_create_operation>(),
                   "Unable to define fees for samet fund operations prior to the core-2351 hardfork");
         FC_ASSERT(!op.new_parameters.current_fees->exists<samet_fund_delete_operation>(),
                   "Unable to define fees for samet fund operations prior to the core-2351 hardfork");
         FC_ASSERT(!op.new_parameters.current_fees->exists<samet_fund_update_operation>(),
                   "Unable to define fees for samet fund operations prior to the core-2351 hardfork");
         FC_ASSERT(!op.new_parameters.current_fees->exists<samet_fund_borrow_operation>(),
                   "Unable to define fees for samet fund operations prior to the core-2351 hardfork");
         FC_ASSERT(!op.new_parameters.current_fees->exists<samet_fund_repay_operation>(),
                   "Unable to define fees for samet fund operations prior to the core-2351 hardfork");
      }
   }
   void operator()(const graphene::chain::htlc_create_operation &op) const {
      FC_ASSERT( block_time >= HARDFORK_CORE_1468_TIME, "Not allowed until hardfork 1468" );
      if (block_time < HARDFORK_CORE_BSIP64_TIME)
      {
         // memo field added at harfork BSIP64
         // NOTE: both of these checks can be removed after hardfork time
         FC_ASSERT( !op.extensions.value.memo.valid(), 
               "Memo unavailable until after HARDFORK BSIP64");
         // HASH160 added at hardfork BSIP64
         FC_ASSERT( !op.preimage_hash.is_type<fc::hash160>(),
               "HASH160 unavailable until after HARDFORK BSIP64" );   
      }
   }
   void operator()(const graphene::chain::htlc_redeem_operation&) const {
      FC_ASSERT( block_time >= HARDFORK_CORE_1468_TIME, "Not allowed until hardfork 1468" );
   }
   void operator()(const graphene::chain::htlc_extend_operation&) const {
      FC_ASSERT( block_time >= HARDFORK_CORE_1468_TIME, "Not allowed until hardfork 1468" );
   }
   void operator()(const graphene::chain::custom_authority_create_operation&) const {
      FC_ASSERT( HARDFORK_BSIP_40_PASSED(block_time), "Not allowed until hardfork BSIP 40" );
   }
   void operator()(const graphene::chain::custom_authority_update_operation&) const {
      FC_ASSERT( HARDFORK_BSIP_40_PASSED(block_time), "Not allowed until hardfork BSIP 40" );
   }
   void operator()(const graphene::chain::custom_authority_delete_operation&) const {
      FC_ASSERT( HARDFORK_BSIP_40_PASSED(block_time), "Not allowed until hardfork BSIP 40" );
   }
   void operator()(const graphene::chain::ticket_create_operation&) const {
      FC_ASSERT( HARDFORK_CORE_2103_PASSED(block_time), "Not allowed until hardfork 2103" );
   }
   void operator()(const graphene::chain::ticket_update_operation&) const {
      FC_ASSERT( HARDFORK_CORE_2103_PASSED(block_time), "Not allowed until hardfork 2103" );
   }
   void operator()(const graphene::chain::liquidity_pool_create_operation&) const {
      FC_ASSERT( HARDFORK_LIQUIDITY_POOL_PASSED(block_time), "Not allowed until the LP hardfork" );
   }
   void operator()(const graphene::chain::liquidity_pool_delete_operation&) const {
      FC_ASSERT( HARDFORK_LIQUIDITY_POOL_PASSED(block_time), "Not allowed until the LP hardfork" );
   }
   void operator()(const graphene::chain::liquidity_pool_deposit_operation&) const {
      FC_ASSERT( HARDFORK_LIQUIDITY_POOL_PASSED(block_time), "Not allowed until the LP hardfork" );
   }
   void operator()(const graphene::chain::liquidity_pool_withdraw_operation&) const {
      FC_ASSERT( HARDFORK_LIQUIDITY_POOL_PASSED(block_time), "Not allowed until the LP hardfork" );
   }
   void operator()(const graphene::chain::liquidity_pool_exchange_operation&) const {
      FC_ASSERT( HARDFORK_LIQUIDITY_POOL_PASSED(block_time), "Not allowed until the LP hardfork" );
   }
   void operator()(const graphene::chain::samet_fund_create_operation&) const {
      FC_ASSERT( HARDFORK_CORE_2351_PASSED(block_time), "Not allowed until the core-2351 hardfork" );
   }
   void operator()(const graphene::chain::samet_fund_delete_operation&) const {
      FC_ASSERT( HARDFORK_CORE_2351_PASSED(block_time), "Not allowed until the core-2351 hardfork" );
   }
   void operator()(const graphene::chain::samet_fund_update_operation&) const {
      FC_ASSERT( HARDFORK_CORE_2351_PASSED(block_time), "Not allowed until the core-2351 hardfork" );
   }
   void operator()(const graphene::chain::samet_fund_borrow_operation&) const {
      FC_ASSERT( HARDFORK_CORE_2351_PASSED(block_time), "Not allowed until the core-2351 hardfork" );
   }
   void operator()(const graphene::chain::samet_fund_repay_operation&) const {
      FC_ASSERT( HARDFORK_CORE_2351_PASSED(block_time), "Not allowed until the core-2351 hardfork" );
   }

   // loop and self visit in proposals
   void operator()(const graphene::chain::proposal_create_operation &v) const {
      bool already_contains_proposal_update = false;

      for (const op_wrapper &op : v.proposed_ops)
      {
         op.op.visit(*this);
         // Do not allow more than 1 proposal_update in a proposal
         if ( op.op.is_type<proposal_update_operation>() )
         {
            FC_ASSERT( !already_contains_proposal_update, 
                  "At most one proposal update can be nested in a proposal!" );
            already_contains_proposal_update = true;
         }
      }
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

void_result proposal_create_evaluator::do_evaluate( const proposal_create_operation& o )
{ try {
   const database& d = db();

   // Calling the proposal hardfork visitor
   const fc::time_point_sec block_time = d.head_block_time();
   proposal_operation_hardfork_visitor vtor( d, block_time );
   vtor( o );
   if( block_time < HARDFORK_CORE_214_TIME )
   {
      // cannot be removed after hf, unfortunately
      hardfork_visitor_214 hf214;
      for( const op_wrapper &op : o.proposed_ops )
         op.op.visit( hf214 );
   }
   vtor_1479( o );

   const auto& global_parameters = d.get_global_properties().parameters;

   FC_ASSERT( o.expiration_time > block_time, "Proposal has already expired on creation." );
   FC_ASSERT( o.expiration_time <= block_time + global_parameters.maximum_proposal_lifetime,
              "Proposal expiration time is too far in the future." );
   FC_ASSERT( !o.review_period_seconds || 
         fc::seconds( *o.review_period_seconds ) < ( o.expiration_time - block_time ),
         "Proposal review period must be less than its overall lifetime." );

   // Find all authorities required by the proposed operations
   flat_set<account_id_type> tmp_required_active_auths;
   vector<authority> other;
   for( auto& op : o.proposed_ops )
   {
      operation_get_required_authorities( op.op, tmp_required_active_auths, _required_owner_auths, other,
                                          MUST_IGNORE_CUSTOM_OP_REQD_AUTHS( block_time ) );
   }
   // All accounts which must provide both owner and active authority should be omitted from the 
   // active authority set; owner authority approval implies active authority approval.
   std::set_difference( tmp_required_active_auths.begin(), tmp_required_active_auths.end(),
                        _required_owner_auths.begin(), _required_owner_auths.end(),
                        std::inserter( _required_active_auths, _required_active_auths.begin() ) );

   // TODO: what about other???
   FC_ASSERT ( other.empty(),
               "Proposals containing operations requiring non-account authorities are not yet implemented." );

   // If we're dealing with the committee authority, make sure this transaction has a sufficient review period.
   if( _required_active_auths.count( GRAPHENE_COMMITTEE_ACCOUNT ) > 0 ||
       _required_owner_auths.count( GRAPHENE_COMMITTEE_ACCOUNT ) > 0 )
   {
      GRAPHENE_ASSERT( o.review_period_seconds.valid(),
                       proposal_create_review_period_required,
                       "Review period not given, but at least ${min} required",
                       ("min", global_parameters.committee_proposal_review_period) );
      GRAPHENE_ASSERT( *o.review_period_seconds >= global_parameters.committee_proposal_review_period,
                       proposal_create_review_period_insufficient,
                       "Review period of ${t} specified, but at least ${min} required",
                       ("t", *o.review_period_seconds)
                       ("min", global_parameters.committee_proposal_review_period) );
   }

   for( const op_wrapper& op : o.proposed_ops )
      _proposed_trx.operations.push_back( op.op );

   _proposed_trx.validate();

   return void_result();
} FC_CAPTURE_AND_RETHROW( (o) ) }

object_id_type proposal_create_evaluator::do_apply( const proposal_create_operation& o )
{ try {
   database& d = db();
   auto chain_time = d.head_block_time();

   const proposal_object& proposal = d.create<proposal_object>( [&o, this, chain_time](proposal_object& proposal) {
      _proposed_trx.expiration = o.expiration_time;
      proposal.proposed_transaction = _proposed_trx;
      proposal.expiration_time = o.expiration_time;
      proposal.proposer = o.fee_paying_account;
      if( o.review_period_seconds )
         proposal.review_period_time = o.expiration_time - *o.review_period_seconds;

      //Populate the required approval sets
      proposal.required_owner_approvals.insert( _required_owner_auths.begin(), _required_owner_auths.end() );
      proposal.required_active_approvals.insert( _required_active_auths.begin(), _required_active_auths.end() );

      if( chain_time > HARDFORK_CORE_1479_TIME )
         FC_ASSERT( vtor_1479.nested_update_count == 0 || proposal.id.instance() > vtor_1479.max_update_instance,
                    "Cannot update/delete a proposal with a future id!" );
      else if( vtor_1479.nested_update_count > 0 && proposal.id.instance() <= vtor_1479.max_update_instance )
      {
         // Note: This happened on mainnet, proposal 1.10.17503
         // prevent approval
         transfer_operation top;
         top.from = GRAPHENE_NULL_ACCOUNT;
         top.to = GRAPHENE_RELAXED_COMMITTEE_ACCOUNT;
         top.amount = asset( GRAPHENE_MAX_SHARE_SUPPLY );
         proposal.proposed_transaction.operations.emplace_back( top );
      }
   });

   return proposal.id;
} FC_CAPTURE_AND_RETHROW( (o) ) }

void_result proposal_update_evaluator::do_evaluate( const proposal_update_operation& o )
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

   // Potential optimization: if _executed_proposal is true, we can skip the modify step and make push_proposal 
   // skip signature checks. This isn't done now because I just wrote all the proposals code, and I'm not yet 
   // 100% sure the required approvals are sufficient to authorize the transaction.
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
