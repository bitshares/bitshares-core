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

void wallet_api_impl::use_network_node_api()
{
   if( _remote_net_node )
      return;
   try
   {
      _remote_net_node = _remote_api->network_node();
   }
   catch( const fc::exception& e )
   {
      std::cerr << "\nCouldn't get network node API.  You probably are not configured\n"
      "to access the network API on the witness_node you are\n"
      "connecting to.  Please follow the instructions in README.md to set up an apiaccess file.\n"
      "\n";
      throw;
   }
}

void wallet_api_impl::network_add_nodes( const vector<string>& nodes )
{
   use_network_node_api();
   for( const string& node_address : nodes )
   {
      (*_remote_net_node)->add_node( fc::ip::endpoint::from_string( node_address ) );
   }
}

vector< variant > wallet_api_impl::network_get_connected_peers()
{
   use_network_node_api();
   const auto peers = (*_remote_net_node)->get_connected_peers();
   vector< variant > result;
   result.reserve( peers.size() );
   for( const auto& peer : peers )
   {
      variant v;
      fc::to_variant( peer, v, GRAPHENE_MAX_NESTED_OBJECTS );
      result.push_back( v );
   }
   return result;
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
