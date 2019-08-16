/*
 * Copyright (c) 2019 oxarbitrage and contributors.
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

#include <graphene/custom_operations/custom_objects.hpp>
#include <graphene/custom_operations/custom_operations.hpp>
#include <graphene/custom_operations/custom_evaluators.hpp>

namespace graphene { namespace custom_operations {
using namespace chain;

namespace detail
{
    class custom_operations_plugin_impl;
}

class custom_operations_plugin : public graphene::app::plugin
{
   public:
      custom_operations_plugin();
      virtual ~custom_operations_plugin();

      std::string plugin_name()const override;
      std::string plugin_description()const override;
      virtual void plugin_set_program_options(
         boost::program_options::options_description& cli,
         boost::program_options::options_description& cfg) override;
      virtual void plugin_initialize(const boost::program_options::variables_map& options) override;
      virtual void plugin_startup() override;

      friend class detail::custom_operations_plugin_impl;
      std::unique_ptr<detail::custom_operations_plugin_impl> my;
};


typedef fc::static_variant<
      account_contact_operation,
      create_htlc_order_operation,
      take_htlc_order_operation
> custom_plugin_operation;

struct custom_operation_wrapper {
   uint8_t unused_data; // if first char of custom_op.data is 0xFF we unpack, this char is not used anymore then.
   custom_plugin_operation op;
};

struct custom_op_visitor
{
   typedef void result_type;
   account_id_type _fee_payer;
   database* _db;

   custom_op_visitor(database& db, account_id_type fee_payer) { _db = &db; _fee_payer = fee_payer; };

   template<typename T>
   void operator()(T &v) const {
      v.validate();
      custom_generic_evaluator evaluator(*_db, _fee_payer);
      evaluator.do_apply(v);
   }
};


} } //graphene::custom_operations

FC_REFLECT_TYPENAME( graphene::custom_operations::custom_plugin_operation )
FC_REFLECT( graphene::custom_operations::custom_operation_wrapper, (unused_data)(op) )
