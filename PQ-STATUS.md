# Post-quantum status: what is protected, what is not

Companion to [PQ-REVIEW-FINDINGS.md](PQ-REVIEW-FINDINGS.md), which covers defects found
while reviewing the PQ_0 work. This document answers a different question: **if this branch
shipped today, what would actually be protected against a quantum adversary?**

The short answer is that the mechanisms exist and are tested, but the chain is not
post-quantum safe, and none of the remaining distance can be closed by writing more code.
Every gap below is a deployment decision — a hardfork date, a committee vote, an operator
flag, or a user running a command.

## Where the code lives

The work spans **two repositories**, which matters for anyone reviewing or building it:

| | |
|---|---|
| `bitshares-core` | consensus gating, evaluators, wallet, operations, tests |
| `libraries/fc` (submodule) | the primitives — `fc/crypto/pqc.*`, `vendor/pqclean/`, and `fc::raw::pq_format` itself |

`fc::raw::pq_format` is the mechanism every hardfork gate is built on, and it lives in the
submodule.

**This branch does not compile as it stands, and that is deliberate.** The submodule points at
`bitshares/bitshares-fc` and is pinned to what the hardfork branch pins, so that nothing here
depends on a personal repository. Upstream fc contains none of the post-quantum work: no
`fc/crypto/pqc.*`, no `vendor/pqclean/`, no `fc::pq_gated`. Thirty-three files under
`libraries/` include those headers, so the build fails on a missing header rather than on
anything subtle.

**The fc side has to merge first.** The seven commits it needs sit on a branch of the fork,
rebased onto upstream fc master and conflict-free; until they are upstream, the pin cannot move
to a commit that has them, and this branch cannot be built or its tests run.

Everything recorded as verified below was measured against an fc that carries those commits. It
remains a true record of what was run, and it is not something a reviewer can reproduce from
this branch alone today.

## The asymmetry that should drive priorities

Not all of these surfaces fail the same way, and the difference is more important than the
algorithm choices.

**A signature only has to hold until its transaction confirms.** If secp256k1 breaks in
2035, nobody can retroactively forge a 2026 transfer — it is already buried under a decade
of blocks. What breaks is the *future* ability to spend, and an account can re-key at any
time before then. The exposure is real but it is recoverable, and it is bounded by how fast
accounts migrate once the threat is credible.

**A memo is encrypted once and stored forever.** Anyone can archive the chain today and
decrypt it the day secp256k1 falls. Nothing done afterwards — re-keying, migrating,
hardforking — makes a memo written today unreadable again. Harvest-now-decrypt-later is not
hypothetical here; the ciphertext is *already* published to every node and every block
explorer.

So of everything below, the memo work is the only part where delay causes permanent,
unrecoverable loss. The signature work is the part where delay causes recoverable risk.
That is why memos were built even though signatures came first.

## Surface by surface

| Surface | Mechanism | Algorithm | State |
|---|---|---|---|
| Transaction signatures | `authority.pq_key_auths` | ML-DSA-65 | Implemented, **opt-in per account** |
| Block production | `witness_object.pq_signing_key` | ML-DSA-65 | Implemented, **opt-in per witness** |
| Memo encryption | `memo_data.pq_ciphertext` + `account_options.pq_memo_key` | ML-KEM-768 (hybrid with ECDH) | Implemented, **opt-in per recipient** |
| P2P transport | `stcp_socket` handshake | ML-KEM-768 (hybrid with ECDH) | Implemented, **off by default** |
| Address authorities | `authority.address_auths` | secp256k1 only | **No post-quantum equivalent** |
| Hashing (ids, merkle, HTLC preimages) | SHA-256 / RIPEMD-160 | — | No action needed; Grover halves SHA-256 to ~128-bit, which is still adequate |

Everything in the first four rows is gated behind **both** `HARDFORK_PQ_0_PASSED` and the
committee parameter `pq_serialization_active`. Both are currently inactive:

- `HARDFORK_PQ_0_TIME` is `1893456000` = **2030-01-01T00:00:00Z**, an explicit placeholder.
  The real date is a committee decision.
