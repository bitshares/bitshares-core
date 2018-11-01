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

#include <graphene/wallet/api_documentation.hpp>
#include <graphene/wallet/reflect_util.hpp>

namespace graphene { namespace wallet {

namespace detail {

class wallet_api_impl
{
public:
   api_documentation method_documentation;
private:
   void claim_registered_account(const account_object& account);

   // after a witness registration succeeds, this saves the private key in the wallet permanently
   void claim_registered_witness(const std::string& witness_name);

   fc::mutex _resync_mutex;
   void resync();

   void enable_umask_protection();

   void disable_umask_protection();

   void init_prototype_ops();

   map<transaction_handle_type, signed_transaction> _builder_transactions;

   // if the user executes the same command twice in quick succession,
   // we might generate the same transaction id, and cause the second
   // transaction to be rejected.  This can be avoided by altering the
   // second transaction slightly (bumping up the expiration time by
   // a second).  Keep track of recent transaction ids we've generated
   // so we can know if we need to do this
   struct recently_generated_transaction_record
   {
      fc::time_point_sec generation_time;
      graphene::chain::transaction_id_type transaction_id;
   };
   struct timestamp_index{};
   typedef boost::multi_index_container<recently_generated_transaction_record,
                                        boost::multi_index::indexed_by<boost::multi_index::hashed_unique<boost::multi_index::member<recently_generated_transaction_record,
                                                                                                                                    graphene::chain::transaction_id_type,
                                                                                                                                    &recently_generated_transaction_record::transaction_id>,
                                                                                                         std::hash<graphene::chain::transaction_id_type> >,
                                                                       boost::multi_index::ordered_non_unique<boost::multi_index::tag<timestamp_index>,
                                                                                                              boost::multi_index::member<recently_generated_transaction_record, fc::time_point_sec, &recently_generated_transaction_record::generation_time> > > > recently_generated_transaction_set_type;
   recently_generated_transaction_set_type _recently_generated_transactions;

public:
   wallet_api& self;
   wallet_api_impl( wallet_api& s, const wallet_data& initial_data, fc::api<login_api> rapi );
   virtual ~wallet_api_impl();

   void encrypt_keys();

   void on_block_applied( const variant& block_id );

   bool copy_wallet_file( string destination_filename );

   bool is_locked()const;

   template<typename T>
   T get_object(object_id<T::space_id, T::type_id, T> id)const;

   void set_operation_fees( signed_transaction& tx, const fee_schedule& s  );

   variant info() const;

   variant_object about() const;

   chain_property_object get_chain_properties() const;
   global_property_object get_global_properties() const;
   dynamic_global_property_object get_dynamic_global_properties() const;
   std::string account_id_to_string(account_id_type id) const;
   account_object get_account(account_id_type id) const;
   account_object get_account(string account_name_or_id) const;
   account_id_type get_account_id(string account_name_or_id) const;
   optional<asset_object> find_asset(asset_id_type id)const;
   optional<asset_object> find_asset(string asset_symbol_or_id)const;
   asset_object get_asset(asset_id_type id)const;
   asset_object get_asset(string asset_symbol_or_id)const;

   asset_id_type get_asset_id(string asset_symbol_or_id) const;

   string get_wallet_filename() const;

   fc::ecc::private_key get_private_key(const public_key_type& id)const;

   fc::ecc::private_key get_private_key_for_account(const account_object& account)const;

   // imports the private key into the wallet, and associate it in some way (?) with the
   // given account name.
   // @returns true if the key matches a current active/owner/memo key for the named
   //          account, false otherwise (but it is stored either way)
   bool import_key(string account_name_or_id, string wif_key);

   vector< signed_transaction > import_balance( string name_or_id, const vector<string>& wif_keys, bool broadcast );

   bool load_wallet_file(string wallet_filename = "");

   void quit();

   void save_wallet_file(string wallet_filename = "");

   transaction_handle_type begin_builder_transaction();
   void add_operation_to_builder_transaction(transaction_handle_type transaction_handle, const operation& op);
   void replace_operation_in_builder_transaction(transaction_handle_type handle,
                                                 uint32_t operation_index,
                                                 const operation& new_op);
   asset set_fees_on_builder_transaction(transaction_handle_type handle, string fee_asset = GRAPHENE_SYMBOL);
   transaction preview_builder_transaction(transaction_handle_type handle);
   signed_transaction sign_builder_transaction(transaction_handle_type transaction_handle, bool broadcast = true);

