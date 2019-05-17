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

#include <graphene/net/node.hpp>
#include <graphene/protocol/fee_schedule.hpp>

#include <graphene/net/peer_connection.hpp>
#define P2P_IN_DEDICATED_THREAD 1
#include "../../libraries/net/node_impl.hxx"

#include "../common/genesis_file_util.hpp"

/***
 * A peer connection delegate
 */
class test_delegate : public graphene::net::peer_connection_delegate
{
   public:
   test_delegate()
   {
   }
   void on_message(graphene::net::peer_connection* originating_peer, 
         const graphene::net::message& received_message)
   {
      elog("on_message was called with ${msg}", ("msg",received_message));
      try {
         graphene::net::address_request_message m = received_message.as<graphene::net::address_request_message>();
         std::shared_ptr<graphene::net::message> m_ptr = std::make_shared<graphene::net::message>( m );
         last_message = m_ptr;
      } catch (...)
      {
      }
   }
   void on_connection_closed(graphene::net::peer_connection* originating_peer) override {}
   graphene::net::message get_message_for_item(const graphene::net::item_id& item) override
   { 
      return graphene::net::message(); 
   }
   std::shared_ptr<graphene::net::message> last_message = nullptr;
};

class test_node : public graphene::net::node, public graphene::net::node_delegate
{
public:
   test_node(const std::string& name, const fc::path& config_dir, int port, int seed_port = -1) : node(name)
   {
      node_name = name;
   }
   ~test_node() 
   {
      close();
   }

   void on_message(graphene::net::peer_connection_ptr originating_peer, const graphene::net::message& received_message)
   {
      my->get_thread()->async([&]() {
         my->on_message( originating_peer.get(), received_message );
      }).wait();
   }

   std::pair<std::shared_ptr<test_delegate>, graphene::net::peer_connection_ptr> create_peer_connection(std::string url)
   {
      std::pair<std::shared_ptr<test_delegate>, graphene::net::peer_connection_ptr> ret_val;
      ret_val = this->my->get_thread()->async([&, &url = url](){
            std::shared_ptr<test_delegate> d{};
            graphene::net::peer_connection_ptr peer = graphene::net::peer_connection::make_shared(d.get());
            peer->set_remote_endpoint(fc::optional<fc::ip::endpoint>(fc::ip::endpoint::from_string(url)));
            my->move_peer_to_active_list(peer); 
            return std::pair<std::shared_ptr<test_delegate>, graphene::net::peer_connection_ptr>(d, peer);
         }).wait();
      return ret_val;
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
      elog("${name} was asked to handle a transaction", ("name", node_name));
   }
   void handle_message( const graphene::net::message& message_to_process ) 
   {
      elog("${name} received a message", ("name",node_name));
   }
   std::vector<graphene::net::item_hash_t> get_block_ids(
         const std::vector<graphene::net::item_hash_t>& blockchain_synopsis,
         uint32_t& remaining_item_count, uint32_t limit = 2000) 
      { return std::vector<graphene::net::item_hash_t>(); }
   graphene::net::message get_item( const graphene::net::item_id& id )
   {
      elog("${name} get_item was called", ("name",node_name));
      return graphene::net::message(); 
   }
   graphene::net::chain_id_type get_chain_id()const 
   {
      elog("${name} get_chain_id was called", ("name",node_name));
      return graphene::net::chain_id_type(); 
   }
   std::vector<graphene::net::item_hash_t> get_blockchain_synopsis(
         const graphene::net::item_hash_t& reference_point, 
         uint32_t number_of_blocks_after_reference_point)
      { return std::vector<graphene::net::item_hash_t>(); }
   void sync_status( uint32_t item_type, uint32_t item_count ) {}
   void connection_count_changed( uint32_t c ) 
   {
      elog("${name} connection_count_change was called", ("name",node_name));
   }
   uint32_t get_block_number(const graphene::net::item_hash_t& block_id) 
   { 
      elog("${name} get_block_number was called", ("name",node_name));
      return 0; 
   }
   fc::time_point_sec get_block_time(const graphene::net::item_hash_t& block_id)
   { 
      elog("${name} get_block_time was called", ("name",node_name));
      return fc::time_point_sec(); 
   }
   graphene::net::item_hash_t get_head_block_id() const 
   {
      elog("${name} get_head_block_id was called", ("name",node_name));
      return graphene::net::item_hash_t(); 
   }
   uint32_t estimate_last_known_fork_from_git_revision_timestamp(uint32_t unix_timestamp) const
      { return 0; }
   void error_encountered(const std::string& message, const fc::oexception& error)
   {
      elog("${name} error_encountered was called. Message: ${msg}", ("name",node_name)("msg", message));
   }
   uint8_t get_current_block_interval_in_seconds() const
   { 
      elog("${name} get_current_block_interval_in_seconds was called", ("name",node_name));
      return 0; 
   }

   private:
   std::string node_name;
};

