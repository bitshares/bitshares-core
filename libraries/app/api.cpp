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
#include <graphene/app/application.hpp>
#include <graphene/app/impacted.hpp>
#include <graphene/chain/database.hpp>
#include <graphene/chain/get_config.hpp>
#include <graphene/utilities/key_conversion.hpp>
#include <graphene/chain/protocol/fee_schedule.hpp>
#include <graphene/chain/confidential_object.hpp>
#include <graphene/chain/market_object.hpp>
#include <graphene/chain/transaction_object.hpp>
#include <graphene/chain/withdraw_permission_object.hpp>
#include <graphene/chain/worker_object.hpp>

#include <fc/crypto/hex.hpp>
#include <fc/smart_ref_impl.hpp>

namespace graphene { namespace app {

    login_api::login_api(application& a)
    :_app(a)
    {
    }

    login_api::~login_api()
    {
    }

    bool login_api::login(const string& user, const string& password)
    {
       optional< api_access_info > acc = _app.get_api_access_info( user );
       if( !acc.valid() )
          return false;
       if( acc->password_hash_b64 != "*" )
       {
          std::string password_salt = fc::base64_decode( acc->password_salt_b64 );
          std::string acc_password_hash = fc::base64_decode( acc->password_hash_b64 );

          fc::sha256 hash_obj = fc::sha256::hash( password + password_salt );
          if( hash_obj.data_size() != acc_password_hash.length() )
             return false;
          if( memcmp( hash_obj.data(), acc_password_hash.c_str(), hash_obj.data_size() ) != 0 )
             return false;
       }

       for( const std::string& api_name : acc->allowed_apis )
          enable_api( api_name );
       return true;
    }

    void login_api::enable_api( const std::string& api_name )
    {
       if( api_name == "database_api" )
       {
          _database_api = std::make_shared< database_api >( std::ref( *_app.chain_database() ) );
       }
       else if( api_name == "block_api" )
       {
          _block_api = std::make_shared< block_api >( std::ref( *_app.chain_database() ) );
       }
       else if( api_name == "network_broadcast_api" )
       {
          _network_broadcast_api = std::make_shared< network_broadcast_api >( std::ref( _app ) );
       }
       else if( api_name == "history_api" )
       {
          _history_api = std::make_shared< history_api >( _app );
       }
       else if( api_name == "network_node_api" )
       {
          _network_node_api = std::make_shared< network_node_api >( std::ref(_app) );
       }
       else if( api_name == "crypto_api" )
       {
          _crypto_api = std::make_shared< crypto_api >();
       }
       else if( api_name == "asset_api" )
       {
          _asset_api = std::make_shared< asset_api >( std::ref( *_app.chain_database() ) );
       }
       else if( api_name == "debug_api" )
       {
          // can only enable this API if the plugin was loaded
          if( _app.get_plugin( "debug_witness" ) )
             _debug_api = std::make_shared< graphene::debug_witness::debug_api >( std::ref(_app) );
       }
       return;
    }

