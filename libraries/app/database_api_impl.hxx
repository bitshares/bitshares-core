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

#include <graphene/app/database_api.hpp>

#include <fc/bloom_filter.hpp>

#define GET_REQUIRED_FEES_MAX_RECURSION 4

namespace graphene { namespace app {

typedef std::map< std::pair<graphene::chain::asset_id_type, graphene::chain::asset_id_type>,
                  std::vector<fc::variant> > market_queue_type;

class database_api_impl : public std::enable_shared_from_this<database_api_impl>
{
   public:
      explicit database_api_impl( graphene::chain::database& db, const application_options* app_options );
      virtual ~database_api_impl();

      // Objects
      fc::variants get_objects( const vector<object_id_type>& ids, optional<bool> subscribe )const;

      // Subscriptions
      void set_subscribe_callback( std::function<void(const variant&)> cb, bool notify_remove_create );
      void set_auto_subscription( bool enable );
      void set_pending_transaction_callback( std::function<void(const variant&)> cb );
      void set_block_applied_callback( std::function<void(const variant& block_id)> cb );
      void cancel_all_subscriptions(bool reset_callback, bool reset_market_subscriptions);

      // Blocks and transactions
      optional<block_header> get_block_header(uint32_t block_num)const;
      map<uint32_t, optional<block_header>> get_block_header_batch(const vector<uint32_t> block_nums)const;
      optional<signed_block> get_block(uint32_t block_num)const;
      processed_transaction get_transaction( uint32_t block_num, uint32_t trx_in_block )const;

      // Globals
      chain_property_object get_chain_properties()const;
      global_property_object get_global_properties()const;
      fc::variant_object get_config()const;
      chain_id_type get_chain_id()const;
      dynamic_global_property_object get_dynamic_global_properties()const;

      // Keys
      vector<flat_set<account_id_type>> get_key_references( vector<public_key_type> key )const;
      bool is_public_key_registered(string public_key) const;

      // Accounts
      account_id_type get_account_id_from_string(const std::string& name_or_id)const;
      vector<optional<account_object>> get_accounts( const vector<std::string>& account_names_or_ids,
                                                     optional<bool> subscribe )const;
      std::map<string,full_account> get_full_accounts( const vector<string>& names_or_ids,
                                                       optional<bool> subscribe );
      optional<account_object> get_account_by_name( string name )const;
      vector<account_id_type> get_account_references( const std::string account_id_or_name )const;
      vector<optional<account_object>> lookup_account_names(const vector<string>& account_names)const;
      map<string,account_id_type> lookup_accounts( const string& lower_bound_name,
                                                   uint32_t limit,
                                                   optional<bool> subscribe )const;
      uint64_t get_account_count()const;

      // Balances
      vector<asset> get_account_balances( const std::string& account_name_or_id,
                                          const flat_set<asset_id_type>& assets )const;
      vector<asset> get_named_account_balances(const std::string& name, const flat_set<asset_id_type>& assets)const;
      vector<balance_object> get_balance_objects( const vector<address>& addrs )const;
      vector<asset> get_vested_balances( const vector<balance_id_type>& objs )const;
      vector<vesting_balance_object> get_vesting_balances( const std::string account_id_or_name )const;

      // Assets
      uint64_t get_asset_count()const;
      asset_id_type get_asset_id_from_string(const std::string& symbol_or_id)const;
      vector<optional<extended_asset_object>> get_assets( const vector<std::string>& asset_symbols_or_ids,
                                                          optional<bool> subscribe )const;
      vector<extended_asset_object>           list_assets(const string& lower_bound_symbol, uint32_t limit)const;
      vector<optional<extended_asset_object>> lookup_asset_symbols(const vector<string>& symbols_or_ids)const;
      vector<extended_asset_object>           get_assets_by_issuer(const std::string& issuer_name_or_id,
                                                                   asset_id_type start, uint32_t limit)const;

      // Markets / feeds
      vector<limit_order_object>         get_limit_orders( const std::string& a, const std::string& b,
                                                           uint32_t limit)const;
      vector<limit_order_object>         get_account_limit_orders( const string& account_name_or_id,
                                                                   const string &base,
                                                                   const string &quote, uint32_t limit,
                                                                   optional<limit_order_id_type> ostart_id,
                                                                   optional<price> ostart_price );
      vector<call_order_object>          get_call_orders(const std::string& a, uint32_t limit)const;
      vector<call_order_object>          get_call_orders_by_account(const std::string& account_name_or_id,
                                                                    asset_id_type start, uint32_t limit)const;
      vector<force_settlement_object>    get_settle_orders(const std::string& a, uint32_t limit)const;
      vector<force_settlement_object>    get_settle_orders_by_account(const std::string& account_name_or_id,
                                                                      force_settlement_id_type start,
                                                                      uint32_t limit)const;
      vector<call_order_object>          get_margin_positions( const std::string account_id_or_name )const;
      vector<collateral_bid_object>      get_collateral_bids( const std::string& asset,
                                                              uint32_t limit, uint32_t start)const;

