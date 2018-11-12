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

#include "implementation/wallet_account_impl.cpp"

namespace graphene { namespace wallet {

uint64_t wallet_api::get_account_count() const
{
   return my->_remote_db->get_account_count();
}

vector<account_object> wallet_api::list_my_accounts()
{
   return vector<account_object>(my->_wallet.my_accounts.begin(), my->_wallet.my_accounts.end());
}

map<string,account_id_type> wallet_api::list_accounts(const string& lowerbound, uint32_t limit)
{
   return my->_remote_db->lookup_accounts(lowerbound, limit);
}

vector<asset> wallet_api::list_account_balances(const string& id)
{
   /*
    * Compatibility issue
    * Current Date: 2018-09-13 More info: https://github.com/bitshares/bitshares-core/issues/1307
    * Todo: remove the next 2 lines and change always_id to id in remote call after next hardfork
    */
   auto account = get_account(id);
   auto always_id = my->account_id_to_string(account.id);

   return my->_remote_db->get_account_balances(always_id, flat_set<asset_id_type>());
}

vector<operation_detail> wallet_api::get_account_history(string name, int limit)const
{
   vector<operation_detail> result;

   /*
    * Compatibility issue
    * Current Date: 2018-09-14 More info: https://github.com/bitshares/bitshares-core/issues/1307
    * Todo: remove the next 2 lines and change always_id to name in remote call after next hardfork
    */
   auto account = get_account(name);
   auto always_id = my->account_id_to_string(account.id);

   while( limit > 0 )
   {
      bool skip_first_row = false;
      operation_history_id_type start;
      if( result.size() )
      {
         start = result.back().op.id;
         if( start == operation_history_id_type() ) // no more data
            break;
         start = start + (-1);
         if( start == operation_history_id_type() ) // will return most recent history if directly call remote API with this
         {
            start = start + 1;
            skip_first_row = true;
         }
      }

      int page_limit = skip_first_row ? std::min( 100, limit + 1 ) : std::min( 100, limit );

      vector<operation_history_object> current = my->_remote_hist->get_account_history(
            always_id,
            operation_history_id_type(),
            page_limit,
            start );
      bool first_row = true;
      for( auto& o : current )
      {
         if( first_row )
         {
            first_row = false;
            if( skip_first_row )
            {
               continue;
            }
         }
         std::stringstream ss;
         auto memo = o.op.visit(detail::operation_printer(ss, *my, o.result));
         result.push_back( operation_detail{ memo, ss.str(), o } );
      }

      if( int(current.size()) < page_limit )
         break;

      limit -= current.size();
      if( skip_first_row )
         ++limit;
   }

   return result;
}

vector<operation_detail> wallet_api::get_relative_account_history(
      string name,
      uint32_t stop,
      int limit,
      uint32_t start)const
{
   vector<operation_detail> result;
   auto account_id = get_account(name).get_id();

   const account_object& account = my->get_account(account_id);
   const account_statistics_object& stats = my->get_object(account.statistics);

   /*
    * Compatibility issue
    * Current Date: 2018-09-14 More info: https://github.com/bitshares/bitshares-core/issues/1307
    * Todo: remove the next line and change always_id to name in remote call after next hardfork
    */
   auto always_id = my->account_id_to_string(account_id);

   if(start == 0)
       start = stats.total_ops;
   else
      start = std::min<uint32_t>(start, stats.total_ops);

   while( limit > 0 )
   {
      vector <operation_history_object> current = my->_remote_hist->get_relative_account_history(
            always_id,
            stop,
            std::min<uint32_t>(100, limit),
            start);
      for (auto &o : current) {
         std::stringstream ss;
         auto memo = o.op.visit(detail::operation_printer(ss, *my, o.result));
         result.push_back(operation_detail{memo, ss.str(), o});
      }
      if (current.size() < std::min<uint32_t>(100, limit))
         break;
      limit -= current.size();
      start -= 100;
      if( start == 0 ) break;
   }
   return result;
}

account_history_operation_detail wallet_api::get_account_history_by_operations(
      string name,
      vector<uint16_t> operation_types,
      uint32_t start,
      int limit)
{
    account_history_operation_detail result;
    auto account_id = get_account(name).get_id();

    const auto& account = my->get_account(account_id);
    const auto& stats = my->get_object(account.statistics);

    /*
     * Compatibility issue
     * Current Date: 2018-09-14 More info: https://github.com/bitshares/bitshares-core/issues/1307
     * Todo: remove the next line and change always_id to name in remote call after next hardfork
     */
     auto always_id = my->account_id_to_string(account_id);

    // sequence of account_transaction_history_object start with 1
    start = start == 0 ? 1 : start;

    if (start <= stats.removed_ops) {
        start = stats.removed_ops;
        result.total_count =stats.removed_ops;
    }

    while (limit > 0 && start <= stats.total_ops) {
        uint32_t min_limit = std::min<uint32_t> (100, limit);
        auto current = my->_remote_hist->get_account_history_by_operations(always_id, operation_types, start, min_limit);
        for (auto& obj : current.operation_history_objs) {
            std::stringstream ss;
            auto memo = obj.op.visit(detail::operation_printer(ss, *my, obj.result));

            transaction_id_type transaction_id;
            auto block = get_block(obj.block_num);
            if (block.valid() && obj.trx_in_block < block->transaction_ids.size()) {
                transaction_id = block->transaction_ids[obj.trx_in_block];
            }
            result.details.push_back(operation_detail_ex{memo, ss.str(), obj, transaction_id});
        }
        result.result_count += current.operation_history_objs.size();
        result.total_count += current.total_count;

        start += current.total_count > 0 ? current.total_count : min_limit;
        limit -= current.operation_history_objs.size();
    }

    return result;
}

full_account wallet_api::get_full_account( const string& name_or_id)
{
    return my->_remote_db->get_full_accounts({name_or_id}, false)[name_or_id];
}

vector<limit_order_object> wallet_api::get_account_limit_orders(
      const string& name_or_id,
      const string &base,
      const string &quote,
      uint32_t limit,
      optional<limit_order_id_type> ostart_id,
      optional<price> ostart_price)
{
   return my->_remote_db->get_account_limit_orders(name_or_id, base, quote, limit, ostart_id, ostart_price);
}

account_object wallet_api::get_account(string account_name_or_id) const
{
   return my->get_account(account_name_or_id);
}

account_id_type wallet_api::get_account_id(string account_name_or_id) const
{
   return my->get_account_id(account_name_or_id);
}

signed_transaction wallet_api::register_account(string name,
                                                public_key_type owner_pubkey,
                                                public_key_type active_pubkey,
                                                string  registrar_account,
                                                string  referrer_account,
                                                uint32_t referrer_percent,
                                                bool broadcast)
{
   return my->register_account( name, owner_pubkey, active_pubkey, registrar_account, referrer_account, referrer_percent, broadcast );
}
signed_transaction wallet_api::create_account_with_brain_key(string brain_key, string account_name,
                                                             string registrar_account, string referrer_account,
                                                             bool broadcast /* = false */)
{
   return my->create_account_with_brain_key(
            brain_key, account_name, registrar_account,
            referrer_account, broadcast
            );
}

signed_transaction wallet_api::approve_proposal(
   const string& fee_paying_account,
   const string& proposal_id,
   const approval_delta& delta,
   bool broadcast /* = false */
   )
{
   return my->approve_proposal( fee_paying_account, proposal_id, delta, broadcast );
}

signed_transaction wallet_api::transfer(string from, string to, string amount,
                                        string asset_symbol, string memo, bool broadcast /* = false */)
{
   return my->transfer(from, to, amount, asset_symbol, memo, broadcast);
}

vector< vesting_balance_object_with_info > wallet_api::get_vesting_balances( string account_name )
{
   return my->get_vesting_balances( account_name );
}

signed_transaction wallet_api::withdraw_vesting(
   string witness_name,
   string amount,
   string asset_symbol,
   bool broadcast /* = false */)
{
   return my->withdraw_vesting( witness_name, amount, asset_symbol, broadcast );
}

vesting_balance_object_with_info::vesting_balance_object_with_info( const vesting_balance_object& vbo, fc::time_point_sec now )
   : vesting_balance_object( vbo )
{
   allowed_withdraw = get_allowed_withdraw( now );
   allowed_withdraw_time = now;
}

signed_transaction wallet_api::upgrade_account( string name, bool broadcast )
{
   return my->upgrade_account(name,broadcast);
}

signed_transaction wallet_api::whitelist_account(string authorizing_account,
                                                 string account_to_list,
                                                 account_whitelist_operation::account_listing new_listing_status,
                                                 bool broadcast /* = false */)
{
   return my->whitelist_account(authorizing_account, account_to_list, new_listing_status, broadcast);
}

void wallet_api::encrypt_keys()
{
   my->encrypt_keys();
}

memo_data wallet_api::sign_memo(string from, string to, string memo)
{
   FC_ASSERT(!is_locked());
   return my->sign_memo(from, to, memo);
}

string wallet_api::read_memo(const memo_data& memo)
{
   FC_ASSERT(!is_locked());
   return my->read_memo(memo);
}

string wallet_api::get_key_label( public_key_type key )const
{
   auto key_itr   = my->_wallet.labeled_keys.get<by_key>().find(key);
   if( key_itr != my->_wallet.labeled_keys.get<by_key>().end() )
      return key_itr->label;
   return string();
}

public_key_type wallet_api::get_public_key( string label )const
{
   try { return fc::variant(label, 1).as<public_key_type>( 1 ); } catch ( ... ){}

   auto key_itr   = my->_wallet.labeled_keys.get<by_label>().find(label);
   if( key_itr != my->_wallet.labeled_keys.get<by_label>().end() )
      return key_itr->key;
   return public_key_type();
}

bool wallet_api::set_key_label( public_key_type key, string label )
{
   auto result = my->_wallet.labeled_keys.insert( key_label{label,key} );
   if( result.second  ) return true;

   auto key_itr   = my->_wallet.labeled_keys.get<by_key>().find(key);
   auto label_itr = my->_wallet.labeled_keys.get<by_label>().find(label);
   if( label_itr == my->_wallet.labeled_keys.get<by_label>().end() )
   {
      if( key_itr != my->_wallet.labeled_keys.get<by_key>().end() )
         return my->_wallet.labeled_keys.get<by_key>().modify( key_itr, [&]( key_label& obj ){ obj.label = label; } );
   }
   return false;
}

vector<brain_key_info> wallet_api::derive_owner_keys_from_brain_key(string brain_key, int number_of_desired_keys) const
{
   return graphene::wallet::utility::derive_owner_keys_from_brain_key(brain_key, number_of_desired_keys);
}

bool wallet_api::is_public_key_registered(string public_key) const
{
   bool is_known = my->_remote_db->is_public_key_registered(public_key);
   return is_known;
}

}} // graphene::wallet
