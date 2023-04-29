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

#include <graphene/chain/database.hpp>

#include <graphene/chain/hardfork.hpp>

#include <graphene/chain/asset_object.hpp>
#include <graphene/chain/chain_property_object.hpp>
#include <graphene/chain/global_property_object.hpp>
#include <graphene/chain/market_object.hpp>
#include <graphene/chain/custom_authority_object.hpp>

namespace graphene { namespace chain {

const asset_object& database::get_core_asset() const
{
   return *_p_core_asset_obj;
}

const asset_dynamic_data_object& database::get_core_dynamic_data() const
{
   return *_p_core_dynamic_data_obj;
}

const global_property_object& database::get_global_properties()const
{
   return *_p_global_prop_obj;
}

const chain_property_object& database::get_chain_properties()const
{
   return *_p_chain_property_obj;
}

const dynamic_global_property_object& database::get_dynamic_global_properties() const
{
   return *_p_dyn_global_prop_obj;
}

const fee_schedule&  database::current_fee_schedule()const
{
   return get_global_properties().parameters.get_current_fees();
}

time_point_sec database::head_block_time()const
{
   return get_dynamic_global_properties().time;
}

uint32_t database::head_block_num()const
{
   return get_dynamic_global_properties().head_block_number;
}

block_id_type database::head_block_id()const
{
   return get_dynamic_global_properties().head_block_id;
}

decltype( chain_parameters::block_interval ) database::block_interval( )const
{
   return get_global_properties().parameters.block_interval;
}

const chain_id_type& database::get_chain_id( )const
{
   return get_chain_properties().chain_id;
}

const node_property_object& database::get_node_properties()const
{
   return _node_property_object;
}

node_property_object& database::node_properties()
{
   return _node_property_object;
}

vector<authority> database::get_viable_custom_authorities(
      account_id_type account, const operation &op,
      rejected_predicate_map* rejected_authorities) const
{
   const auto& index = get_index_type<custom_authority_index>().indices().get<by_account_custom>();
   auto range = index.equal_range(boost::make_tuple(account, unsigned_int(op.which()), true));

   auto is_valid = [now=head_block_time()](const custom_authority_object& auth) { return auth.is_valid(now); };
   vector<std::reference_wrapper<const custom_authority_object>> valid_auths;
   std::copy_if(range.first, range.second, std::back_inserter(valid_auths), is_valid);

   vector<authority> results;
   for (const auto& cust_auth : valid_auths) {
      try {
         auto result = cust_auth.get().get_predicate()(op);
         if (result.success)
            results.emplace_back(cust_auth.get().auth);
         else if (rejected_authorities != nullptr)
            rejected_authorities->insert(std::make_pair(cust_auth.get().get_id(), std::move(result)));
      } catch (fc::exception& e) {
         if (rejected_authorities != nullptr)
            rejected_authorities->insert(std::make_pair(cust_auth.get().get_id(), std::move(e)));
      }
   }

   return results;
}

uint32_t database::last_non_undoable_block_num() const
{
   //see https://github.com/bitshares/bitshares-core/issues/377
   /*
   There is a case when a value of undo_db.size() is greater then head_block_num(),
   and as result we get a wrong value for last_non_undoable_block_num.
   To resolve it we should take into account a number of active_sessions in calculations of
   last_non_undoable_block_num (active sessions are related to a new block which is under generation).
   */
   return head_block_num() - ( _undo_db.size() - _undo_db.active_sessions() );
}

const account_statistics_object& database::get_account_stats_by_owner( account_id_type owner )const
{
   return account_statistics_id_type(owner.instance)(*this);
}

const witness_schedule_object& database::get_witness_schedule_object()const
{
   return *_p_witness_schedule_obj;
}

const limit_order_object* database::find_settled_debt_order( const asset_id_type& a )const
{
   const auto& limit_index = get_index_type<limit_order_index>().indices().get<by_is_settled_debt>();
   auto itr = limit_index.lower_bound( std::make_tuple( true, a ) );
   if( itr != limit_index.end() && itr->receive_asset_id() == a )
      return &(*itr);
   return nullptr;
}

const call_order_object* database::find_least_collateralized_short( const asset_bitasset_data_object& bitasset,
                                                                    bool force_by_collateral_index )const
{
   bool find_by_collateral = true;
   if( !force_by_collateral_index )
      // core-1270 hard fork : call price caching issue
      find_by_collateral = ( get_dynamic_global_properties().next_maintenance_time > HARDFORK_CORE_1270_TIME );

   const call_order_object* call_ptr = nullptr; // place holder

   auto call_min = price::min( bitasset.options.short_backing_asset, bitasset.asset_id );

   if( !find_by_collateral ) // before core-1270 hard fork, check with call_price
   {
      const auto& call_price_index = get_index_type<call_order_index>().indices().get<by_price>();
      auto call_itr = call_price_index.lower_bound( call_min );
      if( call_itr != call_price_index.end() ) // found a call order
         call_ptr = &(*call_itr);
   }
   else // after core-1270 hard fork, check with collateralization
   {
      // Note: it is safe to check here even if there is no call order due to individual settlements
      const auto& call_collateral_index = get_index_type<call_order_index>().indices().get<by_collateral>();
      auto call_itr = call_collateral_index.lower_bound( call_min );
      if( call_itr != call_collateral_index.end() ) // found a call order
         call_ptr = &(*call_itr);
   }
   if( !call_ptr ) // not found
      return nullptr;
   if( call_ptr->debt_type() != bitasset.asset_id ) // call order is of another asset
      return nullptr;
   return call_ptr;
}

} }
