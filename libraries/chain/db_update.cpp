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

#include <graphene/chain/asset_object.hpp>
#include <graphene/chain/global_property_object.hpp>
#include <graphene/chain/limit_order_object.hpp>
#include <graphene/chain/proposal_object.hpp>
#include <graphene/chain/call_order_object.hpp>
#include <graphene/chain/transaction_object.hpp>
#include <graphene/chain/withdraw_permission_object.hpp>
#include <graphene/chain/witness_object.hpp>

#include <fc/uint128.hpp>

namespace graphene { namespace chain {

void database::update_global_dynamic_data( const signed_block& b )
{
   const dynamic_global_property_object& _dgp =
      dynamic_global_property_id_type(0)(*this);

   //
   // dynamic global properties updating
   //
   modify( _dgp, [&]( dynamic_global_property_object& dgp ){
      secret_hash_type::encoder enc;
      fc::raw::pack( enc, dgp.random );
      fc::raw::pack( enc, b.previous_secret );
      dgp.random = enc.result();
      dgp.head_block_number = b.block_num();
      dgp.head_block_id = b.id();
      dgp.time = b.timestamp;
      dgp.current_witness = b.witness;
   });
}

void database::update_signing_witness(const witness_object& signing_witness, const signed_block& new_block)
{
   const global_property_object& gpo = get_global_properties();
   const dynamic_global_property_object& dpo = get_dynamic_global_properties();

   share_type witness_pay = std::min( gpo.parameters.witness_pay_per_block, dpo.witness_budget );

   modify( dpo, [&]( dynamic_global_property_object& _dpo )
   {
      _dpo.witness_budget -= witness_pay;
   } );

   modify( signing_witness, [&]( witness_object& _wit )
   {
      _wit.last_secret = new_block.previous_secret;
      _wit.next_secret = new_block.next_secret_hash;
      _wit.accumulated_income += witness_pay;
   } );
}

void database::update_pending_block(const signed_block& next_block, uint8_t current_block_interval)
{
   _pending_block.timestamp = next_block.timestamp + current_block_interval;
   _pending_block.previous = next_block.id();
   auto old_pending_trx = std::move(_pending_block.transactions);
   _pending_block.transactions.clear();
   for( auto old_trx : old_pending_trx )
      push_transaction( old_trx );
}

void database::clear_expired_transactions()
{
   //Look for expired transactions in the deduplication list, and remove them.
   //Transactions must have expired by at least two forking windows in order to be removed.
   auto& transaction_idx = static_cast<transaction_index&>(get_mutable_index(implementation_ids, impl_transaction_object_type));
   const auto& dedupe_index = transaction_idx.indices().get<by_expiration>();
   const auto& global_parameters = get_global_properties().parameters;
   auto forking_window_time = global_parameters.maximum_undo_history * global_parameters.block_interval;
   while( !dedupe_index.empty()
          && head_block_time() - dedupe_index.rbegin()->expiration >= fc::seconds(forking_window_time) )
      transaction_idx.remove(*dedupe_index.rbegin());
}

void database::clear_expired_proposals()
{
   const auto& proposal_expiration_index = get_index_type<proposal_index>().indices().get<by_expiration>();
   while( !proposal_expiration_index.empty() && proposal_expiration_index.begin()->expiration_time <= head_block_time() )
   {
      const proposal_object& proposal = *proposal_expiration_index.begin();
      processed_transaction result;
      try {
         if( proposal.is_authorized_to_execute(this) )
         {
            result = push_proposal(proposal);
            //TODO: Do something with result so plugins can process it.
            continue;
         }
      } catch( const fc::exception& e ) {
         elog("Failed to apply proposed transaction on its expiration. Deleting it.\n${proposal}\n${error}",
              ("proposal", proposal)("error", e.to_detail_string()));
      }
      remove(proposal);
   }
}

void database::clear_expired_orders()
{
   with_skip_flags(
      get_node_properties().skip_flags | skip_authority_check, [&](){
         transaction_evaluation_state cancel_context(this);

         //Cancel expired limit orders
         auto& limit_index = get_index_type<limit_order_index>().indices().get<by_expiration>();
         while( !limit_index.empty() && limit_index.begin()->expiration <= head_block_time() )
         {
            limit_order_cancel_operation canceler;
            const limit_order_object& order = *limit_index.begin();
            canceler.fee_paying_account = order.seller;
            canceler.order = order.id;
            apply_operation(cancel_context, canceler);
         }
     });


   //Process expired force settlement orders
   auto& settlement_index = get_index_type<force_settlement_index>().indices().get<by_expiration>();
   if( !settlement_index.empty() )
   {
      asset_id_type current_asset = settlement_index.begin()->settlement_asset_id();
      asset max_settlement_volume;

      auto next_asset = [&current_asset, &settlement_index] {
         auto bound = settlement_index.upper_bound(current_asset);
         if( bound == settlement_index.end() )
            return false;
         current_asset = bound->settlement_asset_id();
         return true;
      };

      // At each iteration, we either consume the current order and remove it, or we move to the next asset
      for( auto itr = settlement_index.lower_bound(current_asset);
           itr != settlement_index.end();
           itr = settlement_index.lower_bound(current_asset) )
      {
         const force_settlement_object& order = *itr;
         auto order_id = order.id;
         current_asset = order.settlement_asset_id();
         const asset_object& mia_object = get(current_asset);
         const asset_bitasset_data_object mia = mia_object.bitasset_data(*this);

         // Has this order not reached its settlement date?
         if( order.settlement_date > head_block_time() )
         {
            if( next_asset() )
               continue;
            break;
         }
         // Can we still settle in this asset?
         if( mia.current_feed.settlement_price.is_null() )
         {
            ilog("Canceling a force settlement in ${asset} because settlement price is null",
                 ("asset", mia_object.symbol));
            cancel_order(order);
            continue;
         }
         if( max_settlement_volume.asset_id != current_asset )
            max_settlement_volume = mia_object.amount(mia.max_force_settlement_volume(mia_object.dynamic_data(*this).current_supply));
         if( mia.force_settled_volume >= max_settlement_volume.amount )
         {
            /*
            ilog("Skipping force settlement in ${asset}; settled ${settled_volume} / ${max_volume}",
                 ("asset", mia_object.symbol)("settlement_price_null",mia.current_feed.settlement_price.is_null())
                 ("settled_volume", mia.force_settled_volume)("max_volume", max_settlement_volume));
                 */
            if( next_asset() )
               continue;
            break;
         }

         auto& pays = order.balance;
         auto receives = (order.balance * mia.current_feed.settlement_price);
         receives.amount = (fc::uint128_t(receives.amount.value) *
                            (GRAPHENE_100_PERCENT - mia.options.force_settlement_offset_percent) / GRAPHENE_100_PERCENT).to_uint64();
         assert(receives <= order.balance * mia.current_feed.settlement_price);

         price settlement_price = pays / receives;

         auto& call_index = get_index_type<call_order_index>().indices().get<by_collateral>();
         asset settled = mia_object.amount(mia.force_settled_volume);
         // Match against the least collateralized short until the settlement is finished or we reach max settlements
         while( settled < max_settlement_volume && find_object(order_id) )
         {
            auto itr = call_index.lower_bound(boost::make_tuple(price::min(mia_object.bitasset_data(*this).options.short_backing_asset,
                                                                           mia_object.get_id())));
            // There should always be a call order, since asset exists!
            assert(itr != call_index.end() && itr->debt_type() == mia_object.get_id());
            asset max_settlement = max_settlement_volume - settled;
            settled += match(*itr, order, settlement_price, max_settlement);
         }
         modify(mia, [settled](asset_bitasset_data_object& b) {
            b.force_settled_volume = settled.amount;
         });
      }
   }
}

void database::update_expired_feeds()
{
   auto& asset_idx = get_index_type<asset_bitasset_data_index>();
   for( const asset_bitasset_data_object* b : asset_idx )
      if( b->feed_is_expired(head_block_time()) )
      {
         modify(*b, [this](asset_bitasset_data_object& a) {
            a.update_median_feeds(head_block_time());
         });

         check_call_orders( b->current_feed.settlement_price.base.asset_id(*this) );
      }
}

void database::update_withdraw_permissions()
{
   auto& permit_index = get_index_type<withdraw_permission_index>().indices().get<by_expiration>();
   while( !permit_index.empty() && permit_index.begin()->expiration <= head_block_time() )
      remove(*permit_index.begin());
}

} }
