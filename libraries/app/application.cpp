/*
 * Copyright (c) 2015 Cryptonomex, Inc., and contributors.
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
#include <graphene/app/api.hpp>
#include <graphene/app/api_access.hpp>
#include <graphene/app/application.hpp>
#include <graphene/app/plugin.hpp>

#include <graphene/chain/genesis_state.hpp>
#include <graphene/chain/protocol/fee_schedule.hpp>
#include <graphene/chain/protocol/types.hpp>

#include <graphene/egenesis/egenesis.hpp>

#include <graphene/net/core_messages.hpp>
#include <graphene/net/exceptions.hpp>

#include <graphene/utilities/key_conversion.hpp>
#include <graphene/chain/worker_evaluator.hpp>

#include <fc/smart_ref_impl.hpp>

#include <fc/io/fstream.hpp>
#include <fc/rpc/api_connection.hpp>
#include <fc/rpc/websocket_api.hpp>
#include <fc/network/resolve.hpp>
#include <fc/crypto/base64.hpp>

#include <boost/filesystem/path.hpp>
#include <boost/signals2.hpp>
#include <boost/range/algorithm/reverse.hpp>
#include <boost/algorithm/string.hpp>

#include <iostream>

#include <fc/log/file_appender.hpp>
#include <fc/log/logger.hpp>
#include <fc/log/logger_config.hpp>

#include <boost/range/adaptor/reversed.hpp>

namespace graphene { namespace app {
using net::item_hash_t;
using net::item_id;
using net::message;
using net::block_message;
using net::trx_message;

using chain::block_header;
using chain::signed_block_header;
using chain::signed_block;
using chain::block_id_type;

using std::vector;

namespace bpo = boost::program_options;

namespace detail {

   graphene::chain::genesis_state_type create_example_genesis() {
      auto nathan_key = fc::ecc::private_key::regenerate(fc::sha256::hash(string("nathan")));
      dlog("Allocating all stake to ${key}", ("key", utilities::key_to_wif(nathan_key)));
      graphene::chain::genesis_state_type initial_state;
      initial_state.initial_parameters.current_fees = fee_schedule::get_default();//->set_all_fees(GRAPHENE_BLOCKCHAIN_PRECISION);
      initial_state.initial_active_witnesses = GRAPHENE_DEFAULT_MIN_WITNESS_COUNT;
      initial_state.initial_timestamp = time_point_sec(time_point::now().sec_since_epoch() /
            initial_state.initial_parameters.block_interval *
            initial_state.initial_parameters.block_interval);
      for( uint64_t i = 0; i < initial_state.initial_active_witnesses; ++i )
      {
         auto name = "init"+fc::to_string(i);
         initial_state.initial_accounts.emplace_back(name,
                                                     nathan_key.get_public_key(),
                                                     nathan_key.get_public_key(),
                                                     true);
         initial_state.initial_committee_candidates.push_back({name});
         initial_state.initial_witness_candidates.push_back({name, nathan_key.get_public_key()});
      }

      initial_state.initial_accounts.emplace_back("nathan", nathan_key.get_public_key());
      initial_state.initial_balances.push_back({nathan_key.get_public_key(),
                                                GRAPHENE_SYMBOL,
                                                GRAPHENE_MAX_SHARE_SUPPLY});
      initial_state.initial_chain_id = fc::sha256::hash( "BOGUS" );

      return initial_state;
   }


}

}}

#include "application_impl.hxx"

namespace graphene { namespace app { namespace detail {

void application_impl::reset_p2p_node(const fc::path& data_dir)
{ try {
   _p2p_network = std::make_shared<net::node>("BitShares Reference Implementation");

   _p2p_network->load_configuration(data_dir / "p2p");
   _p2p_network->set_node_delegate(this);

   if( _options->count("seed-node") )
   {
      auto seeds = _options->at("seed-node").as<vector<string>>();
      for( const string& endpoint_string : seeds )
      {
         try {
            std::vector<fc::ip::endpoint> endpoints = resolve_string_to_ip_endpoints(endpoint_string);
            for (const fc::ip::endpoint& endpoint : endpoints)
            {
               ilog("Adding seed node ${endpoint}", ("endpoint", endpoint));
               _p2p_network->add_node(endpoint);
               _p2p_network->connect_to_endpoint(endpoint);
            }
         } catch( const fc::exception& e ) {
            wlog( "caught exception ${e} while adding seed node ${endpoint}",
                     ("e", e.to_detail_string())("endpoint", endpoint_string) );
         }
      }
   }

   if( _options->count("seed-nodes") )
   {
      auto seeds_str = _options->at("seed-nodes").as<string>();
      auto seeds = fc::json::from_string(seeds_str).as<vector<string>>(2);
      for( const string& endpoint_string : seeds )
      {
         try {
            std::vector<fc::ip::endpoint> endpoints = resolve_string_to_ip_endpoints(endpoint_string);
            for (const fc::ip::endpoint& endpoint : endpoints)
            {
               ilog("Adding seed node ${endpoint}", ("endpoint", endpoint));
               _p2p_network->add_node(endpoint);
            }
         } catch( const fc::exception& e ) {
            wlog( "caught exception ${e} while adding seed node ${endpoint}",
                     ("e", e.to_detail_string())("endpoint", endpoint_string) );
         }
      }
   }
   else
   {
      // https://bitsharestalk.org/index.php/topic,23715.0.html
      vector<string> seeds = {
         "104.236.144.84:1777",               // puppies      (USA)
         "128.199.143.47:2015",               // Harvey       (Singapore)
         "23.92.53.182:1776",                 // sahkan       (USA)
         "192.121.166.162:1776",              // sahkan       (UK)
         "51.15.61.160:1776",                 // lafona       (France)
         "bts-seed1.abit-more.com:62015",     // abit         (China)
         "node.blckchnd.com:4243",            // blckchnd     (Germany)
         "seed.bitsharesdex.com:50696",       // iHashFury    (Europe)
         "seed.bitsharesnodes.com:1776",      // wackou       (Netherlands)
         "seed.blocktrades.us:1776",          // BlockTrades  (USA)
         "seed.cubeconnex.com:1777",          // cube         (USA)
         "seed.roelandp.nl:1776",             // roelandp     (Canada)
         "seed04.bts-nodes.net:1776",         // Thom         (Australia)
         "seed05.bts-nodes.net:1776",          // Thom         (USA)
         "seed06.bts-nodes.net:1776",          // Thom         (USA)
         "seed07.bts-nodes.net:1776",          // Thom         (Singapore)
         "seeds.bitshares.eu:1776"            // pc           (http://seeds.quisquis.de/bitshares.html)
      };
      for( const string& endpoint_string : seeds )
      {
         try {
            std::vector<fc::ip::endpoint> endpoints = resolve_string_to_ip_endpoints(endpoint_string);
            for (const fc::ip::endpoint& endpoint : endpoints)
            {
               ilog("Adding seed node ${endpoint}", ("endpoint", endpoint));
               _p2p_network->add_node(endpoint);
            }
         } catch( const fc::exception& e ) {
            wlog( "caught exception ${e} while adding seed node ${endpoint}",
                     ("e", e.to_detail_string())("endpoint", endpoint_string) );
         }
      }
   }

   if( _options->count("p2p-endpoint") )
      _p2p_network->listen_on_endpoint(fc::ip::endpoint::from_string(_options->at("p2p-endpoint").as<string>()), true);
   else
      _p2p_network->listen_on_port(0, false);
   _p2p_network->listen_to_p2p_network();
   ilog("Configured p2p node to listen on ${ip}", ("ip", _p2p_network->get_actual_listening_endpoint()));

   _p2p_network->connect_to_p2p_network();
   _p2p_network->sync_from(net::item_id(net::core_message_type_enum::block_message_type,
                                        _chain_db->head_block_id()),
                           std::vector<uint32_t>());
} FC_CAPTURE_AND_RETHROW() }

std::vector<fc::ip::endpoint> application_impl::resolve_string_to_ip_endpoints(const std::string& endpoint_string)
{
   try
   {
      string::size_type colon_pos = endpoint_string.find(':');
      if (colon_pos == std::string::npos)
         FC_THROW("Missing required port number in endpoint string \"${endpoint_string}\"",
                  ("endpoint_string", endpoint_string));
      std::string port_string = endpoint_string.substr(colon_pos + 1);
      try
      {
         uint16_t port = boost::lexical_cast<uint16_t>(port_string);

         std::string hostname = endpoint_string.substr(0, colon_pos);
         std::vector<fc::ip::endpoint> endpoints = fc::resolve(hostname, port);
         if (endpoints.empty())
            FC_THROW_EXCEPTION( fc::unknown_host_exception,
                                "The host name can not be resolved: ${hostname}",
                                ("hostname", hostname) );
         return endpoints;
      }
      catch (const boost::bad_lexical_cast&)
      {
         FC_THROW("Bad port: ${port}", ("port", port_string));
      }
   }
   FC_CAPTURE_AND_RETHROW((endpoint_string))
}


void application_impl::new_connection( const fc::http::websocket_connection_ptr& c )
{
   auto wsc = std::make_shared<fc::rpc::websocket_api_connection>(*c, GRAPHENE_NET_MAX_NESTED_OBJECTS);
   auto login = std::make_shared<graphene::app::login_api>( std::ref(*_self) );
   login->enable_api("database_api");

   wsc->register_api(login->database());
   wsc->register_api(fc::api<graphene::app::login_api>(login));
   c->set_session_data( wsc );

   std::string username = "*";
   std::string password = "*";

    // Try to extract login information from "Authorization" header if present
   std::string auth = c->get_request_header("Authorization");
   if( boost::starts_with(auth, "Basic ") ) {

      FC_ASSERT( auth.size() > 6 );
      auto user_pass = fc::base64_decode(auth.substr(6));

      std::vector<std::string> parts;
      boost::split( parts, user_pass, boost::is_any_of(":") );

      FC_ASSERT(parts.size() == 2);

      username = parts[0];
      password = parts[1];
   }

   login->login(username, password);
}

void application_impl::reset_websocket_server()
{ try {
   if( !_options->count("rpc-endpoint") )
      return;

   _websocket_server = std::make_shared<fc::http::websocket_server>();
   _websocket_server->on_connection( std::bind(&application_impl::new_connection, this, std::placeholders::_1) );

   ilog("Configured websocket rpc to listen on ${ip}", ("ip",_options->at("rpc-endpoint").as<string>()));
   _websocket_server->listen( fc::ip::endpoint::from_string(_options->at("rpc-endpoint").as<string>()) );
   _websocket_server->start_accept();
} FC_CAPTURE_AND_RETHROW() }

void application_impl::reset_websocket_tls_server()
{ try {
   if( !_options->count("rpc-tls-endpoint") )
      return;
   if( !_options->count("server-pem") )
   {
      wlog( "Please specify a server-pem to use rpc-tls-endpoint" );
      return;
   }

   string password = _options->count("server-pem-password") ? _options->at("server-pem-password").as<string>() : "";
   _websocket_tls_server = std::make_shared<fc::http::websocket_tls_server>( _options->at("server-pem").as<string>(), password );
   _websocket_tls_server->on_connection( std::bind(&application_impl::new_connection, this, std::placeholders::_1) );

   ilog("Configured websocket TLS rpc to listen on ${ip}", ("ip",_options->at("rpc-tls-endpoint").as<string>()));
   _websocket_tls_server->listen( fc::ip::endpoint::from_string(_options->at("rpc-tls-endpoint").as<string>()) );
   _websocket_tls_server->start_accept();
} FC_CAPTURE_AND_RETHROW() }

void application_impl::set_dbg_init_key( graphene::chain::genesis_state_type& genesis, const std::string& init_key )
{
   flat_set< std::string > initial_witness_names;
   public_key_type init_pubkey( init_key );
   for( uint64_t i=0; i<genesis.initial_active_witnesses; i++ )
      genesis.initial_witness_candidates[i].block_signing_key = init_pubkey;
}

void application_impl::startup()
{ try {
   fc::create_directories(_data_dir / "blockchain");

   auto initial_state = [this] {
      ilog("Initializing database...");
      if( _options->count("genesis-json") )
      {
         std::string genesis_str;
         fc::read_file_contents( _options->at("genesis-json").as<boost::filesystem::path>(), genesis_str );
         graphene::chain::genesis_state_type genesis = fc::json::from_string( genesis_str ).as<graphene::chain::genesis_state_type>( 20 );
         bool modified_genesis = false;
         if( _options->count("genesis-timestamp") )
         {
            genesis.initial_timestamp = fc::time_point_sec( fc::time_point::now() )
                                      + genesis.initial_parameters.block_interval
                                      + _options->at("genesis-timestamp").as<uint32_t>();
            genesis.initial_timestamp -= ( genesis.initial_timestamp.sec_since_epoch()
                                           % genesis.initial_parameters.block_interval );
            modified_genesis = true;
            std::cerr << "Used genesis timestamp:  " << genesis.initial_timestamp.to_iso_string()
                      << " (PLEASE RECORD THIS)\n";
         }
         if( _options->count("dbg-init-key") )
         {
            std::string init_key = _options->at( "dbg-init-key" ).as<string>();
            FC_ASSERT( genesis.initial_witness_candidates.size() >= genesis.initial_active_witnesses );
            set_dbg_init_key( genesis, init_key );
            modified_genesis = true;
            std::cerr << "Set init witness key to " << init_key << "\n";
         }
         if( modified_genesis )
         {
            std::cerr << "WARNING:  GENESIS WAS MODIFIED, YOUR CHAIN ID MAY BE DIFFERENT\n";
            genesis_str += "BOGUS";
            genesis.initial_chain_id = fc::sha256::hash( genesis_str );
         }
         else
            genesis.initial_chain_id = fc::sha256::hash( genesis_str );
         return genesis;
      }
      else
      {
         std::string egenesis_json;
         graphene::egenesis::compute_egenesis_json( egenesis_json );
         FC_ASSERT( egenesis_json != "" );
         FC_ASSERT( graphene::egenesis::get_egenesis_json_hash() == fc::sha256::hash( egenesis_json ) );
         auto genesis = fc::json::from_string( egenesis_json ).as<graphene::chain::genesis_state_type>( 20 );
         genesis.initial_chain_id = fc::sha256::hash( egenesis_json );
         return genesis;
      }
   };

   if( _options->count("resync-blockchain") )
      _chain_db->wipe(_data_dir / "blockchain", true);

   flat_map<uint32_t,block_id_type> loaded_checkpoints;
   if( _options->count("checkpoint") )
   {
      auto cps = _options->at("checkpoint").as<vector<string>>();
      loaded_checkpoints.reserve( cps.size() );
      for( auto cp : cps )
      {
         auto item = fc::json::from_string(cp).as<std::pair<uint32_t,block_id_type> >( 2 );
         loaded_checkpoints[item.first] = item.second;
      }
   }
   _chain_db->add_checkpoints( loaded_checkpoints );

   if( _options->count("replay-blockchain") )
      _chain_db->wipe( _data_dir / "blockchain", false );

   try
   {
      _chain_db->open( _data_dir / "blockchain", initial_state, GRAPHENE_CURRENT_DB_VERSION );
   }
   catch( const fc::exception& e )
   {
      elog( "Caught exception ${e} in open(), you might want to force a replay", ("e", e.to_detail_string()) );
      throw;
   }

   if( _options->count("force-validate") )
   {
      ilog( "All transaction signatures will be validated" );
      _force_validate = true;
   }

   // TODO uncomment this when GUI is ready
   //if( _options->count("enable-subscribe-to-all") )
   //   _app_options.enable_subscribe_to_all = _options->at("enable-subscribe-to-all").as<bool>();

   if( _active_plugins.find( "market_history" ) != _active_plugins.end() )
      _app_options.has_market_history_plugin = true;

   if( _options->count("api-access") ) {

      if(fc::exists(_options->at("api-access").as<boost::filesystem::path>()))
      {
         _apiaccess = fc::json::from_file( _options->at("api-access").as<boost::filesystem::path>() ).as<api_access>( 20 );
         ilog( "Using api access file from ${path}",
               ("path", _options->at("api-access").as<boost::filesystem::path>().string()) );
      }
      else
      {
         elog("Failed to load file from ${path}",
            ("path", _options->at("api-access").as<boost::filesystem::path>().string()));
         std::exit(EXIT_FAILURE);
      }
   }
   else
   {
      // TODO:  Remove this generous default access policy
      // when the UI logs in properly
      _apiaccess = api_access();
      api_access_info wild_access;
      wild_access.password_hash_b64 = "*";
      wild_access.password_salt_b64 = "*";
      wild_access.allowed_apis.push_back( "database_api" );
      wild_access.allowed_apis.push_back( "network_broadcast_api" );
      wild_access.allowed_apis.push_back( "history_api" );
      wild_access.allowed_apis.push_back( "crypto_api" );
      wild_access.allowed_apis.push_back( "orders_api" );
      _apiaccess.permission_map["*"] = wild_access;
   }

   reset_p2p_node(_data_dir);
   reset_websocket_server();
   reset_websocket_tls_server();
} FC_LOG_AND_RETHROW() }

optional< api_access_info > application_impl::get_api_access_info(const string& username)const
{
   optional< api_access_info > result;
   auto it = _apiaccess.permission_map.find(username);
   if( it == _apiaccess.permission_map.end() )
   {
      it = _apiaccess.permission_map.find("*");
      if( it == _apiaccess.permission_map.end() )
         return result;
   }
   return it->second;
}

void application_impl::set_api_access_info(const string& username, api_access_info&& permissions)
{
   _apiaccess.permission_map.insert(std::make_pair(username, std::move(permissions)));
}

/**
 * If delegate has the item, the network has no need to fetch it.
 */
