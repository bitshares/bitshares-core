/*
 * Copyright (c) 2015 Cryptonomex, Inc., and contributors.
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

#include <graphene/market_history/market_history_plugin.hpp>

#include <graphene/chain/account_evaluator.hpp>
#include <graphene/chain/account_object.hpp>
#include <graphene/chain/config.hpp>
#include <graphene/chain/database.hpp>
#include <graphene/chain/evaluator.hpp>
#include <graphene/chain/operation_history_object.hpp>
#include <graphene/chain/transaction_evaluation_state.hpp>
#include <graphene/chain/protocol/fee_schedule.hpp>

#include <fc/thread/thread.hpp>
#include <fc/smart_ref_impl.hpp>

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
      uint32_t                   _maximum_history_per_bucket_size = 1000;
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
      //ilog( "processing ${o}", ("o",o) );
      const auto& buckets = _plugin.tracked_buckets();
      auto& db         = _plugin.database();
      const auto& bucket_idx = db.get_index_type<bucket_index>();
      const auto& history_idx = db.get_index_type<history_index>().indices().get<by_key>();

      auto time = db.head_block_time();

      history_key hkey;
      hkey.base = o.pays.asset_id;
      hkey.quote = o.receives.asset_id;
      if( hkey.base > hkey.quote ) 
         std::swap( hkey.base, hkey.quote );
      hkey.sequence = std::numeric_limits<int64_t>::min();

      auto itr = history_idx.lower_bound( hkey );

      if( itr->key.base == hkey.base && itr->key.quote == hkey.quote )
         hkey.sequence = itr->key.sequence - 1;
      else
         hkey.sequence = 0;

      db.create<order_history_object>( [&]( order_history_object& ho ) {
         ho.key = hkey;
         ho.time = time;
         ho.op = o;
      });

      hkey.sequence += 200;
      itr = history_idx.lower_bound( hkey );

      while( itr != history_idx.end() )
      {
         if( itr->key.base == hkey.base && itr->key.quote == hkey.quote )
         {
            db.remove( *itr );
            itr = history_idx.lower_bound( hkey );
         }
         else break;
      }


      auto max_history = _plugin.max_history();
      for( auto bucket : buckets )
      {
          auto cutoff      = (fc::time_point() + fc::seconds( bucket * max_history));

          bucket_key key;
          key.base    = o.pays.asset_id;
          key.quote   = o.receives.asset_id;


          /** for every matched order there are two fill order operations created, one for
           * each side.  We can filter the duplicates by only considering the fill operations where
           * the base > quote
           */
          if( key.base > key.quote ) 
          {
             //ilog( "     skipping because base > quote" );
             continue;
          }

          price trade_price = o.pays / o.receives;

          key.seconds = bucket;
          key.open    = fc::time_point() + fc::seconds((_now.sec_since_epoch() / key.seconds) * key.seconds);

          const auto& by_key_idx = bucket_idx.indices().get<by_key>();
          auto itr = by_key_idx.find( key );
          if( itr == by_key_idx.end() )
          { // create new bucket
            /* const auto& obj = */
            db.create<bucket_object>( [&]( bucket_object& b ){
                 b.key = key;
                 b.quote_volume += trade_price.quote.amount;
                 b.base_volume += trade_price.base.amount;
                 b.open_base = trade_price.base.amount;
                 b.open_quote = trade_price.quote.amount;
                 b.close_base = trade_price.base.amount;
                 b.close_quote = trade_price.quote.amount;
                 b.high_base = b.close_base;
                 b.high_quote = b.close_quote;
                 b.low_base = b.close_base;
                 b.low_quote = b.close_quote;
            });
            //wlog( "    creating bucket ${b}", ("b",obj) );
          }
          else
          { // update existing bucket
             //wlog( "    before updating bucket ${b}", ("b",*itr) );
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
             //wlog( "    after bucket bucket ${b}", ("b",*itr) );
          }

          if( max_history != 0  )
          {
             key.open = fc::time_point_sec();
             auto itr = by_key_idx.lower_bound( key );

             while( itr != by_key_idx.end() && 
                    itr->key.base == key.base && 
                    itr->key.quote == key.quote && 
                    itr->key.seconds == bucket && 
                    itr->key.open < cutoff )
             {
              //  elog( "    removing old bucket ${b}", ("b", *itr) );
                auto old_itr = itr;
                ++itr;
                db.remove( *old_itr );
             }
          }
      }
   }
};

market_history_plugin_impl::~market_history_plugin_impl()
{}

void market_history_plugin_impl::update_market_histories( const signed_block& b )
{
   if( _maximum_history_per_bucket_size == 0 ) return;
   if( _tracked_buckets.size() == 0 ) return;

   graphene::chain::database& db = database();
   const vector<optional< operation_history_object > >& hist = db.get_applied_operations();
   for( const optional< operation_history_object >& o_op : hist )
   {
      if( o_op.valid() )
         o_op->op.visit( operation_process_fill_order( _self, b.timestamp ) );
   }
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
         ("bucket-size", boost::program_options::value<string>()->default_value("[15,60,300,3600,86400]"),
           "Track market history by grouping orders into buckets of equal size measured in seconds specified as a JSON array of numbers")
         ("history-per-size", boost::program_options::value<uint32_t>()->default_value(1000), 
           "How far back in time to track history for each bucket size, measured in the number of buckets (default: 1000)")
         ;
   cfg.add(cli);
}

void market_history_plugin::plugin_initialize(const boost::program_options::variables_map& options)
{ try {
   database().applied_block.connect( [&]( const signed_block& b){ my->update_market_histories(b); } );
   database().add_index< primary_index< bucket_index  > >();
   database().add_index< primary_index< history_index  > >();

   if( options.count( "bucket-size" ) )
   {
      const std::string& buckets = options["bucket-size"].as<string>(); 
      my->_tracked_buckets = fc::json::from_string(buckets).as<flat_set<uint32_t>>();
   }
   if( options.count( "history-per-size" ) )
      my->_maximum_history_per_bucket_size = options["history-per-size"].as<uint32_t>();
} FC_CAPTURE_AND_RETHROW() }

void market_history_plugin::plugin_startup()
{
}

const flat_set<uint32_t>& market_history_plugin::tracked_buckets() const
{
   return my->_tracked_buckets;
}

uint32_t market_history_plugin::max_history()const
{
   return my->_maximum_history_per_bucket_size;
}

} }
