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

#include <graphene/chain/protocol/asset.hpp>
#include <graphene/db/object.hpp>
#include <graphene/db/generic_index.hpp>
#include <boost/multi_index/composite_key.hpp>
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/identity.hpp>

#include <fc/static_variant.hpp>
#include <fc/uint128.hpp>

#include <algorithm>



namespace graphene { namespace chain {
   using namespace graphene::db;

   class vesting_balance_object;

   struct vesting_policy_context
   {
      vesting_policy_context(
         asset _balance,
         fc::time_point_sec _now,
         asset _amount)
         : balance(_balance), now(_now), amount(_amount) {}

      asset              balance;
      fc::time_point_sec now;
      asset              amount;
   };

   /**
    * @brief Linear vesting balance with cliff
    *
    * This vesting balance type is used to mimic traditional stock vesting contracts where
    * each day a certain amount vests until it is fully matured.
    *
    * @note New funds may not be added to a linear vesting balance.
    */
   struct linear_vesting_policy
   {
      /// This is the time at which funds begin vesting.
      fc::time_point_sec begin_timestamp;
      /// No amount may be withdrawn before this many seconds of the vesting period have elapsed.
      uint32_t vesting_cliff_seconds = 0;
      /// Duration of the vesting period, in seconds. Must be greater than 0 and greater than vesting_cliff_seconds.
      uint32_t vesting_duration_seconds = 0;
      /// The total amount of asset to vest.
      share_type begin_balance;

      asset get_allowed_withdraw(const vesting_policy_context& ctx)const;
      bool is_deposit_allowed(const vesting_policy_context& ctx)const;
      bool is_deposit_vested_allowed(const vesting_policy_context&)const { return false; }
      bool is_withdraw_allowed(const vesting_policy_context& ctx)const;
      void on_deposit(const vesting_policy_context& ctx);
      void on_deposit_vested(const vesting_policy_context&)
      { FC_THROW( "May not deposit vested into a linear vesting balance." ); }
      void on_withdraw(const vesting_policy_context& ctx);
   };

   /**
    * @brief defines vesting in terms of coin-days accrued which allows for dynamic deposit/withdraw
    *
    * The economic effect of this vesting policy is to require a certain amount of "interest" to accrue
    * before the full balance may be withdrawn.  Interest accrues as coindays (balance * length held).  If
    * some of the balance is withdrawn, the remaining balance must be held longer.
    */
   struct cdd_vesting_policy
   {
      uint32_t                       vesting_seconds = 0;
      fc::uint128_t                  coin_seconds_earned;
      /** while coindays may accrue over time, none may be claimed before first_claim date */
      fc::time_point_sec             start_claim;
      fc::time_point_sec             coin_seconds_earned_last_update;

      /**
       * Compute coin_seconds_earned.  Used to
       * non-destructively figure out how many coin seconds
       * are available.
       */
      fc::uint128_t compute_coin_seconds_earned(const vesting_policy_context& ctx)const;

      /**
       * Update coin_seconds_earned and
       * coin_seconds_earned_last_update fields; called by both
       * on_deposit() and on_withdraw().
       */
      void update_coin_seconds_earned(const vesting_policy_context& ctx);

      asset get_allowed_withdraw(const vesting_policy_context& ctx)const;
      bool is_deposit_allowed(const vesting_policy_context& ctx)const;
      bool is_deposit_vested_allowed(const vesting_policy_context& ctx)const;
      bool is_withdraw_allowed(const vesting_policy_context& ctx)const;
      void on_deposit(const vesting_policy_context& ctx);
      void on_deposit_vested(const vesting_policy_context& ctx);
      void on_withdraw(const vesting_policy_context& ctx);
   };

     /**
    * @brief instant vesting policy
    *
    * This policy allows to withdraw everything that is on a balance immediately
    *
    */
   struct instant_vesting_policy
   {
      asset get_allowed_withdraw(const vesting_policy_context& ctx)const;
      bool is_deposit_allowed(const vesting_policy_context& ctx)const;
      bool is_deposit_vested_allowed(const vesting_policy_context&)const { return false; }
      bool is_withdraw_allowed(const vesting_policy_context& ctx)const;
      void on_deposit(const vesting_policy_context& ctx);
      void on_deposit_vested(const vesting_policy_context&);
      void on_withdraw(const vesting_policy_context& ctx);
   };

   typedef fc::static_variant<
      linear_vesting_policy,
      cdd_vesting_policy,
      instant_vesting_policy
      > vesting_policy;

   enum class vesting_balance_type {   unspecified,
                                       cashback,
                                       worker,
                                       witness,
                                       market_fee_sharing };
   /**
    * Vesting balance object is a balance that is locked by the blockchain for a period of time.
    */
   class vesting_balance_object : public abstract_object<vesting_balance_object>
   {
      public:
         static const uint8_t space_id = protocol_ids;
         static const uint8_t type_id = vesting_balance_object_type;

