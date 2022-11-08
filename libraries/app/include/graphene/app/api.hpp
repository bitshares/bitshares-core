/*
 * Copyright (c) 2015 Cryptonomex, Inc., and contributors.
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

#include <graphene/app/database_api.hpp>

#include <graphene/protocol/types.hpp>

#include <graphene/market_history/market_history_plugin.hpp>
#include <graphene/grouped_orders/grouped_orders_plugin.hpp>
#include <graphene/custom_operations/custom_operations_plugin.hpp>

#include <graphene/elasticsearch/elasticsearch_plugin.hpp>

#include <graphene/debug_witness/debug_api.hpp>

#include <graphene/net/node.hpp>

#include <fc/api.hpp>
#include <fc/optional.hpp>
#include <fc/crypto/elliptic.hpp>
#include <fc/network/ip.hpp>

#include <boost/container/flat_set.hpp>

#include <functional>
#include <map>
#include <string>
#include <vector>

namespace graphene { namespace app {
   using namespace graphene::chain;
   using namespace graphene::market_history;
   using namespace graphene::grouped_orders;
   using namespace graphene::custom_operations;

   using std::string;
   using std::vector;
   using std::map;

   class application;

   /**
    * @brief The history_api class implements the RPC API for account history
    *
    * This API contains methods to access account histories
    */
   class history_api
   {
      public:
         explicit history_api(application& app);

         struct history_operation_detail
         {
            uint32_t total_count = 0;
            vector<operation_history_object> operation_history_objs;
         };

         /**
          * @brief Get the history of operations related to the specified account
          * @param account_name_or_id The account name or ID whose history should be queried
          * @param stop ID of the earliest operation to retrieve
          * @param limit Maximum number of operations to retrieve, must not exceed the configured value of
          *              @a api_limit_get_account_history
          * @param start ID of the most recent operation to retrieve
          * @return A list of operations related to the specified account, ordered from most recent to oldest.
          */
         vector<operation_history_object> get_account_history(
            const std::string& account_name_or_id,
            operation_history_id_type stop = operation_history_id_type(),
            uint32_t limit = application_options::get_default().api_limit_get_account_history,
            operation_history_id_type start = operation_history_id_type()
         )const;

         /**
          * @brief Get the history of operations related to the specified account no later than the specified time
          * @param account_name_or_id The account name or ID whose history should be queried
          * @param limit Maximum number of operations to retrieve, must not exceed the configured value of
          *              @a api_limit_get_account_history
          * @param start the time point to start looping back through history
          * @return A list of operations related to the specified account, ordered from most recent to oldest.
          *
          * @note
          * 1. If @p account_name_or_id cannot be tied to an account, an empty list will be returned
          * 2. @p limit can be omitted or be @a null, if so the configured value of
          *       @a api_limit_get_account_history will be used
          * 3. @p start can be omitted or be @a null, if so the api will return the "first page" of the history
          * 4. One or more optional parameters can be omitted from the end of the parameter list, and the optional
          *    parameters in the middle cannot be omitted (but can be @a null).
          */
         vector<operation_history_object> get_account_history_by_time(
            const std::string& account_name_or_id,
            const optional<uint32_t>& limit = optional<uint32_t>(),
            const optional<fc::time_point_sec>& start = optional<fc::time_point_sec>()
         )const;

         /**
          * @brief Get the history of operations related to the specified account filtering by operation types
          * @param account_name_or_id The account name or ID whose history should be queried
          * @param operation_types The IDs of the operation we want to get operations in the account
          *                        ( 0 = transfer , 1 = limit order create, ...)
          * @param start the sequence number where to start looping back through the history
          * @param limit the max number of entries to return (from start number), must not exceed the configured
          *              value of @a api_limit_get_account_history_by_operations
          * @return history_operation_detail
          */
         history_operation_detail get_account_history_by_operations(
            const std::string& account_name_or_id,
            const flat_set<uint16_t>& operation_types,
            uint32_t start,
            uint32_t limit
         )const;

         /**
          * @brief Get the history of operations related to the specified account filtering by operation type
          * @param account_name_or_id The account name or ID whose history should be queried
          * @param operation_type The type of the operation we want to get operations in the account
          *                       ( 0 = transfer , 1 = limit order create, ...)
          * @param stop ID of the earliest operation to retrieve
          * @param limit Maximum number of operations to retrieve, must not exceed the configured value of
          *              @a api_limit_get_account_history_operations
          * @param start ID of the most recent operation to retrieve
          * @return A list of operations related to the specified account, ordered from most recent to oldest.
          */
         vector<operation_history_object> get_account_history_operations(
            const std::string& account_name_or_id,
            int64_t operation_type,
            operation_history_id_type start = operation_history_id_type(),
            operation_history_id_type stop = operation_history_id_type(),
            uint32_t limit = application_options::get_default().api_limit_get_account_history_operations
         )const;

         /**
          * @brief Get the history of operations related to the specified account referenced
          *        by an event numbering specific to the account. The current number of operations
          *        for the account can be found in the account statistics (or use 0 for start).
          * @param account_name_or_id The account name or ID whose history should be queried
          * @param stop Sequence number of earliest operation. 0 is default and will
          *             query 'limit' number of operations.
          * @param limit Maximum number of operations to retrieve, must not exceed the configured value of
          *              @a api_limit_get_relative_account_history
          * @param start Sequence number of the most recent operation to retrieve.
          *              0 is default, which will start querying from the most recent operation.
          * @return A list of operations related to the specified account, ordered from most recent to oldest.
          */
         vector<operation_history_object> get_relative_account_history(
               const std::string& account_name_or_id,
               uint64_t stop = 0,
               uint32_t limit = application_options::get_default().api_limit_get_relative_account_history,
               uint64_t start = 0) const;

         /**
          * @brief Get all operations within a block or a transaction, including virtual operations
          * @param block_num the number (height) of the block to fetch
          * @param trx_in_block the sequence of a transaction in the block, starts from @a 0, optional.
          *                     If specified, will return only operations of that transaction.
          *                     If omitted, will return all operations in the specified block.
          * @return a list of @a operation_history objects ordered by ID
          *
          * @note the data is fetched from the @a account_history plugin, so results may be
          *       incomplete due to the @a partial-operations option configured in the API node.
          *       To get complete data, it is recommended to query from ElasticSearch where the data is
          *       maintained by the @a elastic_search plugin.
          */
         vector<operation_history_object> get_block_operation_history(
               uint32_t block_num,
               const optional<uint16_t>& trx_in_block = {} ) const;

         /**
          * @brief Get all operations, including virtual operations, within the most recent block
          *        (no later than the specified time) containing at least one operation
          * @param start time point, optional, if omitted, the data of the latest block containing at least
          *              one operation will be returned
          * @return a list of @a operation_history objects ordered by ID in descending order
          *
          * @note the data is fetched from the @a account_history plugin, so results may be
          *       incomplete or incorrect due to the @a partial-operations option configured in the API node.
          *       To get complete data, it is recommended to query from ElasticSearch where the data is
          *       maintained by the @a elastic_search plugin.
          */
         vector<operation_history_object> get_block_operations_by_time(
               const optional<fc::time_point_sec>& start = optional<fc::time_point_sec>() ) const;

         /**
          * @brief Get details of order executions occurred most recently in a trading pair
          * @param a Asset symbol or ID in a trading pair
          * @param b The other asset symbol or ID in the trading pair
          * @param limit Maximum records to return
          * @return a list of order_history objects, in "most recent first" order
          */
         vector<order_history_object> get_fill_order_history(
               const std::string& a,
               const std::string& b,
               uint32_t limit )const;

         /**
          * @brief Get OHLCV data of a trading pair in a time range
          * @param a Asset symbol or ID in a trading pair
          * @param b The other asset symbol or ID in the trading pair
          * @param bucket_seconds Length of each time bucket in seconds.
          * Note: it need to be within result of get_market_history_buckets() API, otherwise no data will be returned
          * @param start The start of a time range, E.G. "2018-01-01T00:00:00"
          * @param end The end of the time range
          * @return A list of OHLCV data, in "least recent first" order.
          * If there are more records in the specified time range than the configured value of
          *    @a api_limit_get_market_history, only the first records will be returned.
          */
         vector<bucket_object> get_market_history( const std::string& a, const std::string& b,
                                                   uint32_t bucket_seconds,
                                                   const fc::time_point_sec& start,
                                                   const fc::time_point_sec& end )const;

         /**
          * @brief Get OHLCV time bucket lengths supported (configured) by this API server
          * @return A list of time bucket lengths in seconds. E.G. if the result contains a number "300",
          * it means this API server supports OHLCV data aggregated in 5-minute buckets.
          */
         flat_set<uint32_t> get_market_history_buckets()const;

         /**
          * @brief Get history of a liquidity pool
          * @param pool_id ID of the liquidity pool to query
          * @param start A UNIX timestamp. Optional.
          *              If specified, only the operations occurred not later than this time will be returned.
          * @param stop  A UNIX timestamp. Optional.
          *              If specified, only the operations occurred later than this time will be returned.
          * @param limit Maximum quantity of operations in the history to retrieve. Optional.
          *              If not specified, the configured value of
          *                @a api_limit_get_liquidity_pool_history will be used.
          *              If specified, it must not exceed the configured value of
          *                @a api_limit_get_liquidity_pool_history.
          * @param operation_type Optional. If specified, only the operations whose type is the specified type
          *                       will be returned. Otherwise all operations will be returned.
          * @return operation history of the liquidity pool, ordered by time, most recent first.
          *
          * @note
          * 1. The time must be UTC. The range is (stop, start].
          * 2. In case when there are more operations than @p limit occurred in the same second, this API only returns
          *    the most recent records, the rest records can be retrieved with the
          *    @ref get_liquidity_pool_history_by_sequence API.
          * 3. List of operation type code: 59-creation, 60-deletion, 61-deposit, 62-withdrawal, 63-exchange.
          * 4. One or more optional parameters can be omitted from the end of the parameter list, and the optional
          *    parameters in the middle cannot be omitted (but can be @a null).
          */
         vector<liquidity_pool_history_object> get_liquidity_pool_history(
               liquidity_pool_id_type pool_id,
               const optional<fc::time_point_sec>& start = optional<fc::time_point_sec>(),
               const optional<fc::time_point_sec>& stop = optional<fc::time_point_sec>(),
               const optional<uint32_t>& limit = optional<uint32_t>(),
               const optional<int64_t>& operation_type = optional<int64_t>() )const;

         /**
          * @brief Get history of a liquidity pool
          * @param pool_id ID of the liquidity pool to query
          * @param start An Integer. Optional.
          *              If specified, only the operations whose sequences are not greater than this will be returned.
          * @param stop  A UNIX timestamp. Optional.
          *              If specified, only operations occurred later than this time will be returned.
          * @param limit Maximum quantity of operations in the history to retrieve. Optional.
          *              If not specified, the configured value of
          *                @a api_limit_get_liquidity_pool_history will be used.
          *              If specified, it must not exceed the configured value of
          *                @a api_limit_get_liquidity_pool_history.
          * @param operation_type Optional. If specified, only the operations whose type is the specified type
          *                       will be returned. Otherwise all operations will be returned.
          * @return operation history of the liquidity pool, ordered by time, most recent first.
          *
          * @note
          * 1. The time must be UTC. The range is (stop, start].
          * 2. List of operation type code: 59-creation, 60-deletion, 61-deposit, 62-withdrawal, 63-exchange.
          * 3. One or more optional parameters can be omitted from the end of the parameter list, and the optional
          *    parameters in the middle cannot be omitted (but can be @a null).
          */
         vector<liquidity_pool_history_object> get_liquidity_pool_history_by_sequence(
               liquidity_pool_id_type pool_id,
               const optional<uint64_t>& start = optional<uint64_t>(),
               const optional<fc::time_point_sec>& stop = optional<fc::time_point_sec>(),
               const optional<uint32_t>& limit = optional<uint32_t>(),
               const optional<int64_t>& operation_type = optional<int64_t>() )const;

      private:
           application& _app;
   };

   /**
    * @brief Block api
    */
   class block_api
   {
   public:
      explicit block_api(const graphene::chain::database& db);

      /**
          * @brief Get signed blocks
          * @param block_num_from The lowest block number
          * @param block_num_to The highest block number
          * @return A list of signed blocks from block_num_from till block_num_to
          */
      vector<optional<signed_block>> get_blocks(uint32_t block_num_from, uint32_t block_num_to)const;

   private:
      const graphene::chain::database& _db;
   };


   /**
    * @brief The network_broadcast_api class allows broadcasting of transactions.
    */
   class network_broadcast_api : public std::enable_shared_from_this<network_broadcast_api>
   {
      public:
         explicit network_broadcast_api(application& a);

         struct transaction_confirmation
         {
            transaction_id_type   id;
            uint32_t              block_num;
            uint32_t              trx_num;
            processed_transaction trx;
         };

         using confirmation_callback = std::function<void(variant/*transaction_confirmation*/)>;

         /**
          * @brief Broadcast a transaction to the network
          * @param trx The transaction to broadcast
          *
          * The transaction will be checked for validity in the local database prior to broadcasting. If it fails to
          * apply locally, an error will be thrown and the transaction will not be broadcast.
          */
         void broadcast_transaction(const precomputable_transaction& trx);

         /** This version of broadcast transaction registers a callback method that will be called when the
          * transaction is included into a block.  The callback method includes the transaction id, block number,
          * and transaction number in the block.
          * @param cb the callback method
          * @param trx the transaction
          */
         void broadcast_transaction_with_callback( confirmation_callback cb, const precomputable_transaction& trx);

         /** This version of broadcast transaction waits until the transaction is included into a block,
          *  then the transaction id, block number, and transaction number in the block will be returned.
          * @param trx the transaction
          * @return info about the block including the transaction
          */
         fc::variant broadcast_transaction_synchronous(const precomputable_transaction& trx);

         /**
          * @brief Broadcast a signed block to the network
          * @param block The signed block to broadcast
          */
         void broadcast_block( const signed_block& block );

         /**
          * @brief Not reflected, thus not accessible to API clients.
          *
          * This function is registered to receive the applied_block
          * signal from the chain database when a block is received.
          * It then dispatches callbacks to clients who have requested
          * to be notified when a particular txid is included in a block.
          */
         void on_applied_block( const signed_block& b );
      private:
         boost::signals2::scoped_connection             _applied_block_connection;
         map<transaction_id_type,confirmation_callback> _callbacks;
         application&                                   _app;
   };

   /**
    * @brief The network_node_api class allows maintenance of p2p connections.
    */
   class network_node_api
   {
      public:
         explicit network_node_api(application& a);

         /**
          * @brief Return general network information, such as p2p port
          */
         fc::variant_object get_info() const;

         /**
          * @brief add_node Connect to a new peer
          * @param ep The IP/Port of the peer to connect to
          */
         void add_node(const fc::ip::endpoint& ep);

         /**
          * @brief Get status of all current connections to peers
          */
         std::vector<net::peer_status> get_connected_peers() const;

         /**
          * @brief Get advanced node parameters, such as desired and max
          *        number of connections
          */
         fc::variant_object get_advanced_node_parameters() const;

         /**
          * @brief Set advanced node parameters, such as desired and max
          *        number of connections
          * @param params a JSON object containing the name/value pairs for the parameters to set
          */
         void set_advanced_node_parameters(const fc::variant_object& params);

         /**
          * @brief Return list of potential peers
          */
         std::vector<net::potential_peer_record> get_potential_peers() const;

      private:
         application& _app;
   };

   /**
    * @brief The crypto_api class allows computations related to blinded transfers.
    */
   class crypto_api
   {
      public:

         struct verify_range_result
         {
            bool        success;
            uint64_t    min_val;
            uint64_t    max_val;
         };

         struct verify_range_proof_rewind_result
         {
            bool                          success;
            uint64_t                      min_val;
            uint64_t                      max_val;
            uint64_t                      value_out;
            fc::ecc::blind_factor_type    blind_out;
            string                        message_out;
         };

         /**
          * @brief Generates a pedersen commitment: *commit = blind * G + value * G2.
          * The commitment is 33 bytes, the blinding factor is 32 bytes.
          * For more information about pederson commitment check url https://en.wikipedia.org/wiki/Commitment_scheme
          * @param blind Sha-256 blind factor type
          * @param value Positive 64-bit integer value
          * @return A 33-byte pedersen commitment: *commit = blind * G + value * G2
          */
         fc::ecc::commitment_type blind( const fc::ecc::blind_factor_type& blind, uint64_t value ) const;

         /**
          * @brief Get sha-256 blind factor type
          * @param blinds_in List of sha-256 blind factor types
          * @param non_neg 32-bit integer value
          * @return A blind factor type
          */
         fc::ecc::blind_factor_type blind_sum( const std::vector<blind_factor_type>& blinds_in,
                                               uint32_t non_neg ) const;

         /**
          * @brief Verifies that commits + neg_commits + excess == 0
          * @param commits_in List of 33-byte pedersen commitments
          * @param neg_commits_in List of 33-byte pedersen commitments
          * @param excess Sum of two list of 33-byte pedersen commitments
          *               where sums the first set and subtracts the second
          * @return Boolean - true in event of commits + neg_commits + excess == 0, otherwise false
          */
         bool verify_sum(
            const std::vector<commitment_type>& commits_in,
            const std::vector<commitment_type>& neg_commits_in,
            int64_t excess
         ) const;

         /**
          * @brief Verifies range proof for 33-byte pedersen commitment
          * @param commit 33-byte pedersen commitment
          * @param proof List of characters
          * @return A structure with success, min and max values
          */
         verify_range_result verify_range( const fc::ecc::commitment_type& commit,
                                           const std::vector<char>& proof ) const;

         /**
          * @brief Proves with respect to min_value the range for pedersen
          * commitment which has the provided blinding factor and value
          * @param min_value Positive 64-bit integer value
          * @param commit 33-byte pedersen commitment
          * @param commit_blind Sha-256 blind factor type for the correct digits
          * @param nonce Sha-256 blind factor type for our non-forged signatures
          * @param base10_exp Exponents base 10 in range [-1 ; 18] inclusively
          * @param min_bits 8-bit positive integer, must be in range [0 ; 64] inclusively
          * @param actual_value 64-bit positive integer, must be greater or equal min_value
          * @return A list of characters as proof in proof
          */
         std::vector<char> range_proof_sign( uint64_t min_value,
                                             const commitment_type& commit,
                                             const blind_factor_type& commit_blind,
                                             const blind_factor_type& nonce,
                                             int8_t base10_exp,
                                             uint8_t min_bits,
                                             uint64_t actual_value ) const;

         /**
          * @brief Verifies range proof rewind for 33-byte pedersen commitment
          * @param nonce Sha-256 blind refactor type
          * @param commit 33-byte pedersen commitment
          * @param proof List of characters
          * @return A structure with success, min, max, value_out, blind_out and message_out values
          */
         verify_range_proof_rewind_result verify_range_proof_rewind( const blind_factor_type& nonce,
                                                                     const fc::ecc::commitment_type& commit,
                                                                     const std::vector<char>& proof ) const;

         /**
          * @brief Gets "range proof" info. The cli_wallet includes functionality for sending blind transfers
          * in which the values of the input and outputs amounts are “blinded.”
          * In the case where a transaction produces two or more outputs, (e.g. an amount to the intended
          * recipient plus “change” back to the sender),
          * a "range proof" must be supplied to prove that none of the outputs commit to a negative value.
          * @param proof List of proof's characters
          * @return A range proof info structure with exponent, mantissa, min and max values
          */
         fc::ecc::range_proof_info range_get_info( const std::vector<char>& proof ) const;
   };

   /**
    * @brief The asset_api class allows query of info about asset holders.
    */
   class asset_api
   {
      public:
         explicit asset_api(graphene::app::application& app);

         struct account_asset_balance
         {
            string          name;
            account_id_type account_id;
            share_type      amount;
         };
         struct asset_holders
         {
            asset_id_type   asset_id;
            int64_t         count;
         };

         /**
          * @brief Get asset holders for a specific asset
          * @param asset_symbol_or_id The specific asset symbol or ID
          * @param start The start index
          * @param limit Maximum number of accounts to retrieve, must not exceed the configured value of
          *              @a api_limit_get_asset_holders
          * @return A list of asset holders for the specified asset
          */
         vector<account_asset_balance> get_asset_holders( const std::string& asset_symbol_or_id,
                                                          uint32_t start, uint32_t limit  )const;

         /**
          * @brief Get asset holders count for a specific asset
          * @param asset_symbol_or_id The specific asset symbol or id
          * @return Holders count for the specified asset
          */
         int64_t get_asset_holders_count( const std::string& asset_symbol_or_id )const;

         /**
          * @brief Get all asset holders
          * @return A list of all asset holders
          */
         vector<asset_holders> get_all_asset_holders() const;

      private:
         graphene::app::application& _app;
         graphene::chain::database& _db;
   };

   /**
    * @brief the orders_api class exposes access to data processed with grouped orders plugin.
    */
   class orders_api
   {
      public:
         explicit orders_api(application& app);

         /**
          * @brief summary data of a group of limit orders
          */
         struct limit_order_group
         {
            explicit limit_order_group( const std::pair<limit_order_group_key,limit_order_group_data>& p )
               :  min_price( p.first.min_price ),
                  max_price( p.second.max_price ),
                  total_for_sale( p.second.total_for_sale )
                  {}
            limit_order_group() = default;

            price         min_price; ///< possible lowest price in the group
            price         max_price; ///< possible highest price in the group
            share_type    total_for_sale; ///< total amount of asset for sale, asset id is min_price.base.asset_id
         };

         /**
          * @brief Get tracked groups configured by the server.
          * @return A list of numbers which indicate configured groups, of those, 1 means 0.01% diff on price.
          */
         flat_set<uint16_t> get_tracked_groups()const;

         /**
          * @brief Get grouped limit orders in given market.
          *
          * @param base_asset symbol or ID of asset being sold
          * @param quote_asset symbol or ID of asset being purchased
          * @param group Maximum price diff within each order group, have to be one of configured values
          * @param start Optional price to indicate the first order group to retrieve
          * @param limit Maximum number of order groups to retrieve, must not exceed the configured value of
          *              @a api_limit_get_grouped_limit_orders
          * @return The grouped limit orders, ordered from best offered price to worst
          */
         vector< limit_order_group > get_grouped_limit_orders( const std::string& base_asset,
                                                               const std::string& quote_asset,
                                                               uint16_t group,
                                                               const optional<price>& start,
                                                               uint32_t limit )const;

      private:
         application& _app;
   };

   /**
    * @brief The custom_operations_api class exposes access to standard custom objects parsed by the
    * custom_operations_plugin.
    */
   class custom_operations_api
   {
   public:
      explicit custom_operations_api(application& app);

      /**
       * @brief Get stored objects
       *
       * @param account_name_or_id The account name or ID to get info from. Optional.
       * @param catalog The catalog to get info from. Each account can store data in multiple catalogs. Optional.
       * @param key The key to get info from. Each catalog can contain multiple keys. Optional.
       * @param limit The limitation of items each query can fetch, not greater than the configured value of
       *              @a api_limit_get_storage_info. Optional.
       * @param start_id Start ID of stored object, fetch objects whose IDs are greater than or equal to this ID
       * @return The stored objects found, sorted by their ID
       *
       * @note
       * 1. By passing @a null to various optional parameters, or omitting where applicable, this API can be used to
       *    query stored objects by
       *    a) account, catalog and key, or
       *    b) account and catalog, or
       *    c) account, or
       *    d) catalog and key, or
       *    e) catalog, or
       *    f) unconditionally.
       *    Queries with keys without a catalog are not allowed.
       * 2. If @p account_name_or_id is specified but cannot be tied to an account, an error will be returned.
       * 3. @p limit can be omitted or be @a null, if so the configured value of
       *       @a api_limit_get_storage_info will be used.
       * 4. @p start_id can be omitted or be @a null, if so the API will return the "first page" of objects.
       * 5. One or more optional parameters can be omitted from the end of the parameter list, and the optional
       *    parameters in the middle cannot be omitted (but can be @a null).
       */
      vector<account_storage_object> get_storage_info(
            const optional<std::string>& account_name_or_id = optional<std::string>(),
            const optional<std::string>& catalog = optional<std::string>(),
            const optional<std::string>& key = optional<std::string>(),
            const optional<uint32_t>& limit = optional<uint32_t>(),
            const optional<account_storage_id_type>& start_id = optional<account_storage_id_type>() )const;

   private:
      application& _app;
   };

   /**
    * @brief A dummy API class that does nothing, used when access to database_api is not allowed
    */
   struct dummy_api
   {
      bool dummy() const { return false; }
   };
} } // graphene::app

