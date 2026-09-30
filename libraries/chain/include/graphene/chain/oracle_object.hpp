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
#pragma once

#include <graphene/chain/types.hpp>
#include <graphene/protocol/oracle.hpp>
#include <graphene/db/generic_index.hpp>

#include <boost/multi_index/composite_key.hpp>

namespace graphene { namespace chain {

using graphene::protocol::oracle_options;
using graphene::protocol::oracle_aggregation_method;

/**
 *  @brief A named price series, published to by a set of producers and referenced by id.
 *  @ingroup object
 *  @ingroup protocol
 *
 *  Unlike a legacy price feed, an oracle is not an attribute of an asset. It exists in its
 *  own right so that several consumers can reference the same series, and so that a price
 *  that is not the settlement price of any particular smartcoin has somewhere to live.
 *
 *  See ORACLE-DESIGN.md for the reasoning behind the aggregation rules, and for why there is
 *  no outlier filter.
 */
class oracle_object : public abstract_object<oracle_object, protocol_ids, oracle_object_type>
{
   public:
      account_id_type owner;          ///< Creator and administrator
      string          name;           ///< Unique, e.g. "BTC.USD"
      string          description;

      /// What a published price means. Immutable once created: consumers reference an oracle
      /// by id and would have no way to notice that what it measures had changed.
      asset_id_type   base_asset;
      asset_id_type   quote_asset;

      oracle_options  options;

      /// Latest submission per producer. Bounded by options.producers.
      flat_map<account_id_type, pair<time_point_sec, price>> submissions;

      /// Recent aggregates, oldest first, capped at GRAPHENE_ORACLE_MAX_HISTORY. Recorded from
      /// the moment the oracle exists, because history cannot be reconstructed later.
      vector<pair<time_point_sec, price>> history;

      /**
       * The current aggregate, or absent when there is no usable value.
       *
       * Absent rather than a null price on purpose. A null price reads as zero in several
       * existing code paths, and a consumer that mistakes "no data" for "price is zero" fails
       * in the most expensive possible direction. An absent optional cannot be misread.
       */
      optional<price>  current_value;
      time_point_sec   current_value_time;

      /// Number of live submissions behind @ref current_value, for API visibility into why an
      /// oracle is reporting nothing.
      uint16_t         current_value_producer_count = 0;

      /**
       * Market-issued assets whose settlement price comes from this oracle.
       *
       * Maintained by the asset_update_bitasset evaluator rather than derived by scanning, for
       * two reasons: publishing must refresh every bound asset's feed, and doing that by walking
       * all bitassets on every publish would put work proportional to the whole chain's asset
       * count into an operation designed to be sent every few seconds. It also makes the
       * "refuse to delete an oracle something depends on" check O(1).
       *
       * Capped at GRAPHENE_ORACLE_MAX_SUBSCRIBERS so the work a single publish can trigger is
       * bounded.
       */
      flat_set<asset_id_type> subscribers;

      /// @return whether a submission made at @p published is still live at @p now
      bool is_submission_live( time_point_sec published, time_point_sec now )const
      {
         return published != time_point_sec()
                && ( now - published ).to_seconds() < int64_t( options.value_lifetime_sec );
      }

      /**
       * @return whether @ref current_value is still within its lifetime at @p now.
       *
       * Freshness has to be a READ-time question, not a write-time one. current_value is only
       * recomputed when a producer publishes, so an oracle whose producers all stop simply
       * keeps its last value for ever: there is no sweep that would notice. Consumers that ask
       * only `current_value.valid()` therefore go on using a price that expired long ago, and
       * value_lifetime_sec silently means nothing.
       */
      bool is_value_live( time_point_sec now )const
      {
         return current_value.valid() && is_submission_live( current_value_time, now );
      }

      /**
       * Recomputes @ref current_value from @ref submissions and, for median_over_window, from
       * @ref history, and appends to the history ring.
       *
       * Deterministic by construction: it only ever selects one of the observed prices, never
       * computes a new one, so there is no rounding or overflow that could differ between
       * nodes. @p now must be the head block time.
       */
      void update_current_value( time_point_sec now );
};

struct by_oracle_name;   ///< unique, and how humans address an oracle
struct by_owner;         ///< for API
struct by_asset_pair;    ///< for API

/**
* @ingroup object_index
*/
using oracle_multi_index_type = multi_index_container<
   oracle_object,
   indexed_by<
      ordered_unique< tag<by_id>, member< object, object_id_type, &object::id > >,
      ordered_unique< tag<by_oracle_name>,
         member< oracle_object, string, &oracle_object::name >
      >,
      ordered_unique< tag<by_owner>,
         composite_key< oracle_object,
            member< oracle_object, account_id_type, &oracle_object::owner >,
            member< object, object_id_type, &object::id >
         >
      >,
      ordered_unique< tag<by_asset_pair>,
         composite_key< oracle_object,
            member< oracle_object, asset_id_type, &oracle_object::base_asset >,
            member< oracle_object, asset_id_type, &oracle_object::quote_asset >,
            member< object, object_id_type, &object::id >
         >
      >
   >
>;

/**
* @ingroup object_index
*/
using oracle_index = generic_index<oracle_object, oracle_multi_index_type>;

} } // graphene::chain

MAP_OBJECT_ID_TO_TYPE( graphene::chain::oracle_object )

FC_REFLECT_TYPENAME( graphene::chain::oracle_object )

GRAPHENE_DECLARE_EXTERNAL_SERIALIZATION( graphene::chain::oracle_object )
