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
#include <algorithm>
#include <cctype>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <sstream>
#include <string>
#include <list>

#include <boost/version.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/algorithm/string/replace.hpp>

#include <boost/range/adaptor/map.hpp>
#include <boost/range/algorithm_ext/erase.hpp>
#include <boost/range/algorithm/unique.hpp>
#include <boost/range/algorithm/sort.hpp>

#include <boost/multi_index_container.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/mem_fun.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/random_access_index.hpp>
#include <boost/multi_index/tag.hpp>
#include <boost/multi_index/sequenced_index.hpp>
#include <boost/multi_index/hashed_index.hpp>

#include <fc/git_revision.hpp>
#include <fc/io/fstream.hpp>
#include <fc/io/json.hpp>
#include <fc/io/stdio.hpp>
#include <fc/network/http/websocket.hpp>
#include <fc/rpc/cli.hpp>
#include <fc/rpc/websocket_api.hpp>
#include <fc/crypto/aes.hpp>
#include <fc/crypto/hex.hpp>
#include <fc/thread/mutex.hpp>
#include <fc/thread/scoped_lock.hpp>
#include <fc/rpc/api_connection.hpp>

#include <graphene/app/api.hpp>
#include <graphene/chain/asset_object.hpp>
#include <graphene/chain/protocol/fee_schedule.hpp>
#include <graphene/chain/hardfork.hpp>
#include <graphene/utilities/git_revision.hpp>
#include <graphene/utilities/key_conversion.hpp>
#include <graphene/utilities/words.hpp>
#include <graphene/wallet/wallet.hpp>
#include <graphene/wallet/wallet_impl.hpp>
#include <graphene/wallet/api_documentation.hpp>
#include <graphene/wallet/reflect_util.hpp>
#include <graphene/debug_witness/debug_api.hpp>
#include <fc/smart_ref_impl.hpp>

#ifndef WIN32
# include <sys/types.h>
# include <sys/stat.h>
#endif

#include "wallet_account.cpp"
#include "wallet_asset.cpp"
#include "wallet_blockchain_inspection.cpp"
#include "wallet_general.cpp"
#include "wallet_governance.cpp"
#include "wallet_stealth.cpp"
#include "wallet_trading.cpp"
#include "wallet_transaction_builder.cpp"
#include "wallet_debug.cpp"
#include "wallet_network.cpp"
#include "wallet_help.cpp"

// explicit instantiation for later use
namespace fc {
	template class api<graphene::wallet::wallet_api, identity_member>;
}

namespace graphene { namespace wallet {

namespace detail {

struct op_prototype_visitor
{
   typedef void result_type;

   int t = 0;
   flat_map< std::string, operation >& name2op;

   op_prototype_visitor(
      int _t,
      flat_map< std::string, operation >& _prototype_ops
      ):t(_t), name2op(_prototype_ops) {}

   template<typename Type>
   result_type operator()( const Type& op )const
   {
      string name = fc::get_typename<Type>::name();
      size_t p = name.rfind(':');
      if( p != string::npos )
         name = name.substr( p+1 );
      name2op[ name ] = Type();
   }
};

void wallet_api_impl::init_prototype_ops()
{
   operation op;
   for( int t=0; t<op.count(); t++ )
   {
      op.set_which( t );
      op.visit( op_prototype_visitor(t, _prototype_ops) );
   }
   return;
}

wallet_api_impl::wallet_api_impl( wallet_api& s, const wallet_data& initial_data, fc::api<login_api> rapi )
   : self(s),
      _chain_id(initial_data.chain_id),
      _remote_api(rapi),
      _remote_db(rapi->database()),
      _remote_net_broadcast(rapi->network_broadcast()),
      _remote_hist(rapi->history())
{
   chain_id_type remote_chain_id = _remote_db->get_chain_id();
   if( remote_chain_id != _chain_id )
   {
      FC_THROW( "Remote server gave us an unexpected chain_id",
         ("remote_chain_id", remote_chain_id)
         ("chain_id", _chain_id) );
   }
   init_prototype_ops();

   _remote_db->set_block_applied_callback( [this](const variant& block_id )
   {
      on_block_applied( block_id );
   } );

   _wallet.chain_id = _chain_id;
   _wallet.ws_server = initial_data.ws_server;
   _wallet.ws_user = initial_data.ws_user;
   _wallet.ws_password = initial_data.ws_password;
}
wallet_api_impl::~wallet_api_impl()
{
   try
   {
      _remote_db->cancel_all_subscriptions();
   }
   catch (const fc::exception& e)
   {
      // Right now the wallet_api has no way of knowing if the connection to the
      // witness has already disconnected (via the witness node exiting first).
      // If it has exited, cancel_all_subscriptsions() will throw and there's
      // nothing we can do about it.
      // dlog("Caught exception ${e} while canceling database subscriptions", ("e", e));
   }
}

operation wallet_api_impl::get_prototype_operation( string operation_name )
{
   auto it = _prototype_ops.find( operation_name );
   if( it == _prototype_ops.end() )
      FC_THROW("Unsupported operation: \"${operation_name}\"", ("operation_name", operation_name));
   return it->second;
}

}}} // graphene::wallet::detail

namespace graphene { namespace wallet {

wallet_api::wallet_api(const wallet_data& initial_data, fc::api<login_api> rapi)
   : my(new detail::wallet_api_impl(*this, initial_data, rapi))
{
}

wallet_api::~wallet_api()
{
}

operation wallet_api::get_prototype_operation(string operation_name)
{
   return my->get_prototype_operation( operation_name );
}

} } // graphene::wallet

namespace fc {
   void to_variant( const account_multi_index_type& accts, variant& vo, uint32_t max_depth )
   {
      to_variant( std::vector<account_object>(accts.begin(), accts.end()), vo, max_depth );
   }

   void from_variant( const variant& var, account_multi_index_type& vo, uint32_t max_depth )
   {
      const std::vector<account_object>& v = var.as<std::vector<account_object>>( max_depth );
      vo = account_multi_index_type(v.begin(), v.end());
   }
}
