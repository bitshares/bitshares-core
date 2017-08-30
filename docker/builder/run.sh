#!/bin/sh
docker-compose -f docker/builder/docker-compose.yml -p bitshares run --rm builder
