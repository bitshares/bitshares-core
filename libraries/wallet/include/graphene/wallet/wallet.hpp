/*
 * Copyright (c) 2017 Cryptonomex, Inc., and contributors.
 *
 * The MIT License
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#pragma once

#include <fc/optional.hpp>
#include <graphene/chain/htlc_object.hpp>
#include <graphene/app/api.hpp>
#include <graphene/utilities/key_conversion.hpp>
#include "wallet_structs.hpp"

namespace fc
{
   void to_variant( const account_multi_index_type& accts, variant& vo, uint32_t max_depth );
   void from_variant( const variant &var, account_multi_index_type &vo, uint32_t max_depth );
}

namespace graphene { namespace wallet {

/**
 * This class takes a variant and turns it into an object
 * of the given type, with the new operator.
 */
object* create_object( const variant& v );

/**
 * This wallet assumes it is connected to the database server with a high-bandwidth, low-latency connection and
 * performs minimal caching. This API could be provided locally to be used by a web interface.
 */
class wallet_api
{
   public:
      wallet_api( const wallet_data& initial_data, fc::api<login_api> rapi );
      virtual ~wallet_api();

      bool copy_wallet_file( string destination_filename );

      fc::ecc::private_key derive_private_key(const std::string& prefix_string, int sequence_number) const;

      /** Returns info about head block, chain_id, maintenance, participation, current active witnesses and
       * committee members.
       * @returns runtime info about the blockchain
       */
      variant                           info();
      /** Returns info such as client version, git version of graphene/fc, version of boost, openssl.
       * @returns compile time info and client and dependencies versions
       */
      variant_object                    about() const;
      /** Returns info about a specified block.
       * @param num height of the block to retrieve
       * @returns info about the block, or null if not found
       */
      optional<signed_block_with_info>    get_block( uint32_t num );
      /** Returns the number of accounts registered on the blockchain
       * @returns the number of registered accounts
       */
      uint64_t                          get_account_count()const;
      /** Lists all accounts controlled by this wallet.
       * This returns a list of the full account objects for all accounts whose private keys
       * we possess.
       * @returns a list of account objects
       */
      vector<account_object>            list_my_accounts();
      /** Lists all accounts registered in the blockchain.
       * This returns a list of all account names and their account ids, sorted by account name.
       *
       * Use the \c lowerbound and limit parameters to page through the list.  To retrieve all accounts,
       * start by setting \c lowerbound to the empty string \c "", and then each iteration, pass
       * the last account name returned as the \c lowerbound for the next \c list_accounts() call.
       *
       * @param lowerbound the name of the first account to return.  If the named account does not exist,
       *                   the list will start at the account that comes after \c lowerbound
       * @param limit the maximum number of accounts to return (max: 1000)
       * @returns a list of accounts mapping account names to account ids
       */
      map<string,account_id_type>       list_accounts(const string& lowerbound, uint32_t limit);
      /** List the balances of an account.
       * Each account can have multiple balances, one for each type of asset owned by that
       * account.  The returned list will only contain assets for which the account has a
       * nonzero balance
       * @param id the name or id of the account whose balances you want
       * @returns a list of the given account's balances
       */
      vector<asset>                     list_account_balances(const string& id);
      /** Lists all assets registered on the blockchain.
       *
       * To list all assets, pass the empty string \c "" for the lowerbound to start
       * at the beginning of the list, and iterate as necessary.
       *
       * @param lowerbound  the symbol of the first asset to include in the list.
       * @param limit the maximum number of assets to return (max: 100)
       * @returns the list of asset objects, ordered by symbol
       */
      vector<extended_asset_object>     list_assets(const string& lowerbound, uint32_t limit)const;
      /** Returns assets count registered on the blockchain.
       *
       * @returns assets count
       */
      uint64_t get_asset_count()const;

      /** Returns the most recent operations on the named account.
       *
       * This returns a list of operation history objects, which describe activity on the account.
       *
       * @param name the name or id of the account
       * @param limit the number of entries to return (starting from the most recent)
       * @returns a list of \c operation_history_objects
       */
      vector<operation_detail>  get_account_history(string name, int limit)const;

      /** Returns the relative operations on the named account from start number.
       *
       * @param name the name or id of the account
       * @param stop Sequence number of earliest operation.
       * @param limit the number of entries to return
       * @param start  the sequence number where to start looping back throw the history
       * @returns a list of \c operation_history_objects
       */
     vector<operation_detail>  get_relative_account_history( string name, uint32_t stop,
                                                             int limit, uint32_t start )const;

      /**
       * @brief Fetch all objects relevant to the specified account
       * @param name_or_id Must be the name or ID of an account to retrieve
       * @return All info about the specified account
       *
       * This function fetches all relevant objects for the given account. If the string
       * of \c name_or_id cannot be tied to an account, that input will be ignored.
       *
       */
      full_account                      get_full_account( const string& name_or_id );

      /**
       * @brief Get OHLCV data of a trading pair in a time range
       * @param symbol name or ID of the base asset
       * @param symbol2 name or ID of the quote asset
       * @param bucket length of each time bucket in seconds.
       * @param start the start of a time range, E.G. "2018-01-01T00:00:00"
       * @param end the end of the time range
       * @return A list of OHLCV data, in "least recent first" order.
       */
      vector<bucket_object>             get_market_history( string symbol, string symbol2, uint32_t bucket,
                                                            fc::time_point_sec start, fc::time_point_sec end )const;

      /**
       * @brief Fetch all orders relevant to the specified account sorted descendingly by price
       *
       * @param name_or_id  The name or ID of an account to retrieve
       * @param base  Base asset
       * @param quote  Quote asset
       * @param limit  The limitation of items each query can fetch (max: 101)
       * @param ostart_id  Start order id, fetch orders which price are lower than or equal to this order
       * @param ostart_price  Fetch orders with price lower than or equal to this price
       *
       * @return List of orders from \c name_or_id to the corresponding account
       *
       * @note
       * 1. if \c name_or_id cannot be tied to an account, empty result will be returned
       * 2. \c ostart_id and \c ostart_price can be \c null, if so the api will return the "first page" of orders;
       *    if \c ostart_id is specified and valid, its price will be used to do page query preferentially,
       *    otherwise the \c ostart_price will be used
       */
      vector<limit_order_object>        get_account_limit_orders( const string& name_or_id,
                                            const string &base,
                                            const string &quote,
                                            uint32_t limit = 101,
                                            optional<limit_order_id_type> ostart_id = optional<limit_order_id_type>(),
                                            optional<price> ostart_price = optional<price>());

      /**
       * @brief Get limit orders in a given market
       * @param a symbol or ID of asset being sold
       * @param b symbol or ID of asset being purchased
       * @param limit Maximum number of orders to retrieve
       * @return The limit orders, ordered from least price to greatest
       */
      vector<limit_order_object>        get_limit_orders(string a, string b, uint32_t limit)const;

      /**
       * @brief Get call orders (aka margin positions) for a given asset
       * @param a symbol name or ID of the debt asset
       * @param limit Maximum number of orders to retrieve
       * @return The call orders, ordered from earliest to be called to latest
       */
      vector<call_order_object>         get_call_orders(string a, uint32_t limit)const;

      /**
       * @brief Get forced settlement orders in a given asset
       * @param a Symbol or ID of asset being settled
       * @param limit Maximum number of orders to retrieve
       * @return The settle orders, ordered from earliest settlement date to latest
       */
      vector<force_settlement_object>   get_settle_orders(string a, uint32_t limit)const;

      /** Returns the collateral_bid object for the given MPA
       *
       * @param asset the name or id of the asset
       * @param limit the number of entries to return
       * @param start the sequence number where to start looping back throw the history
       * @returns a list of \c collateral_bid_objects
       */
      vector<collateral_bid_object> get_collateral_bids(string asset, uint32_t limit = 100, uint32_t start = 0)const;

      /** Returns the block chain's slowly-changing settings.
       * This object contains all of the properties of the blockchain that are fixed
       * or that change only once per maintenance interval (daily) such as the
       * current list of witnesses, committee_members, block interval, etc.
       * @see \c get_dynamic_global_properties() for frequently changing properties
       * @returns the global properties
       */
      global_property_object            get_global_properties() const;

      /**
       * Get operations relevant to the specified account filtering by operation type, with transaction id
       *
       * @param name the name or id of the account, whose history shoulde be queried
       * @param operation_types The IDs of the operation we want to get operations in the account
       *                        ( 0 = transfer , 1 = limit order create, ...)
       * @param start the sequence number where to start looping back throw the history
       * @param limit the max number of entries to return (from start number)
       * @returns account_history_operation_detail
       */
      account_history_operation_detail get_account_history_by_operations( string name,
                                                                          vector<uint16_t> operation_types,
                                                                          uint32_t start, int limit);

