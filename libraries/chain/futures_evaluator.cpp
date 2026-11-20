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
#include <graphene/chain/futures_evaluator.hpp>

#include <boost/multiprecision/cpp_int.hpp>
#include <graphene/chain/futures_object.hpp>
#include <graphene/chain/oracle_object.hpp>

#include <graphene/chain/asset_object.hpp>
#include <graphene/chain/database.hpp>
#include <graphene/chain/hardfork.hpp>

#include <fc/uint128.hpp>

#include <algorithm>

namespace graphene { namespace chain {

// Defined below; declared here because update_futures_mark_price samples the premium before
// the interval is closed out, and both helpers depend on futures_ppm_of.
share_type futures_ppm_of( share_type amount, uint32_t ppm );
void accumulate_premium( database& d, const futures_market_object& market, share_type mark );

/**
 * Rate-limit how far the mark may move from where it already is.
 *
 * The oracle's aggregate is the target, not the answer. A single print becomes the price that
 * margin, liquidation and settlement are all measured against, so an outlier that reverts in
 * the next block can still cascade liquidations across every position in the market before it
 * does. Producers aggregate across each other, but a market gets no say in that and inherits
 * whatever aggregation its oracle happens to be configured for.
 *
 * The allowance is proportional to elapsed time, so a sustained move arrives in full -- just
 * over seconds rather than instantly -- while a spike that reverts never lands. Elapsed is
 * capped at the funding interval so a long silence cannot bank an unlimited allowance and hand
 * the first publish afterwards a free hand.
 *
 * Always at least one unit, so a small mark can still converge rather than being frozen by
 * integer rounding.
 */
share_type damp_mark( const futures_market_object& market, share_type target,
                      time_point_sec now )
{
   if( 0 == market.options.max_mark_move_ppm || !market.mark_price.valid() )
      return target;   // limit disabled, or there is no previous mark to move away from

   const share_type previous = *market.mark_price;

   int64_t elapsed = ( now - market.mark_price_time ).to_seconds();
   if( elapsed < 0 )
      elapsed = 0;
   elapsed = std::min<int64_t>( elapsed, int64_t( market.options.funding_interval_sec ) );

   fc::uint128_t allowed = ( fc::uint128_t( previous.value )
                             * market.options.max_mark_move_ppm
                             * uint64_t( elapsed ) + 999999 ) / 1000000;
   if( allowed < 1 )
      allowed = 1;
   if( allowed > fc::uint128_t( GRAPHENE_MAX_SHARE_SUPPLY ) )
      allowed = fc::uint128_t( GRAPHENE_MAX_SHARE_SUPPLY );
   const share_type allowance{ static_cast<int64_t>( allowed ) };

   if( target > previous + allowance )
      return previous + allowance;
   if( target < previous - allowance )
   {
      // A mark of zero or less is not a price; the market reads it as having no mark at all.
      const share_type floored = previous - allowance;
      return floored > 0 ? floored : share_type( 1 );
   }
   return target;
}

void update_futures_mark_price( database& d, const futures_market_object& market )
{
   const oracle_object* o = d.find( market.oracle_id );

   optional<share_type> new_mark;
   if( nullptr != o && o->current_value.valid() )
   {
      // The oracle quotes base/quote as a ratio; a contract is priced as an integer amount of
      // the quote (== collateral) asset. Converting one contract's worth of the base through
      // the oracle price gives exactly that, using the same rounding asset::operator* has
      // always used rather than a second convention invented here.
      const asset one_contract( market.contract_size, o->base_asset );
      const asset in_collateral = one_contract * (*o->current_value);
      // A contract whose whole notional rounds to zero cannot be margined or liquidated
      // meaningfully, so it is treated as no mark at all rather than as a price of nothing.
      if( in_collateral.amount > 0 )
         new_mark = damp_mark( market, in_collateral.amount, d.head_block_time() );
   }

   const auto now = d.head_block_time();
   if( market.mark_price == new_mark && market.mark_price_time == now )
      return;

   d.modify( market, [&new_mark, now]( futures_market_object& m ) {
      m.mark_price = new_mark;
      m.mark_price_time = now;
   } );

   // A new mark is the natural moment both to sample the premium and to check whether a
   // funding interval has elapsed: it is driven by the oracle, which publishes continuously, so
   // no separate timer is needed. Sampling must happen FIRST, so the span since the previous
   // publish is folded in before the interval is closed out.
   if( new_mark.valid() )
      accumulate_premium( d, market, *new_mark );
   accrue_futures_funding( d, market );

   // Expiry fixes the settlement price HERE, on the first oracle publish at or after it,
   // rather than in futures_settle_evaluator on the first caller.
   //
   // Letting the caller fix it handed whoever got there first a free option: the price
   // everyone settles against was the mark in whatever block that caller chose, and a block
   // producer chooses its own blocks. They cannot move the oracle -- that needs a majority of
   // producers -- but they could pick among the values it published, and settle when one
   // suited the position they held. Everyone else in the contract paid for that choice.
   //
   // Driving it from the oracle instead removes the choice. What is left is that a producer
   // could delay the first post-expiry publish by the blocks it controls, which moves the
   // snapshot by seconds rather than by however long it cared to wait.
   if( new_mark.valid() && !market.is_settled && market.expiry.valid() && now >= *market.expiry )
   {
      const share_type final_price = *new_mark;
      d.modify( market, [&final_price]( futures_market_object& m ) {
         m.settlement_price = final_price;
         m.is_settled       = true;
      } );
   }
}

/**
 * The mark price, but only while the oracle behind it is still live.
 *
 * market.mark_price is a cached figure: it is written when a producer publishes and never
 * revisited, so it does not decay. mark_price_time was recorded and then read by nothing --
 * every consumer asked `mark_price.valid()`, which stays true for ever once set.
 *
 * That matters because the mark is what margin, liquidation and settlement are measured
 * against. An oracle that stops publishing froze the mark, and with it froze the risk
 * assessment: positions that should have been liquidated no longer looked liquidatable, and a
 * dated contract could be settled -- permanently, for everyone in it -- against a price from
 * long before expiry.
 *
 * Asking the oracle at read time is the only thing that can notice a producer who simply never
 * comes back. Returning nothing on a stale oracle makes every caller fail closed, which they
 * already handle: they have a "market has no mark price" path from the case where the oracle
 * never had a value at all.
 */
optional<share_type> live_mark_price( const database& d, const futures_market_object& market )
{
   if( !market.mark_price.valid() )
      return {};
   const oracle_object* o = d.find( market.oracle_id );
   if( nullptr == o || !o->is_value_live( d.head_block_time() ) )
      return {};
   return market.mark_price;
}

/// @return the instantaneous premium of the book's mid over the mark, clamped to the market's
/// rate cap, or nothing when either side of the book is empty and there is therefore no mid.
optional<share_type> sample_premium( const database& d, const futures_market_object& market,
                                     share_type mark )
{
   const auto& book = d.get_index_type<futures_order_index>().indices().get<by_market_book>();
   auto bid_end = book.upper_bound( boost::make_tuple( market.get_id(), true ) );
   const auto bid_begin = book.lower_bound( boost::make_tuple( market.get_id(), true ) );
   auto ask_itr = book.lower_bound( boost::make_tuple( market.get_id(), false ) );
   const auto ask_end = book.upper_bound( boost::make_tuple( market.get_id(), false ) );

   if( bid_end == bid_begin || ask_itr == ask_end )
      return {};

   // Volume-weighted price of filling up to `want` contracts, walking outward from the best
   // price on each side: the IMPACT price.
   //
   // Sampling this rather than the top of book is what makes the funding rate cost something
   // to move. With a plain mid, two one-contract orders shifted the rate as far as two
   // million-contract orders would, so a holder could bracket the book with a single contract
   // a side and collect the capped rate on an arbitrarily large position while risking one
   // contract. Here the attacker has to own the first impact_size contracts of depth and stay
   // exposed on all of them, so the cost of moving the rate scales with impact_size.
   //
   // A side holding less than `want` is priced over the depth that exists rather than being
   // skipped: skipping would hand a thin market a premium of zero, which is its own
   // manipulation -- an attacker could suppress funding by pulling one side.
   const int64_t want = std::max<int64_t>( 1, int64_t( market.options.impact_size ) );

   // Bids ascend by price, so the best bid is the last entry and further depth lies backwards.
   fc::uint128_t bid_notional = 0;
   int64_t bid_filled = 0;
   for( auto b = bid_end; b != bid_begin && bid_filled < want; )
   {
      --b;
      const int64_t take = std::min<int64_t>( b->size.value, want - bid_filled );
      bid_notional += fc::uint128_t( b->price_per_contract.value ) * uint64_t( take );
      bid_filled   += take;
   }

   // Asks ascend by price, so the best ask is the first entry and further depth lies forwards.
   fc::uint128_t ask_notional = 0;
   int64_t ask_filled = 0;
   for( auto a = ask_itr; a != ask_end && ask_filled < want; ++a )
   {
      const int64_t take = std::min<int64_t>( a->size.value, want - ask_filled );
      ask_notional += fc::uint128_t( a->price_per_contract.value ) * uint64_t( take );
      ask_filled   += take;
   }

   // Both are at least one: the emptiness of either side was ruled out above, and an order
   // that reached the book carries at least one contract.
   //
   // Computed exactly and rounded once, rather than as three separate integer divisions.
   // Dividing out impact_bid, dividing out impact_ask and then halving their sum truncates
   // three times, and every operand is positive, so all three truncations go the same way:
   // the premium came out low by up to about 1.5 units in every sample. A bias that never
   // changes sign does not average away over an interval, and it is large next to the cap
   // whenever the mark is small -- with a mark of 100 and impact prices of 100.9 and 101.9
   // the true premium is 1.4 while the truncated one is 0, so a rate capped at 1% did not
   // lose a little precision, it read as no premium at all. The same failure mode is already
   // recorded further down, where an integer divide by 100 collapsed every cap below 100 ppm
   // to zero.
   //
   //   premium = ( bid_notional/bid_filled + ask_notional/ask_filled ) / 2 - mark
   //           = ( bid_notional*ask_filled + ask_notional*bid_filled - mark*den ) / den
   //   with      den = 2 * bid_filled * ask_filled
   //
   // Bounds: a price is at most GRAPHENE_MAX_SHARE_SUPPLY (1e15) and a side is filled to at
   // most GRAPHENE_FUTURES_MAX_IMPACT_SIZE (1e6), so a notional is at most 1e21, the
   // numerator stays below ~4e27 and den below 2e12 -- both far inside __int128.
   const __int128 den = __int128( 2 ) * bid_filled * ask_filled;
   const __int128 num = static_cast<__int128>( bid_notional ) * ask_filled
                      + static_cast<__int128>( ask_notional ) * bid_filled
                      - static_cast<__int128>( mark.value ) * den;

   // Half away from zero, so that a premium and its negation round by the same amount and
   // neither side of the market is favoured by the rounding itself. den is even by
   // construction, so half is exact.
   const __int128 half = den / 2;
   const __int128 exact = num >= 0 ? ( num + half ) / den
                                   : -( ( -num + half ) / den );
   share_type premium{ static_cast<int64_t>( exact ) };

   // Clamp each SAMPLE, not just the final average. It bounds the average by construction, and
   // it keeps the weighted sum below in range: an unclamped premium can be as large as the mark
   // itself, and multiplying that by an interval's worth of seconds overflows.
   const share_type cap = futures_ppm_of( mark, market.options.max_funding_rate_ppm );
   if( premium > cap )  premium = cap;
   if( premium < -cap ) premium = -cap;
   return premium;
}

/**
 * Fold the current premium into the interval's time-weighted average.
 *
 * Kept as a running average rather than a running sum on purpose: a sum of premium x seconds
 * overflows int64 for a large mark over a long interval, whereas the average is bounded by the
 * rate cap at every step. The weighting is done in 128-bit and divided straight back down.
 */
void accumulate_premium( database& d, const futures_market_object& market, share_type mark )
{
   const auto now = d.head_block_time();
   const auto last = market.last_premium_time == time_point_sec()
                   ? market.last_funding_time : market.last_premium_time;
   const int64_t dt = ( now - last ).to_seconds();

   const auto sampled = sample_premium( d, market, mark );
   // No mid means no premium, which is a real observation of zero rather than a gap to be
   // skipped: a market with an empty side is not exhibiting a premium.
   const share_type sample = sampled.valid() ? *sampled : share_type( 0 );

   if( dt <= 0 )
   {
      // Same second as the last look. Nothing elapsed to weight; just refresh the observation.
      d.modify( market, [&sample]( futures_market_object& m ) { m.premium_last = sample; } );
      return;
   }

   const int64_t before = ( last - market.last_funding_time ).to_seconds();

   // The span that just elapsed carried the premium observed at its START, not the one being
   // observed now. Weighting by the current sample would hand the whole span to whatever is on
   // the book at this instant -- which is exactly the manipulation this is meant to prevent,
   // reintroduced one level up.
   const boost::multiprecision::int128_t weighted =
         boost::multiprecision::int128_t( market.premium_avg.value ) * before
       + boost::multiprecision::int128_t( market.premium_last.value ) * dt;
   const share_type new_avg{ static_cast<int64_t>( weighted / ( before + dt ) ) };

   d.modify( market, [&new_avg, &sample, now]( futures_market_object& m ) {
      m.premium_avg       = new_avg;
      m.premium_last      = sample;
      m.last_premium_time = now;
   } );
}

void update_futures_markets_for_oracle( database& d, const oracle_object& o )
{
   // Walks only the markets bound to this oracle, via the by_oracle index. Publishing happens
   // continuously, so it must never do work proportional to the total number of markets.
   const auto& idx = d.get_index_type<futures_market_index>().indices().get<by_oracle>();
   auto itr = idx.lower_bound( o.get_id() );
   const auto end = idx.upper_bound( o.get_id() );
   while( itr != end )
   {
      const futures_market_object& market = *itr;
      ++itr;   // update_futures_mark_price modifies the object, so advance first
      update_futures_mark_price( d, market );
   }
}

void_result futures_market_create_evaluator::do_evaluate(
      const futures_market_create_operation& op ) const
{ try {
   const database& d = db();
   const auto now = d.head_block_time();

   FC_ASSERT( HARDFORK_FUTURES_PASSED( now ), "Not allowed until the futures hardfork" );

   const oracle_object& o = op.oracle_id(d);
   op.collateral_asset(d);   // throws if the asset does not exist

   // Margin, PnL and the mark price are all denominated in the collateral asset. Requiring the
   // oracle to quote against that same asset is what removes every unit conversion from
   // position accounting -- and a conversion is a division, which is a rounding decision.
   FC_ASSERT( o.quote_asset == op.collateral_asset,
              "Oracle '${n}' quotes against asset ${q}, but this market margins in ${c}; the "
              "oracle's quote asset must be the collateral asset",
              ("n", o.name)("q", o.quote_asset)("c", op.collateral_asset) );

   FC_ASSERT( o.base_asset != op.collateral_asset,
              "An oracle quoting an asset against itself cannot price a contract" );

   if( op.expiry.valid() )
   {
      FC_ASSERT( *op.expiry > now, "Expiry must be in the future" );
      FC_ASSERT( *op.expiry - now <= fc::days( GRAPHENE_FUTURES_MAX_EXPIRY_DAYS ),
                 "Expiry may not be more than ${d} days out",
                 ("d", GRAPHENE_FUTURES_MAX_EXPIRY_DAYS) );
   }

   const auto& idx = d.get_index_type<futures_market_index>().indices().get<by_futures_symbol>();
   FC_ASSERT( idx.find( op.symbol ) == idx.end(),
              "A futures market with symbol '${s}' already exists", ("s", op.symbol) );

   return void_result();
} FC_CAPTURE_AND_RETHROW( (op) ) } // GCOVR_EXCL_LINE

object_id_type futures_market_create_evaluator::do_apply(
      const futures_market_create_operation& op ) const
{ try {
   database& d = db();
   const auto now = d.head_block_time();

   const auto& market = d.create<futures_market_object>( [&op, now]( futures_market_object& m ) {
      m.owner             = op.owner;
      m.symbol            = op.symbol;
      m.description       = op.description;
      m.oracle_id         = op.oracle_id;
      m.collateral_asset  = op.collateral_asset;
      m.contract_size     = op.contract_size;
      m.expiry            = op.expiry;
      m.options           = op.options;
      m.last_funding_time = now;
   } );

   // Pick up whatever the oracle currently says, so a market is not born untradable purely
   // because nobody has published since it was created.
   update_futures_mark_price( d, market );

   return market.id;
} FC_CAPTURE_AND_RETHROW( (op) ) } // GCOVR_EXCL_LINE


namespace {

/**
 * Realises a position's PnL against the mark, moving it through the market's settlement pool.
 *
 * The pool is the piece that was missing. Deducting a loser's loss from their margin and
 * stopping there DESTROYS it -- the offsetting gain is still unrealised in some other
 * position, so nothing balances and the supply check shows collateral vanishing (it did: 80
 * units, when liquidation settled a position without a pool to settle into).
 *
 * With the pool, value moves rather than evaporating: losers pay into it, winners are paid out
 * of it, and margin + pool is conserved exactly. It can run negative when gains are realised
 * before the matching losses are collected, which is recovered as the losing positions are
 * next touched or liquidated.
 *
 * Exact, like everything else here: no division.
 */
/**
 * Charges a position whatever funding has accrued since it was last touched.
 *
 * Longs pay shorts when the book trades above the mark, and the reverse below it. The payment
 * is size x (index delta), so the two sides are exactly antisymmetric and it nets to zero
 * across the market -- it moves through the settlement pool for the same reason PnL does.
 */
void apply_funding( database& d, const futures_market_object& market,
                    const futures_position_object& pos )
{
   const share_type delta = market.cumulative_funding - pos.last_cumulative_funding;
   if( 0 == delta.value )
      return;

   // Positive index delta means longs pay: a long has size > 0, so payment > 0 leaves them.
   const share_type payment = pos.size * delta;
   d.modify( pos, [&]( futures_position_object& p ) {
      p.margin                  -= payment;
      p.last_cumulative_funding  = market.cumulative_funding;
   } );
   d.modify( market, [&payment]( futures_market_object& m ) { m.insurance_fund += payment; } );
}

void settle_to_mark( database& d, const futures_market_object& market,
                     const futures_position_object& pos )
{
   apply_funding( d, market, pos );

   const auto live = live_mark_price( d, market );
   if( !live.valid() )
      return;
   const share_type mark = *live;
   const share_type pnl  = pos.size * mark - pos.entry_value;
   if( 0 == pnl.value )
      return;

   d.modify( pos, [&]( futures_position_object& p ) {
      p.margin      += pnl;
      p.entry_value  = p.size * mark;
   } );
   d.modify( market, [&pnl]( futures_market_object& m ) { m.insurance_fund -= pnl; } );
}

/**
 * Applies one fill of @p n contracts at @p price to @p account's position in @p market.
 *
 * The whole of position accounting is these four lines, and that is the point. A fill adds
 * +n x price to a long's entry_value and -n x price to a short's, so Sum(entry_value) over the
 * market stays exactly zero, as does Sum(size). Nothing is realised on a partial close and
 * nothing is divided; a position that reaches zero holds exactly -entry_value of realised PnL,
 * which is paid out when it is removed.
 *
 * @param margin_in collateral moving from the order's reservation into the position
 * @return the change in open interest caused by this fill for this side
 */
share_type apply_fill( database& d, const futures_market_object& market, account_id_type account,
                       bool is_long, share_type n, share_type price, share_type margin_in )
{
   const share_type delta      = is_long ? n : -n;
   const share_type delta_value = delta * price;

   const auto& idx = d.get_index_type<futures_position_index>().indices().get<by_market_owner>();
   auto itr = idx.find( boost::make_tuple( market.get_id(), account ) );

   if( itr == idx.end() )
   {
      d.create<futures_position_object>( [&]( futures_position_object& p ) {
         p.owner                   = account;
         p.market_id               = market.get_id();
         p.size                    = delta;
         p.entry_value             = delta_value;
         p.margin                  = margin_in;
         p.last_cumulative_funding = market.cumulative_funding;

         // Settled to the mark below, once the object exists.
      } );
      settle_to_mark( d, market, *idx.find( boost::make_tuple( market.get_id(), account ) ) );
      return n;   // wholly opening
   }

   const futures_position_object& pos = *itr;
   const share_type old_abs = pos.abs_size();
   const share_type new_size = pos.size + delta;

   if( 0 == new_size.value )
   {
      // Funding first, because margin only moves when the position is touched.
      //
      // This is the one path through a fill that used neither settle_to_mark nor
      // apply_funding: a position opened once, held across any number of funding intervals
      // and then closed in a single fill was paid out on the margin it carried before the
      // first of them. Everything it owed stayed in cumulative_funding, unclaimed, while the
      // other side of the market had already paid into the settlement pool -- so the pool,
      // not the trader, carried it. The behaviour needed to collect it was simply to never
      // touch the position: no margin adjustment, no partial fill, one order in and one out.
      //
      // apply_funding is a no-op when nothing has accrued, and it moves size x (index delta)
      // into the pool, which is exactly where the counterparty's side of it already sits.
      apply_funding( d, market, pos );

      // Flat. unrealized = 0 x mark - entry_value = -entry_value, exactly the realised PnL,
      // so the payout is margin - entry_value with no rounding anywhere.
      const share_type final_entry = pos.entry_value + delta_value;
      const share_type payout = pos.margin + margin_in - final_entry;

      // The payout can exceed what this trader ever deposited -- that is what a profit IS --
      // and the excess has to come from somewhere real. It comes from the settlement pool,
      // which the counterparty paid into when they opened away from the mark. Without this the
      // profit is conjured and the supply check shows collateral appearing (it did: 200 units).
      d.modify( market, [&final_entry]( futures_market_object& m )
                        { m.insurance_fund += final_entry; } );

      if( payout > 0 )
         d.adjust_balance( account, asset( payout, market.collateral_asset ) );
      d.remove( pos );
      return -old_abs;
   }

   d.modify( pos, [&]( futures_position_object& p ) {
      p.size        = new_size;
      p.entry_value = p.entry_value + delta_value;
      p.margin      = p.margin + margin_in;

   } );

   settle_to_mark( d, market, pos );

   const share_type new_abs = new_size >= 0 ? new_size : -new_size;
   return new_abs - old_abs;
}

/**
 * After a fill, the position must be able to stand at the CURRENT MARK, not merely at the
 * price it traded at.
 *
 * This is the check whose absence let a market pay out collateral it never held. A trade at a
 * price far from the mark opens a position that is already underwater by the difference: buy
 * at 120 while the mark is 100 and ten contracts are down 200 before the block ends. Margin
 * sized on the fill price does not cover that, and when the counterparty later closes at a
 * profit the market has to pay from money nobody posted.
 *
 * Requiring equity-at-mark to meet the initial margin requirement closes it: nobody can open a
 * position whose immediate mark-to-market loss exceeds what they put up. Failing here rolls
 * the whole operation back atomically.
 */
void require_margin_at_mark( database& d, const futures_market_object& market,
                             account_id_type account )
{
   const auto& idx = d.get_index_type<futures_position_index>().indices().get<by_market_owner>();
   auto itr = idx.find( boost::make_tuple( market.get_id(), account ) );
   if( itr == idx.end() )
      return;   // the fill closed the position outright

   const auto live = live_mark_price( d, market );
   FC_ASSERT( live.valid(), "Market has no live mark price" );
   const share_type mark = *live;

   const share_type equity   = itr->equity( mark );
   const share_type required = futures_margin_required( itr->abs_size(), mark,
                                                        market.options.initial_margin_ratio );
   if( equity >= required )
      return;

   // Trading away from the mark is allowed, but it has to be funded. The order reserved margin
   // against its own limit price, which does not cover the immediate mark-to-market loss, so
   // the difference comes from the trader's balance. adjust_balance throws if it is not there,
   // rolling the whole operation back -- which is the correct outcome, because the alternative
   // is a position the market cannot pay out on.
   const share_type shortfall = required - equity;
   d.adjust_balance( account, -asset( shortfall, market.collateral_asset ) );
   d.modify( *itr, [&shortfall]( futures_position_object& p ) { p.margin += shortfall; } );
}

/// Emits the virtual operation that records a fill in account history.
void record_fill( database& d, const futures_market_object& market, account_id_type account,
                  bool is_long, share_type n, share_type price, bool is_maker )
{
   futures_fill_operation vop( market.get_id(), account, is_long, n, price, is_maker );
   vop.fee = asset( 0, market.collateral_asset );
   d.push_applied_operation( vop );
}

} // namespace

share_type futures_ppm_of( share_type amount, uint32_t ppm )
{
   // Rounded up, so a cap or a fee is never silently zero for small inputs.
   const fc::uint128_t v = ( fc::uint128_t( amount.value ) * ppm + 999999 ) / 1000000;
   FC_ASSERT( v <= fc::uint128_t( GRAPHENE_MAX_SHARE_SUPPLY ), "ppm result is out of range" );
   return share_type( static_cast<int64_t>( v ) );
}

share_type futures_margin_required( share_type size, share_type price, uint16_t ratio )
{
   // Round up. A requirement rounded down would let a position open slightly
   // under-collateralised, and every such shortfall is ultimately the market's to absorb.
   const fc::uint128_t notional = fc::uint128_t( size.value ) * price.value;
   const fc::uint128_t numerator = notional * ratio;
   const fc::uint128_t denom( GRAPHENE_100_PERCENT );
   const fc::uint128_t rounded = ( numerator + denom - 1 ) / denom;
   FC_ASSERT( rounded <= fc::uint128_t( GRAPHENE_MAX_SHARE_SUPPLY ),
              "Required margin is out of range" );
   return share_type( static_cast<int64_t>( rounded ) );
}

void_result futures_order_create_evaluator::do_evaluate( const futures_order_create_operation& op )
{ try {
   database& d = db();
   const auto now = d.head_block_time();

   FC_ASSERT( HARDFORK_FUTURES_PASSED( now ), "Not allowed until the futures hardfork" );

   _market = &op.market_id(d);
   FC_ASSERT( _market->is_tradable( now ),
              "Market '${s}' is not accepting orders: it is halted, settled, past expiry, or "
              "has no mark price", ("s", _market->symbol) );

   // The fee is reserved at the LIMIT price, which is the most the order can ever be charged:
   // a taker fills at the maker's price, which is never worse than its own limit. Whatever is
   // reserved and not spent goes back with the rest of the unused margin.
   _max_fee = futures_ppm_of( op.size * op.price_per_contract,
                              _market->options.taker_fee_ppm );

   _required_margin = futures_margin_required( op.size, op.price_per_contract,
                                               _market->options.initial_margin_ratio );

   // Reserved up front rather than at fill time. An order that could not be paid for if it
   // filled has no business resting on the book.
   FC_ASSERT( d.get_balance( op.owner, _market->collateral_asset ).amount
                 >= _required_margin + _max_fee,
              "Insufficient balance to reserve ${m} margin for this order",
              ("m", _required_margin) );

   return void_result();
} FC_CAPTURE_AND_RETHROW( (op) ) } // GCOVR_EXCL_LINE

/**
 * Re-observe the premium because the book just changed.
 *
 * Sampling only when the oracle publishes leaves the observation stale between publishes: a
 * book that forms and is charged for a whole interval would be weighted by whatever was in
 * force before it existed. Sampling on every book change keeps the running observation honest,
 * and costs an order placement one lookup.
 *
 * It also keeps the manipulation cost real in the other direction. A skewed quote is sampled
 * the moment it is placed and again the moment it is withdrawn, so it earns weight for exactly
 * as long as it was actually exposed to being traded against -- no more, and no less.
 */
void resample_premium( database& d, const futures_market_object& market )
{
   const auto live = live_mark_price( d, market );
   if( live.valid() )
      accumulate_premium( d, market, *live );
}

object_id_type futures_order_create_evaluator::do_apply( const futures_order_create_operation& op )
{ try {
   database& d = db();
   const futures_market_object& market = *_market;

   d.adjust_balance( op.owner, -asset( _required_margin + _max_fee, market.collateral_asset ) );

   share_type remaining = op.size;
   share_type reserved  = _required_margin;
   share_type fee_left  = _max_fee;
   share_type oi_delta  = 0;

   const auto& book = d.get_index_type<futures_order_index>().indices().get<by_market_book>();

   // A taker long lifts the lowest ask; a taker short hits the highest bid. Both fill at the
   // MAKER's price, which is what makes resting on the book worthwhile and stops a taker from
   // choosing its own fill price.
   while( remaining > 0 )
   {
      const futures_order_object* best = nullptr;
      if( op.is_long )
      {
         auto itr = book.lower_bound( boost::make_tuple( market.get_id(), false ) );
         const auto end = book.upper_bound( boost::make_tuple( market.get_id(), false ) );
         if( itr != end && itr->price_per_contract <= op.price_per_contract )
            best = &(*itr);
      }
      else
      {
         auto end = book.upper_bound( boost::make_tuple( market.get_id(), true ) );
         const auto begin = book.lower_bound( boost::make_tuple( market.get_id(), true ) );
         if( end != begin )
         {
            --end;
            if( end->price_per_contract >= op.price_per_contract )
            {
               // Stepping back reaches the highest bid price, but it walks the id tiebreaker
               // backwards too, and ids increase with time -- so it lands on the NEWEST order
               // at that price rather than the oldest. The ask side has no such problem: it is
               // scanned forward, where both price and id ascend, which is what price-time
               // priority means. Taken as it stood, a resting bid could be jumped indefinitely
               // by later bids at its own price.
               //
               // Seek forward to the first order at that price instead: within one price the
               // key orders by id, so lower_bound lands on the one that has been waiting
               // longest.
               const auto oldest = book.lower_bound( boost::make_tuple(
                        market.get_id(), true, end->price_per_contract ) );
               best = &(*oldest);
            }
         }
      }
      if( nullptr == best )
         break;

      // Self-matching would let one account pay itself the spread and, worse, net to a
      // position built from two halves of its own order.
      FC_ASSERT( best->owner != op.owner,
                 "This order would match your own resting order; cancel it first" );

      const share_type fill_price = best->price_per_contract;
      const share_type n = std::min( remaining, best->size );

      // Maker/taker. The taker pays on the notional it just lifted; the maker is paid for
      // having been there to lift, and the remainder capitalises the insurance fund. Charged
      // per fill rather than on the whole order, because each fill has its own price.
      const share_type notional   = n * fill_price;
      const share_type taker_fee  = futures_ppm_of( notional, market.options.taker_fee_ppm );
      const share_type maker_paid = futures_ppm_of( notional, market.options.maker_rebate_ppm );
      // Rounding up both could put the rebate above the fee on a small fill, which would pay
      // the maker out of the fund. validate() bounds the rates; this bounds the rounding.
      const share_type rebate = std::min( maker_paid, taker_fee );

      // The maker's margin was reserved at its own price when it was placed.
      const share_type maker_margin = futures_margin_required(
            n, fill_price, market.options.initial_margin_ratio );
      const share_type maker_share = std::min( maker_margin, best->deferred_margin );

      // The taker reserved at its own limit price, which is not the fill price.
      //
      // A long filling below its bid needs less than it reserved, and the excess simply stays
      // reserved for the rest of the order. A SHORT filling above its ask needs MORE: its
      // notional is size x fill_price, and filling higher is precisely the good case for a
      // seller. Reserving at the limit price alone would then open the position below the
      // initial margin requirement -- which is how a market ends up holding positions it never
      // agreed to hold. The shortfall is taken from the taker's balance instead; if it cannot
      // be paid, adjust_balance throws and the whole operation is rolled back atomically,
      // which is the right outcome: better to reject the order than to open it undercollateralised.
      const share_type taker_margin = futures_margin_required(
            n, fill_price, market.options.initial_margin_ratio );
      const share_type taker_share = std::min( taker_margin, reserved );
      reserved -= taker_share;
      const share_type taker_shortfall = taker_margin - taker_share;
      if( taker_shortfall > 0 )
         d.adjust_balance( op.owner, -asset( taker_shortfall, market.collateral_asset ) );

      // Take the fee from what was reserved for it, pay the maker, and put the rest in the
      // fund. Bounded by fee_left so rounding across many fills can never overdraw the reserve.
      const share_type charged = std::min( taker_fee, fee_left );
      const share_type to_maker = std::min( rebate, charged );
      fee_left -= charged;

      const account_id_type maker_account = best->owner;
      const share_type maker_remaining = best->size - n;
      const share_type maker_left_over  = best->deferred_margin - maker_share;

      if( maker_remaining > 0 )
         d.modify( *best, [&]( futures_order_object& o ) {
            o.size            = maker_remaining;
            o.deferred_margin = maker_left_over;
         } );
      else
      {
         // Any reservation the maker no longer needs goes straight back; it was never spent.
         if( maker_left_over > 0 )
            d.adjust_balance( maker_account, asset( maker_left_over, market.collateral_asset ) );
         d.remove( *best );
      }

      oi_delta += apply_fill( d, market, maker_account, !op.is_long, n, fill_price, maker_share );
      oi_delta += apply_fill( d, market, op.owner,       op.is_long, n, fill_price,
                              taker_share + taker_shortfall );

      // Both sides, every fill. A maker resting far from the mark is exposed to exactly the
      // same problem as a taker crossing to it.
      require_margin_at_mark( d, market, maker_account );
      require_margin_at_mark( d, market, op.owner );

      record_fill( d, market, maker_account, !op.is_long, n, fill_price, true );
      record_fill( d, market, op.owner,       op.is_long, n, fill_price, false );

      // Settle the fee. It was already taken from the taker's balance with the margin, so
      // only the outward halves move here: the rebate to the maker, the rest to the fund.
      if( to_maker > 0 )
         d.adjust_balance( maker_account, asset( to_maker, market.collateral_asset ) );
      const share_type to_fund = charged - to_maker;
      if( to_fund > 0 )
         d.modify( market, [&to_fund]( futures_market_object& m )
                           { m.insurance_fund += to_fund; } );

      remaining -= n;
   }

   // Open interest counts contracts, and each contract has a long and a short, so the two
   // sides' deltas are summed and halved.
   if( oi_delta != 0 )
      d.modify( market, [&oi_delta]( futures_market_object& m ) {
         m.open_interest += oi_delta / 2;
      } );

   if( remaining > 0 && !op.fill_or_kill )
   {
      const auto& order = d.create<futures_order_object>( [&]( futures_order_object& o ) {
         o.owner              = op.owner;
         o.market_id          = market.get_id();
         o.is_long            = op.is_long;
         o.price_per_contract = op.price_per_contract;
         o.size               = remaining;
         o.deferred_margin    = reserved;
      } );
      // The resting remainder will be a MAKER when it fills, and makers are not charged, so
      // the unspent fee reserve is returned rather than held against an order that cannot owe
      // it. Crossing again later reserves afresh.
      if( fee_left > 0 )
         d.adjust_balance( op.owner, asset( fee_left, market.collateral_asset ) );
      // A new resting order changes the book, and so the premium in force from now on.
      resample_premium( d, market );
      return order.id;
   }

   // Fully filled, or killed. Whatever margin was reserved and not used goes back, and with
   // it any of the fee reserve that was never charged -- the reserve was taken at the limit
   // price, and a taker that filled better than its limit owes less than that.
   if( reserved + fee_left > 0 )
      d.adjust_balance( op.owner, asset( reserved + fee_left, market.collateral_asset ) );

   // Fills consume resting orders, which moves the book just as surely as adding one.
   resample_premium( d, market );

   return object_id_type();
} FC_CAPTURE_AND_RETHROW( (op) ) } // GCOVR_EXCL_LINE

void_result futures_order_cancel_evaluator::do_evaluate( const futures_order_cancel_operation& op )
{ try {
   const database& d = db();

   FC_ASSERT( HARDFORK_FUTURES_PASSED( d.head_block_time() ),
              "Not allowed until the futures hardfork" );

   _order = &op.order_id(d);
   FC_ASSERT( _order->owner == op.owner, "Only the order's owner may cancel it" );

   return void_result();
} FC_CAPTURE_AND_RETHROW( (op) ) } // GCOVR_EXCL_LINE

void_result futures_order_cancel_evaluator::do_apply( const futures_order_cancel_operation& op ) const
{ try {
   database& d = db();
   const futures_market_object& market = _order->market_id(d);

   if( _order->deferred_margin > 0 )
      d.adjust_balance( _order->owner,
                        asset( _order->deferred_margin, market.collateral_asset ) );
   d.remove( *_order );

   // The book just changed, so the standing observation is out of date.
   resample_premium( d, market );

   return void_result();
} FC_CAPTURE_AND_RETHROW( (op) ) } // GCOVR_EXCL_LINE


void_result futures_position_adjust_margin_evaluator::do_evaluate(
      const futures_position_adjust_margin_operation& op )
{ try {
   const database& d = db();

   FC_ASSERT( HARDFORK_FUTURES_PASSED( d.head_block_time() ),
              "Not allowed until the futures hardfork" );

   _position = &op.position_id(d);
   FC_ASSERT( _position->owner == op.owner, "Only the position's owner may adjust its margin" );

   if( op.delta < 0 )
   {
      const futures_market_object& market = _position->market_id(d);
      const auto live = live_mark_price( d, market );
      FC_ASSERT( live.valid(),
                 "Margin cannot be withdrawn while the market has no live mark price to "
                 "measure the requirement against" );
      const share_type mark = *live;
      const share_type required = futures_margin_required( _position->abs_size(), mark,
                                                    market.options.initial_margin_ratio );
      // Measured against the INITIAL requirement, not the maintenance one: withdrawing down to
      // the liquidation threshold would leave a position one tick from being taken away.
      //
      // And including accrued funding: do_apply settles it immediately afterwards, so a check
      // without it would let the owner withdraw margin they already owe.
      const share_type equity =
            _position->equity_after_funding( mark, market.cumulative_funding );
      FC_ASSERT( equity + op.delta >= required,
                 "Withdrawing ${w} would leave equity ${e} against an initial margin "
                 "requirement of ${r}",
                 ("w", -op.delta)("e", equity + op.delta)("r", required) );
   }

   return void_result();
} FC_CAPTURE_AND_RETHROW( (op) ) } // GCOVR_EXCL_LINE

void_result futures_position_adjust_margin_evaluator::do_apply(
      const futures_position_adjust_margin_operation& op ) const
{ try {
   database& d = db();
   const futures_market_object& market = _position->market_id(d);

   // Charge whatever funding has accrued first, so the margin being adjusted is the real
   // current figure rather than one that silently owes the market money.
   apply_funding( d, market, *_position );

   // Negative delta pays out, positive takes in; adjust_balance throws if the account cannot
   // fund an addition, rolling the operation back.
   d.adjust_balance( op.owner, asset( -op.delta, market.collateral_asset ) );
   d.modify( *_position, [&op]( futures_position_object& p ) { p.margin += op.delta; } );

   return void_result();
} FC_CAPTURE_AND_RETHROW( (op) ) } // GCOVR_EXCL_LINE

/**
 * Hand `size` contracts (signed) to `who`, carrying `entry_value` and `margin`.
 *
 * Creates a position or merges into the one they already hold, since positions are unique per
 * (market, owner). Summing entry_value is exact: PnL of the merged position is
 * (s1+s2)*mark - (e1+e2), which is PnL1 + PnL2.
 *
 * Open interest counts contracts on the long side, so merging like signs changes nothing while
 * merging opposite signs nets them off and genuinely retires contracts.
 */
void give_position( database& d, const futures_market_object& market, account_id_type who,
                    share_type size, share_type entry_value, share_type margin )
{
   const auto& idx = d.get_index_type<futures_position_index>().indices().get<by_market_owner>();
   const auto existing = idx.find( boost::make_tuple( market.get_id(), who ) );

   if( existing == idx.end() )
   {
      d.create<futures_position_object>( [&]( futures_position_object& p ) {
         p.owner                  = who;
         p.market_id              = market.get_id();
         p.size                   = size;
         p.entry_value            = entry_value;
         p.margin                 = margin;
         p.last_cumulative_funding = market.cumulative_funding;
      } );
      return;
   }

   // Bring their own position up to date first, so both carry the same funding index and the
   // merged margin means one thing rather than two.
   apply_funding( d, market, *existing );

   const share_type merged_size  = existing->size + size;
   const share_type before_long  = std::max( existing->size, share_type( 0 ) )
                                 + std::max( size, share_type( 0 ) );
   const share_type after_long   = std::max( merged_size, share_type( 0 ) );
   const share_type merged_margin = existing->margin + margin;
   const share_type merged_entry  = existing->entry_value + entry_value;

   if( 0 == merged_size.value )
   {
      // The two sides cancelled exactly. There is no position left to hold, so the collateral
      // goes back rather than sitting in an empty one.
      if( merged_margin > 0 )
         d.adjust_balance( who, asset( merged_margin, market.collateral_asset ) );
      d.remove( *existing );
   }
   else
   {
      d.modify( *existing, [&]( futures_position_object& p ) {
         p.size        = merged_size;
         p.entry_value = merged_entry;
         p.margin      = merged_margin;
      } );
   }

   if( after_long != before_long )
      d.modify( market, [&before_long, &after_long]( futures_market_object& m ) {
         m.open_interest += after_long - before_long;
      } );
}

/**
 * The smallest number of contracts that has to leave a position to make what remains healthy.
 *
 * The owner keeps everything except the penalty charged on the part being taken, so the
 * question is the smallest t with
 *
 *     margin - penalty(t)  >=  initial_requirement(size - t)
 *
 * Note this only has a solution because the liquidation penalty is strictly below the initial
 * margin ratio: as t grows the left side falls at the penalty rate while the right falls at the
 * initial-margin rate, so the gap closes. Allocating margin PROPORTIONALLY instead would not
 * work at all -- margin*r/size >= r*mark*imr/100% reduces to margin/size >= mark*imr/100%,
 * which is independent of r, so no partial size would ever fix an unhealthy position.
 *
 * Solved by bisection rather than the closed form: the predicate is monotone in t, and every
 * term is a rounded-up integer, so bisection lands on the exact boundary without having to
 * reason about which way three separate roundings push it.
 */
share_type minimum_liquidation_size( const futures_market_object& market, share_type margin,
                                     share_type abs_size, share_type mark )
{
   const auto healthy_after = [&]( share_type t ) {
      const share_type kept = margin - futures_margin_required(
            t, mark, market.options.liquidation_penalty_ratio );
      return kept >= futures_margin_required( abs_size - t, mark,
                                              market.options.initial_margin_ratio );
   };

   if( healthy_after( 0 ) )
      return 0;              // not actually under water; caller decides what that means
   if( !healthy_after( abs_size ) )
      return abs_size;       // nothing short of the whole position restores it

   int64_t lo = 0, hi = abs_size.value;   // healthy_after(hi) is true, healthy_after(lo) false
   while( hi - lo > 1 )
   {
      const int64_t midpoint = lo + ( hi - lo ) / 2;
      if( healthy_after( share_type( midpoint ) ) )
         hi = midpoint;
      else
         lo = midpoint;
   }
   return share_type( hi );
}

/**
 * Spread an uncovered bankruptcy across the traders who profited from it.
 *
 * When a position is worth less than nothing, someone has to absorb the difference. The
 * insurance fund is the first line and usually enough. When it is not, the previous behaviour
 * charged the remainder to whoever called the liquidation -- which reads as fair and is
 * self-defeating: nobody volunteers to buy a loss, so the bankrupt position is never
 * liquidated at all and simply sits there, growing, while the market pretends it is solvent.
 *
 * The loss is therefore taken from the positions on the other side, in order of how much they
 * have gained, which is what an auto-deleveraging or socialised-loss mechanism does on any
 * venue that has one. Note what it is NOT: this does not close anyone's position. Closing
 * would need a counterparty to close against, and positions here are transferred rather than
 * matched off, so a haircut to margin is the coherent form in this design.
 *
 * Ranked by profit so the traders who gained most from the move that bankrupted the position
 * are the ones who give some of it back, and nobody is taken below their unrealised gain.
 *
 * @return how much was actually recovered, which may be less than asked for.
 */
share_type socialise_deficit( database& d, const futures_market_object& market,
                              share_type bankrupt_sign, share_type amount )
{
   if( amount <= 0 || !market.mark_price.valid() )
      return 0;
   const share_type mark = *market.mark_price;

   struct winner { futures_position_id_type id; share_type profit; };
   vector<winner> winners;

   const auto& idx = d.get_index_type<futures_position_index>().indices().get<by_market_owner>();
   auto itr = idx.lower_bound( boost::make_tuple( market.get_id() ) );
   const auto end = idx.upper_bound( boost::make_tuple( market.get_id() ) );
   for( ; itr != end; ++itr )
   {
      // Only the other side of the trade. A position on the same side lost too.
      const share_type side = itr->size > 0 ? share_type( 1 ) : share_type( -1 );
      if( side == bankrupt_sign )
         continue;
      const share_type profit = itr->unrealized_pnl( mark );
      if( profit > 0 )
         winners.push_back( { itr->get_id(), profit } );
   }

   // Largest gain first. Sorted by id as a tiebreak so the outcome cannot depend on index
   // iteration order, which consensus must never do.
   std::sort( winners.begin(), winners.end(),
              []( const winner& a, const winner& b )
              {
                 if( a.profit != b.profit ) return a.profit > b.profit;
                 return a.id < b.id;
              } );

   share_type recovered = 0;
   for( const auto& w : winners )
   {
      if( recovered >= amount )
         break;
      const share_type take = std::min( amount - recovered, w.profit );
      if( take <= 0 )
         continue;
      d.modify( w.id(d), [&take]( futures_position_object& p ) { p.margin -= take; } );
      recovered += take;
   }
   return recovered;
}

void_result futures_liquidate_evaluator::do_evaluate( const futures_liquidate_operation& op )
{ try {
   const database& d = db();

   FC_ASSERT( HARDFORK_FUTURES_PASSED( d.head_block_time() ),
              "Not allowed until the futures hardfork" );

   _position = &op.position_id(d);
   _market   = &_position->market_id(d);

   FC_ASSERT( live_mark_price( d, *_market ).valid(),
              "Cannot liquidate while the market has no live mark price: risk would be "
              "assessed against a price nobody is currently asserting" );

   const share_type mark = *_market->mark_price;
   const share_type maintenance = futures_margin_required(
         _position->abs_size(), mark, _market->options.maintenance_margin_ratio );

   // Accrued funding MUST count here. do_apply settles it immediately afterwards through
   // settle_to_mark; if this eligibility check left it out, a position that is under water
   // through funding alone could never be liquidated. It could pile up debt without limit, and
   // the loss would reach the fund or the counterparty only the next time the position was
   // touched -- at a point where nobody can step in any more.
   const share_type equity = _position->equity_after_funding( mark, _market->cumulative_funding );
   FC_ASSERT( equity < maintenance,
              "Position is not liquidatable: equity ${e} still meets the maintenance "
              "requirement of ${r} at mark ${m}",
              ("e", equity)("r", maintenance)("m", mark) );

   FC_ASSERT( _position->owner != op.liquidator,
              "An account cannot liquidate its own position; add margin instead" );

   return void_result();
} FC_CAPTURE_AND_RETHROW( (op) ) } // GCOVR_EXCL_LINE

void_result futures_liquidate_evaluator::do_apply( const futures_liquidate_operation& op ) const
{ try {
   database& d = db();
   const futures_market_object& market = *_market;
   const share_type mark = *market.mark_price;

   // Settle through the pool first, so the stored margin IS the position's equity and
   // everything below is plain arithmetic on one number that actually exists.
   settle_to_mark( d, market, *_position );

   const share_type margin_at_mark = _position->margin;
   const share_type abs_size = _position->abs_size();
   const share_type sign = _position->size > 0 ? share_type( 1 ) : share_type( -1 );
   const account_id_type old_owner = _position->owner;

   // How much of the position actually has to go.
   //
   // Taking all of it whenever a position dips below maintenance is the crude version: it costs
   // the owner their whole position and charges the penalty on the whole notional, when a
   // fraction would have restored them to a full initial margin. Every venue that liquidates
   // for a living takes the minimum that does the job.
   //
   // Below zero margin the position is worth less than nothing, no fraction of it is healthy,
   // and the whole thing has to be taken over.
   const share_type take = margin_at_mark < 0
                         ? abs_size
                         : minimum_liquidation_size( market, margin_at_mark, abs_size, mark );

   if( take < abs_size && take > 0 )
   {
      // --- partial ---------------------------------------------------------------------
      // The penalty falls on the part being taken, and is what the liquidator earns for taking
      // it. The owner keeps the rest of their margin along with the rest of their position, so
      // collateral is conserved: (margin - penalty) + required + (paid) == margin.
      const share_type part_penalty = futures_margin_required(
            take, mark, market.options.liquidation_penalty_ratio );
      const share_type part_required = futures_margin_required(
            take, mark, market.options.initial_margin_ratio );
      const share_type liquidator_pays = part_required > part_penalty
                                       ? part_required - part_penalty : share_type( 0 );

      d.adjust_balance( op.liquidator, -asset( liquidator_pays, market.collateral_asset ) );

      // Every contract carries the mark as its entry after settling, so a slice of the position
      // carries exactly its share of entry_value.
      const share_type kept_size = abs_size - take;
      d.modify( *_position, [&]( futures_position_object& p ) {
         p.size        = sign * kept_size;
         p.entry_value = sign * kept_size * mark;
         p.margin      = margin_at_mark - part_penalty;
      } );

      give_position( d, market, op.liquidator, sign * take, sign * take * mark, part_required );
      return void_result();
   }

   // --- whole position -----------------------------------------------------------------
   share_type margin = margin_at_mark;

   // A gap can leave a position worth less than nothing. The insurance fund exists for exactly
   // this; if it cannot cover the whole deficit the remainder falls to the liquidator, who can
   // see it before choosing to call.
   share_type from_fund = 0;
   share_type socialised = 0;
   if( margin < 0 )
   {
      const share_type deficit = -margin;
      from_fund = market.insurance_fund > 0 ? std::min( deficit, market.insurance_fund )
                                             : share_type( 0 );
      // Whatever the fund cannot absorb comes from the traders on the other side, ranked by
      // how much they gained. Charging it to the liquidator instead -- which is what happened
      // before -- means no rational account ever calls this, and the bankrupt position stays
      // open indefinitely.
      const share_type remaining = deficit - from_fund;
      if( remaining > 0 )
         socialised = socialise_deficit( d, market, sign, remaining );
      margin = 0;
   }

   const share_type penalty = std::min(
         futures_margin_required( abs_size, mark, market.options.liquidation_penalty_ratio ),
         margin );
   const share_type owner_payout = margin - penalty;   // >= 0 by the min above
   const share_type retained     = penalty;            // stays with the position

   const share_type required = futures_margin_required( abs_size, mark,
                                                  market.options.initial_margin_ratio );
   // The liquidator tops the position up to a full initial margin and keeps the penalty, which
   // is what makes calling this worth doing.
   // The liquidator covers only what neither the fund nor the other side could absorb, which
   // is the case where the market as a whole is short of collateral and someone has to notice.
   const share_type top_up = ( required > retained ? required - retained : share_type(0) )
                           + ( margin_at_mark < 0
                               ? -margin_at_mark - from_fund - socialised : share_type(0) );

   d.adjust_balance( op.liquidator, -asset( top_up, market.collateral_asset ) );
   if( owner_payout > 0 )
      d.adjust_balance( old_owner, asset( owner_payout, market.collateral_asset ) );

   if( from_fund > 0 )
      d.modify( market, [&from_fund]( futures_market_object& m )
                        { m.insurance_fund -= from_fund; } );

   // NOT plus from_fund and socialised. Those cover the hole -- they are what takes the
   // position's margin from -deficit up to zero -- and adding them on top as well credited the
   // new position with money that had already been spent, creating exactly from_fund +
   // socialised units out of nothing on every bankrupt liquidation. The supply invariant
   // caught it; no existing test drove a position below zero margin, so it had never run.
   const share_type acquired_margin = retained + top_up;

   // Positions are unique per (market, owner). Reassign in place when the liquidator holds
   // nothing here, so the position keeps its id; otherwise merge into what they already have.
   const auto& pos_idx = d.get_index_type<futures_position_index>().indices()
                          .get<by_market_owner>();
   const auto existing = pos_idx.find( boost::make_tuple( market.get_id(), op.liquidator ) );
   if( existing == pos_idx.end() )
   {
      // entry_value is already at the mark after settling, so the position changes hands with
      // no inherited unrealised PnL.
      d.modify( *_position, [&]( futures_position_object& p ) {
         p.owner  = op.liquidator;
         p.margin = acquired_margin;
      } );
      return void_result();
   }

   const share_type taken_size  = _position->size;
   const share_type taken_entry = _position->entry_value;
   d.remove( *_position );
   give_position( d, market, op.liquidator, taken_size, taken_entry, acquired_margin );

   return void_result();
} FC_CAPTURE_AND_RETHROW( (op) ) } // GCOVR_EXCL_LINE


void accrue_futures_funding( database& d, const futures_market_object& market )
{
   if( !market.is_perpetual() || market.is_settled )
      return;
   // Funding transfers value between longs and shorts. Charging it off a frozen mark would
   // move real collateral on the strength of a price nobody is still asserting.
   const auto live = live_mark_price( d, market );
   if( !live.valid() )
      return;

   const auto now = d.head_block_time();
   if( ( now - market.last_funding_time ).to_seconds() < int64_t( market.options.funding_interval_sec ) )
      return;

   const share_type mark = *live;

   // The time-weighted average premium observed across this interval, not a reading taken at
   // the end of it. Each sample was already clamped to the rate cap, so the average is too;
   // the clamp below is kept because the cap is a property of the rate, not of the sampler.
   //
   // The cap is computed in ppm directly rather than by converting to GRAPHENE_100_PERCENT
   // units first: that conversion is an integer divide by 100, so every cap below 100 ppm
   // collapsed to zero and silently switched funding off altogether.
   share_type per_contract = market.premium_avg;
   const share_type cap = futures_ppm_of( mark, market.options.max_funding_rate_ppm );
   if( per_contract > cap )  per_contract = cap;
   if( per_contract < -cap ) per_contract = -cap;

   d.modify( market, [&per_contract, now]( futures_market_object& m ) {
      m.cumulative_funding += per_contract;
      m.last_funding_time   = now;
      // Start the next interval from a clean average, anchored at now.
      m.premium_avg         = 0;
      m.last_premium_time   = now;
   } );
}

void_result futures_settle_evaluator::do_evaluate( const futures_settle_operation& op )
{ try {
   const database& d = db();
   const auto now = d.head_block_time();

   FC_ASSERT( HARDFORK_FUTURES_PASSED( now ), "Not allowed until the futures hardfork" );

   _market = &op.market_id(d);
   FC_ASSERT( !_market->is_perpetual(),
              "A perpetual contract never expires and cannot be settled" );
   FC_ASSERT( now >= *_market->expiry,
              "Contract '${s}' does not expire until ${e}",
              ("s", _market->symbol)("e", *_market->expiry) );
   // Settlement fixes one price for everyone in the market and cannot be revisited, so a
   // stale oracle must block it rather than be snapshotted. Once settled the price is already
   // fixed and the oracle is irrelevant.
   FC_ASSERT( _market->is_settled || live_mark_price( d, *_market ).valid(),
              "Cannot settle: the oracle has no live value to settle against" );

   if( op.position_id.valid() )
   {
      _position = &(*op.position_id)(d);
      FC_ASSERT( _position->market_id == op.market_id,
                 "Position ${p} does not belong to market '${s}'",
                 ("p", *op.position_id)("s", _market->symbol) );
   }

   return void_result();
} FC_CAPTURE_AND_RETHROW( (op) ) } // GCOVR_EXCL_LINE

void_result futures_settle_evaluator::do_apply( const futures_settle_operation& op ) const
{ try {
   database& d = db();

   // The price is already fixed: update_futures_mark_price sets it on the first oracle
   // publication at or after expiry. Setting it again here would hand back exactly the
   // choice that was taken away there.
   //
   // A market can have expired with no publication since -- then it is not settled yet, and
   // do_evaluate already rejects that case through live_mark_price.
   FC_ASSERT( _market->is_settled,
              "Cannot settle yet: the settlement price is fixed by the first oracle "
              "publication at or after expiry, and none has arrived" );

   if( nullptr == _position )
      return void_result();

   const share_type final_price = *_market->settlement_price;

   apply_funding( d, *_market, *_position );

   // Close at the settled price: exactly the flat-position payout, margin - entry_value, with
   // the difference passing through the pool as any other close does.
   const share_type final_entry = _position->size * final_price;
   const share_type realised    = final_entry - _position->entry_value;
   const share_type payout      = _position->margin + realised;
   const share_type abs_size    = _position->abs_size();
   const account_id_type owner  = _position->owner;

   // Open interest counts contracts, and every contract is one long and one short, so it is
   // tracked as the long side alone. Decrementing for both sides of a settled pair would take
   // it negative -- as it did, to -10, before this condition existed.
   const share_type oi_delta = _position->size > 0 ? abs_size : share_type( 0 );
   d.modify( *_market, [&realised, &oi_delta]( futures_market_object& m ) {
      m.insurance_fund -= realised;
      m.open_interest  -= oi_delta;
   } );

   if( payout > 0 )
      d.adjust_balance( owner, asset( payout, _market->collateral_asset ) );

   d.remove( *_position );

   return void_result();
} FC_CAPTURE_AND_RETHROW( (op) ) } // GCOVR_EXCL_LINE

void_result futures_market_update_evaluator::do_evaluate(
      const futures_market_update_operation& op )
{ try {
   const database& d = db();

   FC_ASSERT( HARDFORK_FUTURES_PASSED( d.head_block_time() ),
              "Not allowed until the futures hardfork" );

   _market = &op.market_id(d);
   FC_ASSERT( _market->owner == op.owner, "Only the market's owner may update it" );
   FC_ASSERT( !_market->is_settled, "A settled market cannot be updated" );

   if( op.new_options.valid() && _market->open_interest > 0 )
   {
      // Raising the maintenance requirement under open positions would liquidate people who
      // were within the rules when they opened. Loosening it is allowed: it can only ever make
      // an existing position safer.
      FC_ASSERT( op.new_options->maintenance_margin_ratio
                 <= _market->options.maintenance_margin_ratio,
                 "Maintenance margin ratio cannot be raised while ${oi} contracts are open: "
                 "positions opened under the old rule would become liquidatable",
                 ("oi", _market->open_interest) );
   }

   return void_result();
} FC_CAPTURE_AND_RETHROW( (op) ) } // GCOVR_EXCL_LINE

void_result futures_market_update_evaluator::do_apply(
      const futures_market_update_operation& op ) const
{ try {
   db().modify( *_market, [&op]( futures_market_object& m ) {
      if( op.new_description.valid() )
         m.description = *op.new_description;
      if( op.new_options.valid() )
         m.options = *op.new_options;
   } );
   return void_result();
} FC_CAPTURE_AND_RETHROW( (op) ) } // GCOVR_EXCL_LINE

} } // graphene::chain