- `pq_serialization_active` is an `optional<bool>` that defaults to unset.

Until both are true, none of the post-quantum fields reach the wire at all, and the
evaluators reject operations that carry them.

## Why "implemented" is not "safe"

Three distinct gaps, in increasing order of how hard they are to close.

### 1. Not activated (a committee decision)

Nothing is on. This is deliberate and correct — the placeholder hardfork date should not be
a real one, and a chain should not silently change its wire format. But it means the honest
answer to "is BitShares post-quantum safe" is *no*, and will stay *no* until a committee
schedules it.

### 2. Opt-in, with no forcing mechanism

This is the substantive gap. Every protection above is per-account or per-operator:

- An account keeps classical-only authorities until someone runs `migrate_wallet_pq_only()`.
- **`migrate_wallet()` is not protection.** It adds a PQ key *alongside* the classical one at
  equal weight, so either signature alone authorizes. Against a quantum adversary that is
  exactly as strong as no PQ key at all — the attacker simply uses the classical path. Only
  `migrate_wallet_pq_only()`, which removes the classical keys, changes the security
  property. The distinction is documented in `wallet_sign.cpp` because it is easy to get
  backwards.
- An account receives only classical memos until someone runs `generate_pq_memo_key()`.
  Sending is automatic once the recipient has published a key, so adoption is limited by
  recipients, not senders.
- A witness signs classically until it configures a PQ signing key.
- A node negotiates a classical P2P channel unless started with `--enable-pq-p2p`.

There is currently no deadline, no incentive, and no mechanism that ever makes an account
post-quantum safe without its owner acting. A chain where 5% of accounts have migrated is
not a post-quantum chain. Closing this is a governance design problem — a migration
deadline, a fee differential, a required re-key — and not something that can be decided in
the code.

### 3. `address_auths` has no post-quantum equivalent

`authority` gained `pq_key_auths` but `address_auths` was left classical. An account holding
an address authority remains quantum-spendable through that path even after migrating its
keys. `migrate_wallet_pq_only()` refuses to operate on such accounts rather than reporting a
false success, which is the right behaviour, and `migrate_address_auths_pq()` is now the
step it points at: `address( const pq_public_key_type& )` lets an address entry be derived
from a post-quantum key, and that command replaces each classical entry with one at the
same weight.

It is a separate command rather than part of the pq-only migration for a reason that cannot
be resolved in code. An address is the hash of a key, and the wallet does not know whose key
it was. It cannot convert such an entry, only replace it -- and if the entry belonged to a
co-signer, replacing it removes their ability to sign, silently. That is a decision only the
account holder can make, so it is asked for explicitly.

## The memo construction, specifically

Both the memo and the P2P handshake are **hybrid**, not post-quantum alone:

```
legacy   sha512( nonce ‖ ecdh_secret )
hybrid   sha512( nonce ‖ ecdh_secret ‖ ml_kem_shared_secret )
```

An attacker must break both secp256k1 ECDH and ML-KEM-768. This is a deliberate hedge in
both directions: no weaker than today's memo if this KEM implementation turns out flawed,
and no weaker than the KEM once secp256k1 falls. Given that ML-KEM is young and that a memo
is unrecoverable once written, spending a few hundred bytes to avoid betting everything on
one primitive is worth it.

Two consequences worth knowing:

- **Encryption stays classical until the chain has activated PQ serialization.** The KEM
  shared secret is half of the AES key, so writing a hybrid memo while the ciphertext field
  is still stripped on the wire would confirm a memo that nobody — sender, recipient, or
  anyone else — could ever read. The wallet checks activation before choosing the hybrid
  path.
- **If an account publishes a memo key and loses the secret, memos sent to it afterwards are
  permanently unreadable.** `generate_pq_memo_key()` persists the secret to the wallet file
  *before* broadcasting the account update for exactly this reason, but a lost wallet file
  is unrecoverable. Back it up with `dump_pq_private_keys()`.