      void subscribe_to_market( std::function<void(const variant&)> callback,
                                const std::string& a, const std::string& b );
      void unsubscribe_from_market(const std::string& a, const std::string& b);

      market_ticker                      get_ticker( const string& base, const string& quote,
                                                     bool skip_order_book = false )const;
      market_volume                      get_24_volume( const string& base, const string& quote )const;
      order_book                         get_order_book( const string& base, const string& quote,
                                                         unsigned limit = 50 )const;
      vector<market_ticker>              get_top_markets( uint32_t limit )const;
      vector<market_trade>               get_trade_history( const string& base, const string& quote,
                                                            fc::time_point_sec start, fc::time_point_sec stop,
                                                            unsigned limit = 100 )const;
      vector<market_trade>               get_trade_history_by_sequence( const string& base, const string& quote,
                                                                        int64_t start, fc::time_point_sec stop,
                                                                        unsigned limit = 100 )const;

      // Witnesses
      vector<optional<witness_object>> get_witnesses(const vector<witness_id_type>& witness_ids)const;
      fc::optional<witness_object> get_witness_by_account(const std::string account_id_or_name)const;
      map<string, witness_id_type> lookup_witness_accounts(const string& lower_bound_name, uint32_t limit)const;
      uint64_t get_witness_count()const;

      // Committee members
      vector<optional<committee_member_object>> get_committee_members(
            const vector<committee_member_id_type>& committee_member_ids )const;
      fc::optional<committee_member_object> get_committee_member_by_account(
            const std::string account_id_or_name )const;
      map<string, committee_member_id_type> lookup_committee_member_accounts(
            const string& lower_bound_name, uint32_t limit )const;
      uint64_t get_committee_count()const;

      // Workers
      vector<worker_object> get_all_workers()const;
      vector<optional<worker_object>> get_workers_by_account(const std::string account_id_or_name)const;
      uint64_t get_worker_count()const;

      // Votes
      vector<variant> lookup_vote_ids( const vector<vote_id_type>& votes )const;

      // Authority / validation
      std::string get_transaction_hex(const signed_transaction& trx)const;
      std::string get_transaction_hex_without_sig(const signed_transaction& trx)const;

      set<public_key_type> get_required_signatures( const signed_transaction& trx,
                                                    const flat_set<public_key_type>& available_keys )const;
      set<public_key_type> get_potential_signatures( const signed_transaction& trx )const;
      set<address> get_potential_address_signatures( const signed_transaction& trx )const;
      bool verify_authority( const signed_transaction& trx )const;
      bool verify_account_authority( const string& account_name_or_id,
                                     const flat_set<public_key_type>& signers )const;
      processed_transaction validate_transaction( const signed_transaction& trx )const;
      vector< fc::variant > get_required_fees( const vector<operation>& ops,
                                               const std::string& asset_id_or_symbol )const;

      // Proposed transactions
      vector<proposal_object> get_proposed_transactions( const std::string account_id_or_name )const;

      // Blinded balances
      vector<blinded_balance_object> get_blinded_balances( const flat_set<commitment_type>& commitments )const;

      // Withdrawals
      vector<withdraw_permission_object> get_withdraw_permissions_by_giver( const std::string account_id_or_name,
                                                                            withdraw_permission_id_type start,
                                                                            uint32_t limit )const;
      vector<withdraw_permission_object> get_withdraw_permissions_by_recipient( const std::string account_id_or_name,
                                                                                withdraw_permission_id_type start,
                                                                                uint32_t limit )const;

      // HTLC
      optional<htlc_object> get_htlc( htlc_id_type id, optional<bool> subscribe ) const;
      vector<htlc_object> get_htlc_by_from( const std::string account_id_or_name,
                                            htlc_id_type start, uint32_t limit ) const;
      vector<htlc_object> get_htlc_by_to( const std::string account_id_or_name,
                                          htlc_id_type start, uint32_t limit) const;
      vector<htlc_object> list_htlcs(const htlc_id_type lower_bound_id, uint32_t limit) const;

   //private:

      ////////////////////////////////////////////////
      // Accounts
      ////////////////////////////////////////////////

      const account_object* get_account_from_string( const std::string& name_or_id,
                                                     bool throw_if_not_found = true ) const;

      ////////////////////////////////////////////////
      // Assets
      ////////////////////////////////////////////////

      template<class ASSET>
      extended_asset_object extend_asset( ASSET&& a )const
      {
         asset_id_type id = a.id;
         extended_asset_object result = extended_asset_object( std::forward<ASSET>( a ) );
         if( amount_in_collateral_index )
         {
            result.total_in_collateral = amount_in_collateral_index->get_amount_in_collateral( id );
            if( result.bitasset_data_id.valid() )
               result.total_backing_collateral = amount_in_collateral_index->get_backing_collateral( id );
         }
         return result;
      }

      const asset_object* get_asset_from_string( const std::string& symbol_or_id,
                                                 bool throw_if_not_found = true ) const;
      // helper function
      vector<optional<extended_asset_object>> get_assets( const vector<asset_id_type>& asset_ids,
                                                          optional<bool> subscribe = optional<bool>() )const;

