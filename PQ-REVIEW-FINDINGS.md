# PQ_0 hardfork and StableSwap: review findings

A security and correctness review of the post-quantum (ML-DSA / ML-KEM) hardfork and
StableSwap branch, with the fixes applied on branch `pq-hardfork-review`.

Thirty-nine distinct issues were found and all are addressed. Findings are labelled 01–41,
but the labels carry gaps and sub-numbers from the order of discovery, so the highest label is
not the count. One of them, the block interval, is reverted rather than fixed, and that revert is the
maintainers' to confirm — see finding 39, which also records a correction to an earlier
mischaracterisation of it.
This document exists so the branch explains itself without reference to the conversation that
produced it.

## The one thing to understand first

Most of the serious defects are the same mistake in different places:

> **A value that depends on the serialization format was captured, or cached, at a moment
> when that format did not correspond to the item being handled.**

A block's format follows *its predecessor's* timestamp — `_push_block()` and `_apply_block()`
both read `head_block_time()` before applying, so for block N the deciding value is the
timestamp of block N−1. Every node reaches the same answer, because a node processing block N
has exactly block N−1 as its head.

That rule is correct, but only when evaluated at that moment. Ten sites evaluated it
somewhere else.

## Findings 34–37: caches frozen to the wrong format

These four share a root cause. Each holds a format-dependent value behind a cache keyed only
on "computed yet?", so the value was frozen to whichever format happened to be in effect on
the first call.

| # | Cache | A stale value means |
|---|---|---|
| 34 | `signed_block_header::_block_id` | producer and receiver record different ids for the same block |
| 35 | `signed_block::_calculated_merkle_root` | wrong merkle root; the block fails validation |
| 36 | `precomputable_transaction::_packed_size` | wrong fee calculation and per-block size accounting |
| 37 | `precomputable_transaction::_signees` | wrong recovered signer set, handed to the authority checks |

**Finding 34 made the chain unjoinable after activation.** A receiving node calls `id()` while
logging a block — after decoding it, before applying it — when chain state still reports the
pre-hardfork format. The legacy id was cached and reused by everything downstream, including
the block database. Producer and receiver then recorded different ids for a byte-for-byte
identical block, and the receiver rejected the next block as unlinkable and stopped for good.

The evidence that identified it: block 112 on two devnet nodes was **byte-for-byte identical**
(113 bytes, both in the post-quantum format) yet the two recorded different ids. Same object,
same store statement — only a stale cache does that. It also explains why the signature still
verified: `digest()` packs only `block_header`, which carries no post-quantum field, so it is
format-independent.

Each cache now records the format it was built under and recomputes when that changes.
`transaction::id()` already did the right thing by recomputing every call, which is the only
reason it never developed the same defect.

**Findings 35–37 were unreachable by testing.** `calculate_merkle_root()` returns a fixed empty
checksum before it ever consults its cache when a block has no transactions, and every block
produced across eleven devnet runs was empty. They were found by re-reading the code with the
defect shape already in hand. The reachable path is concrete: a transaction precomputed while
sitting in the pending pool before activation, then re-applied after it.

## Findings 28–33: the format captured at the wrong moment

| # | Site | Moment it captured the format |
|---|---|---|
| 28 | `block_database` reads | the reading thread's ambient format |
| 29 | witness plugin | after `generate_block()` had already advanced head |
| 30 | `node::broadcast` | on the p2p thread, where the thread-local does not reach |
| 31 | `get_item` error path | — aborted the node while building its log message |
| 32 | `get_item` packing | head far past the block being served |
| 33 | four block decode sites | on arrival, with the block applied later |

Two deserve attention beyond the sync failure.

**Finding 28** meant blocks became write-only after activation. `store()` packs under the
writing thread's format; reads happen on other threads under theirs, and every caller in that
file swallows the exception. So `get_block()` returned null for every block after activation,
the node could not serve those blocks to peers, and the startup index check mistook them for
corruption and **truncated the index file**. The fix tries both formats and lets the recorded
block id decide, which needs no on-disk change and cannot accept a wrong decode.

**Finding 31** aborted the process inside a diagnostic: `get_block_id_for_num(0)` was evaluated
while assembling the log arguments, so the node died before writing the line that said what had
gone wrong. Fixing it is what made the remaining failures diagnosable.

