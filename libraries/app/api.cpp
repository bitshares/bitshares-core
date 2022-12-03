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
#include <cctype>

#include <graphene/app/api.hpp>
#include <graphene/app/api_access.hpp>

#include "database_api_helper.hxx"

#include <fc/crypto/base64.hpp>
#include <fc/rpc/api_connection.hpp>
#include <fc/thread/future.hpp>

template class fc::api<graphene::app::block_api>;
template class fc::api<graphene::app::network_broadcast_api>;
template class fc::api<graphene::app::network_node_api>;
template class fc::api<graphene::app::history_api>;
template class fc::api<graphene::app::crypto_api>;
template class fc::api<graphene::app::asset_api>;
template class fc::api<graphene::app::orders_api>;
template class fc::api<graphene::app::custom_operations_api>;
template class fc::api<graphene::debug_witness::debug_api>;
template class fc::api<graphene::app::dummy_api>;
template class fc::api<graphene::app::login_api>;


namespace graphene { namespace app {

    login_api::login_api(application& a)
    :_app(a)
    {
       // Nothing to do
    }

    variant login_api::login(const optional<string>& o_user, const optional<string>& o_password)
    {
       if( !o_user && !o_password )
          return uint32_t(1); // Note: hard code it here for backward compatibility

       FC_ASSERT( o_user.valid() && o_password.valid(), "Must provide both user and password" );

       optional< api_access_info > acc = _app.get_api_access_info( *o_user );
       if( !acc )
          return logout();
       if( acc->password_hash_b64 != "*" )
       {
          std::string acc_password_hash = fc::base64_decode( acc->password_hash_b64 );
          if( fc::sha256::data_size() != acc_password_hash.length() )
             return logout();

          std::string password_salt = fc::base64_decode( acc->password_salt_b64 );
          fc::sha256 hash_obj = fc::sha256::hash( *o_password + password_salt );
          if( memcmp( hash_obj.data(), acc_password_hash.data(), fc::sha256::data_size() ) != 0 )
             return logout();
       }

       // Ideally, we should clean up the API sets that the previous user registered but the new user
       //   no longer has access to.
       // However, the shared pointers to these objects are already saved elsewhere (in FC),
       //   so we are unable to clean up, so it does not make sense to reset the optional fields here.

       _allowed_apis = acc->allowed_apis;
       return true;
    }

    bool login_api::logout()
    {
       // Ideally, we should clean up the API sets that the previous user registered.
       // However, the shared pointers to these objects are already saved elsewhere (in FC),
       //   so we are unable to clean up, so it does not make sense to reset the optional fields here.
       _allowed_apis.clear();
       return false;
    }

    string login_api::get_info() const
    {
       return _app.get_node_info();
    }

    application_options login_api::get_config() const
    {
       bool is_allowed = !_allowed_apis.empty();
       FC_ASSERT( is_allowed, "Access denied, please login" );
       return _app.get_options();
    }

    flat_set<string> login_api::get_available_api_sets() const
    {
       return _allowed_apis;
    }

    bool login_api::is_database_api_allowed() const
    {
       bool is_allowed = ( _allowed_apis.find("database_api") != _allowed_apis.end() );
       return is_allowed;
    }

    // block_api
    block_api::block_api(const graphene::chain::database& db) : _db(db) { /* Nothing to do */ }

    vector<optional<signed_block>> block_api::get_blocks(uint32_t block_num_from, uint32_t block_num_to)const
    {
       FC_ASSERT( block_num_to >= block_num_from );
       vector<optional<signed_block>> res;
       for(uint32_t block_num=block_num_from; block_num<=block_num_to; block_num++) {
          res.push_back(_db.fetch_block_by_number(block_num));
       }
       return res;
    }

    network_broadcast_api::network_broadcast_api(application& a):_app(a)
    {
       _applied_block_connection = _app.chain_database()->applied_block.connect(
                                         [this](const signed_block& b){ on_applied_block(b); });
    }

    void network_broadcast_api::on_applied_block( const signed_block& b )
    {
       if( _callbacks.size() )
       {
          /// we need to ensure the database_api is not deleted for the life of the async operation
          auto capture_this = shared_from_this();
          for( uint32_t trx_num = 0; trx_num < b.transactions.size(); ++trx_num )
          {
             const auto& trx = b.transactions[trx_num];
             auto id = trx.id();
             auto itr = _callbacks.find(id);
             if( itr != _callbacks.end() )
             {
                auto block_num = b.block_num();
                auto& callback = _callbacks.find(id)->second;
                auto v = fc::variant( transaction_confirmation{ id, block_num, trx_num, trx },
                                      GRAPHENE_MAX_NESTED_OBJECTS );
                fc::async( [capture_this,v,callback]() {
                   callback(v);
                } );
             }
          }
       }
    }

    void network_broadcast_api::broadcast_transaction(const precomputable_transaction& trx)
    {
       FC_ASSERT( _app.p2p_node() != nullptr, "Not connected to P2P network, can't broadcast!" );
       _app.chain_database()->precompute_parallel( trx ).wait();
       _app.chain_database()->push_transaction(trx);
       _app.p2p_node()->broadcast_transaction(trx);
    }