   pair<transaction_id_type,signed_transaction> broadcast_transaction(signed_transaction tx);

   signed_transaction propose_builder_transaction(
      transaction_handle_type handle,
      time_point_sec expiration = time_point::now() + fc::minutes(1),
      uint32_t review_period_seconds = 0, bool broadcast = true);

   signed_transaction propose_builder_transaction2(
      transaction_handle_type handle,
      string account_name_or_id,
      time_point_sec expiration = time_point::now() + fc::minutes(1),
      uint32_t review_period_seconds = 0, bool broadcast = true);

   void remove_builder_transaction(transaction_handle_type handle);

   signed_transaction register_account(string name,
                                       public_key_type owner,
                                       public_key_type active,
                                       string  registrar_account,
                                       string  referrer_account,
                                       uint32_t referrer_percent,
                                       bool broadcast = false);

   signed_transaction upgrade_account(string name, bool broadcast);

   // This function generates derived keys starting with index 0 and keeps incrementing
   // the index until it finds a key that isn't registered in the block chain.  To be
   // safer, it continues checking for a few more keys to make sure there wasn't a short gap
   // caused by a failed registration or the like.
   int find_first_unused_derived_key_index(const fc::ecc::private_key& parent_key);

   signed_transaction create_account_with_private_key(fc::ecc::private_key owner_privkey,
                                                      string account_name,
                                                      string registrar_account,
                                                      string referrer_account,
                                                      bool broadcast = false,
                                                      bool save_wallet = true);

   signed_transaction create_account_with_brain_key(string brain_key,
                                                    string account_name,
                                                    string registrar_account,
                                                    string referrer_account,
                                                    bool broadcast = false,
                                                    bool save_wallet = true);

   signed_transaction create_asset(string issuer,
                                   string symbol,
                                   uint8_t precision,
                                   asset_options common,
                                   fc::optional<bitasset_options> bitasset_opts,
                                   bool broadcast = false);

   signed_transaction update_asset(string symbol,
                                   optional<string> new_issuer,
                                   asset_options new_options,
                                   bool broadcast /* = false */);

   signed_transaction update_asset_issuer(string symbol,
                                          string new_issuer,
                                          bool broadcast /* = false */);

   signed_transaction update_bitasset(string symbol,
                                      bitasset_options new_options,
                                      bool broadcast /* = false */);

   signed_transaction update_asset_feed_producers(string symbol,
                                                  flat_set<string> new_feed_producers,
                                                  bool broadcast /* = false */);

   signed_transaction publish_asset_feed(string publishing_account,
                                         string symbol,
                                         price_feed feed,
                                         bool broadcast /* = false */);

   signed_transaction fund_asset_fee_pool(string from,
                                          string symbol,
                                          string amount,
                                          bool broadcast /* = false */);

   signed_transaction claim_asset_fee_pool(string symbol,
                                           string amount,
                                           bool broadcast /* = false */);

   signed_transaction reserve_asset(string from,
                                    string amount,
                                    string symbol,
                                    bool broadcast /* = false */);

   signed_transaction global_settle_asset(string symbol,
                                          price settle_price,
                                          bool broadcast /* = false */);

   signed_transaction settle_asset(string account_to_settle,
                                   string amount_to_settle,
                                   string symbol,
                                   bool broadcast /* = false */);

   signed_transaction bid_collateral(string bidder_name,
                                     string debt_amount, string debt_symbol,
                                     string additional_collateral,
                                     bool broadcast );

   signed_transaction whitelist_account(string authorizing_account,
                                        string account_to_list,
                                        account_whitelist_operation::account_listing new_listing_status,
                                        bool broadcast /* = false */);

   signed_transaction create_committee_member(string owner_account, string url,
                                              bool broadcast /* = false */);

   witness_object get_witness(string owner_account);

   committee_member_object get_committee_member(string owner_account);

   signed_transaction create_witness(string owner_account,
                                     string url,
                                     bool broadcast /* = false */);

   signed_transaction update_witness(string witness_name,
                                     string url,
                                     string block_signing_key,
                                     bool broadcast /* = false */);

   template<typename WorkerInit>
   static WorkerInit _create_worker_initializer( const variant& worker_settings )
   {
      WorkerInit result;
      from_variant( worker_settings, result, GRAPHENE_MAX_NESTED_OBJECTS );
      return result;
   }

