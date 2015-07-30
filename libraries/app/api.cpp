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
#include <cctype>

#include <graphene/app/api.hpp>
#include <graphene/app/api_access.hpp>
#include <graphene/app/application.hpp>
#include <graphene/chain/database.hpp>
#include <graphene/utilities/key_conversion.hpp>
#include <graphene/chain/protocol/fee_schedule.hpp>
#include <graphene/chain/withdraw_permission_object.hpp>
#include <graphene/chain/worker_evaluator.hpp>
#include <graphene/chain/transaction_object.hpp>
#include <graphene/chain/confidential_evaluator.hpp>

#include <fc/crypto/hex.hpp>
#include <fc/smart_ref_impl.hpp>

namespace graphene { namespace app {

    database_api::database_api(graphene::chain::database& db):_db(db)
    {
       wlog("creating database api ${x}", ("x",int64_t(this)) );
       _change_connection = _db.changed_objects.connect([this](const vector<object_id_type>& ids) {
                                    on_objects_changed(ids);
                                    });
       _removed_connection = _db.removed_objects.connect([this](const vector<const object*>& objs) {
                                    on_objects_removed(objs);
                                    });
       _applied_block_connection = _db.applied_block.connect([this](const signed_block&){ on_applied_block(); });
    }

    database_api::~database_api()
    {
       elog("freeing database api ${x}", ("x",int64_t(this)) );
       try {
          if(_broadcast_changes_complete.valid())
          {
             _broadcast_changes_complete.cancel();
              ilog( "waiting..");
             _broadcast_changes_complete.wait();
          }
          if(_broadcast_removed_complete.valid())
          {
             _broadcast_removed_complete.cancel();
              ilog( "waiting..");
             _broadcast_removed_complete.wait();
          }
          ilog( "done" );
       } catch (const fc::exception& e)
       {
          wlog("${e}", ("e",e.to_detail_string()));
       }
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

    vector<optional<asset_object>> database_api::lookup_asset_symbols(const vector<string>& symbols_or_ids)const
    {
       const auto& assets_by_symbol = _db.get_index_type<asset_index>().indices().get<by_symbol>();
       vector<optional<asset_object> > result;
       result.reserve(symbols_or_ids.size());
       std::transform(symbols_or_ids.begin(), symbols_or_ids.end(), std::back_inserter(result),
                      [this, &assets_by_symbol](const string& symbol_or_id) -> optional<asset_object> {
          if( !symbol_or_id.empty() && std::isdigit(symbol_or_id[0]) )
          {
             auto ptr = _db.find(variant(symbol_or_id).as<asset_id_type>());
             return ptr == nullptr? optional<asset_object>() : *ptr;
          }
          auto itr = assets_by_symbol.find(symbol_or_id);
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
    void database_api::unsubscribe_from_accounts( const vector<std::string>& names_or_ids )
    {
       for (const std::string& account_name_or_id : names_or_ids)
       {
          const account_object* account = nullptr;
          if (std::isdigit(account_name_or_id[0]))
             account = _db.find(fc::variant(account_name_or_id).as<account_id_type>());
          else
          {
             const auto& idx = _db.get_index_type<account_index>().indices().get<by_name>();
             auto itr = idx.find(account_name_or_id);
             if (itr != idx.end())
                account = &*itr;
          }
          if (account == nullptr)
             continue;

          _account_subscriptions.erase(account->id);
       }
    }

    std::map<std::string, full_account> database_api::get_full_accounts(std::function<void(const variant&)> callback,
                                                                       const vector<std::string>& names_or_ids, bool subscribe)
    {
       FC_ASSERT( _account_subscriptions.size() < 1024 );
       std::map<std::string, full_account> results;

       for (const std::string& account_name_or_id : names_or_ids)
       {
          const account_object* account = nullptr;
          if (std::isdigit(account_name_or_id[0]))
             account = _db.find(fc::variant(account_name_or_id).as<account_id_type>());
          else
          {
             const auto& idx = _db.get_index_type<account_index>().indices().get<by_name>();
             auto itr = idx.find(account_name_or_id);
             if (itr != idx.end())
                account = &*itr;
          }
          if (account == nullptr)
             continue;

          if( subscribe )
          {
             ilog( "subscribe to ${id}", ("id",account->name) );
             _account_subscriptions[account->id] = callback;
          }
          else
          {
             wlog( "unsubscribe to ${id}", ("id",account->name) );
             _account_subscriptions.erase(account->id);
          }

          // fc::mutable_variant_object full_account;
          full_account acnt;
          acnt.account = *account;
          acnt.statistics = account->statistics(_db);
          acnt.registrar_name = account->registrar(_db).name;
          acnt.referrer_name = account->referrer(_db).name;
          acnt.lifetime_referrer_name = account->lifetime_referrer(_db).name;

          // Add the account itself, its statistics object, cashback balance, and referral account names
          /*
          full_account("account", *account)("statistics", account->statistics(_db))
                ("registrar_name", account->registrar(_db).name)("referrer_name", account->referrer(_db).name)
                ("lifetime_referrer_name", account->lifetime_referrer(_db).name);
                */
          if (account->cashback_vb)
          {
             acnt.cashback_balance = account->cashback_balance(_db);
          }

          // Add the account's balances
          auto balance_range = _db.get_index_type<account_balance_index>().indices().get<by_account>().equal_range(account->id);
          //vector<account_balance_object> balances;
          std::for_each(balance_range.first, balance_range.second,
                        [&acnt](const account_balance_object& balance) {
                           acnt.balances.emplace_back(balance);
                        });

          // Add the account's vesting balances
          auto vesting_range = _db.get_index_type<vesting_balance_index>().indices().get<by_account>().equal_range(account->id);
          std::for_each(vesting_range.first, vesting_range.second,
                        [&acnt](const vesting_balance_object& balance) {
                           acnt.vesting_balances.emplace_back(balance);
                        });

          // Add the account's orders
          auto order_range = _db.get_index_type<limit_order_index>().indices().get<by_account>().equal_range(account->id);
          std::for_each(order_range.first, order_range.second,
                        [&acnt] (const limit_order_object& order) {
                           acnt.limit_orders.emplace_back(order);
                        });
          auto call_range = _db.get_index_type<call_order_index>().indices().get<by_account>().equal_range(account->id);
          std::for_each(call_range.first, call_range.second,
                        [&acnt] (const call_order_object& call) {
                           acnt.call_orders.emplace_back(call);
                        });
          results[account_name_or_id] = acnt;
       }
       wdump((results));
       return results;
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

    fc::optional<committee_member_object> database_api::get_committee_member_by_account(account_id_type account) const
    {
       const auto& idx = _db.get_index_type<committee_member_index>().indices().get<by_account>();
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

    uint64_t database_api::get_witness_count()const
    {
       return _db.get_index_type<witness_index>().indices().size();
    }

    map<string, witness_id_type> database_api::lookup_witness_accounts(const string& lower_bound_name, uint32_t limit)const
    {
       FC_ASSERT( limit <= 1000 );
       const auto& witnesses_by_id = _db.get_index_type<witness_index>().indices().get<by_id>();

       // we want to order witnesses by account name, but that name is in the account object
       // so the witness_index doesn't have a quick way to access it.
       // get all the names and look them all up, sort them, then figure out what
       // records to return.  This could be optimized, but we expect the
       // number of witnesses to be few and the frequency of calls to be rare
       std::map<std::string, witness_id_type> witnesses_by_account_name;
       for (const witness_object& witness : witnesses_by_id)
           if (auto account_iter = _db.find(witness.witness_account))
               if (account_iter->name >= lower_bound_name) // we can ignore anything below lower_bound_name
                   witnesses_by_account_name.insert(std::make_pair(account_iter->name, witness.id));

       auto end_iter = witnesses_by_account_name.begin();
       while (end_iter != witnesses_by_account_name.end() && limit--)
           ++end_iter;
       witnesses_by_account_name.erase(end_iter, witnesses_by_account_name.end());
       return witnesses_by_account_name;
    }

    map<string, committee_member_id_type> database_api::lookup_committee_member_accounts(const string& lower_bound_name, uint32_t limit)const
    {
       FC_ASSERT( limit <= 1000 );
       const auto& committee_members_by_id = _db.get_index_type<committee_member_index>().indices().get<by_id>();

       // we want to order committee_members by account name, but that name is in the account object
       // so the committee_member_index doesn't have a quick way to access it.
       // get all the names and look them all up, sort them, then figure out what
       // records to return.  This could be optimized, but we expect the
       // number of committee_members to be few and the frequency of calls to be rare
       std::map<std::string, committee_member_id_type> committee_members_by_account_name;
       for (const committee_member_object& committee_member : committee_members_by_id)
           if (auto account_iter = _db.find(committee_member.committee_member_account))
               if (account_iter->name >= lower_bound_name) // we can ignore anything below lower_bound_name
                   committee_members_by_account_name.insert(std::make_pair(account_iter->name, committee_member.id));

       auto end_iter = committee_members_by_account_name.begin();
       while (end_iter != committee_members_by_account_name.end() && limit--)
           ++end_iter;
       committee_members_by_account_name.erase(end_iter, committee_members_by_account_name.end());
       return committee_members_by_account_name;
    }

    vector<optional<witness_object>> database_api::get_witnesses(const vector<witness_id_type>& witness_ids)const
    {
       vector<optional<witness_object>> result; result.reserve(witness_ids.size());
       std::transform(witness_ids.begin(), witness_ids.end(), std::back_inserter(result),
                      [this](witness_id_type id) -> optional<witness_object> {
          if(auto o = _db.find(id))
             return *o;
          return {};
       });
       return result;
    }

    vector<optional<committee_member_object>> database_api::get_committee_members(const vector<committee_member_id_type>& committee_member_ids)const
    {
       vector<optional<committee_member_object>> result; result.reserve(committee_member_ids.size());
       std::transform(committee_member_ids.begin(), committee_member_ids.end(), std::back_inserter(result),
                      [this](committee_member_id_type id) -> optional<committee_member_object> {
          if(auto o = _db.find(id))
             return *o;
          return {};
       });
       return result;
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
          for( uint32_t trx_num = 0; trx_num < b.transactions.size(); ++trx_num )
          {
             const auto& trx = b.transactions[trx_num];
             auto id = trx.id();
             auto itr = _callbacks.find(id);
             auto block_num = b.block_num();
             if( itr != _callbacks.end() )
             {
                fc::async( [=](){ itr->second( fc::variant(transaction_confirmation{ id, block_num, trx_num, trx}) ); } );
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

    void network_broadcast_api::broadcast_transaction_with_callback( confirmation_callback cb, const signed_transaction& trx)
    {
       trx.validate();
       _callbacks[trx.id()] = cb;
       _app.chain_database()->push_transaction(trx);
       _app.p2p_node()->broadcast_transaction(trx);
    }

    network_node_api::network_node_api( application& a ) : _app( a )
    {
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
               aobj->proposed_transaction.get_impacted_accounts( impacted );
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
               } case impl_index_meta_object_type:{
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
                  aobj->trx.get_impacted_accounts( impacted );
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
               } case impl_witness_schedule_object_type: {
               }
          }
       }
       return result;
    } // end get_relevant_accounts( obj )


    void database_api::on_objects_removed( const vector<const object*>& objs )
    {
       if( _account_subscriptions.size() )
       {
          map<account_id_type, vector<variant> > broadcast_queue; 
          for( const auto& obj : objs )
          {
             auto relevant = get_relevant_accounts( obj );
             for( const auto& r : relevant )
             {
                auto sub = _account_subscriptions.find(r);
                if( sub != _account_subscriptions.end() )
                   broadcast_queue[r].emplace_back(obj->to_variant());
             }
          }

          if( broadcast_queue.size() )
          {
             _broadcast_removed_complete = fc::async([=](){
                 for( const auto& item : broadcast_queue )
                 {
                   idump((item.first)(item.second) );
                   auto sub = _account_subscriptions.find(item.first);
                   if( sub != _account_subscriptions.end() )
                       sub->second( fc::variant(item.second ) );
                 }
             });
          }
       }
       if( _market_subscriptions.size() )
       {
          map< pair<asset_id_type, asset_id_type>, vector<variant> > broadcast_queue;
          for( const auto& obj : objs )
          {
             const limit_order_object* order = dynamic_cast<const limit_order_object*>(obj);
             if( order )
             {
                auto sub = _market_subscriptions.find( order->get_market() );
                if( sub != _market_subscriptions.end() )
                   broadcast_queue[order->get_market()].emplace_back( order->id );
             }
          }
          if( broadcast_queue.size() )
          {
             _broadcast_removed_complete = fc::async([=](){
                 for( const auto& item : broadcast_queue )
                 {
                   auto sub = _market_subscriptions.find(item.first);
                   if( sub != _market_subscriptions.end() )
                       sub->second( fc::variant(item.second ) );
                 }
             });
          }
       }
    }

    void database_api::on_objects_changed(const vector<object_id_type>& ids)
    {
       idump((ids)(_account_subscriptions.size()));
       vector<object_id_type>                       my_objects;
       map<account_id_type, vector<variant> >       broadcast_queue; 
       map< pair<asset_id_type, asset_id_type>,  vector<variant> > market_broadcast_queue;
       for(auto id : ids)
       {
          if(_subscriptions.find(id) != _subscriptions.end())
             my_objects.push_back(id);

          const object* obj = nullptr;
          if( _account_subscriptions.size() )
          {
             obj = _db.find_object( id );
             if( obj )
             {
                vector<account_id_type> relevant = get_relevant_accounts( obj );
                for( const auto& r : relevant )
                {
                   auto sub = _account_subscriptions.find(r);
                   if( sub != _account_subscriptions.end() )
                      broadcast_queue[r].emplace_back(obj->to_variant());
                }
             }
          }

          if( _market_subscriptions.size() )
          {
             if( !_account_subscriptions.size() ) obj = _db.find_object( id );
             if( obj )
             {
                const limit_order_object* order = dynamic_cast<const limit_order_object*>(obj);
                if( order )
                {
                   auto sub = _market_subscriptions.find( order->get_market() );
                   if( sub != _market_subscriptions.end() )
                      market_broadcast_queue[order->get_market()].emplace_back( order->id );
                }
             }
          }
       }


       /// TODO: consider making _broadcast_changes_complete a deque and
       /// pushing the future back / popping the prior future if it is complete.
       /// if a connection hangs then this could get backed up and result in
       /// a failure to exit cleanly.
       _broadcast_changes_complete = fc::async([=](){
          for( const auto& item : broadcast_queue )
          {
            edump( (item) );
            auto sub = _account_subscriptions.find(item.first);
            if( sub != _account_subscriptions.end() )
                sub->second( fc::variant(item.second ) );
          }
          for( const auto& item : market_broadcast_queue )
          {
            auto sub = _market_subscriptions.find(item.first);
            if( sub != _market_subscriptions.end() )
                sub->second( fc::variant(item.second ) );
          }
          for(auto id : my_objects)
          {
             // just incase _usbscriptions changed between filter and broadcast
             auto itr = _subscriptions.find( id );
             if( itr != _subscriptions.end() )
             {
                const object* obj = _db.find_object( id );
                if( obj != nullptr )
                {
                   itr->second(obj->to_variant());
                }
                else
                {
                   itr->second(fc::variant(id));
                }
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
             /*  This is sent via the object_changed callback
             case operation::tag<limit_order_create_operation>::value:
                market = op.op.get<limit_order_create_operation>().get_market();
                break;
             */
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


    bool database_api::subscribe_to_objects( const std::function<void(const fc::variant&)>&  callback, const vector<object_id_type>& ids)
    {
       FC_ASSERT( _subscriptions.size() < 1024 );
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
    vector<account_id_type> database_api::get_account_references( account_id_type account_id )const
    {
       const auto& idx = _db.get_index_type<account_index>();
       const auto& aidx = dynamic_cast<const primary_index<account_index>&>(idx);
       const auto& refs = aidx.get_secondary_index<graphene::chain::account_member_index>();
       auto itr = refs.account_to_account_memberships.find(account_id);
       vector<account_id_type> result;

       if( itr != refs.account_to_account_memberships.end() )
       {
          result.reserve( itr->second.size() );
          for( auto item : itr->second ) result.push_back(item);
       }
       return result;
    }
    /**
     *  @return all accounts that referr to the key or account id in their owner or active authorities.
     */
    vector<vector<account_id_type>> database_api::get_key_references( vector<public_key_type> keys )const
    {
       vector< vector<account_id_type> > final_result;
       final_result.reserve(keys.size());

       for( auto& key : keys )
       {
          const auto& idx = _db.get_index_type<account_index>();
          const auto& aidx = dynamic_cast<const primary_index<account_index>&>(idx);
          const auto& refs = aidx.get_secondary_index<graphene::chain::account_member_index>();
          auto itr = refs.account_to_key_memberships.find(key);
          vector<account_id_type> result;

          if( itr != refs.account_to_key_memberships.end() )
          {
             result.reserve( itr->second.size() );
             for( auto item : itr->second ) result.push_back(item);
          }
          final_result.emplace_back( std::move(result) );
       }
       return final_result;
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


    vector<asset>           database_api::get_vested_balances( const vector<balance_id_type>& objs )const
    { try {
        vector<asset> result;
        result.reserve( objs.size() );
        auto now = _db.head_block_time();
        for( auto obj : objs )
           result.push_back( obj(_db).available( now ) );
        return result;
    } FC_CAPTURE_AND_RETHROW( (objs) ) }

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

    set<public_key_type> database_api::get_required_signatures( const signed_transaction& trx, const flat_set<public_key_type>& available_keys )const
    {
       return trx.get_required_signatures( available_keys,
                                           [&]( account_id_type id ){ return &id(_db).active; },
                                           [&]( account_id_type id ){ return &id(_db).owner; },
                                           _db.get_global_properties().parameters.max_authority_depth );
    }

    bool    database_api::verify_authority( const signed_transaction& trx )const
    {
       trx.verify_authority( [&]( account_id_type id ){ return &id(_db).active; },
                             [&]( account_id_type id ){ return &id(_db).owner; },
                              _db.get_global_properties().parameters.max_authority_depth );
       return true;
    }
    vector<blinded_balance_object> database_api::get_blinded_balances( const flat_set<commitment_type>& commitments )const
    {
       vector<blinded_balance_object> result; result.reserve(commitments.size());
       const auto& bal_idx = _db.get_index_type<blinded_balance_index>();
       const auto& by_commitment_idx = bal_idx.indices().get<by_commitment>();
       for( const auto& c : commitments )
       {
          auto itr = by_commitment_idx.find( c );
          if( itr != by_commitment_idx.end() )
             result.push_back( *itr );
       }
       return result;
    }

    vector<asset> database_api::get_required_fees( const vector<operation>& ops, asset_id_type id )const
    {
       vector<asset> result;
       result.reserve(ops.size());
       const asset_object&  a = id(_db);
       for( const auto& op : ops )
          result.push_back( _db.current_fee_schedule().calculate_fee( op, a.options.core_exchange_rate ) );
       return result;
    }

} } // graphene::app
