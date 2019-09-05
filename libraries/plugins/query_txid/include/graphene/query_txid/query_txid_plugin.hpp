/*
 * Copyright (c) 2019 GXChain and zhaoxiangfei、bijianing97 .
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
#include <graphene/query_txid/transaction_entry_object.hpp>
#include <graphene/chain/database.hpp>
namespace graphene
{
namespace query_txid
{
using namespace chain;
namespace detail
{
class query_txid_plugin_impl;
}
class query_txid_plugin : public graphene::app::plugin
{
   public:
      query_txid_plugin();
      virtual ~query_txid_plugin();

      std::string plugin_name() const override;

      virtual void plugin_set_program_options(
      boost::program_options::options_description &cli,
      boost::program_options::options_description &cfg) override;

      virtual void plugin_initialize(const boost::program_options::variables_map &options) override;
      virtual void plugin_startup() override;

      static optional<trx_entry_object> query_trx_by_id(std::string txid);

      friend class detail::query_txid_plugin_impl;

      std::unique_ptr<detail::query_txid_plugin_impl> my;
};
} }// graphene::query_txid 