    // block_api
    block_api::block_api(graphene::chain::database& db) : _db(db) { }
    block_api::~block_api() { }

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
       _applied_block_connection = _app.chain_database()->applied_block.connect([this](const signed_block& b){ on_applied_block(b); });
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
                fc::async( [capture_this,this,id,block_num,trx_num,trx,callback](){ callback( fc::variant(transaction_confirmation{ id, block_num, trx_num, trx}) ); } );
             }
          }
       }
    }

    void network_broadcast_api::broadcast_transaction(const signed_transaction& trx)
    {
       trx.validate();
       _app.chain_database()->push_transaction(trx);
       _app.p2p_node()->broadcast_transaction(trx);
    }

    void network_broadcast_api::broadcast_block( const signed_block& b )
    {
       _app.chain_database()->push_block(b);
       _app.p2p_node()->broadcast( net::block_message( b ));
    }

    void network_broadcast_api::broadcast_transaction_with_callback(confirmation_callback cb, const signed_transaction& trx)
    {
       trx.validate();
       _callbacks[trx.id()] = cb;
       _app.chain_database()->push_transaction(trx);
       _app.p2p_node()->broadcast_transaction(trx);
    }

    network_node_api::network_node_api( application& a ) : _app( a )
    {
    }

    fc::variant_object network_node_api::get_info() const
    {
       fc::mutable_variant_object result = _app.p2p_node()->network_get_info();
       result["connection_count"] = _app.p2p_node()->get_connection_count();
       return result;
    }

    void network_node_api::add_node(const fc::ip::endpoint& ep)
    {
       _app.p2p_node()->add_node(ep);
    }

    std::vector<net::peer_status> network_node_api::get_connected_peers() const
    {
       return _app.p2p_node()->get_connected_peers();
    }

    std::vector<net::potential_peer_record> network_node_api::get_potential_peers() const
    {
       return _app.p2p_node()->get_potential_peers();
    }

    fc::variant_object network_node_api::get_advanced_node_parameters() const
    {
       return _app.p2p_node()->get_advanced_node_parameters();
    }

    void network_node_api::set_advanced_node_parameters(const fc::variant_object& params)
    {
       return _app.p2p_node()->set_advanced_node_parameters(params);
    }

    fc::api<network_broadcast_api> login_api::network_broadcast()const
    {
       FC_ASSERT(_network_broadcast_api);
       return *_network_broadcast_api;
    }

    fc::api<block_api> login_api::block()const
    {
       FC_ASSERT(_block_api);
       return *_block_api;
    }

    fc::api<network_node_api> login_api::network_node()const
    {
       FC_ASSERT(_network_node_api);
       return *_network_node_api;
    }

    fc::api<database_api> login_api::database()const
    {
       FC_ASSERT(_database_api);
       return *_database_api;
    }

    fc::api<history_api> login_api::history() const
    {
       FC_ASSERT(_history_api);
       return *_history_api;
    }

    fc::api<crypto_api> login_api::crypto() const
    {
       FC_ASSERT(_crypto_api);
       return *_crypto_api;
    }

    fc::api<asset_api> login_api::asset() const
    {
       FC_ASSERT(_asset_api);
       return *_asset_api;
    }

    fc::api<graphene::debug_witness::debug_api> login_api::debug() const
    {
       FC_ASSERT(_debug_api);
       return *_debug_api;
    }

    vector<account_id_type> get_relevant_accounts( const object* obj )
    {
       vector<account_id_type> result;
       if( obj->id.space() == protocol_ids )
       {
          switch( (object_type)obj->id.type() )
          {
            case null_object_type:
            case base_object_type:
            case OBJECT_TYPE_COUNT:
               return result;
            case account_object_type:{
               result.push_back( obj->id );
               break;
            } case asset_object_type:{
               const auto& aobj = dynamic_cast<const asset_object*>(obj);
               assert( aobj != nullptr );
               result.push_back( aobj->issuer );
               break;
            } case force_settlement_object_type:{
               const auto& aobj = dynamic_cast<const force_settlement_object*>(obj);
               assert( aobj != nullptr );
               result.push_back( aobj->owner );
               break;
            } case committee_member_object_type:{
               const auto& aobj = dynamic_cast<const committee_member_object*>(obj);
               assert( aobj != nullptr );
               result.push_back( aobj->committee_member_account );
               break;
            } case witness_object_type:{
               const auto& aobj = dynamic_cast<const witness_object*>(obj);
               assert( aobj != nullptr );
               result.push_back( aobj->witness_account );
               break;
            } case limit_order_object_type:{
               const auto& aobj = dynamic_cast<const limit_order_object*>(obj);
               assert( aobj != nullptr );
               result.push_back( aobj->seller );
               break;
            } case call_order_object_type:{
               const auto& aobj = dynamic_cast<const call_order_object*>(obj);
               assert( aobj != nullptr );
               result.push_back( aobj->borrower );
               break;
            } case custom_object_type:{
              break;
            } case proposal_object_type:{
               const auto& aobj = dynamic_cast<const proposal_object*>(obj);
               assert( aobj != nullptr );
               flat_set<account_id_type> impacted;
               transaction_get_impacted_accounts( aobj->proposed_transaction, impacted );
               result.reserve( impacted.size() );
               for( auto& item : impacted ) result.emplace_back(item);
               break;
            } case operation_history_object_type:{
               const auto& aobj = dynamic_cast<const operation_history_object*>(obj);
               assert( aobj != nullptr );
               flat_set<account_id_type> impacted;
               operation_get_impacted_accounts( aobj->op, impacted );
               result.reserve( impacted.size() );
               for( auto& item : impacted ) result.emplace_back(item);
               break;
            } case withdraw_permission_object_type:{
               const auto& aobj = dynamic_cast<const withdraw_permission_object*>(obj);
               assert( aobj != nullptr );
               result.push_back( aobj->withdraw_from_account );
               result.push_back( aobj->authorized_account );
               break;
            } case vesting_balance_object_type:{
               const auto& aobj = dynamic_cast<const vesting_balance_object*>(obj);
               assert( aobj != nullptr );
               result.push_back( aobj->owner );
               break;
            } case worker_object_type:{
               const auto& aobj = dynamic_cast<const worker_object*>(obj);
               assert( aobj != nullptr );
               result.push_back( aobj->worker_account );
               break;
            } case balance_object_type:{
               /** these are free from any accounts */
               break;
            }
          }
       }
       else if( obj->id.space() == implementation_ids )
       {
          switch( (impl_object_type)obj->id.type() )
          {
                 case impl_global_property_object_type:
                  break;
                 case impl_dynamic_global_property_object_type:
                  break;
                 case impl_reserved0_object_type:
                  break;
                 case impl_asset_dynamic_data_type:
                  break;
                 case impl_asset_bitasset_data_type:
                  break;
                 case impl_account_balance_object_type:{
                  const auto& aobj = dynamic_cast<const account_balance_object*>(obj);
                  assert( aobj != nullptr );
                  result.push_back( aobj->owner );
                  break;
               } case impl_account_statistics_object_type:{
                  const auto& aobj = dynamic_cast<const account_statistics_object*>(obj);
                  assert( aobj != nullptr );
                  result.push_back( aobj->owner );
                  break;
               } case impl_transaction_object_type:{
                  const auto& aobj = dynamic_cast<const transaction_object*>(obj);
                  assert( aobj != nullptr );
                  flat_set<account_id_type> impacted;
                  transaction_get_impacted_accounts( aobj->trx, impacted );
                  result.reserve( impacted.size() );
                  for( auto& item : impacted ) result.emplace_back(item);
                  break;
               } case impl_blinded_balance_object_type:{
                  const auto& aobj = dynamic_cast<const blinded_balance_object*>(obj);
                  assert( aobj != nullptr );
                  result.reserve( aobj->owner.account_auths.size() );
                  for( const auto& a : aobj->owner.account_auths )
                     result.push_back( a.first );
                  break;
               } case impl_block_summary_object_type:
                  break;
                 case impl_account_transaction_history_object_type:
                  break;
                 case impl_chain_property_object_type:
                  break;
                 case impl_witness_schedule_object_type:
                  break;
                 case impl_budget_record_object_type:
                  break;
                 case impl_special_authority_object_type:
                  break;
                 case impl_buyback_object_type:
                  break;
                 case impl_fba_accumulator_object_type:
                  break;
          }
       }
       return result;
    } // end get_relevant_accounts( obj )

    vector<order_history_object> history_api::get_fill_order_history( asset_id_type a, asset_id_type b, uint32_t limit  )const
    {
       FC_ASSERT(_app.chain_database());
       const auto& db = *_app.chain_database();
       if( a > b ) std::swap(a,b);
       const auto& history_idx = db.get_index_type<graphene::market_history::history_index>().indices().get<by_key>();
       history_key hkey;
       hkey.base = a;
       hkey.quote = b;
       hkey.sequence = std::numeric_limits<int64_t>::min();

       uint32_t count = 0;
       auto itr = history_idx.lower_bound( hkey );
       vector<order_history_object> result;
       while( itr != history_idx.end() && count < limit)
       {
          if( itr->key.base != a || itr->key.quote != b ) break;
          result.push_back( *itr );
          ++itr;
          ++count;
       }

       return result;
    }

    vector<operation_history_object> history_api::get_account_history( account_id_type account, 
                                                                       operation_history_id_type stop, 
                                                                       unsigned limit, 
                                                                       operation_history_id_type start ) const
    {
       FC_ASSERT( _app.chain_database() );
       const auto& db = *_app.chain_database();       
       FC_ASSERT( limit <= 100 );
       vector<operation_history_object> result;
       const auto& stats = account(db).statistics(db);
       if( stats.most_recent_op == account_transaction_history_id_type() ) return result;
       const account_transaction_history_object* node = &stats.most_recent_op(db);
       if( start == operation_history_id_type() )
          start = node->operation_id;
          
       while(node && node->operation_id.instance.value > stop.instance.value && result.size() < limit)
       {
          if( node->operation_id.instance.value <= start.instance.value )
             result.push_back( node->operation_id(db) );
          if( node->next == account_transaction_history_id_type() )
             node = nullptr;
          else node = &node->next(db);
       }
       
       return result;
    }
    
    vector<operation_history_object> history_api::get_account_history_operations( account_id_type account, 
                                                                       int operation_id,
                                                                       operation_history_id_type start, 
                                                                       operation_history_id_type stop,
                                                                       unsigned limit) const
    {
       FC_ASSERT( _app.chain_database() );
       const auto& db = *_app.chain_database();
       FC_ASSERT( limit <= 100 );
       vector<operation_history_object> result;
       const auto& stats = account(db).statistics(db);
       if( stats.most_recent_op == account_transaction_history_id_type() ) return result;
       const account_transaction_history_object* node = &stats.most_recent_op(db);
       if( start == operation_history_id_type() )
          start = node->operation_id;

       while(node && node->operation_id.instance.value > stop.instance.value && result.size() < limit)
       {
          if( node->operation_id.instance.value <= start.instance.value ) {

             if(node->operation_id(db).op.which() == operation_id)
               result.push_back( node->operation_id(db) );
             }
          if( node->next == account_transaction_history_id_type() )
             node = nullptr;
          else node = &node->next(db);
       }
       return result;
    }


    vector<operation_history_object> history_api::get_relative_account_history( account_id_type account, 
                                                                                uint32_t stop, 
                                                                                unsigned limit, 
                                                                                uint32_t start) const
    {
       FC_ASSERT( _app.chain_database() );
       const auto& db = *_app.chain_database();
       FC_ASSERT(limit <= 100);
       vector<operation_history_object> result;
       if( start == 0 )
         start = account(db).statistics(db).total_ops;
       else start = min( account(db).statistics(db).total_ops, start );
       const auto& hist_idx = db.get_index_type<account_transaction_history_index>();
       const auto& by_seq_idx = hist_idx.indices().get<by_seq>();
       
       auto itr = by_seq_idx.upper_bound( boost::make_tuple( account, start ) );
       auto itr_stop = by_seq_idx.lower_bound( boost::make_tuple( account, stop ) );
       --itr;
       
       while ( itr != itr_stop && result.size() < limit )
       {
          result.push_back( itr->operation_id(db) );
          --itr;
       }
       
       return result;
    }

    flat_set<uint32_t> history_api::get_market_history_buckets()const
    {
       auto hist = _app.get_plugin<market_history_plugin>( "market_history" );
       FC_ASSERT( hist );
       return hist->tracked_buckets();
    }

    vector<bucket_object> history_api::get_market_history( asset_id_type a, asset_id_type b,
                                                           uint32_t bucket_seconds, fc::time_point_sec start, fc::time_point_sec end )const
    { try {
       FC_ASSERT(_app.chain_database());
       const auto& db = *_app.chain_database();
       vector<bucket_object> result;
       result.reserve(200);

       if( a > b ) std::swap(a,b);

       const auto& bidx = db.get_index_type<bucket_index>();
       const auto& by_key_idx = bidx.indices().get<by_key>();

       auto itr = by_key_idx.lower_bound( bucket_key( a, b, bucket_seconds, start ) );
       while( itr != by_key_idx.end() && itr->key.open <= end && result.size() < 200 )
       {
          if( !(itr->key.base == a && itr->key.quote == b && itr->key.seconds == bucket_seconds) )
          {
            return result;
          }
          result.push_back(*itr);
          ++itr;
       }
       return result;
    } FC_CAPTURE_AND_RETHROW( (a)(b)(bucket_seconds)(start)(end) ) }
    
    crypto_api::crypto_api(){};
    
    blind_signature crypto_api::blind_sign( const extended_private_key_type& key, const blinded_hash& hash, int i )
    {
       return fc::ecc::extended_private_key( key ).blind_sign( hash, i );
    }
         
    signature_type crypto_api::unblind_signature( const extended_private_key_type& key,
                                                     const extended_public_key_type& bob,
                                                     const blind_signature& sig,
                                                     const fc::sha256& hash,
                                                     int i )
    {
       return fc::ecc::extended_private_key( key ).unblind_signature( extended_public_key( bob ), sig, hash, i );
    }
                                                               
    commitment_type crypto_api::blind( const blind_factor_type& blind, uint64_t value )
    {
       return fc::ecc::blind( blind, value );
    }
   
    blind_factor_type crypto_api::blind_sum( const std::vector<blind_factor_type>& blinds_in, uint32_t non_neg )
    {
       return fc::ecc::blind_sum( blinds_in, non_neg );
    }
   
    bool crypto_api::verify_sum( const std::vector<commitment_type>& commits_in, const std::vector<commitment_type>& neg_commits_in, int64_t excess )
    {
       return fc::ecc::verify_sum( commits_in, neg_commits_in, excess );
    }
    
    verify_range_result crypto_api::verify_range( const commitment_type& commit, const std::vector<char>& proof )
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
                                                    uint64_t actual_value )
    {
       return fc::ecc::range_proof_sign( min_value, commit, commit_blind, nonce, base10_exp, min_bits, actual_value );
    }
                               
    verify_range_proof_rewind_result crypto_api::verify_range_proof_rewind( const blind_factor_type& nonce,
                                                                            const commitment_type& commit, 
                                                                            const std::vector<char>& proof )
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
                                    
    range_proof_info crypto_api::range_get_info( const std::vector<char>& proof )
    {
       return fc::ecc::range_get_info( proof );
    }

    // asset_api
    asset_api::asset_api(graphene::chain::database& db) : _db(db) { }
    asset_api::~asset_api() { }

    vector<account_asset_balance> asset_api::get_asset_holders( asset_id_type asset_id ) const {

      const auto& bal_idx = _db.get_index_type< account_balance_index >().indices().get< by_asset_balance >();
      auto range = bal_idx.equal_range( boost::make_tuple( asset_id ) );

      vector<account_asset_balance> result;

      for( const account_balance_object& bal : boost::make_iterator_range( range.first, range.second ) )
      {
        if( bal.balance.value == 0 ) continue;

        auto account = _db.find(bal.owner);

        account_asset_balance aab;
        aab.name       = account->name;
        aab.account_id = account->id;
        aab.amount     = bal.balance.value;

        result.push_back(aab);
      }

      return result;
    }
    // get number of asset holders.
    int asset_api::get_asset_holders_count( asset_id_type asset_id ) const {

      const auto& bal_idx = _db.get_index_type< account_balance_index >().indices().get< by_asset_balance >();
      auto range = bal_idx.equal_range( boost::make_tuple( asset_id ) );
            
      int count = boost::distance(range) - 1;

      return count;
    }
    // function to get vector of system assets with holders count.
    vector<asset_holders> asset_api::get_all_asset_holders() const {
            
      vector<asset_holders> result;
            
      vector<asset_id_type> total_assets;
      for( const asset_object& asset_obj : _db.get_index_type<asset_index>().indices() )
      {
        const auto& dasset_obj = asset_obj.dynamic_asset_data_id(_db);

        asset_id_type asset_id;
        asset_id = dasset_obj.id;

        const auto& bal_idx = _db.get_index_type< account_balance_index >().indices().get< by_asset_balance >();
        auto range = bal_idx.equal_range( boost::make_tuple( asset_id ) );

        int count = boost::distance(range) - 1;
                
        asset_holders ah;
        ah.asset_id       = asset_id;
        ah.count     = count;

        result.push_back(ah);
      }

      return result;
    }

} } // graphene::app
