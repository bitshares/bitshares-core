# StableSwap: design

A second pricing curve for liquidity pools, for pairs whose members are meant to hold the
same value — two dollar tokens, two wrappings of the same coin, a token and its bridged twin.

BitShares already has liquidity pools on the constant-product curve `x·y = k`. That curve is
correct when the relative price of the two assets is free to move: it quotes every price from
zero to infinity and never runs out of one side. It is the wrong curve for a pegged pair,
because it charges slippage for a trade whose price should not move at all. Swapping 10,000
of a million-unit pool costs about 1% on constant product; on a pegged pair that 1% is pure
loss to both the trader and the liquidity provider, for no economic reason.

## The curve

The invariant is Curve's StableSwap equation, for `n = 2`:

```
A·n^n·Σxᵢ + D = A·D·n^n + D^(n+1) / (n^n·Πxᵢ)
```

`D` replaces `k` as the stored invariant. At perfect balance `D = x + y`, so a balanced pool
of 1,000,000 each has `D = 2,000,000` — a number with a plain meaning, unlike `k`.

`A` is the amplification coefficient. It interpolates between the two curves: as `A → 0` the
equation degenerates to constant product, and as `A → ∞` it approaches a constant-sum line
where the price is fixed at 1:1 regardless of balance. Everything in between is a curve that
is flat near balance and bends towards constant product as the pool becomes lopsided.

`compute_d` solves for `D` by Newton iteration; `compute_new_y` inverts the equation for one
side given the other. Both work in 128-bit integers and never divide before multiplying,
because two nodes that rounded differently would compute different pool states from identical
inputs and fork the chain.

## What A = 100 actually buys

Measured on the pure functions, so fees and evaluator rounding do not colour the result. A
pool of 1,000,000 each, asking what 10,000 of the abundant asset returns as the pool is driven
out of balance:

| Imbalance | Return on 10,000 | Share of face |
|---|---|---|
| 10% | 9,978 | 99.8% |
| 30% | 9,927 | 99.3% |
| 50% | 9,826 | 98.3% |
| 70% | 9,510 | 95.1% |
| 80% | 9,024 | 90.2% |
| 90% | 7,693 | 76.9% |

The flat region holds across the whole realistic range and only collapses once nine tenths of
one side is gone. That is the property the curve exists to provide, and it is the number to
quote when choosing `A` for a pair: a pair that can plausibly reach 80% imbalance during a
depeg still trades within 10% of face.

`A` has no upper bound in the protocol beyond being positive. `compute_d` was fuzzed to
`uint64_t::max()` and either converges in band or refuses; it never wraps. An absurd `A` is
therefore a bad economic choice rather than a safety problem.

## Imbalanced deposits

A deposit that does not match the pool's current ratio moves the pool along its curve, and
whoever deposits that way is taking value from the existing providers unless they pay for it.
The imbalance fee is the swap fee scaled by `n / (4(n−1))`, which for `n = 2` is exactly half
the swap fee — an imbalanced deposit is economically half a swap.

Measured: depositing 300/100 into a balanced 500/500 pool yields 1,994,277 share units rather
than the 4,000,000 a proportional deposit of the same total would have earned.

## Single-sided withdrawal

Withdrawing in one asset is the mirror image and carries the same fee. It exists because a
provider who wants out of one side should not have to take the other and swap it back, paying
twice.

It is refused on a constant-product pool. There the payout has no bounded price — the curve
quotes to infinity — so a single-sided withdrawal would be a swap at whatever the curve says
at that moment, with no protection. The chain rejects it rather than pricing it:

```
Only a stable pool can pay a withdrawal in a single asset
```

The field rides in the operation's typed extension, so a withdrawal that does not use it
produces exactly the bytes it always did and old withdrawals stay valid.

## Rounding

Every rounding step must favour the pool, or a bot grinding small round trips collects the
providers' capital. Verified by grinding: 200 fee-free A→B→A cycles leave the trader 200 units
down and grow `D` from 2,000,000 to 2,000,200 — the pool gains exactly what the trader loses,
one unit per cycle, every cycle.

## What is not settled

- **`A` is a per-pool constant.** Curve ramps `A` over time so a pool can be re-tuned without
  a step change in price. Here it is fixed at creation. Changing it would need a new operation
  and a policy for who may call it.
- **No `A` upper bound.** Safe numerically, but nothing stops a pool advertising `A = 2^64`
  and behaving as a constant-sum line until it runs one side to zero.
- **Two assets only.** The equation generalises to `n` assets; the implementation does not.
- **The depeg numbers above are curve behaviour, not market behaviour.** They say what the
  pool quotes at a given imbalance, not how quickly a real depeg drives it there, nor whether
  arbitrage arrives in time. That needs someone who prices this risk professionally.

## Where it lives

| | |
|---|---|
| Curve | `libraries/chain/include/graphene/chain/stableswap.hpp` |
| Evaluators | `libraries/chain/liquidity_pool_evaluator.cpp` |
| Operation fields | `libraries/protocol/include/graphene/protocol/liquidity_pool.hpp` |
| Hardfork gate | `libraries/chain/hardfork.d/STABLESWAP.hf` |
| Tests | `tests/tests/liquidity_pool_tests.cpp` |
| Wallet | `bitshares-ui` — `PoolCreateForm` (curve + `A`), `PoolWithdrawForm` (single-sided) |
