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
#include <graphene/chain/account_object.hpp>
#include <graphene/chain/asset_object.hpp>
#include <graphene/chain/database.hpp>
#include <fc/uint128.hpp>

namespace graphene { namespace chain {

share_type cut_fee(share_type a, uint16_t p)
{
   if( a == 0 || p == 0 )
      return 0;
   if( p == GRAPHENE_100_PERCENT )
      return a;

   fc::uint128 r(a.value);
   r *= p;
   r /= GRAPHENE_100_PERCENT;
   return r.to_uint64();
}

bool account_object::is_authorized_asset(const asset_object& asset_obj) const {
   for( const auto id : blacklisting_accounts )
      if( asset_obj.options.blacklist_authorities.find(id) != asset_obj.options.blacklist_authorities.end() ) return false;

   for( const auto id : whitelisting_accounts )
      if( asset_obj.options.whitelist_authorities.find(id) != asset_obj.options.whitelist_authorities.end() ) return true;
   return false;
}

void account_balance_object::adjust_balance(const asset& delta)
{
   assert(delta.asset_id == asset_type);
   balance += delta.amount;
}

uint16_t account_statistics_object::calculate_bulk_discount_percent(const chain_parameters& params) const
{
   uint64_t bulk_discount_percent = 0;
   if( lifetime_fees_paid >= params.bulk_discount_threshold_max )
      bulk_discount_percent = params.max_bulk_discount_percent_of_fee;
   else if(params.bulk_discount_threshold_max.value !=
           params.bulk_discount_threshold_min.value)
   {
      bulk_discount_percent =
            (params.max_bulk_discount_percent_of_fee *
             (lifetime_fees_paid.value -
              params.bulk_discount_threshold_min.value)) /
            (params.bulk_discount_threshold_max.value -
             params.bulk_discount_threshold_min.value);
   }
   assert( bulk_discount_percent <= GRAPHENE_100_PERCENT );

   return bulk_discount_percent;
}

void account_statistics_object::process_fees(const account_object& a, database& d) const
{
   if( pending_fees > 0 || pending_vested_fees > 0 )
   {
      const auto& props = d.get_global_properties();

      auto pay_out_fees = [&](const account_object& account, share_type core_fee_total, bool require_vesting)
      {
         share_type network_cut = cut_fee(core_fee_total, account.network_fee_percentage);
         assert( network_cut <= core_fee_total );
         share_type burned = cut_fee(network_cut, props.parameters.burn_percent_of_fee);
         share_type accumulated = network_cut - burned;
         assert( accumulated + burned == network_cut );
         share_type lifetime_cut = cut_fee(core_fee_total, account.lifetime_referrer_fee_percentage);
         share_type referral = core_fee_total - network_cut - lifetime_cut;

         d.modify(dynamic_asset_data_id_type()(d), [network_cut](asset_dynamic_data_object& d) {
            d.accumulated_fees += network_cut;
         });

         // Potential optimization: Skip some of this math and object lookups by special casing on the account type.
         // For example, if the account is a lifetime member, we can skip all this and just deposit the referral to
         // it directly.
         share_type referrer_cut = cut_fee(referral, account.referrer_rewards_percentage);
         share_type registrar_cut = referral - referrer_cut;

         d.deposit_cashback(d.get(account.lifetime_referrer), lifetime_cut, require_vesting);
         d.deposit_cashback(d.get(account.referrer), referrer_cut, require_vesting);
         d.deposit_cashback(d.get(account.registrar), registrar_cut, require_vesting);

         assert( referrer_cut + registrar_cut + accumulated + burned + lifetime_cut == core_fee_total );
      };

      share_type vesting_fee_subtotal(pending_fees);
      share_type vested_fee_subtotal(pending_vested_fees);
      share_type vesting_cashback, vested_cashback;

      if( lifetime_fees_paid > props.parameters.bulk_discount_threshold_min &&
          a.is_member(d.head_block_time()) )
      {
         auto bulk_discount_rate = calculate_bulk_discount_percent(props.parameters);
         vesting_cashback = cut_fee(vesting_fee_subtotal, bulk_discount_rate);
         vesting_fee_subtotal -= vesting_cashback;

         vested_cashback = cut_fee(vested_fee_subtotal, bulk_discount_rate);
         vested_fee_subtotal -= vested_cashback;
      }

      pay_out_fees(a, vesting_fee_subtotal, true);
      d.deposit_cashback(a, vesting_cashback, true);
      pay_out_fees(a, vested_fee_subtotal, false);
      d.deposit_cashback(a, vested_cashback, false);

      d.modify(*this, [vested_fee_subtotal, vesting_fee_subtotal](account_statistics_object& s) {
         s.lifetime_fees_paid += vested_fee_subtotal + vesting_fee_subtotal;
         s.pending_fees = 0;
         s.pending_vested_fees = 0;
      });
   }
}

void account_object::options_type::validate() const
{
   auto needed_witnesses = num_witness;
   auto needed_committee = num_committee;

   for( vote_id_type id : votes )
      if( id.type() == vote_id_type::witness && needed_witnesses )
         --needed_witnesses;
      else if ( id.type() == vote_id_type::committee && needed_committee )
         --needed_committee;

   FC_ASSERT( needed_witnesses == 0 && needed_committee == 0,
              "May not specify fewer witnesses or committee members than the number voted for.");
}

} } // graphene::chain