         /// Account which owns and may withdraw from this vesting balance
         account_id_type owner;
         /// Total amount remaining in this vesting balance
         /// Includes the unvested funds, and the vested funds which have not yet been withdrawn
         asset balance;
         /// The vesting policy stores details on when funds vest, and controls when they may be withdrawn
         vesting_policy policy;
         /// type of the vesting balance
         vesting_balance_type balance_type = vesting_balance_type::unspecified;

         vesting_balance_object() {}

         ///@brief Deposit amount into vesting balance, requiring it to vest before withdrawal
         void deposit(const fc::time_point_sec& now, const asset& amount);
         bool is_deposit_allowed(const fc::time_point_sec& now, const asset& amount)const;

         /// @brief Deposit amount into vesting balance, making the new funds vest immediately
         void deposit_vested(const fc::time_point_sec& now, const asset& amount);
         bool is_deposit_vested_allowed(const fc::time_point_sec& now, const asset& amount)const;

         /**
          * Used to remove a vesting balance from the VBO. As well as the
          * balance field, coin_seconds_earned and
          * coin_seconds_earned_last_update fields are updated.
          *
          * The money doesn't "go" anywhere; the caller is responsible for
          * crediting it to the proper account.
          */
         void withdraw(const fc::time_point_sec& now, const asset& amount);
         bool is_withdraw_allowed(const fc::time_point_sec& now, const asset& amount)const;

         /**
          * Get amount of allowed withdrawal.
          */
         asset get_allowed_withdraw(const time_point_sec& now)const;
   };
   /**
    * @ingroup object_index
    */
   struct by_account;
   // by_vesting_type index MUST NOT be used for iterating because order is not well-defined.
   struct by_vesting_type;

namespace detail {

   /**
      Calculate a hash for account_id_type and asset_id.
      Use 48 bit value (see object_id.hpp) for account_id and XOR it with 24 bit for asset_id
   */
   inline uint64_t vbo_mfs_hash(const account_id_type& account_id, const asset_id_type& asset_id)
   {
      return (asset_id.instance.value << 40) ^ account_id.instance.value;
   }

   /**
    * Used as CompatibleHash
      Calculate a hash vesting_balance_object
      if vesting_balance_object.balance_type is market_fee_sharing
         calculate has as vbo_mfs_hash(vesting_balance_object.owner, hash(vbo.balance.asset_id) (see vbo_mfs_hash)
      otherwise: hash_value(vesting_balance_object.id);
   */
   struct vesting_balance_object_hash
   {
      uint64_t operator()(const vesting_balance_object& vbo) const
      {
         if ( vbo.balance_type == vesting_balance_type::market_fee_sharing )
         {
            return vbo_mfs_hash(vbo.owner, vbo.balance.asset_id);
         }
         return hash_value(vbo.id);
      }
   };

   /**
    * Used as CompatiblePred
    * Compares two vesting_balance_objects
    * if vesting_balance_object.balance_type is a market_fee_sharing
    *    compare owners' ids and assets' ids
    * otherwise: vesting_balance_object.id
   */
   struct vesting_balance_object_equal
   {
      bool operator() (const vesting_balance_object& lhs, const vesting_balance_object& rhs) const
      {
         if ( ( lhs.balance_type == vesting_balance_type::market_fee_sharing ) &&
              ( lhs.balance_type == rhs.balance_type ) &&
              ( lhs.owner == rhs.owner ) &&
              ( lhs.balance.asset_id == rhs.balance.asset_id)
            )
         {
               return true;
         }
         return ( lhs.id == rhs.id );
      }
   };
} // detail

   typedef multi_index_container<
      vesting_balance_object,
      indexed_by<
         ordered_unique< tag<by_id>, member< object, object_id_type, &object::id >
         >,
         ordered_non_unique< tag<by_account>,
            member<vesting_balance_object, account_id_type, &vesting_balance_object::owner>
         >,
         hashed_unique< tag<by_vesting_type>,
            identity<vesting_balance_object>,
            detail::vesting_balance_object_hash,
            detail::vesting_balance_object_equal
         >
      >
   > vesting_balance_multi_index_type;
   /**
    * @ingroup object_index
    */
   typedef generic_index<vesting_balance_object, vesting_balance_multi_index_type> vesting_balance_index;

} } // graphene::chain

FC_REFLECT(graphene::chain::linear_vesting_policy,
           (begin_timestamp)
           (vesting_cliff_seconds)
           (vesting_duration_seconds)
           (begin_balance)
          )

FC_REFLECT(graphene::chain::cdd_vesting_policy,
           (vesting_seconds)
           (start_claim)
           (coin_seconds_earned)
           (coin_seconds_earned_last_update)
          )

FC_REFLECT_EMPTY( graphene::chain::instant_vesting_policy )

FC_REFLECT_TYPENAME( graphene::chain::vesting_policy )

FC_REFLECT_DERIVED(graphene::chain::vesting_balance_object, (graphene::db::object),
                   (owner)
                   (balance)
                   (policy)
                   (balance_type)
                  )

FC_REFLECT_ENUM( graphene::chain::vesting_balance_type, (unspecified)(cashback)(worker)(witness)(market_fee_sharing) )

