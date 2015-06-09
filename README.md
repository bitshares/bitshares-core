Intro for new developers
------------------------

This is a quick introduction to get new developers up to speed on Graphene.

Starting Graphene
-----------------

    git clone https://gitlab.bitshares.org/dlarimer/graphene
    cd graphene
    git submodule update --init --recursive
    cmake -DCMAKE_BUILD_TYPE=Debug .
    make
    ./programs/witness_node/witness_node

This will launch the witness node. If you would like to launch the command-line wallet, you must first specify a port for communication with the witness node. To do this, add text to `witness_node_data_dir/config.ini` as follows, then restart the node:

    rpc-endpoint = 127.0.0.1:8090

Then, in a separate terminal window, start the command-line wallet `cli_wallet`:

    ./programs/cli_wallet/cli_wallet

If you send private keys over this connection, `rpc-endpoint` should be bound to localhost for security.

A list of CLI wallet commands is available [here](https://bitshares.github.io/doxygen/classgraphene_1_1wallet_1_1wallet__api.html).

Code coverage testing
---------------------

TODO:  Write something here

Unit testing
------------

We use the Boost unit test framework for unit testing.  Most unit
tests reside in the `chain_test` build target.

Core mechanics
--------------

- Witnesses
- Key members
- Price feeds
- Global parameters
- Voting on witnesses
- Voting on key members
- Witness pay
- Transfers
- Markets
- Escrow
- Recurring payments

Gotchas
-------

- Key objects can actually contain a key or address

Witness node
------------

The role of the witness node is to broadcast transactions, download blocks, and optionally sign them.

TODO:  How do you get block signing keys into the witness node?

How to use fc async to do recurring tasks
-----------------------------------------

    _my_task = fc::async( callable, "My Task" );
    _my_task = fc::schedule( callable, "My Task 2", exec_time );

Stuff to know about the code
----------------------------

`static_variant<t1, t2>` is a *union type* which says "this variable may be either t1 or t2."  It is serializable if t1 and t2 are both serializable.

The file `operations.hpp` documents the available operations, and `database_fixture.hpp` is a good reference for building and submitting transactions for processing.

Tests also show the way to do many things, but are often cluttered with code that generates corner cases to try to break things in every possible way.

Visitors are at the end of `operations.hpp` after the large typedef for `operation` as a `static_variant`.  TODO:  They should be refactored into a separate header.

Downcasting stuff
-----------------

- You have an `object_id_type` and want to downcast it to a `key_id_type` : `key_id_type( object_id )`
- You have an `operation_result` and want to downcast it to an `object_id_type` : `op_result.get<object_id_type>()`
- Since `operation_result` is a `static_variant`, the above is also how you downcast `static_variant`

Running specific tests
----------------------

- `tests/chain_tests -t block_tests/name_of_test`

Debugging FC exceptions with GDB
--------------------------------

- `catch throw`

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
