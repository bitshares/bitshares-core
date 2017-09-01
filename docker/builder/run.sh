#!/bin/sh
cd ${PWD}/"$(dirname $0)" &&
exec docker-compose \
  -p bitshares \
  run --rm builder
