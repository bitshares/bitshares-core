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


void account_statistics_object::process_fees(const account_object& a, database& d) const
{
   if( pending_fees > 0 || pending_vested_fees > 0 )
   {
      auto pay_out_fees = [&](const account_object& account, share_type core_fee_total, bool require_vesting)
      {
         // Check the referrer -- if he's no longer a member, pay to the lifetime referrer instead.
         // No need to check the registrar; registrars are required to be lifetime members.
         if( account.referrer(d).is_basic_account(d.head_block_time()) )
            d.modify(account, [](account_object& a) {
               a.referrer = a.lifetime_referrer;
            });

         share_type network_cut = cut_fee(core_fee_total, account.network_fee_percentage);
         assert( network_cut <= core_fee_total );

#ifndef NDEBUG
         const auto& props = d.get_global_properties();

         share_type reserveed = cut_fee(network_cut, props.parameters.reserve_percent_of_fee);
         share_type accumulated = network_cut - reserveed;
         assert( accumulated + reserveed == network_cut );
#endif
         share_type lifetime_cut = cut_fee(core_fee_total, account.lifetime_referrer_fee_percentage);
         share_type referral = core_fee_total - network_cut - lifetime_cut;

         d.modify(asset_dynamic_data_id_type()(d), [network_cut](asset_dynamic_data_object& d) {
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

         assert( referrer_cut + registrar_cut + accumulated + reserveed + lifetime_cut == core_fee_total );
      };

      share_type vesting_fee_subtotal(pending_fees);
      share_type vested_fee_subtotal(pending_vested_fees);
      share_type vesting_cashback, vested_cashback;

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

set<account_id_type> account_member_index::get_account_members(const account_object& a)const
{
   set<account_id_type> result;
   for( auto auth : a.owner.account_auths )
      result.insert(auth.first);
   return result;
}
set<public_key_type> account_member_index::get_key_members(const account_object& a)const
{
   set<public_key_type> result;
   for( auto auth : a.owner.key_auths )
      result.insert(auth.first);
   return result;
}
set<address> account_member_index::get_address_members(const account_object& a)const
{
   set<address> result;
   for( auto auth : a.owner.address_auths )
      result.insert(auth.first);
   return result;
}

void account_member_index::object_inserted(const object& obj)
{
    assert( dynamic_cast<const account_object*>(&obj) ); // for debug only
    const account_object& a = static_cast<const account_object&>(obj);

    auto account_members = get_account_members(a);
    for( auto item : account_members )
       account_to_account_memberships[item].insert(obj.id);

    auto key_members = get_key_members(a);
    for( auto item : key_members )
       account_to_key_memberships[item].insert(obj.id);

    auto address_members = get_address_members(a);
    for( auto item : address_members )
       account_to_address_memberships[item].insert(obj.id);
}

void account_member_index::object_removed(const object& obj)
{
    assert( dynamic_cast<const account_object*>(&obj) ); // for debug only
    const account_object& a = static_cast<const account_object&>(obj);

    auto key_members = get_key_members(a);
    for( auto item : key_members )
       account_to_key_memberships[item].erase( obj.id );

    auto address_members = get_address_members(a);
    for( auto item : address_members )
       account_to_address_memberships[item].erase( obj.id );

    auto account_members = get_account_members(a);
    for( auto item : account_members )
       account_to_account_memberships[item].erase( obj.id );
}

void account_member_index::about_to_modify(const object& before)
{
   before_key_members.clear();
   before_account_members.clear();
   assert( dynamic_cast<const account_object*>(&before) ); // for debug only
   const account_object& a = static_cast<const account_object&>(before);
   before_key_members     = get_key_members(a);
   before_address_members = get_address_members(a);
   before_account_members = get_account_members(a);
}

void account_member_index::object_modified(const object& after)
{
    assert( dynamic_cast<const account_object*>(&after) ); // for debug only
    const account_object& a = static_cast<const account_object&>(after);

    {
       set<account_id_type> after_account_members = get_account_members(a);
       vector<account_id_type> removed; removed.reserve(before_account_members.size());
       std::set_difference(before_account_members.begin(), before_account_members.end(),
                           after_account_members.begin(), after_account_members.end(),
                           std::inserter(removed, removed.end()));

       for( auto itr = removed.begin(); itr != removed.end(); ++itr )
          account_to_account_memberships[*itr].erase(after.id);

       vector<object_id_type> added; added.reserve(after_account_members.size());
       std::set_difference(after_account_members.begin(), after_account_members.end(),
                           before_account_members.begin(), before_account_members.end(),
                           std::inserter(added, added.end()));

       for( auto itr = added.begin(); itr != added.end(); ++itr )
          account_to_account_memberships[*itr].insert(after.id);
    }


    {
       set<public_key_type> after_key_members = get_key_members(a);

       vector<public_key_type> removed; removed.reserve(before_key_members.size());
       std::set_difference(before_key_members.begin(), before_key_members.end(),
                           after_key_members.begin(), after_key_members.end(),
                           std::inserter(removed, removed.end()));

       for( auto itr = removed.begin(); itr != removed.end(); ++itr )
          account_to_key_memberships[*itr].erase(after.id);

       vector<public_key_type> added; added.reserve(after_key_members.size());
       std::set_difference(after_key_members.begin(), after_key_members.end(),
                           before_key_members.begin(), before_key_members.end(),
                           std::inserter(added, added.end()));

       for( auto itr = added.begin(); itr != added.end(); ++itr )
          account_to_key_memberships[*itr].insert(after.id);
    }

    {
       set<address> after_address_members = get_address_members(a);

       vector<address> removed; removed.reserve(before_address_members.size());
       std::set_difference(before_address_members.begin(), before_address_members.end(),
                           after_address_members.begin(), after_address_members.end(),
                           std::inserter(removed, removed.end()));

       for( auto itr = removed.begin(); itr != removed.end(); ++itr )
          account_to_address_memberships[*itr].erase(after.id);

       vector<address> added; added.reserve(after_address_members.size());
       std::set_difference(after_address_members.begin(), after_address_members.end(),
                           before_address_members.begin(), before_address_members.end(),
                           std::inserter(added, added.end()));

       for( auto itr = added.begin(); itr != added.end(); ++itr )
          account_to_address_memberships[*itr].insert(after.id);
    }

}

void account_referrer_index::object_inserted( const object& obj )
{
}
void account_referrer_index::object_removed( const object& obj )
{
}
void account_referrer_index::about_to_modify( const object& before )
{
}
void account_referrer_index::object_modified( const object& after  )
{
}

} } // graphene::chain
