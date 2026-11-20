#!/bin/bash
# Replay a prefix of the real mainnet chain through every evaluator.
#
# The unit suites and the serialisation replay in pq_regression_tests both stop short of this:
# neither applies a block. This builds a truncated copy of a mainnet block log and hands it to
# witness_node --replay-blockchain, so every historical transaction runs through the real
# evaluators on whatever branch is checked out.
#
#   ./tools/replay_mainnet_prefix.sh <build-dir> <source-block-log> [num-blocks]
#
# The source log is only ever READ. Everything else happens in a scratch directory.
set -e

BUILD=${1:?usage: replay_mainnet_prefix.sh <build-dir> <source-block-log> [num-blocks]}
SRC=${2:?missing source block_num_to_block directory}
N=${3:-1000000}
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

echo "truncating $SRC to $N blocks"
python3 - "$SRC" "$WORK/blockchain/database/block_num_to_block" "$N" <<'PY'
import os, struct, sys
src, dst, n = sys.argv[1], sys.argv[2], int(sys.argv[3])
os.makedirs(dst, exist_ok=True)
ENTRY = 32                      # pos(8) + size(4) + id(20)
with open(os.path.join(src, "index"), "rb") as idx:
    idx.seek(0, os.SEEK_END)
    n = min(n, idx.tell() // ENTRY)
    idx.seek((n - 1) * ENTRY)
    pos, size = struct.unpack("<QI", idx.read(ENTRY)[:12])
    idx.seek(0)
    open(os.path.join(dst, "index"), "wb").write(idx.read(n * ENTRY))
with open(os.path.join(src, "blocks"), "rb") as b, open(os.path.join(dst, "blocks"), "wb") as o:
    left = pos + size
    while left > 0:
        chunk = b.read(min(8 << 20, left))
        if not chunk: break
        o.write(chunk); left -= len(chunk)
print("kept %d blocks" % n)
PY

# --seed-nodes "[]" matters: without it the node finishes the replay and then starts syncing
# the live chain from peers, which is not what is being measured.
"$BUILD/programs/witness_node/witness_node" --data-dir "$WORK" --replay-blockchain \
    --ignore-api-helper-indexes-warning --seed-nodes "[]" > "$WORK/replay.log" 2>&1 &
NODE=$!
for _ in $(seq 1 360); do
   grep -aq "Done reindexing" "$WORK/replay.log" 2>/dev/null && break
   sleep 5
done
sleep 3
kill -9 $NODE 2>/dev/null || true
wait $NODE 2>/dev/null || true

echo
grep -aE "Done reindexing" "$WORK/replay.log" | tail -1
echo "chain id     : $(grep -a 'Chain ID is' "$WORK/replay.log" | tail -1 | sed 's/.*Chain ID is //')"
echo "last progress: $(grep -a 'by num:' "$WORK/replay.log" | tail -1 | sed 's/.*by num: //')"
FAILS=$(grep -ac 'Failed to push\|unlinkable' "$WORK/replay.log" || true)
echo "push failures: $FAILS"
# assert_exceptions from push_proposal are historical proposals that failed on execution --
# real chain history, not replay errors.
[ "$FAILS" = "0" ] || { echo "REPLAY FAILED"; exit 1; }
echo "REPLAY OK"
