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
#include <graphene/protocol/futures.hpp>
#include <graphene/db/generic_index.hpp>

#include <boost/multi_index/composite_key.hpp>

namespace graphene { namespace chain {

using graphene::protocol::futures_market_options;

/**
 *  @brief A futures contract: perpetual or dated, margined and settled against an oracle.
 *  @ingroup object
 *  @ingroup protocol
 *
 *  Prices here -- @ref mark_price, @ref settlement_price, and order and position prices -- are
 *  integer amounts of @ref collateral_asset per contract, never ratios. See FUTURES-DESIGN.md:
 *  it is what makes position accounting exact, and in a leveraged market exactness is
 *  solvency.
 */
class futures_market_object : public abstract_object<futures_market_object, protocol_ids,
                                                     futures_market_object_type>
{
   public:
      account_id_type owner;
      string          symbol;          ///< Unique, e.g. "BTC-PERP"
      string          description;

      /// Index price source. Immutable: traders hold positions priced against it.
      oracle_id_type  oracle_id;
      /// Margin and PnL currency. Equal to the oracle's quote asset, so nothing is ever
      /// converted between units.
      asset_id_type   collateral_asset;
      /// Units of the oracle's base asset per contract. Immutable.
      share_type      contract_size;
      /// Absent means perpetual.
      optional<time_point_sec> expiry;

      futures_market_options options;

      // ---- state ----

      /**
       * Collateral per contract, derived from the oracle. Absent when the oracle has no value.
       *
       * Absent rather than zero or stale: risk is assessed against this, and a market that
       * kept liquidating against a dead price would be worse than one that pauses.
       */
      optional<share_type> mark_price;
      time_point_sec       mark_price_time;

      /// Contracts open on the long side. Equal to the short side by construction.
      share_type      open_interest;

      /// Monotone funding index, collateral per contract, signed. A position pays the
      /// difference between this and its own last value when next touched, so a funding tick
      /// does not have to walk every position.
      share_type      cumulative_funding;
      time_point_sec  last_funding_time;

      /**
       * Time-weighted average premium over the current funding interval, in collateral per
       * contract, and when it was last sampled.
       *
       * The premium must be averaged over the interval rather than read once at the end of it.
       * A single instantaneous reading of the book mid is set by whichever two orders happen to
       * be best at that instant, so on a thin book one non-marketable order placed just before
       * the sample -- and cancelled just after -- moves the mid as far as the rate cap allows.
       * That is a repeatable transfer from one side of the market to the other, every interval,
       * for the price of an order that never fills.
       *
       * Weighting by time makes that cost real: to move the average you have to hold the quote
       * for a meaningful share of the interval, during which anyone may trade against it.
       */
      share_type      premium_avg;
      /// The most recent instantaneous observation, carried so the NEXT sample can weight the
      /// span that just elapsed by the premium that was actually in force during it.
      share_type      premium_last;
      time_point_sec  last_premium_time;

      /// Holds liquidation penalties and rounding dust, and covers liquidation shortfalls.
      share_type      insurance_fund;

      /// Set once, at expiry.
      optional<share_type> settlement_price;
      bool            is_settled = false;

      bool is_perpetual()const { return !expiry.valid(); }

      /// @return whether new orders may be placed right now
      bool is_tradable( time_point_sec now )const
      {
         return options.enabled && !is_settled && mark_price.valid()
                && ( is_perpetual() || now < *expiry );
      }
};

struct by_futures_symbol;   ///< unique
struct by_owner;            ///< for API
struct by_oracle;           ///< to find the markets a published value must update
struct by_expiry;           ///< to find contracts due to settle

/**
* @ingroup object_index
*/
using futures_market_multi_index_type = multi_index_container<
   futures_market_object,
   indexed_by<
      ordered_unique< tag<by_id>, member< object, object_id_type, &object::id > >,
      ordered_unique< tag<by_futures_symbol>,
         member< futures_market_object, string, &futures_market_object::symbol >
      >,
      ordered_unique< tag<by_owner>,
         composite_key< futures_market_object,
            member< futures_market_object, account_id_type, &futures_market_object::owner >,
            member< object, object_id_type, &object::id >
         >
      >,
      ordered_unique< tag<by_oracle>,
         composite_key< futures_market_object,
            member< futures_market_object, oracle_id_type, &futures_market_object::oracle_id >,
            member< object, object_id_type, &object::id >
         >
      >
   >
>;

/**
* @ingroup object_index
*/
using futures_market_index = generic_index<futures_market_object,
                                           futures_market_multi_index_type>;

/**
 *  @brief One account's position in one futures market.
 *  @ingroup object
 *
 *  @ref size is signed: positive is long, negative short. @ref entry_value is the running sum
 *  of size x fill price, not an average entry price, so that
 *
 *      unrealized pnl = size x mark - entry_value
 *
 *  is exact integer arithmetic and the two sides of a contract are exactly antisymmetric.
 *  Across a whole market, Sum(size) and Sum(entry_value) are both identically zero -- see
 *  FUTURES-DESIGN.md. That is the solvency invariant, and it is why nothing here divides.
 */
class futures_position_object : public abstract_object<futures_position_object, protocol_ids,
                                                       futures_position_object_type>
{
   public:
      account_id_type        owner;
      futures_market_id_type market_id;

      /// Signed contracts. Positive long, negative short. Never zero: a position that reaches
      /// zero is paid out and removed.
      share_type             size;

      /// Running sum of size x fill price.
      share_type             entry_value;

      /// Collateral posted against this position.
      share_type             margin;

      /// Funding index when funding was last applied to this position.
      share_type             last_cumulative_funding;

      bool is_long()const { return size > 0; }

      /// Absolute contract count, for margin requirements.
      share_type abs_size()const { return size >= 0 ? size : -size; }

      /// Unrealized PnL at @p mark. Exact: no division, no rounding.
      share_type unrealized_pnl( share_type mark )const
      { return size * mark - entry_value; }

      /// Margin plus unrealized PnL: what the position is actually worth.
      share_type equity( share_type mark )const
      { return margin + unrealized_pnl( mark ); }

      /**
       * Equity including funding that has accrued but not yet been settled onto this
       * position.
       *
       * `margin` only moves when the position is touched, so between touches it overstates
       * what the position is actually worth by exactly the unsettled funding. Any decision
       * about solvency has to use this, not equity(): a position can be deeply underwater
       * on funding alone while equity() still reads the margin it was opened with.
       */
      share_type equity_after_funding( share_type mark, share_type market_cumulative_funding )const
      {
         const share_type pending = size * ( market_cumulative_funding - last_cumulative_funding );
         return equity( mark ) - pending;
      }
};

/**
 *  @brief A resting limit order on a futures market.
 *  @ingroup object
 *
 *  Margin for the whole order is reserved when it is placed. An order that could not be paid
 *  for if it filled has no business sitting on the book.
 */
class futures_order_object : public abstract_object<futures_order_object, protocol_ids,
                                                    futures_order_object_type>
{
   public:
      account_id_type        owner;
      futures_market_id_type market_id;
      bool                   is_long = true;
      share_type             price_per_contract;
      share_type             size;              ///< contracts remaining
      share_type             deferred_margin;   ///< reserved collateral, returned on cancel
};

struct by_position_owner;   ///< for API, and to find an account's position in a market
struct by_market_owner;     ///< unique: one position per account per market
struct by_market_book;      ///< the order book
struct by_order_owner;      ///< for API

using futures_position_multi_index_type = multi_index_container<
   futures_position_object,
   indexed_by<
      ordered_unique< tag<by_id>, member< object, object_id_type, &object::id > >,
      // One position per account per market. Isolated margin means a second position would be
      // a second, independently liquidatable thing on the same market -- which is cross margin
      // wearing a disguise.
      ordered_unique< tag<by_market_owner>,
         composite_key< futures_position_object,
            member< futures_position_object, futures_market_id_type,
                    &futures_position_object::market_id >,
            member< futures_position_object, account_id_type, &futures_position_object::owner >
         >
      >,
      ordered_unique< tag<by_position_owner>,
         composite_key< futures_position_object,
            member< futures_position_object, account_id_type, &futures_position_object::owner >,
            member< object, object_id_type, &object::id >
         >
      >
   >
>;

using futures_position_index = generic_index<futures_position_object,
                                             futures_position_multi_index_type>;

using futures_order_multi_index_type = multi_index_container<
   futures_order_object,
   indexed_by<
      ordered_unique< tag<by_id>, member< object, object_id_type, &object::id > >,
      // The book. Longs are scanned in reverse (highest bid first), shorts forward (lowest ask
      // first); the id tiebreaker gives price-time priority, since ids increase with time.
      ordered_unique< tag<by_market_book>,
         composite_key< futures_order_object,
            member< futures_order_object, futures_market_id_type,
                    &futures_order_object::market_id >,
            member< futures_order_object, bool, &futures_order_object::is_long >,
            member< futures_order_object, share_type,
                    &futures_order_object::price_per_contract >,
            member< object, object_id_type, &object::id >
         >
      >,
      ordered_unique< tag<by_order_owner>,
         composite_key< futures_order_object,
            member< futures_order_object, account_id_type, &futures_order_object::owner >,
            member< object, object_id_type, &object::id >
         >
      >
   >
>;

using futures_order_index = generic_index<futures_order_object, futures_order_multi_index_type>;

} } // graphene::chain

MAP_OBJECT_ID_TO_TYPE( graphene::chain::futures_market_object )
MAP_OBJECT_ID_TO_TYPE( graphene::chain::futures_position_object )
MAP_OBJECT_ID_TO_TYPE( graphene::chain::futures_order_object )

FC_REFLECT_TYPENAME( graphene::chain::futures_market_object )
FC_REFLECT_TYPENAME( graphene::chain::futures_position_object )
FC_REFLECT_TYPENAME( graphene::chain::futures_order_object )

GRAPHENE_DECLARE_EXTERNAL_SERIALIZATION( graphene::chain::futures_market_object )
GRAPHENE_DECLARE_EXTERNAL_SERIALIZATION( graphene::chain::futures_position_object )
GRAPHENE_DECLARE_EXTERNAL_SERIALIZATION( graphene::chain::futures_order_object )
