/*
 * Copyright (c) 2021 Abit More, and contributors.
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
#include <graphene/chain/asset_object.hpp>
#include <graphene/chain/credit_offer_object.hpp>

#include <graphene/chain/credit_offer_evaluator.hpp>

#include <graphene/chain/database.hpp>
#include <graphene/chain/exceptions.hpp>
#include <graphene/chain/hardfork.hpp>
#include <graphene/chain/is_authorized_asset.hpp>

#include <graphene/protocol/credit_offer.hpp>

namespace graphene { namespace chain {

void_result credit_offer_create_evaluator::do_evaluate(const credit_offer_create_operation& op) const
{ try {
   const database& d = db();
   const auto block_time = d.head_block_time();

   FC_ASSERT( HARDFORK_CORE_2362_PASSED(block_time), "Not allowed until the core-2362 hardfork" );

   if( op.enabled )
   {
      FC_ASSERT( op.auto_disable_time > block_time, "Auto-disable time should be in the future" );
      FC_ASSERT( op.auto_disable_time - block_time <= fc::days(GRAPHENE_MAX_CREDIT_OFFER_DAYS),
                 "Auto-disable time should not be later than ${d} days in the future",
                 ("d", GRAPHENE_MAX_CREDIT_OFFER_DAYS) );
   }

   // Make sure all the collateral asset types exist
   for( const auto& collateral : op.acceptable_collateral )
   {
      collateral.first(d);
   }

   // Make sure all the accounts exist
   for( const auto& borrower : op.acceptable_borrowers )
   {
      borrower.first(d);
   }

   FC_ASSERT( is_authorized_asset( d, *fee_paying_account, op.asset_type(d) ),
              "The account is unauthorized by the asset" );

   return void_result();
} FC_CAPTURE_AND_RETHROW( (op) ) }

object_id_type credit_offer_create_evaluator::do_apply(const credit_offer_create_operation& op) const
{ try {
   database& d = db();

   d.adjust_balance( op.owner_account, -asset( op.balance, op.asset_type ) );

   const auto& new_credit_offer_object = d.create<credit_offer_object>([&op](credit_offer_object& obj){
      obj.owner_account = op.owner_account;
      obj.asset_type = op.asset_type;
      obj.total_balance = op.balance;
      obj.current_balance = op.balance;
      obj.fee_rate = op.fee_rate;
      obj.max_duration_seconds = op.max_duration_seconds;
      obj.min_deal_amount = op.min_deal_amount;
      obj.enabled = op.enabled;
      obj.auto_disable_time = op.auto_disable_time;
      obj.acceptable_collateral = op.acceptable_collateral;
      obj.acceptable_borrowers = op.acceptable_borrowers;
   });
   return new_credit_offer_object.id;
} FC_CAPTURE_AND_RETHROW( (op) ) }

void_result credit_offer_delete_evaluator::do_evaluate(const credit_offer_delete_operation& op)
{ try {
   const database& d = db();

   _offer = &op.offer_id(d);

   FC_ASSERT( _offer->owner_account == op.owner_account, "The account is not the owner of the credit offer" );

   FC_ASSERT( _offer->total_balance == _offer->current_balance,
              "Can only delete a credit offer when the unpaid amount is zero" );

   // Note: no asset authorization check here, allow funds to be moved to account balance

   return void_result();
} FC_CAPTURE_AND_RETHROW( (op) ) }

asset credit_offer_delete_evaluator::do_apply(const credit_offer_delete_operation& op) const
{ try {
   database& d = db();

   asset released( _offer->current_balance, _offer->asset_type );

   if( _offer->current_balance != 0 )
   {
      d.adjust_balance( op.owner_account, released );
   }

   d.remove( *_offer );

   return released;
} FC_CAPTURE_AND_RETHROW( (op) ) }

void_result credit_offer_update_evaluator::do_evaluate(const credit_offer_update_operation& op)
{ try {
   const database& d = db();
   const auto block_time = d.head_block_time();

   _offer = &op.offer_id(d);

   FC_ASSERT( _offer->owner_account == op.owner_account, "The account is not the owner of the credit offer" );

   if( op.delta_amount.valid() )
   {
      FC_ASSERT( _offer->asset_type == op.delta_amount->asset_id, "Asset type mismatch" );

      if( op.delta_amount->amount > 0 )
      {
         // Check asset authorization only when moving funds from account balance to somewhere else
         FC_ASSERT( is_authorized_asset( d, *fee_paying_account, _offer->asset_type(d) ),
                    "The account is unauthorized by the asset" );
      }
      else
      {
         FC_ASSERT( _offer->total_balance > -op.delta_amount->amount,
                    "Should leave some funds in the credit offer when updating" );
         FC_ASSERT( _offer->current_balance >= -op.delta_amount->amount, "Insufficient balance in the credit offer" );
      }
   }

   bool enabled = op.enabled.valid() ? *op.enabled : _offer->enabled;
   if( enabled )
   {
      auto auto_disable_time = op.auto_disable_time.valid() ? *op.auto_disable_time : _offer->auto_disable_time;
      FC_ASSERT( auto_disable_time > block_time, "Auto-disable time should be in the future" );
      FC_ASSERT( auto_disable_time - block_time <= fc::days(GRAPHENE_MAX_CREDIT_OFFER_DAYS),
                 "Auto-disable time should not be later than ${d} days in the future",
                 ("d", GRAPHENE_MAX_CREDIT_OFFER_DAYS) );
   }

   // Make sure all the collateral asset types exist
   if( op.acceptable_collateral.valid() )
   {
      for( const auto& collateral : *op.acceptable_collateral )
      {
         collateral.first(d);
         FC_ASSERT( _offer->asset_type == collateral.second.base.asset_id,
                    "Asset type mismatch in a price of acceptable collateral" );
      }
   }

   // Make sure all the accounts exist
   if( op.acceptable_borrowers.valid() )
   {
      for( const auto& borrower : *op.acceptable_borrowers )
      {
         borrower.first(d);
      }
   }

   return void_result();
} FC_CAPTURE_AND_RETHROW( (op) ) }

void_result credit_offer_update_evaluator::do_apply( const credit_offer_update_operation& op) const
{ try {
   database& d = db();

   if( op.delta_amount.valid() )
      d.adjust_balance( op.owner_account, -(*op.delta_amount) );

   d.modify( *_offer, [&op]( credit_offer_object& coo ){
      if( op.delta_amount.valid() ) {
         coo.total_balance += op.delta_amount->amount;
         coo.current_balance += op.delta_amount->amount;
      }
      if( op.fee_rate.valid() )
         coo.fee_rate = *op.fee_rate;
      if( op.max_duration_seconds.valid() )
         coo.max_duration_seconds = *op.max_duration_seconds;
      if( op.min_deal_amount.valid() )
         coo.min_deal_amount = *op.min_deal_amount;
      if( op.enabled.valid() )
         coo.enabled = *op.enabled;
      if( op.auto_disable_time.valid() )
         coo.auto_disable_time = *op.auto_disable_time;
      if( op.acceptable_collateral.valid() )
         coo.acceptable_collateral = *op.acceptable_collateral;
      if( op.acceptable_borrowers.valid() )
         coo.acceptable_borrowers = *op.acceptable_borrowers;
   });

   // Defensive checks
   FC_ASSERT( _offer->total_balance > 0, "Total balance in the credit offer should be positive" );
   FC_ASSERT( _offer->current_balance >= 0, "Current balance in the credit offer should not be negative" );
   FC_ASSERT( _offer->total_balance >= _offer->current_balance,
              "Total balance in the credit offer should not be less than current balance" );
   if( _offer->enabled )
   {
      FC_ASSERT( _offer->auto_disable_time > d.head_block_time(),
                 "Auto-disable time should be in the future if the credit offer is enabled" );
      FC_ASSERT( _offer->auto_disable_time - d.head_block_time() <= fc::days(GRAPHENE_MAX_CREDIT_OFFER_DAYS),
                 "Auto-disable time should not be too late in the future" );
   }

   return void_result();
} FC_CAPTURE_AND_RETHROW( (op) ) }

void_result credit_offer_accept_evaluator::do_evaluate(const credit_offer_accept_operation& op)
{ try {
   const database& d = db();

   _offer = &op.offer_id(d);

   FC_ASSERT( _offer->enabled, "The credit offer is not enabled" );

   FC_ASSERT( _offer->asset_type == op.borrow_amount.asset_id, "Asset type mismatch" );

   FC_ASSERT( _offer->current_balance >= op.borrow_amount.amount,
              "Insufficient balance in the credit offer thus unable to borrow" );

   FC_ASSERT( _offer->min_deal_amount <= op.borrow_amount.amount,
              "Borrowing amount should not be less than minimum deal amount" );

   auto coll_itr = _offer->acceptable_collateral.find( op.collateral.asset_id );
   FC_ASSERT( coll_itr != _offer->acceptable_collateral.end(),
              "Collateral asset type is not acceptable by the credit offer" );

   const asset_object& debt_asset_obj = _offer->asset_type(d);
   const asset_object& collateral_asset_obj = op.collateral.asset_id(d);

   FC_ASSERT( is_authorized_asset( d, *fee_paying_account, debt_asset_obj ),
              "The borrower is unauthorized by the borrowing asset" );
   FC_ASSERT( is_authorized_asset( d, *fee_paying_account, collateral_asset_obj ),
              "The borrower is unauthorized by the collateral asset" );

   const account_object& offer_owner = _offer->owner_account(d);

   FC_ASSERT( is_authorized_asset( d, offer_owner, debt_asset_obj ),
              "The owner of the credit offer is unauthorized by the borrowing asset" );
   FC_ASSERT( is_authorized_asset( d, offer_owner, collateral_asset_obj ),
              "The owner of the credit offer is unauthorized by the collateral asset" );

   auto required_collateral = op.borrow_amount.multiply_and_round_up( coll_itr->second );
   FC_ASSERT( required_collateral.amount <= op.collateral.amount,
              "Insufficient collateral provided, requires ${r}, provided ${p}",
              ("r", required_collateral.amount) ("p", op.collateral.amount) );

   optional<share_type> max_allowed;
   if( !_offer->acceptable_borrowers.empty() )
   {
      auto itr = _offer->acceptable_borrowers.find( op.borrower );
      FC_ASSERT( itr != _offer->acceptable_borrowers.end(), "Account is not in acceptable borrowers" );
      max_allowed = itr->second;
   }

   share_type already_borrowed = 0;
   const auto& deal_summary_idx = d.get_index_type<credit_deal_summary_index>().indices().get<by_offer_borrower>();
   auto summ_itr = deal_summary_idx.find( boost::make_tuple( op.offer_id, op.borrower ) );
   if( summ_itr != deal_summary_idx.end() )
   {
      _deal_summary = &(*summ_itr);
      already_borrowed = _deal_summary->total_debt_amount;
   }

   if( max_allowed.valid() )
   {
      FC_ASSERT( already_borrowed + op.borrow_amount.amount <= *max_allowed,
                 "Unable to borrow ${b}, already borrowed ${a}, maximum allowed ${m}",
                 ("b", op.borrow_amount.amount) ("a", already_borrowed) ("m", max_allowed) );
   }

   return void_result();
} FC_CAPTURE_AND_RETHROW( (op) ) }

extendable_operation_result credit_offer_accept_evaluator::do_apply( const credit_offer_accept_operation& op) const
{ try {
   database& d = db();

   d.adjust_balance( op.borrower, -op.collateral );
   d.adjust_balance( op.borrower, op.borrow_amount );

   d.modify( *_offer, [&op]( credit_offer_object& coo ){
      coo.current_balance -= op.borrow_amount.amount;
   });

   const auto block_time = d.head_block_time();
   auto repay_time = ( fc::time_point_sec::maximum() - block_time ) >= fc::seconds(_offer->max_duration_seconds)
                     ? ( block_time + _offer->max_duration_seconds )
                     : fc::time_point_sec::maximum();

   const auto& new_deal = d.create<credit_deal_object>([&op,this,&repay_time](credit_deal_object& obj){
      obj.borrower = op.borrower;
      obj.offer_id = op.offer_id;
      obj.offer_owner = _offer->owner_account;
      obj.debt_asset = _offer->asset_type;
      obj.debt_amount = op.borrow_amount.amount;
      obj.collateral_asset = op.collateral.asset_id;
      obj.collateral_amount = op.collateral.amount;
      obj.fee_rate = _offer->fee_rate;
      obj.latest_repay_time = repay_time;
   });

   if( _deal_summary != nullptr )
   {
      d.modify( *_deal_summary, [&op]( credit_deal_summary_object& obj ){
         obj.total_debt_amount += op.borrow_amount.amount;
      });
   }
   else
   {
      d.create<credit_deal_summary_object>([&op,this](credit_deal_summary_object& obj){
         obj.borrower = op.borrower;
         obj.offer_id = op.offer_id;
         obj.offer_owner = _offer->owner_account;
         obj.debt_asset = _offer->asset_type;
         obj.total_debt_amount = op.borrow_amount.amount;
      });
   }

   // Defensive check
   FC_ASSERT( _offer->total_balance > 0, "Total balance in the credit offer should be positive" );
   FC_ASSERT( _offer->current_balance >= 0, "Current balance in the credit offer should not be negative" );
   FC_ASSERT( _offer->total_balance >= _offer->current_balance,
              "Total balance in the credit offer should not be less than current balance" );
   FC_ASSERT( new_deal.latest_repay_time > block_time,
              "Latest repayment time should be in the future" );
   FC_ASSERT( new_deal.latest_repay_time - block_time <= fc::days(GRAPHENE_MAX_CREDIT_DEAL_DAYS),
              "Latest repayment time should not be too late in the future" );

   extendable_operation_result result;
   // Note: only return the deal here, deal summary is impl so we do not return it
   result.value.new_objects = flat_set<object_id_type>({ new_deal.id });
   result.value.impacted_accounts = flat_set<account_id_type>({ _offer->owner_account });

   return result;
} FC_CAPTURE_AND_RETHROW( (op) ) }

void_result credit_deal_repay_evaluator::do_evaluate(const credit_deal_repay_operation& op)
{ try {
   const database& d = db();

   _deal = &op.deal_id(d);

   FC_ASSERT( _deal->borrower == op.account, "A credit deal can only be repaid by the borrower" );

   FC_ASSERT( _deal->debt_asset == op.repay_amount.asset_id, "Asset type mismatch" );

   FC_ASSERT( _deal->debt_amount >= op.repay_amount.amount,
              "Repay amount should not be greater than unpaid amount" );

   // Note: the result can be larger than 64 bit, but since we don't store it, it is allowed
   auto required_fee = ( ( ( fc::uint128_t( op.repay_amount.amount.value ) * _deal->fee_rate )
                         + GRAPHENE_FEE_RATE_DENOM ) - 1 ) / GRAPHENE_FEE_RATE_DENOM; // Round up

   FC_ASSERT( fc::uint128_t(op.credit_fee.amount.value) >= required_fee,
              "Insuffient credit fee, requires ${r}, offered ${p}",
              ("r", required_fee) ("p", op.credit_fee.amount) );

   const asset_object& debt_asset_obj = _deal->debt_asset(d);
   // Note: allow collateral to be moved to account balance regardless of collateral asset authorization

   FC_ASSERT( is_authorized_asset( d, *fee_paying_account, debt_asset_obj ),
              "The account is unauthorized by the repaying asset" );
   FC_ASSERT( is_authorized_asset( d, _deal->offer_owner(d), debt_asset_obj ),
              "The owner of the credit offer is unauthorized by the repaying asset" );

   return void_result();
} FC_CAPTURE_AND_RETHROW( (op) ) }

extendable_operation_result credit_deal_repay_evaluator::do_apply( const credit_deal_repay_operation& op) const
{ try {
   database& d = db();

   share_type total_amount = op.repay_amount.amount + op.credit_fee.amount;

   d.adjust_balance( op.account, asset( -total_amount, op.repay_amount.asset_id ) );

   // Update offer
   const credit_offer_object& offer = _deal->offer_id(d);
   d.modify( offer, [&op,&total_amount]( credit_offer_object& obj ){
      obj.total_balance += op.credit_fee.amount;
      obj.current_balance += total_amount;
   });
   // Defensive check
   FC_ASSERT( offer.total_balance >= offer.current_balance,
              "Total balance in the credit offer should not be less than current balance" );

   extendable_operation_result result;
   result.value.impacted_accounts = flat_set<account_id_type>({ offer.owner_account });
   result.value.updated_objects = flat_set<object_id_type>({ offer.id });

   // Process deal summary
   const auto& deal_summary_idx = d.get_index_type<credit_deal_summary_index>().indices().get<by_offer_borrower>();
   auto summ_itr = deal_summary_idx.find( boost::make_tuple( _deal->offer_id, op.account ) );
   FC_ASSERT( summ_itr != deal_summary_idx.end(), "Internal error" );

   const credit_deal_summary_object& summ_obj = *summ_itr;
   if( summ_obj.total_debt_amount == op.repay_amount.amount )
   {
      d.remove( summ_obj );
   }
   else
   {
      d.modify( summ_obj, [&op]( credit_deal_summary_object& obj ){
         obj.total_debt_amount -= op.repay_amount.amount;
      });
   }

   // Process deal
   asset collateral_released( _deal->collateral_amount, _deal->collateral_asset );
   if( _deal->debt_amount == op.repay_amount.amount ) // to fully repay
   {
      result.value.removed_objects = flat_set<object_id_type>({ _deal->id });
      d.remove( *_deal );
   }
   else // to partially repay
   {
      auto amount_to_release = ( fc::uint128_t( op.repay_amount.amount.value ) * _deal->collateral_amount.value )
                                 / _deal->debt_amount.value; // Round down
      FC_ASSERT( amount_to_release < fc::uint128_t( _deal->collateral_amount.value ), "Internal error" );
      collateral_released.amount = static_cast<int64_t>( amount_to_release );

      d.modify( *_deal, [&op,&collateral_released]( credit_deal_object& obj ){
         obj.debt_amount -= op.repay_amount.amount;
         obj.collateral_amount -= collateral_released.amount;
      });

      result.value.updated_objects->insert( _deal->id );
   }

   d.adjust_balance( op.account, collateral_released );
   result.value.received = vector<asset>({ collateral_released });

   return result;
} FC_CAPTURE_AND_RETHROW( (op) ) }

} } // graphene::chain