bool application_impl::has_item(const net::item_id& id)
{
   try
   {
      if( id.item_type == graphene::net::block_message_type )
         return _chain_db->is_known_block(id.item_hash);
      else
         return _chain_db->is_known_transaction(id.item_hash);
   }
   FC_CAPTURE_AND_RETHROW( (id) )
}

/**
 * @brief allows the application to validate an item prior to broadcasting to peers.
 *
 * @param sync_mode true if the message was fetched through the sync process, false during normal operation
 * @returns true if this message caused the blockchain to switch forks, false if it did not
 *
 * @throws exception if error validating the item, otherwise the item is safe to broadcast on.
 */
bool application_impl::handle_block(const graphene::net::block_message& blk_msg, bool sync_mode,
                          std::vector<fc::uint160_t>& contained_transaction_message_ids)
{ try {

   auto latency = fc::time_point::now() - blk_msg.block.timestamp;
   if (!sync_mode || blk_msg.block.block_num() % 10000 == 0)
   {
      const auto& witness = blk_msg.block.witness(*_chain_db);
      const auto& witness_account = witness.witness_account(*_chain_db);
      auto last_irr = _chain_db->get_dynamic_global_properties().last_irreversible_block_num;
      ilog("Got block: #${n} time: ${t} latency: ${l} ms from: ${w}  irreversible: ${i} (-${d})",
           ("t",blk_msg.block.timestamp)
           ("n", blk_msg.block.block_num())
           ("l", (latency.count()/1000))
           ("w",witness_account.name)
           ("i",last_irr)("d",blk_msg.block.block_num()-last_irr) );
   }
   FC_ASSERT( (latency.count()/1000) > -5000, "Rejecting block with timestamp in the future" );

   try {
      // TODO: in the case where this block is valid but on a fork that's too old for us to switch to,
      // you can help the network code out by throwing a block_older_than_undo_history exception.
      // when the net code sees that, it will stop trying to push blocks from that chain, but
      // leave that peer connected so that they can get sync blocks from us
      bool result = _chain_db->push_block( blk_msg.block,
                                           (_is_block_producer | _force_validate) ?
                                              database::skip_nothing : database::skip_transaction_signatures );

      // the block was accepted, so we now know all of the transactions contained in the block
      if (!sync_mode)
      {
         // if we're not in sync mode, there's a chance we will be seeing some transactions
         // included in blocks before we see the free-floating transaction itself.  If that
         // happens, there's no reason to fetch the transactions, so  construct a list of the
         // transaction message ids we no longer need.
         // during sync, it is unlikely that we'll see any old
         for (const processed_transaction& transaction : blk_msg.block.transactions)
         {
            graphene::net::trx_message transaction_message(transaction);
            contained_transaction_message_ids.push_back(graphene::net::message(transaction_message).id());
         }
      }

      return result;
   } catch ( const graphene::chain::unlinkable_block_exception& e ) {
      // translate to a graphene::net exception
      elog("Error when pushing block:\n${e}", ("e", e.to_detail_string()));
      FC_THROW_EXCEPTION( graphene::net::unlinkable_block_exception,
                          "Error when pushing block:\n${e}",
                          ("e", e.to_detail_string()) );
   } catch( const fc::exception& e ) {
      elog("Error when pushing block:\n${e}", ("e", e.to_detail_string()));
      throw;
   }

   if( !_is_finished_syncing && !sync_mode )
   {
      _is_finished_syncing = true;
      _self->syncing_finished();
   }
} FC_CAPTURE_AND_RETHROW( (blk_msg)(sync_mode) ) return false; }

