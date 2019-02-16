# Docker Container

This repository comes with built-in Dockerfile to support docker
containers. This README serves as documentation.

## Dockerfile Specifications

The `Dockerfile` performs the following steps:

1. Obtain base image (phusion/baseimage:0.10.1)
2. Install required dependencies using `apt-get`
3. Add bitshares-core source code into container
4. Update git submodules
5. Perform `cmake` with build type `Release`
6. Run `make` and `make_install` (this will install binaries into `/usr/local/bin`
7. Purge source code off the container
8. Add a local bitshares user and set `$HOME` to `/var/lib/bitshares`
9. Make `/var/lib/bitshares` and `/etc/bitshares` a docker *volume*
10. Expose ports `8090` and `1776`
11. Add default config from `docker/default_config.ini` and entry point script
12. Run entry point script by default

The entry point simplifies the use of parameters for the `witness_node`
(which is run by default when spinning up the container).

### Supported Environmental Variables

* `$BITSHARESD_SEED_NODES`
* `$BITSHARESD_RPC_ENDPOINT`
* `$BITSHARESD_PLUGINS`
* `$BITSHARESD_REPLAY`
* `$BITSHARESD_RESYNC`
* `$BITSHARESD_P2P_ENDPOINT`
* `$BITSHARESD_WITNESS_ID`
* `$BITSHARESD_PRIVATE_KEY`
* `$BITSHARESD_TRACK_ACCOUNTS`
* `$BITSHARESD_PARTIAL_OPERATIONS`
* `$BITSHARESD_MAX_OPS_PER_ACCOUNT`
* `$BITSHARESD_ES_NODE_URL`
* `$BITSHARESD_TRUSTED_NODE`

### Default config

The default configuration is:

    p2p-endpoint = 0.0.0.0:9090
    rpc-endpoint = 0.0.0.0:8090
    bucket-size = [60,300,900,1800,3600,14400,86400]
    history-per-size = 1000
    max-ops-per-account = 1000
    partial-operations = true

# Docker Compose

With docker compose, multiple nodes can be managed with a single
`docker-compose.yaml` file:

    version: '3'
    services:
     main:
      # Image to run
      image: bitshares/bitshares-core:latest
      # 
      volumes:
       - ./docker/conf/:/etc/bitshares/
      # Optional parameters
      environment:
       - BITSHARESD_ARGS=--help


    version: '3'
    services:
     fullnode:
      # Image to run
      image: bitshares/bitshares-core:latest
      environment:
      # Optional parameters
      environment:
       - BITSHARESD_ARGS=--help
      ports:
       - "0.0.0.0:8090:8090"
      volumes:
      - "bitshares-fullnode:/var/lib/bitshares"


# Docker Hub

This container is properly registered with docker hub under the name:

* [bitshares/bitshares-core](https://hub.docker.com/r/bitshares/bitshares-core/)

Going forward, every release tag as well as all pushes to `develop` and
`testnet` will be built into ready-to-run containers, there.

# Docker Compose

One can use docker compose to setup a trusted full node together with a
delayed node like this:

```
version: '3'
services:

 fullnode:
  image: bitshares/bitshares-core:latest
  ports:
   - "0.0.0.0:8090:8090"
  volumes:
  - "bitshares-fullnode:/var/lib/bitshares"

 delayed_node:
  image: bitshares/bitshares-core:latest
  environment:
   - 'BITSHARESD_PLUGINS=delayed_node witness'
   - 'BITSHARESD_TRUSTED_NODE=ws://fullnode:8090'
  ports:
   - "0.0.0.0:8091:8090"
  volumes:
  - "bitshares-delayed_node:/var/lib/bitshares"
  links: 
  - fullnode

volumes:
 bitshares-fullnode:
```
