/*
 * Copyright (c) 2019 Bitshares Foundation, and contributors.
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
#include <memory>
#include <thread>
#include <iostream>
#include <boost/test/unit_test.hpp>
#include <boost/assign/list_of.hpp>

#include <fc/thread/thread.hpp>
#include <fc/asio.hpp>
#include <fc/filesystem.hpp>

#include <graphene/net/node.hpp>
#include <graphene/net/peer_connection.hpp>

#include <fc/io/raw.hpp>

#include <fc/log/appender.hpp>
#include <fc/log/console_appender.hpp>
#include <fc/log/logger.hpp>
#include <fc/log/logger_config.hpp>

#include <graphene/protocol/fee_schedule.hpp>

#include "../../libraries/net/node_impl.hxx"

#include "../common/genesis_file_util.hpp"
#include "../common/utils.hpp"

/***
 * A peer connection delegate
 */
class test_delegate : public graphene::net::peer_connection_delegate
{
public:
   test_delegate()
   {
   }
   void on_message( graphene::net::peer_connection* originating_peer,
         const graphene::net::message& received_message )
   {
      ilog( "on_message was called with ${msg}", ("msg",received_message) );
      try {
         graphene::net::address_request_message m = received_message.as< graphene::net::address_request_message >();
         std::shared_ptr<graphene::net::message> m_ptr = std::make_shared< graphene::net::message >( m );
         last_message = m_ptr;
      } catch (...)
      {
      }
   }
   void on_connection_closed( graphene::net::peer_connection* originating_peer ) override {}
   graphene::net::message get_message_for_item( const graphene::net::item_id& item ) override
   {
      return graphene::net::message();
   }
   std::shared_ptr< graphene::net::message > last_message = nullptr;
};

class test_peer : public graphene::net::peer_connection
{
public:
   std::vector<graphene::net::message> messages_received;

   test_peer(graphene::net::peer_connection_delegate* del) : graphene::net::peer_connection(del)
   {
   }

   void send_message( const graphene::net::message& message_to_send,
         size_t message_send_time_field_offset = (size_t)-1 ) override
   {
      messages_received.push_back( message_to_send );
   }
};

static void test_closing_connection_message( const graphene::net::message& msg )
{
   try
   {
      const auto& closing_msg = msg.as<graphene::net::closing_connection_message>();
   }
   catch( fc::exception& )
   {
      BOOST_FAIL( "Expecting closing_connection_message" );
   }
}

static void test_address_message( const graphene::net::message& msg, std::size_t num_elements )
{
   try
   {
      const auto& addr_msg = msg.as<graphene::net::address_message>();
      BOOST_CHECK_EQUAL( addr_msg.addresses.size(), num_elements );
   }
   catch( fc::exception& )
   {
      BOOST_FAIL( "Expecting address_message" );
   }
}

class test_node : public graphene::net::node, public graphene::net::node_delegate
{
public:
   std::vector<std::shared_ptr<test_peer>> test_peers;

   test_node( const std::string& name, const fc::path& config_dir, int port, int seed_port = -1 )
         : node( name )
   {
      std::cout << "test_node::test_node(): current thread=" << uint64_t(&fc::thread::current()) << std::endl;
      node_name = name;
   }
   ~test_node()
   {
      my->get_thread()->async( [&]() {
         this->test_peers.clear();
      }).wait();
   }

   void on_message( graphene::net::peer_connection_ptr originating_peer,
                    const graphene::net::message& received_message )
   {
      my->get_thread()->async( [&]() {
         my->on_message( originating_peer.get(), received_message );
      }).wait();
   }

   std::pair<std::shared_ptr<test_delegate>, std::shared_ptr<test_peer>> create_test_peer( std::string url )
   {
      return this->my->get_thread()->async( [&, &url = url](){
            std::shared_ptr<test_delegate> d{};
            auto peer = std::make_shared<test_peer>( d.get() );
            peer->set_remote_endpoint( fc::optional<fc::ip::endpoint>( fc::ip::endpoint::from_string( url )) );
            this->test_peers.push_back( peer );
            return std::make_pair( d, peer );
         }).wait();
   }

