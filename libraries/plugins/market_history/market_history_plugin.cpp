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
#include <graphene/protocol/fee_schedule.hpp>

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

      /// process all operations related to liquidity pools
      void update_liquidity_pool_histories( time_point_sec time, const operation_history_object& oho,
                                            const liquidity_pool_ticker_meta_object*& lp_meta );

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
   market_history_plugin&            _plugin;
   fc::time_point_sec                _now;
   const market_ticker_meta_object*& _meta;

   operation_process_fill_order( market_history_plugin& mhp, fc::time_point_sec n, const market_ticker_meta_object*& meta )
   :_plugin(mhp),_now(n),_meta(meta) {}

   typedef void result_type;

   /** do nothing for other operation types */
   template<typename T>
   void operator()( const T& )const{}

   void operator()( const fill_order_operation& o )const
   {
      //ilog( "processing ${o}", ("o",o) );
      auto& db         = _plugin.database();
      const auto& order_his_idx = db.get_index_type<history_index>().indices();
      const auto& history_idx = order_his_idx.get<by_key>();
      const auto& his_time_idx = order_his_idx.get<by_market_time>();

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

      const auto& new_order_his_obj = db.create<order_history_object>( [&]( order_history_object& ho ) {
         ho.key = hkey;
         ho.time = _now;
         ho.op = o;
      });

      // save a reference to market ticker meta object
      if( _meta == nullptr )
      {
         const auto& meta_idx = db.get_index_type<simple_index<market_ticker_meta_object>>();
         if( meta_idx.size() == 0 )
            _meta = &db.create<market_ticker_meta_object>( [&]( market_ticker_meta_object& mtm ) {
               mtm.rolling_min_order_his_id = new_order_his_obj.id;
               mtm.skip_min_order_his_id = false;
            });
         else
            _meta = &( *meta_idx.begin() );
      }

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

      // To update ticker data and buckets data, only update for maker orders
      if( !o.is_maker )
         return;

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

      // To update ticker data
      const auto& ticker_idx = db.get_index_type<market_ticker_index>().indices().get<by_market>();
      auto ticker_itr = ticker_idx.find( std::make_tuple( key.base, key.quote ) );
      if( ticker_itr == ticker_idx.end() )
      {
         db.create<market_ticker_object>( [&]( market_ticker_object& mt ) {
            mt.base           = key.base;
            mt.quote          = key.quote;
            mt.last_day_base  = 0;
            mt.last_day_quote = 0;
            mt.latest_base    = fill_price.base.amount;
            mt.latest_quote   = fill_price.quote.amount;
            mt.base_volume    = trade_price.base.amount.value;
            mt.quote_volume   = trade_price.quote.amount.value;
         });
      }
      else
      {
         db.modify( *ticker_itr, [&]( market_ticker_object& mt ) {
            mt.latest_base    = fill_price.base.amount;
            mt.latest_quote   = fill_price.quote.amount;
            mt.base_volume    += trade_price.base.amount.value;  // ignore overflow
            mt.quote_volume   += trade_price.quote.amount.value; // ignore overflow
         });
      }

      // To update buckets data
      const auto max_history = _plugin.max_history();
      if( max_history == 0 ) return;

      const auto& buckets = _plugin.tracked_buckets();
      if( buckets.size() == 0 ) return;

      const auto& bucket_idx = db.get_index_type<bucket_index>();
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
                  } catch( fc::overflow_exception& ) {
                     b.base_volume = std::numeric_limits<int64_t>::max();
                  }
                  try {
                     b.quote_volume += trade_price.quote.amount;
                  } catch( fc::overflow_exception& ) {
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

   const market_ticker_meta_object* _meta = nullptr;
   const auto& meta_idx = db.get_index_type<simple_index<market_ticker_meta_object>>();
   if( meta_idx.size() > 0 )
      _meta = &( *meta_idx.begin() );

   const liquidity_pool_ticker_meta_object* _lp_meta = nullptr;
   const auto& lp_meta_idx = db.get_index_type<simple_index<liquidity_pool_ticker_meta_object>>();
   if( lp_meta_idx.size() > 0 )
      _lp_meta = &( *lp_meta_idx.begin() );

   const vector<optional< operation_history_object > >& hist = db.get_applied_operations();
   for( const optional< operation_history_object >& o_op : hist )
   {
      if( o_op.valid() )
      {
         // process market history
         try
         {
            o_op->op.visit( operation_process_fill_order( _self, b.timestamp, _meta ) );
         } FC_CAPTURE_AND_LOG( (o_op) )
         // process liquidity pool history
         update_liquidity_pool_histories( b.timestamp, *o_op, _lp_meta );
      }
   }
   // roll out expired data from ticker
   if( _meta != nullptr )
   {
      time_point_sec last_day = b.timestamp - 86400;
      object_id_type last_min_his_id = _meta->rolling_min_order_his_id;
      bool skip = _meta->skip_min_order_his_id;

      const auto& ticker_idx = db.get_index_type<market_ticker_index>().indices().get<by_market>();
      const auto& history_idx = db.get_index_type<history_index>().indices().get<by_id>();
      auto history_itr = history_idx.lower_bound( _meta->rolling_min_order_his_id );
      while( history_itr != history_idx.end() && history_itr->time < last_day )
      {
         const fill_order_operation& o = history_itr->op;
         if( skip && history_itr->id == _meta->rolling_min_order_his_id )
            skip = false;
         else if( o.is_maker )
         {
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

            auto ticker_itr = ticker_idx.find( std::make_tuple( key.base, key.quote ) );
            if( ticker_itr != ticker_idx.end() ) // should always be true
            {
               db.modify( *ticker_itr, [&]( market_ticker_object& mt ) {
                  mt.last_day_base  = fill_price.base.amount;
                  mt.last_day_quote = fill_price.quote.amount;
                  mt.base_volume    -= trade_price.base.amount.value;  // ignore underflow
                  mt.quote_volume   -= trade_price.quote.amount.value; // ignore underflow
               });
            }
         }
         last_min_his_id = history_itr->id;
         ++history_itr;
      }
      // update meta
      if( history_itr != history_idx.end() ) // if still has some data rolling
      {
         if( history_itr->id != _meta->rolling_min_order_his_id ) // if rolled out some
         {
            db.modify( *_meta, [&]( market_ticker_meta_object& mtm ) {
               mtm.rolling_min_order_his_id = history_itr->id;
               mtm.skip_min_order_his_id = false;
            });
         }
      }
      else // if all data are rolled out
      {
         if( !_meta->skip_min_order_his_id
             || last_min_his_id != _meta->rolling_min_order_his_id ) // if rolled out some
         {
            db.modify( *_meta, [&]( market_ticker_meta_object& mtm ) {
               mtm.rolling_min_order_his_id = last_min_his_id;
               mtm.skip_min_order_his_id = true;
            });
         }
      }
   }
   // roll out expired data from LP ticker
   if( _lp_meta != nullptr )
   {
      time_point_sec last_day = b.timestamp - 86400;
      object_id_type last_min_his_id = _lp_meta->rolling_min_lp_his_id;
      bool skip = _lp_meta->skip_min_lp_his_id;

      const auto& history_idx = db.get_index_type<liquidity_pool_history_index>().indices().get<by_id>();
      auto history_itr = history_idx.lower_bound( _lp_meta->rolling_min_lp_his_id );
      while( history_itr != history_idx.end() && history_itr->time < last_day )
      {
         if( skip && history_itr->id == _lp_meta->rolling_min_lp_his_id )
            skip = false;
         else
         {
            liquidity_pool_ticker_id_type ticker_id( history_itr->pool.instance );
            const liquidity_pool_ticker_object* ticker = db.find<liquidity_pool_ticker_object>( ticker_id );
            if( ticker != nullptr ) // should always be true
            {
               const operation_history_object& oho = history_itr->op;
               if( oho.op.is_type< liquidity_pool_deposit_operation >() )
               {
                  auto& op = oho.op.get< liquidity_pool_deposit_operation >();
                  auto& result = oho.result.get< generic_exchange_operation_result >();
                  db.modify( *ticker, [&op,&result]( liquidity_pool_ticker_object& t ) {
                     t._24h_deposit_count -= 1;
                     t._24h_deposit_amount_a -= op.amount_a.amount.value;
                     t._24h_deposit_amount_b -= op.amount_b.amount.value;
                     t._24h_deposit_share_amount -= result.received.front().amount.value;
                     t._24h_balance_delta_a -= op.amount_a.amount.value;
                     t._24h_balance_delta_b -= op.amount_b.amount.value;
                  });
               }
               else if( oho.op.is_type< liquidity_pool_withdraw_operation >() )
               {
                  auto& op = oho.op.get< liquidity_pool_withdraw_operation >();
                  auto& result = oho.result.get< generic_exchange_operation_result >();
                  db.modify( *ticker, [&op,&result]( liquidity_pool_ticker_object& t ) {
                     t._24h_withdrawal_count -= 1;
                     t._24h_withdrawal_amount_a -= result.received.front().amount.value;
                     t._24h_withdrawal_amount_b -= result.received.back().amount.value;
                     t._24h_withdrawal_share_amount -= op.share_amount.amount.value;
                     t._24h_withdrawal_fee_a -= result.fees.front().amount.value;
                     t._24h_withdrawal_fee_b -= result.fees.back().amount.value;
                     t._24h_balance_delta_a += result.received.front().amount.value;
                     t._24h_balance_delta_b += result.received.back().amount.value;
                  });
               }
               else if( oho.op.is_type< liquidity_pool_exchange_operation >() )
               {
                  auto& op = oho.op.get< liquidity_pool_exchange_operation >();
                  auto& result = oho.result.get< generic_exchange_operation_result >();
                  db.modify( *ticker, [&op,&result]( liquidity_pool_ticker_object& t ) {
                     auto amount_in = op.amount_to_sell.amount - result.fees.front().amount;
                     auto amount_out = result.received.front().amount + result.fees.at(1).amount;
                     if( op.amount_to_sell.asset_id < op.min_to_receive.asset_id ) // pool got a, paid b
                     {
                        t._24h_exchange_a2b_count -= 1;
                        t._24h_exchange_a2b_amount_a -= amount_in.value;
                        t._24h_exchange_a2b_amount_b -= amount_out.value;
                        t._24h_exchange_fee_b -= result.fees.back().amount.value;
                        t._24h_balance_delta_a -= amount_in.value;
                        t._24h_balance_delta_b += amount_out.value;
                     }
                     else // pool got b, paid a
                     {
                        t._24h_exchange_b2a_count -= 1;
                        t._24h_exchange_b2a_amount_a -= amount_out.value;
                        t._24h_exchange_b2a_amount_b -= amount_in.value;
                        t._24h_exchange_fee_a -= result.fees.back().amount.value;
                        t._24h_balance_delta_a += amount_out.value;
                        t._24h_balance_delta_b -= amount_in.value;
                     }
                  });
               }
            }
         }
         last_min_his_id = history_itr->id;
         ++history_itr;
      }
      // update meta
      if( history_itr != history_idx.end() ) // if still has some data rolling
      {
         if( history_itr->id != _lp_meta->rolling_min_lp_his_id ) // if rolled out some
         {
            db.modify( *_lp_meta, [history_itr]( liquidity_pool_ticker_meta_object& mtm ) {
               mtm.rolling_min_lp_his_id = history_itr->id;
               mtm.skip_min_lp_his_id = false;
            });
         }
      }
      else // if all data are rolled out
      {
         if( !_lp_meta->skip_min_lp_his_id
             || last_min_his_id != _lp_meta->rolling_min_lp_his_id ) // if rolled out some
         {
            db.modify( *_lp_meta, [last_min_his_id]( liquidity_pool_ticker_meta_object& mtm ) {
               mtm.rolling_min_lp_his_id = last_min_his_id;
               mtm.skip_min_lp_his_id = true;
            });
         }
      }
   }
}