ML-KEM-768 is fixed rather than negotiable: the memo carries no algorithm tag of its own, so
`account_options::validate()` rejects ML-KEM-512 and ML-KEM-1024 keys. Publishing one would
advertise a key no sender could encapsulate to. Widening this requires making the memo
construction algorithm-aware first.

## What is verified, and what is not

**Verified** (620/620 in `chain_test`; the mainnet replay below is a separate opt-in run,
enabled by pointing `BITSHARES_MAINNET_BLOCK_LOG` at a block log, and is skipped in a default
run so CI stays hermetic):

- The wire format is unchanged under `pq_format::legacy`, field by field, for every gated
  structure — including when the in-memory object holds post-quantum data. This is the
  property that a previously-found defect broke, and it is now asserted directly rather than
  inferred.
- Replaying 35,003 real mainnet blocks (27,809 of them carrying transactions) reproduces
  each block's recorded id and merkle root. This is the check that found the
  witness-operation wire-format defect, and the one that synthetic chains cannot substitute
  for: the ids were computed by upstream code years ago and are not ours to choose. It
  matters most for the memo change specifically — `memo_data` lives inside
  `transfer_operation`, which is the most common operation on the chain, so a mistake in its
  gating would show up here in thousands of blocks rather than in some rare corner.
- Hybrid memos round-trip; the classical key alone cannot read one; a foreign KEM key cannot
  either.
- Post-quantum fields are rejected before activation and accepted after, end to end through
  the chain database.
- A PQ-signed transaction verifies and applies with no classical key in the authority.
- **The vendored primitives match FIPS 203/204.** 354 known-answer checks against NIST's own
  ACVP vectors — in the fc branch that carries the primitives, not in the fc this branch
  currently pins (`libraries/fc/tests/crypto/pqc_kat/` exists only there): ML-KEM-768 keyGen, encapsulation and
  decapsulation, and ML-DSA-65 keyGen and signature generation, all reproducing NIST's
  expected outputs byte for byte. sigGen runs in both modes -- deterministic (`rnd` = 32 zero
  bytes) and hedged (`rnd` from the vector) -- through PQClean's
  `crypto_sign_signature_ctx_derand`. Reproducing a signature exactly is a stronger statement
  than verifying one: a signer with the wrong nonce derivation, domain separation or context
  encoding still produces signatures its own verifier accepts.
  ML-DSA-65 signature verification is checked on 15 cases, 12 of them tampered — modified
  message, modified `z`, modified commitment, modified hint — so a verifier that accepted
  forgeries would fail rather than pass a suite of valid signatures only. The decapsulation
  group covers FIPS 203 §7.3 implicit rejection, where a malformed ciphertext must yield a
  pseudorandom secret rather than an error.

**Not verified:**

- Only the parameter sets in use (ML-KEM-768, ML-DSA-65) have conformance coverage. The
  vendored tree also builds ML-KEM-512/1024 and ML-DSA-44/87, which nothing consumes.
- No third-party cryptographic audit of the integration.
- No side-channel analysis of the vendored primitives.
- Performance under sustained load with PQ signatures on a real network. An ML-DSA-65
  signature is 3309 bytes against secp256k1's 65; block sizes, bandwidth, and validation
  cost all change materially, and this has only been measured on a devnet.

## What activation would actually require

In order:

1. ~~Add NIST KAT vectors for the vendored ML-DSA and ML-KEM parameter sets.~~ Done for
   ML-KEM-768 and ML-DSA-65, key generation, signature generation and verification alike:
   354 checks, all passing. The other parameter sets the vendored tree builds are unused
   and uncovered.
2. Independent review of the cryptographic integration. Not something this repository can
   do for itself; what it can do is remove the excuses, and the conformance coverage in
   item 1 is now complete for the parameter sets in use.
3. ~~Measure PQ signature load.~~ Done on one core, `-O2`, by
   `libraries/fc/tests/crypto/pqc_load_test.cpp`; see "What post-quantum costs" below. Still
   open: the same measurement on a real network under sustained load, which a devnet cannot
   stand in for.