**Finding 33** cannot be resolved by consulting chain state at all — at decode time head
genuinely is not the block's predecessor. The shared decoder instead lets byte-exact buffer
consumption pick the format: the two formats differ in length by the empty-optional marker, so
only one can account for the buffer exactly.

## Finding 40: a new field appended to an existing operation's wire format

The most severe issue found, and the only one that breaks the chain **without any hardfork
being involved**.

The post-quantum signing keys were added straight into `FC_REFLECT` for two operations that
have existed since genesis:

    FC_REFLECT( witness_create_operation, (fee)(witness_account)(url)(block_signing_key)(block_pq_signing_key) )
    FC_REFLECT( witness_update_operation, (fee)(witness)(witness_account)(new_url)(new_signing_key)(new_pq_signing_key) )

`FC_REFLECT`'s field order is the wire order, so this appends an `optional<pq_public_key_type>`
to the serialized form of both operations, unconditionally. Every other post-quantum field in
the branch — `witness_pq_signature`, `pq_key_auths`, `pq_signatures` — is gated on the
serialization format. These two were not.

Reading any historical block containing a witness operation therefore looks for a field that
was never written, consumes a byte belonging to whatever follows, and the transaction's
`extensions` field, next along, fails on a meaningless variant tag:

    Unable to set tag '31' when the number of supported tags is 1
    Error unpacking field extensions -> transaction -> signed_block

**A node running this code cannot sync mainnet from genesis.** The failure is immediate, not
post-activation.

Fixed by giving both operations hand-written `pack`/`unpack` in `libraries/protocol/witness.cpp`
that gate the new field on `pq_format`, exactly as `authority` gates `pq_key_auths`. The fields
stay reflected, so JSON, the API and the field-name machinery still see them; only the binary
encoding is gated, and legacy bytes are byte-identical to a pre-PQ node's.

This was found by replaying real mainnet blocks. Three of 35,000 sampled blocks failed to
decode; the other 34,997, eleven devnet runs, a green 602-case suite and two full re-reviews had
all missed it. A chain built from a fresh genesis has no history to be incompatible with, so
this class of defect is invisible to synthetic testing by construction.

## Finding 41: the wallet API's arity changed under existing callers

`create_witness` and `update_witness` had gained a trailing post-quantum signing-key
parameter carrying a C++ default. That default does nothing over RPC: this wallet applies no
default arguments through its API layer, which is confirmable against an untouched method —
`transfer` with its defaulted `broadcast` omitted fails the same way:

    Assert Exception: a0 != e || optional_args: too few arguments passed to method

So every script or integration calling the documented four-parameter `update_witness` failed
outright. The same shape as finding 40 — appending to a published interface and assuming
existing callers still work — on the API surface rather than the wire format.

Fixed by restoring the original signatures and exposing the post-quantum capability under new
names, `create_witness_pq` and `update_witness_pq`. Existing callers are untouched, and the
header records why it is a separate method rather than an extra parameter.

Verified on a devnet, all four reaching the evaluator:

| call | arguments | result |
|---|---|---|
| `update_witness` | 4 | accepted (previously rejected) |
| `update_witness_pq` | 5 | accepted |
| `create_witness` | 3 | accepted |
| `create_witness_pq` | 4 | accepted |

## Finding 39: the block interval, reverted — but not for the reason first given

The branch changed `GRAPHENE_DEFAULT_BLOCK_INTERVAL` from 5 seconds to 3. An earlier draft of
this document called that unrelated to either feature and probably left-over devnet tuning.
**That was wrong**, and the correction is worth recording because it changes how the decision
should be read.

BitShares mainnet runs at 3-second blocks and always has. Measured directly from a recorded
block log of 113,292,219 blocks:

    early (2015)   gaps: 3s x270, 6s x29     (6s = one missed slot)
    2017           gaps: 3s x298
    2020           gaps: 3s x299
    recent         gaps: 3s x299

and `libraries/egenesis/genesis.json` sets `block_interval: 3`. So 5 is the Graphene framework
default, which BitShares overrode in its own genesis years ago, and setting the constant to 3
plausibly aligns the default with the chain this software actually ships for.

