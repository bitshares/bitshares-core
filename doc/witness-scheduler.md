
Turn/Token witness scheduling algorithm
---------------------------------------

The algorithm which determines the order of witnesses is referred
to as the *witness scheduling algorithm*.

This was designed by a community bounty in thread
https://bitsharestalk.org/index.php/topic,15547.0
however, Graphene has an additional requirement which
is not taken into account by the solutions in the thread:

The membership and length of the list of witnesses may change over
time.

So in this article I'll describe my solution.

Turns and tokens
----------------

The solution is based on terms of *turns* and *tokens*.

- Newly inserted witnesses start out with a turn and a token.
- In order for a witness to be scheduled, it must have a turn and a token.
- The scheduler maintains a FIFO of witnesses without tokens.
- If no witness has a turn, then the scheduler gives a turn to all witnesses.  This is called "emitting a turn."
- While less than half of the witnesses have tokens, give a token to the first witness in the FIFO and remove it from the FIFO.
- Schedule a witness by picking randomly from all witnesses with both a turn and token.
- When a witness is scheduled, it loses its turn and token.

The generic scheduler
---------------------

The generic scheduler implements turns and tokens.  It only depends
on the C++11 stdlib and boost (not even using fc).  Types provided
by Graphene are template parameters.

The generic far future scheduler
--------------------------------

The far future scheduler is implemented with the following rules:

- Run until you emit a turn.
- Record all witnesses produced.
- Run until you emit a second turn.
- The witnesses produced between the emission of the first turn (exclusive)
and emission of the second turn (inclusive) are called the *far future schedule*.

Then the schedule for the rest of time is determined by repeating
the future schedule indefinitely.  The far future scheduler is required
to give the scheduling algorithm bounded runtime and memory usage even
in chains involving very long gaps.

Slots
-----

Due to dynamic block interval, we must carefully keep in mind
the difference between schedule slots and timestamps.  A
*schedule slot number* is a positive integer.  A slot number of `n`
represents the `n`th next block-interval-aligned timestamp after
the head block.

Note that the mapping between slot numbers and timestamps will change
if the block interval changes.

Scheduling blocks
-----------------

When each block is produced, the blockchain must determine whether
the scheduler needs to be run.  If fewer than `num_witnesses` are
scheduled, the scheduler will run until `2*num_witnesses` are scheduled.
A block in which the scheduler runs is called a *scheduling block*.

Changes in the set of active witnesses do not modify the existing
schedule.  Rather, they will be incorporated into new schedule entries
when the scheduler runs in the next scheduling block.  Thus, a witness
that has lost an election may still produce 1-2 blocks.  Such a witness
is called a *lame duck*.

Near vs. far schedule
---------------------

From a particular chain state, it must be possible to specify a
mapping from slots to witnesses, called the *total witness schedule*.
The total witness schedule is partitioned into a prefix, called the
*near schedule*; the remainder is the *far schedule*.

When a block occurs, `n` entries are *drained* (removed) from the head
of the total schedule, where `n` is the slot number of the new block
according to its parent block.

If the block is a scheduling block, the total schedule is further
transformed.  The new near schedule contains `2*num_witnesses` entries,
with the previous near schedule as a prefix.  The rest of the near
schedule is determined by the current blockchain RNG.

The new far schedule is determined by running the far future scheduler,
as described above.  The far future scheduler also obtains entropy
from the current blockchain RNG.

As an optimization, the implementation does not run the far future
scheduler until a far-future slot is actually queried.  With this
optimization, the only circumstance under which validating nodes must
run the far future scheduler is when a block gap longer than `num_witnesses`
occurs (an extremely rare condition).

Minimizing impact of selective dropout
--------------------------------------

The ability of any single malicious witness to affect the results of the
shuffle algorithm is limited because the RNG is based on bit commitment
of the witnesses.  However, a malicious witness *is* able to
refuse to produce a block.  A run of `m` consecutively scheduled
malicious witnesses can independently make `m` independent choices
of whether to refuse to produce a block.  Basically they are able to
control `m` bits of entropy in the shuffle algorithm's output.

It is difficult-to-impossible to entirely eliminate "the last person
being evil" problem in trustless distributed RNG's.  But we can at least
mitigate this vector by rate-limiting changes to the total witness
schedule to a very slow rate.

If every block schedules a witness, our adversary with `m` malicious
witnesses gets `m` chances per round to selectively drop out in order
to manipulate the shuffle order, allowing `m` attacks per round.
If witnesses are only scheduled once per round,
a selective dropout requires the malicious witness to produce the
scheduling block, limiting the probability to `m/n` attacks per round.