      /** Returns the block chain's rapidly-changing properties.
       * The returned object contains information that changes every block interval
       * such as the head block number, the next witness, etc.
       * @see \c get_global_properties() for less-frequently changing properties
       * @returns the dynamic global properties
       */
      dynamic_global_property_object    get_dynamic_global_properties() const;

      /** Returns information about the given account.
       *
       * @param account_name_or_id the name or ID of the account to provide information about
       * @returns the public account data stored in the blockchain
       */
      account_object                    get_account(string account_name_or_id) const;

      /** Returns information about the given asset.
       * @param asset_name_or_id the symbol or id of the asset in question
       * @returns the information about the asset stored in the block chain
       */
      extended_asset_object             get_asset(string asset_name_or_id) const;

      /** Returns the BitAsset-specific data for a given asset.
       * Market-issued assets's behavior are determined both by their "BitAsset Data" and
       * their basic asset data, as returned by \c get_asset().
       * @param asset_name_or_id the symbol or id of the BitAsset in question
       * @returns the BitAsset-specific data for this asset
       */
      asset_bitasset_data_object        get_bitasset_data(string asset_name_or_id)const;

      /**
       * Returns information about the given HTLC object.
       * @param htlc_id the id of the HTLC object.
       * @returns the information about the HTLC object
       */
      fc::optional<fc::variant>             get_htlc(string htlc_id) const;

      /** Lookup the id of a named account.
       * @param account_name_or_id the name or ID of the account to look up
       * @returns the id of the named account
       */
      account_id_type                   get_account_id(string account_name_or_id) const;

      /**
       * Lookup the id of a named asset.
       * @param asset_name_or_id the symbol or ID of an asset to look up
       * @returns the id of the given asset
       */
      asset_id_type                     get_asset_id(string asset_name_or_id) const;

      /**
       * Returns the blockchain object corresponding to the given id.
       *
       * This generic function can be used to retrieve any object from the blockchain
       * that is assigned an ID.  Certain types of objects have specialized convenience
       * functions to return their objects -- e.g., assets have \c get_asset(), accounts
       * have \c get_account(), but this function will work for any object.
       *
       * @param id the id of the object to return
       * @returns the requested object
       */
      variant                           get_object(object_id_type id) const;

      /** Returns the current wallet filename.
       *
       * This is the filename that will be used when automatically saving the wallet.
       *
       * @see set_wallet_filename()
       * @return the wallet filename
       */
      string                            get_wallet_filename() const;

      /**
       * Get the WIF private key corresponding to a public key.  The
       * private key must already be in the wallet.
       * @param pubkey a public key in Base58 format
       * @return the WIF private key
       */
      string                            get_private_key( public_key_type pubkey )const;

      /**
       * @ingroup Transaction Builder API
       *
       * Create a new transaction builder.
       * @return handle of the new transaction builder
       */
      transaction_handle_type begin_builder_transaction();
      /**
       * @ingroup Transaction Builder API
       *
       * Append a new operation to a transaction builder.
       * @param transaction_handle handle of the transaction builder
       * @param op the operation in JSON format
       */
      void add_operation_to_builder_transaction(transaction_handle_type transaction_handle, const operation& op);
      /**
       * @ingroup Transaction Builder API
       *
       * Replace an operation in a transaction builder with a new operation.
       * @param handle handle of the transaction builder
       * @param operation_index the index of the old operation in the builder to be replaced
       * @param new_op the new operation in JSON format
       */
      void replace_operation_in_builder_transaction(transaction_handle_type handle,
                                                    unsigned operation_index,
                                                    const operation& new_op);
      /**
       * @ingroup Transaction Builder API
       *
       * Calculate and update fees for the operations in a transaction builder.
       * @param handle handle of the transaction builder
       * @param fee_asset name or ID of an asset that to be used to pay fees
       * @return total fees
       */
      asset set_fees_on_builder_transaction(transaction_handle_type handle, string fee_asset = GRAPHENE_SYMBOL);
      /**
       * @ingroup Transaction Builder API
       *
       * Show content of a transaction builder.
       * @param handle handle of the transaction builder
       * @return a transaction
       */
      transaction preview_builder_transaction(transaction_handle_type handle);
      /**
       * @ingroup Transaction Builder API
       *
       * Sign the transaction in a transaction builder and optionally broadcast to the network.
       * @param transaction_handle handle of the transaction builder
       * @param broadcast whether to broadcast the signed transaction to the network
       * @return a signed transaction
       */
      signed_transaction sign_builder_transaction(transaction_handle_type transaction_handle, bool broadcast = true);

      /**
       * @ingroup Transaction Builder API
       *
       * Sign the transaction in a transaction builder and optionally broadcast to the network.
       * @param transaction_handle handle of the transaction builder
       * @param signing_keys Keys that must be used when signing the transaction
       * @param broadcast whether to broadcast the signed transaction to the network
       * @return a signed transaction
       */
      signed_transaction sign_builder_transaction2(transaction_handle_type transaction_handle,
                                                  const vector<public_key_type>& signing_keys = vector<public_key_type>(),
                                                  bool broadcast = true);

      /** Broadcast signed transaction
       * @param tx signed transaction
       * @returns the transaction ID along with the signed transaction.
       */
      pair<transaction_id_type,signed_transaction> broadcast_transaction(signed_transaction tx);

      /**
       * @ingroup Transaction Builder API
       *
       * Create a proposal containing the operations in a transaction builder (create a new proposal_create
       * operation, then replace the transaction builder with the new operation), then sign the transaction
       * and optionally broadcast to the network.
       *
       * Note: this command is buggy because unable to specify proposer. It will be deprecated in a future release.
       *       Please use \c propose_builder_transaction2() instead.
       *
       * @param handle handle of the transaction builder
       * @param expiration when the proposal will expire
       * @param review_period_seconds review period of the proposal in seconds
       * @param broadcast whether to broadcast the signed transaction to the network
       * @return a signed transaction
       */
      signed_transaction propose_builder_transaction(
          transaction_handle_type handle,
          time_point_sec expiration = time_point::now() + fc::minutes(1),
          uint32_t review_period_seconds = 0,
          bool broadcast = true
         );

      /**
       * @ingroup Transaction Builder API
       *
       * Create a proposal containing the operations in a transaction builder (create a new proposal_create
       * operation, then replace the transaction builder with the new operation), then sign the transaction
       * and optionally broadcast to the network.
       *
       * @param handle handle of the transaction builder
       * @param account_name_or_id name or ID of the account who would pay fees for creating the proposal
       * @param expiration when the proposal will expire
       * @param review_period_seconds review period of the proposal in seconds
       * @param broadcast whether to broadcast the signed transaction to the network
       * @return a signed transaction
       */
      signed_transaction propose_builder_transaction2(
         transaction_handle_type handle,
         string account_name_or_id,
         time_point_sec expiration = time_point::now() + fc::minutes(1),
         uint32_t review_period_seconds = 0,
         bool broadcast = true
        );

      /**
       * @ingroup Transaction Builder API
       *
       * Destroy a transaction builder.
       * @param handle handle of the transaction builder
       */
      void remove_builder_transaction(transaction_handle_type handle);

      /** Checks whether the wallet has just been created and has not yet had a password set.
       *
       * Calling \c set_password will transition the wallet to the locked state.
       * @return true if the wallet is new
       * @ingroup Wallet Management
       */
      bool    is_new()const;

      /** Checks whether the wallet is locked (is unable to use its private keys).
       *
       * This state can be changed by calling \c lock() or \c unlock().
       * @return true if the wallet is locked
       * @ingroup Wallet Management
       */
      bool    is_locked()const;

      /** Locks the wallet immediately.
       * @ingroup Wallet Management
       */
      void    lock();

      /** Unlocks the wallet.
       *
       * The wallet remain unlocked until the \c lock is called
       * or the program exits.
       *
       * When used in command line, if typed "unlock" without a password followed, the user will be prompted
       * to input a password without echo.
       *
       * @param password the password previously set with \c set_password()
       * @ingroup Wallet Management
       */
      void    unlock(string password);

      /** Sets a new password on the wallet.
       *
       * The wallet must be either 'new' or 'unlocked' to
       * execute this command.
       *
       * When used in command line, if typed "set_password" without a password followed, the user will be prompted
       * to input a password without echo.
       *
       * @param password a new password
       * @ingroup Wallet Management
       */
      void    set_password(string password);

      /** Dumps all private keys owned by the wallet.
       *
       * The keys are printed in WIF format.  You can import these keys into another wallet
       * using \c import_key()
       * @returns a map containing the private keys, indexed by their public key
       */
      map<public_key_type, string> dump_private_keys();

      /** Returns a list of all commands supported by the wallet API.
       *
       * This lists each command, along with its arguments and return types.
       * For more detailed help on a single command, use \c gethelp()
       *
       * @returns a multi-line string suitable for displaying on a terminal
       */
      string  help()const;