    fc::variant network_broadcast_api::broadcast_transaction_synchronous(const precomputable_transaction& trx)
    {
       fc::promise<fc::variant>::ptr prom = fc::promise<fc::variant>::create();
       broadcast_transaction_with_callback( [prom]( const fc::variant& v ){
        prom->set_value(v);
       }, trx );

       return fc::future<fc::variant>(prom).wait();
    }

    void network_broadcast_api::broadcast_block( const signed_block& b )
    {
       FC_ASSERT( _app.p2p_node() != nullptr, "Not connected to P2P network, can't broadcast!" );
       _app.chain_database()->precompute_parallel( b ).wait();
       _app.chain_database()->push_block(b);
       _app.p2p_node()->broadcast( net::block_message( b ));
    }

    void network_broadcast_api::broadcast_transaction_with_callback(confirmation_callback cb, const precomputable_transaction& trx)
    {
       FC_ASSERT( _app.p2p_node() != nullptr, "Not connected to P2P network, can't broadcast!" );
       _app.chain_database()->precompute_parallel( trx ).wait();
       _callbacks[trx.id()] = cb;
       _app.chain_database()->push_transaction(trx);
       _app.p2p_node()->broadcast_transaction(trx);
    }

    network_node_api::network_node_api( application& a ) : _app( a )
    {
       // Nothing to do
    }

    fc::variant_object network_node_api::get_info() const
    {
       FC_ASSERT( _app.p2p_node() != nullptr, "No P2P network!" );
       fc::mutable_variant_object result = _app.p2p_node()->network_get_info();
       result["connection_count"] = _app.p2p_node()->get_connection_count();
       return result;
    }

    void network_node_api::add_node(const fc::ip::endpoint& ep)
    {
       if( _app.p2p_node() != nullptr )
          _app.p2p_node()->add_node(ep);
    }

    std::vector<net::peer_status> network_node_api::get_connected_peers() const
    {
       if( _app.p2p_node() != nullptr )
          return _app.p2p_node()->get_connected_peers();
       return {};
    }

    std::vector<net::potential_peer_record> network_node_api::get_potential_peers() const
    {
       if( _app.p2p_node() != nullptr )
          return _app.p2p_node()->get_potential_peers();
       return {};
    }

    fc::variant_object network_node_api::get_advanced_node_parameters() const
    {
       FC_ASSERT( _app.p2p_node() != nullptr, "No P2P network!" );
       return _app.p2p_node()->get_advanced_node_parameters();
    }

    void network_node_api::set_advanced_node_parameters(const fc::variant_object& params)
    {
       FC_ASSERT( _app.p2p_node() != nullptr, "No P2P network!" );
       return _app.p2p_node()->set_advanced_node_parameters(params);
    }

    fc::api<network_broadcast_api> login_api::network_broadcast()
    {
       bool is_allowed = ( _allowed_apis.find("network_broadcast_api") != _allowed_apis.end() );
       FC_ASSERT( is_allowed, "Access denied" );
       if( !_network_broadcast_api )
       {
          _network_broadcast_api = std::make_shared< network_broadcast_api >( std::ref( _app ) );
       }
       return *_network_broadcast_api;
    }

    fc::api<block_api> login_api::block()
    {
       bool is_allowed = ( _allowed_apis.find("block_api") != _allowed_apis.end() );
       FC_ASSERT( is_allowed, "Access denied" );
       if( !_block_api )
       {
          _block_api = std::make_shared< block_api >( std::ref( *_app.chain_database() ) );
       }
       return *_block_api;
    }

    fc::api<network_node_api> login_api::network_node()
    {
       bool is_allowed = ( _allowed_apis.find("network_node_api") != _allowed_apis.end() );
       FC_ASSERT( is_allowed, "Access denied" );
       if( !_network_node_api )
       {
          _network_node_api = std::make_shared< network_node_api >( std::ref(_app) );
       }
       return *_network_node_api;
    }

    fc::api<database_api> login_api::database()
    {
       bool is_allowed = ( _allowed_apis.find("database_api") != _allowed_apis.end() );
       FC_ASSERT( is_allowed, "Access denied" );
       if( !_database_api )
       {
          _database_api = std::make_shared< database_api >( std::ref( *_app.chain_database() ),
                                                            &( _app.get_options() ) );
       }
       return *_database_api;
    }

    fc::api<history_api> login_api::history()
    {
       bool is_allowed = ( _allowed_apis.find("history_api") != _allowed_apis.end() );
       FC_ASSERT( is_allowed, "Access denied" );
       if( !_history_api )
       {
          _history_api = std::make_shared< history_api >( _app );
       }
       return *_history_api;
    }

    fc::api<crypto_api> login_api::crypto()
    {
       bool is_allowed = ( _allowed_apis.find("crypto_api") != _allowed_apis.end() );
       FC_ASSERT( is_allowed, "Access denied" );
       if( !_crypto_api )
       {
          _crypto_api = std::make_shared< crypto_api >();
       }
       return *_crypto_api;
    }

