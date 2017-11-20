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
      uint32_t                   _max_order_his_records_per_market = 1000;
      uint32_t                   _max_order_his_seconds_per_market = 259200;
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
      auto& db         = _plugin.database();
      const auto& bucket_idx = db.get_index_type<bucket_index>();
      const auto& history_idx = db.get_index_type<history_index>().indices().get<by_key>();
      const auto& his_time_idx = db.get_index_type<history_index>().indices().get<by_market_time>();

      // To save new filled order data
      history_key hkey;
      hkey.base = o.pays.asset_id;
      hkey.quote = o.receives.asset_id;
      if( hkey.base > hkey.quote ) 
         std::swap( hkey.base, hkey.quote );
      hkey.sequence = std::numeric_limits<int64_t>::min();

      auto itr = history_idx.lower_bound( hkey );

      if( itr != history_idx.end() && itr->key.base == hkey.base && itr->key.quote == hkey.quote )
         hkey.sequence = itr->key.sequence - 1;
      else
         hkey.sequence = 0;

      db.create<order_history_object>( [&]( order_history_object& ho ) {
         ho.key = hkey;
         ho.time = _now;
         ho.op = o;
      });

      // To remove old filled order data
      const auto max_records = _plugin.max_order_his_records_per_market();
      hkey.sequence += max_records;
      itr = history_idx.lower_bound( hkey );
      if( itr != history_idx.end() && itr->key.base == hkey.base && itr->key.quote == hkey.quote )
      {
         const auto max_seconds = _plugin.max_order_his_seconds_per_market();
         fc::time_point_sec min_time;
         if( min_time + max_seconds < _now )
            min_time = _now - max_seconds;
         auto time_itr = his_time_idx.lower_bound( std::make_tuple( hkey.base, hkey.quote, min_time ) );
         if( time_itr != his_time_idx.end() && time_itr->key.base == hkey.base && time_itr->key.quote == hkey.quote )
         {
            if( itr->key.sequence >= time_itr->key.sequence )
            {
               while( itr != history_idx.end() && itr->key.base == hkey.base && itr->key.quote == hkey.quote )
               {
                  auto old_itr = itr;
                  ++itr;
                  db.remove( *old_itr );
               }
            }
            else
            {
               while( time_itr != his_time_idx.end() && time_itr->key.base == hkey.base && time_itr->key.quote == hkey.quote )
               {
                  auto old_itr = time_itr;
                  ++time_itr;
                  db.remove( *old_itr );
               }
            }
         }
      }

      // To update buckets data, only update for maker orders
      if( !o.is_maker )
         return;

      const auto max_history = _plugin.max_history();
      if( max_history == 0 ) return;

      const auto& buckets = _plugin.tracked_buckets();
      if( buckets.size() == 0 ) return;

      bucket_key key;
      key.base    = o.pays.asset_id;
      key.quote   = o.receives.asset_id;

      price trade_price = o.pays / o.receives;

      if( key.base > key.quote )
      {
         std::swap( key.base, key.quote );
         trade_price = ~trade_price;
      }

      price fill_price = o.fill_price;
      if( fill_price.base.asset_id > fill_price.quote.asset_id )
         fill_price = ~fill_price;

      for( auto bucket : buckets )
      {
          auto bucket_num = _now.sec_since_epoch() / bucket;
          fc::time_point_sec cutoff;
          if( bucket_num > max_history )
             cutoff = cutoff + ( bucket * ( bucket_num - max_history ) );

          key.seconds = bucket;
          key.open    = fc::time_point_sec() + ( bucket_num * bucket );

          const auto& by_key_idx = bucket_idx.indices().get<by_key>();
          auto bucket_itr = by_key_idx.find( key );
          if( bucket_itr == by_key_idx.end() )
          { // create new bucket
            /* const auto& obj = */
            db.create<bucket_object>( [&]( bucket_object& b ){
                 b.key = key;
                 b.base_volume = trade_price.base.amount;
                 b.quote_volume = trade_price.quote.amount;
                 b.open_base = fill_price.base.amount;
                 b.open_quote = fill_price.quote.amount;
                 b.close_base = fill_price.base.amount;
                 b.close_quote = fill_price.quote.amount;
                 b.high_base = b.close_base;
                 b.high_quote = b.close_quote;
                 b.low_base = b.close_base;
                 b.low_quote = b.close_quote;
            });
            //wlog( "    creating bucket ${b}", ("b",obj) );
          }
          else
          { // update existing bucket
             //wlog( "    before updating bucket ${b}", ("b",*bucket_itr) );
             db.modify( *bucket_itr, [&]( bucket_object& b ){
                  try {
                     b.base_volume += trade_price.base.amount;
                  } catch( fc::overflow_exception ) {
                     b.base_volume = std::numeric_limits<int64_t>::max();
                  }
                  try {
                     b.quote_volume += trade_price.quote.amount;
                  } catch( fc::overflow_exception ) {
                     b.quote_volume = std::numeric_limits<int64_t>::max();
                  }
                  b.close_base = fill_price.base.amount;
                  b.close_quote = fill_price.quote.amount;
                  if( b.high() < fill_price )
                  {
                      b.high_base = b.close_base;
                      b.high_quote = b.close_quote;
                  }
                  if( b.low() > fill_price )
                  {
                      b.low_base = b.close_base;
                      b.low_quote = b.close_quote;
                  }
             });
             //wlog( "    after bucket bucket ${b}", ("b",*bucket_itr) );
          }

          {
             key.open = fc::time_point_sec();
             bucket_itr = by_key_idx.lower_bound( key );

             while( bucket_itr != by_key_idx.end() &&
                    bucket_itr->key.base == key.base &&
                    bucket_itr->key.quote == key.quote &&
                    bucket_itr->key.seconds == bucket &&
                    bucket_itr->key.open < cutoff )
             {
              //  elog( "    removing old bucket ${b}", ("b", *bucket_itr) );
                auto old_bucket_itr = bucket_itr;
                ++bucket_itr;
                db.remove( *old_bucket_itr );
             }
          }
      }
   }
};