      /** Returns detailed help on a single API command.
       * @param method the name of the API command you want help with
       * @returns a multi-line string suitable for displaying on a terminal
       */
      string  gethelp(const string& method)const;

      /** Loads a specified BitShares wallet.
       *
       * The current wallet is closed before the new wallet is loaded.
       *
       * @warning This does not change the filename that will be used for future
       * wallet writes, so this may cause you to overwrite your original
       * wallet unless you also call \c set_wallet_filename()
       *
       * @param wallet_filename the filename of the wallet JSON file to load.
       *                        If \c wallet_filename is empty, it reloads the
       *                        existing wallet file
       * @returns true if the specified wallet is loaded
       */
      bool    load_wallet_file(string wallet_filename = "");

      /** Quit from the wallet.
       *
       * The current wallet will be closed and saved.
       */
      void    quit();

      /** Saves the current wallet to the given filename.
       *
       * @warning This does not change the wallet filename that will be used for future
       * writes, so think of this function as 'Save a Copy As...' instead of
       * 'Save As...'.  Use \c set_wallet_filename() to make the filename
       * persist.
       * @param wallet_filename the filename of the new wallet JSON file to create
       *                        or overwrite.  If \c wallet_filename is empty,
       *                        save to the current filename.
       */
      void    save_wallet_file(string wallet_filename = "");

      /** Sets the wallet filename used for future writes.
       *
       * This does not trigger a save, it only changes the default filename
       * that will be used the next time a save is triggered.
       *
       * @param wallet_filename the new filename to use for future saves
       */
      void    set_wallet_filename(string wallet_filename);

      /** Suggests a safe brain key to use for creating your account.
       * \c create_account_with_brain_key() requires you to specify a 'brain key',
       * a long passphrase that provides enough entropy to generate cyrptographic
       * keys.  This function will suggest a suitably random string that should
       * be easy to write down (and, with effort, memorize).
       * @returns a suggested brain_key
       */
      brain_key_info suggest_brain_key()const;

     /**
      * Derive any number of *possible* owner keys from a given brain key.
      *
      * NOTE: These keys may or may not match with the owner keys of any account.
      * This function is merely intended to assist with account or key recovery.
      *
      * @see suggest_brain_key()
      *
      * @param brain_key    Brain key
      * @param number_of_desired_keys  Number of desired keys
      * @return A list of keys that are deterministically derived from the brainkey
      */
     vector<brain_key_info> derive_owner_keys_from_brain_key(string brain_key, int number_of_desired_keys = 1) const;

     /**
      * Determine whether a textual representation of a public key
      * (in Base-58 format) is *currently* linked
      * to any *registered* (i.e. non-stealth) account on the blockchain
      * @param public_key Public key
      * @return Whether a public key is known
      */
     bool is_public_key_registered(string public_key) const;

      /** Converts a signed_transaction in JSON form to its binary representation.
       *
       * @param tx the transaction to serialize
       * @returns the binary form of the transaction.  It will not be hex encoded,
       *          this returns a raw string that may have null characters embedded
       *          in it
       */
      string serialize_transaction(signed_transaction tx) const;

      /** Imports the private key for an existing account.
       *
       * The private key must match either an owner key or an active key for the
       * named account.
       *
       * @see dump_private_keys()
       *
       * @param account_name_or_id the account owning the key
       * @param wif_key the private key in WIF format
       * @returns true if the key was imported
       */
      bool import_key(string account_name_or_id, string wif_key);

      /** Imports accounts from a BitShares 0.x wallet file.
       * Current wallet file must be unlocked to perform the import.
       *
       * @param filename the BitShares 0.x wallet file to import
       * @param password the password to encrypt the BitShares 0.x wallet file
       * @returns a map containing the accounts found and whether imported
       */
      map<string, bool> import_accounts( string filename, string password );

      /** Imports from a BitShares 0.x wallet file, find keys that were bound to a given account name on the
       * BitShares 0.x chain, rebind them to an account name on the 2.0 chain.
       * Current wallet file must be unlocked to perform the import.
       *
       * @param filename the BitShares 0.x wallet file to import
       * @param password the password to encrypt the BitShares 0.x wallet file
       * @param src_account_name name of the account on BitShares 0.x chain
       * @param dest_account_name name of the account on BitShares 2.0 chain,
       *                          can be same or different to \c src_account_name
       * @returns whether the import has succeeded
       */
      bool import_account_keys( string filename, string password, string src_account_name, string dest_account_name );

      /**
       * This call will construct transaction(s) that will claim all balances controled
       * by wif_keys and deposit them into the given account.
       *
       * @param account_name_or_id name or ID of an account that to claim balances to
       * @param wif_keys private WIF keys of balance objects to claim balances from
       * @param broadcast true to broadcast the transaction on the network
       */
      vector< signed_transaction > import_balance( string account_name_or_id, const vector<string>& wif_keys,
                                                   bool broadcast );

      /** Transforms a brain key to reduce the chance of errors when re-entering the key from memory.
       *
       * This takes a user-supplied brain key and normalizes it into the form used
       * for generating private keys.  In particular, this upper-cases all ASCII characters
       * and collapses multiple spaces into one.
       * @param s the brain key as supplied by the user
       * @returns the brain key in its normalized form
       */
      string normalize_brain_key(string s) const;

      /** Registers a third party's account on the blockckain.
       *
       * This function is used to register an account for which you do not own the private keys.
       * When acting as a registrar, an end user will generate their own private keys and send
       * you the public keys.  The registrar will use this function to register the account
       * on behalf of the end user.
       *
       * @see create_account_with_brain_key()
       *
       * @param name the name of the account, must be unique on the blockchain.  Shorter names
       *             are more expensive to register; the rules are still in flux, but in general
       *             names of more than 8 characters with at least one digit will be cheap.
       * @param owner the owner key for the new account
       * @param active the active key for the new account
       * @param registrar_account the account which will pay the fee to register the user
       * @param referrer_account the account who is acting as a referrer, and may receive a
       *                         portion of the user's transaction fees.  This can be the
       *                         same as the registrar_account if there is no referrer.
       * @param referrer_percent the percentage (0 - 100) of the new user's transaction fees
       *                         not claimed by the blockchain that will be distributed to the
       *                         referrer; the rest will be sent to the registrar.  Will be
       *                         multiplied by GRAPHENE_1_PERCENT when constructing the transaction.
       * @param broadcast true to broadcast the transaction on the network
       * @returns the signed transaction registering the account
       */
      signed_transaction register_account(string name,
                                          public_key_type owner,
                                          public_key_type active,
                                          string  registrar_account,
                                          string  referrer_account,
                                          uint32_t referrer_percent,
                                          bool broadcast = false);

      /**
       *  Upgrades an account to prime status.
       *  This makes the account holder a 'lifetime member'.
       *
       * @param name the name or id of the account to upgrade
       * @param broadcast true to broadcast the transaction on the network
       * @returns the signed transaction upgrading the account
       */
      signed_transaction upgrade_account(string name, bool broadcast);

      /** Creates a new account and registers it on the blockchain.
       *
       * @todo why no referrer_percent here?
       *
       * @see suggest_brain_key()
       * @see register_account()
       *
       * @param brain_key the brain key used for generating the account's private keys
       * @param account_name the name of the account, must be unique on the blockchain.  Shorter names
       *                     are more expensive to register; the rules are still in flux, but in general
       *                     names of more than 8 characters with at least one digit will be cheap.
       * @param registrar_account the account which will pay the fee to register the user
       * @param referrer_account the account who is acting as a referrer, and may receive a
       *                         portion of the user's transaction fees.  This can be the
       *                         same as the registrar_account if there is no referrer.
       * @param broadcast true to broadcast the transaction on the network
       * @returns the signed transaction registering the account
       */
      signed_transaction create_account_with_brain_key(string brain_key,
                                                       string account_name,
                                                       string registrar_account,
                                                       string referrer_account,
                                                       bool broadcast = false);

      /** Transfer an amount from one account to another.
       * @param from the name or id of the account sending the funds
       * @param to the name or id of the account receiving the funds
       * @param amount the amount to send (in nominal units -- to send half of a BTS, specify 0.5)
       * @param asset_symbol the symbol or id of the asset to send
       * @param memo a memo to attach to the transaction.  The memo will be encrypted in the
       *             transaction and readable for the receiver.  There is no length limit
       *             other than the limit imposed by maximum transaction size, but transaction
       *             increase with transaction size
       * @param broadcast true to broadcast the transaction on the network
       * @returns the signed transaction transferring funds
       */
      signed_transaction transfer(string from,
                                  string to,
                                  string amount,
                                  string asset_symbol,
                                  string memo,
                                  bool broadcast = false);

