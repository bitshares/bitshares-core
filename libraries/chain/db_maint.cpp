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

#include <boost/multiprecision/integer.hpp>

#include <fc/uint128.hpp>

#include <graphene/chain/database.hpp>
#include <graphene/chain/hardfork.hpp>

#include <graphene/chain/account_object.hpp>
#include <graphene/chain/asset_object.hpp>
#include <graphene/chain/budget_record_object.hpp>
#include <graphene/chain/buyback_object.hpp>
#include <graphene/chain/chain_property_object.hpp>
#include <graphene/chain/committee_member_object.hpp>
#include <graphene/chain/global_property_object.hpp>
#include <graphene/chain/special_authority_object.hpp>
#include <graphene/chain/vesting_balance_object.hpp>
#include <graphene/chain/vote_count.hpp>
#include <graphene/chain/witness_object.hpp>
#include <graphene/chain/worker_object.hpp>

namespace graphene { namespace chain {

template<class Index>
vector<std::reference_wrapper<const typename Index::object_type>> database::sort_votable_objects(size_t count) const
{
   using ObjectType = typename Index::object_type;
   const auto& all_objects = get_index_type<Index>().indices();
   count = std::min(count, all_objects.size());
   vector<std::reference_wrapper<const ObjectType>> refs;
   refs.reserve(all_objects.size());
   std::transform(all_objects.begin(), all_objects.end(),
                  std::back_inserter(refs),
                  [](const ObjectType& o) { return std::cref(o); });
   std::partial_sort(refs.begin(), refs.begin() + count, refs.end(),
                   [this](const ObjectType& a, const ObjectType& b)->bool {
      share_type oa_vote = _vote_tally_buffer[a.vote_id];
      share_type ob_vote = _vote_tally_buffer[b.vote_id];
      if( oa_vote != ob_vote )
         return oa_vote > ob_vote;
      return a.vote_id < b.vote_id;
   });

   refs.resize(count, refs.front());
   return refs;
}

template<class Index>
vector<std::reference_wrapper<const typename Index::object_type>> database::sort_standby_objects(size_t count) const
{
   using ObjectType = typename Index::object_type;
   const auto& all_objects = get_index_type<Index>().indices();
   size_t active_wit_count = count;
   size_t total_wit_count = 100;
   size_t all_object_count = all_objects.size();
   count = std::min(total_wit_count, all_object_count);
   vector<std::reference_wrapper<const ObjectType>> refs;
   refs.reserve(all_objects.size());
   std::transform(all_objects.begin(), all_objects.end(),
                  std::back_inserter(refs),
                  [](const ObjectType& o) { return std::cref(o); });
   std::partial_sort(refs.begin(), refs.begin() + count, refs.end(),
                   [this](const ObjectType& a, const ObjectType& b)->bool {
      share_type oa_vote = _vote_tally_buffer[a.vote_id];
      share_type ob_vote = _vote_tally_buffer[b.vote_id];
      if( oa_vote != ob_vote )
         return oa_vote > ob_vote;
      return a.vote_id < b.vote_id;
   });

   refs.resize(count, refs.front());
   if( active_wit_count < refs.size()){
      auto first = refs.begin() + count;
      auto last = refs.begin() + active_wit_count;
      vector<std::reference_wrapper<const ObjectType>> newVec(first, last);
      refs = newVec;
   }else{
      refs.clear();
      refs.reserve(0);
   }
   return refs;
}

void database::handle_core_inflation()
{
   const global_property_object& gpo = get_global_properties();
   
   if (gpo.parameters.core_inflation_amount > 0) 
   {
      const asset_dynamic_data_object& core_dd = get_core_dynamic_data();
      modify( core_dd, [gpo](asset_dynamic_data_object& addo) {
         addo.current_max_supply += gpo.parameters.core_inflation_amount;
      });
   }
}

void database::handle_marketing_fees()
{
   const global_property_object& gpo = get_global_properties();
   if (gpo.marketing_partner_account_name != "" ) 
   {
      auto& acnt_indx = get_index_type<account_index>();
      auto marketing_partner_itr = acnt_indx.indices().get<by_name>().find( gpo.marketing_partner_account_name );
      if ( marketing_partner_itr != acnt_indx.indices().get<by_name>().end() )
      {
         // Found current marketing partner account.
         // Now give the marketing partner all the accumulated fees for them and zero it out on the db
         const asset_dynamic_data_object& core_dd = get_core_dynamic_data();
         adjust_balance(marketing_partner_itr->id,  asset(core_dd.accumulated_fees_for_marketing_partner, asset_id_type()));
         modify( core_dd, [](asset_dynamic_data_object& addo) {
            addo.accumulated_fees_for_marketing_partner = 0;
         });
      }
   }
}

void database::handle_charity_fees()
{
   const global_property_object& gpo = get_global_properties();
   if (gpo.charity_account_name != "" ) 
   {
      auto& acnt_indx = get_index_type<account_index>();
      auto charity_itr = acnt_indx.indices().get<by_name>().find( gpo.charity_account_name );
      if ( charity_itr != acnt_indx.indices().get<by_name>().end() )
      {
         // Found current charity account.
         // Now give the charity all the accumulated fees for them and zero it out on the db
         const asset_dynamic_data_object& core_dd = get_core_dynamic_data();
         adjust_balance(charity_itr->id,  asset(core_dd.accumulated_fees_for_charity, asset_id_type()));
         modify( core_dd, [](asset_dynamic_data_object& addo) {
            addo.accumulated_fees_for_charity = 0;
         });
      }
   }
}

void database::handle_block_reward_payout()
{
   
   const global_property_object& gpo = get_global_properties();
   const dynamic_global_property_object& dpo = get_dynamic_global_properties();
   vector<witness_id_type> standby_witnesses_ids = gpo.standby_witnesses;
   const asset_dynamic_data_object& core_dd = get_core_dynamic_data();
   share_type network_reward = dpo.network_fund;
   share_type marketing_partner_reward = dpo.marketing_partner_fund;
   share_type standby_witness_reward = dpo.standby_witness_fund;

   // get all standby witness objects
   vector<optional<witness_object>> standby_witnesses; standby_witnesses.reserve(standby_witnesses_ids.size());
   std::transform(standby_witnesses_ids.begin(), standby_witnesses_ids.end(), std::back_inserter(standby_witnesses),
                  [this](witness_id_type id) -> optional<witness_object> {
      if(auto o = find(id))
         return *o;
      return {};
   });

   if(standby_witnesses.size() == 0){
      return;
   }

   //////////////////
   // START PAYOUT //
   //////////////////

   if (gpo.marketing_partner_account_name != "" ) 
   {
      auto& acnt_indx = get_index_type<account_index>();
      auto marketing_partner_itr = acnt_indx.indices().get<by_name>().find( gpo.marketing_partner_account_name );
      if ( marketing_partner_itr != acnt_indx.indices().get<by_name>().end() )
      {
         // payout marketing partner
         adjust_balance(marketing_partner_itr->id,  marketing_partner_reward);
      }
   }
   
   // payout network
   modify( core_dd, [&]( asset_dynamic_data_object& _core_dd )
   {
      _core_dd.current_supply += network_reward;
   } );

   // payout all standby witnesses
   share_type wit_count = standby_witnesses.size();
   share_type share = standby_witness_reward / wit_count;
   share_type first_wit_share = standby_witness_reward - ( share * ( wit_count -1 ));
   for (std::size_t i = 0; i != wit_count; ++i)
   {
     share_type current_share = i == 0 ? first_wit_share : share;
     deposit_witness_pay( *standby_witnesses[i], current_share );
   }

   // clear all reward accumulators 
   modify( dpo, [&]( dynamic_global_property_object& _dpo )
   {
      _dpo.standby_witness_fund = 0;
      _dpo.marketing_partner_fund = 0;
      _dpo.network_fund = 0;
   } );
   
}

template<class Type>
void database::perform_account_maintenance(Type tally_helper)
{
   const auto& bal_idx = get_index_type< account_balance_index >().indices().get< by_maintenance_flag >();
   if( bal_idx.begin() != bal_idx.end() )
   {
      auto bal_itr = bal_idx.rbegin();
      while( bal_itr->maintenance_flag )
      {
         const account_balance_object& bal_obj = *bal_itr;

         modify( get_account_stats_by_owner( bal_obj.owner ), [&bal_obj](account_statistics_object& aso) {
            aso.core_in_balance = bal_obj.balance;
         });

         modify( bal_obj, []( account_balance_object& abo ) {
            abo.maintenance_flag = false;
         });

         bal_itr = bal_idx.rbegin();
      }
   }

   const auto& stats_idx = get_index_type< account_stats_index >().indices().get< by_maintenance_seq >();
   auto stats_itr = stats_idx.lower_bound( true );

   while( stats_itr != stats_idx.end() )
   {
      const account_statistics_object& acc_stat = *stats_itr;
      const account_object& acc_obj = acc_stat.owner( *this );
      ++stats_itr;

      if( acc_stat.has_some_core_voting() )
         tally_helper( acc_obj, acc_stat );

      if( acc_stat.has_pending_fees() )
         acc_stat.process_fees( acc_obj, *this );
   }

}

/// @brief A visitor for @ref worker_type which calls pay_worker on the worker within
struct worker_pay_visitor
{
   private:
      share_type pay;
      database& db;