void application_impl::handle_transaction(const graphene::net::trx_message& transaction_message)
{ try {
   static fc::time_point last_call;
   static int trx_count = 0;
   ++trx_count;
   auto now = fc::time_point::now();
   if( now - last_call > fc::seconds(1) ) {
      ilog("Got ${c} transactions from network", ("c",trx_count) );
      last_call = now;
      trx_count = 0;
   }

   _chain_db->push_transaction( transaction_message.trx );
} FC_CAPTURE_AND_RETHROW( (transaction_message) ) }

void application_impl::handle_message(const message& message_to_process)
{
   // not a transaction, not a block
   FC_THROW( "Invalid Message Type" );
}

bool application_impl::is_included_block(const block_id_type& block_id)
{
  uint32_t block_num = block_header::num_from_id(block_id);
  block_id_type block_id_in_preferred_chain = _chain_db->get_block_id_for_num(block_num);
  return block_id == block_id_in_preferred_chain;
}

/**
 * Assuming all data elements are ordered in some way, this method should
 * return up to limit ids that occur *after* the last ID in synopsis that
 * we recognize.
 *
 * On return, remaining_item_count will be set to the number of items
 * in our blockchain after the last item returned in the result,
 * or 0 if the result contains the last item in the blockchain
 */
std::vector<item_hash_t> application_impl::get_block_ids(const std::vector<item_hash_t>& blockchain_synopsis,
                                               uint32_t& remaining_item_count,
                                               uint32_t limit)
{ try {
   vector<block_id_type> result;
   remaining_item_count = 0;
   if( _chain_db->head_block_num() == 0 )
      return result;

   result.reserve(limit);
   block_id_type last_known_block_id;

   if (blockchain_synopsis.empty() ||
       (blockchain_synopsis.size() == 1 && blockchain_synopsis[0] == block_id_type()))
   {
     // peer has sent us an empty synopsis meaning they have no blocks.
     // A bug in old versions would cause them to send a synopsis containing block 000000000
     // when they had an empty blockchain, so pretend they sent the right thing here.

     // do nothing, leave last_known_block_id set to zero
   }
   else
   {
     bool found_a_block_in_synopsis = false;
     for (const item_hash_t& block_id_in_synopsis : boost::adaptors::reverse(blockchain_synopsis))
       if (block_id_in_synopsis == block_id_type() ||
           (_chain_db->is_known_block(block_id_in_synopsis) && is_included_block(block_id_in_synopsis)))
       {
         last_known_block_id = block_id_in_synopsis;
         found_a_block_in_synopsis = true;
         break;
       }
     if (!found_a_block_in_synopsis)
       FC_THROW_EXCEPTION( graphene::net::peer_is_on_an_unreachable_fork,
                           "Unable to provide a list of blocks starting at any of the blocks in peer's synopsis" );
   }
   for( uint32_t num = block_header::num_from_id(last_known_block_id);
        num <= _chain_db->head_block_num() && result.size() < limit;
        ++num )
      if( num > 0 )
         result.push_back(_chain_db->get_block_id_for_num(num));

   if( !result.empty() && block_header::num_from_id(result.back()) < _chain_db->head_block_num() )
      remaining_item_count = _chain_db->head_block_num() - block_header::num_from_id(result.back());

   return result;
} FC_CAPTURE_AND_RETHROW( (blockchain_synopsis)(remaining_item_count)(limit) ) }

