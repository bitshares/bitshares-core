#pragma once

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
   /*
    * @brief attempt to find an available port on localhost
    * @returns an available port number, or -1 on error
   */
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
} } // namespace graphene::app
