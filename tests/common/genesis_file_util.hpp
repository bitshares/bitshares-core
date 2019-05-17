#pragma once

#include <boost/filesystem.hpp>
#include <fc/io/json.hpp>
#include <graphene/chain/genesis_state.hpp>


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

namespace graphene { namespace app { 

   //////
   /// @brief attempt to find an available port on localhost
   /// @returns an available port number, or -1 on error
   /////
   int get_available_port()
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
  
   namespace detail {
   /////////
   /// @brief forward declaration, using as a hack to generate a genesis.json file
   /// for testing
   /////////
   graphene::chain::genesis_state_type create_example_genesis();
} } } // graphene::app::detail

/////////
/// @brief create a genesis_json file
/// @param directory the directory to place the file "genesis.json"
/// @returns the full path to the file
////////
boost::filesystem::path create_genesis_file(fc::temp_directory& directory) {
   boost::filesystem::path genesis_path = boost::filesystem::path{directory.path().generic_string()} / "genesis.json";
   fc::path genesis_out = genesis_path;
   graphene::chain::genesis_state_type genesis_state = graphene::app::detail::create_example_genesis();

   /* Work In Progress: Place some accounts in the Genesis file so as to pre-make some accounts to play with
   std::string test_prefix = "test";
   // helper lambda
   auto get_test_key = [&]( std::string prefix, uint32_t i ) -> public_key_type
   {
      return fc::ecc::private_key::regenerate( fc::sha256::hash( test_prefix + prefix + std::to_string(i) ) ).get_public_key();
   };

   // create 2 accounts to use
   for (int i = 1; i <= 2; ++i )
   {
      genesis_state_type::initial_account_type dev_account(
            test_prefix + std::to_string(i),
            get_test_key("owner-", i),
            get_test_key("active-", i),
            false);

      genesis_state.initial_accounts.push_back(dev_account);
      // give her some coin

   }
   */

   fc::json::save_to_file(genesis_state, genesis_out);
   return genesis_path;
}