struct get_liquidity_pool_id_visitor
{
   typedef optional<liquidity_pool_id_type> result_type;

   /** do nothing for other operation types */
   template<typename T>
   result_type operator()( const T& )const
   {
      return {};
   }

   result_type operator()( const liquidity_pool_delete_operation& o )const
   {
      return o.pool;
   }

   result_type operator()( const liquidity_pool_deposit_operation& o )const
   {
      return o.pool;
   }

   result_type operator()( const liquidity_pool_withdraw_operation& o )const
   {
      return o.pool;
   }

   result_type operator()( const liquidity_pool_exchange_operation& o )const
   {
      return o.pool;
   }

};

void market_history_plugin_impl::update_liquidity_pool_histories(
      time_point_sec time, const operation_history_object& oho,
      const liquidity_pool_ticker_meta_object*& lp_meta )
{ try {

   optional<liquidity_pool_id_type> pool;
   uint64_t sequence = 0;
   if( oho.op.is_type< liquidity_pool_create_operation >() )
   {
      pool = *oho.result.get<generic_operation_result>().new_objects.begin();
      sequence = 1;
   }
   else
   {
      pool = oho.op.visit( get_liquidity_pool_id_visitor() );
   }

   if( pool.valid() )
   {
      auto& db = database();
      const auto& his_index = db.get_index_type<liquidity_pool_history_index>().indices();
      const auto& his_seq_idx = his_index.get<by_pool_seq>();
      const auto& his_time_idx = his_index.get<by_pool_time>();

      if( sequence == 0 )
      {
         auto itr = his_seq_idx.lower_bound( *pool );
         if( itr != his_seq_idx.end() && itr->pool == *pool )
            sequence = itr->sequence + 1;
         else
            sequence = 2;
      }

      // To save new data
      const auto& new_his_obj = db.create<liquidity_pool_history_object>( [&pool,sequence,time,&oho](
                                      liquidity_pool_history_object& ho ) {
         ho.pool = *pool;
         ho.sequence = sequence;
         ho.time = time;
         ho.op_type = oho.op.which();
         ho.op = oho;
      });

      // save a reference to the ticker meta object
      if( lp_meta == nullptr )
      {
         const auto& lp_meta_idx = db.get_index_type<simple_index<liquidity_pool_ticker_meta_object>>();
         if( lp_meta_idx.size() == 0 )
            lp_meta = &db.create<liquidity_pool_ticker_meta_object>( [&new_his_obj](
                                      liquidity_pool_ticker_meta_object& lptm ) {
               lptm.rolling_min_lp_his_id = new_his_obj.id;
               lptm.skip_min_lp_his_id = false;
            });
         else
            lp_meta = &( *lp_meta_idx.begin() );
      }

      // To remove old history data
      if( sequence > _max_order_his_records_per_market )
      {
         const auto min_seq = sequence - _max_order_his_records_per_market;
         auto itr = his_seq_idx.lower_bound( std::make_tuple( *pool, min_seq ) );
         if( itr != his_seq_idx.end() && itr->pool == *pool )
         {
            fc::time_point_sec min_time;
            if( min_time + _max_order_his_seconds_per_market < time )
               min_time = time - _max_order_his_seconds_per_market;
            auto time_itr = his_time_idx.lower_bound( std::make_tuple( *pool, min_time ) );
            if( time_itr != his_time_idx.end() && time_itr->pool == *pool )
            {
               if( itr->sequence <= time_itr->sequence )
               {
                  while( itr != his_seq_idx.end() && itr->pool == *pool )
                  {
                     auto old_itr = itr;
                     ++itr;
                     db.remove( *old_itr );
                  }
               }
               else
               {
                  while( time_itr != his_time_idx.end() && time_itr->pool == *pool )
                  {
                     auto old_itr = time_itr;
                     ++time_itr;
                     db.remove( *old_itr );
                  }
               }
            }
         }
      }

      // To update ticker data
      if( sequence == 1 ) // create
      {
         const liquidity_pool_ticker_object* ticker = nullptr;
         do {
            ticker = &db.create<liquidity_pool_ticker_object>( []( liquidity_pool_ticker_object& lpt ) {
            });
         } while( ticker->id.instance() < pool->instance );
      }
      else
      {
         liquidity_pool_ticker_id_type ticker_id( pool->instance );
         const liquidity_pool_ticker_object* ticker = db.find<liquidity_pool_ticker_object>( ticker_id );
         if( ticker != nullptr )
         {
            if( oho.op.is_type< liquidity_pool_deposit_operation >() )
            {
               auto& op = oho.op.get< liquidity_pool_deposit_operation >();
               auto& result = oho.result.get< generic_exchange_operation_result >();

               db.modify( *ticker, [&op,&result]( liquidity_pool_ticker_object& t ) {
                  t._24h_deposit_count += 1;
                  t._24h_deposit_amount_a += op.amount_a.amount.value;
                  t._24h_deposit_amount_b += op.amount_b.amount.value;
                  t._24h_deposit_share_amount += result.received.front().amount.value;
                  t._24h_balance_delta_a += op.amount_a.amount.value;
                  t._24h_balance_delta_b += op.amount_b.amount.value;
                  t.total_deposit_count += 1;
                  t.total_deposit_amount_a += op.amount_a.amount.value;
                  t.total_deposit_amount_b += op.amount_b.amount.value;
                  t.total_deposit_share_amount += result.received.front().amount.value;
               });

            }
            else if( oho.op.is_type< liquidity_pool_withdraw_operation >() )
            {
               auto& op = oho.op.get< liquidity_pool_withdraw_operation >();
               auto& result = oho.result.get< generic_exchange_operation_result >();

               db.modify( *ticker, [&op,&result]( liquidity_pool_ticker_object& t ) {
                  t._24h_withdrawal_count += 1;
                  t._24h_withdrawal_amount_a += result.received.front().amount.value;
                  t._24h_withdrawal_amount_b += result.received.back().amount.value;
                  t._24h_withdrawal_share_amount += op.share_amount.amount.value;
                  t._24h_withdrawal_fee_a += result.fees.front().amount.value;
                  t._24h_withdrawal_fee_b += result.fees.back().amount.value;
                  t._24h_balance_delta_a -= result.received.front().amount.value;
                  t._24h_balance_delta_b -= result.received.back().amount.value;
                  t.total_withdrawal_count += 1;
                  t.total_withdrawal_amount_a += result.received.front().amount.value;
                  t.total_withdrawal_amount_b += result.received.back().amount.value;
                  t.total_withdrawal_share_amount += op.share_amount.amount.value;
                  t.total_withdrawal_fee_a += result.fees.front().amount.value;
                  t.total_withdrawal_fee_b += result.fees.back().amount.value;
               });

            }
            else if( oho.op.is_type< liquidity_pool_exchange_operation >() )
            {
               auto& op = oho.op.get< liquidity_pool_exchange_operation >();
               auto& result = oho.result.get< generic_exchange_operation_result >();

               db.modify( *ticker, [&op,&result]( liquidity_pool_ticker_object& t ) {
                  auto amount_in = op.amount_to_sell.amount - result.fees.front().amount;
                  auto amount_out = result.received.front().amount + result.fees.at(1).amount;
                  if( op.amount_to_sell.asset_id < op.min_to_receive.asset_id ) // pool got a, paid b
                  {
                     t._24h_exchange_a2b_count += 1;
                     t._24h_exchange_a2b_amount_a += amount_in.value;
                     t._24h_exchange_a2b_amount_b += amount_out.value;
                     t._24h_exchange_fee_b += result.fees.back().amount.value;
                     t._24h_balance_delta_a += amount_in.value;
                     t._24h_balance_delta_b -= amount_out.value;
                     t.total_exchange_a2b_count += 1;
                     t.total_exchange_a2b_amount_a += amount_in.value;
                     t.total_exchange_a2b_amount_b += amount_out.value;
                     t.total_exchange_fee_b += result.fees.back().amount.value;
                  }
                  else // pool got b, paid a
                  {
                     t._24h_exchange_b2a_count += 1;
                     t._24h_exchange_b2a_amount_a += amount_out.value;
                     t._24h_exchange_b2a_amount_b += amount_in.value;
                     t._24h_exchange_fee_a += result.fees.back().amount.value;
                     t._24h_balance_delta_a -= amount_out.value;
                     t._24h_balance_delta_b += amount_in.value;
                     t.total_exchange_b2a_count += 1;
                     t.total_exchange_b2a_amount_a += amount_out.value;
                     t.total_exchange_b2a_amount_b += amount_in.value;
                     t.total_exchange_fee_a += result.fees.back().amount.value;
                  }
               });

            }
         }
      }


   }

} FC_CAPTURE_AND_LOG( (time)(oho) ) }


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
           "Track market history by grouping orders into buckets of equal size measured "
           "in seconds specified as a JSON array of numbers")
         ("history-per-size", boost::program_options::value<uint32_t>()->default_value(1000),
           "How far back in time to track history for each bucket size, "
           "measured in the number of buckets (default: 1000)")
         ("max-order-his-records-per-market", boost::program_options::value<uint32_t>()->default_value(1000),
           "Will only store this amount of matched orders for each market in order history for querying, "
           "or those meet the other option, which has more data (default: 1000). "
           "This parameter is reused for liquidity pools as maximum operations per pool in history.")
         ("max-order-his-seconds-per-market", boost::program_options::value<uint32_t>()->default_value(259200),
           "Will only store matched orders in last X seconds for each market in order history for querying, "
           "or those meet the other option, which has more data (default: 259200 (3 days)). "
           "This parameter is reused for liquidity pools as operations in last X seconds per pool in history. "
           "Note: this parameter need to be greater than 24 hours to be able to serve market ticker data correctly.")
         ;
   cfg.add(cli);
}

