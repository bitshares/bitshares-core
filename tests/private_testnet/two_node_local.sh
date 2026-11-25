#!/bin/bash
# Two witness_node processes, loopback only.
# Refuses the BitShares mainnet chain id and never adds the public seed list.
# Node A produces blocks. Node B syncs from 127.0.0.1, then revalidates its block log.
set -euo pipefail

MAINNET_CHAIN_ID=4018d7844c78f6a6c41c6a552b898022310fc5dec06da467ee7905a8dad512c8
ROOT=$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd)
NODE=${NODE:-/tmp/bts-release/programs/witness_node/witness_node}
GENESIS_SRC=${GENESIS_SRC:-"$ROOT/libraries/egenesis/genesis-dev.json"}
WORKDIR=${WORKDIR:-/tmp/bts-private-testnet}
# Public test key for sha256("nathan"), the signing key baked into genesis-dev.json.
PRIV='["BTS6MRyAjQq8ud7hVNYcfnVPJqcVpscN5So8BhtHuGYqET5GDW5CV","5KQwrPbwdL6PhXujxW37FSSQZ1JiwsST4cqQzDeyXtP79zkvFD3"]'

test -x "$NODE"
test -f "$GENESIS_SRC"

cleanup() {
  if [[ -f "$WORKDIR/a.pid" ]]; then kill "$(cat "$WORKDIR/a.pid")" 2>/dev/null || true; fi
  if [[ -f "$WORKDIR/b.pid" ]]; then kill "$(cat "$WORKDIR/b.pid")" 2>/dev/null || true; fi
}
trap cleanup EXIT

rm -rf "$WORKDIR"
mkdir -p "$WORKDIR/a" "$WORKDIR/b"
cp "$GENESIS_SRC" "$WORKDIR/genesis.json"

if grep -q "$MAINNET_CHAIN_ID" "$WORKDIR/genesis.json"; then
  echo "refusing: genesis file contains the mainnet chain id" >&2
  exit 1
fi

CHAIN=$(python3 -c 'import hashlib,sys; print(hashlib.sha256(open(sys.argv[1],"rb").read()).hexdigest())' "$WORKDIR/genesis.json")
if [[ "$CHAIN" == "$MAINNET_CHAIN_ID" ]]; then
  echo "refusing: genesis hashes to the mainnet chain id" >&2
  exit 1
fi
echo "private chain id $CHAIN"

witness_args=()
for i in $(seq 0 10); do
  witness_args+=(--witness-id "\"1.6.$i\"")
done

echo "starting producer on 127.0.0.1:18001"
"$NODE" \
  --data-dir "$WORKDIR/a" \
  --genesis-json "$WORKDIR/genesis.json" \
  --p2p-endpoint 127.0.0.1:18001 \
  --rpc-endpoint 127.0.0.1:18090 \
  --seed-nodes '[]' \
  --plugins witness \
  --ignore-api-helper-indexes-warning \
  --enable-stale-production \
  --required-participation 0 \
  --private-key "$PRIV" \
  "${witness_args[@]}" \
  > "$WORKDIR/a.log" 2>&1 &
echo $! > "$WORKDIR/a.pid"

wait_for() {
  local file=$1 pattern=$2 tries=$3
  local n=0
  while [[ $n -lt $tries ]]; do
    if grep -q "$pattern" "$file" 2>/dev/null; then
      return 0
    fi
    if ! kill -0 "$(cat "$WORKDIR/a.pid")" 2>/dev/null; then
      echo "producer exited early" >&2
      tail -n 40 "$WORKDIR/a.log" >&2 || true
      return 1
    fi
    sleep 1
    n=$((n + 1))
  done
  echo "timed out waiting for $pattern" >&2
  tail -n 40 "$file" >&2 || true
  return 1
}

wait_for "$WORKDIR/a.log" "Configured p2p node to listen on 127.0.0.1:18001" 60
wait_for "$WORKDIR/a.log" "Generated block #" 90