    fc::api<asset_api> login_api::asset()
    {
       bool is_allowed = ( _allowed_apis.find("asset_api") != _allowed_apis.end() );
       FC_ASSERT( is_allowed, "Access denied" );
       if( !_asset_api )
       {
          _asset_api = std::make_shared< asset_api >( _app );
       }
       return *_asset_api;
    }

    fc::api<orders_api> login_api::orders()
    {
       bool is_allowed = ( _allowed_apis.find("orders_api") != _allowed_apis.end() );
       FC_ASSERT( is_allowed, "Access denied" );
       if( !_orders_api )
       {
          _orders_api = std::make_shared< orders_api >( std::ref( _app ) );
       }
       return *_orders_api;
    }

    fc::api<graphene::debug_witness::debug_api> login_api::debug()
    {
       bool is_allowed = ( _allowed_apis.find("debug_api") != _allowed_apis.end() );
       FC_ASSERT( is_allowed, "Access denied" );
       // can only use this API set if the plugin was loaded
       bool plugin_enabled = !!_app.get_plugin( "debug_witness" );
       FC_ASSERT( plugin_enabled, "The debug_witness plugin is not enabled" );
       if( ! _debug_api )
       {
          _debug_api = std::make_shared< graphene::debug_witness::debug_api >( std::ref(_app) );
       }
       return *_debug_api;
    }

    fc::api<custom_operations_api> login_api::custom_operations()
    {
       bool is_allowed = ( _allowed_apis.find("custom_operations_api") != _allowed_apis.end() );
       FC_ASSERT( is_allowed, "Access denied" );
       // can only use this API set if the plugin was loaded
       bool plugin_enabled = !!_app.get_plugin( "custom_operations" );
       FC_ASSERT( plugin_enabled, "The custom_operations plugin is not enabled" );
       if( !_custom_operations_api )
       {
          _custom_operations_api = std::make_shared< custom_operations_api >( std::ref( _app ) );
       }
       return *_custom_operations_api;
    }

    fc::api<dummy_api> login_api::dummy()
    {
       if( !_dummy_api )
       {
          _dummy_api = std::make_shared< dummy_api >();
       }
       return *_dummy_api;
    }

    history_api::history_api(application& app)
    : _app(app)
    { // Nothing else to do
    }

    vector<order_history_object> history_api::get_fill_order_history( const std::string& asset_a,
                                                                      const std::string& asset_b,
                                                                      uint32_t limit )const
    {
       auto market_hist_plugin = _app.get_plugin<market_history_plugin>( "market_history" );
       FC_ASSERT( market_hist_plugin, "Market history plugin is not enabled" );
       FC_ASSERT(_app.chain_database());
       const auto& db = *_app.chain_database();
       database_api_helper db_api_helper( _app );
       asset_id_type a = db_api_helper.get_asset_from_string( asset_a )->get_id();
       asset_id_type b = db_api_helper.get_asset_from_string( asset_b )->get_id();
       if( a > b ) std::swap(a,b);
       const auto& history_idx = db.get_index_type<graphene::market_history::history_index>().indices().get<by_key>();
       history_key hkey;
       hkey.base = a;
       hkey.quote = b;
       hkey.sequence = std::numeric_limits<int64_t>::min();

       auto itr = history_idx.lower_bound( hkey );
       vector<order_history_object> result;
       while( itr != history_idx.end() && result.size() < limit )
       {
          if( itr->key.base != a || itr->key.quote != b ) break;
          result.push_back( *itr );
          ++itr;
       }

       return result;
    }

    vector<operation_history_object> history_api::get_account_history( const std::string& account_id_or_name,
                                                                       operation_history_id_type stop,
                                                                       uint32_t limit,
                                                                       operation_history_id_type start ) const
    {
       FC_ASSERT( _app.chain_database(), "database unavailable" );
       const auto& db = *_app.chain_database();

       const auto configured_limit = _app.get_options().api_limit_get_account_history;
       FC_ASSERT( limit <= configured_limit,
                  "limit can not be greater than ${configured_limit}",
                  ("configured_limit", configured_limit) );

       vector<operation_history_object> result;
       if( start == operation_history_id_type() )
          // Note: this means we can hardly use ID 0 as start to query for exactly the object with ID 0
          start = operation_history_id_type::max();
       if( start < stop )
          return result;

       account_id_type account;
       try {
          database_api_helper db_api_helper( _app );
          account = db_api_helper.get_account_from_string(account_id_or_name)->get_id();
       } catch(...) { return result; }

       if(_app.is_plugin_enabled("elasticsearch")) {
          auto es = _app.get_plugin<elasticsearch::elasticsearch_plugin>("elasticsearch");
          if(es.get()->get_running_mode() != elasticsearch::mode::only_save) {
             if(!_app.elasticsearch_thread)
                _app.elasticsearch_thread= std::make_shared<fc::thread>("elasticsearch");

             return _app.elasticsearch_thread->async([&es, account, stop, limit, start]() {
                return es->get_account_history(account, stop, limit, start);
             }, "thread invoke for method " BOOST_PP_STRINGIZE(method_name)).wait();
          }
       }

       const auto& by_op_idx = db.get_index_type<account_history_index>().indices().get<by_op>();
       auto itr = by_op_idx.lower_bound( boost::make_tuple( account, start ) );
       auto itr_end = by_op_idx.lower_bound( boost::make_tuple( account, stop ) );

       while( itr != itr_end && result.size() < limit )
       {
          result.emplace_back( itr->operation_id(db) );
          ++itr;
       }
       // Deal with a special case : include the object with ID 0 when it fits
       if( 0 == stop.instance.value && result.size() < limit && itr != by_op_idx.end() )
       {
          const auto& obj = *itr;
          if( obj.account == account )
             result.emplace_back( obj.operation_id(db) );
       }

       return result;
    }