void market_history_plugin::plugin_initialize(const boost::program_options::variables_map& options)
{ try {
   database().applied_block.connect( [this]( const signed_block& b){ my->update_market_histories(b); } );

   database().add_index< primary_index< bucket_index  > >();
   database().add_index< primary_index< history_index  > >();
   database().add_index< primary_index< market_ticker_index  > >();
   database().add_index< primary_index< simple_index< market_ticker_meta_object > > >();

   database().add_index< primary_index< liquidity_pool_history_index > >();
   database().add_index< primary_index< simple_index< liquidity_pool_ticker_meta_object > > >();
   database().add_index< primary_index< liquidity_pool_ticker_index, 8 > >(); // 256 pools per chunk

   if( options.count( "bucket-size" ) == 1u )
   {
      const std::string& buckets = options["bucket-size"].as<string>();
      my->_tracked_buckets = fc::json::from_string(buckets).as<flat_set<uint32_t>>(2);
      my->_tracked_buckets.erase( 0 );
   }
   if( options.count( "history-per-size" ) == 1u )
      my->_maximum_history_per_bucket_size = options["history-per-size"].as<uint32_t>();
   if( options.count( "max-order-his-records-per-market" ) == 1u )
      my->_max_order_his_records_per_market = options["max-order-his-records-per-market"].as<uint32_t>();
   if( options.count( "max-order-his-seconds-per-market" ) == 1u )
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
