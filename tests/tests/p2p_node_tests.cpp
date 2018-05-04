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

namespace graphene
{
namespace net
{
namespace detail
{
class node_impl : public graphene::net::peer_connection_delegate
{
public:
   void on_message( peer_connection* originating_peer,
                    const message& received_message );
   void on_connection_closed(peer_connection* originating_peer);
   message get_message_for_item(const item_id& item);
};
}
}
}

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
      message_received = std::shared_ptr<graphene::net::message>(new graphene::net::message(message_to_send));
   }
public:
   test_peer(graphene::net::peer_connection_delegate* del) : graphene::net::peer_connection(del) {
      message_received = nullptr;
   }

};

BOOST_AUTO_TEST_CASE( p2p_disable_peer_advertising )
{
   test_node my_node("Hello");
   graphene::net::detail::node_impl del;
   std::shared_ptr<test_peer> my_peer(new test_peer{&del});
   graphene::net::address_request_message address_request_message_received;
   my_node.on_message(my_peer, address_request_message_received);
   BOOST_CHECK(my_peer->message_received != nullptr);
   // now try with "disable_peer_advertising" set
   my_node.disable_peer_advertising();
}