    vector<operation_history_object> history_api::get_account_history_by_time(
            const std::string& account_name_or_id,
            const optional<uint32_t>& olimit,
            const optional<fc::time_point_sec>& ostart ) const
    {
       FC_ASSERT( _app.chain_database(), "database unavailable" );
       const auto& db = *_app.chain_database();

       const auto configured_limit = _app.get_options().api_limit_get_account_history;
       uint32_t limit = olimit.valid() ? *olimit : configured_limit;
       FC_ASSERT( limit <= configured_limit,
                  "limit can not be greater than ${configured_limit}",
                  ("configured_limit", configured_limit) );

       vector<operation_history_object> result;
       account_id_type account;
       try {
          database_api_helper db_api_helper( _app );
          account = db_api_helper.get_account_from_string(account_name_or_id)->get_id();
       } catch( const fc::exception& ) { return result; }

       fc::time_point_sec start = ostart.valid() ? *ostart : fc::time_point_sec::maximum();

       const auto& op_hist_idx = db.get_index_type<operation_history_index>().indices().get<by_time>();
       auto op_hist_itr = op_hist_idx.lower_bound( start );
       if( op_hist_itr == op_hist_idx.end() )
          return result;

       const auto& acc_hist_idx = db.get_index_type<account_history_index>().indices().get<by_op>();
       auto itr = acc_hist_idx.lower_bound( boost::make_tuple( account, op_hist_itr->get_id() ) );
       auto itr_end = acc_hist_idx.upper_bound( account );

       while( itr != itr_end && result.size() < limit )
       {
          result.emplace_back( itr->operation_id(db) );
          ++itr;
       }

       return result;
    }

    vector<operation_history_object> history_api::get_account_history_operations(
          const std::string& account_id_or_name,
          int64_t operation_type,
          operation_history_id_type start,
          operation_history_id_type stop,
          uint32_t limit ) const
    {
       FC_ASSERT( _app.chain_database(), "database unavailable" );
       const auto& db = *_app.chain_database();

       const auto configured_limit = _app.get_options().api_limit_get_account_history_operations;
       FC_ASSERT( limit <= configured_limit,
                  "limit can not be greater than ${configured_limit}",
                  ("configured_limit", configured_limit) );

       vector<operation_history_object> result;
       account_id_type account;
       try {
          database_api_helper db_api_helper( _app );
          account = db_api_helper.get_account_from_string(account_id_or_name)->get_id();
       } catch(...) { return result; }
       const auto& stats = account(db).statistics(db);
       if( stats.most_recent_op == account_history_id_type() ) return result;
       const account_history_object* node = &stats.most_recent_op(db);
       if( start == operation_history_id_type() )
          start = node->operation_id;

       while(node && node->operation_id.instance.value > stop.instance.value && result.size() < limit)
       {
          if( node->operation_id.instance.value <= start.instance.value ) {

             if(node->operation_id(db).op.which() == operation_type)
               result.push_back( node->operation_id(db) );
          }
          if( node->next == account_history_id_type() )
             node = nullptr;
          else node = &node->next(db);
       }
       if( stop.instance.value == 0 && result.size() < limit ) {
          const auto* head = db.find(account_history_id_type());
          if (head != nullptr && head->account == account && head->operation_id(db).op.which() == operation_type)
            result.push_back(head->operation_id(db));
       }
       return result;
    }


    vector<operation_history_object> history_api::get_relative_account_history( const std::string& account_id_or_name,
                                                                                uint64_t stop,
                                                                                uint32_t limit,
                                                                                uint64_t start ) const
    {
       FC_ASSERT( _app.chain_database(), "database unavailable" );
       const auto& db = *_app.chain_database();

       const auto configured_limit = _app.get_options().api_limit_get_relative_account_history;
       FC_ASSERT( limit <= configured_limit,
                  "limit can not be greater than ${configured_limit}",
                  ("configured_limit", configured_limit) );

       vector<operation_history_object> result;
       account_id_type account;
       try {
          database_api_helper db_api_helper( _app );
          account = db_api_helper.get_account_from_string(account_id_or_name)->get_id();
       } catch(...) { return result; }
       const auto& stats = account(db).statistics(db);
       if( start == 0 )
          start = stats.total_ops;
       else
          start = std::min( stats.total_ops, start );

       if( start >= stop && start > stats.removed_ops && limit > 0 )
       {
          const auto& hist_idx = db.get_index_type<account_history_index>();
          const auto& by_seq_idx = hist_idx.indices().get<by_seq>();

          auto itr = by_seq_idx.upper_bound( boost::make_tuple( account, start ) );
          auto itr_stop = by_seq_idx.lower_bound( boost::make_tuple( account, stop ) );

          do
          {
             --itr;
             result.push_back( itr->operation_id(db) );
          }
          while ( itr != itr_stop && result.size() < limit );
       }
       return result;
    }

