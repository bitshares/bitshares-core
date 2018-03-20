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

#include <graphene/grouped_orders/grouped_orders_plugin.hpp>

#include <graphene/chain/market_object.hpp>

namespace graphene { namespace grouped_orders {

namespace detail
{

class grouped_orders_plugin_impl
{
   public:
      grouped_orders_plugin_impl(grouped_orders_plugin& _plugin)
      :_self( _plugin ) {}
      virtual ~grouped_orders_plugin_impl();

      graphene::chain::database& database()
      {
         return _self.database();
      }

      grouped_orders_plugin&     _self;
      flat_set<uint16_t>         _tracked_groups;
};

/**
 *  @brief This secondary index is used to track changes on limit order objects.
 */
class limit_order_group_index : public secondary_index
{
   public:
      limit_order_group_index( const flat_set<uint16_t>& groups ) : _tracked_groups( groups ) {};

      virtual void object_inserted( const object& obj ) override;
      virtual void object_removed( const object& obj ) override;
      virtual void about_to_modify( const object& before ) override;
      virtual void object_modified( const object& after  ) override;

      const flat_set<uint16_t>& get_tracked_groups() const
      { return _tracked_groups; }

      const map< limit_order_group_key, limit_order_group_data >& get_order_groups() const
      { return _og_data; }

   private:
      void remove_order( const limit_order_object& obj, bool remove_empty = true );

      /** tracked groups */
      flat_set<uint16_t> _tracked_groups;