   std::pair<std::shared_ptr<test_delegate>, graphene::net::peer_connection_ptr>
         create_peer_connection( std::string url )
   {
      return this->my->get_thread()->async( [&, &url = url](){
            std::shared_ptr<test_delegate> d{};
            graphene::net::peer_connection_ptr peer = graphene::net::peer_connection::make_shared( d.get() );
            peer->set_remote_endpoint( fc::optional<fc::ip::endpoint>( fc::ip::endpoint::from_string( url )) );
            peer->remote_inbound_endpoint = peer->get_remote_endpoint();
            my->move_peer_to_active_list( peer );
            return std::make_pair( d, peer );
         }).wait();
   }

   /****
    * Implementation methods of node_delegate
    */
   bool has_item( const graphene::net::item_id& id ) { return false; }
   bool handle_block( const graphene::net::block_message& blk_msg, bool sync_mode,
         std::vector<fc::uint160_t>& contained_transaction_message_ids )
      { return false; }
   void handle_transaction( const graphene::net::trx_message& trx_msg )
   {
      ilog( "${name} was asked to handle a transaction", ("name", node_name) );
   }
   void handle_message( const graphene::net::message& message_to_process )
   {
      ilog( "${name} received a message", ("name",node_name) );
   }
   std::vector<graphene::net::item_hash_t> get_block_ids(
         const std::vector<graphene::net::item_hash_t>& blockchain_synopsis,
         uint32_t& remaining_item_count, uint32_t limit = 2000 )
   {
      return std::vector<graphene::net::item_hash_t>();
   }
   graphene::net::message get_item( const graphene::net::item_id& id )
   {
      ilog("${name} get_item was called", ("name",node_name));
      return graphene::net::message();
   }
   graphene::net::chain_id_type get_chain_id() const
   {
      ilog("${name} get_chain_id was called", ("name",node_name));
      return graphene::net::chain_id_type();
   }
   std::vector<graphene::net::item_hash_t> get_blockchain_synopsis(
         const graphene::net::item_hash_t& reference_point,
         uint32_t number_of_blocks_after_reference_point)
   {
      return std::vector<graphene::net::item_hash_t>();
   }
   void sync_status( uint32_t item_type, uint32_t item_count ) {}
   void connection_count_changed( uint32_t c )
   {
      ilog( "${name} connection_count_change was called", ("name",node_name) );
   }
   uint32_t get_block_number( const graphene::net::item_hash_t& block_id )
   {
      ilog( "${name} get_block_number was called", ("name",node_name) );
      return 0;
   }
   fc::time_point_sec get_block_time( const graphene::net::item_hash_t& block_id )
   {
      ilog( "${name} get_block_time was called", ("name",node_name) );
      return fc::time_point_sec();
   }
   graphene::net::item_hash_t get_head_block_id() const
   {
      ilog( "${name} get_head_block_id was called", ("name",node_name) );
      return graphene::net::item_hash_t();
   }
   uint32_t estimate_last_known_fork_from_git_revision_timestamp( uint32_t unix_timestamp ) const
   {
      return 0;
   }
   void error_encountered( const std::string& message, const fc::oexception& error )
   {
      ilog( "${name} error_encountered was called. Message: ${msg}", ("name",node_name)("msg", message) );
   }
   uint8_t get_current_block_interval_in_seconds() const
   {
      ilog( "${name} get_current_block_interval_in_seconds was called", ("name",node_name) );
      return 0;
   }

private:
   std::string node_name;
};

struct p2p_fixture
{
   p2p_fixture()
   {
      // Configure logging : log p2p messages to console
      fc::logging_config logging_config = fc::logging_config::default_config();

      auto logger = logging_config.loggers.back(); // get a copy of the default logger
      logger.name = "p2p";                         // update the name to p2p
      logging_config.loggers.push_back( logger );  // add it to logging_config

      fc::configure_logging(logging_config);
   }
   ~p2p_fixture()
   {
      // Restore default logging config
      fc::configure_logging( fc::logging_config::default_config() );
   }
};

BOOST_FIXTURE_TEST_SUITE( p2p_node_tests, p2p_fixture )