/**
 * Given the hash of the requested data, fetch the body.
 */
message application_impl::get_item(const item_id& id)
{ try {
  // ilog("Request for item ${id}", ("id", id));
   if( id.item_type == graphene::net::block_message_type )
   {
      auto opt_block = _chain_db->fetch_block_by_id(id.item_hash);
      if( !opt_block )
         elog("Couldn't find block ${id} -- corresponding ID in our chain is ${id2}",
              ("id", id.item_hash)("id2", _chain_db->get_block_id_for_num(block_header::num_from_id(id.item_hash))));
      FC_ASSERT( opt_block.valid() );
      // ilog("Serving up block #${num}", ("num", opt_block->block_num()));
      return block_message(std::move(*opt_block));
   }
   return trx_message( _chain_db->get_recent_transaction( id.item_hash ) );
} FC_CAPTURE_AND_RETHROW( (id) ) }

chain_id_type application_impl::get_chain_id() const
{
   return _chain_db->get_chain_id();
}

/**
 * Returns a synopsis of the blockchain used for syncing.  This consists of a list of
 * block hashes at intervals exponentially increasing towards the genesis block.
 * When syncing to a peer, the peer uses this data to determine if we're on the same
 * fork as they are, and if not, what blocks they need to send us to get us on their
 * fork.
 *
 * In the over-simplified case, this is a straighforward synopsis of our current
 * preferred blockchain; when we first connect up to a peer, this is what we will be sending.
 * It looks like this:
 *   If the blockchain is empty, it will return the empty list.
 *   If the blockchain has one block, it will return a list containing just that block.
 *   If it contains more than one block:
 *     the first element in the list will be the hash of the highest numbered block that
 *         we cannot undo
 *     the second element will be the hash of an item at the half way point in the undoable
 *         segment of the blockchain
 *     the third will be ~3/4 of the way through the undoable segment of the block chain
 *     the fourth will be at ~7/8...
 *       &c.
 *     the last item in the list will be the hash of the most recent block on our preferred chain
 * so if the blockchain had 26 blocks labeled a - z, the synopsis would be:
 *    a n u x z
 * the idea being that by sending a small (<30) number of block ids, we can summarize a huge
 * blockchain.  The block ids are more dense near the end of the chain where because we are
 * more likely to be almost in sync when we first connect, and forks are likely to be short.
 * If the peer we're syncing with in our example is on a fork that started at block 'v',
 * then they will reply to our synopsis with a list of all blocks starting from block 'u',
 * the last block they know that we had in common.
 *
 * In the real code, there are several complications.
 *
 * First, as an optimization, we don't usually send a synopsis of the entire blockchain, we
 * send a synopsis of only the segment of the blockchain that we have undo data for.  If their
 * fork doesn't build off of something in our undo history, we would be unable to switch, so there's
 * no reason to fetch the blocks.
 *
 * Second, when a peer replies to our initial synopsis and gives us a list of the blocks they think
 * we are missing, they only send a chunk of a few thousand blocks at once.  After we get those
 * block ids, we need to request more blocks by sending another synopsis (we can't just say "send me
 * the next 2000 ids" because they may have switched forks themselves and they don't track what
 * they've sent us).  For faster performance, we want to get a fairly long list of block ids first,
 * then start downloading the blocks.
 * The peer doesn't handle these follow-up block id requests any different from the initial request;
 * it treats the synopsis we send as our blockchain and bases its response entirely off that.  So to
 * get the response we want (the next chunk of block ids following the last one they sent us, or,
 * failing that, the shortest fork off of the last list of block ids they sent), we need to construct
 * a synopsis as if our blockchain was made up of:
 *    1. the blocks in our block chain up to the fork point (if there is a fork) or the head block (if no fork)
 *    2. the blocks we've already pushed from their fork (if there's a fork)
 *    3. the block ids they've previously sent us
 * Segment 3 is handled in the p2p code, it just tells us the number of blocks it has (in
 * number_of_blocks_after_reference_point) so we can leave space in the synopsis for them.
 * We're responsible for constructing the synopsis of Segments 1 and 2 from our active blockchain and
 * fork database.  The reference_point parameter is the last block from that peer that has been
 * successfully pushed to the blockchain, so that tells us whether the peer is on a fork or on
 * the main chain.
 */
