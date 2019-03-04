/*
 * Copyright (c) 2017 Bitshares Foundation, and contributors.
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
#include <boost/test/unit_test.hpp>
#include <boost/assign/list_of.hpp>

#include <fc/thread/thread.hpp>

#include <graphene/net/node.hpp>
#include <graphene/chain/protocol/fee_schedule.hpp>

#include <graphene/net/peer_connection.hpp>

namespace graphene { namespace net  { namespace detail {

class node_impl : public graphene::net::peer_connection_delegate
{
public:
   void on_message( graphene::net::peer_connection* originating_peer,
                    const message& received_message );
   void on_connection_closed(peer_connection* originating_peer);
   message get_message_for_item(const item_id& item);
};

} } } // namespace graphene::net::detail

class test_node : public graphene::net::node {
public:
   test_node(const std::string& name) : node(name) {}
   void on_message(graphene::net::peer_connection_ptr originating_peer, const graphene::net::message& received_message)
   {
      get_thread()->async([&](){ return my->on_message(originating_peer.get(), received_message); }, "thread invoke for method on_message").wait();
   }
};

class test_peer : public graphene::net::peer_connection
{
public:
   std::shared_ptr<graphene::net::message> message_received;
   void send_message(const graphene::net::message& message_to_send, size_t message_send_time_field_offset = (size_t)-1) override
   {
      message_received = std::make_shared<graphene::net::message>(message_to_send);
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

BOOST_AUTO_TEST_CASE( disable_peer_advertising )
{
   // set up my node
   test_node my_node("Hello");
   my_node.disable_peer_advertising();

   // a fake peer
   graphene::net::detail::node_impl del;
   std::shared_ptr<test_peer> my_peer(new test_peer{&del});

   // act like my_node received an address_request message from my_peer
   graphene::net::address_request_message address_request_message_received;
   my_node.on_message( my_peer, address_request_message_received );

   // check the results
   std::shared_ptr<graphene::net::message> msg = my_peer->message_received;
   test_address_message(msg, 0);
}

BOOST_AUTO_TEST_CASE( advertise_list )
{
   std::vector<std::string> advert_list = { "127.0.0.1:8090"};
   // set up my node
   test_node my_node("Hello");
   my_node.set_advertise_algorithm( "list", advert_list );
   graphene::net::detail::node_impl del;
   // a fake peer
   std::shared_ptr<test_peer> my_peer(new test_peer{&del});

   // act like my_node received an address_request message from my_peer
   graphene::net::address_request_message address_request_message_received;
   my_node.on_message( my_peer, address_request_message_received );
   // check the results
   std::shared_ptr<graphene::net::message> msg = my_peer->message_received;
   test_address_message( msg, 1 );
}

BOOST_AUTO_TEST_SUITE_END()
