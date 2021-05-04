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

#include <graphene/chain/types.hpp>
#include <graphene/db/generic_index.hpp>

#include <graphene/protocol/asset.hpp>
#include <graphene/protocol/ticket.hpp>

#include <boost/multi_index/composite_key.hpp>

namespace graphene { namespace chain {

using namespace graphene::db;

using graphene::protocol::ticket_type;

/// Status of a ticket
enum ticket_status
{
   charging,
   stable,
   withdrawing,
   TICKET_STATUS_COUNT
};

/// Version of a ticket
enum ticket_version
{
   ticket_v1 = 1,
   ticket_v2 = 2
};

/**
 *  @brief a ticket for governance voting
 *  @ingroup object
 *  @ingroup protocol
 *
 */
class ticket_object : public abstract_object<ticket_object>
{
   public:
      static constexpr uint8_t space_id = protocol_ids;
      static constexpr uint8_t type_id  = ticket_object_type;

      account_id_type  account;      ///< The account who owns the ticket
      ticket_type      target_type;  ///< The target type of the ticket
      asset            amount;       ///< The token type and amount in the ticket

      ticket_type      current_type; ///< The current type of the ticket
      ticket_status    status;       ///< The status of the ticket
      share_type       value;        ///< The current value of the ticket
      time_point_sec   next_auto_update_time; ///< The next time that the ticket will be automatically updated

      /// When the account has ever started a downgrade or withdrawal, the scheduled auto-update time is stored here
      time_point_sec   next_type_downgrade_time = time_point_sec::maximum();

      // Configurations
      static constexpr uint32_t lock_forever_update_steps = 4;
      static constexpr uint32_t seconds_per_lock_forever_update_step = 180 * 86400;
      static constexpr uint32_t seconds_per_charging_step = 15 * 86400;
      static constexpr uint32_t seconds_to_cancel_charging = 7 * 86400;
      static uint32_t seconds_to_downgrade( ticket_type i ) {
         static constexpr uint32_t _seconds_to_downgrade[] = { 180 * 86400, 180 * 86400, 360 * 86400 };
         return _seconds_to_downgrade[ static_cast<uint8_t>(i) ];
      }
      static uint8_t value_multiplier( ticket_type i, ticket_version version ) {
         static constexpr uint32_t _value_multiplier_v1[] = { 1, 2, 4, 8, 8, 0 };
         static constexpr uint32_t _value_multiplier_v2[] = { 0, 2, 4, 8, 8, 0 };
         return ( version == ticket_v1 ? _value_multiplier_v1[ static_cast<uint8_t>(i) ]
                                       : _value_multiplier_v2[ static_cast<uint8_t>(i) ] );
      }

      /// Initialize member variables for a ticket newly created from account balance
      void init_new( time_point_sec now, account_id_type new_account,
                     ticket_type new_target_type, const asset& new_amount, ticket_version version );

      /// Initialize member variables for a ticket split from another ticket
      void init_split( time_point_sec now, const ticket_object& old_ticket,
                       ticket_type new_target_type, const asset& new_amount, ticket_version version );

      /// Set a new target type and update member variables accordingly
      void update_target_type( time_point_sec now, ticket_type new_target_type, ticket_version version );

      /// Adjust amount and update member variables accordingly
      void adjust_amount( const asset& delta_amount, ticket_version version );

      /// Update the ticket when it's time
      void auto_update( ticket_version version );

   private:
      /// Recalculate value of the ticket
      void update_value( ticket_version version );

};

struct by_next_update;
struct by_account;

/**
* @ingroup object_index
*/
typedef multi_index_container<
   ticket_object,
   indexed_by<
      ordered_unique< tag<by_id>, member< object, object_id_type, &object::id > >,
      ordered_unique< tag<by_next_update>,
         composite_key< ticket_object,
            member< ticket_object, time_point_sec, &ticket_object::next_auto_update_time>,
            member< object, object_id_type, &object::id>
         >
      >,
      ordered_unique< tag<by_account>,
         composite_key< ticket_object,
            member< ticket_object, account_id_type, &ticket_object::account>,
            member< object, object_id_type, &object::id>
         >
      >
   >
> ticket_multi_index_type;

/**
* @ingroup object_index
*/
typedef generic_index<ticket_object, ticket_multi_index_type> ticket_index;

} } // graphene::chain

FC_REFLECT_ENUM( graphene::chain::ticket_status,
                 (charging)(stable)(withdrawing)(TICKET_STATUS_COUNT) )

MAP_OBJECT_ID_TO_TYPE( graphene::chain::ticket_object )

FC_REFLECT_TYPENAME( graphene::chain::ticket_object )

GRAPHENE_DECLARE_EXTERNAL_SERIALIZATION( graphene::chain::ticket_object )
