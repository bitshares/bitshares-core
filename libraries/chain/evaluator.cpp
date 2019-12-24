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
#include <graphene/chain/database.hpp>
#include <graphene/chain/evaluator.hpp>
#include <graphene/chain/exceptions.hpp>
#include <graphene/chain/hardfork.hpp>
#include <graphene/chain/is_authorized_asset.hpp>
#include <graphene/chain/transaction_evaluation_state.hpp>

#include <graphene/chain/asset_object.hpp>
#include <graphene/chain/account_object.hpp>
#include <graphene/chain/fba_object.hpp>
#include <graphene/chain/committee_member_object.hpp>
#include <graphene/chain/market_evaluator.hpp>
#include <graphene/protocol/fee_schedule.hpp>

namespace graphene { namespace chain {
database& generic_evaluator::db()const { return trx_state->db(); }

   operation_result generic_evaluator::start_evaluate( transaction_evaluation_state& eval_state, const operation& op, bool apply )
   { try {
      trx_state   = &eval_state;
      //check_required_authorities(op);
      auto result = evaluate( op );

      if( apply ) result = this->apply( op );
      return result;
   } FC_CAPTURE_AND_RETHROW() }

   void generic_evaluator::prepare_fee(account_id_type account_id, asset fee)
   {
      const database& d = db();
      FC_ASSERT( fee.amount >= 0 );
      fee_paying_account = &account_id(d);
      fee_paying_account_statistics = &fee_paying_account->statistics(d);

      fee_asset = &fee.asset_id(d);
      if( fee_asset->get_id() != asset_id_type() )
         fee_asset_dyn_data = &fee_asset->dynamic_asset_data_id(d);

      FC_ASSERT( is_authorized_asset( d, *fee_paying_account, *fee_asset ), 
            "Account ${acct} '${name}' attempted to pay fee by using asset ${a} '${sym}', "
            "which is unauthorized due to whitelist / blacklist",
            ( "acct", fee_paying_account->id)("name", fee_paying_account->name)("a", fee_asset->id)
            ("sym", fee_asset->symbol) );

      if( db().get_balance( account_id, fee.asset_id ).amount < fee.amount )
      {
         borrowed_fee = fee;
         if( !fee_asset_dyn_data )
            fee_asset_dyn_data = &fee_asset->dynamic_asset_data_id(d);
         db().modify( *fee_asset_dyn_data, [this] ( asset_dynamic_data_object& add ) {
            fee_from_account = add.borrowed_fees.issue( borrowed_fee.amount );
         });
      }
      else
      {
         fee_from_account = db().reduce_balance( account_id, fee );
         borrowed_fee = asset(0);
      }
      if( fee.asset_id == asset_id_type() )
         fee_from_pool = 0;
      else
      {
         asset pool_fee = fee_from_account.get_value() * fee_asset->options.core_exchange_rate;
         FC_ASSERT( pool_fee.asset_id == asset_id_type() );
         fee_from_pool = pool_fee.amount;
         FC_ASSERT( fee_from_pool <= fee_asset_dyn_data->fee_pool.get_amount(),
                    "Fee pool balance of '${b}' is less than the ${r} required to convert ${c}",
                    ("r",db().to_pretty_string(pool_fee))
                    ("b",db().to_pretty_string(fee_asset_dyn_data->fee_pool.get_value()))
                    ("c",db().to_pretty_string(fee)) );
      }
   }

   void generic_evaluator::convert_fee()
   {
      if( fee_asset->get_id() == asset_id_type() )
         core_fee_paid = std::move(fee_from_account);
      else if( !trx_state->skip_fee )
         db().modify(*fee_asset_dyn_data, [this](asset_dynamic_data_object& d) {
            d.accumulated_fees += std::move(fee_from_account);
            core_fee_paid = d.fee_pool.split( fee_from_pool );
         });
   }

   void generic_evaluator::pay_fee()
   { try {
      if( !trx_state->skip_fee )
         db().modify(*fee_paying_account_statistics, [this](account_statistics_object& s)
         {
            s.pay_fee( std::move(core_fee_paid), db().get_global_properties().parameters.cashback_vesting_threshold );
         });
   } FC_CAPTURE_AND_RETHROW() }

   void generic_evaluator::pay_fba_fee( uint64_t fba_id )
   {
      database& d = db();
      const fba_accumulator_object& fba = d.get< fba_accumulator_object >( fba_accumulator_id_type( fba_id ) );
      if( !fba.is_configured(d) )
      {
         generic_evaluator::pay_fee();
         return;
      }
      d.modify( fba, [this]( fba_accumulator_object& _fba )
      {
         _fba.accumulated_fba_fees += std::move(core_fee_paid);
      } );
   }

   void generic_evaluator::pay_back_borrowed_fee()
   {
      if( borrowed_fee.amount > 0 )
      {
         stored_value transport = db().reduce_balance( fee_paying_account->id, borrowed_fee );
         db().modify( *fee_asset_dyn_data, [&transport] ( asset_dynamic_data_object& add ) {
            add.borrowed_fees.burn( std::move(transport) );
         });
      }
   }

   share_type generic_evaluator::calculate_fee_for_operation(const operation& op) const
   {
     return db().current_fee_schedule().calculate_fee( op ).amount;
   }
} }