      ////////////////////////////////////////////////
      // Markets
      ////////////////////////////////////////////////

      // helper function
      vector<limit_order_object> get_limit_orders( const asset_id_type a, const asset_id_type b,
                                                   const uint32_t limit )const;

      ////////////////////////////////////////////////
      // Subscription
      ////////////////////////////////////////////////

      // Decides whether to subscribe using member variables and given parameter
      bool get_whether_to_subscribe( optional<bool> subscribe )const
      {
         if( !_subscribe_callback )
            return false;
         if( subscribe.valid() )
            return *subscribe;
         return _enabled_auto_subscription;
      }

      // Note:
      //   Different type of object_id<T> objects could become identical after packed.
      //   For example, both `account_id_type a=1.2.0` and `asset_id_type b=1.3.0` will become `0` after packed.
      //   In order to avoid collision, we don't use a template function here, instead, we implicitly convert all
      //   object IDs to `object_id_type` when subscribing.
      //
      //   If need to subscribe to other data types, override this function with the types as parameter.
      //   For example, we had a `get_subscription_key( const public_key_type& item )` function here, which was
      //   removed lately since we no longer subscribe to public keys.
      vector<char> get_subscription_key( const object_id_type& item )const
      {
         return fc::raw::pack(item);
      }

      template<typename T>
      void subscribe_to_item( const T& item )const
      {
         if( !_subscribe_callback )
            return;

         vector<char> key = get_subscription_key( item );
         if( !_subscribe_filter.contains( key.data(), key.size() ) )
         {
            _subscribe_filter.insert( key.data(), key.size() );
         }
      }

      template<typename T>
      bool is_subscribed_to_item( const T& item )const
      {
         if( !_subscribe_callback )
            return false;

         vector<char> key = get_subscription_key( item );
         return _subscribe_filter.contains( key.data(), key.size() );
      }

      // for full-account subscription
      bool is_impacted_account( const flat_set<account_id_type>& accounts );

      // for market subscription
      template<typename T>
      const std::pair<asset_id_type,asset_id_type> get_order_market( const T& order )
      {
         return order.get_market();
      }

      // for market subscription
      const std::pair<asset_id_type,asset_id_type> get_order_market( const force_settlement_object& order )
      {
         // TODO cache the result to avoid repeatly fetching from db
         asset_id_type backing_id = order.balance.asset_id( _db ).bitasset_data( _db ).options.short_backing_asset;
         auto tmp = std::make_pair( order.balance.asset_id, backing_id );
         if( tmp.first > tmp.second ) std::swap( tmp.first, tmp.second );
         return tmp;
      }

      template<typename T>
      void enqueue_if_subscribed_to_market(const object* obj, market_queue_type& queue, bool full_object=true)
      {
         const T* order = dynamic_cast<const T*>(obj);
         FC_ASSERT( order != nullptr);

         const auto& market = get_order_market( *order );

         auto sub = _market_subscriptions.find( market );
         if( sub != _market_subscriptions.end() ) {
            queue[market].emplace_back( full_object ? obj->to_variant() : fc::variant(obj->id, 1) );
         }
      }

      void broadcast_updates( const vector<variant>& updates );
      void broadcast_market_updates( const market_queue_type& queue);
      void handle_object_changed( bool force_notify,
                                  bool full_object,
                                  const vector<object_id_type>& ids,
                                  const flat_set<account_id_type>& impacted_accounts,
                                  std::function<const object*(object_id_type id)> find_object );

      /** called every time a block is applied to report the objects that were changed */
      void on_objects_new(const vector<object_id_type>& ids, const flat_set<account_id_type>& impacted_accounts);
      void on_objects_changed(const vector<object_id_type>& ids, const flat_set<account_id_type>& impacted_accounts);
      void on_objects_removed(const vector<object_id_type>& ids, const vector<const object*>& objs,
                              const flat_set<account_id_type>& impacted_accounts);
      void on_applied_block();

      ////////////////////////////////////////////////
      // Member variables
      ////////////////////////////////////////////////

      bool _notify_remove_create = false;
      bool _enabled_auto_subscription = true;

      mutable fc::bloom_filter  _subscribe_filter;
      std::set<account_id_type> _subscribed_accounts;

      std::function<void(const fc::variant&)> _subscribe_callback;
      std::function<void(const fc::variant&)> _pending_trx_callback;
      std::function<void(const fc::variant&)> _block_applied_callback;

      boost::signals2::scoped_connection _new_connection;
      boost::signals2::scoped_connection _change_connection;
      boost::signals2::scoped_connection _removed_connection;
      boost::signals2::scoped_connection _applied_block_connection;
      boost::signals2::scoped_connection _pending_trx_connection;

      map< pair<asset_id_type,asset_id_type>, std::function<void(const variant&)> > _market_subscriptions;

      graphene::chain::database& _db;
      const application_options* _app_options = nullptr;

      const graphene::api_helper_indexes::amount_in_collateral_index* amount_in_collateral_index;
};

} } // graphene::app