      /**
       *  This method works just like transfer, except it always broadcasts and
       *  returns the transaction ID (hash) along with the signed transaction.
       * @param from the name or id of the account sending the funds
       * @param to the name or id of the account receiving the funds
       * @param amount the amount to send (in nominal units -- to send half of a BTS, specify 0.5)
       * @param asset_symbol the symbol or id of the asset to send
       * @param memo a memo to attach to the transaction.  The memo will be encrypted in the
       *             transaction and readable for the receiver.  There is no length limit
       *             other than the limit imposed by maximum transaction size, but transaction
       *             increase with transaction size
       * @returns the transaction ID (hash) along with the signed transaction transferring funds
       */
      pair<transaction_id_type,signed_transaction> transfer2(string from,
                                                             string to,
                                                             string amount,
                                                             string asset_symbol,
                                                             string memo ) {
         auto trx = transfer( from, to, amount, asset_symbol, memo, true );
         return std::make_pair(trx.id(),trx);
      }


      /**
       *  This method is used to convert a JSON transaction to its transactin ID.
       * @param trx a JSON transaction
       * @return the ID (hash) of the transaction
       */
      transaction_id_type get_transaction_id( const signed_transaction& trx )const { return trx.id(); }


      /** Sign a memo message.
       *
       * @param from the name or id of signing account; or a public key
       * @param to the name or id of receiving account; or a public key
       * @param memo text to sign
       * @return the signed memo data
       */
      memo_data sign_memo(string from, string to, string memo);

      /** Read a memo.
       *
       * @param memo JSON-enconded memo.
       * @returns string with decrypted message.
       */
      string read_memo(const memo_data& memo);


      /** Sign a message using an account's memo key. The signature is generated as in
       *   in https://github.com/xeroc/python-graphenelib/blob/d9634d74273ebacc92555499eca7c444217ecba0/graphenecommon/message.py#L64 .
       *
       * @param signer the name or id of signing account
       * @param message text to sign
       * @return the signed message in an abstract format
       */
      signed_message sign_message(string signer, string message);

      /** Verify a message signed with sign_message using the given account's memo key.
       *
       * @param message the message text
       * @param account the account name of the message
       * @param block the block number of the message
       * @param time the timestamp of the message
       * @param sig the message signature
       * @return true if signature matches
       */
      bool verify_message( string message, string account, int block, const string& time, compact_signature sig );

      /** Verify a message signed with sign_message
       *
       * @param message the signed_message structure containing message, meta data and signature
       * @return true if signature matches
       */
      bool verify_signed_message( signed_message message );

      /** Verify a message signed with sign_message, in its encapsulated form.
       *
       * @param message the complete encapsulated message string including separators and line feeds
       * @return true if signature matches
       */
      bool verify_encapsulated_message( string message );

      /** These methods are used for stealth transfers */
      ///@{
      /**
       * This method can be used to set a label for a public key
       *
       * @note No two keys can have the same label.
       * @param key a public key
       * @param label a user-defined string as label
       * @return true if the label was set, otherwise false
       */
      bool                        set_key_label( public_key_type key, string label );

      /**
       * Get label of a public key.
       * @param key a public key
       * @return the label if already set by \c set_key_label(), or an empty string if not set
       */
      string                      get_key_label( public_key_type key )const;

      /**
       * Generates a new blind account for the given brain key and assigns it the given label.
       * @param label a label
       * @param brain_key the brain key to be used to generate a new blind account
       * @return the public key of the new account
       */
      public_key_type             create_blind_account( string label, string brain_key  );

      /**
       * Return the total balances of all blinded commitments that can be claimed by the
       * given account key or label.
       * @param key_or_label a public key in Base58 format or a label
       * @return the total balances of all blinded commitments that can be claimed by the
       * given account key or label
       */
      vector<asset>                get_blind_balances( string key_or_label );
      /**
       * Get all blind accounts.
       * @return all blind accounts
       */
      map<string,public_key_type> get_blind_accounts()const;
      /**
       * Get all blind accounts for which this wallet has the private key.
       * @return all blind accounts for which this wallet has the private key
       */
      map<string,public_key_type> get_my_blind_accounts()const;
      /**
       * Get the public key associated with a given label.
       * @param label a label
       * @return the public key associated with the given label
       */
      public_key_type             get_public_key( string label )const;
      ///@}

      /**
       * Get all blind receipts to/form a particular account
       * @param key_or_account a public key in Base58 format or an account
       * @return all blind receipts to/form the account
       */
      vector<blind_receipt> blind_history( string key_or_account );

      /**
       * Given a confirmation receipt, this method will parse it for a blinded balance and confirm
       * that it exists in the blockchain.  If it exists then it will report the amount received and
       * who sent it.
       *
       * @param confirmation_receipt a base58 encoded stealth confirmation
       * @param opt_from if not empty and the sender is a unknown public key,
       *                 then the unknown public key will be given the label \c opt_from
       * @param opt_memo a self-defined label for this transfer to be saved in local wallet file
       * @return a blind receipt
       */
      blind_receipt receive_blind_transfer( string confirmation_receipt, string opt_from, string opt_memo );

      /**
       * Transfers a public balance from \c from_account_id_or_name to one or more blinded balances using a
       * stealth transfer.
       * @param from_account_id_or_name ID or name of an account to transfer from
       * @param asset_symbol symbol or ID of the asset to be transferred
       * @param to_amounts map from key or label to amount
       * @param broadcast true to broadcast the transaction on the network
       * @return a blind confirmation
       */
      blind_confirmation transfer_to_blind( string from_account_id_or_name,
                                            string asset_symbol,
                                            vector<pair<string, string>> to_amounts,
                                            bool broadcast = false );

      /**
       * Transfers funds from a set of blinded balances to a public account balance.
       * @param from_blind_account_key_or_label a public key in Base58 format or a label to transfer from
       * @param to_account_id_or_name ID or name of an account to transfer to
       * @param amount the amount to be transferred
       * @param asset_symbol symbol or ID of the asset to be transferred
       * @param broadcast true to broadcast the transaction on the network
       * @return a blind confirmation
       */
      blind_confirmation transfer_from_blind(
                                            string from_blind_account_key_or_label,
                                            string to_account_id_or_name,
                                            string amount,
                                            string asset_symbol,
                                            bool broadcast = false );

      /**
       * Transfer from one set of blinded balances to another.
       * @param from_key_or_label a public key in Base58 format or a label to transfer from
       * @param to_key_or_label a public key in Base58 format or a label to transfer to
       * @param amount the amount to be transferred
       * @param symbol symbol or ID of the asset to be transferred
       * @param broadcast true to broadcast the transaction on the network
       * @return a blind confirmation
       */
      blind_confirmation blind_transfer( string from_key_or_label,
                                         string to_key_or_label,
                                         string amount,
                                         string symbol,
                                         bool broadcast = false );

      /** Place a limit order attempting to sell one asset for another.
       *
       * Buying and selling are the same operation on BitShares; if you want to buy BTS
       * with USD, you should sell USD for BTS.
       *
       * The blockchain will attempt to sell the \c symbol_to_sell for as
       * much \c symbol_to_receive as possible, as long as the price is at
       * least \c min_to_receive / \c amount_to_sell.
       *
       * In addition to the transaction fees, market fees will apply as specified
       * by the issuer of both the selling asset and the receiving asset as
       * a percentage of the amount exchanged.
       *
       * If either the selling asset or the receiving asset is whitelist
       * restricted, the order will only be created if the seller is on
       * the whitelist of the restricted asset type.
       *
       * Market orders are matched in the order they are included
       * in the block chain.
       *
       * @todo Document default/max expiration time
       *
       * @param seller_account the account providing the asset being sold, and which will
       *                       receive the proceeds of the sale.
       * @param amount_to_sell the amount of the asset being sold to sell (in nominal units)
       * @param symbol_to_sell the name or id of the asset to sell
       * @param min_to_receive the minimum amount you are willing to receive in return for
       *                       selling the entire amount_to_sell
       * @param symbol_to_receive the name or id of the asset you wish to receive
       * @param timeout_sec if the order does not fill immediately, this is the length of
       *                    time the order will remain on the order books before it is
       *                    cancelled and the un-spent funds are returned to the seller's
       *                    account
       * @param fill_or_kill if true, the order will only be included in the blockchain
       *                     if it is filled immediately; if false, an open order will be
       *                     left on the books to fill any amount that cannot be filled
       *                     immediately.
       * @param broadcast true to broadcast the transaction on the network
       * @returns the signed transaction selling the funds
       */
      signed_transaction sell_asset(string seller_account,
                                    string amount_to_sell,
                                    string   symbol_to_sell,
                                    string min_to_receive,
                                    string   symbol_to_receive,
                                    uint32_t timeout_sec = 0,
                                    bool     fill_or_kill = false,
                                    bool     broadcast = false);