extern template class fc::api<graphene::app::block_api>;
extern template class fc::api<graphene::app::network_broadcast_api>;
extern template class fc::api<graphene::app::network_node_api>;
extern template class fc::api<graphene::app::history_api>;
extern template class fc::api<graphene::app::crypto_api>;
extern template class fc::api<graphene::app::asset_api>;
extern template class fc::api<graphene::app::orders_api>;
extern template class fc::api<graphene::debug_witness::debug_api>;
extern template class fc::api<graphene::app::custom_operations_api>;
extern template class fc::api<graphene::app::dummy_api>;

namespace graphene { namespace app {
   /**
    * @brief The login_api class implements the bottom layer of the RPC API
    *
    * All other APIs must be requested from this API.
    */
   class login_api
   {
      public:
         explicit login_api(application& a);

         /**
          * @brief Authenticate to the RPC server, or retrieve the API set ID of the @a login API set
          * @param user Username to login with, optional
          * @param password Password to login with, optional
          * @return @a true if to authenticate and logged in successfully,
          *         @a false if to authenticate and failed to log in,
          *         or the API set ID if to retrieve it
          *
          * @note Provide both @p user and @p password to authenticate,
          *       or provide none of them (or @a null without quotes) to retrieve the API set ID
          *          of the @a login API set.
          * @note This is called automatically for authentication when a HTTP or WebSocket connection is established,
          *       assuming credentials are provided with HTTP Basic authentication headers.
          * @note When trying to authenticate again, even if failed to log in, already allocated API set IDs are
          *       still accessible.
          */
         variant login(const optional<string>& user, const optional<string>& password);

