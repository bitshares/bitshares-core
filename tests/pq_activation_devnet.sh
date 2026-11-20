#!/bin/bash
#
# Multi-node PQ_0 activation test.
#
# Stands up a small devnet, carries it across a PQ hardfork, and then starts a third node
# from an empty data directory to check the chain is still joinable once activated.
#
# This exists because the defects it covers are invisible to the unit suite. Seven
# serialization bugs found during review all passed every existing test, and four of them
# only appeared when two real nodes talked to each other across an activation: a follower
# frozen at the activation block, a producer aborting inside a log statement, block ids
# displaced by one byte, and blocks that became silently unreadable after the fork. None of
# that is reachable from a single-process test.
#
# Usage:  tests/pq_activation_devnet.sh [seconds-until-hardfork]
#
# The script rewrites libraries/chain/hardfork.d/PQ_0.hf to a near-future time, rebuilds,
# runs the test, and always restores the original hardfork time on exit. It needs an
# already-configured build directory.
#
# Exit status is 0 only if every check passes.

set -u -o pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="$REPO/build"
NODE="$BUILD/programs/witness_node/witness_node"
HF_FILE="$REPO/libraries/chain/hardfork.d/PQ_0.hf"
GENESIS_SRC="${GENESIS_SRC:-}"

LEAD="${1:-900}"          # seconds from now until the hardfork fires
BLOCK_INTERVAL=0          # derived from the genesis file below, never assumed
WORKDIR="$(mktemp -d "${TMPDIR:-/tmp}/pq-activation.XXXXXX")"
ORIGINAL_HF=""
FAILED=0

log()  { printf '%s  %s\n' "$(date -u +%H:%M:%S)" "$*"; }
fail() { printf '%s  FAIL: %s\n' "$(date -u +%H:%M:%S)" "$*"; FAILED=1; }
ok()   { printf '%s  ok:   %s\n' "$(date -u +%H:%M:%S)" "$*"; }

cleanup() {
   for d in A B C; do
      [ -f "$WORKDIR/$d.pid" ] && kill -9 "$(cat "$WORKDIR/$d.pid")" 2>/dev/null
   done
   if [ -n "$ORIGINAL_HF" ]; then
      sed -i "s/time_point_sec( [0-9]* )/time_point_sec( $ORIGINAL_HF )/" "$HF_FILE"
      log "restored hardfork time to $ORIGINAL_HF"
   fi
   log "artifacts left in $WORKDIR"
}
trap cleanup EXIT

rpc() { # rpc <port> <method> <json-params>
   curl -s --max-time 5 -H 'Content-Type: application/json' \
        -d "{\"jsonrpc\":\"2.0\",\"method\":\"call\",\"params\":[0,\"$2\",$3],\"id\":1}" \
        "http://127.0.0.1:$1/rpc"
}
head_num() { rpc "$1" get_dynamic_global_properties '[]' | grep -oE '"head_block_number":[0-9]+' | cut -d: -f2; }
head_id()  { rpc "$1" get_dynamic_global_properties '[]' | grep -oE '"head_block_id":"[0-9a-f]+"' | cut -d'"' -f4; }

# ---------------------------------------------------------------- set the hardfork time
[ -f "$HF_FILE" ] || { echo "missing $HF_FILE"; exit 1; }
ORIGINAL_HF="$(grep -oE 'time_point_sec\( [0-9]+ \)' "$HF_FILE" | grep -oE '[0-9]+')"
HF_TIME=$(( ($(date +%s) / 60) * 60 + LEAD ))
sed -i "s/time_point_sec( $ORIGINAL_HF )/time_point_sec( $HF_TIME )/" "$HF_FILE"
log "hardfork set to $(date -u -d "@$HF_TIME" +%H:%M:%S) (was $ORIGINAL_HF)"

log "building witness_node..."
if ! ( cd "$BUILD" && make -j"$(nproc)" witness_node >"$WORKDIR/build.log" 2>&1 ); then
   fail "build failed, see $WORKDIR/build.log"; exit 1
fi
[ -x "$NODE" ] || { fail "no witness_node at $NODE"; exit 1; }

# ---------------------------------------------------------------- genesis
if [ -z "$GENESIS_SRC" ]; then
   fail "set GENESIS_SRC to a devnet genesis.json with pq_serialization_active enabled"
   exit 1
fi
START=$(( ($(date +%s) / 60) * 60 ))
python3 - "$GENESIS_SRC" "$WORKDIR/genesis.json" "$START" <<'PY'
import json, sys, time
src, dst, start = sys.argv[1], sys.argv[2], int(sys.argv[3])
g = json.load(open(src))
g["initial_timestamp"] = time.strftime("%Y-%m-%dT%H:%M:%S", time.gmtime(start))
json.dump(g, open(dst, "w"), indent=2)
PY
# The chain's cadence comes from the genesis file, not from GRAPHENE_DEFAULT_BLOCK_INTERVAL.
# Read it back rather than assuming a value, so this stays correct whatever the default is.
BLOCK_INTERVAL=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["initial_parameters"]["block_interval"])' "$WORKDIR/genesis.json")
if [ -z "$BLOCK_INTERVAL" ] || [ "$BLOCK_INTERVAL" -le 0 ]; then
   fail "could not read block_interval from genesis"; exit 1
fi
log "genesis starts $(date -u -d "@$START" +%H:%M:%S), block interval ${BLOCK_INTERVAL}s"

