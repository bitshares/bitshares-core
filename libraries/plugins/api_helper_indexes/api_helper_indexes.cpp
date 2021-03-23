/*
 * Copyright (c) 2018 api_helper_indexes and contributors.
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

#include <graphene/api_helper_indexes/api_helper_indexes.hpp>
#include <graphene/chain/liquidity_pool_object.hpp>
#include <graphene/chain/market_object.hpp>
#include <graphene/chain/proposal_object.hpp>

namespace graphene { namespace api_helper_indexes {

void amount_in_collateral_index::object_inserted( const object& objct )
{ try {
   const call_order_object& o = static_cast<const call_order_object&>( objct );

   {
      auto itr = in_collateral.find( o.collateral_type() );
      if( itr == in_collateral.end() )
         in_collateral[o.collateral_type()] = o.collateral;
      else
         itr->second += o.collateral;
   }

   {
      auto itr = backing_collateral.find( o.debt_type() );
      if( itr == backing_collateral.end() )
         backing_collateral[o.debt_type()] = o.collateral;
      else
         itr->second += o.collateral;
   }

} FC_CAPTURE_AND_RETHROW( (objct) ) }

void amount_in_collateral_index::object_removed( const object& objct )
{ try {
   const call_order_object& o = static_cast<const call_order_object&>( objct );

   {
      auto itr = in_collateral.find( o.collateral_type() );
      if( itr != in_collateral.end() ) // should always be true
         itr->second -= o.collateral;
   }

   {
      auto itr = backing_collateral.find( o.debt_type() );
      if( itr != backing_collateral.end() ) // should always be true
         itr->second -= o.collateral;
   }

} FC_CAPTURE_AND_RETHROW( (objct) ) }

void amount_in_collateral_index::about_to_modify( const object& objct )
{ try {
   object_removed( objct );
} FC_CAPTURE_AND_RETHROW( (objct) ) }

void amount_in_collateral_index::object_modified( const object& objct )
{ try {
   object_inserted( objct );
} FC_CAPTURE_AND_RETHROW( (objct) ) }

share_type amount_in_collateral_index::get_amount_in_collateral( const asset_id_type& asst )const
{ try {
   auto itr = in_collateral.find( asst );
   if( itr == in_collateral.end() ) return 0;
   return itr->second;
} FC_CAPTURE_AND_RETHROW( (asst) ) }

share_type amount_in_collateral_index::get_backing_collateral( const asset_id_type& asst )const
{ try {
   auto itr = backing_collateral.find( asst );
   if( itr == backing_collateral.end() ) return 0;
   return itr->second;
} FC_CAPTURE_AND_RETHROW( (asst) ) }

void asset_in_liquidity_pools_index::object_inserted( const object& objct )
{ try {
   const auto& o = static_cast<const liquidity_pool_object&>( objct );
   asset_in_pools_map[ o.asset_a ].insert( o.id ); // Note: [] operator will create an entry if not found
   asset_in_pools_map[ o.asset_b ].insert( o.id );
} FC_CAPTURE_AND_RETHROW( (objct) ) }

void asset_in_liquidity_pools_index::object_removed( const object& objct )
{ try {
   const auto& o = static_cast<const liquidity_pool_object&>( objct );
   asset_in_pools_map[ o.asset_a ].erase( o.id );
   asset_in_pools_map[ o.asset_b ].erase( o.id );
   // Note: do not erase entries with an empty set from the map in order to avoid read/write race conditions
} FC_CAPTURE_AND_RETHROW( (objct) ) }

void asset_in_liquidity_pools_index::about_to_modify( const object& objct )
{
   // this secondary index has no interest in the modifications, nothing to do here
}

void asset_in_liquidity_pools_index::object_modified( const object& objct )
{
   // this secondary index has no interest in the modifications, nothing to do here
}

const flat_set<liquidity_pool_id_type>& asset_in_liquidity_pools_index::get_liquidity_pools_by_asset(
            const asset_id_type& a )const
{
   auto itr = asset_in_pools_map.find( a );
   if( itr != asset_in_pools_map.end() )
      return itr->second;
   return empty_set;
}

namespace detail
{

class api_helper_indexes_impl
{
   public:
      api_helper_indexes_impl(api_helper_indexes& _plugin)
         : _self( _plugin )
      {  }

      graphene::chain::database& database()
      {
         return _self.database();
      }

      api_helper_indexes& _self;

   private:

};

} // end namespace detail

api_helper_indexes::api_helper_indexes() :
   my( new detail::api_helper_indexes_impl(*this) )
{
}

api_helper_indexes::~api_helper_indexes()
{
}

std::string api_helper_indexes::plugin_name()const
{
   return "api_helper_indexes";
}
std::string api_helper_indexes::plugin_description()const
{
   return "Provides some helper indexes used by various API calls";
}

void api_helper_indexes::plugin_set_program_options(
   boost::program_options::options_description& cli,
   boost::program_options::options_description& cfg
   )
{
}

void api_helper_indexes::plugin_initialize(const boost::program_options::variables_map& options)
{
}

void api_helper_indexes::plugin_startup()
{
   ilog("api_helper_indexes: plugin_startup() begin");
   amount_in_collateral_idx = database().add_secondary_index< primary_index<call_order_index>,
                                                              amount_in_collateral_index >();
   for( const auto& call : database().get_index_type<call_order_index>().indices() )
      amount_in_collateral_idx->object_inserted( call );

   auto& account_members = *database().add_secondary_index< primary_index<account_index>, account_member_index >();
   for( const auto& account : database().get_index_type< account_index >().indices() )
      account_members.object_inserted( account );

   auto& approvals = *database().add_secondary_index< primary_index<proposal_index>, required_approval_index >();
   for( const auto& proposal : database().get_index_type< proposal_index >().indices() )
      approvals.object_inserted( proposal );

   asset_in_liquidity_pools_idx = database().add_secondary_index< primary_index<liquidity_pool_index>,
                                                        asset_in_liquidity_pools_index >();
   for( const auto& pool : database().get_index_type<liquidity_pool_index>().indices() )
      asset_in_liquidity_pools_idx->object_inserted( pool );

}

} }