         /// @brief Log out
         /// @return @a false
         /// @note Already allocated API set IDs are still accessible after calling this.
         bool logout();

         /// @brief Retrive the node info string configured by the node operator
         string get_info() const;

         /// @brief Retrieve configured application options
         /// @note It requires the user to be logged in and have access to at least one API set other than login_api.
         application_options get_config() const;

         /// @brief Retrieve a list of API sets that the user has access to
         flat_set<string> get_available_api_sets() const;

         /// @brief Retrieve the network block API set
         fc::api<block_api> block();
         /// @brief Retrieve the network broadcast API set
         fc::api<network_broadcast_api> network_broadcast();
         /// @brief Retrieve the database API set
         fc::api<database_api> database();
         /// @brief Retrieve the history API set
         fc::api<history_api> history();
         /// @brief Retrieve the network node API set
         fc::api<network_node_api> network_node();
         /// @brief Retrieve the cryptography API set
         fc::api<crypto_api> crypto();
         /// @brief Retrieve the asset API set
         fc::api<asset_api> asset();
         /// @brief Retrieve the orders API set
         fc::api<orders_api> orders();
         /// @brief Retrieve the debug API set
         fc::api<graphene::debug_witness::debug_api> debug();
         /// @brief Retrieve the custom operations API set
         fc::api<custom_operations_api> custom_operations();

