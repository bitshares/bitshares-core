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

#include <graphene/custom_operations/custom_operations_plugin.hpp>

#include <fc/crypto/hex.hpp>
#include <iostream>
#include <graphene/app/database_api.hpp>

namespace graphene { namespace custom_operations {

namespace detail
{
class custom_operations_plugin_impl
{
   public:
   custom_operations_plugin_impl(custom_operations_plugin& _plugin)
         : _self( _plugin )
      {  }
      virtual ~custom_operations_plugin_impl();

      void onBlock();

      graphene::chain::database& database()
      {
         return _self.database();
      }

      custom_operations_plugin& _self;

      uint32_t _start_block = 45000000;

   private:

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

void custom_operations_plugin_impl::onBlock()
{
   graphene::chain::database& db = database();
   const vector<optional< operation_history_object > >& hist = db.get_applied_operations();
   for( const optional< operation_history_object >& o_operation : hist )
   {
      if(!o_operation.valid() || !o_operation->op.is_type<custom_operation>())
         continue;

      const custom_operation& custom_op = o_operation->op.get<custom_operation>();

      if(custom_op.data.size() == 0)
         continue;

      try {
         auto unpacked = fc::raw::unpack<custom_plugin_operation>(custom_op.data);
         custom_op_visitor vtor(db, custom_op.fee_payer());
         unpacked.visit(vtor);
      }
      catch (fc::exception& e) { // only api node will know if the unpack, validate or apply fails
         wlog("Custom operations plugin serializing error: ${ex} in operation: ${op}",
               ("ex", e.to_detail_string())("op", fc::json::to_string(custom_op)));
         continue;
      }
   }
}

custom_operations_plugin_impl::~custom_operations_plugin_impl()
{
   return;
}

} // end namespace detail

custom_operations_plugin::custom_operations_plugin() :
   my( new detail::custom_operations_plugin_impl(*this) )
{
}

custom_operations_plugin::~custom_operations_plugin()
{
}

std::string custom_operations_plugin::plugin_name()const
{
   return "custom_operations";
}
std::string custom_operations_plugin::plugin_description()const
{
   return "Stores arbitrary data for accounts by creating specially crafted custom operations.";
}

void custom_operations_plugin::plugin_set_program_options(
   boost::program_options::options_description& cli,
   boost::program_options::options_description& cfg
   )
{
   cli.add_options()
         ("custom-operations-start-block", boost::program_options::value<uint32_t>()->default_value(45000000),
          "Start processing custom operations transactions with the plugin only after this block")
         ;
   cfg.add(cli);

}

void custom_operations_plugin::plugin_initialize(const boost::program_options::variables_map& options)
{
   database().add_index< primary_index< account_storage_index  > >();

   if (options.count("custom-operations-start-block") > 0) {
      my->_start_block = options["custom-operations-start-block"].as<uint32_t>();
   }

   database().applied_block.connect( [this]( const signed_block& b) {
      if( b.block_num() >= my->_start_block )
         my->onBlock();
   } );
}

void custom_operations_plugin::plugin_startup()
{
   ilog("custom_operations: plugin_startup() begin");
}

} }