      /** Borrow an asset or update the debt/collateral ratio for the loan.
       *
       * This is the first step in shorting an asset.  Call \c sell_asset() to complete the short.
       *
       * @param borrower_name the name or id of the account associated with the transaction.
       * @param amount_to_borrow the amount of the asset being borrowed.  Make this value
       *                         negative to pay back debt.
       * @param asset_symbol the symbol or id of the asset being borrowed.
       * @param amount_of_collateral the amount of the backing asset to add to your collateral
       *        position.  Make this negative to claim back some of your collateral.
       *        The backing asset is defined in the \c bitasset_options for the asset being borrowed.
       * @param broadcast true to broadcast the transaction on the network
       * @returns the signed transaction borrowing the asset
       */
      signed_transaction borrow_asset(string borrower_name, string amount_to_borrow, string asset_symbol,
                                      string amount_of_collateral, bool broadcast = false);

      /** Borrow an asset or update the debt/collateral ratio for the loan, with additional options.
       *
       * This is the first step in shorting an asset.  Call \c sell_asset() to complete the short.
       *
       * @param borrower_name the name or id of the account associated with the transaction.
       * @param amount_to_borrow the amount of the asset being borrowed.  Make this value
       *                         negative to pay back debt.
       * @param asset_symbol the symbol or id of the asset being borrowed.
       * @param amount_of_collateral the amount of the backing asset to add to your collateral
       *        position.  Make this negative to claim back some of your collateral.
       *        The backing asset is defined in the \c bitasset_options for the asset being borrowed.
       * @param extensions additional options
       * @param broadcast true to broadcast the transaction on the network
       * @returns the signed transaction borrowing the asset
       */
      signed_transaction borrow_asset_ext( string borrower_name, string amount_to_borrow, string asset_symbol,
                                           string amount_of_collateral,
                                           call_order_update_operation::extensions_type extensions,
                                           bool broadcast = false );

      /** Cancel an existing order
       *
       * @param order_id the id of order to be cancelled
       * @param broadcast true to broadcast the transaction on the network
       * @returns the signed transaction canceling the order
       */
      signed_transaction cancel_order(object_id_type order_id, bool broadcast = false);

      /** Creates a new user-issued or market-issued asset.
       *
       * Many options can be changed later using \c update_asset()
       *
       * Right now this function is difficult to use because you must provide raw JSON data
       * structures for the options objects, and those include prices and asset ids.
       *
       * @param issuer the name or id of the account who will pay the fee and become the
       *               issuer of the new asset.  This can be updated later
       * @param symbol the ticker symbol of the new asset
       * @param precision the number of digits of precision to the right of the decimal point,
       *                  must be less than or equal to 12
       * @param common asset options required for all new assets.
       *               Note that core_exchange_rate technically needs to store the asset ID of
       *               this new asset. Since this ID is not known at the time this operation is
       *               created, create this price as though the new asset has instance ID 1, and
       *               the chain will overwrite it with the new asset's ID.
       * @param bitasset_opts options specific to BitAssets.  This may be null unless the
       *               \c market_issued flag is set in common.flags
       * @param broadcast true to broadcast the transaction on the network
       * @returns the signed transaction creating a new asset
       */
      signed_transaction create_asset(string issuer,
                                      string symbol,
                                      uint8_t precision,
                                      asset_options common,
                                      fc::optional<bitasset_options> bitasset_opts,
                                      bool broadcast = false);

      /** Issue new shares of an asset.
       *
       * @param to_account the name or id of the account to receive the new shares
       * @param amount the amount to issue, in nominal units
       * @param symbol the ticker symbol of the asset to issue
       * @param memo a memo to include in the transaction, readable by the recipient
       * @param broadcast true to broadcast the transaction on the network
       * @returns the signed transaction issuing the new shares
       */
      signed_transaction issue_asset(string to_account, string amount,
                                     string symbol,
                                     string memo,
                                     bool broadcast = false);

      /** Update the core options on an asset.
       * There are a number of options which all assets in the network use. These options are
       * enumerated in the asset_object::asset_options struct. This command is used to update
       * these options for an existing asset.
       *
       * @note This operation cannot be used to update BitAsset-specific options. For these options,
       * \c update_bitasset() instead.
       *
       * @param symbol the name or id of the asset to update
       * @param new_issuer if changing the asset's issuer, the name or id of the new issuer.
       *                   null if you wish to remain the issuer of the asset
       * @param new_options the new asset_options object, which will entirely replace the existing
       *                    options.
       * @param broadcast true to broadcast the transaction on the network
       * @returns the signed transaction updating the asset
       */
      signed_transaction update_asset(string symbol,
                                      optional<string> new_issuer,
                                      asset_options new_options,
                                      bool broadcast = false);

      /** Update the issuer of an asset
       * Since this call requires the owner authority of the current issuer to sign the transaction,
       * a separated operation is used to change the issuer. This call simplifies the use of this action.
       *
       * @note This operation requires the owner key to be available in the wallet.
       *
       * @param symbol the name or id of the asset to update
       * @param new_issuer if changing the asset's issuer, the name or id of the new issuer.
       * @param broadcast true to broadcast the transaction on the network
       * @returns the signed transaction updating the asset
       */
      signed_transaction update_asset_issuer(string symbol,
                                             string new_issuer,
                                             bool broadcast = false);

      /** Update the options specific to a BitAsset.
       *
       * BitAssets have some options which are not relevant to other asset types. This operation is used to update those
       * options an an existing BitAsset.
       *
       * @see update_asset()
       *
       * @param symbol the name or id of the asset to update, which must be a market-issued asset
       * @param new_options the new bitasset_options object, which will entirely replace the existing
       *                    options.
       * @param broadcast true to broadcast the transaction on the network
       * @returns the signed transaction updating the bitasset
       */
      signed_transaction update_bitasset(string symbol,
                                         bitasset_options new_options,
                                         bool broadcast = false);

      /** Update the set of feed-producing accounts for a BitAsset.
       *
       * BitAssets have price feeds selected by taking the median values of recommendations from a set of feed producers.
       * This command is used to specify which accounts may produce feeds for a given BitAsset.
       * @param symbol the name or id of the asset to update
       * @param new_feed_producers a list of account names or ids which are authorized to produce feeds for the asset.
       *                           this list will completely replace the existing list
       * @param broadcast true to broadcast the transaction on the network
       * @returns the signed transaction updating the bitasset's feed producers
       */
      signed_transaction update_asset_feed_producers(string symbol,
                                                     flat_set<string> new_feed_producers,
                                                     bool broadcast = false);

      /** Publishes a price feed for the named asset.
       *
       * Price feed providers use this command to publish their price feeds for market-issued assets. A price feed is
       * used to tune the market for a particular market-issued asset. For each value in the feed, the median across all
       * committee_member feeds for that asset is calculated and the market for the asset is configured with the median of that
       * value.
       *
       * The feed object in this command contains three prices: a call price limit, a short price limit, and a settlement price.
       * The call limit price is structured as (collateral asset) / (debt asset) and the short limit price is structured
       * as (asset for sale) / (collateral asset). Note that the asset IDs are opposite to eachother, so if we're
       * publishing a feed for USD, the call limit price will be CORE/USD and the short limit price will be USD/CORE. The
       * settlement price may be flipped either direction, as long as it is a ratio between the market-issued asset and
       * its collateral.
       *
       * @param publishing_account the account publishing the price feed
       * @param symbol the name or id of the asset whose feed we're publishing
       * @param feed the price_feed object containing the three prices making up the feed
       * @param broadcast true to broadcast the transaction on the network
       * @returns the signed transaction updating the price feed for the given asset
       */
      signed_transaction publish_asset_feed(string publishing_account,
                                            string symbol,
                                            price_feed feed,
                                            bool broadcast = false);

      /** Pay into the fee pool for the given asset.
       *
       * User-issued assets can optionally have a pool of the core asset which is
       * automatically used to pay transaction fees for any transaction using that
       * asset (using the asset's core exchange rate).
       *
       * This command allows anyone to deposit the core asset into this fee pool.
       *
       * @param from the name or id of the account sending the core asset
       * @param symbol the name or id of the asset whose fee pool you wish to fund
       * @param amount the amount of the core asset to deposit
       * @param broadcast true to broadcast the transaction on the network
       * @returns the signed transaction funding the fee pool
       */
      signed_transaction fund_asset_fee_pool(string from,
                                             string symbol,
                                             string amount,
                                             bool broadcast = false);