         /// @brief Retrieve a dummy API set, not reflected
         fc::api<dummy_api> dummy();

         /// @brief Check whether database_api is allowed, not reflected
         /// @return @a true if database_api is allowed, @a false otherwise
         bool is_database_api_allowed() const;

      private:
         application& _app;

         flat_set< string > _allowed_apis;

         optional< fc::api<block_api> >                          _block_api;
         optional< fc::api<database_api> >                       _database_api;
         optional< fc::api<network_broadcast_api> >              _network_broadcast_api;
         optional< fc::api<network_node_api> >                   _network_node_api;
         optional< fc::api<history_api> >                        _history_api;
         optional< fc::api<crypto_api> >                         _crypto_api;
         optional< fc::api<asset_api> >                          _asset_api;
         optional< fc::api<orders_api> >                         _orders_api;
         optional< fc::api<graphene::debug_witness::debug_api> > _debug_api;
         optional< fc::api<custom_operations_api> >              _custom_operations_api;
         optional< fc::api<dummy_api> >                          _dummy_api;
   };

}}  // graphene::app

extern template class fc::api<graphene::app::login_api>;

FC_REFLECT( graphene::app::network_broadcast_api::transaction_confirmation,
        (id)(block_num)(trx_num)(trx) )

FC_REFLECT( graphene::app::crypto_api::verify_range_result,
        (success)(min_val)(max_val) )