   public:
      worker_pay_visitor(share_type pay, database& db)
         : pay(pay), db(db) {}

      typedef void result_type;
      template<typename W>
      void operator()(W& worker)const
      {
         worker.pay_worker(pay, db);
      }
};

void database::update_worker_votes()
{
   const auto& idx = get_index_type<worker_index>().indices().get<by_account>();
   auto itr = idx.begin();
   auto itr_end = idx.end();
   bool allow_negative_votes = (head_block_time() < HARDFORK_607_TIME);
   while( itr != itr_end )
   {
      modify( *itr, [this,allow_negative_votes]( worker_object& obj )
      {
         obj.total_votes_for = _vote_tally_buffer[obj.vote_for];
         obj.total_votes_against = allow_negative_votes ? _vote_tally_buffer[obj.vote_against] : 0;
      });
      ++itr;
   }
}

void database::pay_workers( share_type& budget )
{
   const auto head_time = head_block_time();
//   ilog("Processing payroll! Available budget is ${b}", ("b", budget));
   vector<std::reference_wrapper<const worker_object>> active_workers;
   // TODO optimization: add by_expiration index to avoid iterating through all objects
   get_index_type<worker_index>().inspect_all_objects([head_time, &active_workers](const object& o) {
      const worker_object& w = static_cast<const worker_object&>(o);
      if( w.is_active(head_time) && w.approving_stake() > 0 )
         active_workers.emplace_back(w);
   });

   // worker with more votes is preferred
   // if two workers exactly tie for votes, worker with lower ID is preferred
   std::sort(active_workers.begin(), active_workers.end(), [](const worker_object& wa, const worker_object& wb) {
      share_type wa_vote = wa.approving_stake();
      share_type wb_vote = wb.approving_stake();
      if( wa_vote != wb_vote )
         return wa_vote > wb_vote;
      return wa.id < wb.id;
   });

   const auto last_budget_time = get_dynamic_global_properties().last_budget_time;
   const auto passed_time_ms = head_time - last_budget_time;
   const auto passed_time_count = passed_time_ms.count();
   const auto day_count = fc::days(1).count();
   for( uint32_t i = 0; i < active_workers.size() && budget > 0; ++i )
   {
      const worker_object& active_worker = active_workers[i];
      share_type requested_pay = active_worker.daily_pay;

      // Note: if there is a good chance that passed_time_count == day_count,
      //       for better performance, can avoid the 128 bit calculation by adding a check.
      //       Since it's not the case on BitShares mainnet, we're not using a check here.
      fc::uint128 pay(requested_pay.value);
      pay *= passed_time_count;
      pay /= day_count;
      requested_pay = pay.to_uint64();

      share_type actual_pay = std::min(budget, requested_pay);
      //ilog(" ==> Paying ${a} to worker ${w}", ("w", active_worker.id)("a", actual_pay));
      modify(active_worker, [&](worker_object& w) {
         w.worker.visit(worker_pay_visitor(actual_pay, *this));
      });

      budget -= actual_pay;
   }
}

void database::update_active_witnesses()
{ try {
   assert( _witness_count_histogram_buffer.size() > 0 );
   share_type stake_target = (_total_voting_stake-_witness_count_histogram_buffer[0]) / 2;

   /// accounts that vote for 0 or 1 witness do not get to express an opinion on
   /// the number of witnesses to have (they abstain and are non-voting accounts)

   share_type stake_tally = 0; 

   size_t witness_count = 0;
   if( stake_target > 0 )
   {
      while( (witness_count < _witness_count_histogram_buffer.size() - 1)
             && (stake_tally <= stake_target) )
      {
         stake_tally += _witness_count_histogram_buffer[++witness_count];
      }
   }

   const chain_property_object& cpo = get_chain_properties();

   witness_count = std::max( witness_count*2+1, (size_t)cpo.immutable_parameters.min_witness_count );
   auto wits = sort_votable_objects<witness_index>( witness_count );
   auto standby_wits = sort_standby_objects<witness_index>( witness_count );

   const global_property_object& gpo = get_global_properties();

   auto update_witness_total_votes = [this]( const witness_object& wit ) {
      modify( wit, [this]( witness_object& obj )
      {
         obj.total_votes = _vote_tally_buffer[obj.vote_id];
      });
   };

   if( _track_standby_votes )
   {
      const auto& all_witnesses = get_index_type<witness_index>().indices();
      for( const witness_object& wit : all_witnesses )
      {
         update_witness_total_votes( wit );
      }
   }
   else
   {
      for( const witness_object& wit : wits )
      {
         update_witness_total_votes( wit );
      }
   }

   // Update witness authority
   modify( get(GRAPHENE_WITNESS_ACCOUNT), [this,&wits]( account_object& a )
   {
      if( head_block_time() < HARDFORK_533_TIME )
      {
         uint64_t total_votes = 0;
         map<account_id_type, uint64_t> weights;
         a.active.weight_threshold = 0;
         a.active.clear();

         for( const witness_object& wit : wits )
         {
            weights.emplace(wit.witness_account, _vote_tally_buffer[wit.vote_id]);
            total_votes += _vote_tally_buffer[wit.vote_id];
         }

         // total_votes is 64 bits. Subtract the number of leading low bits from 64 to get the number of useful bits,
         // then I want to keep the most significant 16 bits of what's left.
         int8_t bits_to_drop = std::max(int(boost::multiprecision::detail::find_msb(total_votes)) - 15, 0);
         for( const auto& weight : weights )
         {
            // Ensure that everyone has at least one vote. Zero weights aren't allowed.
            uint16_t votes = std::max((weight.second >> bits_to_drop), uint64_t(1) );
            a.active.account_auths[weight.first] += votes;
            a.active.weight_threshold += votes;
         }

         a.active.weight_threshold /= 2;
         a.active.weight_threshold += 1;
      }
      else
      {
         vote_counter vc;
         for( const witness_object& wit : wits )
            vc.add( wit.witness_account, _vote_tally_buffer[wit.vote_id] );
         vc.finish( a.active );
      }
   } );

   modify( gpo, [&wits]( global_property_object& gp )
   {
      gp.active_witnesses.clear();
      gp.active_witnesses.reserve(wits.size());
      std::transform(wits.begin(), wits.end(),
                     std::inserter(gp.active_witnesses, gp.active_witnesses.end()),
                     [](const witness_object& w) {
         return w.id;
      });
   });

   modify( gpo, [&standby_wits]( global_property_object& gp )
   {
      gp.standby_witnesses.clear();
      gp.standby_witnesses.reserve(standby_wits.size());
      std::transform(standby_wits.begin(), standby_wits.end(),
                     std::inserter(gp.standby_witnesses, gp.standby_witnesses.end()),
                     [](const witness_object& w) {
         return w.id;
      });
   });

} FC_CAPTURE_AND_RETHROW() }

void database::update_active_committee_members()
{ try {
   assert( _committee_count_histogram_buffer.size() > 0 );
   share_type stake_target = (_total_voting_stake-_committee_count_histogram_buffer[0]) / 2;

   /// accounts that vote for 0 or 1 witness do not get to express an opinion on
   /// the number of witnesses to have (they abstain and are non-voting accounts)
   uint64_t stake_tally = 0; // _committee_count_histogram_buffer[0];
   size_t committee_member_count = 0;
   if( stake_target > 0 )
   {
      while( (committee_member_count < _committee_count_histogram_buffer.size() - 1)
             && (stake_tally <= stake_target.value) )
      {
         stake_tally += _committee_count_histogram_buffer[++committee_member_count];
      }
   }

   const chain_property_object& cpo = get_chain_properties();

   committee_member_count = std::max( committee_member_count*2+1, (size_t)cpo.immutable_parameters.min_committee_member_count );
   auto committee_members = sort_votable_objects<committee_member_index>( committee_member_count );

   auto update_committee_member_total_votes = [this]( const committee_member_object& cm ) {
      modify( cm, [this]( committee_member_object& obj )
      {
         obj.total_votes = _vote_tally_buffer[obj.vote_id];
      });
   };

   if( _track_standby_votes )
   {
      const auto& all_committee_members = get_index_type<committee_member_index>().indices();
      for( const committee_member_object& cm : all_committee_members )
      {
         update_committee_member_total_votes( cm );
      }
   }
   else
   {
      for( const committee_member_object& cm : committee_members )
      {
         update_committee_member_total_votes( cm );
      }
   }

   // Update committee authorities
   if( !committee_members.empty() )
   {
      const account_object& committee_account = get(GRAPHENE_COMMITTEE_ACCOUNT);
      modify( committee_account, [this,&committee_members](account_object& a)
      {
         if( head_block_time() < HARDFORK_533_TIME )
         {
            uint64_t total_votes = 0;
            map<account_id_type, uint64_t> weights;
            a.active.weight_threshold = 0;
            a.active.clear();

            for( const committee_member_object& cm : committee_members )
            {
               weights.emplace( cm.committee_member_account, _vote_tally_buffer[cm.vote_id] );
               total_votes += _vote_tally_buffer[cm.vote_id];
            }

            // total_votes is 64 bits. Subtract the number of leading low bits from 64 to get the number of useful bits,
            // then I want to keep the most significant 16 bits of what's left.
            int8_t bits_to_drop = std::max(int(boost::multiprecision::detail::find_msb(total_votes)) - 15, 0);
            for( const auto& weight : weights )
            {
               // Ensure that everyone has at least one vote. Zero weights aren't allowed.
               uint16_t votes = std::max((weight.second >> bits_to_drop), uint64_t(1) );
               a.active.account_auths[weight.first] += votes;
               a.active.weight_threshold += votes;
            }

            a.active.weight_threshold /= 2;
            a.active.weight_threshold += 1;
         }
         else
         {
            vote_counter vc;
            for( const committee_member_object& cm : committee_members )
               vc.add( cm.committee_member_account, _vote_tally_buffer[cm.vote_id] );
            vc.finish( a.active );
         }
      });
      modify( get(GRAPHENE_RELAXED_COMMITTEE_ACCOUNT), [&committee_account](account_object& a)
      {
         a.active = committee_account.active;
      });
   }
   modify( get_global_properties(), [&committee_members](global_property_object& gp)
   {
      gp.active_committee_members.clear();
      std::transform(committee_members.begin(), committee_members.end(),
                     std::inserter(gp.active_committee_members, gp.active_committee_members.begin()),
                     [](const committee_member_object& d) { return d.id; });
   });
} FC_CAPTURE_AND_RETHROW() }

void database::initialize_budget_record( fc::time_point_sec now, budget_record& rec )const
{
   const dynamic_global_property_object& dpo = get_dynamic_global_properties();
   const asset_object& core = get_core_asset();
   const asset_dynamic_data_object& core_dd = get_core_dynamic_data();

   rec.from_initial_reserve = core.reserved(*this);
   rec.from_accumulated_fees = core_dd.accumulated_fees;
   rec.from_unused_witness_budget = dpo.witness_budget;

   if(    (dpo.last_budget_time == fc::time_point_sec())
       || (now <= dpo.last_budget_time) )
   {
      rec.time_since_last_budget = 0;
      return;
   }

   int64_t dt = (now - dpo.last_budget_time).to_seconds();
   rec.time_since_last_budget = uint64_t( dt );

   // We'll consider accumulated_fees to be reserved at the BEGINNING
   // of the maintenance interval.  However, for speed we only
   // call modify() on the asset_dynamic_data_object once at the
   // end of the maintenance interval.  Thus the accumulated_fees
   // are available for the budget at this point, but not included
   // in core.reserved().
   share_type reserve = rec.from_initial_reserve + core_dd.accumulated_fees;
   // Similarly, we consider leftover witness_budget to be burned
   // at the BEGINNING of the maintenance interval.
   reserve += dpo.witness_budget;

   fc::uint128_t budget_u128 = reserve.value;
   budget_u128 *= uint64_t(dt);
   budget_u128 *= GRAPHENE_CORE_ASSET_CYCLE_RATE;
   //round up to the nearest satoshi -- this is necessary to ensure
   //   there isn't an "untouchable" reserve, and we will eventually
   //   be able to use the entire reserve
   budget_u128 += ((uint64_t(1) << GRAPHENE_CORE_ASSET_CYCLE_RATE_BITS) - 1);
   budget_u128 >>= GRAPHENE_CORE_ASSET_CYCLE_RATE_BITS;
   if( budget_u128 < reserve.value )
      rec.total_budget = share_type(budget_u128.to_uint64());
   else
      rec.total_budget = reserve;

   return;
}

/**
 * Update the budget for witnesses and workers.
 */
void database::process_budget()
{
   try
   {
      const global_property_object& gpo = get_global_properties();
      const dynamic_global_property_object& dpo = get_dynamic_global_properties();
      const asset_dynamic_data_object& core = get_core_dynamic_data();
      fc::time_point_sec now = head_block_time();

      int64_t time_to_maint = (dpo.next_maintenance_time - now).to_seconds();
      //
      // The code that generates the next maintenance time should
      //    only produce a result in the future.  If this assert
      //    fails, then the next maintenance time algorithm is buggy.
      //
      assert( time_to_maint > 0 );
      //
      // Code for setting chain parameters should validate
      //    block_interval > 0 (as well as the humans proposing /
      //    voting on changes to block interval).
      //
      assert( gpo.parameters.block_interval > 0 );
      uint64_t blocks_to_maint = (uint64_t(time_to_maint) + gpo.parameters.block_interval - 1) / gpo.parameters.block_interval;
      // blocks_to_maint = number of blocks in previous maintenance cycle
      // blocks_to_maint > 0 because time_to_maint > 0,
      // which means numerator is at least equal to block_interval
      
      budget_record rec;
      initialize_budget_record( now, rec );
      share_type available_funds = rec.total_budget;

      share_type witness_budget = gpo.parameters.witness_pay_per_block.value * blocks_to_maint;
      rec.requested_witness_budget = witness_budget;
      witness_budget = std::min(witness_budget, available_funds);
      rec.witness_budget = witness_budget;
      available_funds -= witness_budget;

      fc::uint128_t worker_budget_u128 = gpo.parameters.worker_budget_per_day.value;
      worker_budget_u128 *= uint64_t(time_to_maint);
      worker_budget_u128 /= 60*60*24;

      share_type worker_budget;
      if( worker_budget_u128 >= available_funds.value )
         worker_budget = available_funds;
      else
         worker_budget = worker_budget_u128.to_uint64();
      rec.worker_budget = worker_budget;
      available_funds -= worker_budget;

      share_type leftover_worker_funds = worker_budget;
      pay_workers(leftover_worker_funds);
      rec.leftover_worker_funds = leftover_worker_funds;
      available_funds += leftover_worker_funds;

      rec.supply_delta = rec.witness_budget
         + rec.worker_budget
         - rec.leftover_worker_funds
         - rec.from_accumulated_fees
         - rec.from_unused_witness_budget;

      modify(core, [&]( asset_dynamic_data_object& _core )
      {
         _core.current_supply = (_core.current_supply + rec.supply_delta );

         assert( rec.supply_delta ==
                                   witness_budget
                                 + worker_budget
                                 - leftover_worker_funds
                                 - _core.accumulated_fees
                                 - dpo.witness_budget
                                );
         _core.accumulated_fees = 0;
      });

      modify(dpo, [&]( dynamic_global_property_object& _dpo )
      {
         // Since initial witness_budget was rolled into
         // available_funds, we replace it with witness_budget
         // instead of adding it.
         _dpo.witness_budget = witness_budget;
         _dpo.last_budget_time = now;
      });

      create< budget_record_object >( [&]( budget_record_object& _rec )
      {
         _rec.time = head_block_time();
         _rec.record = rec;
      });

      // available_funds is money we could spend, but don't want to.
      // we simply let it evaporate back into the reserve.
   }
   FC_CAPTURE_AND_RETHROW()
}

template< typename Visitor >
void visit_special_authorities( const database& db, Visitor visit )
{
   const auto& sa_idx = db.get_index_type< special_authority_index >().indices().get<by_id>();

   for( const special_authority_object& sao : sa_idx )
   {
      const account_object& acct = sao.account(db);
      if( acct.owner_special_authority.which() != special_authority::tag< no_special_authority >::value )
      {
         visit( acct, true, acct.owner_special_authority );
      }
      if( acct.active_special_authority.which() != special_authority::tag< no_special_authority >::value )
      {
         visit( acct, false, acct.active_special_authority );
      }
   }
}

void update_top_n_authorities( database& db )
{
   visit_special_authorities( db,
   [&]( const account_object& acct, bool is_owner, const special_authority& auth )
   {
      if( auth.which() == special_authority::tag< top_holders_special_authority >::value )
      {
         // use index to grab the top N holders of the asset and vote_counter to obtain the weights

         const top_holders_special_authority& tha = auth.get< top_holders_special_authority >();
         vote_counter vc;
         const auto& bal_idx = db.get_index_type< account_balance_index >().indices().get< by_asset_balance >();
         uint8_t num_needed = tha.num_top_holders;
         if( num_needed == 0 )
            return;

         // find accounts
         const auto range = bal_idx.equal_range( boost::make_tuple( tha.asset ) );
         for( const account_balance_object& bal : boost::make_iterator_range( range.first, range.second ) )
         {
             assert( bal.asset_type == tha.asset );
             if( bal.owner == acct.id )
                continue;
             vc.add( bal.owner, bal.balance.value );
             --num_needed;
             if( num_needed == 0 )
                break;
         }

         db.modify( acct, [&]( account_object& a )
         {
            vc.finish( is_owner ? a.owner : a.active );
            if( !vc.is_empty() )
               a.top_n_control_flags |= (is_owner ? account_object::top_n_control_owner : account_object::top_n_control_active);
         } );
      }
   } );
}

void deprecate_annual_members( database& db )
{
   const auto& account_idx = db.get_index_type<account_index>().indices().get<by_id>();
   fc::time_point_sec now = db.head_block_time();
   for( const account_object& acct : account_idx )
   {
      try
      {
         transaction_evaluation_state upgrade_context(&db);
         upgrade_context.skip_fee_schedule_check = true;

         if( acct.is_annual_member( now ) )
         {
            account_upgrade_operation upgrade_vop;
            upgrade_vop.fee = asset( 0, asset_id_type() );
            upgrade_vop.account_to_upgrade = acct.id;
            upgrade_vop.upgrade_to_lifetime_member = true;
            db.apply_operation( upgrade_context, upgrade_vop );
         }
      }
      catch( const fc::exception& e )
      {
         // we can in fact get here, e.g. if asset issuer of buy/sell asset blacklists/whitelists the buyback account
         wlog( "Skipping annual member deprecate processing for account ${a} (${an}) at block ${n}; exception was ${e}",
               ("a", acct.id)("an", acct.name)("n", db.head_block_num())("e", e.to_detail_string()) );
         continue;
      }
   }
   return;
}

void database::perform_chain_maintenance(const signed_block& next_block, const global_property_object& global_props)
{
   const auto& gpo = get_global_properties();

   struct vote_tally_helper {
      database& d;
      const global_property_object& props;

      vote_tally_helper(database& d, const global_property_object& gpo)
         : d(d), props(gpo)
      {
         d._vote_tally_buffer.resize(props.next_available_vote_id);
         d._witness_count_histogram_buffer.resize(props.parameters.maximum_witness_count / 2 + 1);
         d._committee_count_histogram_buffer.resize(props.parameters.maximum_committee_count / 2 + 1);
         d._total_voting_stake = 0;
      }

      void operator()( const account_object& stake_account, const account_statistics_object& stats )
      {
         if( props.parameters.count_non_member_votes || stake_account.is_member(d.head_block_time()) )
         {
            // There may be a difference between the account whose stake is voting and the one specifying opinions.
            // Usually they're the same, but if the stake account has specified a voting_account, that account is the one
            // specifying the opinions.
            const account_object& opinion_account =
                  (stake_account.options.voting_account ==
                   GRAPHENE_PROXY_TO_SELF_ACCOUNT)? stake_account
                                     : d.get(stake_account.options.voting_account);

            uint64_t voting_stake = stats.total_core_in_orders.value
                  + (stake_account.cashback_vb.valid() ? (*stake_account.cashback_vb)(d).balance.amount.value: 0)
                  + stats.core_in_balance.value;

            for( vote_id_type id : opinion_account.options.votes )
            {
               uint32_t offset = id.instance();
               // if they somehow managed to specify an illegal offset, ignore it.
               if( offset < d._vote_tally_buffer.size() )
                  d._vote_tally_buffer[offset] += voting_stake;
            }

            if( opinion_account.options.num_witness <= props.parameters.maximum_witness_count )
            {
               uint16_t offset = std::min(size_t(opinion_account.options.num_witness/2),
                                          d._witness_count_histogram_buffer.size() - 1);
               // votes for a number greater than maximum_witness_count
               // are turned into votes for maximum_witness_count.
               //
               // in particular, this takes care of the case where a
               // member was voting for a high number, then the
               // parameter was lowered.
               d._witness_count_histogram_buffer[offset] += voting_stake;
            }
            if( opinion_account.options.num_committee <= props.parameters.maximum_committee_count )
            {
               uint16_t offset = std::min(size_t(opinion_account.options.num_committee/2),
                                          d._committee_count_histogram_buffer.size() - 1);
               // votes for a number greater than maximum_committee_count
               // are turned into votes for maximum_committee_count.
               //
               // same rationale as for witnesses
               d._committee_count_histogram_buffer[offset] += voting_stake;
            }

            d._total_voting_stake += voting_stake;
         }
      }
   } tally_helper(*this, gpo);

   perform_account_maintenance( tally_helper );
   handle_marketing_fees();
   handle_charity_fees();
   handle_block_reward_payout();

   struct clear_canary {
      clear_canary(vector<uint64_t>& target): target(target){}
      ~clear_canary() { target.clear(); }
   private:
      vector<uint64_t>& target;
   };
   clear_canary a(_witness_count_histogram_buffer),
                b(_committee_count_histogram_buffer),
                c(_vote_tally_buffer);

   update_top_n_authorities(*this);
   update_active_witnesses();
   update_active_committee_members();
   update_worker_votes();

   const auto& dgpo = get_dynamic_global_properties();
   
   modify(gpo, [&dgpo](global_property_object& p) {
      // Remove scaling of account registration fee
      p.parameters.get_mutable_fees().get<account_create_operation>().basic_fee >>= p.parameters.account_fee_scale_bitshifts *
            (dgpo.accounts_registered_this_interval / p.parameters.accounts_per_fee_scale);

      if( p.pending_parameters )
      {
         p.parameters = std::move(*p.pending_parameters);
         p.pending_parameters.reset();
      }
   });

   auto next_maintenance_time = dgpo.next_maintenance_time;
   auto maintenance_interval = gpo.parameters.maintenance_interval;

   if( next_maintenance_time <= next_block.timestamp )
   {
      if( next_block.block_num() == 1 )
         next_maintenance_time = time_point_sec() +
               (((next_block.timestamp.sec_since_epoch() / maintenance_interval) + 1) * maintenance_interval);
      else
      {
         // We want to find the smallest k such that next_maintenance_time + k * maintenance_interval > head_block_time()
         //  This implies k > ( head_block_time() - next_maintenance_time ) / maintenance_interval
         //
         // Let y be the right-hand side of this inequality, i.e.
         // y = ( head_block_time() - next_maintenance_time ) / maintenance_interval
         //
         // and let the fractional part f be y-floor(y).  Clearly 0 <= f < 1.
         // We can rewrite f = y-floor(y) as floor(y) = y-f.
         //
         // Clearly k = floor(y)+1 has k > y as desired.  Now we must
         // show that this is the least such k, i.e. k-1 <= y.
         //
         // But k-1 = floor(y)+1-1 = floor(y) = y-f <= y.
         // So this k suffices.
         //
         auto y = (head_block_time() - next_maintenance_time).to_seconds() / maintenance_interval;
         next_maintenance_time += (y+1) * maintenance_interval;
      }
   }

   // Handle hard forks here
   // TODO: remove BitShares specific hardforks.

   if( (dgpo.next_maintenance_time < HARDFORK_613_TIME) && (next_maintenance_time >= HARDFORK_613_TIME) )
      deprecate_annual_members(*this);

   modify(dgpo, [next_maintenance_time](dynamic_global_property_object& d) {
      d.next_maintenance_time = next_maintenance_time;
      d.accounts_registered_this_interval = 0;
   });

   // process_budget needs to run at the bottom because
   //   it needs to know the next_maintenance_time
   process_budget();
}

} }