/****
 * If a node requests addresses without sending hello_message first, it will be disconnected.
 */
BOOST_AUTO_TEST_CASE( address_request_without_hello )
{
   // create a node (node1)
   int node1_port = fc::network::get_available_port();
   fc::temp_directory node1_dir;
   test_node node1( "Node1", node1_dir.path(), node1_port );

   // get something in the list of connections
   std::pair<std::shared_ptr<test_delegate>, graphene::net::peer_connection_ptr> node2_rslts
         = node1.create_peer_connection( "127.0.0.1:8090" );

   // a new peer (peer3)
   std::pair<std::shared_ptr<test_delegate>, std::shared_ptr<test_peer>> peer3
         = node1.create_test_peer( "1.2.3.4:5678" );
   std::shared_ptr<test_peer> peer3_ptr = peer3.second;

   // peer3 request addresses
   graphene::net::address_request_message req;
   node1.on_message( peer3_ptr, req );

   // check the results
   // peer3 didn't send hello so the connection should be closed
   BOOST_REQUIRE_EQUAL( peer3_ptr->messages_received.size(), 1U );
   const auto& msg = peer3_ptr->messages_received.front();
   test_closing_connection_message( msg );

   // peer3 request again
   peer3_ptr->messages_received.clear();
   node1.on_message( peer3_ptr, req );

   // the request is ignored
   BOOST_REQUIRE_EQUAL( peer3_ptr->messages_received.size(), 0 );
}

/****
 * Assure that when disable_peer_advertising is set,
 * the node does not share its peer list
 */
BOOST_AUTO_TEST_CASE( disable_peer_advertising )
{
   // create a node (node1)
   int node1_port = fc::network::get_available_port();
   fc::temp_directory node1_dir;
   test_node node1( "Node1", node1_dir.path(), node1_port );

   // disable peer advertising
   node1.disable_peer_advertising();

   // get something in the list of connections
   std::pair<std::shared_ptr<test_delegate>, graphene::net::peer_connection_ptr> node2_rslts
         = node1.create_peer_connection( "127.0.0.1:8090" );

   // a new peer (peer3)
   std::pair<std::shared_ptr<test_delegate>, std::shared_ptr<test_peer>> peer3
         = node1.create_test_peer( "1.2.3.4:5678" );
   std::shared_ptr<test_peer> peer3_ptr = peer3.second;
   // simulate that node1 got its hello request and accepted the connection
   peer3_ptr->their_state = test_peer::their_connection_state::connection_accepted;

   // peer3 request addresses
   graphene::net::address_request_message req;
   node1.on_message( peer3_ptr, req );

   // check the results
   // Node1 does not share the peer list with others
   BOOST_REQUIRE_EQUAL( peer3_ptr->messages_received.size(), 1U );
   const auto& msg = peer3_ptr->messages_received.front();
   test_address_message( msg, 0 );
}

BOOST_AUTO_TEST_CASE( set_nothing_advertise_algorithm )
{
   // create a node (node1)
   int node1_port = fc::network::get_available_port();
   fc::temp_directory node1_dir;
   test_node node1( "Node1", node1_dir.path(), node1_port );

   // set advertise algorithm to "nothing"
   node1.set_advertise_algorithm( "nothing" );

   // get something in the list of connections
   std::pair<std::shared_ptr<test_delegate>, graphene::net::peer_connection_ptr> node2_rslts
         = node1.create_peer_connection( "127.0.0.1:8090" );

   // a new peer (peer3)
   std::pair<std::shared_ptr<test_delegate>, std::shared_ptr<test_peer>> peer3
         = node1.create_test_peer( "1.2.3.4:5678" );
   std::shared_ptr<test_peer> peer3_ptr = peer3.second;
   // simulate that node1 got its hello request and accepted the connection
   peer3_ptr->their_state = test_peer::their_connection_state::connection_accepted;

   // peer3 request addresses
   graphene::net::address_request_message req;
   node1.on_message( peer3_ptr, req );

   // check the results
   // Node1 does not share the peer list with others
   BOOST_REQUIRE_EQUAL( peer3_ptr->messages_received.size(), 1U );
   const auto& msg = peer3_ptr->messages_received.front();
   test_address_message( msg, 0 );
}