FC_REFLECT( graphene::app::crypto_api::verify_range_proof_rewind_result,
        (success)(min_val)(max_val)(value_out)(blind_out)(message_out) )

FC_REFLECT( graphene::app::history_api::history_operation_detail,
            (total_count)(operation_history_objs) )

FC_REFLECT( graphene::app::orders_api::limit_order_group,
            (min_price)(max_price)(total_for_sale) )

FC_REFLECT( graphene::app::asset_api::account_asset_balance, (name)(account_id)(amount) )
FC_REFLECT( graphene::app::asset_api::asset_holders, (asset_id)(count) )

FC_API(graphene::app::history_api,
       (get_account_history)
       (get_account_history_by_time)
       (get_account_history_by_operations)
       (get_account_history_operations)
       (get_relative_account_history)
       (get_block_operation_history)
       (get_block_operations_by_time)
       (get_fill_order_history)
       (get_market_history)
       (get_market_history_buckets)
       (get_liquidity_pool_history)
       (get_liquidity_pool_history_by_sequence)
     )
FC_API(graphene::app::block_api,
       (get_blocks)
     )
FC_API(graphene::app::network_broadcast_api,
       (broadcast_transaction)
       (broadcast_transaction_with_callback)
       (broadcast_transaction_synchronous)
       (broadcast_block)
     )
FC_API(graphene::app::network_node_api,
       (get_info)
       (add_node)
       (get_connected_peers)
       (get_potential_peers)
       (get_advanced_node_parameters)
       (set_advanced_node_parameters)
     )
FC_API(graphene::app::crypto_api,
       (blind)
       (blind_sum)
       (verify_sum)
       (verify_range)
       (range_proof_sign)
       (verify_range_proof_rewind)
       (range_get_info)
     )
FC_API(graphene::app::asset_api,
       (get_asset_holders)
       (get_asset_holders_count)
       (get_all_asset_holders)
     )
FC_API(graphene::app::orders_api,
       (get_tracked_groups)
       (get_grouped_limit_orders)
     )
FC_API(graphene::app::custom_operations_api,
       (get_storage_info)
     )
FC_API(graphene::app::dummy_api,
       (dummy)
     )
FC_API(graphene::app::login_api,
       (login)
       (logout)
       (get_info)
       (get_config)
       (get_available_api_sets)
       (block)
       (network_broadcast)
       (database)
       (history)
       (network_node)
       (crypto)
       (asset)
       (orders)
       (debug)
       (custom_operations)
     )
