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

namespace graphene { namespace elasticsearch {
   using namespace chain;
   //using namespace graphene::db;
   //using boost::multi_index_container;
   //using namespace boost::multi_index;

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

static size_t WriteCallback(void *contents, size_t size, size_t nmemb, void *userp)
{
   ((std::string*)userp)->append((char*)contents, size * nmemb);
   return size * nmemb;
}

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

   void operator()( const graphene::chain::transfer_operation& o )
   {
      fee_asset = o.fee.asset_id;
      fee_amount = o.fee.amount;

      transfer_asset_id = o.amount.asset_id;
      transfer_amount = o.amount.amount;
   }
   template<typename T>
   void operator()( const T& o )
   {
      fee_asset = o.fee.asset_id;
      fee_amount = o.fee.amount;
   }
};

} } //graphene::elasticsearch
