#include <fc/crypto/elliptic.hpp>
#include <fc/io/json.hpp>
#include <fc/exception/exception.hpp>
#include <fc/io/raw_variant.hpp>
#include <fc/network/ip.hpp>
#include <fc/network/resolve.hpp>
#include <fc/thread/future.hpp>
#include <fstream>
#include <iostream>
#include <queue>
#include <future>

#include <graphene/chain/database.hpp>
#include <graphene/net/peer_connection.hpp>

class peer_probe : public graphene::net::peer_connection_delegate
{
public:
  bool _peer_closed_connection;
  bool _we_closed_connection;
  graphene::net::peer_connection_ptr _connection;
  std::vector<graphene::net::address_info> _peers;
  fc::ecc::public_key _node_id;
  fc::ip::endpoint _remote;
  bool _connection_was_rejected;
  bool _done;
  fc::promise<void>::ptr _probe_complete_promise;

public:
  peer_probe() :
    _peer_closed_connection(false),
    _we_closed_connection(false),
    _connection(graphene::net::peer_connection::make_shared(this)),
    _connection_was_rejected(false),
    _done(false),
    _probe_complete_promise(fc::promise<void>::ptr(new fc::promise<void>("probe_complete")))
  {}

  void start(const fc::ip::endpoint& endpoint_to_probe,
             const fc::ecc::private_key& my_node_id,
             const graphene::chain::chain_id_type& chain_id)
  {
    _remote = endpoint_to_probe;
    fc::future<void> connect_task = fc::async([this](){ _connection->connect_to(_remote); }, "connect_task");
    try
    {
      connect_task.wait(fc::seconds(10));
    }
    catch (const fc::timeout_exception&)
    {
      ilog("timeout connecting to node ${endpoint}", ("endpoint", endpoint_to_probe));
      connect_task.cancel(__FUNCTION__);
      throw;
    }

    fc::sha256::encoder shared_secret_encoder;
    fc::sha512 shared_secret = _connection->get_shared_secret();
    shared_secret_encoder.write(shared_secret.data(), sizeof(shared_secret));
    fc::ecc::compact_signature signature = my_node_id.sign_compact(shared_secret_encoder.result());

    graphene::net::hello_message hello("network_mapper",
                                  GRAPHENE_NET_PROTOCOL_VERSION,
                                  fc::ip::address(), 0, 0,
                                  my_node_id.get_public_key(),
                                  signature,
				  chain_id,
                                  fc::variant_object());

    _connection->send_message(hello);
  }

  void on_message(graphene::net::peer_connection* originating_peer,
                  const graphene::net::message& received_message) override
  {
    graphene::net::message_hash_type message_hash = received_message.id();
    dlog( "handling message ${type} ${hash} size ${size} from peer ${endpoint}",
          ( "type", graphene::net::core_message_type_enum(received_message.msg_type ) )("hash", message_hash )("size", received_message.size )("endpoint", originating_peer->get_remote_endpoint() ) );
    switch ( received_message.msg_type )
    {
    case graphene::net::core_message_type_enum::hello_message_type:
      on_hello_message( originating_peer, received_message.as<graphene::net::hello_message>() );
      break;
    case graphene::net::core_message_type_enum::connection_accepted_message_type:
      on_connection_accepted_message( originating_peer, received_message.as<graphene::net::connection_accepted_message>() );
      break;
    case graphene::net::core_message_type_enum::connection_rejected_message_type:
      on_connection_rejected_message( originating_peer, received_message.as<graphene::net::connection_rejected_message>() );
      break;
    case graphene::net::core_message_type_enum::address_request_message_type:
      on_address_request_message( originating_peer, received_message.as<graphene::net::address_request_message>() );
      break;
    case graphene::net::core_message_type_enum::address_message_type:
      on_address_message( originating_peer, received_message.as<graphene::net::address_message>() );
      break;
    case graphene::net::core_message_type_enum::closing_connection_message_type:
      on_closing_connection_message( originating_peer, received_message.as<graphene::net::closing_connection_message>() );
      break;
    default:
      break;
    }
  }

  void on_hello_message(graphene::net::peer_connection* originating_peer,
                        const graphene::net::hello_message& hello_message_received)
  {
    _node_id = hello_message_received.node_public_key;
    if (hello_message_received.user_data.contains("node_id"))
      originating_peer->node_id = hello_message_received.user_data["node_id"].as<graphene::net::node_id_t>( 1 );
    originating_peer->send_message(graphene::net::connection_rejected_message());
  }

