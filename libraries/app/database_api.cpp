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

#include "database_api_impl.hxx"

#include <graphene/app/util.hpp>
#include <graphene/chain/get_config.hpp>
#include <graphene/chain/hardfork.hpp>
#include <graphene/protocol/pts_address.hpp>

#include <fc/crypto/hex.hpp>
#include <fc/rpc/api_connection.hpp>

#include <boost/range/iterator_range.hpp>

#include <cctype>

template class fc::api<graphene::app::database_api>;

namespace graphene { namespace app {

//////////////////////////////////////////////////////////////////////
//                                                                  //
// Constructors                                                     //
//                                                                  //
//////////////////////////////////////////////////////////////////////

database_api::database_api( graphene::chain::database& db, const application_options* app_options )
   : my( new database_api_impl( db, app_options ) ) {}

database_api::~database_api() {}

database_api_impl::database_api_impl( graphene::chain::database& db, const application_options* app_options )
:_db(db), _app_options(app_options)
{
   dlog("creating database api ${x}", ("x",int64_t(this)) );
   _new_connection = _db.new_objects.connect([this](const vector<object_id_type>& ids,
                                                    const flat_set<account_id_type>& impacted_accounts) {
                                on_objects_new(ids, impacted_accounts);
                                });
   _change_connection = _db.changed_objects.connect([this](const vector<object_id_type>& ids,
                                                           const flat_set<account_id_type>& impacted_accounts) {
                                on_objects_changed(ids, impacted_accounts);
                                });
   _removed_connection = _db.removed_objects.connect([this](const vector<object_id_type>& ids,
                                                            const vector<const object*>& objs,
                                                            const flat_set<account_id_type>& impacted_accounts) {
                                on_objects_removed(ids, objs, impacted_accounts);
                                });
   _applied_block_connection = _db.applied_block.connect([this](const signed_block&){ on_applied_block(); });

   _pending_trx_connection = _db.on_pending_transaction.connect([this](const signed_transaction& trx ){
                                if( _pending_trx_callback )
                                   _pending_trx_callback( fc::variant(trx, GRAPHENE_MAX_NESTED_OBJECTS) );
                      });
   try
   {
      amount_in_collateral_index = &_db.get_index_type< primary_index< call_order_index > >()
                                    .get_secondary_index<graphene::api_helper_indexes::amount_in_collateral_index>();
   }
   catch( fc::assert_exception& e )
   {
      amount_in_collateral_index = nullptr;
   }
}

database_api_impl::~database_api_impl()
{
   dlog("freeing database api ${x}", ("x",int64_t(this)) );
}

//////////////////////////////////////////////////////////////////////
//                                                                  //
// Objects                                                          //
//                                                                  //
//////////////////////////////////////////////////////////////////////

fc::variants database_api::get_objects( const vector<object_id_type>& ids, optional<bool> subscribe )const
{
   return my->get_objects( ids, subscribe );
}

fc::variants database_api_impl::get_objects( const vector<object_id_type>& ids, optional<bool> subscribe )const
{
   bool to_subscribe = get_whether_to_subscribe( subscribe );

   fc::variants result;
   result.reserve(ids.size());

   std::transform(ids.begin(), ids.end(), std::back_inserter(result),
                  [this,to_subscribe](object_id_type id) -> fc::variant {
      if(auto obj = _db.find_object(id))
      {
         if( to_subscribe && !id.is<operation_history_id_type>() && !id.is<account_transaction_history_id_type>() )
            this->subscribe_to_item( id );
         return obj->to_variant();
      }
      return {};
   });

   return result;
}

//////////////////////////////////////////////////////////////////////
//                                                                  //
// Subscriptions                                                    //
//                                                                  //
//////////////////////////////////////////////////////////////////////

void database_api::set_subscribe_callback( std::function<void(const variant&)> cb, bool notify_remove_create )
{
   my->set_subscribe_callback( cb, notify_remove_create );
}

void database_api_impl::set_subscribe_callback( std::function<void(const variant&)> cb, bool notify_remove_create )
{
   if( notify_remove_create )
   {
      FC_ASSERT( _app_options && _app_options->enable_subscribe_to_all,
                 "Subscribing to universal object creation and removal is disallowed in this server." );
   }

   cancel_all_subscriptions(false, false);

   _subscribe_callback = cb;
   _notify_remove_create = notify_remove_create;
}

void database_api::set_auto_subscription( bool enable )
{
   my->set_auto_subscription( enable );
}

void database_api_impl::set_auto_subscription( bool enable )
{
   _enabled_auto_subscription = enable;
}

void database_api::set_pending_transaction_callback( std::function<void(const variant&)> cb )
{
   my->set_pending_transaction_callback( cb );
}

void database_api_impl::set_pending_transaction_callback( std::function<void(const variant&)> cb )
{
   _pending_trx_callback = cb;
}

void database_api::set_block_applied_callback( std::function<void(const variant& block_id)> cb )
{
   my->set_block_applied_callback( cb );
}

void database_api_impl::set_block_applied_callback( std::function<void(const variant& block_id)> cb )
{
   _block_applied_callback = cb;
}

void database_api::cancel_all_subscriptions()
{
   my->cancel_all_subscriptions(true, true);
}

void database_api_impl::cancel_all_subscriptions( bool reset_callback, bool reset_market_subscriptions )
{
   if ( reset_callback )
      _subscribe_callback = std::function<void(const fc::variant&)>();

   if ( reset_market_subscriptions )
      _market_subscriptions.clear();

   _notify_remove_create = false;
   _subscribed_accounts.clear();
   static fc::bloom_parameters param(10000, 1.0/100, 1024*8*8*2);
   _subscribe_filter = fc::bloom_filter(param);
}

//////////////////////////////////////////////////////////////////////
//                                                                  //
// Blocks and transactions                                          //
//                                                                  //
//////////////////////////////////////////////////////////////////////

optional<block_header> database_api::get_block_header(uint32_t block_num)const
{
   return my->get_block_header( block_num );
}

optional<block_header> database_api_impl::get_block_header(uint32_t block_num) const
{
   auto result = _db.fetch_block_by_number(block_num);
   if(result)
      return *result;
   return {};
}
map<uint32_t, optional<block_header>> database_api::get_block_header_batch(const vector<uint32_t> block_nums)const
{
   return my->get_block_header_batch( block_nums );
}

map<uint32_t, optional<block_header>> database_api_impl::get_block_header_batch(
                                         const vector<uint32_t> block_nums) const
{
   map<uint32_t, optional<block_header>> results;
   for (const uint32_t block_num : block_nums)
   {
      results[block_num] = get_block_header(block_num);
   }
   return results;
}

optional<signed_block> database_api::get_block(uint32_t block_num)const
{
   return my->get_block( block_num );
}

optional<signed_block> database_api_impl::get_block(uint32_t block_num)const
{
   return _db.fetch_block_by_number(block_num);
}

processed_transaction database_api::get_transaction( uint32_t block_num, uint32_t trx_in_block )const
{
   return my->get_transaction( block_num, trx_in_block );
}

optional<signed_transaction> database_api::get_recent_transaction_by_id( const transaction_id_type& id )const
{
   try {
      return my->_db.get_recent_transaction( id );
   } catch ( ... ) {
      return optional<signed_transaction>();
   }
}

processed_transaction database_api_impl::get_transaction(uint32_t block_num, uint32_t trx_num)const
{
   auto opt_block = _db.fetch_block_by_number(block_num);
   FC_ASSERT( opt_block );
   FC_ASSERT( opt_block->transactions.size() > trx_num );
   return opt_block->transactions[trx_num];
}

//////////////////////////////////////////////////////////////////////
//                                                                  //
// Globals                                                          //
//                                                                  //
//////////////////////////////////////////////////////////////////////

chain_property_object database_api::get_chain_properties()const
{
   return my->get_chain_properties();
}

chain_property_object database_api_impl::get_chain_properties()const
{
   return _db.get(chain_property_id_type());
}

global_property_object database_api::get_global_properties()const
{
   return my->get_global_properties();
}

global_property_object database_api_impl::get_global_properties()const
{
   return _db.get(global_property_id_type());
}

fc::variant_object database_api::get_config()const
{
   return my->get_config();
}

fc::variant_object database_api_impl::get_config()const
{
   return graphene::chain::get_config();
}

chain_id_type database_api::get_chain_id()const
{
   return my->get_chain_id();
}

chain_id_type database_api_impl::get_chain_id()const
{
   return _db.get_chain_id();
}

dynamic_global_property_object database_api::get_dynamic_global_properties()const
{
   return my->get_dynamic_global_properties();
}

dynamic_global_property_object database_api_impl::get_dynamic_global_properties()const
{
   return _db.get(dynamic_global_property_id_type());
}

//////////////////////////////////////////////////////////////////////
//                                                                  //
// Keys                                                             //
//                                                                  //
//////////////////////////////////////////////////////////////////////

vector<flat_set<account_id_type>> database_api::get_key_references( vector<public_key_type> key )const
{
   return my->get_key_references( key );
}

/**
 *  @return all accounts that referr to the key or account id in their owner or active authorities.
 */
vector<flat_set<account_id_type>> database_api_impl::get_key_references( vector<public_key_type> keys )const
{
   uint64_t api_limit_get_key_references=_app_options->api_limit_get_key_references;
   FC_ASSERT(keys.size() <= api_limit_get_key_references);
   const auto& idx = _db.get_index_type<account_index>();
   const auto& aidx = dynamic_cast<const base_primary_index&>(idx);
   const auto& refs = aidx.get_secondary_index<graphene::chain::account_member_index>();

   vector< flat_set<account_id_type> > final_result;
   final_result.reserve(keys.size());

   for( auto& key : keys )
   {
      address a1( pts_address(key, false, 56) );
      address a2( pts_address(key, true, 56) );
      address a3( pts_address(key, false, 0)  );
      address a4( pts_address(key, true, 0)  );
      address a5( key );

      flat_set<account_id_type> result;

      for( auto& a : {a1,a2,a3,a4,a5} )
      {
          auto itr = refs.account_to_address_memberships.find(a);
          if( itr != refs.account_to_address_memberships.end() )
          {
             result.reserve( result.size() + itr->second.size() );
             for( auto item : itr->second )
             {
                result.insert(item);
             }
          }
      }

      auto itr = refs.account_to_key_memberships.find(key);
      if( itr != refs.account_to_key_memberships.end() )
      {
         result.reserve( result.size() + itr->second.size() );
         for( auto item : itr->second ) result.insert(item);
      }
      final_result.emplace_back( std::move(result) );
   }

   return final_result;
}

bool database_api::is_public_key_registered(string public_key) const
{
    return my->is_public_key_registered(public_key);
}

bool database_api_impl::is_public_key_registered(string public_key) const
{
    // Short-circuit
    if (public_key.empty()) {
        return false;
    }

    // Search among all keys using an existing map of *current* account keys
    public_key_type key;
    try {
        key = public_key_type(public_key);
    } catch ( ... ) {
        // An invalid public key was detected
        return false;
    }
    const auto& idx = _db.get_index_type<account_index>();
    const auto& aidx = dynamic_cast<const base_primary_index&>(idx);
    const auto& refs = aidx.get_secondary_index<graphene::chain::account_member_index>();
    auto itr = refs.account_to_key_memberships.find(key);
    bool is_known = itr != refs.account_to_key_memberships.end();

    return is_known;
}

//////////////////////////////////////////////////////////////////////
//                                                                  //
// Accounts                                                         //
//                                                                  //
//////////////////////////////////////////////////////////////////////

account_id_type database_api::get_account_id_from_string(const std::string& name_or_id)const
{
   return my->get_account_from_string( name_or_id )->id;
}

vector<optional<account_object>> database_api::get_accounts( const vector<std::string>& account_names_or_ids,
                                                             optional<bool> subscribe )const
{
   return my->get_accounts( account_names_or_ids, subscribe );
}

vector<optional<account_object>> database_api_impl::get_accounts( const vector<std::string>& account_names_or_ids,
                                                                  optional<bool> subscribe )const
{
   bool to_subscribe = get_whether_to_subscribe( subscribe );
   vector<optional<account_object>> result; result.reserve(account_names_or_ids.size());
   std::transform(account_names_or_ids.begin(), account_names_or_ids.end(), std::back_inserter(result),
                  [this,to_subscribe](std::string id_or_name) -> optional<account_object> {

      const account_object *account = get_account_from_string(id_or_name, false);
      if(account == nullptr)
         return {};
      if( to_subscribe )
         subscribe_to_item( account->id );
      return *account;
   });
   return result;
}

std::map<string,full_account> database_api::get_full_accounts( const vector<string>& names_or_ids,
                                                               optional<bool> subscribe )
{
   return my->get_full_accounts( names_or_ids, subscribe );
}

std::map<std::string, full_account> database_api_impl::get_full_accounts( const vector<std::string>& names_or_ids,
                                                                          optional<bool> subscribe )
{
   FC_ASSERT( names_or_ids.size() <= _app_options->api_limit_get_full_accounts );

   const auto& proposal_idx = _db.get_index_type< primary_index< proposal_index > >();
   const auto& proposals_by_account = proposal_idx.get_secondary_index<graphene::chain::required_approval_index>();

   bool to_subscribe = get_whether_to_subscribe( subscribe );

   std::map<std::string, full_account> results;

   for (const std::string& account_name_or_id : names_or_ids)
   {
      const account_object* account = get_account_from_string(account_name_or_id, false);
      if (account == nullptr)
         continue;

      if( to_subscribe )
      {
         if(_subscribed_accounts.size() < 100) {
            _subscribed_accounts.insert( account->get_id() );
            subscribe_to_item( account->id );
         }
      }

      full_account acnt;
      acnt.account = *account;
      acnt.statistics = account->statistics(_db);
      acnt.registrar_name = account->registrar(_db).name;
      acnt.referrer_name = account->referrer(_db).name;
      acnt.lifetime_referrer_name = account->lifetime_referrer(_db).name;
      acnt.votes = lookup_vote_ids( vector<vote_id_type>( account->options.votes.begin(),
                                                          account->options.votes.end() ) );

      if (account->cashback_vb)
      {
         acnt.cashback_balance = account->cashback_balance(_db);
      }

      size_t api_limit_get_full_accounts_lists = static_cast<size_t>(
                _app_options->api_limit_get_full_accounts_lists );

      // Add the account's proposals
      auto required_approvals_itr = proposals_by_account._account_to_proposals.find( account->id );
      if( required_approvals_itr != proposals_by_account._account_to_proposals.end() )
      {
         acnt.proposals.reserve( std::min(required_approvals_itr->second.size(),
                                          api_limit_get_full_accounts_lists) );
         for( auto proposal_id : required_approvals_itr->second )
         {
            if(acnt.proposals.size() >= api_limit_get_full_accounts_lists) {
               acnt.more_data_available.proposals = true;
               break;
            }
            acnt.proposals.push_back(proposal_id(_db));
         }
      }

      // Add the account's balances
      const auto& balances = _db.get_index_type< primary_index< account_balance_index > >().
            get_secondary_index< balances_by_account_index >().get_account_balances( account->id );
      for( const auto balance : balances )
      {
         if(acnt.balances.size() >= api_limit_get_full_accounts_lists) {
            acnt.more_data_available.balances = true;
            break;
         }
         acnt.balances.emplace_back(*balance.second);
      }

      // Add the account's vesting balances
      auto vesting_range = _db.get_index_type<vesting_balance_index>().indices().get<by_account>()
                              .equal_range(account->id);
      for(auto itr = vesting_range.first; itr != vesting_range.second; ++itr)
      {
         if(acnt.vesting_balances.size() >= api_limit_get_full_accounts_lists) {
            acnt.more_data_available.vesting_balances = true;
            break;
         }
         acnt.vesting_balances.emplace_back(*itr);
      }

      // Add the account's orders
      auto order_range = _db.get_index_type<limit_order_index>().indices().get<by_account>()
                            .equal_range(account->id);
      for(auto itr = order_range.first; itr != order_range.second; ++itr)
      {
         if(acnt.limit_orders.size() >= api_limit_get_full_accounts_lists) {
            acnt.more_data_available.limit_orders = true;
            break;
         }
         acnt.limit_orders.emplace_back(*itr);
      }
      auto call_range = _db.get_index_type<call_order_index>().indices().get<by_account>().equal_range(account->id);
      for(auto itr = call_range.first; itr != call_range.second; ++itr)
      {
         if(acnt.call_orders.size() >= api_limit_get_full_accounts_lists) {
            acnt.more_data_available.call_orders = true;
            break;
         }
         acnt.call_orders.emplace_back(*itr);
      }
      auto settle_range = _db.get_index_type<force_settlement_index>().indices().get<by_account>()
                             .equal_range(account->id);
      for(auto itr = settle_range.first; itr != settle_range.second; ++itr)
      {
         if(acnt.settle_orders.size() >= api_limit_get_full_accounts_lists) {
            acnt.more_data_available.settle_orders = true;
            break;
         }
         acnt.settle_orders.emplace_back(*itr);
      }

      // get assets issued by user
      auto asset_range = _db.get_index_type<asset_index>().indices().get<by_issuer>().equal_range(account->id);
      for(auto itr = asset_range.first; itr != asset_range.second; ++itr)
      {
         if(acnt.assets.size() >= api_limit_get_full_accounts_lists) {
            acnt.more_data_available.assets = true;
            break;
         }
         acnt.assets.emplace_back(itr->id);
      }

      // get withdraws permissions
      auto withdraw_indices = _db.get_index_type<withdraw_permission_index>().indices();
      auto withdraw_from_range = withdraw_indices.get<by_from>().equal_range(account->id);
      for(auto itr = withdraw_from_range.first; itr != withdraw_from_range.second; ++itr)
      {
         if(acnt.withdraws_from.size() >= api_limit_get_full_accounts_lists) {
            acnt.more_data_available.withdraws_from = true;
            break;
         }
         acnt.withdraws_from.emplace_back(*itr);
      }
      auto withdraw_authorized_range = withdraw_indices.get<by_authorized>().equal_range(account->id);
      for(auto itr = withdraw_authorized_range.first; itr != withdraw_authorized_range.second; ++itr)
      {
         if(acnt.withdraws_to.size() >= api_limit_get_full_accounts_lists) {
            acnt.more_data_available.withdraws_to = true;
            break;
         }
         acnt.withdraws_to.emplace_back(*itr);
      }

      // get htlcs
      auto htlc_from_range = _db.get_index_type<htlc_index>().indices().get<by_from_id>().equal_range(account->id);
      for(auto itr = htlc_from_range.first; itr != htlc_from_range.second; ++itr)
      {
         if(acnt.htlcs_from.size() >= api_limit_get_full_accounts_lists) {
            acnt.more_data_available.htlcs_from = true;
            break;
         }
         acnt.htlcs_from.emplace_back(*itr);
      }
      auto htlc_to_range = _db.get_index_type<htlc_index>().indices().get<by_to_id>().equal_range(account->id);
      for(auto itr = htlc_to_range.first; itr != htlc_to_range.second; ++itr)
      {
         if(acnt.htlcs_to.size() >= api_limit_get_full_accounts_lists) {
            acnt.more_data_available.htlcs_to = true;
            break;
         }
         acnt.htlcs_to.emplace_back(*itr);
      }

      results[account_name_or_id] = acnt;
   }
   return results;
}

optional<account_object> database_api::get_account_by_name( string name )const
{
   return my->get_account_by_name( name );
}

optional<account_object> database_api_impl::get_account_by_name( string name )const
{
   const auto& idx = _db.get_index_type<account_index>().indices().get<by_name>();
   auto itr = idx.find(name);
   if (itr != idx.end())
      return *itr;
   return optional<account_object>();
}

vector<account_id_type> database_api::get_account_references( const std::string account_id_or_name )const
{
   return my->get_account_references( account_id_or_name );
}

vector<account_id_type> database_api_impl::get_account_references( const std::string account_id_or_name )const
{
   const auto& idx = _db.get_index_type<account_index>();
   const auto& aidx = dynamic_cast<const base_primary_index&>(idx);
   const auto& refs = aidx.get_secondary_index<graphene::chain::account_member_index>();
   const account_id_type account_id = get_account_from_string(account_id_or_name)->id;
   auto itr = refs.account_to_account_memberships.find(account_id);
   vector<account_id_type> result;

   if( itr != refs.account_to_account_memberships.end() )
   {
      result.reserve( itr->second.size() );
      for( auto item : itr->second ) result.push_back(item);
   }
   return result;
}

vector<optional<account_object>> database_api::lookup_account_names(const vector<string>& account_names)const
{
   return my->lookup_account_names( account_names );
}

vector<optional<account_object>> database_api_impl::lookup_account_names(const vector<string>& account_names)const
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

map<string,account_id_type> database_api::lookup_accounts( const string& lower_bound_name,
                                                           uint32_t limit,
                                                           optional<bool> subscribe )const
{
   return my->lookup_accounts( lower_bound_name, limit, subscribe );
}

map<string,account_id_type> database_api_impl::lookup_accounts( const string& lower_bound_name,
                                                                uint32_t limit,
                                                                optional<bool> subscribe )const
{
   FC_ASSERT( limit <= 1000 );
   const auto& accounts_by_name = _db.get_index_type<account_index>().indices().get<by_name>();
   map<string,account_id_type> result;

   if( limit == 0 ) // shortcut to save a database query
      return result;
   // In addition to the common auto-subscription rules, here we auto-subscribe if only look for one account
   bool to_subscribe = (limit == 1 && get_whether_to_subscribe( subscribe ));
   for( auto itr = accounts_by_name.lower_bound(lower_bound_name);
        limit-- && itr != accounts_by_name.end();
        ++itr )
   {
      result.insert(make_pair(itr->name, itr->get_id()));
      if( to_subscribe )
         subscribe_to_item( itr->id );
   }

   return result;
}

uint64_t database_api::get_account_count()const
{
   return my->get_account_count();
}

uint64_t database_api_impl::get_account_count()const
{
   return _db.get_index_type<account_index>().indices().size();
}

//////////////////////////////////////////////////////////////////////
//                                                                  //
// Balances                                                         //
//                                                                  //
//////////////////////////////////////////////////////////////////////

vector<asset> database_api::get_account_balances( const std::string& account_name_or_id,
                                                  const flat_set<asset_id_type>& assets )const
{
   return my->get_account_balances( account_name_or_id, assets );
}

vector<asset> database_api_impl::get_account_balances( const std::string& account_name_or_id,
                                                       const flat_set<asset_id_type>& assets )const
{
   const account_object* account = get_account_from_string(account_name_or_id);
   account_id_type acnt = account->id;
   vector<asset> result;
   if (assets.empty())
   {
      // if the caller passes in an empty list of assets, return balances for all assets the account owns
      const auto& balance_index = _db.get_index_type< primary_index< account_balance_index > >();
      const auto& balances = balance_index.get_secondary_index< balances_by_account_index >()
                                          .get_account_balances( acnt );
      for( const auto balance : balances )
         result.push_back( balance.second->get_balance() );
   }
   else
   {
      result.reserve(assets.size());

      std::transform(assets.begin(), assets.end(), std::back_inserter(result),
                     [this, acnt](asset_id_type id) { return _db.get_balance(acnt, id); });
   }

   return result;
}

vector<asset> database_api::get_named_account_balances( const std::string& name,
                                                        const flat_set<asset_id_type>& assets )const
{
   return my->get_account_balances( name, assets );
}

vector<balance_object> database_api::get_balance_objects( const vector<address>& addrs )const
{
   return my->get_balance_objects( addrs );
}

vector<balance_object> database_api_impl::get_balance_objects( const vector<address>& addrs )const
{
   try
   {
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
   }
   FC_CAPTURE_AND_RETHROW( (addrs) )
}

vector<asset> database_api::get_vested_balances( const vector<balance_id_type>& objs )const
{
   return my->get_vested_balances( objs );
}

vector<asset> database_api_impl::get_vested_balances( const vector<balance_id_type>& objs )const
{
   try
   {
      vector<asset> result;
      result.reserve( objs.size() );
      auto now = _db.head_block_time();
      for( auto obj : objs )
         result.push_back( obj(_db).available( now ) );
      return result;
   } FC_CAPTURE_AND_RETHROW( (objs) )
}

vector<vesting_balance_object> database_api::get_vesting_balances( const std::string account_id_or_name )const
{
   return my->get_vesting_balances( account_id_or_name );
}

vector<vesting_balance_object> database_api_impl::get_vesting_balances( const std::string account_id_or_name )const
{
   try
   {
      const account_id_type account_id = get_account_from_string(account_id_or_name)->id;
      vector<vesting_balance_object> result;
      auto vesting_range = _db.get_index_type<vesting_balance_index>().indices().get<by_account>()
                              .equal_range(account_id);
      std::for_each(vesting_range.first, vesting_range.second,
                    [&result](const vesting_balance_object& balance) {
                       result.emplace_back(balance);
                    });
      return result;
   }
   FC_CAPTURE_AND_RETHROW( (account_id_or_name) );
}

//////////////////////////////////////////////////////////////////////
//                                                                  //
// Assets                                                           //
//                                                                  //
//////////////////////////////////////////////////////////////////////

asset_id_type database_api::get_asset_id_from_string(const std::string& symbol_or_id)const
{
   return my->get_asset_from_string( symbol_or_id )->id;
}

vector<optional<extended_asset_object>> database_api::get_assets(
      const vector<std::string>& asset_symbols_or_ids,
      optional<bool> subscribe )const
{
   return my->get_assets( asset_symbols_or_ids, subscribe );
}

vector<optional<extended_asset_object>> database_api_impl::get_assets(
      const vector<std::string>& asset_symbols_or_ids,
      optional<bool> subscribe )const
{
   bool to_subscribe = get_whether_to_subscribe( subscribe );
   vector<optional<extended_asset_object>> result; result.reserve(asset_symbols_or_ids.size());
   std::transform(asset_symbols_or_ids.begin(), asset_symbols_or_ids.end(), std::back_inserter(result),
                  [this,to_subscribe](std::string id_or_name) -> optional<extended_asset_object> {

      const asset_object* asset_obj = get_asset_from_string( id_or_name, false );
      if( asset_obj == nullptr )
         return {};
      if( to_subscribe )
         subscribe_to_item( asset_obj->id );
      return extend_asset( *asset_obj );
   });
   return result;
}

vector<extended_asset_object> database_api::list_assets(const string& lower_bound_symbol, uint32_t limit)const
{
   return my->list_assets( lower_bound_symbol, limit );
}

vector<extended_asset_object> database_api_impl::list_assets(const string& lower_bound_symbol, uint32_t limit)const
{
   uint64_t api_limit_get_assets = _app_options->api_limit_get_assets;
   FC_ASSERT( limit <= api_limit_get_assets );

   const auto& assets_by_symbol = _db.get_index_type<asset_index>().indices().get<by_symbol>();
   vector<extended_asset_object> result;
   result.reserve(limit);

   auto itr = assets_by_symbol.lower_bound(lower_bound_symbol);

   if( lower_bound_symbol == "" )
      itr = assets_by_symbol.begin();

   while(limit-- && itr != assets_by_symbol.end())
      result.emplace_back( extend_asset( *itr++ ) );

   return result;
}

uint64_t database_api::get_asset_count()const
{
   return my->get_asset_count();
}

uint64_t database_api_impl::get_asset_count()const
{
   return _db.get_index_type<asset_index>().indices().size();
}

vector<extended_asset_object> database_api::get_assets_by_issuer(const std::string& issuer_name_or_id,
                                                                 asset_id_type start, uint32_t limit)const
{
   return my->get_assets_by_issuer(issuer_name_or_id, start, limit);
}

vector<extended_asset_object> database_api_impl::get_assets_by_issuer(const std::string& issuer_name_or_id,
                                                                      asset_id_type start, uint32_t limit)const
{
   uint64_t api_limit_get_assets = _app_options->api_limit_get_assets;
   FC_ASSERT( limit <= api_limit_get_assets );

   vector<extended_asset_object> result;
   const account_id_type account = get_account_from_string(issuer_name_or_id)->id;
   const auto& asset_idx = _db.get_index_type<asset_index>().indices().get<by_issuer>();
   auto asset_index_end = asset_idx.end();
   auto asset_itr = asset_idx.lower_bound(boost::make_tuple(account, start));
   while(asset_itr != asset_index_end && asset_itr->issuer == account && result.size() < limit)
   {
      result.emplace_back( extend_asset( *asset_itr ) );
      ++asset_itr;
   }
   return result;
}

vector<optional<extended_asset_object>> database_api::lookup_asset_symbols(
                                                         const vector<string>& symbols_or_ids )const
{
   return my->lookup_asset_symbols( symbols_or_ids );
}

vector<optional<extended_asset_object>> database_api_impl::lookup_asset_symbols(
                                                         const vector<string>& symbols_or_ids )const
{
   const auto& assets_by_symbol = _db.get_index_type<asset_index>().indices().get<by_symbol>();
   vector<optional<extended_asset_object> > result;
   result.reserve(symbols_or_ids.size());
   std::transform(symbols_or_ids.begin(), symbols_or_ids.end(), std::back_inserter(result),
                  [this, &assets_by_symbol](const string& symbol_or_id) -> optional<extended_asset_object> {
      if( !symbol_or_id.empty() && std::isdigit(symbol_or_id[0]) )
      {
         auto ptr = _db.find(variant(symbol_or_id, 1).as<asset_id_type>(1));
         return ptr == nullptr? optional<extended_asset_object>() : extend_asset( *ptr );
      }
      auto itr = assets_by_symbol.find(symbol_or_id);
      return itr == assets_by_symbol.end()? optional<extended_asset_object>() : extend_asset( *itr );
   });
   return result;
}

//////////////////////////////////////////////////////////////////////
//                                                                  //
// Markets / feeds                                                  //
//                                                                  //
//////////////////////////////////////////////////////////////////////

vector<limit_order_object> database_api::get_limit_orders(std::string a, std::string b, uint32_t limit)const
{
   return my->get_limit_orders( a, b, limit );
}

vector<limit_order_object> database_api_impl::get_limit_orders( const std::string& a, const std::string& b,
                                                                uint32_t limit )const
{
   uint64_t api_limit_get_limit_orders=_app_options->api_limit_get_limit_orders;
   FC_ASSERT( limit <= api_limit_get_limit_orders );

   const asset_id_type asset_a_id = get_asset_from_string(a)->id;
   const asset_id_type asset_b_id = get_asset_from_string(b)->id;

   return get_limit_orders(asset_a_id, asset_b_id, limit);
}

vector<limit_order_object> database_api::get_account_limit_orders(
                              const string& account_name_or_id, const string &base, const string &quote,
                              uint32_t limit, optional<limit_order_id_type> ostart_id, optional<price> ostart_price )
{
   return my->get_account_limit_orders( account_name_or_id, base, quote, limit, ostart_id, ostart_price );
}

vector<limit_order_object> database_api_impl::get_account_limit_orders(
                              const string& account_name_or_id, const string &base, const string &quote,
                              uint32_t limit, optional<limit_order_id_type> ostart_id, optional<price> ostart_price )
{
   FC_ASSERT( limit <= 101 );

   vector<limit_order_object>   results;
   uint32_t                     count = 0;

   const account_object* account = get_account_from_string(account_name_or_id);
   if (account == nullptr)
      return results;

   auto assets = lookup_asset_symbols( {base, quote} );
   FC_ASSERT( assets[0], "Invalid base asset symbol: ${s}", ("s",base) );
   FC_ASSERT( assets[1], "Invalid quote asset symbol: ${s}", ("s",quote) );

   auto base_id = assets[0]->id;
   auto quote_id = assets[1]->id;

   if (ostart_price.valid()) {
      FC_ASSERT(ostart_price->base.asset_id == base_id, "Base asset inconsistent with start price");
      FC_ASSERT(ostart_price->quote.asset_id == quote_id, "Quote asset inconsistent with start price");
   }

   const auto& index_by_account = _db.get_index_type<limit_order_index>().indices().get<by_account>();
   limit_order_multi_index_type::index<by_account>::type::const_iterator lower_itr;
   limit_order_multi_index_type::index<by_account>::type::const_iterator upper_itr;

   // if both order_id and price are invalid, query the first page
   if ( !ostart_id.valid() && !ostart_price.valid() )
   {
      lower_itr = index_by_account.lower_bound(std::make_tuple(account->id, price::max(base_id, quote_id)));
   }
   else if ( ostart_id.valid() )
   {
      // in case of the order been deleted during page querying
      const limit_order_object *p_loo = _db.find(*ostart_id);

      if ( !p_loo )
      {
         if ( ostart_price.valid() )
         {
            lower_itr = index_by_account.lower_bound(std::make_tuple(account->id, *ostart_price, *ostart_id));
         }
         else
         {
            // start order id been deleted, yet not provided price either
            FC_THROW("Order id invalid (maybe just been canceled?), and start price not provided");
         }
      }
      else
      {
         const limit_order_object &loo = *p_loo;

         // in case of the order not belongs to specified account or market
         FC_ASSERT(loo.sell_price.base.asset_id == base_id, "Order base asset inconsistent");
         FC_ASSERT(loo.sell_price.quote.asset_id == quote_id, "Order quote asset inconsistent with order");
         FC_ASSERT(loo.seller == account->get_id(), "Order not owned by specified account");

         lower_itr = index_by_account.lower_bound(std::make_tuple(account->id, loo.sell_price, *ostart_id));
      }
   }
   else
   {
      // if reach here start_price must be valid
      lower_itr = index_by_account.lower_bound(std::make_tuple(account->id, *ostart_price));
   }

   upper_itr = index_by_account.upper_bound(std::make_tuple(account->id, price::min(base_id, quote_id)));

   // Add the account's orders
   for ( ; lower_itr != upper_itr && count < limit; ++lower_itr, ++count)
   {
      const limit_order_object &order = *lower_itr;
      results.emplace_back(order);
   }

   return results;
}

vector<call_order_object> database_api::get_call_orders(const std::string& a, uint32_t limit)const
{
   return my->get_call_orders( a, limit );
}

vector<call_order_object> database_api_impl::get_call_orders(const std::string& a, uint32_t limit)const
{
   uint64_t api_limit_get_call_orders = _app_options->api_limit_get_call_orders;
   FC_ASSERT( limit <= api_limit_get_call_orders );

   const asset_object* mia = get_asset_from_string(a);
   const auto& call_index = _db.get_index_type<call_order_index>().indices().get<by_collateral>();
   price index_price = price::min( mia->bitasset_data(_db).options.short_backing_asset, mia->get_id() );

   vector< call_order_object> result;
   auto itr_min = call_index.lower_bound(index_price);
   auto itr_max = call_index.upper_bound(index_price.max());
   while( itr_min != itr_max && result.size() < limit )
   {
      result.emplace_back(*itr_min);
      ++itr_min;
   }
   return result;
}

vector<call_order_object> database_api::get_call_orders_by_account(const std::string& account_name_or_id,
                                                                   asset_id_type start, uint32_t limit)const
{
   return my->get_call_orders_by_account( account_name_or_id, start, limit );
}

vector<call_order_object> database_api_impl::get_call_orders_by_account(const std::string& account_name_or_id,
                                                                        asset_id_type start, uint32_t limit)const
{
   uint64_t api_limit_get_call_orders = _app_options->api_limit_get_call_orders;
   FC_ASSERT( limit <= api_limit_get_call_orders );

   vector<call_order_object> result;
   const account_id_type account = get_account_from_string(account_name_or_id)->id;
   const auto& call_idx = _db.get_index_type<call_order_index>().indices().get<by_account>();
   auto call_index_end = call_idx.end();
   auto call_itr = call_idx.lower_bound(boost::make_tuple(account, start));
   while(call_itr != call_index_end && call_itr->borrower == account && result.size() < limit)
   {
      result.push_back(*call_itr);
      ++call_itr;
   }
   return result;
}

vector<force_settlement_object> database_api::get_settle_orders(const std::string& a, uint32_t limit)const
{
   return my->get_settle_orders( a, limit );
}

vector<force_settlement_object> database_api_impl::get_settle_orders(const std::string& a, uint32_t limit)const
{
   uint64_t api_limit_get_settle_orders = _app_options->api_limit_get_settle_orders;
   FC_ASSERT( limit <= api_limit_get_settle_orders );

   const asset_id_type asset_a_id = get_asset_from_string(a)->id;
   const auto& settle_index = _db.get_index_type<force_settlement_index>().indices().get<by_expiration>();
   const asset_object& mia = _db.get(asset_a_id);

   vector<force_settlement_object> result;
   auto itr_min = settle_index.lower_bound(mia.get_id());
   auto itr_max = settle_index.upper_bound(mia.get_id());
   while( itr_min != itr_max && result.size() < limit )
   {
      result.emplace_back(*itr_min);
      ++itr_min;
   }
   return result;
}

vector<force_settlement_object> database_api::get_settle_orders_by_account(
      const std::string& account_name_or_id,
      force_settlement_id_type start,
      uint32_t limit )const
{
   return my->get_settle_orders_by_account( account_name_or_id, start, limit);
}

vector<force_settlement_object> database_api_impl::get_settle_orders_by_account(
      const std::string& account_name_or_id,
      force_settlement_id_type start,
      uint32_t limit )const
{
   uint64_t api_limit_get_settle_orders = _app_options->api_limit_get_settle_orders;
   FC_ASSERT( limit <= api_limit_get_settle_orders );

   vector<force_settlement_object> result;
   const account_id_type account = get_account_from_string(account_name_or_id)->id;
   const auto& settle_idx = _db.get_index_type<force_settlement_index>().indices().get<by_account>();
   auto settle_index_end = settle_idx.end();
   auto settle_itr = settle_idx.lower_bound(boost::make_tuple(account, start));
   while(settle_itr != settle_index_end && settle_itr->owner == account && result.size() < limit)
   {
      result.push_back(*settle_itr);
      ++settle_itr;
   }
   return result;
}


vector<call_order_object> database_api::get_margin_positions( const std::string account_id_or_name )const
{
   return my->get_margin_positions( account_id_or_name );
}

vector<call_order_object> database_api_impl::get_margin_positions( const std::string account_id_or_name )const
{
   try
   {
      const auto& idx = _db.get_index_type<call_order_index>();
      const auto& aidx = idx.indices().get<by_account>();
      const account_id_type id = get_account_from_string(account_id_or_name)->id;
      auto start = aidx.lower_bound( boost::make_tuple( id, asset_id_type(0) ) );
      auto end = aidx.lower_bound( boost::make_tuple( id+1, asset_id_type(0) ) );
      vector<call_order_object> result;
      while( start != end )
      {
         result.push_back(*start);
         ++start;
      }
      return result;
   } FC_CAPTURE_AND_RETHROW( (account_id_or_name) )
}

vector<collateral_bid_object> database_api::get_collateral_bids( const std::string& asset,
                                                                 uint32_t limit, uint32_t start )const
{
   return my->get_collateral_bids( asset, limit, start );
}

vector<collateral_bid_object> database_api_impl::get_collateral_bids( const std::string& asset,
                                                                      uint32_t limit, uint32_t skip )const
{ try {
   FC_ASSERT( limit <= 100 );
   const asset_id_type asset_id = get_asset_from_string(asset)->id;
   const asset_object& swan = asset_id(_db);
   FC_ASSERT( swan.is_market_issued() );
   const asset_bitasset_data_object& bad = swan.bitasset_data(_db);
   const asset_object& back = bad.options.short_backing_asset(_db);
   const auto& idx = _db.get_index_type<collateral_bid_index>();
   const auto& aidx = idx.indices().get<by_price>();
   auto start = aidx.lower_bound( boost::make_tuple( asset_id,
                                                     price::max(back.id, asset_id),
                                                     collateral_bid_id_type() ) );
   auto end = aidx.lower_bound( boost::make_tuple( asset_id,
                                                   price::min(back.id, asset_id),
                                                   collateral_bid_id_type(GRAPHENE_DB_MAX_INSTANCE_ID) ) );
   vector<collateral_bid_object> result;
   while( skip-- > 0 && start != end ) { ++start; }
   while( start != end && limit-- > 0)
   {
      result.push_back(*start);
      ++start;
   }
   return result;
} FC_CAPTURE_AND_RETHROW( (asset)(limit)(skip) ) }

void database_api::subscribe_to_market( std::function<void(const variant&)> callback,
                                        const std::string& a, const std::string& b )
{
   my->subscribe_to_market( callback, a, b );
}

void database_api_impl::subscribe_to_market( std::function<void(const variant&)> callback,
                                             const std::string& a, const std::string& b )
{
   auto asset_a_id = get_asset_from_string(a)->id;
   auto asset_b_id = get_asset_from_string(b)->id;

   if(asset_a_id > asset_b_id) std::swap(asset_a_id,asset_b_id);
   FC_ASSERT(asset_a_id != asset_b_id);
   _market_subscriptions[ std::make_pair(asset_a_id,asset_b_id) ] = callback;
}

void database_api::unsubscribe_from_market(const std::string& a, const std::string& b)
{
   my->unsubscribe_from_market( a, b );
}

void database_api_impl::unsubscribe_from_market(const std::string& a, const std::string& b)
{
   auto asset_a_id = get_asset_from_string(a)->id;
   auto asset_b_id = get_asset_from_string(b)->id;

   if(a > b) std::swap(asset_a_id,asset_b_id);
   FC_ASSERT(asset_a_id != asset_b_id);
   _market_subscriptions.erase(std::make_pair(asset_a_id,asset_b_id));
}

market_ticker database_api::get_ticker( const string& base, const string& quote )const
{
    return my->get_ticker( base, quote );
}

market_ticker database_api_impl::get_ticker( const string& base, const string& quote, bool skip_order_book )const
{
   FC_ASSERT( _app_options && _app_options->has_market_history_plugin, "Market history plugin is not enabled." );

   const auto assets = lookup_asset_symbols( {base, quote} );

   FC_ASSERT( assets[0], "Invalid base asset symbol: ${s}", ("s",base) );
   FC_ASSERT( assets[1], "Invalid quote asset symbol: ${s}", ("s",quote) );

   auto base_id = assets[0]->id;
   auto quote_id = assets[1]->id;
   if( base_id > quote_id ) std::swap( base_id, quote_id );
   const auto& ticker_idx = _db.get_index_type<market_ticker_index>().indices().get<by_market>();
   auto itr = ticker_idx.find( std::make_tuple( base_id, quote_id ) );
   const fc::time_point_sec now = _db.head_block_time();
   if( itr != ticker_idx.end() )
   {
      order_book orders;
      if (!skip_order_book)
      {
         orders = get_order_book(assets[0]->symbol, assets[1]->symbol, 1);
      }
      return market_ticker(*itr, now, *assets[0], *assets[1], orders);
   }
   // if no ticker is found for this market we return an empty ticker
   market_ticker empty_result(now, *assets[0], *assets[1]);
   return empty_result;
}

market_volume database_api::get_24_volume( const string& base, const string& quote )const
{
    return my->get_24_volume( base, quote );
}

market_volume database_api_impl::get_24_volume( const string& base, const string& quote )const
{
   const auto& ticker = get_ticker( base, quote, true );

   market_volume result;
   result.time = ticker.time;
   result.base = ticker.base;
   result.quote = ticker.quote;
   result.base_volume = ticker.base_volume;
   result.quote_volume = ticker.quote_volume;

   return result;
}

order_book database_api::get_order_book( const string& base, const string& quote, unsigned limit )const
{
   return my->get_order_book( base, quote, limit);
}

order_book database_api_impl::get_order_book( const string& base, const string& quote, unsigned limit )const
{
   uint64_t api_limit_get_order_book=_app_options->api_limit_get_order_book;
   FC_ASSERT( limit <= api_limit_get_order_book );

   order_book result;
   result.base = base;
   result.quote = quote;

   auto assets = lookup_asset_symbols( {base, quote} );
   FC_ASSERT( assets[0], "Invalid base asset symbol: ${s}", ("s",base) );
   FC_ASSERT( assets[1], "Invalid quote asset symbol: ${s}", ("s",quote) );

   auto base_id = assets[0]->id;
   auto quote_id = assets[1]->id;
   auto orders = get_limit_orders( base_id, quote_id, limit );

   for( const auto& o : orders )
   {
      if( o.sell_price.base.asset_id == base_id )
      {
         order ord;
         ord.price = price_to_string( o.sell_price, *assets[0], *assets[1] );
         ord.quote = assets[1]->amount_to_string( share_type( fc::uint128_t( o.for_sale.value )
                                                              * o.sell_price.quote.amount.value
                                                              / o.sell_price.base.amount.value ) );
         ord.base = assets[0]->amount_to_string( o.for_sale );
         result.bids.push_back( ord );
      }
      else
      {
         order ord;
         ord.price = price_to_string( o.sell_price, *assets[0], *assets[1] );
         ord.quote = assets[1]->amount_to_string( o.for_sale );
         ord.base = assets[0]->amount_to_string( share_type( fc::uint128_t( o.for_sale.value )
                                                             * o.sell_price.quote.amount.value
                                                             / o.sell_price.base.amount.value ) );
         result.asks.push_back( ord );
      }
   }

   return result;
}

vector<market_ticker> database_api::get_top_markets(uint32_t limit)const
{
   return my->get_top_markets(limit);
}

vector<market_ticker> database_api_impl::get_top_markets(uint32_t limit)const
{
   FC_ASSERT( _app_options && _app_options->has_market_history_plugin, "Market history plugin is not enabled." );

   FC_ASSERT( limit <= 100 );

   const auto& volume_idx = _db.get_index_type<market_ticker_index>().indices().get<by_volume>();
   auto itr = volume_idx.rbegin();
   vector<market_ticker> result;
   result.reserve(limit);
   const fc::time_point_sec now = _db.head_block_time();

   while( itr != volume_idx.rend() && result.size() < limit)
   {
      const asset_object base = itr->base(_db);
      const asset_object quote = itr->quote(_db);
      order_book orders;
      orders = get_order_book(base.symbol, quote.symbol, 1);

      result.emplace_back(market_ticker(*itr, now, base, quote, orders));
      ++itr;
   }
   return result;
}

vector<market_trade> database_api::get_trade_history( const string& base,
                                                      const string& quote,
                                                      fc::time_point_sec start,
                                                      fc::time_point_sec stop,
                                                      unsigned limit )const
{
   return my->get_trade_history( base, quote, start, stop, limit );
}

vector<market_trade> database_api_impl::get_trade_history( const string& base,
                                                           const string& quote,
                                                           fc::time_point_sec start,
                                                           fc::time_point_sec stop,
                                                           unsigned limit )const
{
   FC_ASSERT( _app_options && _app_options->has_market_history_plugin, "Market history plugin is not enabled." );

   FC_ASSERT( limit <= 100 );

   auto assets = lookup_asset_symbols( {base, quote} );
   FC_ASSERT( assets[0], "Invalid base asset symbol: ${s}", ("s",base) );
   FC_ASSERT( assets[1], "Invalid quote asset symbol: ${s}", ("s",quote) );

   auto base_id = assets[0]->id;
   auto quote_id = assets[1]->id;

   if( base_id > quote_id ) std::swap( base_id, quote_id );

   if ( start.sec_since_epoch() == 0 )
      start = fc::time_point_sec( fc::time_point::now() );

   uint32_t count = 0;
   const auto& history_idx = _db.get_index_type<market_history::history_index>().indices().get<by_market_time>();
   auto itr = history_idx.lower_bound( std::make_tuple( base_id, quote_id, start ) );
   vector<market_trade> result;

   while( itr != history_idx.end() && count < limit
          && !( itr->key.base != base_id || itr->key.quote != quote_id || itr->time < stop ) )
   {
      {
         market_trade trade;

         if( assets[0]->id == itr->op.receives.asset_id )
         {
            trade.amount = assets[1]->amount_to_string( itr->op.pays );
            trade.value = assets[0]->amount_to_string( itr->op.receives );
         }
         else
         {
            trade.amount = assets[1]->amount_to_string( itr->op.receives );
            trade.value = assets[0]->amount_to_string( itr->op.pays );
         }

         trade.date = itr->time;
         trade.price = price_to_string( itr->op.fill_price, *assets[0], *assets[1] );

         if( itr->op.is_maker )
         {
            trade.sequence = -itr->key.sequence;
            trade.side1_account_id = itr->op.account_id;
         }
         else
            trade.side2_account_id = itr->op.account_id;

         auto next_itr = std::next(itr);
         // Trades are usually tracked in each direction, exception: for global settlement only one side is recorded
         if( next_itr != history_idx.end() && next_itr->key.base == base_id && next_itr->key.quote == quote_id
             && next_itr->time == itr->time && next_itr->op.is_maker != itr->op.is_maker )
         {  // next_itr now could be the other direction // FIXME not 100% sure
            if( next_itr->op.is_maker )
            {
               trade.sequence = -next_itr->key.sequence;
               trade.side1_account_id = next_itr->op.account_id;
            }
            else
               trade.side2_account_id = next_itr->op.account_id;
            // skip the other direction
            itr = next_itr;
         }

         result.push_back( trade );
         ++count;
      }

      ++itr;
   }

   return result;
}

vector<market_trade> database_api::get_trade_history_by_sequence(
                                                      const string& base,
                                                      const string& quote,
                                                      int64_t start,
                                                      fc::time_point_sec stop,
                                                      unsigned limit )const
{
   return my->get_trade_history_by_sequence( base, quote, start, stop, limit );
}

vector<market_trade> database_api_impl::get_trade_history_by_sequence(
                                                           const string& base,
                                                           const string& quote,
                                                           int64_t start,
                                                           fc::time_point_sec stop,
                                                           unsigned limit )const
{
   FC_ASSERT( _app_options && _app_options->has_market_history_plugin, "Market history plugin is not enabled." );

   FC_ASSERT( limit <= 100 );
   FC_ASSERT( start >= 0 );
   int64_t start_seq = -start;

   auto assets = lookup_asset_symbols( {base, quote} );
   FC_ASSERT( assets[0], "Invalid base asset symbol: ${s}", ("s",base) );
   FC_ASSERT( assets[1], "Invalid quote asset symbol: ${s}", ("s",quote) );

   auto base_id = assets[0]->id;
   auto quote_id = assets[1]->id;

   if( base_id > quote_id ) std::swap( base_id, quote_id );
   const auto& history_idx = _db.get_index_type<graphene::market_history::history_index>().indices().get<by_key>();
   history_key hkey;
   hkey.base = base_id;
   hkey.quote = quote_id;
   hkey.sequence = start_seq;

   uint32_t count = 0;
   auto itr = history_idx.lower_bound( hkey );
   vector<market_trade> result;

   while( itr != history_idx.end() && count < limit
          && !( itr->key.base != base_id || itr->key.quote != quote_id || itr->time < stop ) )
   {
      if( itr->key.sequence == start_seq ) // found the key, should skip this and the other direction if found
      {
         auto next_itr = std::next(itr);
         if( next_itr != history_idx.end() && next_itr->key.base == base_id && next_itr->key.quote == quote_id
             && next_itr->time == itr->time && next_itr->op.is_maker != itr->op.is_maker )
         {  // next_itr now could be the other direction // FIXME not 100% sure
            // skip the other direction
            itr = next_itr;
         }
      }
      else
      {
         market_trade trade;

         if( assets[0]->id == itr->op.receives.asset_id )
         {
            trade.amount = assets[1]->amount_to_string( itr->op.pays );
            trade.value = assets[0]->amount_to_string( itr->op.receives );
         }
         else
         {
            trade.amount = assets[1]->amount_to_string( itr->op.receives );
            trade.value = assets[0]->amount_to_string( itr->op.pays );
         }

         trade.date = itr->time;
         trade.price = price_to_string( itr->op.fill_price, *assets[0], *assets[1] );

         if( itr->op.is_maker )
         {
            trade.sequence = -itr->key.sequence;
            trade.side1_account_id = itr->op.account_id;
         }
         else
            trade.side2_account_id = itr->op.account_id;

         auto next_itr = std::next(itr);
         // Trades are usually tracked in each direction, exception: for global settlement only one side is recorded
         if( next_itr != history_idx.end() && next_itr->key.base == base_id && next_itr->key.quote == quote_id
             && next_itr->time == itr->time && next_itr->op.is_maker != itr->op.is_maker )
         {  // next_itr now could be the other direction // FIXME not 100% sure
            if( next_itr->op.is_maker )
            {
               trade.sequence = -next_itr->key.sequence;
               trade.side1_account_id = next_itr->op.account_id;
            }
            else
               trade.side2_account_id = next_itr->op.account_id;
            // skip the other direction
            itr = next_itr;
         }

         result.push_back( trade );
         ++count;
      }

      ++itr;
   }

   return result;
}

//////////////////////////////////////////////////////////////////////
//                                                                  //
// Witnesses                                                        //
//                                                                  //
//////////////////////////////////////////////////////////////////////

vector<optional<witness_object>> database_api::get_witnesses(const vector<witness_id_type>& witness_ids)const
{
   return my->get_witnesses( witness_ids );
}

vector<optional<witness_object>> database_api_impl::get_witnesses(const vector<witness_id_type>& witness_ids)const
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

fc::optional<witness_object> database_api::get_witness_by_account(const std::string account_id_or_name)const
{
   return my->get_witness_by_account( account_id_or_name );
}

fc::optional<witness_object> database_api_impl::get_witness_by_account(const std::string account_id_or_name) const
{
   const auto& idx = _db.get_index_type<witness_index>().indices().get<by_account>();
   const account_id_type account = get_account_from_string(account_id_or_name)->id;
   auto itr = idx.find(account);
   if( itr != idx.end() )
      return *itr;
   return {};
}

map<string, witness_id_type> database_api::lookup_witness_accounts( const string& lower_bound_name,
                                                                    uint32_t limit )const
{
   return my->lookup_witness_accounts( lower_bound_name, limit );
}

map<string, witness_id_type> database_api_impl::lookup_witness_accounts( const string& lower_bound_name,
                                                                         uint32_t limit )const
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

uint64_t database_api::get_witness_count()const
{
   return my->get_witness_count();
}

uint64_t database_api_impl::get_witness_count()const
{
   return _db.get_index_type<witness_index>().indices().size();
}

//////////////////////////////////////////////////////////////////////
//                                                                  //
// Committee members                                                //
//                                                                  //
//////////////////////////////////////////////////////////////////////

vector<optional<committee_member_object>> database_api::get_committee_members(
                                             const vector<committee_member_id_type>& committee_member_ids )const
{
   return my->get_committee_members( committee_member_ids );
}

vector<optional<committee_member_object>> database_api_impl::get_committee_members(
                                             const vector<committee_member_id_type>& committee_member_ids )const
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

fc::optional<committee_member_object> database_api::get_committee_member_by_account(
                                         const std::string account_id_or_name )const
{
   return my->get_committee_member_by_account( account_id_or_name );
}

fc::optional<committee_member_object> database_api_impl::get_committee_member_by_account(
                                         const std::string account_id_or_name )const
{
   const auto& idx = _db.get_index_type<committee_member_index>().indices().get<by_account>();
   const account_id_type account = get_account_from_string(account_id_or_name)->id;
   auto itr = idx.find(account);
   if( itr != idx.end() )
      return *itr;
   return {};
}

map<string, committee_member_id_type> database_api::lookup_committee_member_accounts(
                                         const string& lower_bound_name, uint32_t limit )const
{
   return my->lookup_committee_member_accounts( lower_bound_name, limit );
}

map<string, committee_member_id_type> database_api_impl::lookup_committee_member_accounts(
                                         const string& lower_bound_name, uint32_t limit )const
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

uint64_t database_api::get_committee_count()const
{
    return my->get_committee_count();
}

uint64_t database_api_impl::get_committee_count()const
{
    return _db.get_index_type<committee_member_index>().indices().size();
}


//////////////////////////////////////////////////////////////////////
//                                                                  //
// Workers                                                          //
//                                                                  //
//////////////////////////////////////////////////////////////////////

vector<worker_object> database_api::get_all_workers()const
{
    return my->get_all_workers();
}

vector<worker_object> database_api_impl::get_all_workers()const
{
    vector<worker_object> result;
    const auto& workers_idx = _db.get_index_type<worker_index>().indices().get<by_id>();
    for( const auto& w : workers_idx )
    {
       result.push_back( w );
    }
    return result;
}

vector<optional<worker_object>> database_api::get_workers_by_account(const std::string account_id_or_name)const
{
    return my->get_workers_by_account( account_id_or_name );
}

vector<optional<worker_object>> database_api_impl::get_workers_by_account(const std::string account_id_or_name)const
{
   vector<optional<worker_object>> result;
   const auto& workers_idx = _db.get_index_type<worker_index>().indices().get<by_account>();

   const account_id_type account = get_account_from_string(account_id_or_name)->id;
   for( const auto& w : workers_idx )
    {
        if( w.worker_account == account )
            result.push_back( w );
    }
    return result;
}

uint64_t database_api::get_worker_count()const
{
    return my->get_worker_count();
}

uint64_t database_api_impl::get_worker_count()const
{
    return _db.get_index_type<worker_index>().indices().size();
}



//////////////////////////////////////////////////////////////////////
//                                                                  //
// Votes                                                            //
//                                                                  //
//////////////////////////////////////////////////////////////////////

vector<variant> database_api::lookup_vote_ids( const vector<vote_id_type>& votes )const
{
   return my->lookup_vote_ids( votes );
}

vector<variant> database_api_impl::lookup_vote_ids( const vector<vote_id_type>& votes )const
{
   FC_ASSERT( votes.size() < 1000, "Only 1000 votes can be queried at a time" );

   const auto& witness_idx = _db.get_index_type<witness_index>().indices().get<by_vote_id>();
   const auto& committee_idx = _db.get_index_type<committee_member_index>().indices().get<by_vote_id>();
   const auto& for_worker_idx = _db.get_index_type<worker_index>().indices().get<by_vote_for>();
   const auto& against_worker_idx = _db.get_index_type<worker_index>().indices().get<by_vote_against>();

   vector<variant> result;
   result.reserve( votes.size() );
   for( auto id : votes )
   {
      switch( id.type() )
      {
         case vote_id_type::committee:
         {
            auto itr = committee_idx.find( id );
            if( itr != committee_idx.end() )
               result.emplace_back( variant( *itr, 2 ) ); // Depth of committee_member_object is 1, add 1 to be safe
            else
               result.emplace_back( variant() );
            break;
         }
         case vote_id_type::witness:
         {
            auto itr = witness_idx.find( id );
            if( itr != witness_idx.end() )
               result.emplace_back( variant( *itr, 2 ) ); // Depth of witness_object is 1, add 1 here to be safe
            else
               result.emplace_back( variant() );
            break;
         }
         case vote_id_type::worker:
         {
            auto itr = for_worker_idx.find( id );
            if( itr != for_worker_idx.end() ) {
               result.emplace_back( variant( *itr, 4 ) ); // Depth of worker_object is 3, add 1 here to be safe.
                                                          // If we want to extract the balance object inside,
                                                          //   need to increase this value
            }
            else {
               auto itr = against_worker_idx.find( id );
               if( itr != against_worker_idx.end() ) {
                  result.emplace_back( variant( *itr, 4 ) ); // Depth of worker_object is 3, add 1 here to be safe.
                                                             // If we want to extract the balance object inside,
                                                             //   need to increase this value
               }
               else {
                  result.emplace_back( variant() );
               }
            }
            break;
         }
         case vote_id_type::VOTE_TYPE_COUNT: break; // supress unused enum value warnings
         default:
            FC_CAPTURE_AND_THROW( fc::out_of_range_exception, (id) );
      }
   }
   return result;
}

//////////////////////////////////////////////////////////////////////
//                                                                  //
// Authority / validation                                           //
//                                                                  //
//////////////////////////////////////////////////////////////////////

std::string database_api::get_transaction_hex(const signed_transaction& trx)const
{
   return my->get_transaction_hex( trx );
}

std::string database_api_impl::get_transaction_hex(const signed_transaction& trx)const
{
   return fc::to_hex(fc::raw::pack(trx));
}

std::string database_api::get_transaction_hex_without_sig(
   const signed_transaction &trx) const
{
   return my->get_transaction_hex_without_sig(trx);
}

std::string database_api_impl::get_transaction_hex_without_sig(
   const signed_transaction &trx) const
{
   return fc::to_hex(fc::raw::pack(static_cast<transaction>(trx)));
}

set<public_key_type> database_api::get_required_signatures( const signed_transaction& trx,
                                                            const flat_set<public_key_type>& available_keys )const
{
   return my->get_required_signatures( trx, available_keys );
}

set<public_key_type> database_api_impl::get_required_signatures( const signed_transaction& trx,
                                                            const flat_set<public_key_type>& available_keys )const
{
   bool allow_non_immediate_owner = ( _db.head_block_time() >= HARDFORK_CORE_584_TIME );
   auto result = trx.get_required_signatures( _db.get_chain_id(),
                                       available_keys,
                                       [&]( account_id_type id ){ return &id(_db).active; },
                                       [&]( account_id_type id ){ return &id(_db).owner; },
                                       allow_non_immediate_owner,
                                       _db.get_global_properties().parameters.max_authority_depth );
   return result;
}

set<public_key_type> database_api::get_potential_signatures( const signed_transaction& trx )const
{
   return my->get_potential_signatures( trx );
}
set<address> database_api::get_potential_address_signatures( const signed_transaction& trx )const
{
   return my->get_potential_address_signatures( trx );
}

set<public_key_type> database_api_impl::get_potential_signatures( const signed_transaction& trx )const
{
   bool allow_non_immediate_owner = ( _db.head_block_time() >= HARDFORK_CORE_584_TIME );
   set<public_key_type> result;
   trx.get_required_signatures(
      _db.get_chain_id(),
      flat_set<public_key_type>(),
      [&]( account_id_type id )
      {
         const auto& auth = id(_db).active;
         for( const auto& k : auth.get_keys() )
            result.insert(k);
         return &auth;
      },
      [&]( account_id_type id )
      {
         const auto& auth = id(_db).owner;
         for( const auto& k : auth.get_keys() )
            result.insert(k);
         return &auth;
      },
      allow_non_immediate_owner,
      _db.get_global_properties().parameters.max_authority_depth
   );

   // Insert keys in required "other" authories
   flat_set<account_id_type> required_active;
   flat_set<account_id_type> required_owner;
   vector<authority> other;
   trx.get_required_authorities( required_active, required_owner, other );
   for( const auto& auth : other )
      for( const auto& key : auth.get_keys() )
         result.insert( key );

   return result;
}

set<address> database_api_impl::get_potential_address_signatures( const signed_transaction& trx )const
{
   set<address> result;
   trx.get_required_signatures(
      _db.get_chain_id(),
      flat_set<public_key_type>(),
      [&]( account_id_type id )
      {
         const auto& auth = id(_db).active;
         for( const auto& k : auth.get_addresses() )
            result.insert(k);
         return &auth;
      },
      [&]( account_id_type id )
      {
         const auto& auth = id(_db).owner;
         for( const auto& k : auth.get_addresses() )
            result.insert(k);
         return &auth;
      },
      _db.get_global_properties().parameters.max_authority_depth
   );
   return result;
}

bool database_api::verify_authority( const signed_transaction& trx )const
{
   return my->verify_authority( trx );
}

bool database_api_impl::verify_authority( const signed_transaction& trx )const
{
   bool allow_non_immediate_owner = ( _db.head_block_time() >= HARDFORK_CORE_584_TIME );
   trx.verify_authority( _db.get_chain_id(),
                         [this]( account_id_type id ){ return &id(_db).active; },
                         [this]( account_id_type id ){ return &id(_db).owner; },
                         allow_non_immediate_owner,
                         _db.get_global_properties().parameters.max_authority_depth );
   return true;
}

bool database_api::verify_account_authority( const string& account_name_or_id,
                                             const flat_set<public_key_type>& signers )const
{
   return my->verify_account_authority( account_name_or_id, signers );
}

bool database_api_impl::verify_account_authority( const string& account_name_or_id,
      const flat_set<public_key_type>& keys )const
{
   // create a dummy transfer
   transfer_operation op;
   op.from = get_account_from_string(account_name_or_id)->id;
   std::vector<operation> ops;
   ops.emplace_back(op);

   try
   {
      graphene::chain::verify_authority(ops, keys,
            [this]( account_id_type id ){ return &id(_db).active; },
            [this]( account_id_type id ){ return &id(_db).owner; },
            true );
   }
   catch (fc::exception& ex)
   {
      return false;
   }

   return true;
}

processed_transaction database_api::validate_transaction( const signed_transaction& trx )const
{
   return my->validate_transaction( trx );
}

processed_transaction database_api_impl::validate_transaction( const signed_transaction& trx )const
{
   return _db.validate_transaction(trx);
}

vector< fc::variant > database_api::get_required_fees( const vector<operation>& ops,
                                                       const std::string& asset_id_or_symbol )const
{
   return my->get_required_fees( ops, asset_id_or_symbol );
}

/**
 * Container method for mutually recursive functions used to
 * implement get_required_fees() with potentially nested proposals.
 */
struct get_required_fees_helper
{
   get_required_fees_helper(
      const fee_schedule& _current_fee_schedule,
      const price& _core_exchange_rate,
      uint32_t _max_recursion
      )
      : current_fee_schedule(_current_fee_schedule),
        core_exchange_rate(_core_exchange_rate),
        max_recursion(_max_recursion)
   {}

