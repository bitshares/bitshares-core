/*
 * Copyright (c) 2026 BitShares contributors.
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
#include <graphene/chain/oracle_object.hpp>

#include <fc/io/raw.hpp>

#include <algorithm>

namespace graphene { namespace chain {

namespace {

/// The lowest value whose cumulative weight *exceeds* half the total.
///
/// Strictly greater, not greater-or-equal, so that with every weight equal to 1 this reduces
/// to the element at index size/2 -- exactly the median the legacy feed code picks
/// (asset_object.cpp, `effective_feeds.begin() + size/2`). That matters for migration: an
/// asset switching from legacy feeds to an oracle with the same producers must not see its
/// price jump because the two disagreed about which element of an even-sized set is "the"
/// median.
price weighted_median( vector<pair<price, uint32_t>>& entries )
{
   std::sort( entries.begin(), entries.end(),
              []( const pair<price, uint32_t>& a, const pair<price, uint32_t>& b )
              { return a.first < b.first; } );

   uint64_t total = 0;
   for( const auto& e : entries )
      total += e.second;

   uint64_t accumulated = 0;
   for( const auto& e : entries )
   {
      accumulated += e.second;
      if( accumulated * 2 > total )
         return e.first;
   }
   return entries.back().first; // unreachable while total > 0
}

/// Upper median for even-sized sets, matching both the weighted median above and the legacy
/// feed code. Deliberately not the mean of the two middle values: averaging means dividing,
/// and a rounded result is not something every node is guaranteed to agree on.
price upper_median( vector<price>& values )
{
   std::sort( values.begin(), values.end() );
   return values[ values.size() / 2 ];
}

} // namespace

void oracle_object::update_current_value( time_point_sec now )
{
   vector<pair<price, uint32_t>> live;
   live.reserve( submissions.size() );
   for( const auto& s : submissions )
   {
      if( !is_submission_live( s.second.first, now ) )
         continue;
      // A producer removed since publishing keeps its stored submission but stops counting,
      // so removing a misbehaving producer takes effect immediately rather than at expiry.
      const auto itr = options.producers.find( s.first );
      if( itr == options.producers.end() )
         continue;
      live.emplace_back( s.second.second, uint32_t( itr->second ) );
   }

   current_value_time = now;

   if( live.size() < size_t( options.minimum_producers ) )
   {
      // Reported as absent rather than stale. A consumer that kept using the last known value
      // through an outage would be trading on a number nobody is still asserting.
      current_value.reset();
      current_value_producer_count = 0;
      return;
   }

   const price latest = weighted_median( live );
   current_value_producer_count = uint16_t( live.size() );

   // Sample into time buckets rather than appending every publish.
   //
   // The ring holds GRAPHENE_ORACLE_MAX_HISTORY entries. Appending one per publish made the
   // window it can span a function of how often producers publish, not of window_sec: at one
   // publish per block the ring covers about five minutes, so an oracle configured to take a
   // median over an hour -- or a day -- was in fact taking it over five minutes, and nothing
   // said so. The damping got WEAKER the more diligently the producers published.
   //
   // Spacing entries at window_sec/MAX_HISTORY makes a full ring span the configured window
   // whatever the publish rate. Publishes inside the current bucket refresh its value rather
   // than consuming a slot, so the newest sample is always the newest aggregate.
   const int64_t bucket = std::max<int64_t>(
      1, int64_t( options.window_sec ) / int64_t( GRAPHENE_ORACLE_MAX_HISTORY ) );

   // Refresh the VALUE but keep the timestamp, because it marks when the bucket opened.
   // Advancing it on every publish would push the boundary forward each time and the bucket
   // would never close -- the ring would hold one perpetually-refreshed entry.
   if( !history.empty() && ( now - history.back().first ).to_seconds() < bucket )
      history.back().second = latest;
   else
      history.emplace_back( now, latest );

   if( history.size() > size_t( GRAPHENE_ORACLE_MAX_HISTORY ) )
      history.erase( history.begin() );

   if( oracle_aggregation_method::median_of_latest == options.get_aggregation() )
   {
      current_value = latest;
      return;
   }

   vector<price> in_window;
   in_window.reserve( history.size() );
   for( const auto& h : history )
   {
      if( ( now - h.first ).to_seconds() <= int64_t( options.window_sec ) )
         in_window.push_back( h.second );
   }
   // Never empty: the entry just appended carries timestamp `now`.
   current_value = upper_median( in_window );
}

} } // graphene::chain

FC_REFLECT_DERIVED_NO_TYPENAME( graphene::chain::oracle_object, (graphene::db::object),
                    (owner)
                    (name)
                    (description)
                    (base_asset)
                    (quote_asset)
                    (options)
                    (submissions)
                    (history)
                    (current_value)
                    (current_value_time)
                    (current_value_producer_count)
                    (subscribers)
                  )

GRAPHENE_IMPLEMENT_EXTERNAL_SERIALIZATION( graphene::chain::oracle_object )
