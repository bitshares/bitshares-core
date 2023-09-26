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
#include <graphene/protocol/market.hpp>

#include <fc/io/raw.hpp>

namespace graphene { namespace protocol {

void create_take_profit_order_action::validate() const
{
   FC_ASSERT( spread_percent > 0, "The spread percentage must be positive" );
   FC_ASSERT( size_percent > 0, "The size percentage must be positive" );
   FC_ASSERT( size_percent <= GRAPHENE_100_PERCENT, "The size percentage must not exceed 100%" );
   FC_ASSERT( expiration_seconds > 0, "The expiration seconds must be positive" );
}

struct lo_action_validate_visitor
{
   using result_type = void;

   template<typename ActionType>
   result_type operator()( const ActionType& action )const
   {
      action.validate();
   }
};

void limit_order_create_operation::validate()const
{
   FC_ASSERT( amount_to_sell.asset_id != min_to_receive.asset_id );
   FC_ASSERT( fee.amount >= 0 );
   FC_ASSERT( amount_to_sell.amount > 0 );
   FC_ASSERT( min_to_receive.amount > 0 );

   if( extensions.value.on_fill.valid() )
   {
      // Note: an empty on_fill action list is allowed
      for( const auto& action : *extensions.value.on_fill )
         action.visit( lo_action_validate_visitor() );
   }

}

void limit_order_update_operation::validate() const
{ try {
   FC_ASSERT(fee.amount >= 0, "Fee must not be negative");
   FC_ASSERT(new_price || delta_amount_to_sell || new_expiration || on_fill,
             "Cannot update limit order if nothing is specified to update");
   if (new_price)
      new_price->validate();
   if (delta_amount_to_sell)
      FC_ASSERT(delta_amount_to_sell->amount != 0, "Cannot change limit order amount by zero");

   if( on_fill.valid() )
   {
      // Note: an empty on_fill action list is allowed
      for( const auto& action : *on_fill )
         action.visit( lo_action_validate_visitor() );
   }

} FC_CAPTURE_AND_RETHROW((*this)) } // GCOVR_EXCL_LINE

void limit_order_cancel_operation::validate()const
{
   FC_ASSERT( fee.amount >= 0 );
}

void call_order_update_operation::validate()const
{ try {
   FC_ASSERT( fee.amount >= 0 );
   FC_ASSERT( delta_collateral.asset_id != delta_debt.asset_id );
   FC_ASSERT( delta_collateral.amount != 0 || delta_debt.amount != 0 );

   // note: no validation is needed for extensions so far: the only attribute inside is target_collateral_ratio

} FC_CAPTURE_AND_RETHROW((*this)) } // GCOVR_EXCL_LINE

void bid_collateral_operation::validate()const
{ try {
   FC_ASSERT( fee.amount >= 0 );
   FC_ASSERT( debt_covered.amount == 0 || (debt_covered.amount > 0 && additional_collateral.amount > 0) );
} FC_CAPTURE_AND_RETHROW((*this)) } // GCOVR_EXCL_LINE

} } // graphene::protocol

GRAPHENE_IMPLEMENT_EXTERNAL_SERIALIZATION( graphene::protocol::create_take_profit_order_action )

GRAPHENE_IMPLEMENT_EXTERNAL_SERIALIZATION( graphene::protocol::limit_order_create_operation::options_type )
GRAPHENE_IMPLEMENT_EXTERNAL_SERIALIZATION( graphene::protocol::call_order_update_operation::options_type )

GRAPHENE_IMPLEMENT_EXTERNAL_SERIALIZATION( graphene::protocol::limit_order_create_operation::fee_params_t )
GRAPHENE_IMPLEMENT_EXTERNAL_SERIALIZATION( graphene::protocol::limit_order_update_operation::fee_params_t )
GRAPHENE_IMPLEMENT_EXTERNAL_SERIALIZATION( graphene::protocol::limit_order_cancel_operation::fee_params_t )
GRAPHENE_IMPLEMENT_EXTERNAL_SERIALIZATION( graphene::protocol::call_order_update_operation::fee_params_t )
GRAPHENE_IMPLEMENT_EXTERNAL_SERIALIZATION( graphene::protocol::bid_collateral_operation::fee_params_t )

GRAPHENE_IMPLEMENT_EXTERNAL_SERIALIZATION( graphene::protocol::limit_order_create_operation )
GRAPHENE_IMPLEMENT_EXTERNAL_SERIALIZATION( graphene::protocol::limit_order_update_operation )
GRAPHENE_IMPLEMENT_EXTERNAL_SERIALIZATION( graphene::protocol::limit_order_cancel_operation )
GRAPHENE_IMPLEMENT_EXTERNAL_SERIALIZATION( graphene::protocol::call_order_update_operation )
GRAPHENE_IMPLEMENT_EXTERNAL_SERIALIZATION( graphene::protocol::bid_collateral_operation )
GRAPHENE_IMPLEMENT_EXTERNAL_SERIALIZATION( graphene::protocol::fill_order_operation )
GRAPHENE_IMPLEMENT_EXTERNAL_SERIALIZATION( graphene::protocol::execute_bid_operation )
