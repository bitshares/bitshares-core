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
#include <boost/algorithm/string/replace.hpp>

#include <fc/rpc/api_connection.hpp>
#include <fc/popcount.hpp>
#include <fc/git_revision.hpp>

#include <graphene/wallet/wallet.hpp>
#include <graphene/wallet/wallet_api_impl.hpp>
#include <graphene/utilities/git_revision.hpp>

// explicit instantiation for later use
namespace fc {
	template class api<graphene::wallet::wallet_api, identity_member_with_optionals>;
}

/****
 * General methods for wallet impl object (ctor, info, about, etc.)
 */

namespace graphene { namespace wallet { namespace detail {

   wallet_api_impl::wallet_api_impl( wallet_api& s, const wallet_data& initial_data, fc::api<login_api> rapi )
      : self(s),
        _chain_id(initial_data.chain_id),
        _remote_api(rapi),
        _remote_db(rapi->database()),
        _remote_net_broadcast(rapi->network_broadcast()),
        _remote_hist(rapi->history()),
        _custom_operations(rapi->custom())
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

   fc::variant wallet_api_impl::info() const
   {
      auto chain_props = get_chain_properties();
      auto global_props = get_global_properties();
      auto dynamic_props = get_dynamic_global_properties();
      fc::mutable_variant_object result;
      result["head_block_num"] = dynamic_props.head_block_number;
      result["head_block_id"] = fc::variant(dynamic_props.head_block_id, 1);
      result["head_block_age"] = fc::get_approximate_relative_time_string(dynamic_props.time,
                                                                          time_point_sec(time_point::now()),
                                                                          " old");
      result["next_maintenance_time"] =
            fc::get_approximate_relative_time_string(dynamic_props.next_maintenance_time);
      result["chain_id"] = chain_props.chain_id;
      stringstream participation;
      participation << fixed << std::setprecision(2) << (100.0*fc::popcount(dynamic_props.recent_slots_filled)) / 128.0;
      result["participation"] = participation.str();
      result["active_witnesses"] = fc::variant(global_props.active_witnesses, GRAPHENE_MAX_NESTED_OBJECTS);
      result["active_committee_members"] =
            fc::variant(global_props.active_committee_members, GRAPHENE_MAX_NESTED_OBJECTS);
      return result;
   }

   /***
    * @brief return basic information about this program
    */
   fc::variant_object wallet_api_impl::about() const
   {
      string client_version( graphene::utilities::git_revision_description );
      const size_t pos = client_version.find( '/' );
      if( pos != string::npos && client_version.size() > pos )
         client_version = client_version.substr( pos + 1 );

      fc::mutable_variant_object result;
      //result["blockchain_name"]        = BLOCKCHAIN_NAME;
      //result["blockchain_description"] = BTS_BLOCKCHAIN_DESCRIPTION;
      result["client_version"]           = client_version;
      result["graphene_revision"]        = graphene::utilities::git_revision_sha;
      result["graphene_revision_age"]    = fc::get_approximate_relative_time_string( fc::time_point_sec(
                                                 graphene::utilities::git_revision_unix_timestamp ) );
      result["fc_revision"]              = fc::git_revision_sha;
      result["fc_revision_age"]          = fc::get_approximate_relative_time_string( fc::time_point_sec(
                                                 fc::git_revision_unix_timestamp ) );
      result["compile_date"]             = "compiled on " __DATE__ " at " __TIME__;
      result["boost_version"]            = boost::replace_all_copy(std::string(BOOST_LIB_VERSION), "_", ".");
      result["openssl_version"]          = OPENSSL_VERSION_TEXT;

      std::string bitness = boost::lexical_cast<std::string>(8 * sizeof(int*)) + "-bit";
#if defined(__APPLE__)
      std::string os = "osx";
#elif defined(__linux__)
      std::string os = "linux";
#elif defined(_MSC_VER)
      std::string os = "win32";
#else
      std::string os = "other";
#endif
      result["build"] = os + " " + bitness;

      return result;
   }

   void wallet_api_impl::quit()
   {
      ilog( "Quitting Cli Wallet ..." );

      throw fc::canceled_exception();
   }

   pair<transaction_id_type,signed_transaction> wallet_api_impl::broadcast_transaction(signed_transaction tx)
   {
       try {
           _remote_net_broadcast->broadcast_transaction(tx);
       }
       catch (const fc::exception& e) {
           elog("Caught exception while broadcasting tx ${id}:  ${e}",
                ("id", tx.id().str())("e", e.to_detail_string()));
           throw;
       }
       return std::make_pair(tx.id(),tx);
   }   

   chain_property_object wallet_api_impl::get_chain_properties() const
   {
      return _remote_db->get_chain_properties();
   }
   global_property_object wallet_api_impl::get_global_properties() const
   {
      return _remote_db->get_global_properties();
   }
   dynamic_global_property_object wallet_api_impl::get_dynamic_global_properties() const
   {
      return _remote_db->get_dynamic_global_properties();
   }

   void wallet_api_impl::on_block_applied( const variant& block_id )
   {
      fc::async([this]{resync();}, "Resync after block");
   }

}}} // namespace graphene::wallet::detail