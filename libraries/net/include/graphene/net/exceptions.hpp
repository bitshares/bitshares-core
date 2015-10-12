/*
 * Copyright (c) 2015 Cryptonomex, Inc., and contributors.  All rights reserved.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */
#pragma once
#include <fc/exception/exception.hpp>

namespace graphene { namespace net {
   // registered in node.cpp 
   
   FC_DECLARE_EXCEPTION( net_exception, 90000, "P2P Networking Exception" ); 
   FC_DECLARE_DERIVED_EXCEPTION( send_queue_overflow,                   graphene::net::net_exception, 90001, "send queue for this peer exceeded maximum size" ); 
   FC_DECLARE_DERIVED_EXCEPTION( insufficient_relay_fee,                graphene::net::net_exception, 90002, "insufficient relay fee" );
   FC_DECLARE_DERIVED_EXCEPTION( already_connected_to_requested_peer,   graphene::net::net_exception, 90003, "already connected to requested peer" );
   FC_DECLARE_DERIVED_EXCEPTION( block_older_than_undo_history,         graphene::net::net_exception, 90004, "block is older than our undo history allows us to process" );
   FC_DECLARE_DERIVED_EXCEPTION( peer_is_on_an_unreachable_fork,        graphene::net::net_exception, 90005, "peer is on another fork" );
   FC_DECLARE_DERIVED_EXCEPTION( unlinkable_block_exception,            graphene::net::net_exception, 90006, "unlinkable block" )

} }