std::vector<item_hash_t> application_impl::get_blockchain_synopsis(const item_hash_t& reference_point,
                                                         uint32_t number_of_blocks_after_reference_point)
{ try {
    std::vector<item_hash_t> synopsis;
    synopsis.reserve(30);
    uint32_t high_block_num;
    uint32_t non_fork_high_block_num;
    uint32_t low_block_num = _chain_db->last_non_undoable_block_num();
    std::vector<block_id_type> fork_history;

    if (reference_point != item_hash_t())
    {
      // the node is asking for a summary of the block chain up to a specified
      // block, which may or may not be on a fork
      // for now, assume it's not on a fork
      if (is_included_block(reference_point))
      {
        // reference_point is a block we know about and is on the main chain
        uint32_t reference_point_block_num = block_header::num_from_id(reference_point);
        assert(reference_point_block_num > 0);
        high_block_num = reference_point_block_num;
        non_fork_high_block_num = high_block_num;

        if (reference_point_block_num < low_block_num)
        {
          // we're on the same fork (at least as far as reference_point) but we've passed
          // reference point and could no longer undo that far if we diverged after that
          // block.  This should probably only happen due to a race condition where
          // the network thread calls this function, and then immediately pushes a bunch of blocks,
          // then the main thread finally processes this function.
          // with the current framework, there's not much we can do to tell the network
          // thread what our current head block is, so we'll just pretend that
          // our head is actually the reference point.
          // this *may* enable us to fetch blocks that we're unable to push, but that should
          // be a rare case (and correctly handled)
          low_block_num = reference_point_block_num;
        }
      }
      else
      {
        // block is a block we know about, but it is on a fork
        try
        {
          fork_history = _chain_db->get_block_ids_on_fork(reference_point);
          // returns a vector where the last element is the common ancestor with the preferred chain,
          // and the first element is the reference point you passed in
          assert(fork_history.size() >= 2);

          if( fork_history.front() != reference_point )
          {
             edump( (fork_history)(reference_point) );
             assert(fork_history.front() == reference_point);
          }
          block_id_type last_non_fork_block = fork_history.back();
          fork_history.pop_back();  // remove the common ancestor
          boost::reverse(fork_history);

          if (last_non_fork_block == block_id_type()) // if the fork goes all the way back to genesis (does graphene's fork db allow this?)
            non_fork_high_block_num = 0;
          else
            non_fork_high_block_num = block_header::num_from_id(last_non_fork_block);

          high_block_num = non_fork_high_block_num + fork_history.size();
          assert(high_block_num == block_header::num_from_id(fork_history.back()));
        }
        catch (const fc::exception& e)
        {
          // unable to get fork history for some reason.  maybe not linked?
          // we can't return a synopsis of its chain
          elog( "Unable to construct a blockchain synopsis for reference hash ${hash}: ${exception}",
                ("hash", reference_point)("exception", e) );
          throw;
        }
        if (non_fork_high_block_num < low_block_num)
        {
          wlog("Unable to generate a usable synopsis because the peer we're generating it for forked too long ago "
               "(our chains diverge after block #${non_fork_high_block_num} but only undoable to block #${low_block_num})",
               ("low_block_num", low_block_num)
               ("non_fork_high_block_num", non_fork_high_block_num));
          FC_THROW_EXCEPTION(graphene::net::block_older_than_undo_history, "Peer is are on a fork I'm unable to switch to");
        }
      }
    }
    else
    {
      // no reference point specified, summarize the whole block chain
      high_block_num = _chain_db->head_block_num();
      non_fork_high_block_num = high_block_num;
      if (high_block_num == 0)
        return synopsis; // we have no blocks
    }

    if( low_block_num == 0)
       low_block_num = 1;

    // at this point:
    // low_block_num is the block before the first block we can undo,
    // non_fork_high_block_num is the block before the fork (if the peer is on a fork, or otherwise it is the same as high_block_num)
    // high_block_num is the block number of the reference block, or the end of the chain if no reference provided

    // true_high_block_num is the ending block number after the network code appends any item ids it
    // knows about that we don't
    uint32_t true_high_block_num = high_block_num + number_of_blocks_after_reference_point;
    do
    {
      // for each block in the synopsis, figure out where to pull the block id from.
      // if it's <= non_fork_high_block_num, we grab it from the main blockchain;
      // if it's not, we pull it from the fork history
      if (low_block_num <= non_fork_high_block_num)
        synopsis.push_back(_chain_db->get_block_id_for_num(low_block_num));
      else
        synopsis.push_back(fork_history[low_block_num - non_fork_high_block_num - 1]);
      low_block_num += (true_high_block_num - low_block_num + 2) / 2;
    }
    while (low_block_num <= high_block_num);

    //idump((synopsis));
    return synopsis;
} FC_CAPTURE_AND_RETHROW() }

