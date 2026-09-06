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

#include <cstdlib>   // std::rand, used to pick a candidate port
#include <cstring>   // memset

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
   namespace detail {
      /// Probe one port: bind it, then let it go again. Returns false if it is taken.
      inline bool port_is_free( int port )
      {
         int socket_fd = socket(AF_INET, SOCK_STREAM, 0);
         if (socket_fd == -1)
            return false;
         struct sockaddr_in sin;
         memset( &sin, 0, sizeof(sin) );
         sin.sin_family = AF_INET;
         sin.sin_port = htons( static_cast<uint16_t>(port) );
         sin.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
         const bool ok = ::bind(socket_fd, (struct sockaddr*)&sin, sizeof(sin)) != -1;
      #ifdef _WIN32
         closesocket(socket_fd);
      #else
         close(socket_fd);
      #endif
         return ok;
      }
   }

   //////
   /// @brief attempt to find an available port on localhost
   /// @returns an available port number, or -1 on error
   ///
   /// Asking the kernel for a port (sin_port = 0) hands one out of the ephemeral range --
   /// on Linux typically 32768-60999, which is the very range it also draws from for every
   /// outgoing connection. This function has to close the socket before returning the
   /// number, so between that close and the caller's own bind() the kernel is free to hand
   /// the same port to something else: an Elasticsearch connection from the plugin, another
   /// shard of a parallel test run, anything. The result is an intermittent
   ///
   ///     bind: Address already in use
   ///
   /// that depends on what else the machine happens to be doing -- which is why it appears
   /// on CI and not on a quiet development box, and why it moves between test cases and
   /// between Debug and Release runs rather than sticking to one.
   ///
   /// Drawing from below the ephemeral range removes that source of collision: the kernel
   /// does not spontaneously assign ports there. The window between probe and use still
   /// exists, but nothing is competing for it any more. The old behaviour is kept as a
   /// fallback so an unusual port configuration cannot leave the tests with no port at all.
   /////
   int get_available_port()
   {
      for( int attempt = 0; attempt < 64; ++attempt )
      {
         const int candidate = 10000 + ( std::rand() % 20000 );  // 10000-29999
         if( detail::port_is_free( candidate ) )
            return candidate;
      }

      // Fallback: let the kernel choose, as before.
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
