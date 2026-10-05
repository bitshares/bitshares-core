# Futures: design

Margin-traded futures contracts — perpetual and dated — settled against an oracle.

Built on the oracle work, which is why that came first: a futures contract needs a mark price
that is not the settlement price of any particular smartcoin, and before oracles there was
nowhere on chain to put one. This branch therefore extends `oracles` rather than `master`.

## What BitShares has and does not have

BitShares already has collateralized leverage. A `call_order_object` is a debt position: the
borrower is short the smartcoin and long the collateral, liquidated when collateralization
falls below MCR. Margin calls, forced settlement, and an order book all exist and work.

What is missing for futures is **symmetry**. A debt position is short-only; the long side is
just holding the token, unleveraged. There is no expiry, no funding, no mark price distinct
from the feed, and no notion of a position that can be either direction with the same
machinery. A smartcoin is close to a perpetual short, but you cannot be a leveraged long.

## The core decision: integer prices

Everything below follows from one choice. **A futures market quotes price as an integer amount
of the collateral asset per contract**, not as a `price` ratio.

BitShares' `price` is a ratio of two amounts, and multiplying by one requires a division —
`asset * price` truncates through a 128-bit intermediate. That is fine for spot, where the
rounding convention is long-established, but futures PnL is computed continuously against a
moving mark and then again at liquidation and settlement. Every one of those is a rounding
opportunity, and rounding errors in a derivatives market do not merely round — they decide
whether the market is solvent.

With integer prices:

```
notional  = size × price_per_contract              exact
pnl(long) = size × (mark_price − entry_price)      exact
pnl(short) = −pnl(long)                            exactly antisymmetric
```

No division appears anywhere in position accounting. The only division in the whole design is
converting the oracle's ratio into a mark price, which happens once per mark update:

```
mark_price_per_contract = ( asset( contract_size, oracle.base_asset ) * oracle.current_value ).amount
```

That reuses `asset::operator*`, the codebase's existing rounding convention, rather than
inventing a second one.

This is not a compromise: real exchanges quote futures in a fixed unit with a fixed tick size
for exactly this reason.

### The solvency invariant, correctly stated

An earlier draft of this document claimed `Σ entry_value = 0` across a market. **That is
false**, and believing it hid a real hole. It holds only while every close is symmetric. When
one side exits against a fresh counterparty, its realised PnL leaves as cash and what remains
in `Σ entry_value` is exactly the negative of everything paid out so far. A test
(`sum_of_sizes_is_zero_but_entry_values_track_realised_pnl`) now demonstrates this rather than
asserting the comfortable version.

What is actually true:

```
Σ size                     = 0            always
Σ (size × mark − entry_value) = −Σ entry_value = total cash paid out so far
```

So conservation of value is **not** an `entry_value` identity. It is checked where it can
actually be checked: `verify_asset_supplies` accounts for every unit of collateral held in
positions, resting orders and the insurance fund, and fails if any is created or destroyed.
That check is what caught the hole below.

**The hole.** Unrealised losses are not cash until they are collected. A position opened at a
price far from the mark is underwater immediately — buy ten contracts at 120 while the mark is
100 and it is down 200 before the block ends — and if its margin was sized on the *fill price*
it does not cover that. When the counterparty later closes at a profit, the market pays from
money nobody posted. Left unfixed, the supply check showed 200 units of collateral appearing
from nowhere.

**The fix.** After every fill, on both sides, the position's equity measured at the **mark
price** must meet the initial margin requirement measured at the mark. Nobody can open a
position whose immediate mark-to-market loss exceeds what they put up, so a close can always
be paid from margin that is really there.

`entry_value` stays a running signed sum rather than an average entry price, because an average
would have to be divided out and rounded on every partial fill, with the two sides rounding
independently. It also removes division from partial closes: reducing a position moves `size`
toward zero and adjusts `entry_value` by `n × p`, realising nothing. When `size` reaches zero
the position holds `−entry_value` of realised PnL, so the payout is `margin − entry_value`
exactly. No proportional split, no dust.

### Which way rounding goes

Where rounding is unavoidable — margin requirements, fees, funding — **it always rounds in
favour of the market and against the trader**. Requirements round up, payouts round down. Dust
accrues to the market's insurance fund. A market that is short by a rounding error is
insolvent; a market that is long by one satoshi is merely slightly overfunded, and that is the
only acceptable direction for the error to go.

## Objects