/**
 * Call this after the call to handle_message succeeds.
 *
 * @param item_type the type of the item we're synchronizing, will be the same as item passed to the sync_from() call
 * @param item_count the number of items known to the node that haven't been sent to handle_item() yet.
 *                   After `item_count` more calls to handle_item(), the node will be in sync
 */
void application_impl::sync_status(uint32_t item_type, uint32_t item_count)
{
   // any status reports to GUI go here
}

/**
 * Call any time the number of connected peers changes.
 */
void application_impl::connection_count_changed(uint32_t c)
{
  // any status reports to GUI go here
}

uint32_t application_impl::get_block_number(const item_hash_t& block_id)
{ try {
   return block_header::num_from_id(block_id);
} FC_CAPTURE_AND_RETHROW( (block_id) ) }

/**
 * Returns the time a block was produced (if block_id = 0, returns genesis time).
 * If we don't know about the block, returns time_point_sec::min()
 */
fc::time_point_sec application_impl::get_block_time(const item_hash_t& block_id)
{ try {
   auto opt_block = _chain_db->fetch_block_by_id( block_id );
   if( opt_block.valid() ) return opt_block->timestamp;
   return fc::time_point_sec::min();
} FC_CAPTURE_AND_RETHROW( (block_id) ) }

item_hash_t application_impl::get_head_block_id() const
{
   return _chain_db->head_block_id();
}