BOOST_AUTO_TEST_CASE( advertise_list_test )
{
   // create a node (node1)
   int node1_port = fc::network::get_available_port();
   fc::temp_directory node1_dir;
   test_node node1( "Node1", node1_dir.path(), node1_port );

   // set advertise algorithm to "list"
   std::vector<std::string> advert_list = { "127.0.0.1:8090", "2.3.4.55:1234" };
   node1.set_advertise_algorithm( "list", advert_list );

   // add some connections, 1 of which appears on the advertise_list
   std::pair<std::shared_ptr<test_delegate>, graphene::net::peer_connection_ptr> node1_rslts
         = node1.create_peer_connection("127.0.0.1:8089");
   std::pair<std::shared_ptr<test_delegate>, graphene::net::peer_connection_ptr> node2_rslts
         = node1.create_peer_connection("127.0.0.1:8090");
   std::pair<std::shared_ptr<test_delegate>, graphene::net::peer_connection_ptr> node2_reslts
         = node1.create_peer_connection("127.0.0.1:8091");

   // a new peer (peer3)
   std::pair<std::shared_ptr<test_delegate>, std::shared_ptr<test_peer>> peer3
         = node1.create_test_peer( "1.2.3.4:5678" );
   std::shared_ptr<test_peer> peer3_ptr = peer3.second;
   // simulate that node1 got its hello request and rejected the connection
   peer3_ptr->their_state = test_peer::their_connection_state::connection_rejected;

   // peer3 request addresses
   graphene::net::address_request_message req;
   node1.on_message( peer3_ptr, req );

   // check the results
   // node1 replies with 1 address, then closes the connection
   BOOST_REQUIRE_EQUAL( peer3_ptr->messages_received.size(), 2U );
   const auto& msg1 = peer3_ptr->messages_received.front();
   test_address_message( msg1, 1 );
   const auto& msg2 = peer3_ptr->messages_received.back();
   test_closing_connection_message( msg2 );
}

BOOST_AUTO_TEST_CASE( exclude_list )
{
   // create a node (node1)
   int node1_port = fc::network::get_available_port();
   fc::temp_directory node1_dir;
   test_node node1( "Node1", node1_dir.path(), node1_port );

   // set advertise algorithm to "exclude_list"
   std::vector<std::string> ex_list = { "127.0.0.1:8090", "2.3.4.55:1234" };
   node1.set_advertise_algorithm( "exclude_list", ex_list );

   // add some connections, 1 of which appears on the exclude_list
   std::pair<std::shared_ptr<test_delegate>, graphene::net::peer_connection_ptr> node1_rslts
         = node1.create_peer_connection("127.0.0.1:8089");
   std::pair<std::shared_ptr<test_delegate>, graphene::net::peer_connection_ptr> node2_rslts
         = node1.create_peer_connection("127.0.0.1:8090");
   std::pair<std::shared_ptr<test_delegate>, graphene::net::peer_connection_ptr> node2_reslts
         = node1.create_peer_connection("127.0.0.1:8091");

   // a new peer (peer3)
   std::pair<std::shared_ptr<test_delegate>, std::shared_ptr<test_peer>> peer3
         = node1.create_test_peer( "1.2.3.4:5678" );
   std::shared_ptr<test_peer> peer3_ptr = peer3.second;
   // simulate that node1 got its hello request and rejected the connection
   peer3_ptr->their_state = test_peer::their_connection_state::connection_rejected;

   // peer3 request addresses
   graphene::net::address_request_message req;
   node1.on_message( peer3_ptr, req );

   // check the results
   // node1 replies with 2 addresses, then closes the connection
   BOOST_REQUIRE_EQUAL( peer3_ptr->messages_received.size(), 2U );
   const auto& msg1 = peer3_ptr->messages_received.front();
   test_address_message( msg1, 2 );
   const auto& msg2 = peer3_ptr->messages_received.back();
   test_closing_connection_message( msg2 );
}

BOOST_AUTO_TEST_SUITE_END()
