# BitShares Libraries

The libraries are the core of the project and defines everything where applications can build on top.

A **graphene** blockchain software will use the `app` library to define what the application will do, what services it will offer. The blockchain is defined by the `chain` library and include all the objects, types, operations, protocols that builds current consensus blockchain. The lowest level in memory database of Bitshares is developed at the `db` library. The `fc` is a helper module broadly used in the libraries code, `egenesis` will help with the genesis file, `plugins` will be loaded optionally to the application. Wallet software like the cli_wallet will benefit from the `wallet` library.

Code in libraries is the most important part of **bitshares-core** project and it is maintained by the Bitshares Core Team and contributors.
# Available Libraries

Folder | Name | Description | Status
---|---|---|---
[app](app) | Application | Bundles component libraries (chain, network, plugins) into a useful application. Also provides API access. | Active 
[chain](chain) | Blockchain | Defines all objects, operations and types. This include the consensus protocol, defines the whole blockchain behaviour. | Active 
[db](db) | Database | Defines the internal database graphene uses. | Active 
[egenesis](egenesis) | Genesis | Hardcodes the `genesis.json` file into the `witness_node` executable.| Active
[fc](fc) | Fast-compiling C++ library | https://github.com/bitshares/bitshares-fc | Active 
[net](net) | Network | The graphene p2p layer. | Active 
[plugins](plugins) | Plugins | Collection of singleton designed modules used for extending the bitshares-core.  | Active 
[utilities](utilities) | Utilities | Common utility calls used in applications or other libraries. | Active 
[wallet](wallet) | Wallet | Wallet definition for the `cli_wallet` software. | Active
