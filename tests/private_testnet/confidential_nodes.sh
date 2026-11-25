#!/bin/bash
# Three loopback witness nodes on genesis-dev, plus cli_wallet.
# The witness_node and cli_wallet binaries must be configured with
# -DGRAPHENE_PRIVATE_CT_HF_IN_PAST=ON so the confidential hardforks are in 2015.
# A raw -D on CMAKE_CXX_FLAGS is not enough: the project overwrites that variable.
# This script refuses the mainnet chain id and never adds public seeds.
set -euo pipefail

MAINNET_CHAIN_ID=4018d7844c78f6a6c41c6a552b898022310fc5dec06da467ee7905a8dad512c8
ROOT=$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd)
NODE=${NODE:-/tmp/bts-private-hf/programs/witness_node/witness_node}
WALLET=${WALLET:-/tmp/bts-private-hf/programs/cli_wallet/cli_wallet}
GENESIS_SRC=${GENESIS_SRC:-"$ROOT/libraries/egenesis/genesis-dev.json"}
WORKDIR=${WORKDIR:-/tmp/bts-ct-net}
# Public test key for sha256("nathan"), the signing key in genesis-dev.json.
WIF='5KQwrPbwdL6PhXujxW37FSSQZ1JiwsST4cqQzDeyXtP79zkvFD3'
PRIV='["BTS6MRyAjQq8ud7hVNYcfnVPJqcVpscN5So8BhtHuGYqET5GDW5CV","'"$WIF"'"]'

test -x "$NODE"
test -x "$WALLET"
test -f "$GENESIS_SRC"

cleanup() {
  if [[ -f "$WORKDIR/pids" ]]; then
    while read -r pid; do kill "$pid" 2>/dev/null || true; done < "$WORKDIR/pids"
  fi
}
trap cleanup EXIT

rm -rf "$WORKDIR"
mkdir -p "$WORKDIR/a" "$WORKDIR/b" "$WORKDIR/c" "$WORKDIR/wallet"
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

start_node() {
  local name=$1 p2p=$2 rpc=$3 seeds=$4
  local extra=()
  if [[ "$name" == "a" ]]; then
    extra=(--enable-stale-production --required-participation 0 --private-key "$PRIV" "${witness_args[@]}")
  fi
  "$NODE" \
    --data-dir "$WORKDIR/$name" \
    --genesis-json "$WORKDIR/genesis.json" \
    --p2p-endpoint "127.0.0.1:$p2p" \
    --rpc-endpoint "127.0.0.1:$rpc" \
    --seed-nodes "$seeds" \
    --plugins witness \
    --ignore-api-helper-indexes-warning \
    "${extra[@]}" \
    > "$WORKDIR/$name.log" 2>&1 &
  echo $! >> "$WORKDIR/pids"
}

start_node a 18101 18190 '[]'
for _ in $(seq 1 60); do
  if grep -q "Generated block #" "$WORKDIR/a.log"; then break; fi
  if ! kill -0 "$(tail -n 1 "$WORKDIR/pids")" 2>/dev/null; then
    echo "producer exited" >&2
    tail -n 40 "$WORKDIR/a.log" >&2 || true
    exit 1
  fi
  sleep 1
done
grep -q "Generated block #" "$WORKDIR/a.log"
grep -q "Configured p2p node to listen on 127.0.0.1:18101" "$WORKDIR/a.log"
if grep -E 'seed1.abit-more.com|seed.roelandp.nl|xbts.io|btsnodes.com' "$WORKDIR/a.log"; then
  echo "refusing: public seed in producer log" >&2
  exit 1
fi

start_node b 18102 18191 '["127.0.0.1:18101"]'
start_node c 18103 18192 '["127.0.0.1:18101"]'
for _ in $(seq 1 90); do
  if grep -q "Chain ID is $CHAIN" "$WORKDIR/b.log" && grep -q "Chain ID is $CHAIN" "$WORKDIR/c.log"; then
    break
  fi
  sleep 1
done
grep -q "Chain ID is $CHAIN" "$WORKDIR/b.log"
grep -q "Chain ID is $CHAIN" "$WORKDIR/c.log"
# Block 1 may be applied in sync mode without a "Got block" line. Give the
# normal broadcast path a couple of slots.
sleep 12

"$WALLET" \
  --server-rpc-endpoint ws://127.0.0.1:18190 \
  --rpc-http-endpoint 127.0.0.1:18193 \
  --chain-id "$CHAIN" \
  --wallet-file "$WORKDIR/wallet/wallet.json" \
  --daemon \
  > "$WORKDIR/wallet.log" 2>&1 &
echo $! >> "$WORKDIR/pids"
for _ in $(seq 1 30); do
  if grep -q "Listening" "$WORKDIR/wallet.log" || curl -sf -o /dev/null http://127.0.0.1:18193; then
    break
  fi
  sleep 1
done