    vector<operation_history_object> history_api::get_block_operation_history(
          uint32_t block_num,
          const optional<uint16_t>& trx_in_block ) const
    {
       FC_ASSERT( _app.chain_database(), "database unavailable" );
       const auto& db = *_app.chain_database();
       const auto& idx = db.get_index_type<operation_history_index>().indices().get<by_block>();
       auto range = trx_in_block.valid() ? idx.equal_range( boost::make_tuple( block_num, *trx_in_block  ) )
                                         : idx.equal_range( block_num );
       vector<operation_history_object> result;
       std::copy( range.first, range.second, std::back_inserter( result ) );
       return result;
    }

    vector<operation_history_object> history_api::get_block_operations_by_time(
          const optional<fc::time_point_sec>& start ) const
    {
       FC_ASSERT( _app.chain_database(), "database unavailable" );
       const auto& db = *_app.chain_database();
       const auto& idx = db.get_index_type<operation_history_index>().indices().get<by_time>();
       auto itr = start.valid() ? idx.lower_bound( *start ) : idx.begin();

       vector<operation_history_object> result;
       if( itr == idx.end() )
          return result;

       auto itr_end = idx.upper_bound( itr->block_time );

       std::copy( itr, itr_end, std::back_inserter( result ) );

       return result;
    }

    flat_set<uint32_t> history_api::get_market_history_buckets()const
    {
       auto market_hist_plugin = _app.get_plugin<market_history_plugin>( "market_history" );
       FC_ASSERT( market_hist_plugin, "Market history plugin is not enabled" );
       return market_hist_plugin->tracked_buckets();
    }

    history_api::history_operation_detail history_api::get_account_history_by_operations(
          const std::string& account_id_or_name,
          const flat_set<uint16_t>& operation_types,
          uint32_t start, uint32_t limit )const
    {
       const auto configured_limit = _app.get_options().api_limit_get_account_history_by_operations;
       FC_ASSERT( limit <= configured_limit,
                  "limit can not be greater than ${configured_limit}",
                  ("configured_limit", configured_limit) );

       history_operation_detail result;
       vector<operation_history_object> objs = get_relative_account_history( account_id_or_name, start, limit,
                                                                             limit + start - 1 );
       result.total_count = objs.size();

       if( operation_types.empty() )
          result.operation_history_objs = std::move(objs);
       else
       {
          for( const operation_history_object &o : objs )
          {
             if( operation_types.find(o.op.which()) != operation_types.end() ) {
                result.operation_history_objs.push_back(o);
             }
          }
       }

       return result;
    }

    vector<bucket_object> history_api::get_market_history( const std::string& asset_a, const std::string& asset_b,
                                                           uint32_t bucket_seconds,
                                                           const fc::time_point_sec& start,
                                                           const fc::time_point_sec& end )const
    { try {

       auto market_hist_plugin = _app.get_plugin<market_history_plugin>( "market_history" );
       FC_ASSERT( market_hist_plugin, "Market history plugin is not enabled" );
       FC_ASSERT(_app.chain_database());

       const auto& db = *_app.chain_database();
       database_api_helper db_api_helper( _app );
       asset_id_type a = db_api_helper.get_asset_from_string( asset_a )->get_id();
       asset_id_type b = db_api_helper.get_asset_from_string( asset_b )->get_id();
       vector<bucket_object> result;
       const auto configured_limit = _app.get_options().api_limit_get_market_history;
       result.reserve( configured_limit );

       if( a > b ) std::swap(a,b);

       const auto& bidx = db.get_index_type<bucket_index>();
       const auto& by_key_idx = bidx.indices().get<by_key>();

       auto itr = by_key_idx.lower_bound( bucket_key( a, b, bucket_seconds, start ) );
       while( itr != by_key_idx.end() && itr->key.open <= end && result.size() < configured_limit )
       {
          if( !(itr->key.base == a && itr->key.quote == b && itr->key.seconds == bucket_seconds) )
          {
            return result;
          }
          result.push_back(*itr);
          ++itr;
       }
       return result;
    } FC_CAPTURE_AND_RETHROW( (asset_a)(asset_b)(bucket_seconds)(start)(end) ) }