      /** Claim funds from the fee pool for the given asset.
       *
       * User-issued assets can optionally have a pool of the core asset which is
       * automatically used to pay transaction fees for any transaction using that
       * asset (using the asset's core exchange rate).
       *
       * This command allows the issuer to withdraw those funds from the fee pool.
       *
       * @param symbol the name or id of the asset whose fee pool you wish to claim
       * @param amount the amount of the core asset to withdraw
       * @param broadcast true to broadcast the transaction on the network
       * @returns the signed transaction claiming from the fee pool
       */
      signed_transaction claim_asset_fee_pool(string symbol,
                                              string amount,
                                              bool broadcast = false);

      /** Burns an amount of given asset.
       *
       * This command burns an amount of given asset to reduce the amount in circulation.
       * @note you cannot burn market-issued assets.
       * @param from the account containing the asset you wish to burn
       * @param amount the amount to burn, in nominal units
       * @param symbol the name or id of the asset to burn
       * @param broadcast true to broadcast the transaction on the network
       * @returns the signed transaction burning the asset
       */
      signed_transaction reserve_asset(string from,
                                    string amount,
                                    string symbol,
                                    bool broadcast = false);

      /** Forces a global settling of the given asset (black swan or prediction markets).
       *
       * In order to use this operation, asset_to_settle must have the global_settle flag set
       *
       * When this operation is executed all open margin positions are called at the settle price.
       * A pool will be formed containing the collateral got from the margin positions.
       * Users owning an amount of the asset may use \c settle_asset() to claim collateral instantly
       * at the settle price from the pool.
       * If this asset is used as backing for other bitassets, those bitassets will not be affected.
       *
       * @note this operation is used only by the asset issuer.
       *
       * @param symbol the name or id of the asset to globally settle
       * @param settle_price the price at which to settle
       * @param broadcast true to broadcast the transaction on the network
       * @returns the signed transaction settling the named asset
       */
      signed_transaction global_settle_asset(string symbol,
                                             price settle_price,
                                             bool broadcast = false);

      /** Schedules a market-issued asset for automatic settlement.
       *
       * Holders of market-issued assests may request a forced settlement for some amount of their asset.
       * This means that the specified sum will be locked by the chain and held for the settlement period,
       * after which time the chain will
       * choose a margin posision holder and buy the settled asset using the margin's collateral.
       * The price of this sale
       * will be based on the feed price for the market-issued asset being settled.
       * The exact settlement price will be the
       * feed price at the time of settlement with an offset in favor of the margin position, where the offset is a
       * blockchain parameter set in the global_property_object.
       *
       * @param account_to_settle the name or id of the account owning the asset
       * @param amount_to_settle the amount of the named asset to schedule for settlement
       * @param symbol the name or id of the asset to settle
       * @param broadcast true to broadcast the transaction on the network
       * @returns the signed transaction settling the named asset
       */
      signed_transaction settle_asset(string account_to_settle,
                                      string amount_to_settle,
                                      string symbol,
                                      bool broadcast = false);

      /** Creates or updates a bid on an MPA after global settlement.
       *
       * In order to revive a market-pegged asset after global settlement (aka
       * black swan), investors can bid collateral in order to take over part of
       * the debt and the settlement fund, see BSIP-0018. Updating an existing
       * bid to cover 0 debt will delete the bid.
       *
       * @param bidder_name the name or id of the account making the bid
       * @param debt_amount the amount of debt of the named asset to bid for
       * @param debt_symbol the name or id of the MPA to bid for
       * @param additional_collateral the amount of additional collateral to bid
       *        for taking over debt_amount. The asset type of this amount is
       *        determined automatically from debt_symbol.
       * @param broadcast true to broadcast the transaction on the network
       * @returns the signed transaction creating/updating the bid
       */
      signed_transaction bid_collateral(string bidder_name, string debt_amount, string debt_symbol,
                                        string additional_collateral, bool broadcast = false);

      /** Whitelist and blacklist accounts, primarily for transacting in whitelisted assets.
       *
       * Accounts can freely specify opinions about other accounts, in the form of either whitelisting or blacklisting
       * them. This information is used in chain validation only to determine whether an account is authorized to
       * transact in an asset type which enforces a whitelist, but third parties can use this information for other
       * uses as well, as long as it does not conflict with the use of whitelisted assets.
       *
       * An asset which enforces a whitelist specifies a list of accounts to maintain its whitelist, and a list of
       * accounts to maintain its blacklist. In order for a given account A to hold and transact in a whitelisted
       * asset S, A must be whitelisted by at least one of S's whitelist_authorities and blacklisted by none of S's
       * blacklist_authorities. If A receives a balance of S, and is later removed from the whitelist(s) which allowed
       * it to hold S, or added to any blacklist S specifies as authoritative, A's balance of S will be frozen until
       * A's authorization is reinstated.
       *
       * @param authorizing_account the account who is doing the whitelisting
       * @param account_to_list the account being whitelisted
       * @param new_listing_status the new whitelisting status
       * @param broadcast true to broadcast the transaction on the network
       * @returns the signed transaction changing the whitelisting status
       */
      signed_transaction whitelist_account(string authorizing_account,
                                           string account_to_list,
                                           account_whitelist_operation::account_listing new_listing_status,
                                           bool broadcast = false);

      /** Creates a committee_member object owned by the given account.
       *
       * An account can have at most one committee_member object.
       *
       * @param owner_account the name or id of the account which is creating the committee_member
       * @param url a URL to include in the committee_member record in the blockchain.  Clients may
       *            display this when showing a list of committee_members.  May be blank.
       * @param broadcast true to broadcast the transaction on the network
       * @returns the signed transaction registering a committee_member
       */
      signed_transaction create_committee_member(string owner_account,
                                         string url,
                                         bool broadcast = false);

      /** Lists all witnesses registered in the blockchain.
       * This returns a list of all account names that own witnesses, and the associated witness id,
       * sorted by name.  This lists witnesses whether they are currently voted in or not.
       *
       * Use the \c lowerbound and limit parameters to page through the list.  To retrieve all witnesss,
       * start by setting \c lowerbound to the empty string \c "", and then each iteration, pass
       * the last witness name returned as the \c lowerbound for the next \c list_witnesss() call.
       *
       * @param lowerbound the name of the first witness to return.  If the named witness does not exist,
       *                   the list will start at the witness that comes after \c lowerbound
       * @param limit the maximum number of witnesss to return (max: 1000)
       * @returns a list of witnesss mapping witness names to witness ids
       */
      map<string,witness_id_type>       list_witnesses(const string& lowerbound, uint32_t limit);

      /** Lists all committee_members registered in the blockchain.
       * This returns a list of all account names that own committee_members, and the associated committee_member id,
       * sorted by name.  This lists committee_members whether they are currently voted in or not.
       *
       * Use the \c lowerbound and limit parameters to page through the list.  To retrieve all committee_members,
       * start by setting \c lowerbound to the empty string \c "", and then each iteration, pass
       * the last committee_member name returned as the \c lowerbound for the next \c list_committee_members() call.
       *
       * @param lowerbound the name of the first committee_member to return.  If the named committee_member does not
       *                   exist, the list will start at the committee_member that comes after \c lowerbound
       * @param limit the maximum number of committee_members to return (max: 1000)
       * @returns a list of committee_members mapping committee_member names to committee_member ids
       */
      map<string, committee_member_id_type>       list_committee_members(const string& lowerbound, uint32_t limit);

      /** Returns information about the given witness.
       * @param owner_account the name or id of the witness account owner, or the id of the witness
       * @returns the information about the witness stored in the block chain
       */
      witness_object get_witness(string owner_account);

      /** Returns information about the given committee_member.
       * @param owner_account the name or id of the committee_member account owner, or the id of the committee_member
       * @returns the information about the committee_member stored in the block chain
       */
      committee_member_object get_committee_member(string owner_account);

      /** Creates a witness object owned by the given account.
       *
       * An account can have at most one witness object.
       *
       * @param owner_account the name or id of the account which is creating the witness
       * @param url a URL to include in the witness record in the blockchain.  Clients may
       *            display this when showing a list of witnesses.  May be blank.
       * @param broadcast true to broadcast the transaction on the network
       * @returns the signed transaction registering a witness
       */
      signed_transaction create_witness(string owner_account,
                                        string url,
                                        bool broadcast = false);

      /**
       * Update a witness object owned by the given account.
       *
       * @param witness_name The name of the witness's owner account.
       *                     Also accepts the ID of the owner account or the ID of the witness.
       * @param url Same as for create_witness.  The empty string makes it remain the same.
       * @param block_signing_key The new block signing public key.  The empty string makes it remain the same.
       * @param broadcast true if you wish to broadcast the transaction.
       * @return the signed transaction
       */
      signed_transaction update_witness(string witness_name,
                                        string url,
                                        string block_signing_key,
                                        bool broadcast = false);


