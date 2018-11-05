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

void wallet_api_impl::use_debug_api()
{
   if( _remote_debug )
      return;
   try
   {
      _remote_debug = _remote_api->debug();
   }
   catch( const fc::exception& e )
   {
      std::cerr << "\nCouldn't get debug node API.  You probably are not configured\n"
      "to access the debug API on the node you are connecting to.\n"
      "\n"
      "To fix this problem:\n"
      "- Please ensure you are running debug_node, not witness_node.\n"
      "- Please follow the instructions in README.md to set up an apiaccess file.\n"
      "\n";
   }
}

void wallet_api_impl::dbg_make_uia(string creator, string symbol)
{
   asset_options opts;
   opts.flags &= ~(white_list | disable_force_settle | global_settle);
   opts.issuer_permissions = opts.flags;
   opts.core_exchange_rate = price(asset(1), asset(1,asset_id_type(1)));
   create_asset(get_account(creator).name, symbol, 2, opts, {}, true);
}

void wallet_api_impl::dbg_make_mia(string creator, string symbol)
{
   asset_options opts;
   opts.flags &= ~white_list;
   opts.issuer_permissions = opts.flags;
   opts.core_exchange_rate = price(asset(1), asset(1,asset_id_type(1)));
   bitasset_options bopts;
   create_asset(get_account(creator).name, symbol, 2, opts, bopts, true);
}

void wallet_api_impl::dbg_push_blocks( const std::string& src_filename, uint32_t count )
{
   use_debug_api();
   (*_remote_debug)->debug_push_blocks( src_filename, count );
   (*_remote_debug)->debug_stream_json_objects_flush();
}

void wallet_api_impl::dbg_generate_blocks( const std::string& debug_wif_key, uint32_t count )
{
   use_debug_api();
   (*_remote_debug)->debug_generate_blocks( debug_wif_key, count );
   (*_remote_debug)->debug_stream_json_objects_flush();
}

void wallet_api_impl::dbg_stream_json_objects( const std::string& filename )
{
   use_debug_api();
   (*_remote_debug)->debug_stream_json_objects( filename );
   (*_remote_debug)->debug_stream_json_objects_flush();
}

void wallet_api_impl::dbg_update_object( const fc::variant_object& update )
{
   use_debug_api();
   (*_remote_debug)->debug_update_object( update );
   (*_remote_debug)->debug_stream_json_objects_flush();
}

void wallet_api_impl::flood_network(string prefix, uint32_t number_of_transactions)
{
   try
   {
      const account_object& master = *_wallet.my_accounts.get<by_name>().lower_bound("import");
      int number_of_accounts = number_of_transactions / 3;
      number_of_transactions -= number_of_accounts;
      try {
         dbg_make_uia(master.name, "SHILL");
      } catch(...) {/* Ignore; the asset probably already exists.*/}

      fc::time_point start = fc::time_point::now();
      for( int i = 0; i < number_of_accounts; ++i )
      {
         std::ostringstream brain_key;
         brain_key << "brain key for account " << prefix << i;
         signed_transaction trx = create_account_with_brain_key(brain_key.str(), prefix + fc::to_string(i), master.name, master.name, /* broadcast = */ true, /* save wallet = */ false);
      }
      fc::time_point end = fc::time_point::now();
      ilog("Created ${n} accounts in ${time} milliseconds",
            ("n", number_of_accounts)("time", (end - start).count() / 1000));

      start = fc::time_point::now();
      for( int i = 0; i < number_of_accounts; ++i )
      {
         signed_transaction trx = transfer(master.name, prefix + fc::to_string(i), "10", "CORE", "", true);
         trx = transfer(master.name, prefix + fc::to_string(i), "1", "CORE", "", true);
      }
      end = fc::time_point::now();
      ilog("Transferred to ${n} accounts in ${time} milliseconds",
            ("n", number_of_accounts*2)("time", (end - start).count() / 1000));

      start = fc::time_point::now();
      for( int i = 0; i < number_of_accounts; ++i )
      {
         signed_transaction trx = issue_asset(prefix + fc::to_string(i), "1000", "SHILL", "", true);
      }
      end = fc::time_point::now();
      ilog("Issued to ${n} accounts in ${time} milliseconds",
            ("n", number_of_accounts)("time", (end - start).count() / 1000));
   }
   catch (...)
   {
      throw;
   }
}

}}} // graphene::wallet::detail
