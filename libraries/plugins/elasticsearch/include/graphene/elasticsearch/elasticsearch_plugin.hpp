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

class elasticsearch_plugin : public graphene::app::plugin
{
   public:
      elasticsearch_plugin();
      virtual ~elasticsearch_plugin();

      std::string plugin_name()const override;
      std::string plugin_description()const override;
      virtual void plugin_set_program_options(
         boost::program_options::options_description& cli,
         boost::program_options::options_description& cfg) override;
      virtual void plugin_initialize(const boost::program_options::variables_map& options) override;
      virtual void plugin_startup() override;

      friend class detail::elasticsearch_plugin_impl;
      std::unique_ptr<detail::elasticsearch_plugin_impl> my;
};

struct operation_visitor
{
   typedef void result_type;

   share_type fee_amount;
   asset_id_type fee_asset;

   asset_id_type transfer_asset_id;
   share_type transfer_amount;
   account_id_type transfer_from;
   account_id_type transfer_to;

   void operator()( const graphene::chain::transfer_operation& o )
   {
      fee_asset = o.fee.asset_id;
      fee_amount = o.fee.amount;

      transfer_asset_id = o.amount.asset_id;
      transfer_amount = o.amount.amount;
      transfer_from = o.from;
      transfer_to = o.to;
   }

   object_id_type      fill_order_id;
   account_id_type     fill_account_id;
   asset_id_type       fill_pays_asset_id;
   share_type          fill_pays_amount;
   asset_id_type       fill_receives_asset_id;
   share_type          fill_receives_amount;
   double              fill_fill_price;
   bool                fill_is_maker;

   void operator()( const graphene::chain::fill_order_operation& o )
   {
      fee_asset = o.fee.asset_id;
      fee_amount = o.fee.amount;

      fill_order_id = o.order_id;
      fill_account_id = o.account_id;
      fill_pays_asset_id = o.pays.asset_id;
      fill_pays_amount = o.pays.amount;
      fill_receives_asset_id = o.receives.asset_id;
      fill_receives_amount = o.receives.amount;
      fill_fill_price = o.fill_price.to_real();
      fill_is_maker = o.is_maker;
   }

   template<typename T>
   void operator()( const T& o )
   {
      fee_asset = o.fee.asset_id;
      fee_amount = o.fee.amount;
   }
};

struct operation_history_struct {
   int trx_in_block;
   int op_in_trx;
   std::string operation_result;
   int virtual_op;
   std::string op;
   variant op_object;
};

struct block_struct {
   int block_num;
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
   account_transaction_history_object account_history;
   operation_history_struct operation_history;
   int operation_type;
   int operation_id_num;
   block_struct block_data;
   optional<visitor_struct> additional_data;
};

struct adaptor_struct {
   variant adapt(const variant_object& op)
   {
      fc::mutable_variant_object o(op);
      vector<string> keys_to_rename;
      for (auto i = o.begin(); i != o.end(); ++i)
      {
         auto& element = (*i).value();
         if (element.is_object())
         {
            const string& name = (*i).key();
            auto& vo = element.get_object();
            if (vo.contains(name.c_str()))
               keys_to_rename.emplace_back(name);
            element = adapt(vo);
         }
         else if (element.is_array())
            adapt(element.get_array());
      }
      for (const auto& i : keys_to_rename)
      {
         string new_name = i + "_";
         o[new_name] = variant(o[i]);
         o.erase(i);
      }

      if (o.find("memo") != o.end())
      {
         auto& memo = o["memo"];
         if (memo.is_string())
         {
            o["memo_"] = o["memo"];
            o.erase("memo");
         }
         else if (memo.is_object())
         {
            fc::mutable_variant_object tmp(memo.get_object());
            if (tmp.find("nonce") != tmp.end())
            {
               tmp["nonce"] = tmp["nonce"].as_string();
               o["memo"] = tmp;
            }
         }
      }
      if (o.find("new_parameters") != o.end())
      {
         auto& tmp = o["new_parameters"];
         if (tmp.is_object())
         {
            fc::mutable_variant_object tmp2(tmp.get_object());
            if (tmp2.find("current_fees") != tmp2.end())
            {
               tmp2.erase("current_fees");
               o["new_parameters"] = tmp2;
            }
         }
      }
      if (o.find("owner") != o.end() && o["owner"].is_string())
      {
         o["owner_"] = o["owner"].as_string();
         o.erase("owner");
      }
      if (o.find("proposed_ops") != o.end())
      {
         o["proposed_ops"] = fc::json::to_string(o["proposed_ops"]);
      }
      if (o.find("initializer") != o.end())
      {
         o["initializer"] = fc::json::to_string(o["initializer"]);
      }
      if (o.find("policy") != o.end())
      {
         o["policy"] = fc::json::to_string(o["policy"]);
      }
      if (o.find("predicates") != o.end())
      {
         o["predicates"] = fc::json::to_string(o["predicates"]);
      }
      if (o.find("active_special_authority") != o.end())
      {
         o["active_special_authority"] = fc::json::to_string(o["active_special_authority"]);
      }
      if (o.find("owner_special_authority") != o.end())
      {
         o["owner_special_authority"] = fc::json::to_string(o["owner_special_authority"]);
      }


      variant v;
      fc::to_variant(o, v, FC_PACK_MAX_DEPTH);
      return v;
   }

   void adapt(fc::variants& v)
   {
      for (auto& array_element : v)
      {
         if (array_element.is_object())
            array_element = adapt(array_element.get_object());
         else if (array_element.is_array())
            adapt(array_element.get_array());
         else
            array_element = array_element.as_string();
      }
   }
};

} } //graphene::elasticsearch

FC_REFLECT( graphene::elasticsearch::operation_history_struct, (trx_in_block)(op_in_trx)(operation_result)(virtual_op)(op)(op_object) )
FC_REFLECT( graphene::elasticsearch::block_struct, (block_num)(block_time)(trx_id) )
FC_REFLECT( graphene::elasticsearch::fee_struct, (asset)(asset_name)(amount)(amount_units) )
FC_REFLECT( graphene::elasticsearch::transfer_struct, (asset)(asset_name)(amount)(amount_units)(from)(to) )
FC_REFLECT( graphene::elasticsearch::fill_struct, (order_id)(account_id)(pays_asset_id)(pays_asset_name)(pays_amount)(pays_amount_units)
                                                  (receives_asset_id)(receives_asset_name)(receives_amount)(receives_amount_units)(fill_price)
                                                  (fill_price_units)(is_maker))
FC_REFLECT( graphene::elasticsearch::visitor_struct, (fee_data)(transfer_data)(fill_data) )
FC_REFLECT( graphene::elasticsearch::bulk_struct, (account_history)(operation_history)(operation_type)(operation_id_num)(block_data)(additional_data) )
