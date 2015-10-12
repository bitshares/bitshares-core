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
#include <graphene/net/core_messages.hpp>


namespace graphene { namespace net {

  const core_message_type_enum trx_message::type                             = core_message_type_enum::trx_message_type;
  const core_message_type_enum block_message::type                           = core_message_type_enum::block_message_type;
  const core_message_type_enum item_ids_inventory_message::type              = core_message_type_enum::item_ids_inventory_message_type;
  const core_message_type_enum blockchain_item_ids_inventory_message::type   = core_message_type_enum::blockchain_item_ids_inventory_message_type;
  const core_message_type_enum fetch_blockchain_item_ids_message::type       = core_message_type_enum::fetch_blockchain_item_ids_message_type;
  const core_message_type_enum fetch_items_message::type                     = core_message_type_enum::fetch_items_message_type;
  const core_message_type_enum item_not_available_message::type              = core_message_type_enum::item_not_available_message_type;
  const core_message_type_enum hello_message::type                           = core_message_type_enum::hello_message_type;
  const core_message_type_enum connection_accepted_message::type             = core_message_type_enum::connection_accepted_message_type;
  const core_message_type_enum connection_rejected_message::type             = core_message_type_enum::connection_rejected_message_type;
  const core_message_type_enum address_request_message::type                 = core_message_type_enum::address_request_message_type;
  const core_message_type_enum address_message::type                         = core_message_type_enum::address_message_type;
  const core_message_type_enum closing_connection_message::type              = core_message_type_enum::closing_connection_message_type;
  const core_message_type_enum current_time_request_message::type            = core_message_type_enum::current_time_request_message_type;
  const core_message_type_enum current_time_reply_message::type              = core_message_type_enum::current_time_reply_message_type;
  const core_message_type_enum check_firewall_message::type                  = core_message_type_enum::check_firewall_message_type;
  const core_message_type_enum check_firewall_reply_message::type            = core_message_type_enum::check_firewall_reply_message_type;
  const core_message_type_enum get_current_connections_request_message::type = core_message_type_enum::get_current_connections_request_message_type;
  const core_message_type_enum get_current_connections_reply_message::type   = core_message_type_enum::get_current_connections_reply_message_type;

} } // graphene::net

