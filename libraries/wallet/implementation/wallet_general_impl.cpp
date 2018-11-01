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

namespace detail {

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