start_node() { # start_node <name> <rpc-port> <p2p-port> <seed-or-empty> [producer]
   local name=$1 rpc_port=$2 p2p_port=$3 seed=$4 producer=${5:-no}
   local args=( --data-dir "$WORKDIR/$name" --genesis-json "$WORKDIR/genesis.json"
                --rpc-endpoint "127.0.0.1:$rpc_port" --p2p-endpoint "127.0.0.1:$p2p_port" )
   if [ -n "$seed" ]; then args+=( --seed-nodes "[\"$seed\"]" ); else args+=( --seed-nodes '[]' ); fi
   if [ "$producer" = yes ]; then
      args+=( --enable-stale-production --required-participation 0 )
      for i in $(seq 1 11); do args+=( -w "\"1.6.$i\"" ); done
      [ -n "${WITNESS_KEY:-}" ] && args+=( --private-key "$WITNESS_KEY" )
   fi
   "$NODE" "${args[@]}" >>"$WORKDIR/$name.log" 2>&1 &
   echo $! > "$WORKDIR/$name.pid"
   disown %% 2>/dev/null   # so cleanup's kill does not print job-control noise
   log "started $name (rpc $rpc_port, p2p $p2p_port)"
}

start_node A 18310 11210 "" yes
sleep 8
start_node B 18311 11211 "127.0.0.1:11210"
sleep 8

# ---------------------------------------------------------------- carry them across
TARGET_BLOCKS=$(( (HF_TIME - START) / BLOCK_INTERVAL + 20 ))
log "waiting for the producer to pass block ~$TARGET_BLOCKS (past the hardfork)"
DEADLINE=$(( $(date +%s) + LEAD + 300 ))
while [ "$(date +%s)" -lt "$DEADLINE" ]; do
   a=$(head_num 18310); [ -n "${a:-}" ] && [ "$a" -ge "$TARGET_BLOCKS" ] && break
   sleep 10
done

A_NUM=$(head_num 18310); B_NUM=$(head_num 18311)
A_ID=$(head_id 18310);   B_ID=$(head_id 18311)
log "producer at ${A_NUM:-?}, follower at ${B_NUM:-?}"

[ -n "${A_NUM:-}" ] && [ "$A_NUM" -ge "$TARGET_BLOCKS" ] \
   && ok "producer crossed the hardfork" \
   || fail "producer did not reach block $TARGET_BLOCKS (got ${A_NUM:-none})"

# The original defect: the follower froze on the activation block and never moved again.
[ -n "${B_NUM:-}" ] && [ "$B_NUM" -ge $(( TARGET_BLOCKS - 5 )) ] \
   && ok "follower kept up across the hardfork" \
   || fail "follower stalled at ${B_NUM:-none} while producer reached ${A_NUM:-none}"

[ -n "${A_ID:-}" ] && [ "$A_ID" = "${B_ID:-}" ] \
   && ok "producer and follower agree on head id ($A_ID)" \
   || fail "head id mismatch: producer=$A_ID follower=${B_ID:-none}"

# ---------------------------------------------------------------- can a new node still join?
log "starting a third node from an empty data dir, chain already activated"
start_node C 18312 11212 "127.0.0.1:11210"
JOIN_DEADLINE=$(( $(date +%s) + 240 ))
while [ "$(date +%s)" -lt "$JOIN_DEADLINE" ]; do
   c=$(head_num 18312); [ -n "${c:-}" ] && [ "$c" -ge "$TARGET_BLOCKS" ] && break
   sleep 10
done
C_NUM=$(head_num 18312); C_ID=$(head_id 18312)

[ -n "${C_NUM:-}" ] && [ "$C_NUM" -ge "$TARGET_BLOCKS" ] \
   && ok "new node synced from genesis past the activation block (at $C_NUM)" \
   || fail "new node could not sync past activation (stuck at ${C_NUM:-none}) -- chain is unjoinable"

[ -n "${C_ID:-}" ] && [ "$C_ID" = "$(head_id 18310)" ] \
   && ok "new node agrees with the producer on head id" \
   || fail "new node head id differs from producer"

# ---------------------------------------------------------------- blocks readable either side
ACTIVATION_BLOCK=$(( (HF_TIME - START) / BLOCK_INTERVAL ))
for n in 1 $(( ACTIVATION_BLOCK > 2 ? ACTIVATION_BLOCK - 1 : 1 )) "$ACTIVATION_BLOCK" $(( ACTIVATION_BLOCK + 5 )); do
   got=$(rpc 18312 get_block "[$n]" | grep -oE '"timestamp":"[^"]+"')
   [ -n "$got" ] && ok "block $n readable on the new node" \
                 || fail "block $n reads back null -- stored blocks unreadable after activation"
done

# ---------------------------------------------------------------- crash / error sweep
for n in A B C; do
   [ -f "$WORKDIR/$n.log" ] || continue
   grep -q "Assertion" "$WORKDIR/$n.log" && fail "$n hit an assertion (see $WORKDIR/$n.log)"
   c=$(grep -ac "unlinkable" "$WORKDIR/$n.log")
   [ "$c" -eq 0 ] || fail "$n reported $c unlinkable blocks"
   c=$(grep -ac "Couldn't find block" "$WORKDIR/$n.log")
   [ "$c" -eq 0 ] || fail "$n was asked for $c blocks it did not have (displaced block ids)"
done
[ "$FAILED" -eq 0 ] && ok "no assertions, unlinkable blocks, or missing-block requests"

echo
if [ "$FAILED" -eq 0 ]; then log "PASS"; else log "FAILED"; fi
exit "$FAILED"
