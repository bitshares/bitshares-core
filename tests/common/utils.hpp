/*
 * Copyright (c) 2015 Cryptonomex, Inc., and contributors.
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
#pragma once

#include <fc/thread/thread.hpp>
#include <boost/test/included/unit_test.hpp>

#ifdef _WIN32
   #ifndef _WIN32_WINNT
      #define _WIN32_WINNT 0x0501
   #endif
   #include <winsock2.h>
   #include <ws2tcpip.h>
#else
   #include <sys/types.h>
   #include <sys/socket.h>
   #include <netinet/in.h>
   #include <netinet/ip.h>
#endif

namespace fc {

   /** Waits for F() to return true before max_duration has passed.
    */
   template<typename Functor>
   static void wait_for( const fc::microseconds max_duration, const Functor&& f )
   {
      const auto start = fc::time_point::now();
      while( !f() && fc::time_point::now() < start + max_duration )
         fc::usleep(fc::milliseconds(100));
      BOOST_REQUIRE( f() );
   }

namespace network {
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

} // namespace fc::network

} // namespace fc
