# BitShares Programs

The bitshares programs are a collection of binaries to run the blockchain, interact with it or utilities.

The main program is the `witness_node`, used to run a bitshares block producer, API or plugin node. The second in importance is the `cli_wallet`, used to interact with the blockchain. This 2 programs are the most used by the community and updated by the developers, rest of the programs are utilities.

Programs in here are part of the **bitshares-core** project and are maintained by the bitshares core team and contributors.


# Available Programs

Folder | Name  | Description | Category | Status | Help 
---|---|---|---|---|---
[witness_node](witness_node) | Witness Node | Main software used to sign blocks or provide services. | Node | Active | `./witness_node --help`
[cli_wallet](cli_wallet) | CLI Wallet | Software to interact with the blockchain by command line.  | Wallet | Active | `./cli_wallet --help` 
[delayed_node](delayed_node) | Delayed Node | Runs a node with `delayed_node` plugin loaded. This is deprecated in favour of `./witness_node --plugins "delayed_node"`. | Node | Deprecated | `./delayed_node --help`
[js_operation_serializer](js_operation_serializer) | Operation Serializer | Dump all blockchain operations and types. Used by the UI. | Tool | Old | `./js_operation_serializer`
[size_checker](size_checker) | Size Checker | Return wire size average in bytes of all the operations.  | Tool | Old | `./size_checker`
[build_helpers](build_helpers) | Build Helpers | | Tool | Old | `./member_enumerator` and `./cat-parts`
[genesis_util](genesis_util) | Genesis Utils | | Tool | Old |