      /** maps the group key to group data */
      map< limit_order_group_key, limit_order_group_data > _og_data;
};

void limit_order_group_index::object_inserted( const object& objct )
{ try {
   const limit_order_object& o = static_cast<const limit_order_object&>( objct );

   auto& idx = _og_data;

   for( uint16_t group : get_tracked_groups() )
   {
      auto create_ogo = [&]() {
         idx[ limit_order_group_key( group, o.sell_price ) ] = limit_order_group_data( o.sell_price, o.for_sale );
      };
      // if idx is empty, insert this order
      // Note: not capped
      if( idx.empty() )
      {
         create_ogo();
         continue;
      }

      // cap the price
      price capped_price = o.sell_price;
      price max = o.sell_price.max();
      price min = o.sell_price.min();
      bool capped_max = false;
      bool capped_min = false;
      if( o.sell_price > max )
      {
         capped_price = max;
         capped_max = true;
      }
      else if( o.sell_price < min )
      {
         capped_price = min;
         capped_min = true;
      }
      // if idx is not empty, find the group that is next to this order
      auto itr = idx.lower_bound( limit_order_group_key( group, capped_price ) );
      bool check_previous = false;
      if( itr == idx.end() || itr->first.group != group
            || itr->first.min_price.base.asset_id != o.sell_price.base.asset_id
            || itr->first.min_price.quote.asset_id != o.sell_price.quote.asset_id )
         // not same market or group type
         check_previous = true;
      else // same market and group type
      {
         bool update_max = false;
         if( capped_price > itr->second.max_price ) // implies itr->min_price <= itr->max_price < max
         {
            update_max = true;
            price max_price = itr->first.min_price * ratio_type( GRAPHENE_100_PERCENT + group, GRAPHENE_100_PERCENT );
            // max_price should have been capped here
            if( capped_price > max_price ) // new order is out of range
               check_previous = true;
         }
         if( !check_previous ) // new order is within the range
         {
            if( capped_min && o.sell_price < itr->first.min_price )
            {  // need to update itr->min_price here, if itr is below min, and new order is even lower
               // TODO improve performance
               limit_order_group_data data( itr->second.max_price, o.for_sale + itr->second.total_for_sale );
               idx.erase( itr );
               idx[ limit_order_group_key( group, o.sell_price ) ] = data;
            }
            else
            {
               if( update_max || ( capped_max && o.sell_price > itr->second.max_price ) )
                  itr->second.max_price = o.sell_price; // store real price here, not capped
               itr->second.total_for_sale += o.for_sale;
            }
         }
      }

      if( check_previous )
      {
         if( itr == idx.begin() ) // no previous
            create_ogo();
         else
         {
            --itr; // should be valid
            if( itr->first.group != group || itr->first.min_price.base.asset_id != o.sell_price.base.asset_id
                                          || itr->first.min_price.quote.asset_id != o.sell_price.quote.asset_id )
               // not same market or group type
               create_ogo();
            else // same market and group type
            {
               // due to lower_bound, always true: capped_price < itr->first.min_price, so no need to check again,
               // if new order is in range of itr group, always need to update itr->first.min_price, unless
               //   o.sell_price is higher than max
               price min_price = itr->second.max_price / ratio_type( GRAPHENE_100_PERCENT + group, GRAPHENE_100_PERCENT );
               // min_price should have been capped here
               if( capped_price < min_price ) // new order is out of range
                  create_ogo();
               else if( capped_max && o.sell_price >= itr->first.min_price )
               {  // itr is above max, and price of new order is even higher
                  if( o.sell_price > itr->second.max_price )
                     itr->second.max_price = o.sell_price;
                  itr->second.total_for_sale += o.for_sale;
               }
               else
               {  // new order is within the range
                  // TODO improve performance
                  limit_order_group_data data( itr->second.max_price, o.for_sale + itr->second.total_for_sale );
                  idx.erase( itr );
                  idx[ limit_order_group_key( group, o.sell_price ) ] = data;
               }
            }
         }
      }
   }
} FC_CAPTURE_AND_RETHROW( (objct) ); }

void limit_order_group_index::object_removed( const object& objct )
{ try {
   const limit_order_object& o = static_cast<const limit_order_object&>( objct );
   remove_order( o );
} FC_CAPTURE_AND_RETHROW( (objct) ); }

void limit_order_group_index::about_to_modify( const object& objct )
{ try {
   const limit_order_object& o = static_cast<const limit_order_object&>( objct );
   remove_order( o, false );
} FC_CAPTURE_AND_RETHROW( (objct) ); }

void limit_order_group_index::object_modified( const object& objct )
{ try {
   object_inserted( objct );
} FC_CAPTURE_AND_RETHROW( (objct) ); }

void limit_order_group_index::remove_order( const limit_order_object& o, bool remove_empty )
{
   auto& idx = _og_data;

   for( uint16_t group : get_tracked_groups() )
   {
      // find the group that should contain this order
      auto itr = idx.lower_bound( limit_order_group_key( group, o.sell_price ) );
      if( itr == idx.end() || itr->first.group != group
            || itr->first.min_price.base.asset_id != o.sell_price.base.asset_id
            || itr->first.min_price.quote.asset_id != o.sell_price.quote.asset_id
            || itr->second.max_price < o.sell_price )
      {
         // can not find corresponding group, should not happen
         wlog( "can not find the order group containing order for removing (price dismatch): ${o}", ("o",o) );
         continue;
      }
      else // found
      {
         if( itr->second.total_for_sale < o.for_sale )
            // should not happen
            wlog( "can not find the order group containing order for removing (amount dismatch): ${o}", ("o",o) );
         else if( !remove_empty || itr->second.total_for_sale > o.for_sale )
            itr->second.total_for_sale -= o.for_sale;
         else
            // it's the only order in the group and need to be removed
            idx.erase( itr );
      }
   }
}

grouped_orders_plugin_impl::~grouped_orders_plugin_impl()
{}

} // end namespace detail


grouped_orders_plugin::grouped_orders_plugin() :
   my( new detail::grouped_orders_plugin_impl(*this) )
{
}

grouped_orders_plugin::~grouped_orders_plugin()
{
}

std::string grouped_orders_plugin::plugin_name()const
{
   return "grouped_orders";
}

void grouped_orders_plugin::plugin_set_program_options(
   boost::program_options::options_description& cli,
   boost::program_options::options_description& cfg
   )
{
   cli.add_options()
         ("tracked-groups", boost::program_options::value<string>()->default_value("[10,100]"), // 0.1% and 1%
          "Group orders by percentage increase on price. Specify a JSON array of numbers here, each number is a group, number 1 means 0.01%. ")
         ;
   cfg.add(cli);
}

void grouped_orders_plugin::plugin_initialize(const boost::program_options::variables_map& options)
{ try {

   if( options.count( "tracked-groups" ) )
   {
      const std::string& groups = options["tracked-groups"].as<string>();
      my->_tracked_groups = fc::json::from_string(groups).as<flat_set<uint16_t>>( 2 );
      my->_tracked_groups.erase( 0 );
   }
   else
      my->_tracked_groups = fc::json::from_string("[10,100]").as<flat_set<uint16_t>>(2);

   database().add_secondary_index< primary_index<limit_order_index>, detail::limit_order_group_index >( my->_tracked_groups );

} FC_CAPTURE_AND_RETHROW() }

void grouped_orders_plugin::plugin_startup()
{
}

const flat_set<uint16_t>& grouped_orders_plugin::tracked_groups() const
{
   return my->_tracked_groups;
}

const map< limit_order_group_key, limit_order_group_data >& grouped_orders_plugin::limit_order_groups()
{
   const auto& idx = database().get_index_type< limit_order_index >();
   const auto& pidx = dynamic_cast<const primary_index< limit_order_index >&>(idx);
   const auto& logidx = pidx.get_secondary_index< detail::limit_order_group_index >();
   return logidx.get_order_groups();
}

} }
