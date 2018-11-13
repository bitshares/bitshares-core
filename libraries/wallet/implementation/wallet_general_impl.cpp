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

namespace graphene { namespace wallet {

#define BRAIN_KEY_WORD_COUNT 16

brain_key_info utility::suggest_brain_key()
{
   brain_key_info result;
   // create a private key for secure entropy
   fc::sha256 sha_entropy1 = fc::ecc::private_key::generate().get_secret();
   fc::sha256 sha_entropy2 = fc::ecc::private_key::generate().get_secret();
   fc::bigint entropy1(sha_entropy1.data(), sha_entropy1.data_size());
   fc::bigint entropy2(sha_entropy2.data(), sha_entropy2.data_size());
   fc::bigint entropy(entropy1);
   entropy <<= 8 * sha_entropy1.data_size();
   entropy += entropy2;
   string brain_key = "";

   for (int i = 0; i < BRAIN_KEY_WORD_COUNT; i++)
   {
      fc::bigint choice = entropy % graphene::words::word_list_size;
      entropy /= graphene::words::word_list_size;
      if (i > 0)
         brain_key += " ";
      brain_key += graphene::words::word_list[choice.to_int64()];
   }

   brain_key = detail::normalize_brain_key(brain_key);
   fc::ecc::private_key priv_key = detail::derive_private_key(brain_key, 0);
   result.brain_priv_key = brain_key;
   result.wif_priv_key = key_to_wif(priv_key);
   result.pub_key = priv_key.get_public_key();
   return result;
}

vector<brain_key_info> utility::derive_owner_keys_from_brain_key(string brain_key, int number_of_desired_keys)
{
   // Safety-check
   FC_ASSERT( number_of_desired_keys >= 1 );

   // Create as many derived owner keys as requested
   vector<brain_key_info> results;
   brain_key = graphene::wallet::detail::normalize_brain_key(brain_key);
   for (int i = 0; i < number_of_desired_keys; ++i) {
      fc::ecc::private_key priv_key = graphene::wallet::detail::derive_private_key( brain_key, i );

      brain_key_info result;
      result.brain_priv_key = brain_key;
      result.wif_priv_key = key_to_wif( priv_key );
      result.pub_key = priv_key.get_public_key();

      results.push_back(result);
   }

   return results;
}

namespace detail {

std::map<string,std::function<string(fc::variant,const fc::variants&)>> wallet_api_impl::get_result_formatters() const
{
   std::map<string,std::function<string(fc::variant,const fc::variants&)> > m;
   m["help"] = [](variant result, const fc::variants& a)
   {
      return result.get_string();
   };

   m["gethelp"] = [](variant result, const fc::variants& a)
   {
      return result.get_string();
   };

   m["get_account_history"] = [this](variant result, const fc::variants& a)
   {
      auto r = result.as<vector<operation_detail>>( GRAPHENE_MAX_NESTED_OBJECTS );
      std::stringstream ss;

      for( operation_detail& d : r )
      {
         operation_history_object& i = d.op;
         auto b = _remote_db->get_block_header(i.block_num);
         FC_ASSERT(b);
         ss << b->timestamp.to_iso_string() << " ";
         i.op.visit(operation_printer(ss, *this, i.result));
         ss << " \n";
      }

      return ss.str();
   };
   m["get_relative_account_history"] = [this](variant result, const fc::variants& a)
   {
      auto r = result.as<vector<operation_detail>>( GRAPHENE_MAX_NESTED_OBJECTS );
      std::stringstream ss;

      for( operation_detail& d : r )
      {
         operation_history_object& i = d.op;
         auto b = _remote_db->get_block_header(i.block_num);
         FC_ASSERT(b);
         ss << b->timestamp.to_iso_string() << " ";
         i.op.visit(operation_printer(ss, *this, i.result));
         ss << " \n";
      }

      return ss.str();
   };

   m["get_account_history_by_operations"] = [this](variant result, const fc::variants& a) {
         auto r = result.as<account_history_operation_detail>( GRAPHENE_MAX_NESTED_OBJECTS );
         std::stringstream ss;
         ss << "total_count : ";
         ss << r.total_count;
         ss << " \n";
         ss << "result_count : ";
         ss << r.result_count;
         ss << " \n";
         for (operation_detail_ex& d : r.details) {
            operation_history_object& i = d.op;
            auto b = _remote_db->get_block_header(i.block_num);
            FC_ASSERT(b);
            ss << b->timestamp.to_iso_string() << " ";
            i.op.visit(operation_printer(ss, *this, i.result));
            ss << " transaction_id : ";
            ss << d.transaction_id.str();
            ss << " \n";
         }

         return ss.str();
   };

   m["list_account_balances"] = [this](variant result, const fc::variants& a)
   {
      auto r = result.as<vector<asset>>( GRAPHENE_MAX_NESTED_OBJECTS );
      vector<asset_object> asset_recs;
      std::transform(r.begin(), r.end(), std::back_inserter(asset_recs), [this](const asset& a) {
         return get_asset(a.asset_id);
      });

      std::stringstream ss;
      for( unsigned i = 0; i < asset_recs.size(); ++i )
         ss << asset_recs[i].amount_to_pretty_string(r[i]) << "\n";

      return ss.str();
   };

   m["get_blind_balances"] = [this](variant result, const fc::variants& a)
   {
      auto r = result.as<vector<asset>>( GRAPHENE_MAX_NESTED_OBJECTS );
      vector<asset_object> asset_recs;
      std::transform(r.begin(), r.end(), std::back_inserter(asset_recs), [this](const asset& a) {
         return get_asset(a.asset_id);
      });

      std::stringstream ss;
      for( unsigned i = 0; i < asset_recs.size(); ++i )
         ss << asset_recs[i].amount_to_pretty_string(r[i]) << "\n";

      return ss.str();
   };
   m["transfer_to_blind"] = [this](variant result, const fc::variants& a)
   {
      auto r = result.as<blind_confirmation>( GRAPHENE_MAX_NESTED_OBJECTS );
      std::stringstream ss;
      r.trx.operations[0].visit( operation_printer( ss, *this, operation_result() ) );
      ss << "\n";
      for( const auto& out : r.outputs )
      {
         asset_object a = get_asset( out.decrypted_memo.amount.asset_id );
         ss << a.amount_to_pretty_string( out.decrypted_memo.amount ) << " to  " << out.label << "\n\t  receipt: " << out.confirmation_receipt <<"\n\n";
      }
      return ss.str();
   };
   m["blind_transfer"] = [this](variant result, const fc::variants& a)
   {
      auto r = result.as<blind_confirmation>( GRAPHENE_MAX_NESTED_OBJECTS );
      std::stringstream ss;
      r.trx.operations[0].visit( operation_printer( ss, *this, operation_result() ) );
      ss << "\n";
      for( const auto& out : r.outputs )
      {
         asset_object a = get_asset( out.decrypted_memo.amount.asset_id );
         ss << a.amount_to_pretty_string( out.decrypted_memo.amount ) << " to  " << out.label << "\n\t  receipt: " << out.confirmation_receipt <<"\n\n";
      }
      return ss.str();
   };
   m["receive_blind_transfer"] = [this](variant result, const fc::variants& a)
   {
      auto r = result.as<blind_receipt>( GRAPHENE_MAX_NESTED_OBJECTS );
      std::stringstream ss;
      asset_object as = get_asset( r.amount.asset_id );
      ss << as.amount_to_pretty_string( r.amount ) << "  " << r.from_label << "  =>  " << r.to_label  << "  " << r.memo <<"\n";
      return ss.str();
   };
   m["blind_history"] = [this](variant result, const fc::variants& a)
   {
      auto records = result.as<vector<blind_receipt>>( GRAPHENE_MAX_NESTED_OBJECTS );
      std::stringstream ss;
      ss << "WHEN         "
         << "  " << "AMOUNT"  << "  " << "FROM" << "  =>  " << "TO" << "  " << "MEMO" <<"\n";
      ss << "====================================================================================\n";
      for( auto& r : records )
      {
         asset_object as = get_asset( r.amount.asset_id );
         ss << fc::get_approximate_relative_time_string( r.date )
            << "  " << as.amount_to_pretty_string( r.amount ) << "  " << r.from_label << "  =>  " << r.to_label  << "  " << r.memo <<"\n";
      }
      return ss.str();
   };
   m["get_order_book"] = [](variant result, const fc::variants& a)
   {
      auto orders = result.as<order_book>( GRAPHENE_MAX_NESTED_OBJECTS );
      auto bids = orders.bids;
      auto asks = orders.asks;
      std::stringstream ss;
      std::stringstream sum_stream;
      sum_stream << "Sum(" << orders.base << ')';
      double bid_sum = 0;
      double ask_sum = 0;
      const int spacing = 20;

      auto prettify_num = [&ss]( double n )
      {
         if (abs( round( n ) - n ) < 0.00000000001 )
         {
            ss << (int) n;
         }
         else if (n - floor(n) < 0.000001)
         {
            ss << setiosflags( ios::fixed ) << setprecision(10) << n;
         }
         else
         {
            ss << setiosflags( ios::fixed ) << setprecision(6) << n;
         }
      };
      auto prettify_num_string = [&]( string& num_string )
      {
         double n = fc::to_double( num_string );
         prettify_num( n );
      };

      ss << setprecision( 8 ) << setiosflags( ios::fixed ) << setiosflags( ios::left );

      ss << ' ' << setw( (spacing * 4) + 6 ) << "BUY ORDERS" << "SELL ORDERS\n"
         << ' ' << setw( spacing + 1 ) << "Price" << setw( spacing ) << orders.quote << ' ' << setw( spacing )
         << orders.base << ' ' << setw( spacing ) << sum_stream.str()
         << "   " << setw( spacing + 1 ) << "Price" << setw( spacing ) << orders.quote << ' ' << setw( spacing )
         << orders.base << ' ' << setw( spacing ) << sum_stream.str()
         << "\n====================================================================================="
         << "|=====================================================================================\n";

      for (unsigned int i = 0; i < bids.size() || i < asks.size() ; i++)
      {
         if ( i < bids.size() )
         {
               bid_sum += fc::to_double( bids[i].base );
               ss << ' ' << setw( spacing );
               prettify_num_string( bids[i].price );
               ss << ' ' << setw( spacing );
               prettify_num_string( bids[i].quote );
               ss << ' ' << setw( spacing );
               prettify_num_string( bids[i].base );
               ss << ' ' << setw( spacing );
               prettify_num( bid_sum );
               ss << ' ';
         }
         else
         {
               ss << setw( (spacing * 4) + 5 ) << ' ';
         }

         ss << '|';

         if ( i < asks.size() )
         {
            ask_sum += fc::to_double( asks[i].base );
            ss << ' ' << setw( spacing );
            prettify_num_string( asks[i].price );
            ss << ' ' << setw( spacing );
            prettify_num_string( asks[i].quote );
            ss << ' ' << setw( spacing );
            prettify_num_string( asks[i].base );
            ss << ' ' << setw( spacing );
            prettify_num( ask_sum );
         }

         ss << '\n';
      }

      ss << endl
         << "Buy Total:  " << bid_sum << ' ' << orders.base << endl
         << "Sell Total: " << ask_sum << ' ' << orders.base << endl;

      return ss.str();
   };

   return m;
}

pair<transaction_id_type,signed_transaction> wallet_api_impl::broadcast_transaction(signed_transaction tx)
{
      try {
         _remote_net_broadcast->broadcast_transaction(tx);
      }
      catch (const fc::exception& e) {
         elog("Caught exception while broadcasting tx ${id}:  ${e}", ("id", tx.id().str())("e", e.to_detail_string()));
         throw;
      }
      return std::make_pair(tx.id(),tx);
}

signed_transaction wallet_api_impl::sign_transaction(signed_transaction tx, bool broadcast)
{
   set<public_key_type> pks = _remote_db->get_potential_signatures( tx );
   flat_set<public_key_type> owned_keys;
   owned_keys.reserve( pks.size() );
   std::copy_if( pks.begin(), pks.end(), std::inserter(owned_keys, owned_keys.end()),
                  [this](const public_key_type& pk){ return _keys.find(pk) != _keys.end(); } );
   tx.clear_signatures();
   set<public_key_type> approving_key_set = _remote_db->get_required_signatures( tx, owned_keys );

   auto dyn_props = get_dynamic_global_properties();
   tx.set_reference_block( dyn_props.head_block_id );

   // first, some bookkeeping, expire old items from _recently_generated_transactions
   // since transactions include the head block id, we just need the index for keeping transactions unique
   // when there are multiple transactions in the same block.  choose a time period that should be at
   // least one block long, even in the worst case.  2 minutes ought to be plenty.
   fc::time_point_sec oldest_transaction_ids_to_track(dyn_props.time - fc::minutes(2));
   auto oldest_transaction_record_iter = _recently_generated_transactions.get<timestamp_index>().lower_bound(oldest_transaction_ids_to_track);
   auto begin_iter = _recently_generated_transactions.get<timestamp_index>().begin();
   _recently_generated_transactions.get<timestamp_index>().erase(begin_iter, oldest_transaction_record_iter);

   uint32_t expiration_time_offset = 0;
   for (;;)
   {
      tx.set_expiration( dyn_props.time + fc::seconds(30 + expiration_time_offset) );
      tx.clear_signatures();

      for( const public_key_type& key : approving_key_set )
         tx.sign( get_private_key(key), _chain_id );

      graphene::chain::transaction_id_type this_transaction_id = tx.id();
      auto iter = _recently_generated_transactions.find(this_transaction_id);
      if (iter == _recently_generated_transactions.end())
      {
         // we haven't generated this transaction before, the usual case
         recently_generated_transaction_record this_transaction_record;
         this_transaction_record.generation_time = dyn_props.time;
         this_transaction_record.transaction_id = this_transaction_id;
         _recently_generated_transactions.insert(this_transaction_record);
         break;
      }

      // else we've generated a dupe, increment expiration time and re-sign it
      ++expiration_time_offset;
   }

   if( broadcast )
   {
      try
      {
         _remote_net_broadcast->broadcast_transaction( tx );
      }
      catch (const fc::exception& e)
      {
         elog("Caught exception while broadcasting tx ${id}:  ${e}", ("id", tx.id().str())("e", e.to_detail_string()) );
         throw;
      }
   }

   return tx;
}

void wallet_api_impl::enable_umask_protection()
{
#ifdef __unix__
   _old_umask = umask( S_IRWXG | S_IRWXO );
#endif
}

void wallet_api_impl::disable_umask_protection()
{
#ifdef __unix__
   umask( _old_umask );
#endif
}

string wallet_api_impl::get_wallet_filename() const
{
   return _wallet_filename;
}

bool wallet_api_impl::copy_wallet_file( string destination_filename )
{
   fc::path src_path = get_wallet_filename();
   if( !fc::exists( src_path ) )
      return false;
   fc::path dest_path = destination_filename + _wallet_filename_extension;
   int suffix = 0;
   while( fc::exists(dest_path) )
   {
      ++suffix;
      dest_path = destination_filename + "-" + to_string( suffix ) + _wallet_filename_extension;
   }
   wlog( "backing up wallet ${src} to ${dest}",
         ("src", src_path)
         ("dest", dest_path) );

   fc::path dest_parent = fc::absolute(dest_path).parent_path();
   try
   {
      enable_umask_protection();
      if( !fc::exists( dest_parent ) )
         fc::create_directories( dest_parent );
      fc::copy( src_path, dest_path );
      disable_umask_protection();
   }
   catch(...)
   {
      disable_umask_protection();
      throw;
   }
   return true;
}

fc::ecc::private_key wallet_api_impl::get_private_key(const public_key_type& id)const
{
   auto it = _keys.find(id);
   FC_ASSERT( it != _keys.end() );

   fc::optional< fc::ecc::private_key > privkey = wif_to_key( it->second );
   FC_ASSERT( privkey );
   return *privkey;
}

bool wallet_api_impl::load_wallet_file(string wallet_filename)
{
   // TODO:  Merge imported wallet with existing wallet,
   //        instead of replacing it
   if( wallet_filename == "" )
      wallet_filename = _wallet_filename;

   if( ! fc::exists( wallet_filename ) )
      return false;

   _wallet = fc::json::from_file( wallet_filename ).as< wallet_data >( 2 * GRAPHENE_MAX_NESTED_OBJECTS );
   if( _wallet.chain_id != _chain_id )
      FC_THROW( "Wallet chain ID does not match",
         ("wallet.chain_id", _wallet.chain_id)
         ("chain_id", _chain_id) );

   size_t account_pagination = 100;
   vector< std::string > account_ids_to_send;
   size_t n = _wallet.my_accounts.size();
   account_ids_to_send.reserve( std::min( account_pagination, n ) );
   auto it = _wallet.my_accounts.begin();

   for( size_t start=0; start<n; start+=account_pagination )
   {
      size_t end = std::min( start+account_pagination, n );
      assert( end > start );
      account_ids_to_send.clear();
      std::vector< account_object > old_accounts;
      for( size_t i=start; i<end; i++ )
      {
         assert( it != _wallet.my_accounts.end() );
         old_accounts.push_back( *it );
         std::string account_id = account_id_to_string(old_accounts.back().id);
         account_ids_to_send.push_back( account_id );
         ++it;
      }
      std::vector< optional< account_object > > accounts = _remote_db->get_accounts(account_ids_to_send);
      // server response should be same length as request
      FC_ASSERT( accounts.size() == account_ids_to_send.size() );
      size_t i = 0;
      for( const optional< account_object >& acct : accounts )
      {
         account_object& old_acct = old_accounts[i];
         if( !acct.valid() )
         {
            elog( "Could not find account ${id} : \"${name}\" does not exist on the chain!", ("id", old_acct.id)("name", old_acct.name) );
            i++;
            continue;
         }
         // this check makes sure the server didn't send results
         // in a different order, or accounts we didn't request
         FC_ASSERT( acct->id == old_acct.id );
         if( fc::json::to_string(*acct) != fc::json::to_string(old_acct) )
         {
            wlog( "Account ${id} : \"${name}\" updated on chain", ("id", acct->id)("name", acct->name) );
         }
         _wallet.update_account( *acct );
         i++;
      }
   }

   return true;
}

void wallet_api_impl::save_wallet_file(string wallet_filename)
{
   //
   // Serialize in memory, then save to disk
   //
   // This approach lessens the risk of a partially written wallet
   // if exceptions are thrown in serialization
   //

   encrypt_keys();

   if( wallet_filename == "" )
      wallet_filename = _wallet_filename;

   wlog( "saving wallet to file ${fn}", ("fn", wallet_filename) );

   string data = fc::json::to_pretty_string( _wallet );

   try
   {
      enable_umask_protection();
      //
      // Parentheses on the following declaration fails to compile,
      // due to the Most Vexing Parse.  Thanks, C++
      //
      // http://en.wikipedia.org/wiki/Most_vexing_parse
      //
      std::string tmp_wallet_filename = wallet_filename + ".tmp";
      fc::ofstream outfile{ fc::path( tmp_wallet_filename ) };
      outfile.write( data.c_str(), data.length() );
      outfile.flush();
      outfile.close();

      wlog( "saved successfully wallet to tmp file ${fn}", ("fn", tmp_wallet_filename) );

      std::string wallet_file_content;
      fc::read_file_contents(tmp_wallet_filename, wallet_file_content);

      if (wallet_file_content == data) {
         wlog( "validated successfully tmp wallet file ${fn}", ("fn", tmp_wallet_filename) );

         fc::rename( tmp_wallet_filename, wallet_filename );

         wlog( "renamed successfully tmp wallet file ${fn}", ("fn", tmp_wallet_filename) );
      } 
      else 
      {
         FC_THROW("tmp wallet file cannot be validated ${fn}", ("fn", tmp_wallet_filename) );
      }

      wlog( "successfully saved wallet to file ${fn}", ("fn", wallet_filename) );

      disable_umask_protection();
   }
   catch(...)
   {
      string ws_password = _wallet.ws_password;
      _wallet.ws_password = "";
      wlog("wallet file content is next: ${data}", ("data", fc::json::to_pretty_string( _wallet ) ) );
      _wallet.ws_password = ws_password;

      disable_umask_protection();
      throw;
   }
}

bool wallet_api_impl::is_locked()const
{
   return _checksum == fc::sha512();
}

vector< signed_transaction > wallet_api_impl::import_balance( string name_or_id, const vector<string>& wif_keys, bool broadcast )
{ try {
   FC_ASSERT(!is_locked());
   const dynamic_global_property_object& dpo = _remote_db->get_dynamic_global_properties();
   account_object claimer = get_account( name_or_id );
   uint32_t max_ops_per_tx = 30;

   map< address, private_key_type > keys;  // local index of address -> private key
   vector< address > addrs;
   bool has_wildcard = false;
   addrs.reserve( wif_keys.size() );
   for( const string& wif_key : wif_keys )
   {
      if( wif_key == "*" )
      {
         if( has_wildcard )
            continue;
         for( const public_key_type& pub : _wallet.extra_keys[ claimer.id ] )
         {
            addrs.push_back( pub );
            auto it = _keys.find( pub );
            if( it != _keys.end() )
            {
               fc::optional< fc::ecc::private_key > privkey = wif_to_key( it->second );
               FC_ASSERT( privkey );
               keys[ addrs.back() ] = *privkey;
            }
            else
            {
               wlog( "Somehow _keys has no private key for extra_keys public key ${k}", ("k", pub) );
            }
         }
         has_wildcard = true;
      }
      else
      {
         optional< private_key_type > key = wif_to_key( wif_key );
         FC_ASSERT( key.valid(), "Invalid private key" );
         fc::ecc::public_key pk = key->get_public_key();
         addrs.push_back( pk );
         keys[addrs.back()] = *key;
         // see chain/balance_evaluator.cpp
         addrs.push_back( pts_address( pk, false, 56 ) );
         keys[addrs.back()] = *key;
         addrs.push_back( pts_address( pk, true, 56 ) );
         keys[addrs.back()] = *key;
         addrs.push_back( pts_address( pk, false, 0 ) );
         keys[addrs.back()] = *key;
         addrs.push_back( pts_address( pk, true, 0 ) );
         keys[addrs.back()] = *key;
      }
   }

   vector< balance_object > balances = _remote_db->get_balance_objects( addrs );
   addrs.clear();

   set<asset_id_type> bal_types;
   for( auto b : balances ) bal_types.insert( b.balance.asset_id );

   struct claim_tx
   {
      vector< balance_claim_operation > ops;
      set< address > addrs;
   };
   vector< claim_tx > claim_txs;

   for( const asset_id_type& a : bal_types )
   {
      balance_claim_operation op;
      op.deposit_to_account = claimer.id;
      for( const balance_object& b : balances )
      {
         if( b.balance.asset_id == a )
         {
            op.total_claimed = b.available( dpo.time );
            if( op.total_claimed.amount == 0 )
               continue;
            op.balance_to_claim = b.id;
            op.balance_owner_key = keys[b.owner].get_public_key();
            if( (claim_txs.empty()) || (claim_txs.back().ops.size() >= max_ops_per_tx) )
               claim_txs.emplace_back();
            claim_txs.back().ops.push_back(op);
            claim_txs.back().addrs.insert(b.owner);
         }
      }
   }

   vector< signed_transaction > result;

   for( const claim_tx& ctx : claim_txs )
   {
      signed_transaction tx;
      tx.operations.reserve( ctx.ops.size() );
      for( const balance_claim_operation& op : ctx.ops )
         tx.operations.emplace_back( op );
      set_operation_fees( tx, _remote_db->get_global_properties().parameters.current_fees );
      tx.validate();
      signed_transaction signed_tx = sign_transaction( tx, false );
      for( const address& addr : ctx.addrs )
         signed_tx.sign( keys[addr], _chain_id );
      // if the key for a balance object was the same as a key for the account we're importing it into,
      // we may end up with duplicate signatures, so remove those
      boost::erase(signed_tx.signatures, boost::unique<boost::return_found_end>(boost::sort(signed_tx.signatures)));
      result.push_back( signed_tx );
      if( broadcast )
         _remote_net_broadcast->broadcast_transaction(signed_tx);
   }

   return result;
} FC_CAPTURE_AND_RETHROW( (name_or_id) ) }

void wallet_api_impl::quit()
{
   ilog( "Quitting Cli Wallet ..." );
   
   throw fc::canceled_exception();
}

}}} // graphene::wallet::detail
