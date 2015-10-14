/*
 * Copyright (c) 2015 Cryptonomex, Inc., and contributors.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:
 *
 * 1. Any modified source or binaries are used only with the BitShares network.
 *
 * 2. Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
 *
 * 3. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */
#include <graphene/p2p/node.hpp>

namespace graphene { namespace p2p {

   node::node( chain_database& db )
   :_db(db)
   {

   }

   node::~node()
   {

   }

   void node::add_peer( const fc::ip::endpoint& ep )
   {

   }

   void node::configure( const node_config& cfg )
   {
      listen_on_endpoint( cfg.server_endpoint, wait_if_not_available );

      /** don't allow node to go out of scope until accept loop exits */
      auto self = shared_from_this();
      _accept_loop_complete = fc::async( [self](){ self->accept_loop(); } )
   }

   void node::accept_loop()
   {
      auto self = shared_from_this();
      while( !_accept_loop_complete.canceled() )
      {
         try {
            auto new_peer = std::make_shared<peer_connection>(self);
            _tcp_server.accept( new_peer.get_socket() );

            if( _accept_loop_complete.canceled() )
               return;

            _peers.insert( new_peer );



            // limit the rate at which we accept connections to mitigate DOS attacks
            fc::usleep( fc::milliseconds(10) );
         } FC_CAPTURE_AND_RETHROW() 
      }
   } // accept_loop()



   void node::listen_on_endpoint( fc::ip::endpoint ep, bool wait_if_not_available )
   {
      if( ep.port() != 0 )
      {
        // if the user specified a port, we only want to bind to it if it's not already
        // being used by another application.  During normal operation, we set the
        // SO_REUSEADDR/SO_REUSEPORT flags so that we can bind outbound sockets to the
        // same local endpoint as we're listening on here.  On some platforms, setting
        // those flags will prevent us from detecting that other applications are
        // listening on that port.  We'd like to detect that, so we'll set up a temporary
        // tcp server without that flag to see if we can listen on that port.
        bool first = true;
        for( ;; )
        {
          bool listen_failed = false;

          try
          {
            fc::tcp_server temporary_server;
            if( listen_endpoint.get_address() != fc::ip::address() )
              temporary_server.listen( ep );
            else
              temporary_server.listen( ep.port() );
            break;
          }
          catch ( const fc::exception&)
          {
            listen_failed = true;
          }

          if (listen_failed)
          {
            if( wait_if_endpoint_is_busy )
            {
              std::ostringstream error_message_stream;
              if( first )
              {
                error_message_stream << "Unable to listen for connections on port " 
                                     << ep.port()
                                     << ", retrying in a few seconds\n";
                error_message_stream << "You can wait for it to become available, or restart "
                                        "this program using\n";
                error_message_stream << "the --p2p-port option to specify another port\n";
                first = false;
              }
              else
              {
                error_message_stream << "\nStill waiting for port " << listen_endpoint.port() << " to become available\n";
              }

              std::string error_message = error_message_stream.str();
              ulog(error_message);
              fc::usleep( fc::seconds(5 ) );
            }
            else // don't wait, just find a random port
            {
              wlog( "unable to bind on the requested endpoint ${endpoint}, "
                    "which probably means that endpoint is already in use",
                   ( "endpoint", ep ) );
              ep.set_port( 0 );
            }
          } // if (listen_failed)
        } // for(;;)
      } // if (listen_endpoint.port() != 0)


      _tcp_server.set_reuse_address();
      try
      {
        if( ep.get_address() != fc::ip::address() )
          _tcp_server.listen( ep );
        else
          _tcp_server.listen( ep.port() );

        _actual_listening_endpoint = _tcp_server.get_local_endpoint();
        ilog( "listening for connections on endpoint ${endpoint} (our first choice)",
              ( "endpoint", _actual_listening_endpoint ) );
      }
      catch ( fc::exception& e )
      {
        FC_RETHROW_EXCEPTION( e, error, 
             "unable to listen on ${endpoint}", ("endpoint",listen_endpoint ) );
      }
   }



} }
