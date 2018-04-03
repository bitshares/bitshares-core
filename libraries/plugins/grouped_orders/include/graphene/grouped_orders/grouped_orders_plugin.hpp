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
#pragma once

#include <graphene/app/plugin.hpp>
#include <graphene/chain/database.hpp>

namespace graphene { namespace grouped_orders {
using namespace chain;

struct limit_order_group_key
{
   limit_order_group_key( const uint16_t g, const price& p ) : group(g), min_price(p) {}
   limit_order_group_key() {}

   uint16_t      group = 0; ///< percentage, 1 means 1 / 10000
   price         min_price;

   friend bool operator < ( const limit_order_group_key& a, const limit_order_group_key& b )
   {
      // price is ordered descendingly, same as limit_order_index
      return std::tie( a.group, b.min_price ) < std::tie( b.group, a.min_price );
   }
   friend bool operator == ( const limit_order_group_key& a, const limit_order_group_key& b )
   {
      return std::tie( a.group, a.min_price ) == std::tie( b.group, b.min_price );
   }
};

struct limit_order_group_data
{
   limit_order_group_data( const price& p, const share_type s ) : max_price(p), total_for_sale(s) {}
   limit_order_group_data() {}

   price         max_price;
   share_type    total_for_sale; ///< asset id is min_price.base.asset_id
};

namespace detail
{
    class grouped_orders_plugin_impl;
}

/**
 *  The grouped orders plugin can be configured to track any number of price diff percentages via its configuration.
 *  Every time when there is a change on an order in object database, it will update internal state to reflect the change.
 */
class grouped_orders_plugin : public graphene::app::plugin
{
   public:
      grouped_orders_plugin();
      virtual ~grouped_orders_plugin();

      std::string plugin_name()const override;
      virtual void plugin_set_program_options(
         boost::program_options::options_description& cli,
         boost::program_options::options_description& cfg) override;
      virtual void plugin_initialize(
         const boost::program_options::variables_map& options) override;
      virtual void plugin_startup() override;

      const flat_set<uint16_t>&   tracked_groups()const;

      const map< limit_order_group_key, limit_order_group_data >& limit_order_groups();

   private:
      friend class detail::grouped_orders_plugin_impl;
      std::unique_ptr<detail::grouped_orders_plugin_impl> my;
};

} } //graphene::grouped_orders

FC_REFLECT( graphene::grouped_orders::limit_order_group_key, (group)(min_price) )
FC_REFLECT( graphene::grouped_orders::limit_order_group_data, (max_price)(total_for_sale) )
