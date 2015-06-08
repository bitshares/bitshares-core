/*
 * Copyright (c) 2015, Cryptonomex, Inc.
 * All rights reserved.
 *
 * This source code is provided for evaluation in private test networks only, until September 8, 2015. After this date, this license expires and
 * the code may not be used, modified or distributed for any purpose. Redistribution and use in source and binary forms, with or without modification,
 * are permitted until September 8, 2015, provided that the following conditions are met:
 *
 * 1. The code and/or derivative works are used only for private test networks consisting of no more than 10 P2P nodes.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#pragma once

#include <algorithm>

#include <fc/static_variant.hpp>
#include <fc/uint128.hpp>

#include <graphene/chain/asset.hpp>
#include <graphene/db/object.hpp>

namespace graphene { namespace chain {
   using namespace graphene::db;

   class vesting_balance_object;

   struct vesting_policy_context
   {
      vesting_policy_context(
         asset _balance,
         fc::time_point_sec _now,
         asset _amount )
         : balance( _balance ), now( _now ), amount( _amount ) {}
   
      asset balance;
      fc::time_point_sec now;
      asset amount;
   };

   /**
    * Linear vesting balance.
    */
   struct linear_vesting_policy
   {
      uint32_t                          vesting_seconds; // must be greater than zero
      fc::time_point_sec                begin_date;
      share_type                        begin_balance;   // same asset as balance
      share_type                        total_withdrawn; // same asset as balance

      asset get_allowed_withdraw( const vesting_policy_context& ctx )const;
      bool is_deposit_allowed( const vesting_policy_context& ctx )const;
      bool is_withdraw_allowed( const vesting_policy_context& ctx )const;
      void on_deposit( const vesting_policy_context& ctx );
      void on_withdraw( const vesting_policy_context& ctx );
   };

   struct cdd_vesting_policy
   {
      uint32_t                       vesting_seconds;
      fc::uint128_t                  coin_seconds_earned;
      fc::time_point_sec             coin_seconds_earned_last_update;

      /**
       * Compute coin_seconds_earned.  Used to
       * non-destructively figure out how many coin seconds
       * are available.
       */
      fc::uint128_t compute_coin_seconds_earned( const vesting_policy_context& ctx )const;

      /**
       * Update coin_seconds_earned and
       * coin_seconds_earned_last_update fields; called by both
       * on_deposit() and on_withdraw().
       */
      void update_coin_seconds_earned( const vesting_policy_context& ctx );

      asset get_allowed_withdraw( const vesting_policy_context& ctx )const;
      bool is_deposit_allowed( const vesting_policy_context& ctx )const;
      bool is_withdraw_allowed( const vesting_policy_context& ctx )const;
      void on_deposit( const vesting_policy_context& ctx );
      void on_withdraw( const vesting_policy_context& ctx );
   };

   typedef fc::static_variant<
      linear_vesting_policy,
      cdd_vesting_policy
      > vesting_policy;

   /**
    * Timelocked balance object is a balance that is locked by the
    * blockchain for a period of time.
    */
   class vesting_balance_object : public abstract_object<vesting_balance_object>
   {
      public:
         static const uint8_t space_id = protocol_ids;
         static const uint8_t type_id = vesting_balance_object_type;

         account_id_type                owner;
         asset                          balance;
         vesting_policy                 policy;

         vesting_balance_object() {}

         /**
          * Used to increase existing vesting balances.
          */
         void deposit( const fc::time_point_sec& now, const asset& amount );
         bool is_deposit_allowed( const fc::time_point_sec& now, const asset& amount )const;

         /**
          * Used to remove a vesting balance from the VBO.  As well
          * as the balance field, coin_seconds_earned and
          * coin_seconds_earned_last_update fields are updated.
          *
          * The money doesn't "go" anywhere; the caller is responsible
          * for crediting it to the proper account.
          */
         void withdraw( const fc::time_point_sec& now, const asset& amount );
         bool is_withdraw_allowed( const fc::time_point_sec& now, const asset& amount )const;
   };

} } // graphene::chain

FC_REFLECT( graphene::chain::linear_vesting_policy,
   (vesting_seconds)
   (begin_date)
   (begin_balance)
   (total_withdrawn)
)

FC_REFLECT( graphene::chain::cdd_vesting_policy,
   (vesting_seconds)
   (coin_seconds_earned)
   (coin_seconds_earned_last_update)
)

FC_REFLECT_DERIVED( graphene::chain::vesting_balance_object, (graphene::db::object),
   (owner)
   (balance)
   (policy)
)
