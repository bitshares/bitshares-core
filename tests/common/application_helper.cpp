#include "application_helper.hpp"
#include "genesis_file_util.hpp"
#include <graphene/account_history/account_history_plugin.hpp>
#include <graphene/market_history/market_history_plugin.hpp>
#include <graphene/witness/witness.hpp>
#include <graphene/grouped_orders/grouped_orders_plugin.hpp>
#include <boost/filesystem.hpp>

#ifdef _WIN32
   #ifndef _WIN32_WINNT
      #define _WIN32_WINNT 0x0501
   #endif
   #include <winsock2.h>
   #include <WS2tcpip.h>
int sockInit(void)
{
   WSADATA wsa_data;
   return WSAStartup(MAKEWORD(1,1), &wsa_data);
}
int sockQuit(void)
{
   return WSACleanup();
}
#else
   #include <sys/socket.h>
   #include <netinet/ip.h>
   #include <sys/types.h>
#endif

#include <boost/filesystem/path.hpp>

namespace graphene { namespace test {

application_runner::application_runner()
{
   _app = std::make_shared<graphene::app::application>();

   _app->register_plugin< graphene::account_history::account_history_plugin >(true);
   _app->register_plugin< graphene::market_history::market_history_plugin >(true);
   _app->register_plugin< graphene::witness_plugin::witness_plugin >(true);
   _app->register_plugin< graphene::grouped_orders::grouped_orders_plugin >(true);
   _app->startup_plugins();
#ifdef _WIN32
   sockInit();
#endif
   rpc_port_number = get_available_port();
   p2p_port_number = get_available_port();
}

void application_runner::start()
{
   boost::program_options::variables_map cfg;
   cfg.emplace(
      "rpc-endpoint", 
      boost::program_options::variable_value(std::string("127.0.0.1:" + std::to_string(rpc_port_number)), false)
   );
   cfg.emplace( "p2p-endpoint", 
         boost::program_options::variable_value(std::string("127.0.0.1:" + std::to_string(p2p_port_number)), false));
   cfg.emplace("genesis-json", boost::program_options::variable_value(create_genesis_file(_dir), false));
   std::string seed_node_string = "[";
   bool needs_comma = false;
   for(auto url : seed_nodes)
   {
      if (needs_comma)
         seed_node_string += ", ";
      seed_node_string += "\"" + url + "\"";
      needs_comma = true;
   }
   seed_node_string += "]";
   cfg.emplace("seed-nodes", boost::program_options::variable_value(seed_node_string, false));
   _app->initialize(_dir.path(), cfg);

   _app->initialize_plugins(cfg);
   _app->startup_plugins();

   _app->startup();
   fc::usleep(fc::milliseconds(500));
}

std::shared_ptr<graphene::app::application> application_runner::get_app()
{
   return _app;
}

void application_runner::add_seed_node(std::string addr)
{
   /*
   std::vector<fc::ip::endpoint> endpoints = graphene::app::application::resolve_string_to_ip_endpoints(addr);
   for(const auto& ep : endpoints)
      _app->p2p_node()->add_node(ep);
   */
  seed_nodes.push_back(addr);
}

uint32_t application_runner::get_connection_count()
{
   return _app->p2p_node()->get_connection_count();
}

bool application_runner::is_connected( std::string addr )
{
   auto peer_statuses = _app->p2p_node()->get_connected_peers();
   for (auto status : peer_statuses )
   {
      std::string host = status.host;
      if ( host == addr )
         return true;
   }
   return false;
}

//////
/// @brief attempt to find an available port on localhost
/// @returns an available port number, or -1 on error
/////
int application_runner::get_available_port()
{
   struct sockaddr_in sin;
   int socket_fd = socket(AF_INET, SOCK_STREAM, 0);
   if (socket_fd == -1)
      return -1;
   sin.sin_family = AF_INET;
   sin.sin_port = 0;
   sin.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
   if (::bind(socket_fd, (struct sockaddr*)&sin, sizeof(struct sockaddr_in)) == -1)
      return -1;
   socklen_t len = sizeof(sin);
   if (getsockname(socket_fd, (struct sockaddr *)&sin, &len) == -1)
      return -1;
#ifdef _WIN32
   closesocket(socket_fd);
#else
   close(socket_fd);
#endif
   return ntohs(sin.sin_port);
}

}}
