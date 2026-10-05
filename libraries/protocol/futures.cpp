/*
 * Copyright (c) 2026 BitShares contributors.
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
#include <graphene/protocol/futures.hpp>

#include <fc/io/raw.hpp>
#include <fc/uint128.hpp>

namespace graphene { namespace protocol {

bool is_valid_futures_symbol( const string& symbol )
{
   if( symbol.size() < GRAPHENE_FUTURES_MIN_SYMBOL_LENGTH
       || symbol.size() > GRAPHENE_FUTURES_MAX_SYMBOL_LENGTH )
      return false;

   // Same restricted alphabet as oracle names, for the same reason: a symbol is how a trader
   // picks a market out of a list, and a lookalike built from mixed case would be a way to get
   // someone to trade the wrong contract.
   char previous = 0;
   for( size_t i = 0; i < symbol.size(); ++i )
   {
      const char c = symbol[i];
      const bool is_alnum = ( c >= 'A' && c <= 'Z' ) || ( c >= '0' && c <= '9' );
      const bool is_sep   = ( '.' == c || '-' == c );
      if( !is_alnum && !is_sep )
         return false;
      if( is_sep && ( 0 == i || i + 1 == symbol.size() || '.' == previous || '-' == previous ) )
         return false;
      previous = c;
   }
   return true;
}

void futures_market_options::validate()const
{
   FC_ASSERT( initial_margin_ratio >= GRAPHENE_FUTURES_MIN_INITIAL_MARGIN_RATIO,
              "Initial margin ratio must be at least ${m} (i.e. at most ${x}x leverage)",
              ("m", GRAPHENE_FUTURES_MIN_INITIAL_MARGIN_RATIO)
              ("x", GRAPHENE_100_PERCENT / GRAPHENE_FUTURES_MIN_INITIAL_MARGIN_RATIO) );
   FC_ASSERT( initial_margin_ratio <= GRAPHENE_100_PERCENT,
              "Initial margin ratio may not exceed 100%" );

   FC_ASSERT( maintenance_margin_ratio > 0, "Maintenance margin ratio must be positive" );
   // Equal ratios would make a position liquidatable the instant it opened: it would meet the
   // requirement exactly, and the first adverse tick would put it under.
   FC_ASSERT( maintenance_margin_ratio < initial_margin_ratio,
              "Maintenance margin ratio (${m}) must be below the initial margin ratio (${i}), "
              "otherwise a position is liquidatable the moment it is opened",
              ("m", maintenance_margin_ratio)("i", initial_margin_ratio) );

   // A limit above 100%/second cannot bind on any real move and only reads as if it does.
   FC_ASSERT( max_mark_move_ppm <= 1000000,
              "Maximum mark move may not exceed 1000000 ppm (100%) per second" );

   FC_ASSERT( taker_fee_ppm <= GRAPHENE_FUTURES_MAX_TRADING_FEE_PPM,
              "Taker fee may not exceed ${m} ppm of notional",
              ("m", GRAPHENE_FUTURES_MAX_TRADING_FEE_PPM) );
   // A rebate above the fee would pay makers more than takers put in, so every fill would
   // drain the insurance fund rather than capitalise it.
   FC_ASSERT( maker_rebate_ppm <= taker_fee_ppm,
              "Maker rebate (${r}) may not exceed the taker fee (${t}), or every fill would "
              "pay out more than it collects",
              ("r", maker_rebate_ppm)("t", taker_fee_ppm) );

   FC_ASSERT( funding_interval_sec >= GRAPHENE_FUTURES_MIN_FUNDING_INTERVAL_SEC,
              "Funding interval must be at least ${s} seconds",
              ("s", GRAPHENE_FUTURES_MIN_FUNDING_INTERVAL_SEC) );
   FC_ASSERT( max_funding_rate_ppm <= GRAPHENE_FUTURES_MAX_FUNDING_RATE_PPM,
              "Maximum funding rate may not exceed ${m} ppm per interval",
              ("m", GRAPHENE_FUTURES_MAX_FUNDING_RATE_PPM) );
   // Zero would price the premium over an empty walk and divide by nothing. One reproduces the
   // top-of-book sampling this option exists to replace, so it is not offered either.
   FC_ASSERT( impact_size > 1, "Impact size must be greater than one contract" );
   FC_ASSERT( impact_size <= GRAPHENE_FUTURES_MAX_IMPACT_SIZE,
              "Impact size may not exceed ${m} contracts",
              ("m", GRAPHENE_FUTURES_MAX_IMPACT_SIZE) );

   // A penalty at or above the maintenance requirement would take more than the position has
   // left at the moment it is liquidated, leaving the owner owing money.
   FC_ASSERT( liquidation_penalty_ratio > 0,
              "Liquidation penalty must be positive, or nobody has any reason to liquidate" );
   FC_ASSERT( liquidation_penalty_ratio < maintenance_margin_ratio,
              "Liquidation penalty (${p}) must be below the maintenance margin ratio (${m}), "
              "otherwise liquidation takes more than the position has left",
              ("p", liquidation_penalty_ratio)("m", maintenance_margin_ratio) );
}

void futures_market_create_operation::validate()const
{
   FC_ASSERT( fee.amount >= 0, "Fee should not be negative" );
   FC_ASSERT( is_valid_futures_symbol( symbol ),
              "Futures symbol '${s}' is not valid: ${min}-${max} characters, uppercase letters, "
              "digits, '.' and '-', with no leading, trailing or repeated separator",
              ("s", symbol)("min", GRAPHENE_FUTURES_MIN_SYMBOL_LENGTH)
              ("max", GRAPHENE_FUTURES_MAX_SYMBOL_LENGTH) );
   FC_ASSERT( description.size() <= GRAPHENE_FUTURES_MAX_DESCRIPTION_LENGTH,
              "Description should not exceed ${m} characters",
              ("m", GRAPHENE_FUTURES_MAX_DESCRIPTION_LENGTH) );

   FC_ASSERT( contract_size > 0, "Contract size must be positive" );
   FC_ASSERT( contract_size <= GRAPHENE_MAX_SHARE_SUPPLY,
              "Contract size may not exceed ${m}", ("m", GRAPHENE_MAX_SHARE_SUPPLY) );

   options.validate();
}

share_type futures_market_create_operation::calculate_fee( const fee_params_t& schedule )const
{
   share_type core_fee_required = schedule.fee;
   core_fee_required += calculate_data_fee( fc::raw::pack_size(*this), schedule.price_per_kbyte );
   return core_fee_required;
}

void futures_market_update_operation::validate()const
{
   FC_ASSERT( fee.amount >= 0, "Fee should not be negative" );
   FC_ASSERT( new_description.valid() || new_options.valid(), "Should change something" );
   if( new_description.valid() )
      FC_ASSERT( new_description->size() <= GRAPHENE_FUTURES_MAX_DESCRIPTION_LENGTH,
                 "Description should not exceed ${m} characters",
                 ("m", GRAPHENE_FUTURES_MAX_DESCRIPTION_LENGTH) );
   if( new_options.valid() )
      new_options->validate();
}

share_type futures_market_update_operation::calculate_fee( const fee_params_t& schedule )const
{
   share_type core_fee_required = schedule.fee;
   core_fee_required += calculate_data_fee( fc::raw::pack_size(*this), schedule.price_per_kbyte );
   return core_fee_required;
}

void futures_order_create_operation::validate()const
{
   FC_ASSERT( fee.amount >= 0, "Fee should not be negative" );
   FC_ASSERT( size > 0, "Order size must be positive" );
   FC_ASSERT( price_per_contract > 0, "Price per contract must be positive" );
   // Notional is size x price, and it must stay inside the share range so that margin,
   // entry_value and PnL cannot overflow anywhere downstream.
   FC_ASSERT( size <= GRAPHENE_MAX_SHARE_SUPPLY, "Order size is out of range" );
   FC_ASSERT( price_per_contract <= GRAPHENE_MAX_SHARE_SUPPLY, "Price is out of range" );
   FC_ASSERT( fc::uint128_t( size.value ) * price_per_contract.value
              <= fc::uint128_t( GRAPHENE_MAX_SHARE_SUPPLY ),
              "Order notional (size x price) exceeds ${m}", ("m", GRAPHENE_MAX_SHARE_SUPPLY) );
}

void futures_order_cancel_operation::validate()const
{
   FC_ASSERT( fee.amount >= 0, "Fee should not be negative" );
}

void futures_position_adjust_margin_operation::validate()const
{
   FC_ASSERT( fee.amount >= 0, "Fee should not be negative" );
   FC_ASSERT( delta != 0, "Margin adjustment must be non-zero" );
   FC_ASSERT( delta >= -GRAPHENE_MAX_SHARE_SUPPLY && delta <= GRAPHENE_MAX_SHARE_SUPPLY,
              "Margin adjustment is out of range" );
}

void futures_liquidate_operation::validate()const
{
   FC_ASSERT( fee.amount >= 0, "Fee should not be negative" );
}

void futures_settle_operation::validate()const
{
   FC_ASSERT( fee.amount >= 0, "Fee should not be negative" );
}

} } // graphene::protocol

GRAPHENE_IMPLEMENT_EXTERNAL_SERIALIZATION( graphene::protocol::futures_market_options )
GRAPHENE_IMPLEMENT_EXTERNAL_SERIALIZATION(
      graphene::protocol::futures_market_create_operation::fee_params_t )
GRAPHENE_IMPLEMENT_EXTERNAL_SERIALIZATION(
      graphene::protocol::futures_market_update_operation::fee_params_t )
GRAPHENE_IMPLEMENT_EXTERNAL_SERIALIZATION( graphene::protocol::futures_market_create_operation )
GRAPHENE_IMPLEMENT_EXTERNAL_SERIALIZATION( graphene::protocol::futures_market_update_operation )
GRAPHENE_IMPLEMENT_EXTERNAL_SERIALIZATION(
      graphene::protocol::futures_order_create_operation::fee_params_t )
GRAPHENE_IMPLEMENT_EXTERNAL_SERIALIZATION(
      graphene::protocol::futures_order_cancel_operation::fee_params_t )
GRAPHENE_IMPLEMENT_EXTERNAL_SERIALIZATION( graphene::protocol::futures_fill_operation::fee_params_t )
GRAPHENE_IMPLEMENT_EXTERNAL_SERIALIZATION( graphene::protocol::futures_order_create_operation )
GRAPHENE_IMPLEMENT_EXTERNAL_SERIALIZATION( graphene::protocol::futures_order_cancel_operation )
GRAPHENE_IMPLEMENT_EXTERNAL_SERIALIZATION( graphene::protocol::futures_fill_operation )
GRAPHENE_IMPLEMENT_EXTERNAL_SERIALIZATION(
      graphene::protocol::futures_position_adjust_margin_operation::fee_params_t )
GRAPHENE_IMPLEMENT_EXTERNAL_SERIALIZATION(
      graphene::protocol::futures_liquidate_operation::fee_params_t )
GRAPHENE_IMPLEMENT_EXTERNAL_SERIALIZATION(
      graphene::protocol::futures_position_adjust_margin_operation )
GRAPHENE_IMPLEMENT_EXTERNAL_SERIALIZATION( graphene::protocol::futures_liquidate_operation )
GRAPHENE_IMPLEMENT_EXTERNAL_SERIALIZATION(
      graphene::protocol::futures_settle_operation::fee_params_t )
GRAPHENE_IMPLEMENT_EXTERNAL_SERIALIZATION( graphene::protocol::futures_settle_operation )