    static uint32_t validate_get_lp_history_params( const application& _app, const optional<uint32_t>& olimit )
    {
       FC_ASSERT( _app.get_options().has_market_history_plugin, "Market history plugin is not enabled." );

       const auto configured_limit = _app.get_options().api_limit_get_liquidity_pool_history;
       uint32_t limit = olimit.valid() ? *olimit : configured_limit;
       FC_ASSERT( limit <= configured_limit,
                  "limit can not be greater than ${configured_limit}",
                  ("configured_limit", configured_limit) );

       FC_ASSERT( _app.chain_database(), "Internal error: the chain database is not availalbe" );

       return limit;
    }

    vector<liquidity_pool_history_object> history_api::get_liquidity_pool_history(
               liquidity_pool_id_type pool_id,
               const optional<fc::time_point_sec>& start,
               const optional<fc::time_point_sec>& stop,
               const optional<uint32_t>& olimit,
               const optional<int64_t>& operation_type )const
    { try {
       uint32_t limit = validate_get_lp_history_params( _app, olimit );

       vector<liquidity_pool_history_object> result;

       if( 0 == limit || ( start.valid() && stop.valid() && *start <= *stop ) ) // empty result
          return result;

       const auto& db = *_app.chain_database();

       const auto& hist_idx = db.get_index_type<liquidity_pool_history_index>();

       if( operation_type.valid() ) // one operation type
       {
          const auto& idx = hist_idx.indices().get<by_pool_op_type_time>();
          auto itr = start.valid() ? idx.lower_bound( boost::make_tuple( pool_id, *operation_type, *start ) )
                                   : idx.lower_bound( boost::make_tuple( pool_id, *operation_type ) );
          auto itr_stop = stop.valid() ? idx.lower_bound( boost::make_tuple( pool_id, *operation_type, *stop ) )
                                       : idx.upper_bound( boost::make_tuple( pool_id, *operation_type ) );
          while( itr != itr_stop && result.size() < limit )
          {
             result.push_back( *itr );
             ++itr;
          }
       }
       else // all operation types
       {
          const auto& idx = hist_idx.indices().get<by_pool_time>();
          auto itr = start.valid() ? idx.lower_bound( boost::make_tuple( pool_id, *start ) )
                                   : idx.lower_bound( pool_id );
          auto itr_stop = stop.valid() ? idx.lower_bound( boost::make_tuple( pool_id, *stop ) )
                                       : idx.upper_bound( pool_id );
          while( itr != itr_stop && result.size() < limit )
          {
             result.push_back( *itr );
             ++itr;
          }
       }

       return result;

    } FC_CAPTURE_AND_RETHROW( (pool_id)(start)(stop)(olimit)(operation_type) ) }

    vector<liquidity_pool_history_object> history_api::get_liquidity_pool_history_by_sequence(
               liquidity_pool_id_type pool_id,
               const optional<uint64_t>& start,
               const optional<fc::time_point_sec>& stop,
               const optional<uint32_t>& olimit,
               const optional<int64_t>& operation_type )const
    { try {
       uint32_t limit = validate_get_lp_history_params( _app, olimit );

       vector<liquidity_pool_history_object> result;

       if( 0 == limit ) // empty result
          return result;

       const auto& db = *_app.chain_database();

       const auto& hist_idx = db.get_index_type<liquidity_pool_history_index>();

       if( operation_type.valid() ) // one operation type
       {
          const auto& idx = hist_idx.indices().get<by_pool_op_type_seq>();
          const auto& idx_t = hist_idx.indices().get<by_pool_op_type_time>();
          auto itr = start.valid() ? idx.lower_bound( boost::make_tuple( pool_id, *operation_type, *start ) )
                                   : idx.lower_bound( boost::make_tuple( pool_id, *operation_type ) );
          if( itr == idx.end() || itr->pool != pool_id || itr->op_type != *operation_type ) // empty result
             return result;
          if( stop.valid() && itr->time <= *stop ) // empty result
             return result;
          auto itr_temp = stop.valid() ? idx_t.lower_bound( boost::make_tuple( pool_id, *operation_type, *stop ) )
                                       : idx_t.upper_bound( boost::make_tuple( pool_id, *operation_type ) );
          auto itr_stop = ( itr_temp == idx_t.end() ? idx.end() : idx.iterator_to( *itr_temp ) );
          while( itr != itr_stop && result.size() < limit )
          {
             result.push_back( *itr );
             ++itr;
          }
       }
       else // all operation types
       {
          const auto& idx = hist_idx.indices().get<by_pool_seq>();
          const auto& idx_t = hist_idx.indices().get<by_pool_time>();
          auto itr = start.valid() ? idx.lower_bound( boost::make_tuple( pool_id, *start ) )
                                   : idx.lower_bound( pool_id );
          if( itr == idx.end() || itr->pool != pool_id ) // empty result
             return result;
          if( stop.valid() && itr->time <= *stop ) // empty result
             return result;
          auto itr_temp = stop.valid() ? idx_t.lower_bound( boost::make_tuple( pool_id, *stop ) )
                                       : idx_t.upper_bound( pool_id );
          auto itr_stop = ( itr_temp == idx_t.end() ? idx.end() : idx.iterator_to( *itr_temp ) );
          while( itr != itr_stop && result.size() < limit )
          {
             result.push_back( *itr );
             ++itr;
          }
       }

       return result;

    } FC_CAPTURE_AND_RETHROW( (pool_id)(start)(stop)(olimit)(operation_type) ) }


