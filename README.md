BitShares Core
==============

[BitShares Core](https://github.com/bitshares/bitshares-core) is the BitShares blockchain implementation and command-line interface.
The web browser based wallet is [BitShares UI](https://github.com/bitshares/bitshares-ui).

Visit [BitShares.org](https://bitshares.org/) to learn about BitShares and join the community at [BitSharesTalk.org](https://bitsharestalk.org/).

Information for developers can be found in the [Wiki](https://github.com/bitshares/bitshares-core/wiki) and the [BitShares Developer Portal](https://dev.bitshares.works/). Users interested in how BitShares works can go to the [BitShares Documentation](https://how.bitshares.works/) site.

Visit [Awesome BitShares](https://github.com/bitshares/awesome-bitshares) to find more resources and links E.G. chat groups, client libraries and extended APIs.

* [Getting Started](#getting-started)
* [Support](#support)
* [Using the API](#using-the-api)
* [Accessing restrictable node API's](#accessing-restrictable-node-apis)
* [FAQ](#faq)
* [License](#license)

|Branch|Build Status|
|---|---|
|`master`|[![](https://github.com/bitshares/bitshares-core/workflows/macOS/badge.svg?branch=master)](https://github.com/bitshares/bitshares-core/actions?query=workflow%3A"macOS"+branch%3Amaster) [![](https://github.com/bitshares/bitshares-core/workflows/Ubuntu%20Debug/badge.svg?branch=master)](https://github.com/bitshares/bitshares-core/actions?query=workflow%3A"Ubuntu+Debug"+branch%3Amaster) [![](https://github.com/bitshares/bitshares-core/workflows/Ubuntu%20Release/badge.svg?branch=master)](https://github.com/bitshares/bitshares-core/actions?query=workflow%3A"Ubuntu+Release"+branch%3Amaster) [![](https://github.com/bitshares/bitshares-core/workflows/Windows%20MinGW64/badge.svg?branch=master)](https://github.com/bitshares/bitshares-core/actions?query=workflow%3A"Windows+MinGW64"+branch%3Amaster)|
|`develop`|[![](https://github.com/bitshares/bitshares-core/workflows/macOS/badge.svg?branch=develop)](https://github.com/bitshares/bitshares-core/actions?query=workflow%3A"macOS"+branch%3Adevelop) [![](https://github.com/bitshares/bitshares-core/workflows/Ubuntu%20Debug/badge.svg?branch=develop)](https://github.com/bitshares/bitshares-core/actions?query=workflow%3A"Ubuntu+Debug"+branch%3Adevelop) [![](https://github.com/bitshares/bitshares-core/workflows/Ubuntu%20Release/badge.svg?branch=develop)](https://github.com/bitshares/bitshares-core/actions?query=workflow%3A"Ubuntu+Release"+branch%3Adevelop) [![](https://github.com/bitshares/bitshares-core/workflows/Windows%20MinGW64/badge.svg?branch=develop)](https://github.com/bitshares/bitshares-core/actions?query=workflow%3A"Windows+MinGW64"+branch%3Adevelop)|
|`hardfork`|[![](https://github.com/bitshares/bitshares-core/workflows/macOS/badge.svg?branch=hardfork)](https://github.com/bitshares/bitshares-core/actions?query=workflow%3A"macOS"+branch%3Ahardfork) [![](https://github.com/bitshares/bitshares-core/workflows/Ubuntu%20Debug/badge.svg?branch=hardfork)](https://github.com/bitshares/bitshares-core/actions?query=workflow%3A"Ubuntu+Debug"+branch%3Ahardfork) [![](https://github.com/bitshares/bitshares-core/workflows/Ubuntu%20Release/badge.svg?branch=hardfork)](https://github.com/bitshares/bitshares-core/actions?query=workflow%3A"Ubuntu+Release"+branch%3Ahardfork) [![](https://github.com/bitshares/bitshares-core/workflows/Windows%20MinGW64/badge.svg?branch=hardfork)](https://github.com/bitshares/bitshares-core/actions?query=workflow%3A"Windows+MinGW64"+branch%3Ahardfork)|
|`testnet`|[![](https://github.com/bitshares/bitshares-core/workflows/macOS/badge.svg?branch=testnet)](https://github.com/bitshares/bitshares-core/actions?query=workflow%3A"macOS"+branch%3Atestnet) [![](https://github.com/bitshares/bitshares-core/workflows/Ubuntu%20Debug/badge.svg?branch=testnet)](https://github.com/bitshares/bitshares-core/actions?query=workflow%3A"Ubuntu+Debug"+branch%3Atestnet) [![](https://github.com/bitshares/bitshares-core/workflows/Ubuntu%20Release/badge.svg?branch=testnet)](https://github.com/bitshares/bitshares-core/actions?query=workflow%3A"Ubuntu+Release"+branch%3Atestnet) [![](https://github.com/bitshares/bitshares-core/workflows/Windows%20MinGW64/badge.svg?branch=testnet)](https://github.com/bitshares/bitshares-core/actions?query=workflow%3A"Windows+MinGW64"+branch%3Atestnet)|
|`master` of `bitshares-fc`|[![](https://github.com/bitshares/bitshares-fc/workflows/macOS/badge.svg?branch=master)](https://github.com/bitshares/bitshares-fc/actions?query=workflow%3A"macOS"+branch%3Amaster) [![](https://github.com/bitshares/bitshares-fc/workflows/Ubuntu%20Debug/badge.svg?branch=master)](https://github.com/bitshares/bitshares-fc/actions?query=workflow%3A"Ubuntu+Debug"+branch%3Amaster) [![](https://github.com/bitshares/bitshares-fc/workflows/Ubuntu%20Release/badge.svg?branch=master)](https://github.com/bitshares/bitshares-fc/actions?query=workflow%3A"Ubuntu+Release"+branch%3Amaster)|

Getting Started
---------------
Build instructions and additional documentation are available in the
[Wiki](https://github.com/bitshares/bitshares-core/wiki).

Prebuilt binaries can be found in the [releases page](https://github.com/bitshares/bitshares-core/releases) for download.

### Build

We recommend building on Ubuntu 20.04 LTS (64-bit)

**Build Dependencies:**

    sudo apt-get update
    sudo apt-get install autoconf cmake make automake libtool git libboost-all-dev libssl-dev g++ libcurl4-openssl-dev doxygen

**Build Script:**

    git clone https://github.com/bitshares/bitshares-core.git
    cd bitshares-core
    git checkout master # may substitute "master" with current release tag
    git submodule update --init --recursive
    mkdir build
    cd build
    cmake -DCMAKE_BUILD_TYPE=Release ..
    make

**Upgrade Script:** (prepend to the Build Script above if you built a prior release):

    git remote set-url origin https://github.com/bitshares/bitshares-core.git
    git checkout master
    git remote set-head origin --auto
    git pull
    git submodule update --init --recursive # this command may fail
    git submodule sync --recursive
    git submodule update --init --recursive

**NOTE:**

* BitShares requires a 64-bit operating system to build, and will not build on a 32-bit OS. Tested operating systems:
  * Linux (heavily tested with Ubuntu LTS releases)
  * macOS (various versions)
  * Windows (various versions, Visual Studio and MinGW)
  * OpenBSD (various versions)

* BitShares requires [Boost](https://www.boost.org/) libraries to build, supports version `1.58` to `1.74`.
Newer versions may work, but have not been tested.
If your system came pre-installed with a version of Boost libraries that you do not wish to use, you may
manually build your preferred version and use it with BitShares by specifying it on the CMake command line.

  Example: `cmake -DBOOST_ROOT=/path/to/boost ..`

* BitShares requires [OpenSSL](https://www.openssl.org/) libraries to build, supports version `1.0.2` to `1.1.1`.
If your system came pre-installed with a version of OpenSSL libraries that you do not wish to use, you may
manually build your preferred version and use it with BitShares by specifying it on the CMake command line.

  Example: `cmake -DOPENSSL_ROOT_DIR=/path/to/openssl ..`


### Run the node software

**After Building**, the node software `witness_node` can be launched with:

    ./programs/witness_node/witness_node

The node will automatically create a `witness_node_data_dir` directory with some config files.
The blockchain data will be stored in the directory too.
It may take several hours to fully synchronize the blockchain.

You can exit the node using `Ctrl+C`. Please be aware that the node may need some time (usually a few minutes) to exit cleanly, please be patient.

**IMPORTANT:** By default the node will start in reduced memory mode by using some of the commands detailed in [Memory reduction for nodes](https://github.com/bitshares/bitshares-core/wiki/Memory-reduction-for-nodes).
In order to run a full node with all the account histories (which is usually not necessary) you need to remove `partial-operations` and `max-ops-per-account` from your config file. Please note that currently(2018-10-17) a full node will need more than 160GB of RAM to operate and required memory is growing fast. Consider the following table as **minimal requirements** before running a node:

| Default | Full | Minimal  | ElasticSearch
| --- | --- | --- | ---
| 150G HDD, 16G RAM | 640G SSD, 64G RAM * | 120G HDD, 4G RAM | 1TB SSD, 32G RAM

\* For this setup, allocate at least 500GB of SSD as swap.

To use the command-line wallet or other wallets / clients with the node, the node need to be started with RPC connection enabled, which can be done by starting the node with the `--rpc-endpoint` parameter, E.G.

    ./programs/witness_node/witness_node --rpc-endpoint=127.0.0.1:8090

or configure it in the config file by editing `witness_node_data_dir/config.ini` as follows:

    rpc-endpoint = 127.0.0.1:8090

You can run the program with `--help` parameter to see more info:

    ./programs/witness_node/witness_node --help


### Run the command-line wallet software

To start the command-line wallet, in a separate terminal you can run:

    ./programs/cli_wallet/cli_wallet

**IMPORTANT:** The cli_wallet or API interfaces to the witness node wouldn't be fully functional unless the witness node is fully synchronized with the blockchain. The cli_wallet command `info` will show result `head_block_age` which will tell you how far you are from the live current block of the blockchain.

To check your current block:

    new >>> info

To query the blockchain, E.G. get info about an account:

    new >>> get_account <account_name_or_id>

If you need to transact with your account but not only query, firstly set your initial password and unlock the wallet:

* For non-Windows operating systems, you can type the commands and press `[ENTER]`, then input the password and press `[ENTER]`, in this case the password won't show:

      new >>> set_password [ENTER]
      Enter password:
      locked >>> unlock [ENTER]
      Enter password:
      unlocked >>>

* For Windows, or you'd like to show the password, type the commands with the password:

      new >>> set_password <PASSWORD>
      locked >>> unlock <PASSWORD>
      unlocked >>>

To be able to transact with your account, import the corresponding private keys:

    unlocked >>> import_key <ACCOUNT_NAME> <WIF_KEY>

The private keys will be encrypted and stored in the wallet file, the file name is `wallet.json` by default.
The private keys are accessible when the wallet is unlocked.

    unlocked >>> dump_private_keys

Use `lock` command to make the private keys inaccessible. There is no auto-lock feature so far.

    unlocked >>> lock

To import your initial (genesis) balances, import the private keys corresponding to the balances:

    unlocked >>> import_balance <ACCOUNT_NAME> [<WIF_KEY> ...] true

Use `help` to see all available wallet commands.

    >>> help

Use `gethelp <COMMAND>` to see more info about individual commands. E.G.

    >>> gethelp get_order_book

The definition of all commands is available in the
[wallet.hpp](https://github.com/bitshares/bitshares-core/blob/master/libraries/wallet/include/graphene/wallet/wallet.hpp) souce code file.
Corresponding documentation can be found in the [Doxygen documentation](https://doxygen.bitshares.org/classgraphene_1_1wallet_1_1wallet__api.html).

You can run the program with `--help` parameter to see more info:

    ./programs/cli_wallet/cli_wallet --help

There is also some info in the [Wiki](https://github.com/bitshares/bitshares-core/wiki/CLI-Wallet-Cookbook).


Support
-------

Technical support is available in the [BitSharesTalk technical support subforum](https://bitsharestalk.org/index.php?board=45.0).

BitShares Core bugs can be reported directly to the [issue tracker](https://github.com/bitshares/bitshares-core/issues).

Questions can be posted in [Github Discussions](https://github.com/bitshares/bitshares-core/discussions).

BitShares UI bugs should be reported to the [UI issue tracker](https://github.com/bitshares/bitshares-ui/issues).

Up to date online Doxygen documentation can be found at [Doxygen.BitShares.org](https://doxygen.bitshares.org/hierarchy.html).


Using the API
-------------

### Node API

The `witness_node` software provides several different API's, known as *node API*.

Each API has its own ID and a name.
When running `witness_node` with RPC connection enabled, initially two API's are available:
* API 0 has name *"database"*, it provides read-only access to the database,
* API 1 has name *"login"*, it is used to login and gain access to additional, restrictable API's.

Here is an example using `wscat` package from `npm` for websockets:

    $ npm install -g wscat
    $ wscat -c ws://127.0.0.1:8090
    > {"id":1, "method":"call", "params":[0,"get_accounts",[["1.2.0"]]]}
    < {"id":1,"result":[{"id":"1.2.0","annotations":[],"membership_expiration_date":"1969-12-31T23:59:59","registrar":"1.2.0","referrer":"1.2.0","lifetime_referrer":"1.2.0","network_fee_percentage":2000,"lifetime_referrer_fee_percentage":8000,"referrer_rewards_percentage":0,"name":"committee-account","owner":{"weight_threshold":1,"account_auths":[],"key_auths":[],"address_auths":[]},"active":{"weight_threshold":6,"account_auths":[["1.2.5",1],["1.2.6",1],["1.2.7",1],["1.2.8",1],["1.2.9",1],["1.2.10",1],["1.2.11",1],["1.2.12",1],["1.2.13",1],["1.2.14",1]],"key_auths":[],"address_auths":[]},"options":{"memo_key":"GPH1111111111111111111111111111111114T1Anm","voting_account":"1.2.0","num_witness":0,"num_committee":0,"votes":[],"extensions":[]},"statistics":"2.7.0","whitelisting_accounts":[],"blacklisting_accounts":[]}]}

We can do the same thing using an HTTP client such as `curl` for API's which do not require login or other session state:

    $ curl --data '{"jsonrpc": "2.0", "method": "call", "params": [0, "get_accounts", [["1.2.0"]]], "id": 1}' http://127.0.0.1:8090/
    {"id":1,"result":[{"id":"1.2.0","annotations":[],"membership_expiration_date":"1969-12-31T23:59:59","registrar":"1.2.0","referrer":"1.2.0","lifetime_referrer":"1.2.0","network_fee_percentage":2000,"lifetime_referrer_fee_percentage":8000,"referrer_rewards_percentage":0,"name":"committee-account","owner":{"weight_threshold":1,"account_auths":[],"key_auths":[],"address_auths":[]},"active":{"weight_threshold":6,"account_auths":[["1.2.5",1],["1.2.6",1],["1.2.7",1],["1.2.8",1],["1.2.9",1],["1.2.10",1],["1.2.11",1],["1.2.12",1],["1.2.13",1],["1.2.14",1]],"key_auths":[],"address_auths":[]},"options":{"memo_key":"GPH1111111111111111111111111111111114T1Anm","voting_account":"1.2.0","num_witness":0,"num_committee":0,"votes":[],"extensions":[]},"statistics":"2.7.0","whitelisting_accounts":[],"blacklisting_accounts":[]}]}

When using an HTTP client, the API ID can be replaced by the API name, E.G.

    $ curl --data '{"jsonrpc": "2.0", "method": "call", "params": ["database", "get_accounts", [["1.2.0"]]], "id": 1}' http://127.0.0.1:8090/

The definition of all node API's is available in the source code files including
[database_api.hpp](https://github.com/bitshares/bitshares-core/blob/master/libraries/app/include/graphene/app/database_api.hpp)
and [api.hpp](https://github.com/bitshares/bitshares-core/blob/master/libraries/app/include/graphene/app/api.hpp).
Corresponding documentation can be found in Doxygen:
* [database API](https://doxygen.bitshares.org/classgraphene_1_1app_1_1database__api.html)
* [other API's](https://doxygen.bitshares.org/namespacegraphene_1_1app.html)


### Wallet API

The `cli_wallet` program can also be configured to serve **all of its commands** as API's, known as *wallet API*.

Start `cli_wallet` with RPC connection enabled:

    $ ./programs/cli_wallet/cli_wallet --rpc-endpoint=127.0.0.8091

Access the wallet API using an HTTP client:

    $ curl --data '{"jsonrpc": "2.0", "method": "info", "params": [], "id": 1}' http://127.0.0.1:8091/rpc
    $ curl --data '{"jsonrpc": "2.0", "method": "get_account", "params": ["1.2.0"], "id": 1}' http://127.0.0.1:8091/rpc

Note: The syntax to access wallet API is a bit different than accessing node API.

**Important:**
* When RPC connection is enabled for `cli_wallet`, sensitive data E.G. private keys which is accessible via commands will be accessible via RPC too. It is recommended that only open network connection to localhost or trusted addresses E.G. configure a firewall.
* When using wallet API, sensitive data E.G. the wallet password and private keys is transmitted as plain text, thus may be vulnerable to network sniffing. It is recommended that only use wallet API with localhost, or in a clean network, and / or use `--rpc-tls-endpoint` parameter to only serve wallet API via secure connections.


Accessing restrictable node API's
---------------------------------

You can restrict node API's to particular users by specifying an `api-access` file in `config.ini`
or by using the `--api-access /full/path/to/api-access.json` startup node command.  Here is an example `api-access` file which allows
user `bytemaster` with password `supersecret` to access four different API's, while allowing any other user to access the three public API's
necessary to use the node:

    {
       "permission_map" :
       [
          [
             "bytemaster",
             {
                "password_hash_b64" : "9e9GF7ooXVb9k4BoSfNIPTelXeGOZ5DrgOYMj94elaY=",
                "password_salt_b64" : "INDdM6iCi/8=",
                "allowed_apis" : ["database_api", "network_broadcast_api", "history_api", "network_node_api"]
             }
          ],
          [
             "*",
             {
                "password_hash_b64" : "*",
                "password_salt_b64" : "*",
                "allowed_apis" : ["database_api", "network_broadcast_api", "history_api"]
             }
          ]
       ]
    }

Passwords are stored in `base64` as salted `sha256` hashes.  A simple Python script,
[`saltpass.py`](https://github.com/bitshares/bitshares-core/blob/master/programs/witness_node/saltpass.py)
is avaliable to obtain hash and salt values from a password.
A single asterisk `"*"` may be specified as username or password hash to accept any value.

With the above configuration, here is an example of how to call `add_node` from the `network_node` API:

    {"id":1, "method":"call", "params":[1,"login",["bytemaster", "supersecret"]]}
    {"id":2, "method":"call", "params":[1,"network_node",[]]}
    {"id":3, "method":"call", "params":[2,"add_node",["127.0.0.1:9090"]]}

Note, the call to `network_node` is necessary to obtain the correct API identifier for the network API.  It is not guaranteed that the network API identifier will always be `2`.

The restricted API's are accessible via HTTP too using *basic access authentication*. E.G.

    $ curl --data '{"jsonrpc": "2.0", "method": "call", "params": ["network_node", "add_node", ["127.0.0.1:9090"]], "id": 1}' http://bytemaster:supersecret@127.0.0.1:8090/

Our `doxygen` documentation contains the most up-to-date information
about API's for the [node](https://doxygen.bitshares.org/namespacegraphene_1_1app.html) and the
[wallet](https://doxygen.bitshares.org/classgraphene_1_1wallet_1_1wallet__api.html).


FAQ
---

- Is there a way to generate help with parameter names and method descriptions?

    Yes. Documentation of the code base, including APIs, can be generated using Doxygen. Simply run `doxygen` in this directory.

    If both Doxygen and perl are available in your build environment, the CLI wallet's `help` and `gethelp`
    commands will display help generated from the doxygen documentation.

    If your CLI wallet's `help` command displays descriptions without parameter names like
        `signed_transaction transfer(string, string, string, string, string, bool)`
    it means CMake was unable to find Doxygen or perl during configuration.  If found, the
    output should look like this:
        `signed_transaction transfer(string from, string to, string amount, string asset_symbol, string memo, bool broadcast)`

- Is there a way to allow external program to drive `cli_wallet` via websocket, JSONRPC, or HTTP?

    Yes. External programs may connect to the CLI wallet and make its calls over a websockets API. To do this, run the wallet in
    server mode, i.e. `cli_wallet -s "127.0.0.1:9999"` and then have the external program connect to it over the specified port
    (in this example, port 9999). Please check the ["Using the API"](#using-the-api) section for more info.

- Is there a way to access methods which require login over HTTP?

    Yes. Most of the methods can be accessed by specifying the API name instead of an API ID. If an API is protected by a username and a password, it can be accessed by using *basic access authentication*. Please check the ["Accessing restrictable node API's"](#accessing-restrictable-node-apis) section for more info.

    However, HTTP is not really designed for "server push" notifications, and we would have to figure out a way to queue notifications for a polling client. Websockets solves this problem. If you need to access the stateful methods, use Websockets.

- What is the meaning of `a.b.c` numbers?

    The first number specifies the *space*.  Space 1 is for protocol objects, 2 is for implementation objects.
    Protocol space objects can appear on the wire, for example in the binary form of transactions.
    Implementation space objects cannot appear on the wire and solely exist for implementation
    purposes, such as optimization or internal bookkeeping.

    The second number specifies the *type*.  The type of the object determines what fields it has.  For a
    complete list of type ID's, see `GRAPHENE_DEFINE_IDS(protocol, protocol_ids ...)` in
    [protocol/types.hpp](https://github.com/bitshares/bitshares-core/blob/master/libraries/protocol/include/graphene/protocol/types.hpp)
    and `GRAPHENE_DEFINE_IDS(chain, implementation_ids ...)` in [chain/types.hpp](https://github.com/bitshares/bitshares-core/blob/master/libraries/chain/include/graphene/chain/types.hpp).

    The third number specifies the *instance*.  The instance of the object is different for each individual
    object.

- The answer to the previous question was really confusing.  Can you make it clearer?

    All account ID's are of the form `1.2.x`.  If you were the 9735th account to be registered,
    your account's ID will be `1.2.9735`.  Account `0` is special (it's the "committee account,"
    which is controlled by the committee members and has a few abilities and restrictions other accounts
    do not).

    All asset ID's are of the form `1.3.x`.  If you were the 29th asset to be registered,
    your asset's ID will be `1.3.29`.  Asset `0` is special (it's BTS, which is considered the "core asset").

    The first and second number together identify the kind of thing you're talking about (`1.2` for accounts,
    `1.3` for assets).  The third number identifies the particular thing.

- How do I get the `network_add_nodes` command to work?  Why is it so complicated?

    You need to follow the instructions in the ["Accessing restrictable node API's"](#accessing-restrictable-node-apis) section to
    allow a username/password access to the `network_node` API.  Then you need
    to pass the username/password to the `cli_wallet` on the command line.

    It's set up this way so that the default configuration is secure even if the RPC port is
    publicly accessible.  It's fine if your `witness_node` allows the general public to query
    the database or broadcast transactions (in fact, this is how the hosted web UI works).  It's
    less fine if your `witness_node` allows the general public to control which p2p nodes it's
    connecting to.  Therefore the API to add p2p connections needs to be set up with proper access
    controls.

License
-------
BitShares Core is under the MIT license. See [LICENSE](https://github.com/bitshares/bitshares-core/blob/master/LICENSE.txt)
for more information.