      /**
       * Create a worker object.
       *
       * @param owner_account The account which owns the worker and will be paid
       * @param work_begin_date When the work begins
       * @param work_end_date When the work ends
       * @param daily_pay Amount of pay per day (NOT per maint interval)
       * @param name Any text
       * @param url Any text
       * @param worker_settings {"type" : "burn"|"refund"|"vesting", "pay_vesting_period_days" : x}
       * @param broadcast true if you wish to broadcast the transaction.
       * @return the signed transaction
       */
      signed_transaction create_worker(
         string owner_account,
         time_point_sec work_begin_date,
         time_point_sec work_end_date,
         share_type daily_pay,
         string name,
         string url,
         variant worker_settings,
         bool broadcast = false
         );

      /**
       * Update your votes for workers
       *
       * @param account The account which will pay the fee and update votes.
       * @param delta {"vote_for" : [...], "vote_against" : [...], "vote_abstain" : [...]}
       * @param broadcast true if you wish to broadcast the transaction.
       * @return the signed transaction
       */
      signed_transaction update_worker_votes(
         string account,
         worker_vote_delta delta,
         bool broadcast = false
         );

      /**
       * Create a hashed time lock contract
       *
       * @param source The account that will reserve the funds (and pay the fee)
       * @param destination The account that will receive the funds if the preimage is presented
       * @param amount the amount of the asset that is to be traded
       * @param asset_symbol The asset that is to be traded
       * @param hash_algorithm the algorithm used to generate the hash from the preimage. Can be RIPEMD160, SHA1 or SHA256.
       * @param preimage_hash the hash of the preimage
       * @param preimage_size the size of the preimage in bytes
       * @param claim_period_seconds how long after creation until the lock expires
       * @param broadcast true if you wish to broadcast the transaction
       * @return the signed transaction
       */
      signed_transaction htlc_create( string source, string destination, string amount, string asset_symbol,
            string hash_algorithm, const std::string& preimage_hash, uint32_t preimage_size,
            const uint32_t claim_period_seconds, bool broadcast = false );

      /****
       * Update a hashed time lock contract
       *
       * @param htlc_id The object identifier of the HTLC on the blockchain
       * @param issuer Who is performing this operation (and paying the fee)
       * @param preimage the preimage that should evaluate to the preimage_hash
       * @return the signed transaction
       */
      signed_transaction htlc_redeem( string htlc_id, string issuer, const std::string& preimage,
            bool broadcast = false );

      /*****
       * Increase the timelock on an existing HTLC
       *
       * @param htlc_id The object identifier of the HTLC on the blockchain
       * @param issuer Who is performing this operation (and paying the fee)
       * @param seconds_to_add how many seconds to add to the existing timelock
       * @param broadcast true to broadcast to the network
       * @return the signed transaction
       */
      signed_transaction htlc_extend(string htlc_id, string issuer, const uint32_t seconds_to_add,
            bool broadcast = false);

      /**
       * Get information about a vesting balance object or vesting balance objects owned by an account.
       *
       * @param account_name An account name, account ID, or vesting balance object ID.
       * @return a list of vesting balance objects with additional info
       */
      vector< vesting_balance_object_with_info > get_vesting_balances( string account_name );

      /**
       * Withdraw a vesting balance.
       *
       * @param witness_name The account name of the witness, also accepts account ID or vesting balance ID type.
       * @param amount The amount to withdraw.
       * @param asset_symbol The symbol of the asset to withdraw.
       * @param broadcast true if you wish to broadcast the transaction
       * @return the signed transaction
       */
      signed_transaction withdraw_vesting(
         string witness_name,
         string amount,
         string asset_symbol,
         bool broadcast = false);

      /** Vote for a given committee_member.
       *
       * An account can publish a list of all committee_members they approve of.  This
       * command allows you to add or remove committee_members from this list.
       * Each account's vote is weighted according to the number of shares of the
       * core asset owned by that account at the time the votes are tallied.
       *
       * @note you cannot vote against a committee_member, you can only vote for the committee_member
       *       or not vote for the committee_member.
       *
       * @param voting_account the name or id of the account who is voting with their shares
       * @param committee_member the name or id of the committee_member' owner account
       * @param approve true if you wish to vote in favor of that committee_member, false to
       *                remove your vote in favor of that committee_member
       * @param broadcast true if you wish to broadcast the transaction
       * @return the signed transaction changing your vote for the given committee_member
       */
      signed_transaction vote_for_committee_member(string voting_account,
                                           string committee_member,
                                           bool approve,
                                           bool broadcast = false);

      /** Vote for a given witness.
       *
       * An account can publish a list of all witnesses they approve of.  This
       * command allows you to add or remove witnesses from this list.
       * Each account's vote is weighted according to the number of shares of the
       * core asset owned by that account at the time the votes are tallied.
       *
       * @note you cannot vote against a witness, you can only vote for the witness
       *       or not vote for the witness.
       *
       * @param voting_account the name or id of the account who is voting with their shares
       * @param witness the name or id of the witness' owner account
       * @param approve true if you wish to vote in favor of that witness, false to
       *                remove your vote in favor of that witness
       * @param broadcast true if you wish to broadcast the transaction
       * @return the signed transaction changing your vote for the given witness
       */
      signed_transaction vote_for_witness(string voting_account,
                                          string witness,
                                          bool approve,
                                          bool broadcast = false);

      /** Set the voting proxy for an account.
       *
       * If a user does not wish to take an active part in voting, they can choose
       * to allow another account to vote their stake.
       *
       * Setting a vote proxy does not remove your previous votes from the blockchain,
       * they remain there but are ignored.  If you later null out your vote proxy,
       * your previous votes will take effect again.
       *
       * This setting can be changed at any time.
       *
       * @param account_to_modify the name or id of the account to update
       * @param voting_account the name or id of an account authorized to vote account_to_modify's shares,
       *                       or null to vote your own shares
       *
       * @param broadcast true if you wish to broadcast the transaction
       * @return the signed transaction changing your vote proxy settings
       */
      signed_transaction set_voting_proxy(string account_to_modify,
                                          optional<string> voting_account,
                                          bool broadcast = false);

      /** Set your vote for the number of witnesses and committee_members in the system.
       *
       * Each account can voice their opinion on how many committee_members and how many
       * witnesses there should be in the active committee_member/active witness list.  These
       * are independent of each other.  You must vote your approval of at least as many
       * committee_members or witnesses as you claim there should be (you can't say that there should
       * be 20 committee_members but only vote for 10).
       *
       * There are maximum values for each set in the blockchain parameters (currently
       * defaulting to 1001).
       *
       * This setting can be changed at any time.  If your account has a voting proxy
       * set, your preferences will be ignored.
       *
       * @param account_to_modify the name or id of the account to update
       * @param desired_number_of_witnesses desired number of active witnesses
       * @param desired_number_of_committee_members desired number of active committee members
       *
       * @param broadcast true if you wish to broadcast the transaction
       * @return the signed transaction changing your vote proxy settings
       */
      signed_transaction set_desired_witness_and_committee_member_count(string account_to_modify,
                                                                uint16_t desired_number_of_witnesses,
                                                                uint16_t desired_number_of_committee_members,
                                                                bool broadcast = false);

      /** Signs a transaction.
       *
       * Given a fully-formed transaction that is only lacking signatures, this signs
       * the transaction with the necessary keys and optionally broadcasts the transaction
       * @param tx the unsigned transaction
       * @param broadcast true if you wish to broadcast the transaction
       * @return the signed version of the transaction
       */
      signed_transaction sign_transaction(signed_transaction tx, bool broadcast = false);

      /** Signs a transaction.
       *
       * Given a fully-formed transaction that is only lacking signatures, this signs
       * the transaction with the inferred necessary keys and the explicitly provided keys,
       * and optionally broadcasts the transaction
       * @param tx the unsigned transaction
       * @param signing_keys Keys that must be used when signing the transaction
       * @param broadcast true if you wish to broadcast the transaction
       * @return the signed version of the transaction
       */
      signed_transaction sign_transaction2(signed_transaction tx,
                                           const vector<public_key_type>& signing_keys = vector<public_key_type>(),
                                           bool broadcast = true);


      /** Get transaction signers.
       *
       * Returns information about who signed the transaction, specifically,
       * the corresponding public keys of the private keys used to sign the transaction.
       * @param tx the signed transaction
       * @return the set of public_keys
       */
      flat_set<public_key_type> get_transaction_signers(const signed_transaction &tx) const;

      /** Get key references.
       *
       * Returns accounts related to given public keys.
       * @param keys public keys to search for related accounts
       * @return the set of related accounts
       */
      vector<flat_set<account_id_type>> get_key_references(const vector<public_key_type> &keys) const;

