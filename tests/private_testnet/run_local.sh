#!/bin/sh
# Run confidential-transfer tests on a local private chain.
# This script does not dial BitShares mainnet and will not start a node
# whose genesis chain id is the public mainnet id.
set -eu

MAINNET_CHAIN_ID=4018d7844c78f6a6c41c6a552b898022310fc5dec06da467ee7905a8dad512c8
ROOT=$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd)
BUILD=${BUILD:-"$ROOT/build-release"}

if [ "${1:-}" = "--help" ] || [ "${1:-}" = "" ]; then
  echo "Usage: $0 chain-test | replay-check GENESIS.json" >&2
  echo "chain-test runs the in-process two-step replay (pop_block/push_block) inside chain_test." >&2
  echo "replay-check only accepts a genesis file that does not contain the mainnet chain id." >&2
  exit 2
fi

if [ "$1" = "chain-test" ]; then
  test -x "$BUILD/tests/chain_test"
  exec "$BUILD/tests/chain_test" --run_test=confidential_ct_tests --log_level=message
fi

if [ "$1" = "replay-check" ]; then
  genesis=$2
  test -f "$genesis"
  if grep -q "$MAINNET_CHAIN_ID" "$genesis"; then
    echo "refusing: genesis contains the BitShares mainnet chain id" >&2
    exit 1
  fi
  echo "genesis is not the embedded mainnet id. Do not add seed nodes."
  echo "If you start witness_node yourself, bind p2p and rpc to 127.0.0.1 only and pass --seed-nodes '[]'."
  exit 0
fi

echo "unknown command" >&2
exit 2