    fc::ecc::commitment_type crypto_api::blind( const blind_factor_type& blind, uint64_t value ) const
    {
       return fc::ecc::blind( blind, value );
    }

    fc::ecc::blind_factor_type crypto_api::blind_sum( const std::vector<blind_factor_type>& blinds_in,
                                                      uint32_t non_neg ) const
    {
       return fc::ecc::blind_sum( blinds_in, non_neg );
    }

    bool crypto_api::verify_sum( const std::vector<commitment_type>& commits_in,
                                 const std::vector<commitment_type>& neg_commits_in,
                                 int64_t excess ) const
    {
       return fc::ecc::verify_sum( commits_in, neg_commits_in, excess );
    }

    crypto_api::verify_range_result crypto_api::verify_range( const commitment_type& commit,
                                                              const std::vector<char>& proof ) const
    {
       verify_range_result result;
       result.success = fc::ecc::verify_range( result.min_val, result.max_val, commit, proof );
       return result;
    }

    std::vector<char> crypto_api::range_proof_sign( uint64_t min_value,
                                                    const commitment_type& commit,
                                                    const blind_factor_type& commit_blind,
                                                    const blind_factor_type& nonce,
                                                    int8_t base10_exp,
                                                    uint8_t min_bits,
                                                    uint64_t actual_value ) const
    {
       return fc::ecc::range_proof_sign( min_value, commit, commit_blind, nonce, base10_exp, min_bits, actual_value );
    }

    crypto_api::verify_range_proof_rewind_result crypto_api::verify_range_proof_rewind(
          const blind_factor_type& nonce,
          const commitment_type& commit,
          const std::vector<char>& proof ) const
    {
       verify_range_proof_rewind_result result;
       result.success = fc::ecc::verify_range_proof_rewind( result.blind_out,
                                                            result.value_out,
                                                            result.message_out,
                                                            nonce,
                                                            result.min_val,
                                                            result.max_val,
                                                            const_cast< commitment_type& >( commit ),
                                                            proof );
       return result;
    }

    fc::ecc::range_proof_info crypto_api::range_get_info( const std::vector<char>& proof ) const
    {
       return fc::ecc::range_get_info( proof );
    }

    // asset_api
    asset_api::asset_api(graphene::app::application& app)
    : _app(app),
      _db( *app.chain_database() )
    { // Nothing else to do
    }

    vector<asset_api::account_asset_balance> asset_api::get_asset_holders( const std::string& asset_symbol_or_id,
                                                                           uint32_t start, uint32_t limit ) const
    {
       const auto configured_limit = _app.get_options().api_limit_get_asset_holders;
       FC_ASSERT( limit <= configured_limit,
                  "limit can not be greater than ${configured_limit}",
                  ("configured_limit", configured_limit) );

       database_api_helper db_api_helper( _app );
       asset_id_type asset_id = db_api_helper.get_asset_from_string( asset_symbol_or_id )->get_id();
       const auto& bal_idx = _db.get_index_type< account_balance_index >().indices().get< by_asset_balance >();
       auto range = bal_idx.equal_range( boost::make_tuple( asset_id ) );

       vector<account_asset_balance> result;

       uint32_t index = 0;
       for( const account_balance_object& bal : boost::make_iterator_range( range.first, range.second ) )
       {
          if( result.size() >= limit )
             break;

          if( bal.balance.value == 0 )
             continue;

          if( index++ < start )
             continue;

          const auto account = _db.find(bal.owner);

          account_asset_balance aab;
          aab.name       = account->name;
          aab.account_id = account->id;
          aab.amount     = bal.balance.value;

          result.push_back(aab);
       }

       return result;
    }
    // get number of asset holders.
    int64_t asset_api::get_asset_holders_count( const std::string& asset_symbol_or_id ) const {
       const auto& bal_idx = _db.get_index_type< account_balance_index >().indices().get< by_asset_balance >();
       database_api_helper db_api_helper( _app );
       asset_id_type asset_id = db_api_helper.get_asset_from_string( asset_symbol_or_id )->get_id();
       auto range = bal_idx.equal_range( boost::make_tuple( asset_id ) );

       int64_t count = boost::distance(range) - 1;

       return count;
    }
    // function to get vector of system assets with holders count.
    vector<asset_api::asset_holders> asset_api::get_all_asset_holders() const {
       vector<asset_holders> result;
       vector<asset_id_type> total_assets;
       for( const asset_object& asset_obj : _db.get_index_type<asset_index>().indices() )
       {
          const auto& dasset_obj = asset_obj.dynamic_asset_data_id(_db);

          asset_id_type asset_id;
          asset_id = dasset_obj.id;

          const auto& bal_idx = _db.get_index_type< account_balance_index >().indices().get< by_asset_balance >();
          auto range = bal_idx.equal_range( boost::make_tuple( asset_id ) );

          int64_t count = boost::distance(range) - 1;

          asset_holders ah;
          ah.asset_id       = asset_id;
          ah.count     = count;

          result.push_back(ah);
       }

       return result;
    }

