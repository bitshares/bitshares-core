#include "application_helper.hpp"
#include "genesis_file_util.hpp"
#include <graphene/account_history/account_history_plugin.hpp>
#include <graphene/market_history/market_history_plugin.hpp>
#include <graphene/witness/witness.hpp>
#include <graphene/grouped_orders/grouped_orders_plugin.hpp>
#include <boost/filesystem.hpp>
#include <memory>
#include <fstream>

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

application_runner::application_runner(std::shared_ptr<fc::path> dir, int port)  : application_runner() 
{
   _dir = dir;
   if (port != 0) 
      p2p_port_number = port; 
}
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
   _dir = std::make_shared<fc::path>();
}

bool file_exists(std::string path)
{
   std::ifstream s(path);
   if (s)
      return true;
   return false;
}

void application_runner::start()
{
   std::string genesis_path = _dir->generic_string() + "/genesis.json";
   boost::filesystem::path genesis = boost::filesystem::path(genesis_path);
   if (!file_exists(genesis_path))
      genesis = create_genesis_file(*_dir);
   start(*_dir, genesis);
}
void application_runner::start(const fc::path& data_path, const boost::filesystem::path& genesis  )
{
   cfg.emplace(
      "rpc-endpoint", 
      boost::program_options::variable_value(std::string("127.0.0.1:" + std::to_string(rpc_port_number)), false)
   );
   cfg.emplace( "p2p-endpoint", 
         boost::program_options::variable_value(std::string("127.0.0.1:" + std::to_string(p2p_port_number)), false));
   cfg.emplace("genesis-json", boost::program_options::variable_value( genesis, false ));
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
   _app->initialize(data_path, cfg);

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
  seed_nodes.push_back(addr);
}

void application_runner::add_node(std::string addr)
{
   auto endpoints = application::resolve_string_to_ip_endpoints(addr);
   if (endpoints.empty())
   {
      std::cerr << "Invalid node address passed\n";
      return;
   }
   _app->p2p_node()->add_node( endpoints[0] );
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