class test_peer : public graphene::net::peer_connection
{
public:
   std::shared_ptr<graphene::net::message> message_received;
   void send_message(const graphene::net::message& message_to_send, size_t message_send_time_field_offset = (size_t)-1) override
   {
      try {
         // make a copy
         graphene::net::address_message m = message_to_send.as<graphene::net::address_message>();
         std::shared_ptr<graphene::net::message> msg_ptr = std::make_shared<graphene::net::message>(m);
         // store it for later
         message_received = msg_ptr;
         return;
      } catch (...) {}
      message_received = nullptr;
   }
public:
   test_peer(graphene::net::peer_connection_delegate* del) : graphene::net::peer_connection(del) {
      message_received = nullptr;
   }
};

void test_address_message( std::shared_ptr<graphene::net::message> msg, std::size_t num_elements)
{
   if (msg != nullptr)
   {
      graphene::net::address_message addr_msg = static_cast<graphene::net::address_message>( msg->as<graphene::net::address_message>() );
      BOOST_CHECK_EQUAL(addr_msg.addresses.size(), num_elements);
   } 
   else 
   {
      BOOST_FAIL( "address_message was null" );
   }
}

BOOST_AUTO_TEST_SUITE( p2p_node_tests )

/****
 * Assure that when disable_peer_advertising is set,
 * the node does not share its peer list
 */
BOOST_AUTO_TEST_CASE( disable_peer_advertising )
{
   // create a node
   int node1_port = graphene::app::get_available_port();
   fc::temp_directory node1_dir;
   test_node node1("Node1", node1_dir.path(), node1_port);
   node1.disable_peer_advertising();

   // get something in their list of connections
   std::pair<std::shared_ptr<test_delegate>, graphene::net::peer_connection_ptr> node2_rslts
    = node1.create_peer_connection( "127.0.0.1:8090" );

   // verify that they do not share it with others
   test_delegate peer3_delegate{};
   std::shared_ptr<test_peer> peer3_ptr = std::make_shared<test_peer>(&peer3_delegate);
   graphene::net::address_request_message req;
   node1.on_message( peer3_ptr, req );

   // check the results
   std::shared_ptr<graphene::net::message> msg = peer3_ptr->message_received;
   test_address_message(msg, 0);
}

BOOST_AUTO_TEST_CASE( set_nothing_advertise_algorithm )
{
   // create a node
   int node1_port = graphene::app::get_available_port();
   fc::temp_directory node1_dir;
   test_node node1("Node1", node1_dir.path(), node1_port);
   node1.set_advertise_algorithm( "nothing" );

   // get something in their list of connections
   std::pair<std::shared_ptr<test_delegate>, graphene::net::peer_connection_ptr> node2_rslts 
         = node1.create_peer_connection( "127.0.0.1:8090" );

   // verify that they do not share it with others
   test_delegate peer3_delegate{};
   std::shared_ptr<test_peer> peer3_ptr = std::make_shared<test_peer>(&peer3_delegate);
   graphene::net::address_request_message req;
   node1.on_message( peer3_ptr, req );

   // check the results
   std::shared_ptr<graphene::net::message> msg = peer3_ptr->message_received;
   test_address_message(msg, 0);
}

BOOST_AUTO_TEST_CASE( advertise_list )
{
   std::vector<std::string> advert_list = { "127.0.0.1:8090"};
   // set up my node
   int my_node_port = graphene::app::get_available_port();
   fc::temp_directory my_node_dir;
   test_node my_node("Hello", my_node_dir.path(), my_node_port);
   my_node.set_advertise_algorithm( "list", advert_list );
   test_delegate del{};
   // a fake peer
   std::shared_ptr<test_peer> my_peer(new test_peer{&del});

   // act like my_node received an address_request message from my_peer
   graphene::net::address_request_message address_request_message_received;
   my_node.on_message( my_peer, address_request_message_received );
   // check the results
   std::shared_ptr<graphene::net::message> msg = my_peer->message_received;
   test_address_message( msg, 1 );
}

BOOST_AUTO_TEST_CASE( exclude_list )
{
   std::vector<std::string> ex_list = { "127.0.0.1:8090"};
   // set up my node
   int my_node_port = graphene::app::get_available_port();
   fc::temp_directory my_node_dir;
   test_node my_node("Hello", my_node_dir.path(), my_node_port);
   my_node.set_advertise_algorithm( "exclude_list", ex_list );
   // some peers
   std::pair<std::shared_ptr<test_delegate>, graphene::net::peer_connection_ptr> node2_rslts 
         = my_node.create_peer_connection("127.0.0.1:8089");
   std::pair<std::shared_ptr<test_delegate>, graphene::net::peer_connection_ptr> node3_rslts 
         = my_node.create_peer_connection("127.0.0.1:8090");
   std::pair<std::shared_ptr<test_delegate>, graphene::net::peer_connection_ptr> node4_rslts 
         = my_node.create_peer_connection("127.0.0.1:8091");

   // act like my_node received an address_request message from my_peer
   test_delegate del_4{};
   std::shared_ptr<test_peer> peer_4( new test_peer(&del_4) );
   graphene::net::address_request_message address_request_message_received;
   my_node.on_message( peer_4, address_request_message_received );
   // check the results
   std::shared_ptr<graphene::net::message> msg = peer_4->message_received;
   test_address_message( msg, 2 );
}

BOOST_AUTO_TEST_SUITE_END()
