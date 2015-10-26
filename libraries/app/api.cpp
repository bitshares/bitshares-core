/*
 * Copyright (c) 2015 Cryptonomex, Inc., and contributors.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:
 *
 * 1. Any modified source or binaries are used only with the BitShares network.
 *
 * 2. Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
 *
 * 3. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
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
#include <graphene/chain/withdraw_permission_object.hpp>
#include <graphene/chain/worker_evaluator.hpp>
#include <graphene/chain/transaction_object.hpp>
#include <graphene/chain/confidential_evaluator.hpp>

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
       return;
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

    fc::variant network_node_api::get_info() const
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

    fc::api<network_broadcast_api> login_api::network_broadcast()const
    {
       FC_ASSERT(_network_broadcast_api);
       return *_network_broadcast_api;
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
            }
          }
       }
       else if( obj->id.space() == implementation_ids )
       {
          switch( (impl_object_type)obj->id.type() )
          {
                 case impl_global_property_object_type:{
               } case impl_dynamic_global_property_object_type:{
               } case impl_reserved0_object_type:{
               } case impl_asset_dynamic_data_type:{
               } case impl_asset_bitasset_data_type:{
                  break;
               } case impl_account_balance_object_type:{
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
               } case impl_block_summary_object_type:{
               } case impl_account_transaction_history_object_type:{
               } case impl_chain_property_object_type: {
               } case impl_witness_schedule_object_type: {
               } case impl_budget_record_object_type: {
               }
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
       while( itr != history_idx.end() )
       {
          if( itr->key.base != a || itr->key.quote != b ) break;
          result.push_back( *itr );
          ++itr;
          ++count;
          if( count  > limit ) break;
       }

       return result;
    }

    vector<operation_history_object> history_api::get_account_history(account_id_type account, operation_history_id_type stop, unsigned limit, operation_history_id_type start) const
    {
       FC_ASSERT(_app.chain_database());
       const auto& db = *_app.chain_database();
       FC_ASSERT(limit <= 100);
       vector<operation_history_object> result;
       const auto& stats = account(db).statistics(db);
       if(stats.most_recent_op == account_transaction_history_id_type()) return result;
       const account_transaction_history_object* node = &stats.most_recent_op(db);
       if(start == operation_history_id_type())
          start = node->id;
       while(node && node->operation_id.instance.value > stop.instance.value && result.size() < limit)
       {
          if(node->id.instance() <= start.instance.value)
             result.push_back(node->operation_id(db));
          if(node->next == account_transaction_history_id_type())
             node = nullptr;
          else node = db.find(node->next);
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

} } // graphene::app
