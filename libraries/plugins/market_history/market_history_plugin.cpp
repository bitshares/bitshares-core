/*
 * Copyright (c) 2015, Cryptonomex, Inc.
 * All rights reserved.
 *
 * This source code is provided for evaluation in private test networks only, until September 8, 2015. After this date, this license expires and
 * the code may not be used, modified or distributed for any purpose. Redistribution and use in source and binary forms, with or without modification,
 * are permitted until September 8, 2015, provided that the following conditions are met:
 *
 * 1. The code and/or derivative works are used only for private test networks consisting of no more than 10 P2P nodes.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <graphene/market_history/market_history_plugin.hpp>

#include <graphene/chain/account_evaluator.hpp>
#include <graphene/chain/account_object.hpp>
#include <graphene/chain/config.hpp>
#include <graphene/chain/database.hpp>
#include <graphene/chain/evaluator.hpp>
#include <graphene/chain/key_object.hpp>
#include <graphene/chain/operation_history_object.hpp>
#include <graphene/chain/transaction_evaluation_state.hpp>

#include <fc/thread/thread.hpp>

namespace graphene { namespace market_history {

namespace detail
{

class market_history_plugin_impl
{
   public:
      market_history_plugin_impl(market_history_plugin& _plugin)
      :_self( _plugin ) {}
      virtual ~market_history_plugin_impl();

      /** this method is called as a callback after a block is applied
       * and will process/index all operations that were applied in the block.
       */
      void update_market_histories( const signed_block& b );

      graphene::chain::database& database()
      {
         return _self.database();
      }

      market_history_plugin&     _self;
      flat_set<uint32_t>         _tracked_buckets;
};


struct operation_process_fill_order
{
   market_history_plugin&    _plugin;
   fc::time_point_sec        _now;

   operation_process_fill_order( market_history_plugin& mhp, fc::time_point_sec n )
   :_plugin(mhp),_now(n) {}

   typedef void result_type;

   /** do nothing for other operation types */
   template<typename T>
   void operator()( const T& )const{}

   void operator()( const fill_order_operation& o )const 
   {
      const auto& buckets = _plugin.tracked_buckets();
      auto& db         = _plugin.database();
      const auto& bucket_idx = db.get_index_type<bucket_index>();

      for( auto bucket : buckets )
      {
          bucket_key key;
          key.base    = o.pays.asset_id;
          key.quote   = o.receives.asset_id;
          price trade_price = o.pays / o.receives;
          if( key.base > key.quote ) 
          {
             std::swap( key.base,key.quote );
             trade_price = ~trade_price;
          }

          key.seconds = bucket;
          key.open    = fc::time_point() + fc::seconds((_now.sec_since_epoch() / key.seconds) * key.seconds);

          const auto& by_key_idx = bucket_idx.indices().get<by_key>();
          auto itr = by_key_idx.find( key );
          if( itr == by_key_idx.end() )
          { // create new bucket
            db.create<bucket_object>( [&]( bucket_object& b ){
                 b.key = key;
                 b.quote_volume += trade_price.quote.amount;
                 b.open_base = trade_price.base.amount;
                 b.open_quote = trade_price.quote.amount;
                 b.close_base = trade_price.base.amount;
                 b.close_quote = trade_price.quote.amount;
                 b.high_base = b.close_base;
                 b.high_quote = b.close_quote;
                 b.low_base = b.close_base;
                 b.low_quote = b.close_quote;
            });
          }
          else
          { // update existing bucket
             db.modify( *itr, [&]( bucket_object& b ){
                  b.base_volume += trade_price.base.amount;
                  b.quote_volume += trade_price.quote.amount;
                  b.close_base = trade_price.base.amount;
                  b.close_quote = trade_price.quote.amount;
                  if( b.high() < trade_price ) 
                  {
                      b.high_base = b.close_base;
                      b.high_quote = b.close_quote;
                  }
                  if( b.low() > trade_price ) 
                  {
                      b.low_base = b.close_base;
                      b.low_quote = b.close_quote;
                  }
             });
          }
      }
   }
};

market_history_plugin_impl::~market_history_plugin_impl()
{}

void market_history_plugin_impl::update_market_histories( const signed_block& b )
{
   graphene::chain::database& db = database();
   const vector<operation_history_object>& hist = db.get_applied_operations();
   for( auto op : hist )
      op.op.visit( operation_process_fill_order( _self, b.timestamp ) );
}

} // end namespace detail






market_history_plugin::market_history_plugin() :
   my( new detail::market_history_plugin_impl(*this) )
{
}

market_history_plugin::~market_history_plugin()
{
}

std::string market_history_plugin::plugin_name()const
{
   return "market_history";
}

void market_history_plugin::plugin_set_program_options(
   boost::program_options::options_description& cli,
   boost::program_options::options_description& cfg
   )
{
   cli.add_options()
         ("bucket-size", boost::program_options::value<std::vector<uint32_t>>()->composing()->multitoken(), "Bucket size in seconds to track history for (may specify multiple times)")
         ;
   cfg.add(cli);
}

void market_history_plugin::plugin_initialize(const boost::program_options::variables_map& options)
{
   database().applied_block.connect( [&]( const signed_block& b){ my->update_market_histories(b); } );
   database().add_index< primary_index< bucket_index  > >();

   LOAD_VALUE_SET(options, "bucket-size", my->_tracked_buckets, uint32_t);
}

void market_history_plugin::plugin_startup()
{
}

const flat_set<uint32_t>& market_history_plugin::tracked_buckets() const
{
   return my->_tracked_buckets;
}

} }
