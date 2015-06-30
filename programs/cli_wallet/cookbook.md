# Graphene CLI Wallet Cookbook
### Running a Local Test Network

Right now, there is no public testnet, so the only way to test is to run your
own private network.  To do this, launch a witness node to generate blocks.  In
the directory where you built your graphene distribution:

````
cd programs/witness_node
# if you have previously run a witness node, you may need to remove the old blockchain.
# at this early stage, new commits often make it impossible to reuse an old database
#   rm -r witness_node_data_dir
./witness_node --rpc-endpoint --enable-stale-production --witness-id \""1.7.0"\" \""1.7.1"\" \""1.7.2"\" \""1.7.3"\" \""1.7.4"\"  \""1.7.5"\" \""1.7.6"\" \""1.7.7"\" \""1.7.8"\" \""1.7.9"\" --private-key "[\"1.2.0\",\"5KQwrPbwdL6PhXujxW37FSSQZ1JiwsST4cqQzDeyXtP79zkvFD3\"]"
````
The initial genesis state has ten pre-configured delegates (1.7.0-9) that all
use the same private key to sign their blocks.  Launching `witness_node` this
way allows you to act as all ten delegates.

Now, in a second window, launch a `cli_wallet` process to interact with the
network.
```
cd programs/cli_wallet
# similarly, if you have previously run a wallet, you may need to wipe out your 
# old wallet
#    rm wallet.json
./cli_wallet
```
Before doing anything with the new wallet, set a password and unlock the
wallet.  

*Warning*: your passwords will be displayed on the screen.
```
new >>> set_password my_password
locked >>> unlock my_password
unlocked >>>
```

### Account Management
To create a new account, you will need to start with an existing account with
some of the CORE asset that will pay the transaction fee registering your new
account.  The account paying this fee will be the *Registrar*.

In the initial genesis state, the account `nathan` has all of the money, so we
will use it as the registrar.
```
# first, import the private key to take ownership of the 'nathan' account
unlocked >>> import_key "nathan" 5KQwrPbwdL6PhXujxW37FSSQZ1JiwsST4cqQzDeyXtP79zkvFD3
unlocked >>> list_my_accounts
[{
    "id": "1.3.15",
    ...
    "name": "nathan",
    ...
]
# before nathan can create other accounts, we need to upgrade it to a prime member.
unlocked >>> upgrade_account nathan true
unlocked >>> create_account_with_brain_key "this is the brain key for my account" my-account nathan nathan true
```
Like most methods in the wallet, `create_account_with_brain_key`'s last
parameter is the boolean `broadcast`.  This parameter tells the wallet whether
you want to publish the transaction on the network immediately, which is
usually what you want to do.  If you pass false, it will just create the
transaction and sign it, and display it on the console, but it wouldn't be sent
out onto the network.  This could be used to build up a multi-sig transaction
and collect the other signatures offline, or it could be used to construct a
transaction in a offline cold wallet that you could put on a flash drive and
broadcast from a machine connected to the network.  Here, we'll always pass
`true` for the `broadcast` parameter.

### Transferring Currency
Your newly-created account doesn't have any funds in it yet, the `nathan`
account still has all the money.  To send some CORE from `nathan` to your
account, use the `transfer` command:
```
unlocked >>> transfer nathan my-account 10000 CORE "have some CORE" true
```
### Becoming a Witness
To become a witness and be able to produce blocks, you first need to create a
witness object that can be voted in.

Note: If you want to experiment with things that require voting, be aware that
votes are only tallied once per day at the maintenance interval.  For testing,
it's helpful to change the `GRAPHENE_DEFAULT_MAINTENANCE_INTERVAL` in
`libraries/chain/include/graphene/chain/config.hpp` to, say, 10 minutes.

Before we get started, we can see the current list of witnesses voted in, which
will simply be the ten default witnesses:
```
unlocked >>> get_global_properties
...
  "active_witnesses": [
    "1.7.0",
    "1.7.1",
    "1.7.2",
    "1.7.3",
    "1.7.4",
    "1.7.5",
    "1.7.6",
    "1.7.7",
    "1.7.8",
    "1.7.9"
  ],
...
```
Only lifetime members can become witnesses, so you must first upgrade to a
lifetime member.  Upgrade and create our witness object.
```
unlocked >>> upgrade_account my-account true
unlocked >>> create_witness my-account "http://foo.bar.com/" true
{
  "ref_block_num": 139,
  "ref_block_prefix": 3692461913,
  "relative_expiration": 3,
  "operations": [[
      21,{
        "fee": {
          "amount": 0,
          "asset_id": "1.4.0"
        },
        "witness_account": "1.3.16",
        "url": "http://foo.bar.com/",
        "block_signing_key": "1.2.0",
        "initial_secret": "00000000000000000000000000000000000000000000000000000000"
      }
    ]
  ],
  "signatures": [[
      "1.2.23",
      "1f2ad5597af2ac4bf7a50f1eef2db49c9c0f7616718776624c2c09a2dd72a0c53a26e8c2bc928f783624c4632924330fc03f08345c8f40b9790efa2e4157184a37"
    ]
  ],
  "extra_signatures": []
}
```
Our witness is registered, but it can't produce blocks because nobody has voted
it in.  You can see the current list of active witnesses with
`get_global_properties`:
```
unlocked >>> get_global_properties
{
  "active_witnesses": [
    "1.7.0",
    "1.7.1",
    "1.7.2",
    "1.7.3",
    "1.7.4",
    "1.7.5",
    "1.7.7",
    "1.7.8",
    "1.7.9"
  ],
  ...
```
Now, we should vote our witness in.  Vote all of the shares in both
`my-account` and `nathan` in favor of your new witness.
```
unlocked >>> vote_for_witness my-account my-account true true
unlocked >>> vote_for_witness nathan my-account true true
```
Now we wait until the next maintenance interval.
`get_dynamic_global_properties` tells us when that will be in
`next_maintenance_time`.  Once the next maintenance interval passes, run
`get_global_properties` again and you should see that your new witness has been
voted in.  

Even though it's voted in, it isn't producing any blocks yet because we only
told the witness_node to produce blocks for 1.7.0 - 1.7.9 on the command line,
and it doesn't know the private key for the witness.  Get the witness object to
find out its id and the key we need, then find the the private key.  

Warning: `dump_private_keys` will display your keys unencrypted on the
terminal, don't do this with someone looking over your shoulder.
```
unlocked >>> get_witness my-account
{
  "id": "1.7.10",
  ...
  "signing_key": "1.2.25",
  ...
}
unlocked >>> dump_private_keys
[[
   ...
  ],[
    "1.2.25",
    "5JGi7DM7J8fSTizZ4D9roNgd8dUc5pirUe9taxYCUUsnvQ4zCaQ"
  ]
]
```
Now we need to re-start the witness, so shut down the wallet (ctrl-d),  and
shut down the witness (ctrl-c).  Re-launch the witness, now mentioning the new
witness 1.7.0 and its key 1.2.25:
```
./witness_node --rpc-endpoint --enable-stale-production --witness-id \""1.7.0"\" \""1.7.1"\" \""1.7.2"\" \""1.7.3"\" \""1.7.4"\"  \""1.7.5"\" \""1.7.6"\" \""1.7.7"\" \""1.7.8"\" \""1.7.9"\" \""1.7.10"\" --private-key "[\"1.2.0\",\"5KQwrPbwdL6PhXujxW37FSSQZ1JiwsST4cqQzDeyXtP79zkvFD3\"]" "[\"1.2.25\",\"5JGi7DM7J8fSTizZ4D9roNgd8dUc5pirUe9taxYCUUsnvQ4zCaQ\"]"
```
