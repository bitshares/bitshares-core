/*
 * Copyright (c) 2017 Cryptonomex, Inc., and contributors.
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
#include <graphene/chain/operation_history_object.hpp>
#include <graphene/utilities/elasticsearch.hpp>

namespace graphene { namespace elasticsearch {
   using namespace chain;

//
// Plugins should #define their SPACE_ID's so plugins with
// conflicting SPACE_ID assignments can be compiled into the
// same binary (by simply re-assigning some of the conflicting #defined
// SPACE_ID's in a build script).
//
// Assignment of SPACE_ID's cannot be done at run-time because
// various template automagic depends on them being known at compile
// time.
//
#ifndef ELASTICSEARCH_SPACE_ID
#define ELASTICSEARCH_SPACE_ID 6
#endif

namespace detail
{
    class elasticsearch_plugin_impl;
}

enum class mode { only_save = 0 , only_query = 1, all = 2 };

class elasticsearch_plugin : public graphene::app::plugin
{
   public:
      explicit elasticsearch_plugin(graphene::app::application& app);
      ~elasticsearch_plugin() override;

      std::string plugin_name()const override;
      std::string plugin_description()const override;
      void plugin_set_program_options(
         boost::program_options::options_description& cli,
         boost::program_options::options_description& cfg) override;
      void plugin_initialize(const boost::program_options::variables_map& options) override;
      void plugin_startup() override;

      operation_history_object get_operation_by_id(const operation_history_id_type& id) const;
      vector<operation_history_object> get_account_history(
            const account_id_type& account_id,
            const operation_history_id_type& stop = operation_history_id_type(),
            uint64_t limit = 100,
            const operation_history_id_type& start = operation_history_id_type() ) const;
      mode get_running_mode() const;

   private:
      std::unique_ptr<detail::elasticsearch_plugin_impl> my;
};


struct operation_history_struct {
   uint16_t trx_in_block;
   uint16_t op_in_trx;
   uint32_t virtual_op;
   bool is_virtual;
   account_id_type fee_payer;
   std::string op;
   std::string operation_result;
   variant op_object;
   variant operation_result_object;
};

struct block_struct {
   uint32_t block_num;
   fc::time_point_sec block_time;
   std::string trx_id;
};

struct fee_struct {
   asset_id_type asset;
   std::string asset_name;
   share_type amount;
   double amount_units;
};

struct transfer_struct {
   asset_id_type asset;
   std::string asset_name;
   share_type amount;
   double amount_units;
   account_id_type from;
   account_id_type to;
};

struct fill_struct {
   object_id_type order_id;
   account_id_type account_id;
   asset_id_type pays_asset_id;
   std::string pays_asset_name;
   share_type pays_amount;
   double pays_amount_units;
   asset_id_type receives_asset_id;
   std::string receives_asset_name;
   share_type receives_amount;
   double receives_amount_units;
   double fill_price;
   double fill_price_units;
   bool is_maker;
};

struct visitor_struct {
   fee_struct fee_data;
   transfer_struct transfer_data;
   fill_struct fill_data;
};

struct bulk_struct {
   account_history_object account_history;
   operation_history_struct operation_history;
   int64_t  operation_type;
   uint64_t operation_id_num;
   block_struct block_data;
   optional<visitor_struct> additional_data;
};

} } //graphene::elasticsearch

FC_REFLECT_ENUM( graphene::elasticsearch::mode, (only_save)(only_query)(all) )
FC_REFLECT( graphene::elasticsearch::operation_history_struct,
            (trx_in_block)(op_in_trx)(virtual_op)(is_virtual)(fee_payer)
            (op)(operation_result)(op_object)(operation_result_object) )
FC_REFLECT( graphene::elasticsearch::block_struct, (block_num)(block_time)(trx_id) )
FC_REFLECT( graphene::elasticsearch::fee_struct, (asset)(asset_name)(amount)(amount_units) )
FC_REFLECT( graphene::elasticsearch::transfer_struct, (asset)(asset_name)(amount)(amount_units)(from)(to) )
FC_REFLECT( graphene::elasticsearch::fill_struct,
            (order_id)(account_id)(pays_asset_id)(pays_asset_name)(pays_amount)(pays_amount_units)
            (receives_asset_id)(receives_asset_name)(receives_amount)(receives_amount_units)(fill_price)
            (fill_price_units)(is_maker) )
FC_REFLECT( graphene::elasticsearch::visitor_struct, (fee_data)(transfer_data)(fill_data) )
FC_REFLECT( graphene::elasticsearch::bulk_struct,
            (account_history)(operation_history)(operation_type)(operation_id_num)(block_data)(additional_data) )