4. Set a real `HARDFORK_PQ_0_TIME`. A committee decision, not a code change — but note that
   the date participates in every block's serialization format, so it has to be agreed
   before it is compiled into anything that produces blocks. Two binaries with different
   dates disagree about the id of block 1 and cannot open each other's chains.
5. Committee enables `pq_serialization_active`. The chain-side plumbing is there and
   `database::is_pq_serialization_active()` reads it; the approval cycle itself is
   governance. One obstacle on the way was removed: bitsharesjs could not serialize any
   operation carrying a fee schedule, which is every committee parameter change. A second
   remains — `proposal_create` built from that library comes out with a mangled
   `review_period_seconds` and `expiration_time`. Committee proposals are normally built
   with `cli_wallet`, which is unaffected.
6. ~~Witnesses configure PQ signing keys.~~ Verified end to end on a devnet: a witness with
   `pq-private-key` configured and `witness_object.pq_signing_key` set produces blocks
   carrying `witness_pq_signature`, signed by exactly that key, and the chain builds on
   them. Witnesses without one produce none. The per-block cost is about 5.3 KB.
7. Design and agree a migration policy for the opt-in gap above — this is the one that
   determines whether the chain is actually post-quantum safe, rather than merely capable
   of being. A design is proposed in [PQ-MIGRATION.md](PQ-MIGRATION.md); agreeing it is
   the community's, and none of it is implemented.

## What post-quantum costs

Measured on one core with an optimized build (`pqc_load_test`; secp256k1 arrives already
optimized, so a Debug build handicaps only ML-DSA and overstates the gap by more than
double).

| | secp256k1 | ML-DSA-65 | ratio |
|---|---|---|---|
| verification | 79 us (a key *recovery*) | 224 us | 2.6x |
| signing | 53 us | 814 us | 15x |
| key generation | — | 233 us | — |
| bytes on the wire | 65 | 5261 (3309 sig + 1952 key) | 81x |
| transfers in a 2 MB block | 14,463 | 392 | 37x fewer |

**The constraint is bandwidth, not CPU.** A full post-quantum block validates in 0.08s of a
single core — under 3% of the 3-second slot, and Graphene verifies in parallel on top of
that. What collapses is throughput: the same block carries 37x fewer transfers, because
ML-DSA offers no public-key recovery and so every signature drags its 1952-byte key along.

Two consequences worth stating plainly:

- `maximum_block_size`, not `maximum_transaction_size`, is what decides post-quantum
  capacity. The transaction-size limit only has to clear key-bearing *operations* (see the
  sizing note in `hardfork.d/PQ_0.hf`); the block-size limit is what caps how many PQ-signed
  transactions a chain can carry per second.
- Signing at 814 us is a wallet-side cost and irrelevant to consensus, but it is 15x
  classical and will be noticeable on constrained hardware — a phone, a hardware wallet —
  where it lands on the user rather than on a validator.

## Building a node with PQ active, for a devnet

`HARDFORK_PQ_0_TIME` is a placeholder date in 2030, so a stock build has PQ permanently
inactive and cannot exercise any of this. A devnet build has to move that date before
genesis, and `PQ_0.hf` guards the `#define` with `#ifndef` precisely so it can be
overridden without editing the file:

```
cmake -DCMAKE_CXX_FLAGS='-DHARDFORK_PQ_0_TIME=fc::time_point_sec(1600000000)' ...
```

Setting it *before* the genesis timestamp matters, and is worth stating explicitly: the
date takes part in every block's serialization format, so two binaries with different
dates disagree about the id of the very first block. A chain produced by one cannot be
opened by the other — it fails with `unlinkable_block_exception`, or asserts on
`head_block_id() == next_block.previous` during replay, which reads like data corruption
and is not. Record the date a devnet chain was built with, alongside its genesis file.

The committee parameter `pq_serialization_active` is the second half of the gate and is
set in the genesis (`initial_parameters.extensions`), not at build time.

Steps 1–6 are engineering and governance mechanics. Step 7 is the one that decides the
answer.