```
futures_market_object              1.24.x
  owner                            creator and administrator
  symbol                           unique, e.g. "BTC-PERP"
  oracle_id                        index price source
  collateral_asset                 margin and PnL currency; must equal the oracle's quote asset
  contract_size                    units of the oracle's base asset per contract
  expiry                           optional; absent means perpetual
  initial_margin_ratio             /GRAPHENE_100_PERCENT, bounds leverage at open
  maintenance_margin_ratio         /GRAPHENE_100_PERCENT, liquidation threshold
  funding_interval_sec             perpetuals only
  max_funding_rate_ppm             per-interval cap
  --- state ---
  mark_price                       collateral per contract, from the oracle
  mark_price_time
  open_interest                    contracts, long side (= short side by construction)
  cumulative_funding               signed, collateral per contract, monotone index
  last_funding_time
  insurance_fund                   collateral held against liquidation shortfalls and dust
  settlement_price                 set once at expiry
  is_settled

futures_position_object            1.25.x
  owner, market_id
  size                             signed contracts; + long, − short
  margin                           collateral posted
  entry_value                      Σ(size × fill price) at entry, so pnl = size×mark − entry_value
  last_cumulative_funding          funding index when funding was last applied

futures_order_object               1.26.x
  owner, market_id
  is_long
  price_per_contract               integer, collateral per contract
  size                             contracts remaining
  deferred_margin                  collateral reserved while the order rests
```

## Operations

| Tag | Operation | Effect |
|---|---|---|
| 82 | `futures_market_create` | defines a contract |
| 83 | `futures_market_update` | owner adjusts margin ratios, funding parameters |
| 84 | `futures_order_create` | places a limit order; reserves margin |
| 85 | `futures_order_cancel` | cancels, returning reserved margin |
| 86 | `futures_position_adjust_margin` | add or withdraw margin on an open position |
| 87 | `futures_position_close` | close at market against resting orders |

Plus virtual operations, emitted rather than signed, so history and block explorers can see
what the chain did: `futures_fill`, `futures_liquidate`, `futures_funding`, `futures_settle`.

## Matching

A dedicated order book per market, not the spot engine. It is much simpler than the spot book:
one market, one collateral asset, integer prices, no cross-asset routing.

When a long order and a short order cross, both sides get a position at the maker's price, each
posting its own margin, and open interest rises by the matched size. A fill against an existing
opposite-side position of the same account reduces that position rather than opening a hedged
pair, so a trader cannot end up paying margin twice for a flat book.

## Risk

**Mark price, not last trade.** Liquidation is assessed against the oracle-derived mark, never
against the last traded price. A thin book is otherwise trivially pushed through someone's
liquidation level by a single trade — the classic on-chain derivatives attack.

**Liquidation** triggers when `margin + pnl < notional × maintenance_margin_ratio`. The position
is closed against the book; any shortfall is drawn from the insurance fund. If the fund cannot
cover it, the loss is socialised across profitable positions on the other side — stated here
because a design that pretends shortfalls cannot happen is a design that becomes insolvent
quietly rather than loudly.

**Funding** (perpetuals) transfers between longs and shorts once per `funding_interval_sec`,
proportional to the gap between the book's mid price and the oracle mark, capped by
`max_funding_rate_ppm`. It is applied as a monotone `cumulative_funding` index rather than by
touching every position: a position pays the difference between the current index and its own
`last_cumulative_funding` when it is next touched. Iterating every position on every funding
tick would put unbounded work into a maintenance block.

**Expiry** (dated contracts) snapshots the oracle into `settlement_price`, closes every position
at that price, and returns margin plus PnL. After settlement the market accepts no new orders.

## Staging

1. ~~`futures_market_object`, create/update, mark price tracking from the oracle, hardfork gate.~~
2. ~~Orders, matching, positions, margin.~~
3. ~~Settlement pool, mark-to-market, liquidation.~~
4. ~~Funding for perpetuals, settlement for dated contracts.~~

All four are done, in four commits, 28 tests.

Two mechanisms were added during stage 3 that this document did not originally anticipate, and
both exist because `verify_asset_supplies` failed rather than because anyone reasoned them out
in advance:

**The settlement pool.** Deducting a loser's loss from their margin and stopping there destroys
it — the offsetting gain is still unrealised in some other position. Losers pay into the pool,
winners are paid out of it, and `margin + pool` is conserved exactly. The field is
`insurance_fund`; it doubles as the buffer for liquidation shortfalls.

**Margin measured at the mark.** A position opened far from the mark is underwater immediately,
and margin sized on the fill price does not cover it. Every fill now requires equity-at-mark to
meet the initial requirement on both sides, funded from the trader's balance if the order's own
reservation falls short.

Funding is applied as a monotone `cumulative_funding` index, charged to a position the next time
it is touched, so a funding tick never walks the market. It is skipped entirely when the book is
one-sided: there is no mid, and a guessed funding rate moves real money on a made-up number.

Settlement of a dated contract is permissionless and one position at a time, for the same reason
liquidation is. The first caller after expiry snapshots the oracle into `settlement_price`, and
every caller after that closes a named position against that same fixed number.

## Out of scope

- **Cross-margin.** Every position is isolated: its own margin, its own liquidation. Cross
  margin means a single account failure can cascade across markets, and netting it correctly is
  a much larger problem than it looks.
- **Portfolio margin, options, spreads.**
- **A matching engine shared with spot.** The books stay separate.
- **Off-chain order relay.** Orders are on-chain objects, with the cost that implies.
- **Insurance fund capitalisation policy.** The fund exists and receives dust and liquidation
  penalties; who tops it up beyond that is a governance question, not a protocol one.
