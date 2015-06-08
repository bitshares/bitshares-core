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

#include <graphene/chain/vesting_balance_object.hpp>

namespace graphene { namespace chain {

inline bool sum_below_max_shares( const asset& a, const asset& b )
{
   assert( GRAPHENE_MAX_SHARE_SUPPLY + GRAPHENE_MAX_SHARE_SUPPLY > GRAPHENE_MAX_SHARE_SUPPLY );
   return ( a.amount             <= GRAPHENE_MAX_SHARE_SUPPLY)
       && (            b.amount  <= GRAPHENE_MAX_SHARE_SUPPLY)
       && ((a.amount + b.amount) <= GRAPHENE_MAX_SHARE_SUPPLY)
      ;
}

asset linear_vesting_policy::get_allowed_withdraw( const vesting_policy_context& ctx ) const
{
   if( ctx.now <= begin_date )
      return asset( 0, ctx.balance.asset_id );
   if( vesting_seconds == 0 )
      return ctx.balance;

   int64_t elapsed_seconds = (ctx.now - begin_date).to_seconds();

   // if elapsed_seconds <= 0, then ctx.now <= begin_date,
   // and we should have returned above.
   assert( elapsed_seconds > 0 );

   fc::uint128_t total_allowed = begin_balance.value;
   total_allowed *= uint64_t( elapsed_seconds );
   total_allowed /= vesting_seconds;

   if( total_allowed <= total_withdrawn.value )
      return asset( 0, ctx.balance.asset_id );
   total_allowed -= total_withdrawn.value;
   FC_ASSERT( total_allowed <= GRAPHENE_MAX_SHARE_SUPPLY );
   return asset( total_allowed.to_uint64(), ctx.balance.asset_id );
}

void linear_vesting_policy::on_deposit( const vesting_policy_context& ctx )
{
   return;
}

bool linear_vesting_policy::is_deposit_allowed( const vesting_policy_context& ctx )const
{
   return (ctx.amount.asset_id == ctx.balance.asset_id)
      && sum_below_max_shares( ctx.amount, ctx.balance );
}

void linear_vesting_policy::on_withdraw( const vesting_policy_context& ctx )
{
   total_withdrawn += ctx.amount.amount;
   return;
}

bool linear_vesting_policy::is_withdraw_allowed( const vesting_policy_context& ctx )const
{
   return ( ctx.amount <= get_allowed_withdraw( ctx ) );
}

fc::uint128_t cdd_vesting_policy::compute_coin_seconds_earned( const vesting_policy_context& ctx )const
{
   assert( ctx.now >= coin_seconds_earned_last_update );
   int64_t delta_seconds = (ctx.now - coin_seconds_earned_last_update).to_seconds();
   assert( delta_seconds >= 0 );

   fc::uint128_t delta_coin_seconds = ctx.balance.amount.value;
   delta_coin_seconds *= delta_seconds;

   fc::uint128_t coin_seconds_earned_cap = ctx.balance.amount.value;
   coin_seconds_earned_cap *= vesting_seconds;

   return std::min(
      coin_seconds_earned + delta_coin_seconds,
      coin_seconds_earned_cap
      );
}

void cdd_vesting_policy::update_coin_seconds_earned( const vesting_policy_context& ctx )
{
   coin_seconds_earned = compute_coin_seconds_earned( ctx );
   coin_seconds_earned_last_update = ctx.now;
   return;
}

asset cdd_vesting_policy::get_allowed_withdraw( const vesting_policy_context& ctx )const
{
   fc::uint128_t cs_earned = compute_coin_seconds_earned( ctx );
   fc::uint128_t withdraw_available = cs_earned / vesting_seconds;
   assert( withdraw_available <= ctx.balance.amount.value );
   return asset( withdraw_available.to_uint64(), ctx.balance.asset_id );
}

void cdd_vesting_policy::on_deposit( const vesting_policy_context& ctx )
{
   update_coin_seconds_earned( ctx );
   return;
}

void cdd_vesting_policy::on_withdraw( const vesting_policy_context& ctx )
{
   update_coin_seconds_earned( ctx );
   fc::uint128_t coin_seconds_needed = ctx.amount.amount.value;
   coin_seconds_needed *= vesting_seconds;
   // is_withdraw_allowed should forbid any withdrawal that
   // would trigger this assert
   assert( coin_seconds_needed <= coin_seconds_earned );

   coin_seconds_earned -= coin_seconds_needed;
   return;
}

bool cdd_vesting_policy::is_deposit_allowed( const vesting_policy_context& ctx )const
{
   return (ctx.amount.asset_id == ctx.balance.asset_id)
      && sum_below_max_shares( ctx.amount, ctx.balance );
}

bool cdd_vesting_policy::is_withdraw_allowed( const vesting_policy_context& ctx )const
{
   return ( ctx.amount <= get_allowed_withdraw( ctx ) );
}

#define VESTING_VISITOR( NAME, MAYBE_CONST )                  \
struct NAME ## _visitor                                       \
{                                                             \
   typedef decltype(                                          \
      std::declval<linear_vesting_policy>().NAME(             \
         std::declval<vesting_policy_context>())              \
      ) result_type;                                          \
                                                              \
   NAME ## _visitor(                                          \
      const asset& balance,                                   \
      const time_point_sec& now,                              \
      const asset& amount                                     \
      )                                                       \
   : ctx( balance, now, amount ) {}                           \
                                                              \
   template< typename Policy >                                \
   result_type                                                \
   operator()( MAYBE_CONST Policy& policy ) MAYBE_CONST       \
   {                                                          \
      return policy.NAME( ctx );                              \
   }                                                          \
                                                              \
   vesting_policy_context ctx;                                \
}

VESTING_VISITOR( on_deposit, );
VESTING_VISITOR( on_withdraw, );
VESTING_VISITOR( is_deposit_allowed, const );
VESTING_VISITOR( is_withdraw_allowed, const );

bool vesting_balance_object::is_deposit_allowed(const time_point_sec& now, const asset& amount)const
{
   return policy.visit( is_deposit_allowed_visitor( balance, now, amount ) );
}

bool vesting_balance_object::is_withdraw_allowed(const time_point_sec& now, const asset& amount)const
{
   bool result = policy.visit( is_withdraw_allowed_visitor( balance, now, amount ) );
   // if some policy allows you to withdraw more than your balance,
   //    there's a programming bug in the policy algorithm
   assert( (amount <= balance) || (!result) );
   return result;
}

void vesting_balance_object::deposit(const time_point_sec& now, const asset& amount)
{
   on_deposit_visitor vtor( balance, now, amount );
   policy.visit( vtor );
   balance += amount;
}

void vesting_balance_object::withdraw(const time_point_sec& now, const asset& amount)
{
   assert( amount <= balance );
   on_withdraw_visitor vtor( balance, now, amount );
   policy.visit( vtor );
   balance -= amount;
}

} } // graphene::chain
