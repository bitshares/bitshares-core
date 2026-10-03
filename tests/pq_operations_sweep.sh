#!/bin/bash
#
# Runs the same set of chain and wallet operations on both sides of a PQ activation, then
# checks that a node starting from an empty data directory can replay the whole thing.
#
# The multi-node activation test (pq_activation_devnet.sh) crosses the hardfork with empty
# blocks. That leaves the paths which only exist when a block actually contains transactions
# untested across the boundary -- the merkle root, the packed size that feeds fees, and the
# recovered signer set. All three had defects found during review, and none of them is
# reachable from a chain of empty blocks: calculate_merkle_root() returns a fixed empty
# checksum before it consults its cache when a block has no transactions.
#
# So this drives real operations through cli_wallet before and after activation, and then
# makes a fresh node replay blocks that carry transactions on both sides of it.
#
# Usage:  GENESIS_SRC=/path/genesis.json WITNESS_KEY='["<pub>","<wif>"]' \
#         tests/pq_operations_sweep.sh [seconds-until-hardfork]
#
# Restores the original hardfork time on exit. Exit status 0 only if every check passes.

set -u -o pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="$REPO/build"
NODE="$BUILD/programs/witness_node/witness_node"
WALLET="$BUILD/programs/cli_wallet/cli_wallet"
HF_FILE="$REPO/libraries/chain/hardfork.d/PQ_0.hf"
GENESIS_SRC="${GENESIS_SRC:-}"
WITNESS_KEY="${WITNESS_KEY:-}"

LEAD="${1:-900}"
WORKDIR="$(mktemp -d "${TMPDIR:-/tmp}/pq-opsweep.XXXXXX")"
NODE_RPC=18400; NODE_P2P=11400; WALLET_RPC=18500; FRESH_RPC=18401; FRESH_P2P=11401
ORIGINAL_HF=""; FAILED=0

log()  { printf '%s  %s\n' "$(date -u +%H:%M:%S)" "$*" >&2; }
ok()   { printf '%s  ok:   %s\n' "$(date -u +%H:%M:%S)" "$*" >&2; }
fail() { printf '%s  FAIL: %s\n' "$(date -u +%H:%M:%S)" "$*" >&2; FAILED=1; }

