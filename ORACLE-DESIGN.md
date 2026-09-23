# Oracles: design

Replacing the price feed mechanism with first-class oracles.

**Revision 2**, after review on
[bitshares-core#2866](https://github.com/bitshares/bitshares-core/pull/2866). Revision 1 priced
every oracle in two on-chain asset ids. Revision 2 prices oracles in *reference assets*, and
on-chain assets declare which reference asset they track. The code on this branch still
implements revision 1; nothing in revision 2 is implemented yet. Decisions that are not settled
are marked **Open**, and the table at the end lists every one of them with the positions so far.

## What exists today

Price data enters the chain through `asset_publish_feed_operation`
([asset_ops.hpp:462](libraries/protocol/include/graphene/protocol/asset_ops.hpp#L462)). A
publisher submits a `price_feed` for one market-issued asset; the chain keeps the latest
submission per publisher in `asset_bitasset_data_object::feeds`, and
`update_median_feeds()` ([asset_object.cpp:47](libraries/chain/asset_object.cpp#L47)) takes
the median of those still inside `feed_lifetime_sec`, provided at least `minimum_feeds`
survive.

Authorization is one of three fixed shapes
([asset_evaluator.cpp:1453](libraries/chain/asset_evaluator.cpp#L1453)): active witnesses if
the asset is `witness_fed_asset`, active committee members if `committee_fed_asset`,
otherwise the asset's own producer allow-list.

It works, and it has secured BitShares smartcoins for a decade. The limits below are about
what it *cannot express*, not about it being broken.

## Why that is not enough

**A feed is an attribute of an asset, not a thing in itself.** There is no way to publish
"BTC/USD" as a datum that several consumers reference. Every consumer needs its own asset,
its own producer set, and its own copy of the same numbers.

**The payload is a fixed struct of smartcoin parameters.** `price_feed` carries
`settlement_price`, `core_exchange_rate`, MCR, MSSR and ICR. A consumer that wants only a
price still inherits collateral-ratio machinery that means nothing to it.

**Producer sets are coarse.** Witness-fed, committee-fed, or a per-asset allow-list, with one
vote each. There is no way to say "these five exchanges, and require four of them", or to
weight a producer that has been reliable for a year above one added yesterday.

**Aggregation is median-of-latest, and nothing else.** There is no outlier handling, no bound
on how far one update may move the result, and no time dimension at all.

**There is no history.** Only the latest submission per publisher is kept, so no
time-averaged measure can be computed even in principle. History has to start being recorded
before it can be used.

### What revision 1 got wrong

Revision 1 claimed to fix the first point and did not, for the consumer that matters most. It
described an oracle as a series that several consumers reference, but binding a smartcoin
required `oracle.base_asset == asset_to_update` and `oracle.quote_asset ==
short_backing_asset`. One oracle could therefore feed exactly one smartcoin: HONEST.USD and
COMMITTEE.USD could not share a USD/BTS oracle.

Futures had the same defect from the other side. A market's contract size was an amount of
`oracle.base_asset`, so a cash-settled BTC contract needed someone to create an on-chain "BTC"
token only to name what the contract tracks.

Both follow from one choice: pricing in asset ids ties creating an oracle to creating an
asset. Revision 2 removes that choice.

## Concepts

| Term | What it is | Example |
|---|---|---|
| reference asset | A unit a price can be quoted in. On chain it is only a symbol and a precision; nobody holds it. | USD, precision 4 |
| mapping | An on-chain asset's declaration that it tracks a reference asset, at a ratio | HONEST.USD → USD at 1 : 1 |
| oracle | A price series between two reference assets, with producers and an aggregation policy | USD/BTS |
| series | One of the values an oracle publishes, declaring what it is and how it was rounded | bid, rounded down |
| consumer | Anything that reads an oracle | HONEST.USD's settlement price; a futures market's mark price |

## Reference assets

```
reference_asset_object           new protocol object type; id space assigned at merge
  symbol                         unique among reference assets, e.g. "USD"
  precision                      digits of the smallest unit, exactly like an asset's precision
  description                    free text, bounded
  creator
```

**Precision is kept, and prices stay rational.** EUR and USD naturally have 2 decimal places,
and financial systems usually carry 4. Precision gives every reference asset a smallest unit,
and in the common case it makes the conversion to an on-chain price exact with no arithmetic.
HONEST.USD has precision 4. Mapped 1 : 1 to a USD reference asset of precision 4, an oracle
value is already a HONEST.USD price. Rationals do not eliminate rounding, but they confine it
to the few places where two scales genuinely differ.

**Symbol and precision are immutable.** Every value ever published against a reference asset
is counted in its smallest unit. Changing the precision would rescale all of them silently.

**Reference assets have their own namespace.** The obvious names are already asset symbols on
mainnet: USD (1.3.121), EUR (1.3.120), BTC (1.3.103), GOLD (1.3.106) and CNY (1.3.113) are
the committee's smartcoins. A reference asset "USD" and the smartcoin "USD" are different
things, and the smartcoin maps to the reference asset. Wallets have to show the difference.

**Who may create one.** Eventually, anyone. Creation is gated by a chain parameter that the
committee controls, so that opening it later needs no hardfork. The parameter starts as
committee-only. **Open:** whether an open regime needs per-name committee approval.

**The BTS reference asset.** The core asset's issuer is `null-account` (1.2.3), so nobody can
sign an update that declares its mapping. The core asset's mapping therefore has to come from
the protocol, however the reference asset itself is created. The simplest arrangement is to
create reference asset BTS (precision 5) at the hardfork and map 1.3.0 to it 1 : 1 in the
same step. Having the committee create it later would also work, but it still needs a protocol
rule for the core asset's mapping. **Open**, low stakes.

**Fees and expiry.** **Open.** An annual fee with expiry, as for domain names, would bound
state. The difficulty is consumers: deleting a reference asset that oracles or mappings still
point at breaks every one of them. If expiry is added, a reference asset that anything still
references cannot be deleted. The proposal here is a creation fee only in the first stage.

## Mapping

An asset's issuer declares the mapping in the asset's options extension:

```
reference_mapping
  reference                      reference_asset_id_type
  asset_amount, reference_amount the ratio, both in smallest units, both > 0
```

Examples, in smallest units:

| Asset | Precision | Reference | Mapping |
|---|---|---|---|
| HONEST.USD | 4 | USD (4) | 1 : 1 |
| XBTSX.USDT | 6 | USD (4) | 100 : 1 |
| GDEX.USDT | 7 | USD (4) | 1000 : 1 |
| a 1-gram gold token | 4 | XAU, troy ounce (4) | 311035 : 10000 |

**The issuer declares it.** A mapping is a statement about the issuer's own asset, and nobody
else should be able to say "your token equals USD".

**An issuer may change it.** Consumers bear that risk as they bear every other
issuer-controlled risk: fees, permissions, whitelists, feeds, and events such as a depeg.
**Proposal:** follow the existing permission model and add an issuer permission bit, "may
change reference mapping". An issuer can renounce it permanently, so an asset whose consumers
need a fixed mapping can offer one. That costs a single flag bit.

The extension is encoded as a count followed by (field index, value) pairs, so the new field
must be appended after every existing one. Inserting it would shift the indices of fields
already on chain.

## Oracles

Two things change from revision 1. `base_asset` and `quote_asset` become `base` and `quote` of
type `reference_asset_id_type`. And a published value is a list of rationals, one per declared
series, instead of one `price`.

A value is `(base_amount, quote_amount)`: that many smallest units of the base reference asset
equal that many smallest units of the quote. Both are positive int64. Comparing two values
stays exact, by 128-bit cross-multiplication, as in revision 1.

Everything else carries over unchanged: owner, name, producers and weights, quorum, value
lifetime, both aggregation methods, the deviation filter and history.

### Series

Suggested in review: an oracle publishes a set of prices, each declaring what it is and how it
was rounded, and each consumer decides which one to use.

An oracle declares a fixed, ordered list of series when it is created:

```
series
  kind          mid | last | bid | ask
  rounding      unspecified | down | up
```

Each publish carries exactly one value per declared series, in order. The evaluator enforces
the relations a single submission must satisfy: bid ≤ ask, and a series rounded down ≤ the
same kind rounded up.

**Aggregation runs per series, over the same set of producers for every series.** This is what
keeps those relations true after aggregation. If every producer's bid ≤ ask, then the weighted
median of the bids ≤ the weighted median of the asks, provided both medians are taken over the
same producers with the same weights. Proof: for any v, every producer whose ask is ≤ v also
has a bid ≤ v, so the weight at or below v is at least as large for bids as for asks, and the
bid median is reached first. `median_over_window` inherits the property, because every history
entry satisfies it.

This constrains the outlier filter. In revision 1 each producer has one value, so excluding a
value and excluding a producer were the same thing. With several series they are not, and the
filter must exclude a producer from all series or from none. Otherwise the bid median and the ask
median are taken over different producers, and the aggregated bid can exceed the aggregated
ask. The rule: a producer is an outlier when any of its values deviates by more than
`max_deviation_ppm` from that series' median for the round. The quorum rule is unchanged.

History stores one value per series per entry, so its bound becomes
`GRAPHENE_ORACLE_MAX_HISTORY` × the series count.

A consumer names the series it reads when it binds. **Open:** the exact list of kinds, and
whether the first stage allows more than one series. Either way the wire format should carry
the list from the start, so that adding series later needs no new operation.

## Consumers

### Smartcoins

A smartcoin binds with `bitasset_options::ext::price_oracle { oracle_id, series }`. The binding
is valid when:

- `oracle.base` is the reference asset of the smartcoin's own mapping, and
- `oracle.quote` is the reference asset of the backing asset's mapping.

This is checked when the binding is made **and again when the value is read**, because a
mapping can change after binding. A binding that no longer matches yields no price, and the
smartcoin behaves as it does today when its feeds expire. It fails closed.

Many smartcoins can now bind to one oracle, so `GRAPHENE_ORACLE_MAX_SUBSCRIBERS` becomes a
bound that can actually be reached.

**Conversion.** Take an oracle value `(b, q)`, the smartcoin's mapping `(s_a, s_r)` and the
backing asset's mapping `(c_a, c_r)`. The settlement price is

```
(s_a · b · c_r) smartcoin units  =  (s_r · q · c_a) backing units
```

When both mappings are 1 : 1 in smallest units, the common case (HONEST.USD → USD,
BTS → BTS), this is the oracle value itself and involves no arithmetic. Otherwise each side
is a product of three int64 values, computed in 256 bits and reduced by their greatest common
divisor. If a side still exceeds `GRAPHENE_MAX_SHARE_SUPPLY`, both sides are scaled down, and
that is the one place where rounding happens. **Open:** the direction. The proposal is that the
conversion rounds the same way as the series the smartcoin chose: a smartcoin that reads a
series rounded against borrowers keeps that property after conversion.

### Futures

A market names an underlying reference asset, an on-chain collateral asset, and an oracle and
series. It is valid when `oracle.base` is the underlying, and `oracle.quote` is the reference
asset of the collateral's mapping. Contract size is counted in smallest units of the
underlying reference asset, so a cash-settled BTC contract needs no BTC token. The mark price
converts as above, and the underlying side needs no mapping.

### Later: the StableSwap multiplier

A StableSwap pool whose two assets are both mapped has a natural multiplier M. When both
assets map to the same reference asset, M follows from the mappings alone: XBTSX.USDT
(100 : 1) against GDEX.USDT (1000 : 1) gives M = 10, with no oracle involved. When they map
to different reference assets, an oracle between the two supplies a moving M. Fixed M is
planned as a separate PR; a moving M is not part of this work.

## Aggregation

Both methods are exact. **No floating point and no lossy arithmetic appears anywhere in
consensus aggregation.** Both methods below only ever *select* an observed value and never
compute a new one, which rules out the whole class of rounding and overflow divergence between
nodes. With series, each method runs once per series.

**`median_of_latest`** generalizes today's behaviour. Take submissions newer than
`value_lifetime_sec`. If fewer than `minimum_producers` remain, the value is absent. Otherwise
take the weighted median: the lowest value whose cumulative weight *exceeds* half the total.

With every weight equal to 1, that reduces to the element at index `size/2`, which is exactly
the element the legacy code picks (`effective_feeds.begin() + size/2` in
[asset_object.cpp:103](libraries/chain/asset_object.cpp#L103)): the upper of the two middle
values in an even-sized set. An asset that migrates from legacy feeds to an oracle with the
same producers must not see its settlement price jump because the two disagreed about which
element is "the" median.

**`median_over_window`** is the median of the *recorded aggregates* within `window_sec`,
drawn from `history`. It is a windowed median, not a time-weighted average, and deliberately
so. A real TWAP accumulates price × duration, which for a ratio type means either lossy
scaling or wide accumulators whose overflow behaviour has to match on every node forever. A
windowed median returns a price that was actually observed, so it is exact by construction,
and it still defeats a single-block spike.

## Outlier handling

`max_deviation_ppm` bounds how far a submission may sit from the round's median before it is
excluded. With series, the exclusion is per producer; see above.

The subtle case is a genuine crash, when every honest producer moves at once. A rule that
rejects every deviating submission would freeze the oracle exactly when it most needs to move.
So:

> Outliers are excluded **only while enough non-outliers remain to meet quorum.** If
> excluding them would drop the count below `minimum_producers`, nothing is excluded and the
> aggregate moves.

A minority that disagrees with the rest is filtered out; everyone moving together is believed.

## Consensus safety

- **Determinism.** Aggregation selects among observed values. The only arithmetic is the
  consumer conversion. It is exact in the common case, and elsewhere it uses 256-bit
  integers and a declared rounding direction.
- **Wire format.** None of these operations is on chain yet, so their fields can still change
  freely. After the hardfork they are append-only. Every field added to an existing extension
  is appended, never inserted.
- **Hardfork gating.** Every new operation, and every new extension field on an existing
  operation, is rejected before the hardfork, both in the evaluators and in
  `proposal_operation_hardfork_visitor`.
- **State growth.** History is a fixed ring per series. Submissions are bounded by the
  producer set, and reference assets by their creation fee and, while creation is restricted,
  by the committee.

## Staging

Revision 2 is larger than revision 1. Staging keeps the first hardfork reviewable:

1. **First hardfork:** reference assets with precision, with creation behind the chain
   parameter and the parameter starting as committee-only. The BTS reference asset, mapped to
   the core asset. Issuer mappings, with the renounceable permission bit. Oracles over
   reference assets, with the series list in the wire format. Smartcoin and futures bindings,
   and the conversion.
2. **Without a hardfork:** opening reference-asset creation, by changing the parameter.
3. **With a later hardfork, if wanted:** per-name approval, fees and expiry, a moving
   StableSwap multiplier.

## Out of scope

- **Non-price data.** The value type is a price.
- **Off-chain sourcing, signing schemes or a fetch protocol.** How a producer decides what to
  publish stays off-chain, as it is today.
- **Incentives, staking or slashing.** The owner removes a bad producer. There is no automatic
  punishment.
- **Replacing the legacy feed path.** It stays. Assets migrate individually, and the old path
  must keep producing identical results for assets that do not.

## Decisions

| # | Question | Review position | Proposal here | Status |
|---|---|---|---|---|
| 1 | Who creates reference assets | Everyone eventually; a switch that starts committee-only; a chain parameter preferred to a hardfork | Same | Settled |
| 2 | Per-name committee approval | To be decided | Not in the first stage. While creation is committee-only, every name is committee-approved anyway | Open |
| 3 | BTS reference asset at the hardfork, or by the committee | Either | At the hardfork: the core asset's issuer is `null-account`, so its mapping needs the protocol anyway | Open |
| 4 | Annual fee and expiry | Possible, with risk to consumers | Not in the first stage; a referenced reference asset is never deleted | Open |
| 5 | Who declares a mapping | The issuer | The issuer | Settled |
| 6 | Can a mapping change | Consumers bear this like other issuer risks | Yes, behind a renounceable permission bit | Open |
| 7 | Precision on reference assets | Keep it; prices stay rational | Same | Settled |
| 8 | Series with kind and rounding | Suggested in review | Same producer set across series; per-producer outlier rule | Open: which kinds, and which stage |
| 9 | A token with no off-chain meaning | Reasonable; implementation complexity is the concern | Its own reference asset, mapped 1 : 1, so the oracle type stays uniform | Open |
| 10 | Namespace | — | Separate from asset symbols | Proposal |
| 11 | Rounding direction in the conversion | Consumers choose among declared series | The conversion rounds the same way as the chosen series | Open |

## Implementation status

Revision 1 is implemented on this branch: the oracle object and index, the four operations
with hardfork gating, submissions and `median_of_latest`, history and `median_over_window`,
outlier handling, `price_oracle_id` on smartcoins, and the wallet and database API. That code
remains useful. Aggregation, history, the outlier filter and the quorum rules carry over, and
what changes is concentrated in the object's base and quote, the publish payload, and the two
binding checks. Revision 2 code waits for the open decisions above.
