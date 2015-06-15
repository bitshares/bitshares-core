Intro for new developers
------------------------

This is a quick introduction to get new developers up to speed on Graphene.

Starting Graphene
-----------------

For Ubuntu 14.04 LTS users, see this link first:
    https://github.com/cryptonomex/graphene/wiki/build-ubuntu
    
and then proceed with:

    git clone https://github.com/cryptonomex/graphene.git
    cd graphene
    git submodule update --init --recursive
    cmake -DCMAKE_BUILD_TYPE=Debug .
    make
    ./programs/witness_node/witness_node

This will launch the witness node. If you would like to launch the command-line wallet, you must first specify a port for communication with the witness node. To do this, add text to `witness_node_data_dir/config.ini` as follows, then restart the node:

    rpc-endpoint = 127.0.0.1:8090

Then, in a separate terminal window, start the command-line wallet `cli_wallet`:

    ./programs/cli_wallet/cli_wallet

To set your iniital password to 'password' use:

    >>> set_password password
    >>> unlock password

If you send private keys over this connection, `rpc-endpoint` should be bound to localhost for security.

A list of CLI wallet commands is available [here](https://bitshares.github.io/doxygen/classgraphene_1_1wallet_1_1wallet__api.html).

Code coverage testing
---------------------

TODO:  Write something here

Unit testing
------------

We use the Boost unit test framework for unit testing.  Most unit
tests reside in the `chain_test` build target.

Witness node
------------

The role of the witness node is to broadcast transactions, download blocks, and optionally sign them.

./witness_node --rpc-endpoint "127.0.0.1:8090" --enable-stale-production  -w \""1.7.0"\" \""1.7.1"\" \""1.7.2"\" \""1.7.3"\" \""1.7.4"\" --private-key "[\"1.2.0\",\"aeebad4a796fcc2e15dc4c6061b45ed9b373f26adfc798ca7d2d8cc58182718e\"]"

Running specific tests
----------------------

- `tests/chain_tests -t block_tests/name_of_test`

Using the API
-------------

We provide several different API's.  Each API has its own ID.
When running `witness_node`, initially two API's are available:
API 0 provides read-only access to the database, while API 1 is
used to login and gain access to additional, restricted API's.

Here is an example using `wscat` package from `npm` for websockets:

    $ npm install -g wscat
    $ wscat -c ws://127.0.0.1:8090
    > {"id":1, "method":"call", "params":[0,"get_accounts",[["1.3.0"]]]}
    < {"id":1,"result":[{"id":"1.3.0","annotations":[],"registrar":"1.3.0","referrer":"1.3.0","referrer_percent":0,"name":"genesis","owner":{"weight_threshold":1,"auths":[["1.2.0",1]]},"active":{"weight_threshold":1,"auths":[["1.2.0",1]]},"memo_key":"1.2.0","voting_account":"1.3.0","num_witness":0,"num_committee":0,"votes":[],"statistics":"2.7.0","whitelisting_accounts":[],"blacklisting_accounts":[]}]}
    $

We can do the same thing using an HTTP client such as `curl` for API's which do not require login or other session state:

    $ curl --data '{"jsonrpc": "2.0", "method": "call", "params": [0, "get_accounts", [["1.3.0"]]], "id": 1}' http://127.0.0.1:8090/rpc
    {"id":1,"result":[{"id":"1.3.0","annotations":[],"registrar":"1.3.0","referrer":"1.3.0","referrer_percent":0,"name":"genesis","owner":{"weight_threshold":1,"auths":[["1.2.0",1]]},"active":{"weight_threshold":1,"auths":[["1.2.0",1]]},"memo_key":"1.2.0","voting_account":"1.3.0","num_witness":0,"num_committee":0,"votes":[],"statistics":"2.7.0","whitelisting_accounts":[],"blacklisting_accounts":[]}]}    

API 0 is accessible using regular JSON-RPC:

    $ curl --data '{"jsonrpc": "2.0", "method": "get_accounts", "params": [["1.3.0"]], "id": 1}' http://127.0.0.1:8090/rpc

You can use the login API to obtain `network`, `database` and `history` API's.  Here is an example of how to call `add_node` from the `network` API:

    {"id":1, "method":"call", "params":[1,"login",["bytemaster", "supersecret"]]}
    {"id":2, "method":"call", "params":[1,"network",[]]}
    {"id":3, "method":"call", "params":[2,"add_node",["127.0.0.1:9090"]]}

Note, the call to `network` is necessary to obtain the correct API identifier for the network API.  It is not guaranteed that the network API identifier will always be `2`.

Since the `network` API requires login, it is only accessible over the websocket RPC.  Our `doxygen` documentation contains the most up-to-date information
about API's for the [witness node](https://bitshares.github.io/doxygen/namespacegraphene_1_1app.html) and the
[wallet](https://bitshares.github.io/doxygen/classgraphene_1_1wallet_1_1wallet__api.html).
If you want information which is not available from an API, it might be available
from the [database](https://bitshares.github.io/doxygen/classgraphene_1_1chain_1_1database.html);
it is fairly simple to write API methods to expose database methods.

Running private testnet
-----------------------

Normally `witness_node` assumes it won't be producing blocks from
genesis, or against very old chain state.  We need to get `witness_node`
to discard this assumption if we actually want to start a new chain,
so we will need to specify in `config.ini`:

    enable-stale-production = true

We also need to specify which witnesses will produce blocks locally;
`witness_node` does not assume that it should produce blocks for a given
witness just because it has the correct private key to do so.  There are
ten witnesses at genesis of the testnet, block production can be
enabled for all of them by specifying multiple times in `config.ini`:

    witness-id = "1.7.0"
    witness-id = "1.7.1"
    witness-id = "1.7.2"
    witness-id = "1.7.3"
    witness-id = "1.7.4"
    witness-id = "1.7.5"
    witness-id = "1.7.6"
    witness-id = "1.7.7"
    witness-id = "1.7.8"
    witness-id = "1.7.9"

Questions
---------

- Is there a way to generate help with parameter names and method descriptions?

    Yes. Documentation of the code base, including APIs, can be generated using Doxygen. Simply run `doxygen` in this directory.
    We are thinking of integrating Doxygen's XML output format to provide a better `help` command to the CLI wallet.

- Is there a way to allow external program to drive `cli_wallet` via websocket, JSONRPC, or HTTP?

    Yes. External programs may connect to the CLI wallet and make its calls over a websockets API. To do this, run the wallet in
    server mode, i.e. `cli_wallet -s "127.0.0.1:9999"` and then have the external program connect to it over the specified port
    (in this example, port 9999).

- Is there a way to access methods which require login over HTTP?

    No.  Login is inherently a stateful process (logging in changes what the server will do for certain requests, that's kind
    of the point of having it).  If you need to track state across HTTP RPC calls, you must maintain a session across multiple
    connections.  This is a famous source of security vulnerabilities for HTTP applications.  Additionally, HTTP is not really
    designed for "server push" notifications, and we would have to figure out a way to queue notifications for a polling client.

    Websockets solves all these problems.  If you need to access Graphene's stateful methods, you need to use Websockets.

- What is the meaning of `a.b.c` numbers?

    The first number specifies the *space*.  Space 1 is for protocol objects, 2 is for implementation objects.
    Protocol space objects can appear on the wire, for example in the binary form of transactions.
    Implementation space objects cannot appear on the wire and solely exist for implementation
    purposes, such as optimization or internal bookkeeping.

    The second number specifies the *type*.  The type of the object determines what fields it has.  For a
    complete list of type ID's, see `enum object_type` and `enum impl_object_type` in
    [types.hpp](https://github.com/cryptonomex/graphene/blob/master/libraries/chain/include/graphene/chain/types.hpp).

    The third number specifies the *instance*.  The instance of the object is different for each individual
    object.

- The answer to the previous question was really confusing.  Can you make it clearer?

    All account ID's are of the form `1.3.x`.  If you were the 9735th account to be registered,
    your account's ID will be `1.3.9735`.  Account `0` is special (it's the "genesis account,"
    which is controlled by the delegates and has a few abilities and restrictions other accounts
    do not).

    All asset ID's are of the form `1.4.x`.  If you were the 29th asset to be registered,
    your asset's ID will be `1.4.29`.  Asset `0` is special (it's BTS, which is considered the "core asset").

    The first and second number together identify the kind of thing you're talking about (`1.3` for accounts,
    `1.4` for assets).  The third number identifies the particular thing.
