# Template Plugin

This plugin is a template that shows the use of the plugin system to
create a new API. In this case, it is called `hello` and serves no real reason
other than demostration of the plugin subsystem.

# Plugin Source Code

## Plugin

### `*_plugin.hpp` and `*_plugin.cpp`

These files deal with the plugin system of graphene. They allow for
configuration using program parameters and deal with the startup and
shutdown of the application. This is useful of additional features need to be
added on the backend or to provide features needed by and additional API (sel
below). See further instructions on the general structure directly in the
sources.

### Activating a plugin

Plugins are activated in `programs/witness_node/main.cpp` by

* adding the corresponding header for the plugin, and
* instanciating it.

For example:

    --- a/programs/witness_node/main.cpp
    +++ b/programs/witness_node/main.cpp
    @@ -26,6 +26,7 @@
     #include <graphene/witness/witness.hpp>
     #include <graphene/account_history/account_history_plugin.hpp>
     #include <graphene/market_history/market_history_plugin.hpp>
    +#include <graphene/hello/hello_plugin.hpp>

     #include <fc/exception/exception.hpp>
     #include <fc/thread/thread.hpp>
    @@ -71,9 +72,13 @@ int main(int argc, char** argv) {

           bpo::variables_map options;

           auto witness_plug = node->register_plugin<witness_plugin::witness_plugin>();
           auto history_plug = node->register_plugin<account_history::account_history_plugin>();
           auto market_history_plug = node->register_plugin<market_history::market_history_plugin>();
    +      auto hello_plug = node->register_plugin<hello_plugin::hello_plugin>();


If done correctly, the witness_node will call the plugin's `startup` and show
the following log message:

    1674879ms th_a       hello_plugin.cpp:40           plugin_startup       ] hello plugin:  plugin_startup()


## API

The API allows to customize API calls by providing new API subclasses that can
later be called with queries such as:

    {"method":"call", "params": [
         "API-NAME",
         "METHOD-TO-CALL",
         ["PARAMETER 1", "PARAMETER 2"]
    ], "json-rpc": 2.0, "id": 0}

### `*_api.hpp` and `*_api.cpp`

These files provide the actual API that can be later used to add additional API
calls that are reflected into the RPC subsystem of graphene. See further
instructions on the general structure directly in the sources.

### Integrating the API

The API needs to be loaded in `libraries/app/api.hpp` by adding

* the header file for the plugin,
* declaring the API's interface, and
* reflecting the entire API

For example:

    --- a/libraries/app/include/graphene/app/api.hpp
    +++ b/libraries/app/include/graphene/app/api.hpp
    @@ -31,6 +31,7 @@
     #include <graphene/market_history/market_history_plugin.hpp>

     #include <graphene/debug_witness/debug_api.hpp>
    +#include <graphene/hello/hello_api.hpp>

     #include <graphene/net/node.hpp>

    @@ -356,6 +357,8 @@ namespace graphene { namespace app {
              fc::api<asset_api> asset()const;
              /// @brief Retrieve the debug API (if available)
              fc::api<graphene::debug_witness::debug_api> debug()const;
    +         /// @brief Retrieve the hello API
    +         fc::api<graphene::hello::hello_api> hello()const;

              /// @brief Called to enable an API, not reflected.
              void enable_api( const string& api_name );
    @@ -370,6 +373,7 @@ namespace graphene { namespace app {
              optional< fc::api<crypto_api> > _crypto_api;
              optional< fc::api<asset_api> > _asset_api;
              optional< fc::api<graphene::debug_witness::debug_api> > _debug_api;
    +         optional< fc::api<graphene::hello::hello_api> > _hello_api;
        };

     }}  // graphene::app
    @@ -437,4 +441,5 @@ FC_API(graphene::app::login_api,
            (crypto)
            (asset)
            (debug)
    +       (hello)
          )


and integrated into `libraries/app/api.cpp` like this

    --- a/libraries/app/api.cpp
    +++ b/libraries/app/api.cpp
    @@ -100,6 +100,10 @@ namespace graphene { namespace app {
            {
               _crypto_api = std::make_shared< crypto_api >();
            }
    +       else if( api_name == "hello_api" )
    +       {
    +          _hello_api = std::make_shared< graphene::hello::hello_api >( std::ref(_app) );
    +       }
            else if( api_name == "asset_api" )
            {
               _asset_api = std::make_shared< asset_api >( std::ref( *_app.chain_database() ) );
    @@ -268,6 +272,12 @@ namespace graphene { namespace app {
            return *_debug_api;
         }

    +    fc::api<graphene::hello::hello_api> login_api::hello() const
    +    {
    +       FC_ASSERT(_hello_api);
    +       return *_hello_api;
    +    }
    +
         vector<account_id_type> get_relevant_accounts( const object* obj )
         {
            vector<account_id_type> result;

In this case, we hand over the entire `_app` context to the API constructor as
a std reference. After instanciation, the `_hello_api` is connected to the
`login_api` namespace.

### Activating the API

By adding

    wild_access.allowed_apis.push_back( "hello_api" );

into `libraries/app/application.cpp` (line 484), we active the `hello_plugin`
by default. Alternatively, one can use the *api-access* configuration
attribute.

## Namespaces

The namespaces used are

    graphene::hello
    graphene::hello_plugin

# CMake

If you create a new plugin, make sure to also modify:

* `libraries/app/CMakeLists.txt` and
* `programs/witness_node/CMakeLists.txt`