uint32_t application_impl::estimate_last_known_fork_from_git_revision_timestamp(uint32_t unix_timestamp) const
{
   return 0; // there are no forks in graphene
}

void application_impl::error_encountered(const std::string& message, const fc::oexception& error)
{
   // notify GUI or something cool
}

uint8_t application_impl::get_current_block_interval_in_seconds() const
{
   return _chain_db->get_global_properties().parameters.block_interval;
}



} } } // namespace graphene namespace app namespace detail

namespace graphene { namespace app {

application::application()
   : my(new detail::application_impl(this))
{}

application::~application()
{
   if( my->_p2p_network )
   {
      my->_p2p_network->close();
      my->_p2p_network.reset();
   }
   if( my->_chain_db )
   {
      my->_chain_db->close();
   }
}

void application::set_program_options(boost::program_options::options_description& command_line_options,
                                      boost::program_options::options_description& configuration_file_options) const
{
   configuration_file_options.add_options()
         ("p2p-endpoint", bpo::value<string>(), "Endpoint for P2P node to listen on")
         ("seed-node,s", bpo::value<vector<string>>()->composing(),
          "P2P nodes to connect to on startup (may specify multiple times)")
         ("seed-nodes", bpo::value<string>()->composing(),
          "JSON array of P2P nodes to connect to on startup")
         ("checkpoint,c", bpo::value<vector<string>>()->composing(),
          "Pairs of [BLOCK_NUM,BLOCK_ID] that should be enforced as checkpoints.")
         ("rpc-endpoint", bpo::value<string>()->implicit_value("127.0.0.1:8090"),
          "Endpoint for websocket RPC to listen on")
         ("rpc-tls-endpoint", bpo::value<string>()->implicit_value("127.0.0.1:8089"),
          "Endpoint for TLS websocket RPC to listen on")
         ("server-pem,p", bpo::value<string>()->implicit_value("server.pem"), "The TLS certificate file for this server")
         ("server-pem-password,P", bpo::value<string>()->implicit_value(""), "Password for this certificate")
         ("genesis-json", bpo::value<boost::filesystem::path>(), "File to read Genesis State from")
         ("dbg-init-key", bpo::value<string>(), "Block signing key to use for init witnesses, overrides genesis file")
         ("api-access", bpo::value<boost::filesystem::path>(), "JSON file specifying API permissions")
         ("plugins", bpo::value<string>(), "Space-separated list of plugins to activate")
         // TODO uncomment this when GUI is ready
         //("enable-subscribe-to-all", bpo::value<bool>()->implicit_value(false),
         // "Whether allow API clients to subscribe to universal object creation and removal events")
         ;
   command_line_options.add(configuration_file_options);
   command_line_options.add_options()
         ("create-genesis-json", bpo::value<boost::filesystem::path>(),
          "Path to create a Genesis State at. If a well-formed JSON file exists at the path, it will be parsed and any "
          "missing fields in a Genesis State will be added, and any unknown fields will be removed. If no file or an "
          "invalid file is found, it will be replaced with an example Genesis State.")
         ("replay-blockchain", "Rebuild object graph by replaying all blocks")
         ("resync-blockchain", "Delete all blocks and re-sync with network from scratch")
         ("force-validate", "Force validation of all transactions")
         ("genesis-timestamp", bpo::value<uint32_t>(),
          "Replace timestamp from genesis.json with current time plus this many seconds (experts only!)")
         ;
   command_line_options.add(_cli_options);
   configuration_file_options.add(_cfg_options);
}

void application::initialize(const fc::path& data_dir, const boost::program_options::variables_map& options)
{
   my->_data_dir = data_dir;
   my->_options = &options;

   if( options.count("create-genesis-json") )
   {
      fc::path genesis_out = options.at("create-genesis-json").as<boost::filesystem::path>();
      genesis_state_type genesis_state = detail::create_example_genesis();
      if( fc::exists(genesis_out) )
      {
         try {
            genesis_state = fc::json::from_file(genesis_out).as<genesis_state_type>( 20 );
         } catch(const fc::exception& e) {
            std::cerr << "Unable to parse existing genesis file:\n" << e.to_string()
                      << "\nWould you like to replace it? [y/N] ";
            char response = std::cin.get();
            if( toupper(response) != 'Y' )
               return;
         }

         std::cerr << "Updating genesis state in file " << genesis_out.generic_string() << "\n";
      } else {
         std::cerr << "Creating example genesis state in file " << genesis_out.generic_string() << "\n";
      }
      fc::json::save_to_file(genesis_state, genesis_out);

      std::exit(EXIT_SUCCESS);
   }

   std::vector<string> wanted;
   if( options.count("plugins") )
   {
      boost::split(wanted, options.at("plugins").as<std::string>(), [](char c){return c == ' ';});
   }
   else
   {
      wanted.push_back("witness");
      wanted.push_back("account_history");
      wanted.push_back("market_history");
      wanted.push_back("grouped_orders");
   }
   int es_ah_conflict_counter = 0;
   for (auto& it : wanted)
   {
      if(it == "account_history")
         ++es_ah_conflict_counter;
      if(it == "elasticsearch")
         ++es_ah_conflict_counter;

      if(es_ah_conflict_counter > 1) {
         elog("Can't start program with elasticsearch and account_history plugin at the same time");
         std::exit(EXIT_FAILURE);
      }
      if (!it.empty()) enable_plugin(it);
   }
}

void application::startup()
{
   try {
   my->startup();
   } catch ( const fc::exception& e ) {
      elog( "${e}", ("e",e.to_detail_string()) );
      throw;
   } catch ( ... ) {
      elog( "unexpected exception" );
      throw;
   }
}

std::shared_ptr<abstract_plugin> application::get_plugin(const string& name) const
{
   return my->_active_plugins[name];
}

net::node_ptr application::p2p_node()
{
   return my->_p2p_network;
}

std::shared_ptr<chain::database> application::chain_database() const
{
   return my->_chain_db;
}

void application::set_block_production(bool producing_blocks)
{
   my->_is_block_producer = producing_blocks;
}

optional< api_access_info > application::get_api_access_info( const string& username )const
{
   return my->get_api_access_info( username );
}

void application::set_api_access_info(const string& username, api_access_info&& permissions)
{
   my->set_api_access_info(username, std::move(permissions));
}

bool application::is_finished_syncing() const
{
   return my->_is_finished_syncing;
}

void graphene::app::application::enable_plugin(const string& name)
{
   FC_ASSERT(my->_available_plugins[name], "Unknown plugin '" + name + "'");
   my->_active_plugins[name] = my->_available_plugins[name];
   my->_active_plugins[name]->plugin_set_app(this);
}

void graphene::app::application::add_available_plugin(std::shared_ptr<graphene::app::abstract_plugin> p)
{
   my->_available_plugins[p->plugin_name()] = p;
}

void application::shutdown_plugins()
{
   for( auto& entry : my->_active_plugins )
      entry.second->plugin_shutdown();
   return;
}
void application::shutdown()
{
   if( my->_p2p_network )
      my->_p2p_network->close();
   if( my->_chain_db )
   {
      my->_chain_db->close();
      my->_chain_db = nullptr;
   }
}

void application::initialize_plugins( const boost::program_options::variables_map& options )
{
   for( auto& entry : my->_active_plugins )
      entry.second->plugin_initialize( options );
   return;
}

void application::startup_plugins()
{
   for( auto& entry : my->_active_plugins )
      entry.second->plugin_startup();
   return;
}

const application_options& application::get_options()
{
   return my->_app_options;
}

// namespace detail
} }
