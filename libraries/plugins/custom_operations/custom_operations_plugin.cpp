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

      void onBlock( const signed_block& b );

      graphene::chain::database& database()
      {
         return _self.database();
      }

      custom_operations_plugin& _self;

   private:

};

void custom_operations_plugin_impl::onBlock( const signed_block& b )
{
   graphene::chain::database& db = database();
   const vector<optional< operation_history_object > >& hist = db.get_applied_operations();
   for( const optional< operation_history_object >& o_operation : hist )
   {
      if(!o_operation.valid() || !o_operation->op.is_type<custom_operation>())
         continue;

      const custom_operation& custom_op = o_operation->op.get<custom_operation>();

      if(custom_op.data.size() == 0 || uint8_t(custom_op.data.data()[0]) != 0xFF)
         continue;

      try {
         auto unpacked = fc::raw::unpack<custom_operation_wrapper>(custom_op.data);
         custom_op_visitor vtor(db, custom_op.fee_payer());
         unpacked.op.visit(vtor);
      }
      catch (fc::exception e) { // only api node will know if the unpack, validate or apply fails
         wlog("Error: ${ex} in operation: ${op}", ("ex", e.to_detail_string())("op", fc::json::to_string(custom_op)));
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
   return "custom_operations description";
}

void custom_operations_plugin::plugin_set_program_options(
   boost::program_options::options_description& cli,
   boost::program_options::options_description& cfg
   )
{
}

void custom_operations_plugin::plugin_initialize(const boost::program_options::variables_map& options)
{
   database().add_index< primary_index< account_contact_index  > >();
   database().add_index< primary_index< htlc_orderbook_index  > >();

   database().applied_block.connect( [this]( const signed_block& b) {
      my->onBlock(b);
   } );
}

void custom_operations_plugin::plugin_startup()
{
   ilog("custom_operations: plugin_startup() begin");
}

} }