"$WALLET" \
  --server-rpc-endpoint ws://127.0.0.1:18191 \
  --rpc-http-endpoint 127.0.0.1:18194 \
  --chain-id "$CHAIN" \
  --wallet-file "$WORKDIR/wallet/wallet-b.json" \
  --daemon \
  > "$WORKDIR/wallet-b.log" 2>&1 &
echo $! >> "$WORKDIR/pids"

python3 - "$WORKDIR" "$WIF" << 'PY'
import json, sys, time, urllib.request
workdir, wif = sys.argv[1], sys.argv[2]
url = "http://127.0.0.1:18193"
url_b = "http://127.0.0.1:18194"

def call(method, params, timeout=180, endpoint=url):
    body = json.dumps({"jsonrpc": "2.0", "id": 1, "method": "call", "params": [0, method, params]}).encode()
    req = urllib.request.Request(endpoint, data=body, headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=timeout) as resp:
        payload = json.load(resp)
    if "error" in payload:
        raise SystemExit("wallet error in %s: %s" % (method, json.dumps(payload["error"])[:800]))
    return payload.get("result")

# The wallet HTTP server may need a moment after the port opens.
last = None
for _ in range(30):
    try:
        call("set_password", ["ct-local-only"])
        break
    except Exception as exc:
        last = exc
        time.sleep(1)
else:
    raise SystemExit("wallet did not accept RPC: %s" % last)
call("unlock", ["ct-local-only"])
call("import_key", ["init0", wif])
claimed = call("import_balance", ["init0", [wif], True])
print("import_balance txs", len(claimed) if isinstance(claimed, list) else claimed)
amounts = [["blind", "100"]] * 16
call("create_blind_account", ["blind", "confidential local test brain key alpha"])
call("create_blind_account", ["blind2", "confidential local test brain key beta"])
deposit = call("transfer_to_blind", ["init0", "BTS", amounts, True])
open(workdir + "/deposit.json", "w").write(json.dumps(deposit))
print("deposit broadcast")
time.sleep(12)
ring = call("ring_blind_transfer", ["blind", "blind2", "50", "BTS", True])
open(workdir + "/ring.json", "w").write(json.dumps(ring))
print("ring_blind_transfer broadcast")
time.sleep(12)
exited = call("ring_transfer_from_blind", ["blind2", "init0", "1", "BTS", True])
open(workdir + "/exit.json", "w").write(json.dumps(exited))
print("ring_transfer_from_blind broadcast")
trx = ring["trx"] if isinstance(ring, dict) and "trx" in ring else ring
try:
    call("broadcast_transaction", [trx], endpoint=url_b)
    raise SystemExit("double-spend broadcast was accepted")
except SystemExit as exc:
    if "double-spend" in str(exc):
        raise
    print("double-spend rejected:", str(exc)[:300])
PY

# Give the block producer time to include the ring operations and the peers time to sync.
sleep 15
if grep -E 'seed1.abit-more.com|seed.roelandp.nl|xbts.io|btsnodes.com' "$WORKDIR"/*.log; then
  echo "refusing: public seed mentioned" >&2
  exit 1
fi
HEAD_A=$(grep -o 'Generated block #[0-9]*' "$WORKDIR/a.log" | tail -n 1 | grep -o '[0-9]*')
echo "producer head $HEAD_A"
test "$HEAD_A" -ge 2
# Followers must have applied a block. "Got block" is the normal-mode path;
# sync mode only logs every 10000, so also accept the maintenance log.
grep -q "Got block: #" "$WORKDIR/b.log" || grep -q "at block " "$WORKDIR/b.log"
grep -q "Got block: #" "$WORKDIR/c.log" || grep -q "at block " "$WORKDIR/c.log"

# Replay node C from its block log with public seeds disabled, then confirm the head.
C_PID=$(ps -eo pid,cmd | awk '/witness_node/ && /18103/ && !/awk/ {print $1; exit}')
if [[ -n "${C_PID:-}" ]]; then kill "$C_PID" || true; fi
for _ in $(seq 1 20); do
  if ! ps -p "$C_PID" >/dev/null 2>&1; then break; fi
  sleep 1
done
"$NODE" \
  --data-dir "$WORKDIR/c" \
  --genesis-json "$WORKDIR/genesis.json" \
  --replay-blockchain --revalidate-blockchain --force-validate \
  --p2p-endpoint 127.0.0.1:18103 \
  --rpc-endpoint 127.0.0.1:18192 \
  --seed-nodes '[]' \
  --plugins witness \
  --ignore-api-helper-indexes-warning \
  > "$WORKDIR/c-replay.log" 2>&1 &
echo $! >> "$WORKDIR/pids"
for _ in $(seq 1 60); do
  if grep -q "Chain ID is" "$WORKDIR/c-replay.log"; then break; fi
  sleep 1
done
grep -q "Chain ID is $CHAIN" "$WORKDIR/c-replay.log"
grep -q "Started BitShares node on a chain with" "$WORKDIR/c-replay.log"
echo "CONFIDENTIAL_NODES_OK chain=$CHAIN producer_head=$HEAD_A"
echo "logs: $WORKDIR"
