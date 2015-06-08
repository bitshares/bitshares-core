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
#include <graphene/chain/database.hpp>
#include <graphene/chain/evaluator.hpp>
#include <graphene/chain/transaction_evaluation_state.hpp>
#include <graphene/chain/key_object.hpp>
#include <graphene/chain/asset_object.hpp>
#include <graphene/chain/account_object.hpp>
#include <graphene/chain/delegate_object.hpp>
#include <graphene/chain/limit_order_object.hpp>
#include <graphene/chain/short_order_object.hpp>

#include <fc/uint128.hpp>

namespace graphene { namespace chain {
   database& generic_evaluator::db()const { return trx_state->db(); }
   operation_result generic_evaluator::start_evaluate( transaction_evaluation_state& eval_state, const operation& op, bool apply )
   {
      trx_state   = &eval_state;
      check_required_authorities(op);
      auto result = evaluate( op );

      if( apply ) result = this->apply( op );
      return result;
   }

   void generic_evaluator::prepare_fee(account_id_type account_id, asset fee)
   {
      fee_from_account = fee;
      FC_ASSERT( fee.amount >= 0 );
      fee_paying_account = &account_id(db());
      fee_paying_account_statistics = &fee_paying_account->statistics(db());

      fee_asset = &fee.asset_id(db());
      fee_asset_dyn_data = &fee_asset->dynamic_asset_data_id(db());

      if( fee_from_account.asset_id == asset_id_type() )
         core_fee_paid = fee_from_account.amount;
      else {
         asset fee_from_pool = fee_from_account * fee_asset->options.core_exchange_rate;
         FC_ASSERT( fee_from_pool.asset_id == asset_id_type() );
         core_fee_paid = fee_from_pool.amount;
         FC_ASSERT( core_fee_paid <= fee_asset_dyn_data->fee_pool );
      }
   }

   void generic_evaluator::pay_fee()
   { try {
      asset core_fee_subtotal(core_fee_paid);
      const auto& gp = db().get_global_properties();
      share_type bulk_cashback  = share_type(0);
      if( fee_paying_account_statistics->lifetime_fees_paid > gp.parameters.bulk_discount_threshold_min &&
          fee_paying_account->is_prime() )
      {
         uint64_t bulk_discount_percent = 0;
         if( fee_paying_account_statistics->lifetime_fees_paid > gp.parameters.bulk_discount_threshold_max )
            bulk_discount_percent = gp.parameters.max_bulk_discount_percent_of_fee;
         else if(gp.parameters.bulk_discount_threshold_max.value - gp.parameters.bulk_discount_threshold_min.value != 0)
         {
            bulk_discount_percent =
                  (gp.parameters.max_bulk_discount_percent_of_fee *
                            (fee_paying_account_statistics->lifetime_fees_paid.value -
                             gp.parameters.bulk_discount_threshold_min.value)) /
                  (gp.parameters.bulk_discount_threshold_max.value - gp.parameters.bulk_discount_threshold_min.value);
         }
         assert( bulk_discount_percent <= GRAPHENE_100_PERCENT );
         assert( bulk_discount_percent >= 0 );

         bulk_cashback = (core_fee_subtotal.amount.value * bulk_discount_percent) / GRAPHENE_100_PERCENT;
         assert( bulk_cashback <= core_fee_subtotal.amount );
      }

      share_type core_fee_total = core_fee_subtotal.amount - bulk_cashback;
      share_type accumulated = (core_fee_total.value  * gp.parameters.witness_percent_of_fee)/GRAPHENE_100_PERCENT;
      share_type burned     = (core_fee_total.value  * gp.parameters.burn_percent_of_fee)/GRAPHENE_100_PERCENT;
      share_type referral   = core_fee_total.value - accumulated - burned;
      auto& d = db();

      assert( accumulated + burned <= core_fee_total );

      if( fee_asset->get_id() != asset_id_type() )
         d.modify(*fee_asset_dyn_data, [this](asset_dynamic_data_object& d) {
            d.accumulated_fees += fee_from_account.amount;
            d.fee_pool -= core_fee_paid;
         });
      d.modify(dynamic_asset_data_id_type()(d), [burned,accumulated](asset_dynamic_data_object& d) {
         d.accumulated_fees += accumulated + burned;
      });

      d.modify(fee_paying_account->statistics(d), [core_fee_total](account_statistics_object& s) {
         s.lifetime_fees_paid += core_fee_total;
      });

      d.deposit_cashback( fee_paying_account->referrer(d), referral );
      d.deposit_cashback( *fee_paying_account, bulk_cashback );

      assert( referral + bulk_cashback + accumulated + burned == core_fee_subtotal.amount );
   } FC_CAPTURE_AND_RETHROW() }

   bool generic_evaluator::verify_authority( const account_object& a, authority::classification c )
   {
       return trx_state->check_authority( a, c );
   }
   void generic_evaluator::check_required_authorities(const operation& op)
   {
      flat_set<account_id_type> active_auths;
      flat_set<account_id_type> owner_auths;
      op.visit(operation_get_required_auths(active_auths, owner_auths));

      for( auto id : active_auths )
      {
         FC_ASSERT(verify_authority(id(db()), authority::active) ||
                   verify_authority(id(db()), authority::owner), "", ("id", id));
      }
      for( auto id : owner_auths )
      {
         FC_ASSERT(verify_authority(id(db()), authority::owner), "", ("id", id));
      }
   }

   /*
   bool generic_evaluator::verify_signature( const key_object& k )
   {
      return trx_state->_skip_signature_check || trx_state->signed_by( k.id );
   }
   */

   object_id_type generic_evaluator::get_relative_id( object_id_type rel_id )const
   {
      if( rel_id.space() == relative_protocol_ids )
      {
         FC_ASSERT( rel_id.instance() < trx_state->operation_results.size() );
         // fetch the object just to make sure it exists.
         auto r = trx_state->operation_results[rel_id.instance()].get<object_id_type>();
         db().get_object( r ); // make sure it exists.
         return r;
      }
      return rel_id;
   }

   authority generic_evaluator::resolve_relative_ids( const authority& a )const
   {
      authority result;
      result.auths.reserve( a.auths.size() );
      result.weight_threshold = a.weight_threshold;

      for( const auto& item : a.auths )
      {
          auto id = get_relative_id( item.first );
          FC_ASSERT( id.type() == key_object_type || id.type() == account_object_type );
          result.auths[id] = item.second;
      }

      return result;
   }

} }