cleanup() {
   for f in "$WORKDIR"/*.pid; do [ -f "$f" ] && kill -9 "$(cat "$f")" 2>/dev/null; done
   [ -n "$ORIGINAL_HF" ] && \
      sed -i "s/time_point_sec( [0-9]* )/time_point_sec( $ORIGINAL_HF )/" "$HF_FILE" && \
      log "restored hardfork time to $ORIGINAL_HF"
   log "artifacts in $WORKDIR"
}
trap cleanup EXIT

node_rpc() {
   curl -s --max-time 8 -H 'Content-Type: application/json' \
        -d "{\"jsonrpc\":\"2.0\",\"method\":\"call\",\"params\":[0,\"$2\",$3],\"id\":1}" \
        "http://127.0.0.1:$1/rpc"
}
# w <method> <json-params> -- drives the wallet over its HTTP RPC endpoint
w() {
   curl -s --max-time 60 -H 'Content-Type: application/json' \
        -d "{\"jsonrpc\":\"2.0\",\"method\":\"$1\",\"params\":$2,\"id\":1}" \
        "http://127.0.0.1:$WALLET_RPC/rpc"
}
# Broadcasting only puts a transaction in the mempool. Anything that depends on its effect --
# an account existing, an asset existing, a balance being spendable -- has to wait for a block.
await_block() { local h0 h1 i; h0=$(head_num "$NODE_RPC"); for i in $(seq 1 40); do
   sleep 2; h1=$(head_num "$NODE_RPC"); [ -n "${h1:-}" ] && [ -n "${h0:-}" ] && [ "$h1" -gt "$h0" ] && return 0
done; return 0; }

head_num() { node_rpc "$1" get_dynamic_global_properties '[]' | grep -oE '"head_block_number":[0-9]+' | cut -d: -f2; }

# check <label> <json-response> -- an operation counts as passing only if the wallet did not
# return an error object; "result": null is a legitimate answer for several calls.
check() {
   local label="$1" resp="$2"
   if [ -z "$resp" ]; then fail "$label: no response"; return 1; fi
   if grep -q '"error"' <<<"$resp"; then
      fail "$label: $(grep -oE '"message":"[^"]*"' <<<"$resp" | head -1)"
      return 1
   fi
   ok "$label"
   return 0
}

[ -n "$GENESIS_SRC" ] || { echo "set GENESIS_SRC"; exit 1; }
[ -n "$WITNESS_KEY" ] || { echo "set WITNESS_KEY"; exit 1; }
[ -x "$NODE" ] && [ -x "$WALLET" ] || { echo "build witness_node and cli_wallet first"; exit 1; }

# ------------------------------------------------------------------ hardfork + build
ORIGINAL_HF="$(grep -oE 'time_point_sec\( [0-9]+ \)' "$HF_FILE" | grep -oE '[0-9]+')"
HF_TIME=$(( ($(date +%s) / 60) * 60 + LEAD ))
sed -i "s/time_point_sec( $ORIGINAL_HF )/time_point_sec( $HF_TIME )/" "$HF_FILE"
log "hardfork set to $(date -u -d "@$HF_TIME" +%H:%M:%S) (was $ORIGINAL_HF)"

log "building..."
( cd "$BUILD" && make -j"$(nproc)" witness_node cli_wallet >"$WORKDIR/build.log" 2>&1 ) \
   || { fail "build failed, see $WORKDIR/build.log"; exit 1; }

# ------------------------------------------------------------------ genesis + node
START=$(( ($(date +%s) / 60) * 60 ))
BLOCK_INTERVAL=$(python3 - "$GENESIS_SRC" "$WORKDIR/genesis.json" "$START" <<'PY'
import json, sys, time
src, dst, start = sys.argv[1], sys.argv[2], int(sys.argv[3])
g = json.load(open(src))
g["initial_timestamp"] = time.strftime("%Y-%m-%dT%H:%M:%S", time.gmtime(start))
json.dump(g, open(dst, "w"), indent=2)
print(g["initial_parameters"]["block_interval"])
PY
)
log "genesis starts $(date -u -d "@$START" +%H:%M:%S), block interval ${BLOCK_INTERVAL}s"

start_node() { # start_node <name> <rpc> <p2p> <seed|""> <producer yes|no>
   local name=$1 rpc=$2 p2p=$3 seed=$4 producer=$5
   local args=( --data-dir "$WORKDIR/$name" --genesis-json "$WORKDIR/genesis.json"
                --rpc-endpoint "127.0.0.1:$rpc" --p2p-endpoint "127.0.0.1:$p2p" )
   if [ -n "$seed" ]; then args+=( --seed-nodes "[\"$seed\"]" ); else args+=( --seed-nodes '[]' ); fi
   if [ "$producer" = yes ]; then
      args+=( --enable-stale-production --required-participation 0 --private-key "$WITNESS_KEY" )
      for i in $(seq 1 11); do args+=( -w "\"1.6.$i\"" ); done
   fi
   "$NODE" "${args[@]}" >>"$WORKDIR/$name.log" 2>&1 &
   echo $! > "$WORKDIR/$name.pid"; disown %% 2>/dev/null
   log "started node $name"
}

start_node A "$NODE_RPC" "$NODE_P2P" "" yes
for _ in $(seq 1 30); do [ -n "$(head_num "$NODE_RPC" || true)" ] && break; sleep 2; done

CHAIN_ID=$(node_rpc "$NODE_RPC" get_chain_id '[]' | grep -oE '[0-9a-f]{64}')
[ -n "$CHAIN_ID" ] || { fail "no chain id from node"; exit 1; }

"$WALLET" -s "ws://127.0.0.1:$NODE_RPC" -w "$WORKDIR/wallet.json" --chain-id "$CHAIN_ID" \
          -H "127.0.0.1:$WALLET_RPC" -d >>"$WORKDIR/wallet.log" 2>&1 &
echo $! > "$WORKDIR/wallet.pid"; disown %% 2>/dev/null
for _ in $(seq 1 30); do grep -q '"result"' <<<"$(w info '[]')" && break; sleep 2; done
log "wallet daemon up"

WIF=$(python3 -c 'import json,sys; print(json.loads(sys.argv[1])[1])' "$WITNESS_KEY")
check "wallet set_password" "$(w set_password '["pw"]')" >/dev/null
check "wallet unlock"       "$(w unlock '["pw"]')"       >/dev/null
check "wallet import_key"   "$(w import_key "[\"init0\",\"$WIF\"]")" >/dev/null
check "claim genesis balance" "$(w import_balance "[\"init0\",[\"$WIF\"],true]")" >/dev/null

# ------------------------------------------------------------------ the sweep
# Runs the same operations in both phases. Names and symbols are suffixed so the second pass
# is not rejected as a duplicate of the first.
sweep() {
   local tag=$1 n=0
   log "--- operation sweep: $tag ---"

   # wallet-only calls: no chain transaction, but they exercise the wallet's key handling
   check "[$tag] suggest_brain_key"    "$(w suggest_brain_key '[]')"          && n=$((n+1))
   check "[$tag] list_my_accounts"     "$(w list_my_accounts '[]')"           && n=$((n+1))
   check "[$tag] get_account init0"    "$(w get_account '["init0"]')"         && n=$((n+1))
   check "[$tag] list_account_balances" "$(w list_account_balances '["init0"]')" && n=$((n+1))
   check "[$tag] dump_private_keys"    "$(w dump_private_keys '[]')"          && n=$((n+1))
   check "[$tag] dump_pq_private_keys" "$(w dump_pq_private_keys '[]')"       && n=$((n+1))

   # chain operations: each broadcasts, so blocks in this phase carry transactions
   local brain; brain=$(w suggest_brain_key '[]' | python3 -c 'import json,sys; print(json.load(sys.stdin)["result"]["brain_priv_key"])' 2>/dev/null)
   local SYM="AST${tag^^}"   # is_valid_symbol() rejects lower case
   check "[$tag] create_account acc$tag" \
      "$(w create_account_with_brain_key "[\"$brain\",\"acc$tag\",\"init0\",\"init0\",true]")" && n=$((n+1))
   await_block
   # enough to cover the lifetime-membership fee charged by upgrade_account below
   check "[$tag] transfer to acc$tag" \
      "$(w transfer "[\"init0\",\"acc$tag\",\"50000\",\"BTS\",\"memo-$tag\",true]")" && n=$((n+1))
   # init1 pays its own witness-update fee, so it needs a balance too
   w transfer "[\"init0\",\"init1\",\"1000\",\"BTS\",\"fee-$tag\",true]" >/dev/null
   await_block
   check "[$tag] create_asset $SYM" \
      "$(w create_asset "[\"init0\",\"$SYM\",4,{\"max_supply\":\"1000000000\",\"market_fee_percent\":0,\"max_market_fee\":\"0\",\"issuer_permissions\":79,\"flags\":0,\"core_exchange_rate\":{\"base\":{\"amount\":1,\"asset_id\":\"1.3.0\"},\"quote\":{\"amount\":1,\"asset_id\":\"1.3.1\"}},\"whitelist_authorities\":[],\"blacklist_authorities\":[],\"whitelist_markets\":[],\"blacklist_markets\":[],\"description\":\"\"},null,true]")" && n=$((n+1))
   await_block
   check "[$tag] issue_asset $SYM" \
      "$(w issue_asset "[\"acc$tag\",\"1000\",\"$SYM\",\"issue-$tag\",true]")" && n=$((n+1))
   await_block
   check "[$tag] upgrade_account acc$tag" \
      "$(w upgrade_account "[\"acc$tag\",true]")" && n=$((n+1))
   await_block
   # witness_update_operation -- the operation whose wire format regressed (finding 40).
   # Five arguments, not four: this wallet does not apply C++ default arguments over RPC, so
   # the post-quantum signing key the branch appended has to be passed explicitly. Callers
   # written against the previous four-parameter signature break.
   check "[$tag] update_witness init1" \
      "$(w update_witness "[\"init1\",\"http://$tag.example\",\"\",true,\"\"]")" && n=$((n+1))

   log "[$tag] $n operations returned without error"
   echo "$n"
}

ACTIVATION_BLOCK=$(( (HF_TIME - START) / BLOCK_INTERVAL ))
log "activation block will be ~$ACTIVATION_BLOCK"

PRE_START=$(head_num "$NODE_RPC")
PRE_COUNT=$(sweep pre | tail -1)
await_block; await_block
PRE_END=$(head_num "$NODE_RPC")

log "waiting for the hardfork..."
DEADLINE=$(( $(date +%s) + LEAD + 300 ))
while [ "$(date +%s)" -lt "$DEADLINE" ]; do
   h=$(head_num "$NODE_RPC"); [ -n "${h:-}" ] && [ "$h" -gt $(( ACTIVATION_BLOCK + 3 )) ] && break
   sleep 10
done

POST_START=$(head_num "$NODE_RPC")
POST_COUNT=$(sweep post | tail -1)
await_block; await_block
POST_END=$(head_num "$NODE_RPC")

# ------------------------------------------------------------------ checks
[ "${PRE_COUNT:-0}" -eq 12 ]  && ok "all 12 operations succeeded before activation" \
                              || fail "only ${PRE_COUNT:-0}/12 operations succeeded before activation"
[ "${POST_COUNT:-0}" -eq 12 ] && ok "all 12 operations succeeded after activation" \
                              || fail "only ${POST_COUNT:-0}/12 operations succeeded after activation"

# The point of the exercise: blocks carrying transactions must exist on BOTH sides, otherwise
# the merkle-root path was never crossed and this test proves nothing the empty-block one didn't.
count_tx_blocks() { # count_tx_blocks <from> <to>
   local from=$1 to=$2 n=0 i
   for (( i=from; i<=to && i>0; i++ )); do
      local c
      if node_rpc "$NODE_RPC" get_block "[$i]" | grep -q '"transactions":\[{'; then n=$((n+1)); fi
   done
   echo "$n"
}
PRE_TX_BLOCKS=$(count_tx_blocks "$PRE_START" "$PRE_END")
POST_TX_BLOCKS=$(count_tx_blocks "$POST_START" "$POST_END")
log "blocks carrying transactions: $PRE_TX_BLOCKS before activation, $POST_TX_BLOCKS after"
[ "$PRE_TX_BLOCKS" -gt 0 ]  || fail "no non-empty blocks before activation -- merkle path not exercised"
[ "$POST_TX_BLOCKS" -gt 0 ] || fail "no non-empty blocks after activation -- merkle path not exercised"
[ "$PRE_TX_BLOCKS" -gt 0 ] && [ "$POST_TX_BLOCKS" -gt 0 ] && \
   ok "non-empty blocks exist on both sides of the activation"

# A fresh node must replay all of it, which is what re-validates every merkle root and every
# signature across the boundary rather than merely accepting blocks as they were produced.
log "starting a fresh node to replay the chain from genesis"
start_node C "$FRESH_RPC" "$FRESH_P2P" "127.0.0.1:$NODE_P2P" no
TARGET=$(head_num "$NODE_RPC")
JOIN_DEADLINE=$(( $(date +%s) + 300 ))
while [ "$(date +%s)" -lt "$JOIN_DEADLINE" ]; do
   c=$(head_num "$FRESH_RPC"); [ -n "${c:-}" ] && [ "$c" -ge "$TARGET" ] && break
   sleep 10
done
C_HEAD=$(head_num "$FRESH_RPC")
[ -n "${C_HEAD:-}" ] && [ "$C_HEAD" -ge "$TARGET" ] \
   && ok "fresh node replayed the whole chain including both sweeps (at $C_HEAD)" \
   || fail "fresh node stalled at ${C_HEAD:-none}, target $TARGET"

A_ID=$(node_rpc "$NODE_RPC" get_dynamic_global_properties '[]' | grep -oE '"head_block_id":"[0-9a-f]+"')
C_ID=$(node_rpc "$FRESH_RPC" get_dynamic_global_properties '[]' | grep -oE '"head_block_id":"[0-9a-f]+"')
[ -n "$A_ID" ] && [ "$A_ID" = "$C_ID" ] && ok "producer and fresh node agree on head id" \
                                        || fail "head id mismatch: $A_ID vs $C_ID"

for n in A C; do
   grep -q "Assertion" "$WORKDIR/$n.log" 2>/dev/null && fail "node $n hit an assertion"
   c=$(grep -ac "unlinkable" "$WORKDIR/$n.log" 2>/dev/null || true)
   [ "${c:-0}" -eq 0 ] || fail "node $n reported ${c} unlinkable blocks"
done
[ "$FAILED" -eq 0 ] && ok "no assertions or unlinkable blocks on either node"

echo
[ "$FAILED" -eq 0 ] && log "PASS" || log "FAILED"
exit "$FAILED"