  void on_connection_accepted_message(graphene::net::peer_connection* originating_peer,
                                      const graphene::net::connection_accepted_message& connection_accepted_message_received)
  {
    _connection_was_rejected = false;
    originating_peer->send_message(graphene::net::address_request_message());
  }

  void on_connection_rejected_message( graphene::net::peer_connection* originating_peer,
                                       const graphene::net::connection_rejected_message& connection_rejected_message_received )
  {
    _connection_was_rejected = true;
    originating_peer->send_message(graphene::net::address_request_message());
  }

  void on_address_request_message(graphene::net::peer_connection* originating_peer,
                                  const graphene::net::address_request_message& address_request_message_received)
  {
    originating_peer->send_message(graphene::net::address_message());
  }


  void on_address_message(graphene::net::peer_connection* originating_peer,
                          const graphene::net::address_message& address_message_received)
  {
    _peers = address_message_received.addresses;
    originating_peer->send_message(graphene::net::closing_connection_message("Thanks for the info"));
    _we_closed_connection = true;
  }

  void on_closing_connection_message(graphene::net::peer_connection* originating_peer,
                                     const graphene::net::closing_connection_message& closing_connection_message_received)
  {
    if (_we_closed_connection)
      _connection->close_connection();
    else
      _peer_closed_connection = true;
  }

  void on_connection_closed(graphene::net::peer_connection* originating_peer) override
  {
    _done = true;
    _probe_complete_promise->set_value();
  }

  graphene::net::message get_message_for_item(const graphene::net::item_id& item) override
  {
    return graphene::net::item_not_available_message(item);
  }

  void wait( const fc::microseconds& timeout_us )
  {
    _probe_complete_promise->wait( timeout_us );
  }
};