   // orders_api
   orders_api::orders_api(application& app)
   : _app(app)
   { // Nothing else to do
   }

   flat_set<uint16_t> orders_api::get_tracked_groups()const
   {
      auto plugin = _app.get_plugin<grouped_orders_plugin>( "grouped_orders" );
      FC_ASSERT( plugin );
      return plugin->tracked_groups();
   }

   vector< orders_api::limit_order_group > orders_api::get_grouped_limit_orders( const std::string& base_asset,
                                                                                 const std::string& quote_asset,
                                                                                 uint16_t group,
                                                                                 const optional<price>& start,
                                                                                 uint32_t limit )const
   {
      const auto configured_limit = _app.get_options().api_limit_get_grouped_limit_orders;
      FC_ASSERT( limit <= configured_limit,
                 "limit can not be greater than ${configured_limit}",
                 ("configured_limit", configured_limit) );

      auto plugin = _app.get_plugin<graphene::grouped_orders::grouped_orders_plugin>( "grouped_orders" );
      FC_ASSERT( plugin );
      const auto& limit_groups = plugin->limit_order_groups();
      vector< limit_order_group > result;

      database_api_helper db_api_helper( _app );
      asset_id_type base_asset_id = db_api_helper.get_asset_from_string( base_asset )->get_id();
      asset_id_type quote_asset_id = db_api_helper.get_asset_from_string( quote_asset )->get_id();

      price max_price = price::max( base_asset_id, quote_asset_id );
      price min_price = price::min( base_asset_id, quote_asset_id );
      if( start.valid() && !start->is_null() )
         max_price = std::max( std::min( max_price, *start ), min_price );

      auto itr = limit_groups.lower_bound( limit_order_group_key( group, max_price ) );
      // use an end iterator to try to avoid expensive price comparison
      auto end = limit_groups.upper_bound( limit_order_group_key( group, min_price ) );
      while( itr != end && result.size() < limit )
      {
         result.emplace_back( *itr );
         ++itr;
      }
      return result;
   }

   // custom operations api
   custom_operations_api::custom_operations_api(application& app)
   : _app(app)
   { // Nothing else to do
   }

   vector<account_storage_object> custom_operations_api::get_storage_info(
         const optional<std::string>& o_account_name_or_id,
         const optional<std::string>& catalog,
         const optional<std::string>& key,
         const optional<uint32_t>& limit,
         const optional<account_storage_id_type>& start_id )const
   {
      auto plugin = _app.get_plugin<graphene::custom_operations::custom_operations_plugin>("custom_operations");
      FC_ASSERT( plugin, "The custom_operations plugin is not enabled" );

      database_api_helper db_api_helper( _app );
      const auto& storage_index = _app.chain_database()->get_index_type<account_storage_index>().indices();

      if( o_account_name_or_id.valid() )
      {
         const string& account_name_or_id = *o_account_name_or_id;
         const account_id_type account_id = db_api_helper.get_account_from_string(account_name_or_id)->get_id();
         if( catalog.valid() )
         {
            if( key.valid() )
               return db_api_helper.get_objects_by_x< account_storage_object,
                                                      account_storage_id_type
                                                     >( &application_options::api_limit_get_storage_info,
                                                        storage_index.get<by_account_catalog_key>(),
                                                        limit, start_id, account_id, *catalog, *key );
            else
               return db_api_helper.get_objects_by_x< account_storage_object,
                                                      account_storage_id_type
                                                     >( &application_options::api_limit_get_storage_info,
                                                        storage_index.get<by_account_catalog>(),
                                                        limit, start_id, account_id, *catalog );
         }
         else
         {
            FC_ASSERT( !key.valid(), "Can not specify key if catalog is not specified" );
            return db_api_helper.get_objects_by_x< account_storage_object,
                                                   account_storage_id_type
                                                  >( &application_options::api_limit_get_storage_info,
                                                     storage_index.get<custom_operations::by_account>(),
                                                     limit, start_id, account_id );
         }
      }
      else if( catalog.valid() )
      {
         if( key.valid() )
            return db_api_helper.get_objects_by_x< account_storage_object,
                                                   account_storage_id_type
                                                  >( &application_options::api_limit_get_storage_info,
                                                     storage_index.get<by_catalog_key>(),
                                                     limit, start_id, *catalog, *key );
         else
            return db_api_helper.get_objects_by_x< account_storage_object,
                                                   account_storage_id_type
                                                  >( &application_options::api_limit_get_storage_info,
                                                     storage_index.get<by_catalog>(),
                                                     limit, start_id, *catalog );
      }
      else
      {
         FC_ASSERT( !key.valid(), "Can not specify key if catalog is not specified" );
         return db_api_helper.get_objects_by_x< account_storage_object,
                                                account_storage_id_type
                                               >( &application_options::api_limit_get_storage_info,
                                                  storage_index.get<by_id>(),
                                                  limit, start_id );
      }

   }

} } // graphene::app
