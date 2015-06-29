/*
 * Copyright (c) 2015, Cryptonomex, Inc.
 * All rights reserved.
 *
 * This source code is provided for evaluation in private test networks only, until September 8, 2015. After this date, this license expires and
 * the code may not be used, modified or distributed for any purpose. Redistribution and use in source and binary forms, with or without modification,
 * are permitted until September 8, 2015, provided that the following conditions are met:
 *
 * 1. The code and/or derivative works are used only for private test networks consisting of no more than 10 P2P nodes.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#include <graphene/app/api.hpp>
#include <graphene/app/application.hpp>
#include <graphene/chain/database.hpp>
#include <graphene/utilities/key_conversion.hpp>

#include <fc/crypto/hex.hpp>

namespace graphene { namespace app {

    database_api::database_api(graphene::chain::database& db):_db(db)
    {
       _change_connection = _db.changed_objects.connect([this](const vector<object_id_type>& ids) {
                                    on_objects_changed(ids);
                                    });
       _applied_block_connection = _db.applied_block.connect([this](const signed_block&){ on_applied_block(); });
    }

    fc::variants database_api::get_objects(const vector<object_id_type>& ids)const
    {
       fc::variants result;
       result.reserve(ids.size());

       std::transform(ids.begin(), ids.end(), std::back_inserter(result),
                      [this](object_id_type id) -> fc::variant {
          if(auto obj = _db.find_object(id))
             return obj->to_variant();
          return {};
       });

       return result;
    }

    optional<block_header> database_api::get_block_header(uint32_t block_num) const
    {
       auto result = _db.fetch_block_by_number(block_num);
       if(result)
          return *result;
       return {};
    }

    optional<signed_block> database_api::get_block(uint32_t block_num)const
    {
       return _db.fetch_block_by_number(block_num);
    }
    processed_transaction database_api::get_transaction(uint32_t block_num, uint32_t trx_num)const
    {
       auto opt_block = _db.fetch_block_by_number(block_num);
       FC_ASSERT( opt_block );
       FC_ASSERT( opt_block->transactions.size() > trx_num );
       return opt_block->transactions[trx_num];
    }

    vector<optional<account_object>> database_api::lookup_account_names(const vector<string>& account_names)const
    {
       const auto& accounts_by_name = _db.get_index_type<account_index>().indices().get<by_name>();
       vector<optional<account_object> > result;
       result.reserve(account_names.size());
       std::transform(account_names.begin(), account_names.end(), std::back_inserter(result),
                      [&accounts_by_name](const string& name) -> optional<account_object> {
          auto itr = accounts_by_name.find(name);
          return itr == accounts_by_name.end()? optional<account_object>() : *itr;
       });
       return result;
    }

    vector<optional<asset_object>> database_api::lookup_asset_symbols(const vector<string>& symbols)const
    {
       const auto& assets_by_symbol = _db.get_index_type<asset_index>().indices().get<by_symbol>();
       vector<optional<asset_object> > result;
       result.reserve(symbols.size());
       std::transform(symbols.begin(), symbols.end(), std::back_inserter(result),
                      [&assets_by_symbol](const string& symbol) -> optional<asset_object> {
          auto itr = assets_by_symbol.find(symbol);
          return itr == assets_by_symbol.end()? optional<asset_object>() : *itr;
       });
       return result;
    }

    global_property_object database_api::get_global_properties()const
    {
       return _db.get(global_property_id_type());
    }

    dynamic_global_property_object database_api::get_dynamic_global_properties()const
    {
       return _db.get(dynamic_global_property_id_type());
    }

    vector<optional<key_object>> database_api::get_keys(const vector<key_id_type>& key_ids)const
    {
       vector<optional<key_object>> result; result.reserve(key_ids.size());
       std::transform(key_ids.begin(), key_ids.end(), std::back_inserter(result),
                      [this](key_id_type id) -> optional<key_object> {
          if(auto o = _db.find(id))
             return *o;
          return {};
       });
       return result;
    }

    vector<optional<account_object>> database_api::get_accounts(const vector<account_id_type>& account_ids)const
    {
       vector<optional<account_object>> result; result.reserve(account_ids.size());
       std::transform(account_ids.begin(), account_ids.end(), std::back_inserter(result),
                      [this](account_id_type id) -> optional<account_object> {
          if(auto o = _db.find(id))
             return *o;
          return {};
       });
       return result;
    }

    vector<optional<asset_object>> database_api::get_assets(const vector<asset_id_type>& asset_ids)const
    {
       vector<optional<asset_object>> result; result.reserve(asset_ids.size());
       std::transform(asset_ids.begin(), asset_ids.end(), std::back_inserter(result),
                      [this](asset_id_type id) -> optional<asset_object> {
          if(auto o = _db.find(id))
             return *o;
          return {};
       });
       return result;
    }

    uint64_t database_api::get_account_count()const
    {
       return _db.get_index_type<account_index>().indices().size();
    }

    map<string,account_id_type> database_api::lookup_accounts(const string& lower_bound_name, uint32_t limit)const
    {
       FC_ASSERT( limit <= 1000 );
       const auto& accounts_by_name = _db.get_index_type<account_index>().indices().get<by_name>();
       map<string,account_id_type> result;

       for( auto itr = accounts_by_name.lower_bound(lower_bound_name);
            limit-- && itr != accounts_by_name.end();
            ++itr )
          result.insert(make_pair(itr->name, itr->get_id()));

       return result;
    }

    vector<asset> database_api::get_account_balances(account_id_type acnt, const flat_set<asset_id_type>& assets)const
    {
       vector<asset> result;
       if (assets.empty())
       {
          // if the caller passes in an empty list of assets, return balances for all assets the account owns
          const account_balance_index& balance_index = _db.get_index_type<account_balance_index>();
          auto range = balance_index.indices().get<by_account>().equal_range(acnt);
          for (const account_balance_object& balance : boost::make_iterator_range(range.first, range.second))
             result.push_back(asset(balance.get_balance()));
       }
       else
       {
          result.reserve(assets.size());

          std::transform(assets.begin(), assets.end(), std::back_inserter(result),
                         [this, acnt](asset_id_type id) { return _db.get_balance(acnt, id); });
       }

       return result;
    }

    vector<asset> database_api::get_named_account_balances(const std::string& name, const flat_set<asset_id_type>& assets) const
    {
       const auto& accounts_by_name = _db.get_index_type<account_index>().indices().get<by_name>();
       auto itr = accounts_by_name.find(name);
       FC_ASSERT( itr != accounts_by_name.end() );
       return get_account_balances(itr->get_id(), assets);
    }

    /**
     *  @return the limit orders for both sides of the book for the two assets specified up to limit number on each side.
     */
    vector<limit_order_object> database_api::get_limit_orders(asset_id_type a, asset_id_type b, uint32_t limit)const
    {
       const auto& limit_order_idx = _db.get_index_type<limit_order_index>();
       const auto& limit_price_idx = limit_order_idx.indices().get<by_price>();

       vector<limit_order_object> result;

       uint32_t count = 0;
       auto limit_itr = limit_price_idx.lower_bound(price::max(a,b));
       auto limit_end = limit_price_idx.upper_bound(price::min(a,b));
       while(limit_itr != limit_end && count < limit)
       {
          result.push_back(*limit_itr);
          ++limit_itr;
          ++count;
       }
       count = 0;
       limit_itr = limit_price_idx.lower_bound(price::max(b,a));
       limit_end = limit_price_idx.upper_bound(price::min(b,a));
       while(limit_itr != limit_end && count < limit)
       {
          result.push_back(*limit_itr);
          ++limit_itr;
          ++count;
       }

       return result;
    }

    vector<call_order_object> database_api::get_call_orders(asset_id_type a, uint32_t limit)const
    {
       const auto& call_index = _db.get_index_type<call_order_index>().indices().get<by_price>();
       const asset_object& mia = _db.get(a);
       price index_price = price::min(mia.bitasset_data(_db).options.short_backing_asset, mia.get_id());

       return vector<call_order_object>(call_index.lower_bound(index_price.min()),
                                        call_index.lower_bound(index_price.max()));
    }

    vector<force_settlement_object> database_api::get_settle_orders(asset_id_type a, uint32_t limit)const
    {
       const auto& settle_index = _db.get_index_type<force_settlement_index>().indices().get<by_expiration>();
       const asset_object& mia = _db.get(a);
       return vector<force_settlement_object>(settle_index.lower_bound(mia.get_id()),
                                              settle_index.upper_bound(mia.get_id()));
    }

    vector<asset_object> database_api::list_assets(const string& lower_bound_symbol, uint32_t limit)const
    {
       FC_ASSERT( limit <= 100 );
       const auto& assets_by_symbol = _db.get_index_type<asset_index>().indices().get<by_symbol>();
       vector<asset_object> result;
       result.reserve(limit);

       auto itr = assets_by_symbol.lower_bound(lower_bound_symbol);

       if( lower_bound_symbol == "" )
          itr = assets_by_symbol.begin();

       while(limit-- && itr != assets_by_symbol.end())
          result.emplace_back(*itr++);

       return result;
    }

    fc::optional<delegate_object> database_api::get_delegate_by_account(account_id_type account) const
    {
       const auto& idx = _db.get_index_type<delegate_index>().indices().get<by_account>();
       auto itr = idx.find(account);
       if( itr != idx.end() )
          return *itr;
       return {};
    }

    fc::optional<witness_object> database_api::get_witness_by_account(account_id_type account) const
    {
       const auto& idx = _db.get_index_type<witness_index>().indices().get<by_account>();
       auto itr = idx.find(account);
       if( itr != idx.end() )
          return *itr;
       return {};
    }

    login_api::login_api(application& a)
    :_app(a)
    {
    }
    login_api::~login_api()
    {
    }

    bool login_api::login(const string& user, const string& password)
    {
       auto db_api = std::make_shared<database_api>(std::ref(*_app.chain_database()));
       auto net_api = std::make_shared<network_api>(std::ref(_app));
       auto hist_api = std::make_shared<history_api>(_app);
       _database_api = db_api;
       _network_api = net_api;
       _history_api = hist_api;
       return true;
    }

    void network_api::add_node(const fc::ip::endpoint& ep)
    {
       _app.p2p_node()->add_node(ep);
    }

    void network_api::broadcast_transaction(const signed_transaction& trx)
    {
       trx.validate();
       _app.chain_database()->push_transaction(trx);
       _app.p2p_node()->broadcast_transaction(trx);
    }

    std::vector<net::peer_status> network_api::get_connected_peers() const
    {
      return _app.p2p_node()->get_connected_peers();
    }

    fc::api<network_api> login_api::network()const
    {
       FC_ASSERT(_network_api);
       return *_network_api;
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

    void database_api::on_objects_changed(const vector<object_id_type>& ids)
    {
       vector<object_id_type> my_objects;
       for(auto id : ids)
          if(_subscriptions.find(id) != _subscriptions.end())
             my_objects.push_back(id);

       _broadcast_changes_complete = fc::async([=](){
          for(auto id : my_objects)
          {
             const object* obj = _db.find_object(id);
             if(obj)
             {
                _subscriptions[id](obj->to_variant());
             }
             else
             {
                _subscriptions[id](fc::variant(id));
             }
          }
       });
    }

    /** note: this method cannot yield because it is called in the middle of
     * apply a block.
     */
    void database_api::on_applied_block()
    {
       if(_market_subscriptions.size() == 0)
          return;

       const auto& ops = _db.get_applied_operations();
       map< std::pair<asset_id_type,asset_id_type>, vector<pair<operation, operation_result>> > subscribed_markets_ops;
       for(const auto& op : ops)
       {
          std::pair<asset_id_type,asset_id_type> market;
          switch(op.op.which())
          {
             case operation::tag<limit_order_create_operation>::value:
                market = op.op.get<limit_order_create_operation>().get_market();
                break;
             case operation::tag<fill_order_operation>::value:
                market = op.op.get<fill_order_operation>().get_market();
                break;
                /*
             case operation::tag<limit_order_cancel_operation>::value:
             */
             default: break;
          }
          if(_market_subscriptions.count(market))
             subscribed_markets_ops[market].push_back(std::make_pair(op.op, op.result));
       }
       fc::async([=](){
          for(auto item : subscribed_markets_ops)
          {
             auto itr = _market_subscriptions.find(item.first);
             if(itr != _market_subscriptions.end())
                itr->second(fc::variant(item.second));
          }
       });
    }

    database_api::~database_api()
    {
       try {
          if(_broadcast_changes_complete.valid())
          {
             _broadcast_changes_complete.cancel();
             _broadcast_changes_complete.wait();
          }
       } catch (const fc::exception& e)
       {
          wlog("${e}", ("e",e.to_detail_string()));
       }
    }

    bool database_api::subscribe_to_objects( const std::function<void(const fc::variant&)>&  callback, const vector<object_id_type>& ids)
    {
       for(auto id : ids) _subscriptions[id] = callback;
       return true;
    }

    bool database_api::unsubscribe_from_objects(const vector<object_id_type>& ids)
    {
       for(auto id : ids) _subscriptions.erase(id);
       return true;
    }

    void database_api::subscribe_to_market(std::function<void(const variant&)> callback, asset_id_type a, asset_id_type b)
    {
       if(a > b) std::swap(a,b);
       FC_ASSERT(a != b);
       _market_subscriptions[ std::make_pair(a,b) ] = callback;
    }

    void database_api::unsubscribe_from_market(asset_id_type a, asset_id_type b)
    {
       if(a > b) std::swap(a,b);
       FC_ASSERT(a != b);
       _market_subscriptions.erase(std::make_pair(a,b));
    }

    std::string database_api::get_transaction_hex(const signed_transaction& trx)const
    {
       return fc::to_hex(fc::raw::pack(trx));
    }

    vector<operation_history_object> history_api::get_account_history(account_id_type account, operation_history_id_type stop, int limit, operation_history_id_type start) const
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
       result.reserve(100);

       if( a > b ) std::swap(a,b);

       const auto& bidx = db.get_index_type<bucket_index>();
       const auto& by_key_idx = bidx.indices().get<by_key>();

       auto itr = by_key_idx.lower_bound( bucket_key( a, b, bucket_seconds, start ) );
       while( itr != by_key_idx.end() && itr->key.open <= end && result.size() < 100 )
       {
          if( !(itr->key.base == a && itr->key.quote == b && itr->key.seconds == bucket_seconds) )
            return result;
          result.push_back(*itr);
          ++itr;
       }
       return result;
    } FC_CAPTURE_AND_RETHROW( (a)(b)(bucket_seconds)(start)(end) ) }

    /**
     *  @return all accounts that referr to the key or account id in their owner or active authorities.
     */
    vector<account_id_type> database_api::get_account_references( object_id_type key_or_account_id )const
    {
       const auto& idx = _db.get_index_type<account_index>();
       const auto& aidx = dynamic_cast<const primary_index<account_index>&>(idx);
       const auto& refs = aidx.get_secondary_index<graphene::chain::account_member_index>();
       auto itr = refs.account_to_memberships.find(key_or_account_id);
       vector<account_id_type> result;

       if( itr != refs.account_to_memberships.end() )
       {
          result.reserve( itr->second.size() );
          for( auto item : itr->second ) result.push_back(item);
       }
       return result;
    }

    /** TODO: add secondary index that will accelerate this process */
    vector<proposal_object> database_api::get_proposed_transactions( account_id_type id )const
    {
       const auto& idx = _db.get_index_type<proposal_index>();
       vector<proposal_object> result;

       idx.inspect_all_objects( [&](const object& obj){
               const proposal_object& p = static_cast<const proposal_object&>(obj);
               if( p.required_active_approvals.find( id ) != p.required_active_approvals.end() )
                  result.push_back(p);
               else if ( p.required_owner_approvals.find( id ) != p.required_owner_approvals.end() )
                  result.push_back(p);
               else if ( p.available_active_approvals.find( id ) != p.available_active_approvals.end() )
                  result.push_back(p);
       });
       return result;
    }

    /**
     *  @return all key_ids that have been registered for a given address. 
     */
    vector<key_id_type>  database_api::get_keys_for_address( const address& a )const
    { try {
       vector<key_id_type> result;
       const auto& idx = _db.get_index_type<key_index>();
       const auto& aidx = idx.indices().get<by_address>();
       auto itr = aidx.find(a);

       while( itr != aidx.end() && itr->key_address() == a )
       {
          result.push_back( itr->id );
          ++itr;
       }
       return result;
    } FC_CAPTURE_AND_RETHROW( (a) ) } 

    vector<call_order_object> database_api::get_margin_positions( const account_id_type& id )const
    { try {
       const auto& idx = _db.get_index_type<call_order_index>();
       const auto& aidx = idx.indices().get<by_account>();
       auto start = aidx.lower_bound( boost::make_tuple( id, 0 ) );
       auto end = aidx.lower_bound( boost::make_tuple( id+1, 0 ) );
       vector<call_order_object> result;
       while( start != end )
       {
          result.push_back(*start);
          ++start;
       }
       return result;
    } FC_CAPTURE_AND_RETHROW( (id) ) }


    vector<balance_object>  database_api::get_balance_objects( const vector<address>& addrs )const
    { try {
         const auto& bal_idx = _db.get_index_type<balance_index>();
         const auto& by_owner_idx = bal_idx.indices().get<by_owner>();

         vector<balance_object> result;

         for( const auto& owner : addrs )
         {
            auto itr = by_owner_idx.lower_bound( boost::make_tuple( owner, asset_id_type(0) ) );
            while( itr != by_owner_idx.end() && itr->owner == owner )
            {
               result.push_back( *itr );
               ++itr;
            }
         }
         return result;
    } FC_CAPTURE_AND_RETHROW( (addrs) ) }


} } // graphene::app