      /** Returns an uninitialized object representing a given blockchain operation.
       *
       * This returns a default-initialized object of the given type; it can be used
       * during early development of the wallet when we don't yet have custom commands for
       * creating all of the operations the blockchain supports.
       *
       * Any operation the blockchain supports can be created using the transaction builder's
       * \c add_operation_to_builder_transaction() , but to do that from the CLI you need to
       * know what the JSON form of the operation looks like.  This will give you a template
       * you can fill in.  It's better than nothing.
       *
       * @param operation_type the type of operation to return, must be one of the
       *                       operations defined in `graphene/protocol/operations.hpp`
       *                       (e.g., "global_parameters_update_operation")
       * @return a default-constructed operation of the given type
       */
      operation get_prototype_operation(string operation_type);

      /** Creates a transaction to propose a parameter change.
       *
       * Multiple parameters can be specified if an atomic change is
       * desired.
       *
       * @param proposing_account The account paying the fee to propose the tx
       * @param expiration_time Timestamp specifying when the proposal will either take effect or expire.
       * @param changed_values The values to change; all other chain parameters are filled in with default values
       * @param broadcast true if you wish to broadcast the transaction
       * @return the signed version of the transaction
       */
      signed_transaction propose_parameter_change(
         const string& proposing_account,
         fc::time_point_sec expiration_time,
         const variant_object& changed_values,
         bool broadcast = false);

      /** Propose a fee change.
       *
       * @param proposing_account The account paying the fee to propose the tx
       * @param expiration_time Timestamp specifying when the proposal will either take effect or expire.
       * @param changed_values Map of operation type to new fee.  Operations may be specified by name or ID.
       *    The "scale" key changes the scale.  All other operations will maintain current values.
       * @param broadcast true if you wish to broadcast the transaction
       * @return the signed version of the transaction
       */
      signed_transaction propose_fee_change(
         const string& proposing_account,
         fc::time_point_sec expiration_time,
         const variant_object& changed_values,
         bool broadcast = false);

      /** Approve or disapprove a proposal.
       *
       * @param fee_paying_account The account paying the fee for the op.
       * @param proposal_id The proposal to modify.
       * @param delta Members contain approvals to create or remove.  In JSON you can leave empty members undefined.
       * @param broadcast true if you wish to broadcast the transaction
       * @return the signed version of the transaction
       */
      signed_transaction approve_proposal(
         const string& fee_paying_account,
         const string& proposal_id,
         const approval_delta& delta,
         bool broadcast /* = false */
         );

      /**
       * Returns the order book for the market base:quote.
       * @param base symbol name or ID of the base asset
       * @param quote symbol name or ID of the quote asset
       * @param limit depth of the order book to retrieve, for bids and asks each, capped at 50
       * @return Order book of the market
       */
      order_book get_order_book( const string& base, const string& quote, unsigned limit = 50);

      /** Signs a transaction.
       *
       * Given a fully-formed transaction with or without signatures, signs
       * the transaction with the owned keys and optionally broadcasts the
       * transaction.
       *
       * @param tx the unsigned transaction
       * @param broadcast true if you wish to broadcast the transaction
       *
       * @return the signed transaction
       */
      signed_transaction add_transaction_signature( signed_transaction tx,
                                                    bool broadcast = false );

      void dbg_make_uia(string creator, string symbol);
      void dbg_make_mia(string creator, string symbol);
      void dbg_push_blocks( std::string src_filename, uint32_t count );
      void dbg_generate_blocks( std::string debug_wif_key, uint32_t count );
      void dbg_stream_json_objects( const std::string& filename );
      void dbg_update_object( fc::variant_object update );

      void flood_network(string prefix, uint32_t number_of_transactions);

      void network_add_nodes( const vector<string>& nodes );
      vector< variant > network_get_connected_peers();

      /**
       *  Used to transfer from one set of blinded balances to another
       */
      blind_confirmation blind_transfer_help( string from_key_or_label,
                                         string to_key_or_label,
                                         string amount,
                                         string symbol,
                                         bool broadcast = false,
                                         bool to_temp = false );


      std::map<string,std::function<string(fc::variant,const fc::variants&)>> get_result_formatters() const;

      fc::signal<void(bool)> lock_changed;
      std::shared_ptr<detail::wallet_api_impl> my;
      void encrypt_keys();

      /**
       * Manage account storage map(key->value) by using the custom operations plugin.
       *
       * Each account can optionally add random information in the form of a key-value map
       * to be retrieved by any interested party.
       *
       * @param account The account ID or name that we are adding additional information to.
       * @param catalog The name of the catalog the operation will insert data to.
       * @param remove true if you want to remove stuff from a catalog.
       * @param key_values The map to be inserted/removed to/from the catalog
       * @param broadcast true if you wish to broadcast the transaction
       *
       * @return The signed transaction
       */
      signed_transaction account_store_map(string account, string catalog, bool remove,
            flat_map<string, optional<string>> key_values, bool broadcast);

      /**
       * Get \c account_storage_object of an account by using the custom operations plugin.
       *
       * Storage data added to the map with @ref account_store_map will be returned.
       *
       * @param account Account ID or name to get contact data from.
       * @param catalog The catalog to retrieve.
       *
       * @return An \c account_storage_object or empty.
       */
      vector<account_storage_object> get_account_storage(string account, string catalog);

};

} }

extern template class fc::api<graphene::wallet::wallet_api>;

FC_API( graphene::wallet::wallet_api,
        (help)
        (gethelp)
        (info)
        (about)
        (begin_builder_transaction)
        (add_operation_to_builder_transaction)
        (replace_operation_in_builder_transaction)
        (set_fees_on_builder_transaction)
        (preview_builder_transaction)
        (sign_builder_transaction)
        (sign_builder_transaction2)
        (broadcast_transaction)
        (propose_builder_transaction)
        (propose_builder_transaction2)
        (remove_builder_transaction)
        (is_new)
        (is_locked)
        (lock)(unlock)(set_password)
        (dump_private_keys)
        (list_my_accounts)
        (list_accounts)
        (list_account_balances)
        (list_assets)
        (get_asset_count)
        (import_key)
        (import_accounts)
        (import_account_keys)
        (import_balance)
        (suggest_brain_key)
        (derive_owner_keys_from_brain_key)
        (register_account)
        (upgrade_account)
        (create_account_with_brain_key)
        (sell_asset)
        (borrow_asset)
        (borrow_asset_ext)
        (cancel_order)
        (transfer)
        (transfer2)
        (get_transaction_id)
        (create_asset)
        (update_asset)
        (update_asset_issuer)
        (update_bitasset)
        (get_htlc)
        (update_asset_feed_producers)
        (publish_asset_feed)
        (issue_asset)
        (get_asset)
        (get_bitasset_data)
        (fund_asset_fee_pool)
        (claim_asset_fee_pool)
        (reserve_asset)
        (global_settle_asset)
        (settle_asset)
        (bid_collateral)
        (whitelist_account)
        (create_committee_member)
        (get_witness)
        (get_committee_member)
        (list_witnesses)
        (list_committee_members)
        (create_witness)
        (update_witness)
        (create_worker)
        (update_worker_votes)
        (htlc_create)
        (htlc_redeem)
        (htlc_extend)
        (get_vesting_balances)
        (withdraw_vesting)
        (vote_for_committee_member)
        (vote_for_witness)
        (set_voting_proxy)
        (set_desired_witness_and_committee_member_count)
        (get_account)
        (get_account_id)
        (get_block)
        (get_account_count)
        (get_account_history)
        (get_relative_account_history)
        (get_account_history_by_operations)
        (get_collateral_bids)
        (is_public_key_registered)
        (get_full_account)
        (get_market_history)
        (get_global_properties)
        (get_dynamic_global_properties)
        (get_object)
        (get_private_key)
        (load_wallet_file)
        (normalize_brain_key)
        (get_account_limit_orders)
        (get_limit_orders)
        (get_call_orders)
        (get_settle_orders)
        (save_wallet_file)
        (serialize_transaction)
        (sign_transaction)
        (sign_transaction2)
        (add_transaction_signature)
        (get_transaction_signers)
        (get_key_references)
        (get_prototype_operation)
        (propose_parameter_change)
        (propose_fee_change)
        (approve_proposal)
        (dbg_make_uia)
        (dbg_make_mia)
        (dbg_push_blocks)
        (dbg_generate_blocks)
        (dbg_stream_json_objects)
        (dbg_update_object)
        (flood_network)
        (network_add_nodes)
        (network_get_connected_peers)
        (sign_memo)
        (read_memo)
        (sign_message)
        (verify_message)
        (verify_signed_message)
        (verify_encapsulated_message)
        (set_key_label)
        (get_key_label)
        (get_public_key)
        (get_blind_accounts)
        (get_my_blind_accounts)
        (get_blind_balances)
        (create_blind_account)
        (transfer_to_blind)
        (transfer_from_blind)
        (blind_transfer)
        (blind_history)
        (receive_blind_transfer)
        (get_order_book)
        (account_store_map)
        (get_account_storage)
        (quit)
      )
