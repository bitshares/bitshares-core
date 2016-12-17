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
#include <graphene/chain/protocol/authority.hpp>
#include <graphene/db/generic_index.hpp>
#include <boost/multi_index/composite_key.hpp>

namespace graphene { namespace chain {
  /***
   * A descriptor of a particular withdrawal period
   */
  struct withdrawal_period_descriptor {
     withdrawal_period_descriptor(const time_point_sec start, const time_point_sec end, const asset available, const asset claimed)
             : period_start_time(start), period_end_time(end), available_this_period(available), claimed_this_period(claimed) {}

     // Start of period
     time_point_sec period_start_time;

     // End of period
     time_point_sec period_end_time;

     // Quantify how much is still available to be withdrawn during this period
     asset available_this_period;

     // Quantify how much has already been claimed during this period
     asset claimed_this_period;

     string const to_string() const {
         string asset_id = fc::to_string(available_this_period.asset_id.space_id)
                           + "." + fc::to_string(available_this_period.asset_id.type_id)
                           + "." + fc::to_string(available_this_period.asset_id.instance.value);
         string text = fc::to_string(available_this_period.amount.value)
                       + " " + asset_id
                       + " is available from " + period_start_time.to_iso_string()
                       + " to " + period_end_time.to_iso_string();
         return text;
     }
  };

  /**
   * @class withdraw_permission_object
   * @brief Grants another account authority to withdraw a limited amount of funds per interval
   *
   * The primary purpose of this object is to enable recurring payments on the blockchain. An account which wishes to
   * process a recurring payment may use a @ref withdraw_permission_claim_operation to reference an object of this type
   * and withdraw up to @ref withdrawal_limit from @ref withdraw_from_account. Only @ref authorized_account may do
   * this. Any number of withdrawals may be made so long as the total amount withdrawn per period does not exceed the
   * limit for any given period.
   */
  class withdraw_permission_object : public graphene::db::abstract_object<withdraw_permission_object>
  {
     public:
        static const uint8_t space_id = protocol_ids;
        static const uint8_t type_id  = withdraw_permission_object_type;

        /// The account authorizing @ref authorized_account to withdraw from it
        account_id_type    withdraw_from_account;
        /// The account authorized to make withdrawals from @ref withdraw_from_account
        account_id_type    authorized_account;
        /// The maximum amount which may be withdrawn per period. All withdrawals must be of this asset type
        asset              withdrawal_limit;
        /// The duration of a withdrawal period in seconds
        uint32_t           withdrawal_period_sec = 0;
       /***
        * The beginning of the next withdrawal period
        * WARNING: Due to caching, this value does not always represent the start of the next or current period (because it is only updated after a withdrawal operation such as claim).  For the latest current period, use current_period().
        * @see current_period()
        */
        time_point_sec     period_start_time;
        /// The time at which this withdraw permission expires
        time_point_sec     expiration;

       /***
        * Tracks the total amount
        * WARNING: Due to caching, this value does not always represent the total amount claimed during the current period; it may represent what was claimed during the last claimed period (because it is only updated after a withdrawal operation such as claim).  For the latest current period, use current_period().
        * @see current_period()
        */
        share_type         claimed_this_period;

       /***
        * Determine how much is still available to be claimed during the period that contains a time of interest.  This object and function is mainly intended to be used with the "current" time as a parameter.  The current time can be obtained from the time of the current head of the blockchain.
        */
        asset              available_this_period( fc::time_point_sec current_time )const
        {
           if( current_time >= period_start_time + withdrawal_period_sec )
              return withdrawal_limit;
           return asset(
              ( withdrawal_limit.amount > claimed_this_period )
              ? withdrawal_limit.amount - claimed_this_period
              : 0, withdrawal_limit.asset_id );
        }

       /***
        * Get a description of the current withdrawal period
        * @param current_time   Current time
        * @return A description of the current period
        */
       withdrawal_period_descriptor current_period(fc::time_point_sec current_time) const {
           // @todo [6] Is there a potential race condition where a call to available_this_period might become out of sync with this function's later use of period start time?
           asset available = available_this_period(current_time);
           asset claimed = asset(withdrawal_limit.amount - available.amount, withdrawal_limit.asset_id);
           auto periods = (current_time - period_start_time).to_seconds() / withdrawal_period_sec;
           time_point_sec current_period_start = period_start_time + (periods * withdrawal_period_sec);
           time_point_sec current_period_end = current_period_start + withdrawal_period_sec;
           withdrawal_period_descriptor descriptor = withdrawal_period_descriptor(current_period_start, current_period_end, available, claimed);

           return descriptor;
       }
   };

   struct by_from;
   struct by_authorized;
   struct by_expiration;

   typedef multi_index_container<
      withdraw_permission_object,
      indexed_by<
         ordered_unique< tag<by_id>, member< object, object_id_type, &object::id > >,
         ordered_unique< tag<by_from>,
            composite_key< withdraw_permission_object,
               member<withdraw_permission_object, account_id_type, &withdraw_permission_object::withdraw_from_account>,
               member< object, object_id_type, &object::id >
            >
         >,
         ordered_unique< tag<by_authorized>,
            composite_key< withdraw_permission_object,
               member<withdraw_permission_object, account_id_type, &withdraw_permission_object::authorized_account>,
               member< object, object_id_type, &object::id >
            >
         >,
         ordered_unique< tag<by_expiration>,
            composite_key< withdraw_permission_object,
               member<withdraw_permission_object, time_point_sec, &withdraw_permission_object::expiration>,
               member< object, object_id_type, &object::id >
            >
         >
      >
   > withdraw_permission_object_multi_index_type;

   typedef generic_index<withdraw_permission_object, withdraw_permission_object_multi_index_type> withdraw_permission_index;


} } // graphene::chain

FC_REFLECT_DERIVED( graphene::chain::withdraw_permission_object, (graphene::db::object),
                    (withdraw_from_account)
                    (authorized_account)
                    (withdrawal_limit)
                    (withdrawal_period_sec)
                    (period_start_time)
                    (expiration)
                    (claimed_this_period)
                 )
