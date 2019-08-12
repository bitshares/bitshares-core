/*
 * Copyright (c) 2019 Abit More, and contributors.
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

#include <graphene/plugins/txid/txid_plugin.hpp>

#include <graphene/chain/database.hpp>

namespace graphene { namespace plugins { namespace txid {

namespace detail
{

class txid_plugin_impl
{
   public:
      txid_plugin_impl(txid_plugin& _plugin)
         : _self( _plugin )
      { }
      virtual ~txid_plugin_impl();


      /** This method is called as a callback after a block is applied
       * and will process/index all transactions that were included in the block.
       */
      void on_applied_block( const signed_block& b );

      graphene::chain::database& database()
      {
         return _self.database();
      }

      txid_plugin& _self;
};

txid_plugin_impl::~txid_plugin_impl()
{
}

void txid_plugin_impl::on_applied_block( const signed_block& b )
{
   graphene::chain::database& db = database();

   uint32_t block_num = b.block_num();
   uint16_t trx_in_block = 0;
   for( const auto& tx : b.transactions )
   {
      db.create<transaction_position_object>( [&tx,block_num,&trx_in_block]( transaction_position_object& obj ){
         obj.trx_id = tx.id();
         obj.block_num = block_num;
         obj.trx_in_block = trx_in_block;
      });
      ++trx_in_block;
   }
}

} // end namespace detail


txid_plugin::txid_plugin() :
   my( new detail::txid_plugin_impl(*this) )
{
}

txid_plugin::~txid_plugin()
{
}

std::string txid_plugin::plugin_name()const
{
   return "txid";
}

std::string txid_plugin::plugin_description()const
{
   return "Provides data to search for transactions by hash (txid)";
}

void txid_plugin::plugin_set_program_options(
   boost::program_options::options_description& cli,
   boost::program_options::options_description& cfg
   )
{
}

void txid_plugin::plugin_initialize(const boost::program_options::variables_map& options)
{
   database().applied_block.connect( [&]( const signed_block& b){ my->on_applied_block(b); } );
   database().add_index< primary_index< transaction_position_index > >();
}

void txid_plugin::plugin_startup()
{
}

} } }