   signed_transaction create_worker(
      string owner_account,
      time_point_sec work_begin_date,
      time_point_sec work_end_date,
      share_type daily_pay,
      string name,
      string url,
      variant worker_settings,
      bool broadcast
      );

   signed_transaction update_worker_votes(
      string account,
      worker_vote_delta delta,
      bool broadcast
      );

   vector< vesting_balance_object_with_info > get_vesting_balances( string account_name );

   signed_transaction withdraw_vesting(
      string witness_name,
      string amount,
      string asset_symbol,
      bool broadcast = false );

   signed_transaction vote_for_committee_member(string voting_account,
                                                string committee_member,
                                                bool approve,
                                                bool broadcast /* = false */);

   signed_transaction vote_for_witness(string voting_account,
                                       string witness,
                                       bool approve,
                                       bool broadcast /* = false */);

   signed_transaction set_voting_proxy(string account_to_modify,
                                       optional<string> voting_account,
                                       bool broadcast /* = false */);

   signed_transaction set_desired_witness_and_committee_member_count(string account_to_modify,
                                                                     uint16_t desired_number_of_witnesses,
                                                                     uint16_t desired_number_of_committee_members,
                                                                     bool broadcast /* = false */);

   signed_transaction sign_transaction(signed_transaction tx, bool broadcast = false);

   memo_data sign_memo(string from, string to, string memo);

   string read_memo(const memo_data& md);

   signed_transaction sell_asset(string seller_account,
                                 string amount_to_sell,
                                 string symbol_to_sell,
                                 string min_to_receive,
                                 string symbol_to_receive,
                                 uint32_t timeout_sec = 0,
                                 bool   fill_or_kill = false,
                                 bool   broadcast = false);

   signed_transaction borrow_asset(string seller_name, string amount_to_borrow, string asset_symbol,
                                   string amount_of_collateral, bool broadcast = false);

   signed_transaction borrow_asset_ext( string seller_name, string amount_to_borrow, string asset_symbol,
                                        string amount_of_collateral,
                                        call_order_update_operation::extensions_type extensions,
                                        bool broadcast = false);

   signed_transaction cancel_order(object_id_type order_id, bool broadcast = false);

   signed_transaction transfer(string from, string to, string amount,
                               string asset_symbol, string memo, bool broadcast = false);

   signed_transaction issue_asset(string to_account, string amount, string symbol,
                                  string memo, bool broadcast = false);

   std::map<string,std::function<string(fc::variant,const fc::variants&)>> get_result_formatters() const;

   signed_transaction propose_parameter_change(
      const string& proposing_account,
      fc::time_point_sec expiration_time,
      const variant_object& changed_values,
      bool broadcast = false);

   signed_transaction propose_fee_change(
      const string& proposing_account,
      fc::time_point_sec expiration_time,
      const variant_object& changed_fees,
      bool broadcast = false);

   signed_transaction approve_proposal(
      const string& fee_paying_account,
      const string& proposal_id,
      const approval_delta& delta,
      bool broadcast = false);

   void dbg_make_uia(string creator, string symbol);

   void dbg_make_mia(string creator, string symbol);

   void dbg_push_blocks( const std::string& src_filename, uint32_t count );

   void dbg_generate_blocks( const std::string& debug_wif_key, uint32_t count );

   void dbg_stream_json_objects( const std::string& filename );

   void dbg_update_object( const fc::variant_object& update );

   void use_network_node_api();

   void use_debug_api();

   void network_add_nodes( const vector<string>& nodes );

   vector< variant > network_get_connected_peers();

   void flood_network(string prefix, uint32_t number_of_transactions);

   operation get_prototype_operation( string operation_name );

   string                  _wallet_filename;
   wallet_data             _wallet;

   map<public_key_type,string> _keys;
   fc::sha512                  _checksum;

   chain_id_type           _chain_id;
   fc::api<login_api>      _remote_api;
   fc::api<database_api>   _remote_db;
   fc::api<network_broadcast_api>   _remote_net_broadcast;
   fc::api<history_api>    _remote_hist;
   optional< fc::api<network_node_api> > _remote_net_node;
   optional< fc::api<graphene::debug_witness::debug_api> > _remote_debug;

   flat_map<string, operation> _prototype_ops;

   static_variant_map _operation_which_map = create_static_variant_map< operation >();

#ifdef __unix__
   mode_t                  _old_umask;
#endif
   const string _wallet_filename_extension = ".wallet";
};

}}} // graphene::wallet::detail
