# BitShares Libraries

The libraries are the core of the project and defines everything where applications can build on top.

A **graphene** blockchain software will use the `app` library to define what the application will do, what services it will offer. The blockchain is defined by the `chain` library and include all the objects, types, operations, protocols that builds current consensus blockchain. The lowest level in memory database of Bitshares is developed at the `db` library. The `fc` is a helper module broadly used in the libraries code, `egenesis` will help with the genesis file, `plugins` will be loaded optionally to the application. Wallet software like the cli_wallet will benefit from the `wallet` library.

Code in libraries is the most important part of **bitshares-core** project and it is maintained by the Bitshares Core Team and contributors.
# Available Libraries

Folder | Name | Description | Category | Status
---|---|---|---|---
[app](app) | Application | Bundles component libraries (chain, network, plugins) into a useful application. Also provides API access. | Library | Active 
[chain](chain) | Blockchain | Define all objects, operations and types. This include the consensus protocol, defines the whole blockchain behaviour. | Library | Active 
[db](db) | Database | Define the internal database graphene uses. | Library | Active 
[egenesis](egenesis) | Genesis |  | Library | Active
[fc](fc) | Fast-compiling C++ library | https://github.com/bitshares/bitshares-fc | Library | Active 
[net](net) | Network | The graphene p2p layer. | Library | Active 
[plugins](plugins) | Plugins | All plugin modules are stored here. | Library | Active 
[utilities](utilities) | Network | Provide common utility calls used in applications or other libraries. | Library | Active 
[wallet](wallet) | Wallet | Wallet definition for the `cli_wallet` software. | Library | Active
