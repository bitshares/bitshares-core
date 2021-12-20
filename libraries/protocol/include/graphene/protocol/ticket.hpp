/*
 * Copyright (c) 2020 Abit More, and contributors.
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
#include <graphene/protocol/base.hpp>
#include <graphene/protocol/asset.hpp>

#include <fc/optional.hpp>

namespace graphene { namespace protocol {

   /// Type of a ticket
   enum ticket_type
   {
      liquid            = 0,
      lock_180_days     = 1,
      lock_360_days     = 2,
      lock_720_days     = 3,
      lock_forever      = 4,
      TICKET_TYPE_COUNT = 5
   };

   /**
    * @brief Creates a new ticket
    * @ingroup operations
    */
   struct ticket_create_operation : public base_operation
   {
      struct fee_parameters_type { uint64_t fee = 50 * GRAPHENE_BLOCKCHAIN_PRECISION; };

      asset           fee;         ///< Operation fee
      account_id_type account;     ///< The account who creates the ticket
      unsigned_int    target_type; ///< The target ticket type, see @ref ticket_type
      asset           amount;      ///< The amount of the ticket

      extensions_type extensions;  ///< Unused. Reserved for future use.

      account_id_type fee_payer()const { return account; }
      void            validate()const;
   };

   /**
    * @brief Updates an existing ticket
    * @ingroup operations
    */
   struct ticket_update_operation : public base_operation
   {
      struct fee_parameters_type { uint64_t fee = 50 * GRAPHENE_BLOCKCHAIN_PRECISION; };

      asset           fee;         ///< Operation fee
      ticket_id_type  ticket;      ///< The ticket to update
      account_id_type account;     ///< The account who owns the ticket
      unsigned_int    target_type; ///< New target ticket type, see @ref ticket_type
      optional<asset> amount_for_new_target; ///< The amount to be used for the new target

      extensions_type extensions;  ///< Unused. Reserved for future use.

      account_id_type fee_payer()const { return account; }
      void            validate()const;
   };

} } // graphene::protocol

FC_REFLECT_ENUM( graphene::protocol::ticket_type,
                 (liquid)(lock_180_days)(lock_360_days)(lock_720_days)(lock_forever)(TICKET_TYPE_COUNT) )

FC_REFLECT( graphene::protocol::ticket_create_operation::fee_parameters_type, (fee) )
FC_REFLECT( graphene::protocol::ticket_update_operation::fee_parameters_type, (fee) )

FC_REFLECT( graphene::protocol::ticket_create_operation,
            (fee)(account)(target_type)(amount)(extensions) )
FC_REFLECT( graphene::protocol::ticket_update_operation,
            (fee)(ticket)(account)(target_type)(amount_for_new_target)(extensions) )

GRAPHENE_DECLARE_EXTERNAL_SERIALIZATION( graphene::protocol::ticket_create_operation::fee_parameters_type )
GRAPHENE_DECLARE_EXTERNAL_SERIALIZATION( graphene::protocol::ticket_update_operation::fee_parameters_type )

GRAPHENE_DECLARE_EXTERNAL_SERIALIZATION( graphene::protocol::ticket_create_operation )
GRAPHENE_DECLARE_EXTERNAL_SERIALIZATION( graphene::protocol::ticket_update_operation )
