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
#include <graphene/app/api.hpp>
#include <graphene/app/api_access.hpp>
#include <graphene/app/application.hpp>
#include <graphene/app/plugin.hpp>

#include <graphene/chain/protocol/fee_schedule.hpp>
#include <graphene/chain/protocol/types.hpp>
#include <graphene/time/time.hpp>

#include <graphene/egenesis/egenesis.hpp>

#include <graphene/net/core_messages.hpp>

#include <graphene/time/time.hpp>

#include <graphene/utilities/key_conversion.hpp>

#include <fc/smart_ref_impl.hpp>

#include <fc/rpc/api_connection.hpp>
#include <fc/rpc/websocket_api.hpp>
#include <fc/network/resolve.hpp>

#include <boost/filesystem/path.hpp>
#include <boost/signals2.hpp>

#include <iostream>

#include <fc/log/file_appender.hpp>
#include <fc/log/logger.hpp>
#include <fc/log/logger_config.hpp>

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

   genesis_state_type create_example_genesis() {
      auto nathan_key = fc::ecc::private_key::regenerate(fc::sha256::hash(string("nathan")));
      dlog("Allocating all stake to ${key}", ("key", utilities::key_to_wif(nathan_key)));
      genesis_state_type initial_state;
      initial_state.initial_parameters.current_fees = fee_schedule::get_default();//->set_all_fees(GRAPHENE_BLOCKCHAIN_PRECISION);
      initial_state.initial_active_witnesses = GRAPHENE_DEFAULT_MIN_WITNESS_COUNT;
      initial_state.initial_timestamp = time_point_sec(time_point::now().sec_since_epoch() /
            initial_state.initial_parameters.block_interval *
            initial_state.initial_parameters.block_interval);
      for( int i = 0; i < initial_state.initial_active_witnesses; ++i )
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

      return initial_state;
   }

   class application_impl : public net::node_delegate
   {
   public:
      fc::optional<fc::temp_file> _lock_file;
      bool _is_block_producer = false;

      void reset_p2p_node(const fc::path& data_dir)
      { try {
         _p2p_network = std::make_shared<net::node>("Graphene Reference Implementation");

         _p2p_network->load_configuration(data_dir / "p2p");
         _p2p_network->set_node_delegate(this);

         if( _options->count("seed-node") )
         {
            auto seeds = _options->at("seed-node").as<vector<string>>();
            for( const string& endpoint_string : seeds )
            {
               std::vector<fc::ip::endpoint> endpoints = resolve_string_to_ip_endpoints(endpoint_string);
               for (const fc::ip::endpoint& endpoint : endpoints)
               {
                  ilog("Adding seed node ${endpoint}", ("endpoint", endpoint));
                  _p2p_network->add_node(endpoint);
                  _p2p_network->connect_to_endpoint(endpoint);
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

      std::vector<fc::ip::endpoint> resolve_string_to_ip_endpoints(const std::string& endpoint_string)
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
                  FC_THROW_EXCEPTION(fc::unknown_host_exception, "The host name can not be resolved: ${hostname}", ("hostname", hostname));
               return endpoints;
            }
            catch (const boost::bad_lexical_cast&)
            {
               FC_THROW("Bad port: ${port}", ("port", port_string));
            }
         }
         FC_CAPTURE_AND_RETHROW((endpoint_string))
      }

      void reset_websocket_server()
      { try {
         if( !_options->count("rpc-endpoint") )
            return;

         _websocket_server = std::make_shared<fc::http::websocket_server>();

         _websocket_server->on_connection([&]( const fc::http::websocket_connection_ptr& c ){
            auto wsc = std::make_shared<fc::rpc::websocket_api_connection>(*c);
            auto login = std::make_shared<graphene::app::login_api>( std::ref(*_self) );
            auto db_api = std::make_shared<graphene::app::database_api>( std::ref(*_self->chain_database()) );
            wsc->register_api(fc::api<graphene::app::database_api>(db_api));
            wsc->register_api(fc::api<graphene::app::login_api>(login));
            c->set_session_data( wsc );
         });
         ilog("Configured websocket rpc to listen on ${ip}", ("ip",_options->at("rpc-endpoint").as<string>()));
         _websocket_server->listen( fc::ip::endpoint::from_string(_options->at("rpc-endpoint").as<string>()) );
         _websocket_server->start_accept();
      } FC_CAPTURE_AND_RETHROW() }


      void reset_websocket_tls_server()
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

         _websocket_tls_server->on_connection([&]( const fc::http::websocket_connection_ptr& c ){
            auto wsc = std::make_shared<fc::rpc::websocket_api_connection>(*c);
            auto login = std::make_shared<graphene::app::login_api>( std::ref(*_self) );
            auto db_api = std::make_shared<graphene::app::database_api>( std::ref(*_self->chain_database()) );
            wsc->register_api(fc::api<graphene::app::database_api>(db_api));
            wsc->register_api(fc::api<graphene::app::login_api>(login));
            c->set_session_data( wsc );
         });
         ilog("Configured websocket TLS rpc to listen on ${ip}", ("ip",_options->at("rpc-tls-endpoint").as<string>()));
         _websocket_tls_server->listen( fc::ip::endpoint::from_string(_options->at("rpc-tls-endpoint").as<string>()) );
         _websocket_tls_server->start_accept();
      } FC_CAPTURE_AND_RETHROW() }

      application_impl(application* self)
         : _self(self),
           _chain_db(std::make_shared<chain::database>())
      {
      }

      ~application_impl()
      {
         fc::remove_all(_data_dir / "blockchain/dblock");
      }

      void startup()
      { try {
         bool clean = !fc::exists(_data_dir / "blockchain/dblock");
         fc::create_directories(_data_dir / "blockchain/dblock");

         auto initial_state = [&] {
            ilog("Initializing database...");
            if( _options->count("genesis-json") )
               return fc::json::from_file(_options->at("genesis-json").as<boost::filesystem::path>())
                     .as<genesis_state_type>();
            else
            {
               std::string egenesis_json;
               graphene::egenesis::compute_egenesis_json( egenesis_json );
               FC_ASSERT( egenesis_json != "" );
               FC_ASSERT( graphene::egenesis::get_egenesis_json_hash() == fc::sha256::hash( egenesis_json ) );
               return fc::json::from_string( egenesis_json ).as<genesis_state_type>();
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
               auto item = fc::json::from_string(cp).as<std::pair<uint32_t,block_id_type> >();
               loaded_checkpoints[item.first] = item.second;
            }
         }
         _chain_db->add_checkpoints( loaded_checkpoints );

         if( _options->count("replay-blockchain") )
         {
            ilog("Replaying blockchain on user request.");
            _chain_db->reindex(_data_dir/"blockchain", initial_state());
         } else if( clean )
            _chain_db->open(_data_dir / "blockchain", initial_state);
         else {
            wlog("Detected unclean shutdown. Replaying blockchain...");
            _chain_db->reindex(_data_dir / "blockchain", initial_state());
         }

         if (!_options->count("genesis-json") &&
             _chain_db->get_chain_id() != graphene::egenesis::get_egenesis_chain_id()) {
            elog("Detected old database. Nuking and starting over.");
            _chain_db->wipe(_data_dir / "blockchain", true);
            _chain_db.reset();
            _chain_db = std::make_shared<chain::database>();
            _chain_db->add_checkpoints(loaded_checkpoints);
            _chain_db->open(_data_dir / "blockchain", initial_state);
         }

         graphene::time::now();

         if( _options->count("apiaccess") )
            _apiaccess = fc::json::from_file( _options->at("apiaccess").as<boost::filesystem::path>() )
               .as<api_access>();
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
            _apiaccess.permission_map["*"] = wild_access;
         }

         reset_p2p_node(_data_dir);
         reset_websocket_server();
         reset_websocket_tls_server();
      } FC_CAPTURE_AND_RETHROW() }

      optional< api_access_info > get_api_access_info(const string& username)const
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

      /**
       * If delegate has the item, the network has no need to fetch it.
       */
      virtual bool has_item(const net::item_id& id) override
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
      virtual bool handle_block(const graphene::net::block_message& blk_msg, bool sync_mode,
                                std::vector<fc::uint160_t>& contained_transaction_message_ids) override
      { try {
         if (!sync_mode || blk_msg.block.block_num() % 10000 == 0)
            ilog("Got block #${n} from network", ("n", blk_msg.block.block_num()));

         try {
            bool result = _chain_db->push_block(blk_msg.block, _is_block_producer ? database::skip_nothing : database::skip_transaction_signatures);

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
         } catch( const fc::exception& e ) {
            elog("Error when pushing block:\n${e}", ("e", e.to_detail_string()));
            throw;
         }

         if( !_is_finished_syncing && !sync_mode )
         {
            _is_finished_syncing = true;
            _self->syncing_finished();
         }
      } FC_CAPTURE_AND_RETHROW( (blk_msg)(sync_mode) ) }

      virtual void handle_transaction(const graphene::net::trx_message& transaction_message) override
      { try {
         ilog("Got transaction from network");
         _chain_db->push_transaction( transaction_message.trx );
      } FC_CAPTURE_AND_RETHROW( (transaction_message) ) }

      virtual void handle_message(const message& message_to_process) override
      {
         // not a transaction, not a block
         FC_THROW( "Invalid Message Type" );
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
      virtual std::vector<item_hash_t> get_item_ids(uint32_t item_type,
                                                    const std::vector<item_hash_t>& blockchain_synopsis,
                                                    uint32_t& remaining_item_count,
                                                    uint32_t limit) override
      { try {
         FC_ASSERT( item_type == graphene::net::block_message_type );
         vector<block_id_type>  result;
         remaining_item_count = 0;
         if( _chain_db->head_block_num() == 0 )
            return result;

         result.reserve(limit);
         block_id_type last_known_block_id;
         auto itr = blockchain_synopsis.rbegin();
         while( itr != blockchain_synopsis.rend() )
         {
            if( _chain_db->is_known_block(*itr) || *itr == block_id_type() )
            {
               last_known_block_id = *itr;
               break;
            }
            ++itr;
         }

         for( auto num = block_header::num_from_id(last_known_block_id);
              num <= _chain_db->head_block_num() && result.size() < limit;
              ++num )
            if( num > 0 )
               result.push_back(_chain_db->get_block_id_for_num(num));

         if( block_header::num_from_id(result.back()) < _chain_db->head_block_num() )
            remaining_item_count = _chain_db->head_block_num() - block_header::num_from_id(result.back());

         return result;
      } FC_CAPTURE_AND_RETHROW( (blockchain_synopsis)(remaining_item_count)(limit) ) }

      /**
       * Given the hash of the requested data, fetch the body.
       */
      virtual message get_item(const item_id& id) override
      { try {
         ilog("Request for item ${id}", ("id", id));
         if( id.item_type == graphene::net::block_message_type )
         {
            auto opt_block = _chain_db->fetch_block_by_id(id.item_hash);
            if( !opt_block )
               elog("Couldn't find block ${id} -- corresponding ID in our chain is ${id2}",
                    ("id", id.item_hash)("id2", _chain_db->get_block_id_for_num(block_header::num_from_id(id.item_hash))));
            FC_ASSERT( opt_block.valid() );
            ilog("Serving up block #${num}", ("num", opt_block->block_num()));
            return block_message(std::move(*opt_block));
         }
         return trx_message( _chain_db->get_recent_transaction( id.item_hash ) );
      } FC_CAPTURE_AND_RETHROW( (id) ) }

      virtual chain_id_type get_chain_id()const override
      {
         return _chain_db->get_chain_id();
      }

      /**
       * Returns a synopsis of the blockchain used for syncing.
       * This consists of a list of selected item hashes from our current preferred
       * blockchain, exponentially falling off into the past.  Horrible explanation.
       *
       * If the blockchain is empty, it will return the empty list.
       * If the blockchain has one block, it will return a list containing just that block.
       * If it contains more than one block:
       *   the first element in the list will be the hash of the genesis block
       *   the second element will be the hash of an item at the half way point in the blockchain
       *   the third will be ~3/4 of the way through the block chain
       *   the fourth will be at ~7/8...
       *     &c.
       *   the last item in the list will be the hash of the most recent block on our preferred chain
       */
      virtual std::vector<item_hash_t> get_blockchain_synopsis(uint32_t item_type,
                                                               const graphene::net::item_hash_t& reference_point,
                                                               uint32_t number_of_blocks_after_reference_point) override
      { try {
         std::vector<item_hash_t> result;
         result.reserve(30);
         uint32_t head_block_num = _chain_db->head_block_num();
         result.push_back(_chain_db->head_block_id());
         uint32_t current = 1;
         while( current < head_block_num )
         {
            result.push_back(_chain_db->get_block_id_for_num(head_block_num - current));
            current = current*2;
         }
         std::reverse( result.begin(), result.end() );
         idump((reference_point)(number_of_blocks_after_reference_point)(result));
         return result;
      } FC_CAPTURE_AND_RETHROW( (reference_point)(number_of_blocks_after_reference_point) ) }

      /**
       * Call this after the call to handle_message succeeds.
       *
       * @param item_type the type of the item we're synchronizing, will be the same as item passed to the sync_from() call
       * @param item_count the number of items known to the node that haven't been sent to handle_item() yet.
       *                   After `item_count` more calls to handle_item(), the node will be in sync
       */
      virtual void sync_status(uint32_t item_type, uint32_t item_count) override
      {
         // any status reports to GUI go here
      }

      /**
       * Call any time the number of connected peers changes.
       */
      virtual void connection_count_changed(uint32_t c) override
      {
        // any status reports to GUI go here
      }

      virtual uint32_t get_block_number(const item_hash_t& block_id) override
      { try {
         return block_header::num_from_id(block_id);
      } FC_CAPTURE_AND_RETHROW( (block_id) ) }

      /**
       * Returns the time a block was produced (if block_id = 0, returns genesis time).
       * If we don't know about the block, returns time_point_sec::min()
       */
      virtual fc::time_point_sec get_block_time(const item_hash_t& block_id) override
      { try {
         auto opt_block = _chain_db->fetch_block_by_id( block_id );
         if( opt_block.valid() ) return opt_block->timestamp;
         return fc::time_point_sec::min();
      } FC_CAPTURE_AND_RETHROW( (block_id) ) }

      /** returns graphene::time::now() */
      virtual fc::time_point_sec get_blockchain_now() override
      {
         return graphene::time::now();
      }

      virtual item_hash_t get_head_block_id() const override
      {
         return _chain_db->head_block_id();
      }

      virtual uint32_t estimate_last_known_fork_from_git_revision_timestamp(uint32_t unix_timestamp) const override
      {
         return 0; // there are no forks in graphene
      }

      virtual void error_encountered(const std::string& message, const fc::oexception& error) override
      {
         // notify GUI or something cool
      }

      application* _self;

      fc::path _data_dir;
      const bpo::variables_map* _options = nullptr;
      api_access _apiaccess;

      std::shared_ptr<graphene::chain::database>            _chain_db;
      std::shared_ptr<graphene::net::node>                  _p2p_network;
      std::shared_ptr<fc::http::websocket_server>      _websocket_server;
      std::shared_ptr<fc::http::websocket_tls_server>  _websocket_tls_server;

      std::map<string, std::shared_ptr<abstract_plugin>> _plugins;

      bool _is_finished_syncing = false;
   };

}

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
         ("seed-node,s", bpo::value<vector<string>>()->composing(), "P2P nodes to connect to on startup (may specify multiple times)")
         ("checkpoint,c", bpo::value<vector<string>>()->composing(), "Pairs of [BLOCK_NUM,BLOCK_ID] that should be enforced as checkpoints.")
         ("rpc-endpoint", bpo::value<string>()->implicit_value("127.0.0.1:8090"), "Endpoint for websocket RPC to listen on")
         ("rpc-tls-endpoint", bpo::value<string>()->implicit_value("127.0.0.1:8089"), "Endpoint for TLS websocket RPC to listen on")
         ("server-pem,p", bpo::value<string>()->implicit_value("server.pem"), "The TLS certificate file for this server")
         ("server-pem-password,P", bpo::value<string>()->implicit_value(""), "Password for this certificate")
         ("genesis-json", bpo::value<boost::filesystem::path>(), "File to read Genesis State from")
         ("api-access", bpo::value<boost::filesystem::path>(), "JSON file specifying API permissions")
         ;
   command_line_options.add(configuration_file_options);
   command_line_options.add_options()
         ("create-genesis-json", bpo::value<boost::filesystem::path>(),
          "Path to create a Genesis State at. If a well-formed JSON file exists at the path, it will be parsed and any "
          "missing fields in a Genesis State will be added, and any unknown fields will be removed. If no file or an "
          "invalid file is found, it will be replaced with an example Genesis State.")
         ("replay-blockchain", "Rebuild object graph by replaying all blocks")
         ("resync-blockchain", "Delete all blocks and re-sync with network from scratch")
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
            genesis_state = fc::json::from_file(genesis_out).as<genesis_state_type>();
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
}

void application::startup()
{
   my->startup();
}

std::shared_ptr<abstract_plugin> application::get_plugin(const string& name) const
{
   return my->_plugins[name];
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

bool application::is_finished_syncing() const
{
   return my->_is_finished_syncing;
}

void graphene::app::application::add_plugin(const string& name, std::shared_ptr<graphene::app::abstract_plugin> p)
{
   my->_plugins[name] = p;
}

void application::shutdown_plugins()
{
   for( auto& entry : my->_plugins )
      entry.second->plugin_shutdown();
   return;
}

void application::initialize_plugins( const boost::program_options::variables_map& options )
{
   for( auto& entry : my->_plugins )
      entry.second->plugin_initialize( options );
   return;
}

void application::startup_plugins()
{
   for( auto& entry : my->_plugins )
      entry.second->plugin_startup();
   return;
}

// namespace detail
} }
