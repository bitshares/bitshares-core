#!/bin/bash
BITSHARESD="/usr/local/bin/witness_node"

# For blockchain download
VERSION=`cat /etc/bitshares/version`

## seed nodes come from doc/seednodes.txt which is
## installed by docker into /etc/bitsharesd/seednodes.txt
# SEED_NODES="$(cat /etc/bitsharesd/seednodes.txt | awk -F' ' '{print $1}')"

## if user did not pass in any desired
## seed nodes, use the ones above:
#if [[ -z "$BITSHARESD_SEED_NODES" ]]; then
#    for NODE in $SEED_NODES ; do
#        ARGS+=" --seed-node=$NODE"
#    done
#fi

## Link the bitshares config file into home
## This link has been created in Dockerfile, already
#ln -f -s /etc/bitshares/config.ini /var/lib/bitshares

## get blockchain state from an S3 bucket
# echo bitsharesd: beginning download and decompress of s3://$S3_BUCKET/blockchain-$VERSION-latest.tar.bz2

## get blockchain state from an S3 bucket
#s3cmd get s3://$S3_BUCKET/blockchain-$VERSION-latest.tar.bz2 - | pbzip2 -m2000dc | tar x
#if [[ $? -ne 0 ]]; then
#    echo unable to pull blockchain state from S3 - exiting
#    exit 1
#fi

## Deploy Healthcheck daemon

$BITSHARESD --data-dir ${HOME} ${ARGS} ${BITSHARESD_ARGS}
