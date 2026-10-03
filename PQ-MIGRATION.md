# Migrating BitShares to post-quantum authorities

A design, not a decision. Everything here is a proposal for the committee and the community
to argue with; the parts that are code are marked as such, and nothing in this document is
implemented.

`PQ-STATUS.md` records what exists. Its section "Opt-in, with no forcing mechanism" is the
gap this addresses: every post-quantum protection on the chain is per-account or
per-operator, and nothing ever makes an account safe without its owner acting. A chain where
5% of accounts have migrated is not a post-quantum chain.

## The question is not how to migrate everyone

It cannot be done. Accounts go dormant, keys get lost, owners die. Some fraction of the
chain will never act, and that fraction holds real value — on most chains the oldest, least
active accounts hold the most.

So the design question is narrower and harsher:

> **What happens to an account that has not migrated when secp256k1 falls?**

There are exactly two answers. Either an attacker takes it, or the chain stops honouring its
classical authority. There is no third option where it stays both spendable and safe,
because after the break "possession of the classical key" no longer identifies the owner —
it identifies everyone.

That has a consequence worth stating before any mechanism is chosen: **a recovery path built
on the classical key is worthless after the break.** Any deadline must therefore fall
strictly before it, and after that deadline the classical path must close whether or not the
owner acted. Policies that promise otherwise are promising something the mathematics does
not allow.

## Two clocks, not one

`PQ-STATUS.md` already draws the distinction that matters here, and the migration policy has
to follow it rather than treat every surface the same.

**Memos are already losing.** Ciphertext published today is archived today and decrypted the
day the break lands. Nothing done afterwards makes it unreadable again. For memos there is
no useful deadline other than *now*: every day between activation and adoption is permanent,
unrecoverable loss.

**Signatures are recoverable until the break.** A 2026 transfer cannot be forged in 2035; it
is buried. What breaks is the future ability to spend, and that can be fixed at any point
before the break. Here a deadline is a real instrument, and the only question is where to
put it.

Treating these on one schedule gets both wrong: it makes memo adoption look like it has time
(it does not), and it makes signature migration look as urgent as memos (it is not, and
pretending otherwise burns the political capital needed for the deadline that matters).

## Mechanisms, and what each is actually good for

| Mechanism | Reaches | Does not reach | Honest verdict |
|---|---|---|---|
| Fee differential on classical-only operations | Active, fee-sensitive accounts | Dormant accounts, and anyone who does not care about fees | Useful as a nudge, useless as protection. A dormant account pays no fees, so a fee cannot reach it. |
| Mandatory re-key on next spend | Every active account, at the moment its owner is already paying attention | Dormant accounts entirely | The best cost/benefit of anything here, and it protects exactly the wrong population — active accounts were never the ones at risk. |
| Refusing to credit classical-only authorities | Anyone receiving value | Accounts that only ever send | Pushes migration to where a counterparty notices, which is where support burden is lowest. |
| Deadline, then classical authorities stop authorizing | Everyone | — | The only mechanism that actually closes the gap, and the only one with a real cost. |

The first three are worth doing and none of them are sufficient. The fourth is the policy;
the others reduce how many accounts it lands on.

## Proposal

**One deadline, announced far in advance, applying to signatures only. Memos start
immediately.**

1. **At activation** (`pq_serialization_active` = true): wallets publish a PQ memo key on
   first unlock and prefer post-quantum memos wherever the recipient has one. No deadline
   needed and none useful — this is the surface where waiting costs permanently.

2. **At activation + 6 months**: the first transaction an account sends must carry a PQ
   authority, added in the same transaction. Nothing is blocked, nothing is lost; an active
   account migrates as a side effect of being active, and the wallet does it without asking.

3. **At activation + 6 months**: an account whose authority is classical-only can no longer
   be the *recipient* of a transfer. Senders see it before signing, which is when a human is
   present.

4. **At deadline D** (a real date, set by the committee, no earlier than activation + 2
   years): classical-only authorities stop authorizing. Balances are untouched and the
   account is not deleted — it is frozen, and only frozen.

Freezing rather than forfeiting is the whole point of step 4. A frozen account can still be
restored later by whatever governance the chain chooses to build, on evidence that has
nothing to do with a broken signature scheme. A drained account cannot be restored at all.
Between "possibly recoverable" and "certainly gone", the choice is not close.

**Witnesses are a separate and earlier deadline.** Block production is not an account-level
concern: if a supermajority of witnesses still sign classically, block history is forgeable
regardless of how many users migrated. A witness without a PQ signing key should stop being
scheduled at activation + 6 months — far earlier than the account deadline, because the
population is small, professional, reachable, and already expected to run current software.

## What the code would need

None of this exists. In rough order of how much argument each will cause:

- ~~**A wallet command for `address_auths`.**~~ Done: `migrate_address_auths_pq()`. An
  address is a hash of a key, so an entry derived from a classical key can only ever be
  satisfied classically; the command replaces each with the address of a freshly generated
  ML-DSA key at the same weight, leaving the threshold arithmetic untouched.

  It stays a separate command, and `migrate_wallet_pq_only()` still refuses rather than
  calling it. The wallet does not know whose key an address entry hashed — if it belonged to
  a co-signer, replacing it removes their ability to sign and they are not told. That is the
  account holder's decision, so it is asked for rather than assumed.

- **A committee parameter for the deadline**, alongside `pq_serialization_active`, so the
  date is governance rather than a compile-time constant. It has to be settable long before
  it binds, and visible to wallets so they can warn.

- **Evaluator gates** at each step: reject a transfer into a classical-only authority after
  step 3; reject authorization by a classical-only authority after step 4. Both are
  consensus changes and both need their own hardfork gate.

- **A bundled re-key** for step 2, so the wallet can attach the PQ authority to whatever the
  user was already doing rather than making migration a separate act the user can decline.

- **Wallet warnings** driven by the published deadline, escalating as it approaches. The
  single most effective thing on this list per line of code, and the only one that reaches
  people rather than transactions.

## What is deliberately not proposed

**No automatic key derivation on the user's behalf.** A chain cannot generate a
post-quantum key for an account without possessing the secret, and any scheme that appears
to — deriving the PQ key from the classical one, say — inherits the classical key's
security exactly. It would look like migration and provide nothing. The wallet's derivation
from the account's own root secret (see `app/lib/common/PQKeys.js` in the reference wallet)
is safe only because that secret never leaves the wallet.

**No shortening of the deadline in response to news.** A credible break announcement will
produce pressure to bring D forward. Doing so converts an orderly migration into a race that
the attacker, who has been preparing, wins. The deadline's value is that it is known in
advance; a deadline that moves is not a deadline.