The revert stands anyway, on a different argument. The constant governs only chains whose
genesis omits `block_interval`. Mainnet sets it explicitly. This branch's own
`libraries/egenesis/genesis-pq.json` sets it explicitly too. The one thing that inherits the
compile-time default is the **test fixture** — which is exactly why changing the constant moves
six tests and nothing else.

Measured with the genesis timestamp aligned to 3 (so that the `init_genesis()` divisibility
assert is satisfied and alignment is not a confounder), interval 3 gives 598 of 604 passing.
The six failures are:

| test | why it fails at 3s |
|---|---|
| `block_tests/change_block_interval` | hardcoded `5u` / `10u` where the same file elsewhere derives from `parameters.block_interval` |
| `operation_tests/witness_pay_test` | budget 594 → 357, per-block pay 259 → 156 |
| `operation_tests/force_settle_test` | force-settled volume 50 → 0 |
| `settle_tests/settle_order_cancel_due_to_no_feed` (x2) | the price feed has not expired yet — lifetime in seconds spans more blocks |
| `block_tests/miss_some_blocks` | which witnesses miss their slots shifts |

Only the first is a literal. The rest are expected economic outcomes computed for a 5-second
chain: at 3 seconds a maintenance interval contains 5/3 as many blocks, so budgets, per-block
pay and feed-lifetime-versus-block-count all genuinely differ. Recomputing them is domain work,
and tuning numbers until the suite goes green would mask real regressions.

So: the change buys this branch nothing it does not already have through its own genesis, and
costs six tests that cannot be corrected mechanically. Reverted here, with `config.hpp` left
byte-identical to upstream. **Aligning the framework default with mainnet is a reasonable
change on its own merits** — it should be made deliberately, in its own right, with those five
tests' expectations recomputed for a 3-second chain.

## Verification

| Check | Result |
|---|---|
| Full test suite | 602 / 602 cases, 10,711,860 assertions, 0 failures |
| Multi-node activation (5s blocks) | producer + follower cross the hardfork, third node joins from genesis |
| Operations sweep across activation | 12 operations each side, 6 transaction-bearing blocks each side, fresh node replays all of it |
| Regression suite, against unfixed code | each case fails; verified by temporarily reverting the fixes |
| Mainnet block log replay | see below |

### Running the tests

The regression suite runs by default:

    ./tests/chain_test --run_test=pq_regression_tests

The multi-node activation test needs a devnet genesis and a witness key. It retimes the
hardfork, rebuilds, and restores the original hardfork time on exit:

    GENESIS_SRC=/path/to/genesis.json WITNESS_KEY='["<pub>","<wif>"]' \
      tests/pq_activation_devnet.sh 900

The operations sweep drives real transactions through cli_wallet on both sides of the
activation and then has a fresh node replay them, which is what exercises the merkle root,
packed size and signer recovery at the boundary:

    GENESIS_SRC=/path/to/genesis.json WITNESS_KEY='["<pub>","<wif>"]' \
      tests/pq_operations_sweep.sh 1200

The mainnet replay is skipped unless pointed at a block log. It opens the log strictly
read-only:

    BITSHARES_MAINNET_BLOCK_LOG=/path/to/witness_node_data_dir/blockchain/database/block_num_to_block \
      ./tests/chain_test --run_test=pq_regression_tests/mainnet_block_log_replay

## What is not verified

Nothing here has been run against a live network. The devnets are built from a fresh genesis,
and the mainnet check replays recorded blocks rather than participating in consensus.

## A note on how these were found

Four of these defects were invisible to any single-process test and only appeared when two real
nodes talked to each other across an activation. Three more were invisible to the devnet as
well, because every block it produced was empty, and were found by reading. Every one of them
passed the entire existing test suite before it was found.

Two of the caching defects were only reachable with blocks that carry transactions, and for a
long time every devnet run here produced empty ones. An activation test that crosses the
hardfork on an idle chain looks reassuring and proves very little; `tests/pq_operations_sweep.sh`
exists because of that.

If one practice is worth carrying forward, it is that **multi-node activation should be a
standing gate**, not a one-off — which is what `tests/pq_activation_devnet.sh` exists to make
cheap. And once the defect shape is known to be "a cache holding a format-dependent value", the
remaining instances are found by grepping for `mutable`, not by running nodes.
