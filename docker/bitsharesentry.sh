#!/bin/bash
BITSHARESD="/usr/local/bin/witness_node"

# For blockchain download
VERSION=`cat /etc/bitshares/version`

## Supported Environmental Variables
#
#   * $BITSHARESD_SEED_NODES
#   * $BITSHARESD_RPC_ENDPOINT
#   * $BITSHARESD_PLUGINS
#   * $BITSHARESD_REPLAY
#   * $BITSHARESD_RESYNC
#   * $BITSHARESD_P2P_ENDPOINT
#   * $BITSHARESD_WITNESS_ID
#   * $BITSHARESD_PRIVATE_KEY
#   * $BITSHARESD_TRACK_ACCOUNTS
#   * $BITSHARESD_PARTIAL_OPERATIONS
#   * $BITSHARESD_MAX_OPS_PER_ACCOUNT
#   * $BITSHARESD_ES_NODE_URL
#   * $BITSHARESD_TRUSTED_NODE
#

ARGS=""
# Translate environmental variables
if [[ ! -z "$BITSHARESD_SEED_NODES" ]]; then
    for NODE in $BITSHARESD_SEED_NODES ; do
        ARGS+=" --seed-node=$NODE"
    done
fi
if [[ ! -z "$BITSHARESD_RPC_ENDPOINT" ]]; then
    ARGS+=" --rpc-endpoint=${BITSHARESD_RPC_ENDPOINT}"
fi

if [[ ! -z "$BITSHARESD_REPLAY" ]]; then
    ARGS+=" --replay-blockchain"
fi

if [[ ! -z "$BITSHARESD_RESYNC" ]]; then
    ARGS+=" --resync-blockchain"
fi

if [[ ! -z "$BITSHARESD_P2P_ENDPOINT" ]]; then
    ARGS+=" --p2p-endpoint=${BITSHARESD_P2P_ENDPOINT}"
fi

if [[ ! -z "$BITSHARESD_WITNESS_ID" ]]; then
    ARGS+=" --witness-id=$BITSHARESD_WITNESS_ID"
fi

if [[ ! -z "$BITSHARESD_PRIVATE_KEY" ]]; then
    ARGS+=" --private-key=$BITSHARESD_PRIVATE_KEY"
fi

if [[ ! -z "$BITSHARESD_TRACK_ACCOUNTS" ]]; then
    for ACCOUNT in $BITSHARESD_TRACK_ACCOUNTS ; do
        ARGS+=" --track-account=$ACCOUNT"
    done
fi

if [[ ! -z "$BITSHARESD_PARTIAL_OPERATIONS" ]]; then
    ARGS+=" --partial-operations=${BITSHARESD_PARTIAL_OPERATIONS}"
fi

if [[ ! -z "$BITSHARESD_MAX_OPS_PER_ACCOUNT" ]]; then
    ARGS+=" --max-ops-per-account=${BITSHARESD_MAX_OPS_PER_ACCOUNT}"
fi

if [[ ! -z "$BITSHARESD_ES_NODE_URL" ]]; then
    ARGS+=" --elasticsearch-node-url=${BITSHARESD_ES_NODE_URL}"
fi

if [[ ! -z "$BITSHARESD_TRUSTED_NODE" ]]; then
    ARGS+=" --trusted-node=${BITSHARESD_TRUSTED_NODE}"
fi

## Link the bitshares config file into home
## This link has been created in Dockerfile, already
ln -f -s /etc/bitshares/config.ini /var/lib/bitshares

# Plugins need to be provided in a space-separated list, which
# makes it necessary to write it like this
if [[ ! -z "$BITSHARESD_PLUGINS" ]]; then
   exec $BITSHARESD --data-dir ${HOME} ${ARGS} ${BITSHARESD_ARGS} --plugins "${BITSHARESD_PLUGINS}"
else
   exec $BITSHARESD --data-dir ${HOME} ${ARGS} ${BITSHARESD_ARGS}
fi