market_history_plugin_impl::~market_history_plugin_impl()
{}

void market_history_plugin_impl::update_market_histories( const signed_block& b )
{
   graphene::chain::database& db = database();
   const vector<optional< operation_history_object > >& hist = db.get_applied_operations();
   for( const optional< operation_history_object >& o_op : hist )
   {
      if( o_op.valid() )
      {
         try
         {
            o_op->op.visit( operation_process_fill_order( _self, b.timestamp ) );
         } FC_CAPTURE_AND_LOG( (o_op) )
      }
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
         ("bucket-size", boost::program_options::value<string>()->default_value("[60,300,900,1800,3600,14400,86400]"),
           "Track market history by grouping orders into buckets of equal size measured in seconds specified as a JSON array of numbers")
         ("history-per-size", boost::program_options::value<uint32_t>()->default_value(1000),
           "How far back in time to track history for each bucket size, measured in the number of buckets (default: 1000)")
         ("max-order-his-records-per-market", boost::program_options::value<uint32_t>()->default_value(1000),
           "Will only store this amount of matched orders for each market in order history for querying, or those meet the other option, which has more data (default: 1000)")
         ("max-order-his-seconds-per-market", boost::program_options::value<uint32_t>()->default_value(259200),
           "Will only store matched orders in last X seconds for each market in order history for querying, or those meet the other option, which has more data (default: 259200 (3 days))")
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
      my->_tracked_buckets.erase( 0 );
   }
   if( options.count( "history-per-size" ) )
      my->_maximum_history_per_bucket_size = options["history-per-size"].as<uint32_t>();
   if( options.count( "max-order-his-records-per-market" ) )
      my->_max_order_his_records_per_market = options["max-order-his-records-per-market"].as<uint32_t>();
   if( options.count( "max-order-his-seconds-per-market" ) )
      my->_max_order_his_seconds_per_market = options["max-order-his-seconds-per-market"].as<uint32_t>();
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

uint32_t market_history_plugin::max_order_his_records_per_market()const
{
   return my->_max_order_his_records_per_market;
}

uint32_t market_history_plugin::max_order_his_seconds_per_market()const
{
   return my->_max_order_his_seconds_per_market;
}

} }