if grep -E 'seed1.abit-more.com|seed.roelandp.nl|xbts.io|btsnodes.com' "$WORKDIR/a.log"; then
  echo "refusing: producer log mentions a public seed" >&2
  exit 1
fi

echo "starting follower on 127.0.0.1:18002"
"$NODE" \
  --data-dir "$WORKDIR/b" \
  --genesis-json "$WORKDIR/genesis.json" \
  --p2p-endpoint 127.0.0.1:18002 \
  --rpc-endpoint 127.0.0.1:18091 \
  --seed-node 127.0.0.1:18001 \
  --seed-nodes '["127.0.0.1:18001"]' \
  --plugins witness \
  --ignore-api-helper-indexes-warning \
  > "$WORKDIR/b.log" 2>&1 &
echo $! > "$WORKDIR/b.pid"

n=0
while [[ $n -lt 60 ]]; do
  if grep -q "Got block: #" "$WORKDIR/b.log" 2>/dev/null; then
    break
  fi
  if ! kill -0 "$(cat "$WORKDIR/b.pid")" 2>/dev/null; then
    echo "follower exited early" >&2
    tail -n 40 "$WORKDIR/b.log" >&2 || true
    exit 1
  fi
  sleep 1
  n=$((n + 1))
done
grep -q "Got block: #" "$WORKDIR/b.log"

if grep -E 'seed1.abit-more.com|seed.roelandp.nl|xbts.io|btsnodes.com' "$WORKDIR/b.log"; then
  echo "refusing: follower log mentions a public seed" >&2
  exit 1
fi

# Give the follower a couple more blocks, then replay its local log offline.
sleep 12
HEAD_B=$(grep -o 'Got block: #[0-9]*' "$WORKDIR/b.log" | tail -n 1 | grep -o '[0-9]*')
echo "follower reached block $HEAD_B"
test -n "$HEAD_B"
test "$HEAD_B" -ge 1

kill "$(cat "$WORKDIR/b.pid")"
# Wait until the process is gone so the database lock is released.
for _ in $(seq 1 30); do
  if ! kill -0 "$(cat "$WORKDIR/b.pid")" 2>/dev/null; then
    break
  fi
  sleep 1
done
if kill -0 "$(cat "$WORKDIR/b.pid")" 2>/dev/null; then
  echo "follower did not exit" >&2
  exit 1
fi

echo "revalidating follower block log with public seeds disabled"
"$NODE" \
  --data-dir "$WORKDIR/b" \
  --genesis-json "$WORKDIR/genesis.json" \
  --replay-blockchain \
  --revalidate-blockchain \
  --force-validate \
  --p2p-endpoint 127.0.0.1:18002 \
  --rpc-endpoint 127.0.0.1:18091 \
  --seed-nodes '[]' \
  --plugins witness \
  --ignore-api-helper-indexes-warning \
  > "$WORKDIR/b-replay.log" 2>&1 &
echo $! > "$WORKDIR/b.pid"

n=0
while [[ $n -lt 90 ]]; do
  if grep -q "Done starting up application" "$WORKDIR/b-replay.log" 2>/dev/null; then
    break
  fi
  if ! kill -0 "$(cat "$WORKDIR/b.pid")" 2>/dev/null; then
    echo "replay exited early" >&2
    tail -n 50 "$WORKDIR/b-replay.log" >&2 || true
    exit 1
  fi
  sleep 1
  n=$((n + 1))
done
grep -q "Done starting up application" "$WORKDIR/b-replay.log"

if grep -E 'seed1.abit-more.com|seed.roelandp.nl|xbts.io|btsnodes.com' "$WORKDIR/b-replay.log"; then
  echo "refusing: replay log mentions a public seed" >&2
  exit 1
fi

echo "TWO_NODE_OK chain=$CHAIN follower_block=$HEAD_B"
echo "logs: $WORKDIR/a.log $WORKDIR/b.log $WORKDIR/b-replay.log"