int main(int argc, char** argv)
{
  std::queue<fc::ip::endpoint> nodes_to_visit;
  std::set<fc::ip::endpoint> nodes_to_visit_set;
  std::set<fc::ip::endpoint> nodes_already_visited;

  if ( argc < 3 ) {
     std::cerr << "Usage: " << argv[0] << " <chain-id> <seed-addr> [<seed-addr> ...]\n";
     exit(1);
  }

  const graphene::chain::chain_id_type chain_id( argv[1] );
  for ( int i = 2; i < argc; i++ )
  {
     std::string ep(argv[i]);
     uint16_t port;
     auto pos = ep.find(':');
     if (pos > 0)
        port = boost::lexical_cast<uint16_t>( ep.substr( pos+1, ep.size() ) );
     else
        port = 1776;
     for (const auto& addr : fc::resolve( ep.substr( 0, pos > 0 ? pos : ep.size() ), port ))
        nodes_to_visit.push( addr );
  }

  fc::path data_dir = fc::temp_directory_path() / ("network_map_" + (fc::string) chain_id);
  fc::create_directories(data_dir);

  fc::ip::endpoint seed_node1 = nodes_to_visit.front();

  fc::ecc::private_key my_node_id = fc::ecc::private_key::generate();
  std::map<graphene::net::node_id_t, graphene::net::address_info> address_info_by_node_id;
  std::map<graphene::net::node_id_t, std::vector<graphene::net::address_info> > connections_by_node_id;
  std::vector<std::shared_ptr<peer_probe>> probes;

  while (!nodes_to_visit.empty() || !probes.empty())
  {
    while (!nodes_to_visit.empty())
    {
       fc::ip::endpoint remote = nodes_to_visit.front();
       nodes_to_visit.pop();
       nodes_to_visit_set.erase( remote );
       nodes_already_visited.insert( remote );

       try
       {
          std::shared_ptr<peer_probe> probe(new peer_probe());
          probe->start(remote, my_node_id, chain_id);
          probes.emplace_back( std::move( probe ) );
       }
       catch (const fc::exception&)
       {
          std::cerr << "Failed to connect " << fc::string(remote) << " - skipping!" << std::endl;
       }
    }

    if (!probes.empty())
    {
       fc::yield();
       std::vector<std::shared_ptr<peer_probe>> running;
       for ( auto& probe : probes ) {
          if (probe->_probe_complete_promise->error())
          {
             std::cerr << fc::string(probe->_remote) << " ran into an error!\n";
             continue;
          }
          if (!probe->_probe_complete_promise->ready())
          {
             running.push_back( probe );
             continue;
          }

          if( probe->_node_id.valid() )
          {
             graphene::net::address_info this_node_info;
             this_node_info.direction = graphene::net::peer_connection_direction::outbound;
             this_node_info.firewalled = graphene::net::firewalled_state::not_firewalled;
             this_node_info.remote_endpoint = probe->_remote;
             this_node_info.node_id = probe->_node_id;

             connections_by_node_id[this_node_info.node_id] = probe->_peers;
             if (address_info_by_node_id.find(this_node_info.node_id) == address_info_by_node_id.end())
                address_info_by_node_id[this_node_info.node_id] = this_node_info;
          }

          for (const graphene::net::address_info& info : probe->_peers)
          {
             if (nodes_already_visited.find(info.remote_endpoint) == nodes_already_visited.end() &&
                 info.firewalled == graphene::net::firewalled_state::not_firewalled &&
                 nodes_to_visit_set.find(info.remote_endpoint) == nodes_to_visit_set.end())
             {
                nodes_to_visit.push(info.remote_endpoint);
                nodes_to_visit_set.insert(info.remote_endpoint);
             }
             if (address_info_by_node_id.find(info.node_id) == address_info_by_node_id.end())
                address_info_by_node_id[info.node_id] = info;
          }
       }
       probes = std::move( running );
       std::cout << address_info_by_node_id.size() << " checked, "
                 << probes.size() << " active, "
                 << nodes_to_visit.size() << " to do\n";
    }
  }

  graphene::net::node_id_t seed_node_id;
  std::set<graphene::net::node_id_t> non_firewalled_nodes_set;
  for (const auto& address_info_for_node : address_info_by_node_id)
  {
    if (address_info_for_node.second.remote_endpoint == seed_node1)
      seed_node_id = address_info_for_node.first;
    if (address_info_for_node.second.firewalled == graphene::net::firewalled_state::not_firewalled)
      non_firewalled_nodes_set.insert(address_info_for_node.first);
  }
  std::set<graphene::net::node_id_t> seed_node_connections;
  for (const graphene::net::address_info& info : connections_by_node_id[seed_node_id])
    seed_node_connections.insert(info.node_id);
  std::set<graphene::net::node_id_t> seed_node_missing_connections;
  std::set_difference(non_firewalled_nodes_set.begin(), non_firewalled_nodes_set.end(),
                      seed_node_connections.begin(), seed_node_connections.end(),
                      std::inserter(seed_node_missing_connections, seed_node_missing_connections.end()));
  seed_node_missing_connections.erase(seed_node_id);

  std::ofstream dot_stream((data_dir / "network_graph.dot").string().c_str());

  dot_stream << "graph G {\n";
  dot_stream << "  // Total " << address_info_by_node_id.size() << " nodes, firewalled: " << (address_info_by_node_id.size() - non_firewalled_nodes_set.size())
                              << ", non-firewalled: " << non_firewalled_nodes_set.size() << "\n";
  dot_stream << "  // Seed node is " << (std::string)address_info_by_node_id[seed_node_id].remote_endpoint << " id: " << fc::variant( seed_node_id, 1 ).as_string() << "\n";
  dot_stream << "  // Seed node is connected to " << connections_by_node_id[seed_node_id].size() << " nodes\n";
  dot_stream << "  // Seed node is missing connections to " << seed_node_missing_connections.size() << " non-firewalled nodes:\n";
  for (const graphene::net::node_id_t& id : seed_node_missing_connections)
    dot_stream << "  //           " << (std::string)address_info_by_node_id[id].remote_endpoint << "\n";

  dot_stream << "  layout=\"circo\";\n";

  for (const auto& address_info_for_node : address_info_by_node_id)
  {
    dot_stream << "  \"" << fc::variant( address_info_for_node.first, 1 ).as_string() << "\"[label=\"" << (std::string)address_info_for_node.second.remote_endpoint << "\"";
    if (address_info_for_node.second.firewalled != graphene::net::firewalled_state::not_firewalled)
      dot_stream << ",shape=rectangle";
    dot_stream << "];\n";
  }
  for (auto& node_and_connections : connections_by_node_id)
    for (const graphene::net::address_info& this_connection : node_and_connections.second)
      dot_stream << "  \"" << fc::variant( node_and_connections.first, 2 ).as_string() << "\" -- \"" << fc::variant( this_connection.node_id, 1 ).as_string() << "\";\n";

  dot_stream << "}\n";

  return 0;
}