   fc::variant set_op_fees( operation& op )
   {
      if( op.is_type<proposal_create_operation>() )
      {
         return set_proposal_create_op_fees( op );
      }
      else
      {
         asset fee = current_fee_schedule.set_fee( op, core_exchange_rate );
         fc::variant result;
         fc::to_variant( fee, result, GRAPHENE_NET_MAX_NESTED_OBJECTS );
         return result;
      }
   }

   fc::variant set_proposal_create_op_fees( operation& proposal_create_op )
   {
      proposal_create_operation& op = proposal_create_op.get<proposal_create_operation>();
      std::pair< asset, fc::variants > result;
      for( op_wrapper& prop_op : op.proposed_ops )
      {
         FC_ASSERT( current_recursion < max_recursion );
         ++current_recursion;
         result.second.push_back( set_op_fees( prop_op.op ) );
         --current_recursion;
      }
      // we need to do this on the boxed version, which is why we use
      // two mutually recursive functions instead of a visitor
      result.first = current_fee_schedule.set_fee( proposal_create_op, core_exchange_rate );
      fc::variant vresult;
      fc::to_variant( result, vresult, GRAPHENE_NET_MAX_NESTED_OBJECTS );
      return vresult;
   }

   const fee_schedule& current_fee_schedule;
   const price& core_exchange_rate;
   uint32_t max_recursion;
   uint32_t current_recursion = 0;
};

vector< fc::variant > database_api_impl::get_required_fees( const vector<operation>& ops,
                                                            const std::string& asset_id_or_symbol )const
{
   vector< operation > _ops = ops;
   //
   // we copy the ops because we need to mutate an operation to reliably
   // determine its fee, see #435
   //

   vector< fc::variant > result;
   result.reserve(ops.size());
   const asset_object& a = *get_asset_from_string(asset_id_or_symbol);
   get_required_fees_helper helper(
      _db.current_fee_schedule(),
      a.options.core_exchange_rate,
      GET_REQUIRED_FEES_MAX_RECURSION );
   for( operation& op : _ops )
   {
      result.push_back( helper.set_op_fees( op ) );
   }
   return result;
}

//////////////////////////////////////////////////////////////////////
//                                                                  //
// Proposed transactions                                            //
//                                                                  //
//////////////////////////////////////////////////////////////////////

vector<proposal_object> database_api::get_proposed_transactions( const std::string account_id_or_name )const
{
   return my->get_proposed_transactions( account_id_or_name );
}

vector<proposal_object> database_api_impl::get_proposed_transactions( const std::string account_id_or_name )const
{
   const auto& proposal_idx = _db.get_index_type< primary_index< proposal_index > >();
   const auto& proposals_by_account = proposal_idx.get_secondary_index<graphene::chain::required_approval_index>();

   vector<proposal_object> result;
   const account_id_type id = get_account_from_string(account_id_or_name)->id;

   auto required_approvals_itr = proposals_by_account._account_to_proposals.find( id );
   if( required_approvals_itr != proposals_by_account._account_to_proposals.end() )
   {
      result.reserve( required_approvals_itr->second.size() );
      for( auto proposal_id : required_approvals_itr->second )
      {
         result.push_back( proposal_id(_db) );
      }
   }
   return result;
}

//////////////////////////////////////////////////////////////////////
//                                                                  //
// Blinded balances                                                 //
//                                                                  //
//////////////////////////////////////////////////////////////////////

vector<blinded_balance_object> database_api::get_blinded_balances(
                                  const flat_set<commitment_type>& commitments )const
{
   return my->get_blinded_balances( commitments );
}

vector<blinded_balance_object> database_api_impl::get_blinded_balances(
                                  const flat_set<commitment_type>& commitments )const
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

//////////////////////////////////////////////////////////////////////
//                                                                  //
//  Withdrawals                                                     //
//                                                                  //
//////////////////////////////////////////////////////////////////////

vector<withdraw_permission_object> database_api::get_withdraw_permissions_by_giver(
                                      const std::string account_id_or_name,
                                      withdraw_permission_id_type start,
                                      uint32_t limit)const
{
   return my->get_withdraw_permissions_by_giver( account_id_or_name, start, limit );
}

vector<withdraw_permission_object> database_api_impl::get_withdraw_permissions_by_giver(
                                      const std::string account_id_or_name,
                                      withdraw_permission_id_type start,
                                      uint32_t limit)const
{
   FC_ASSERT( limit <= 101 );
   vector<withdraw_permission_object> result;

   const auto& withdraw_idx = _db.get_index_type<withdraw_permission_index>().indices().get<by_from>();
   auto withdraw_index_end = withdraw_idx.end();
   const account_id_type account = get_account_from_string(account_id_or_name)->id;
   auto withdraw_itr = withdraw_idx.lower_bound(boost::make_tuple(account, start));
   while( withdraw_itr != withdraw_index_end && withdraw_itr->withdraw_from_account == account
          && result.size() < limit )
   {
      result.push_back(*withdraw_itr);
      ++withdraw_itr;
   }
   return result;
}

vector<withdraw_permission_object> database_api::get_withdraw_permissions_by_recipient(
                                      const std::string account_id_or_name,
                                      withdraw_permission_id_type start,
                                      uint32_t limit)const
{
   return my->get_withdraw_permissions_by_recipient( account_id_or_name, start, limit );
}

vector<withdraw_permission_object> database_api_impl::get_withdraw_permissions_by_recipient(
                                      const std::string account_id_or_name,
                                      withdraw_permission_id_type start,
                                      uint32_t limit)const
{
   FC_ASSERT( limit <= 101 );
   vector<withdraw_permission_object> result;

   const auto& withdraw_idx = _db.get_index_type<withdraw_permission_index>().indices().get<by_authorized>();
   auto withdraw_index_end = withdraw_idx.end();
   const account_id_type account = get_account_from_string(account_id_or_name)->id;
   auto withdraw_itr = withdraw_idx.lower_bound(boost::make_tuple(account, start));
   while(withdraw_itr != withdraw_index_end && withdraw_itr->authorized_account == account && result.size() < limit)
   {
      result.push_back(*withdraw_itr);
      ++withdraw_itr;
   }
   return result;
}

//////////////////////////////////////////////////////////////////////
//                                                                  //
//  HTLC                                                            //
//                                                                  //
//////////////////////////////////////////////////////////////////////

optional<htlc_object> database_api::get_htlc( htlc_id_type id, optional<bool> subscribe )const
{
   return my->get_htlc( id, subscribe );
}

fc::optional<htlc_object> database_api_impl::get_htlc( htlc_id_type id, optional<bool> subscribe )const
{
   auto obj = get_objects( { id }, subscribe ).front();
   if ( !obj.is_null() )
   {
      return fc::optional<htlc_object>(obj.template as<htlc_object>(GRAPHENE_MAX_NESTED_OBJECTS));
   }
   return fc::optional<htlc_object>();
}

vector<htlc_object> database_api::get_htlc_by_from( const std::string account_id_or_name,
                                                    htlc_id_type start, uint32_t limit )const
{
   return my->get_htlc_by_from(account_id_or_name, start, limit);
}

vector<htlc_object> database_api_impl::get_htlc_by_from( const std::string account_id_or_name,
                                                         htlc_id_type start, uint32_t limit ) const
{
   FC_ASSERT( limit <= _app_options->api_limit_get_htlc_by );
   vector<htlc_object> result;

   const auto& htlc_idx = _db.get_index_type< htlc_index >().indices().get< by_from_id >();
   auto htlc_index_end = htlc_idx.end();
   const account_id_type account = get_account_from_string(account_id_or_name)->id;
   auto htlc_itr = htlc_idx.lower_bound(boost::make_tuple(account, start));

   while(htlc_itr != htlc_index_end && htlc_itr->transfer.from == account && result.size() < limit)
   {
      result.push_back(*htlc_itr);
      ++htlc_itr;
   }
   return result;
}

vector<htlc_object> database_api::get_htlc_by_to( const std::string account_id_or_name,
                                                  htlc_id_type start, uint32_t limit )const
{
   return my->get_htlc_by_to(account_id_or_name, start, limit);
}

vector<htlc_object> database_api_impl::get_htlc_by_to( const std::string account_id_or_name,
                                                       htlc_id_type start, uint32_t limit ) const
{

   FC_ASSERT( limit <= _app_options->api_limit_get_htlc_by );
   vector<htlc_object> result;

   const auto& htlc_idx = _db.get_index_type< htlc_index >().indices().get< by_to_id >();
   auto htlc_index_end = htlc_idx.end();
   const account_id_type account = get_account_from_string(account_id_or_name)->id;
   auto htlc_itr = htlc_idx.lower_bound(boost::make_tuple(account, start));

   while(htlc_itr != htlc_index_end && htlc_itr->transfer.to == account && result.size() < limit)
   {
      result.push_back(*htlc_itr);
      ++htlc_itr;
   }
   return result;
}

vector<htlc_object> database_api::list_htlcs(const htlc_id_type start, uint32_t limit)const
{
   return my->list_htlcs(start, limit);
}

vector<htlc_object> database_api_impl::list_htlcs(const htlc_id_type start, uint32_t limit) const
{
   FC_ASSERT( limit <= _app_options->api_limit_list_htlcs );

   vector<htlc_object> result;
   const auto& htlc_idx = _db.get_index_type<htlc_index>().indices().get<by_id>();
   auto itr = htlc_idx.lower_bound(start);
   while(itr != htlc_idx.end() && result.size() < limit)
   {
      result.push_back(*itr);
      ++itr;
   }
   return result;
}

//////////////////////////////////////////////////////////////////////
//                                                                  //
// Private methods                                                  //
//                                                                  //
//////////////////////////////////////////////////////////////////////

const account_object* database_api_impl::get_account_from_string( const std::string& name_or_id,
                                                                  bool throw_if_not_found ) const
{
   // TODO cache the result to avoid repeatly fetching from db
   FC_ASSERT( name_or_id.size() > 0);
   const account_object* account = nullptr;
   if (std::isdigit(name_or_id[0]))
      account = _db.find(fc::variant(name_or_id, 1).as<account_id_type>(1));
   else
   {
      const auto& idx = _db.get_index_type<account_index>().indices().get<by_name>();
      auto itr = idx.find(name_or_id);
      if (itr != idx.end())
         account = &*itr;
   }
   if(throw_if_not_found)
      FC_ASSERT( account, "no such account" );
   return account;
}

const asset_object* database_api_impl::get_asset_from_string( const std::string& symbol_or_id,
                                                              bool throw_if_not_found ) const
{
   // TODO cache the result to avoid repeatly fetching from db
   FC_ASSERT( symbol_or_id.size() > 0);
   const asset_object* asset = nullptr;
   if (std::isdigit(symbol_or_id[0]))
      asset = _db.find(fc::variant(symbol_or_id, 1).as<asset_id_type>(1));
   else
   {
      const auto& idx = _db.get_index_type<asset_index>().indices().get<by_symbol>();
      auto itr = idx.find(symbol_or_id);
      if (itr != idx.end())
         asset = &*itr;
   }
   if(throw_if_not_found)
      FC_ASSERT( asset, "no such asset" );
   return asset;
}

// helper function
vector<optional<extended_asset_object>> database_api_impl::get_assets( const vector<asset_id_type>& asset_ids,
                                                                       optional<bool> subscribe )const
{
   bool to_subscribe = get_whether_to_subscribe( subscribe );
   vector<optional<extended_asset_object>> result; result.reserve(asset_ids.size());
   std::transform(asset_ids.begin(), asset_ids.end(), std::back_inserter(result),
           [this,to_subscribe](asset_id_type id) -> optional<extended_asset_object> {
      if(auto o = _db.find(id))
      {
         if( to_subscribe )
            subscribe_to_item( id );
         return extend_asset( *o );
      }
      return {};
   });
   return result;
}

// helper function
vector<limit_order_object> database_api_impl::get_limit_orders( const asset_id_type a, const asset_id_type b,
                                                                const uint32_t limit )const
{
   uint64_t api_limit_get_limit_orders=_app_options->api_limit_get_limit_orders;
   FC_ASSERT( limit <= api_limit_get_limit_orders );

   const auto& limit_order_idx = _db.get_index_type<limit_order_index>();
   const auto& limit_price_idx = limit_order_idx.indices().get<by_price>();

   vector<limit_order_object> result;
   result.reserve(limit*2);

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

bool database_api_impl::is_impacted_account( const flat_set<account_id_type>& accounts)
{
   if( !_subscribed_accounts.size() || !accounts.size() )
      return false;

   return std::any_of(accounts.begin(), accounts.end(), [this](const account_id_type& account) {
      return _subscribed_accounts.find(account) != _subscribed_accounts.end();
   });
}

void database_api_impl::broadcast_updates( const vector<variant>& updates )
{
   if( updates.size() && _subscribe_callback ) {
      auto capture_this = shared_from_this();
      fc::async([capture_this,updates](){
          if(capture_this->_subscribe_callback)
            capture_this->_subscribe_callback( fc::variant(updates) );
      });
   }
}

void database_api_impl::broadcast_market_updates( const market_queue_type& queue)
{
   if( queue.size() )
   {
      auto capture_this = shared_from_this();
      fc::async([capture_this, this, queue](){
          for( const auto& item : queue )
          {
            auto sub = _market_subscriptions.find(item.first);
            if( sub != _market_subscriptions.end() )
                sub->second( fc::variant(item.second ) );
          }
      });
   }
}

void database_api_impl::on_objects_removed( const vector<object_id_type>& ids,
                                            const vector<const object*>& objs,
                                            const flat_set<account_id_type>& impacted_accounts )
{
   handle_object_changed(_notify_remove_create, false, ids, impacted_accounts,
      [objs](object_id_type id) -> const object* {
         auto it = std::find_if(
               objs.begin(), objs.end(),
               [id](const object* o) {return o != nullptr && o->id == id;});

         if (it != objs.end())
            return *it;

         return nullptr;
      }
   );
}

void database_api_impl::on_objects_new( const vector<object_id_type>& ids,
                                        const flat_set<account_id_type>& impacted_accounts )
{
   handle_object_changed(_notify_remove_create, true, ids, impacted_accounts,
      std::bind(&object_database::find_object, &_db, std::placeholders::_1)
   );
}

void database_api_impl::on_objects_changed( const vector<object_id_type>& ids,
                                            const flat_set<account_id_type>& impacted_accounts )
{
   handle_object_changed(false, true, ids, impacted_accounts,
      std::bind(&object_database::find_object, &_db, std::placeholders::_1)
   );
}

void database_api_impl::handle_object_changed( bool force_notify,
                                               bool full_object,
                                               const vector<object_id_type>& ids,
                                               const flat_set<account_id_type>& impacted_accounts,
                                               std::function<const object*(object_id_type id)> find_object )
{
   if( _subscribe_callback )
   {
      vector<variant> updates;

      for(auto id : ids)
      {
         if( force_notify || is_subscribed_to_item(id) || is_impacted_account(impacted_accounts) )
         {
            if( full_object )
            {
               auto obj = find_object(id);
               if( obj )
               {
                  updates.emplace_back( obj->to_variant() );
               }
            }
            else
            {
               updates.emplace_back( fc::variant( id, 1 ) );
            }
         }
      }

      if( updates.size() )
         broadcast_updates(updates);
   }

   if( _market_subscriptions.size() )
   {
      market_queue_type broadcast_queue;

      for(auto id : ids)
      {
         if( id.is<call_order_object>() )
         {
            enqueue_if_subscribed_to_market<call_order_object>( find_object(id), broadcast_queue, full_object );
         }
         else if( id.is<limit_order_object>() )
         {
            enqueue_if_subscribed_to_market<limit_order_object>( find_object(id), broadcast_queue, full_object );
         }
         else if( id.is<force_settlement_object>() )
         {
            enqueue_if_subscribed_to_market<force_settlement_object>( find_object(id), broadcast_queue,
                                                                      full_object );
         }
      }

      if( broadcast_queue.size() )
         broadcast_market_updates(broadcast_queue);
   }
}

/** note: this method cannot yield because it is called in the middle of
 * apply a block.
 */
void database_api_impl::on_applied_block()
{
   if (_block_applied_callback)
   {
      auto capture_this = shared_from_this();
      block_id_type block_id = _db.head_block_id();
      fc::async([this,capture_this,block_id](){
         _block_applied_callback(fc::variant(block_id, 1));
      });
   }

   if(_market_subscriptions.size() == 0)
      return;

   const auto& ops = _db.get_applied_operations();
   map< std::pair<asset_id_type,asset_id_type>, vector<pair<operation, operation_result>> > subscribed_markets_ops;
   for(const optional< operation_history_object >& o_op : ops)
   {
      if( !o_op.valid() )
         continue;
      const operation_history_object& op = *o_op;

      optional< std::pair<asset_id_type,asset_id_type> > market;
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
      if( market.valid() && _market_subscriptions.count(*market) )
         // FIXME this may cause fill_order_operation be pushed before order creation
         subscribed_markets_ops[*market].emplace_back(std::make_pair(op.op, op.result));
   }
   /// we need to ensure the database_api is not deleted for the life of the async operation
   auto capture_this = shared_from_this();
   fc::async([this,capture_this,subscribed_markets_ops](){
      for(auto item : subscribed_markets_ops)
      {
         auto itr = _market_subscriptions.find(item.first);
         if(itr != _market_subscriptions.end())
            itr->second(fc::variant(item.second, GRAPHENE_NET_MAX_NESTED_OBJECTS));
      }
   });
}

} } // graphene::app
