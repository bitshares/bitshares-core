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
#include <graphene/chain/account_object.hpp>
#include <graphene/chain/database.hpp>

#include <fc/io/raw.hpp>
#include <fc/uint128.hpp>

namespace graphene { namespace chain {

static share_type cut_fee(share_type a, uint16_t p)
{
   if( a == 0 || p == 0 )
      return 0;
   if( p == GRAPHENE_100_PERCENT )
      return a;

   fc::uint128_t r = a.value;
   r *= p;
   r /= GRAPHENE_100_PERCENT;
   return static_cast<uint64_t>(r);
}

void account_balance_object::add_balance( stored_value&& delta )
{
   balance += std::move( delta );
   if( balance.get_asset() == asset_id_type() ) // CORE asset
      maintenance_flag = true;
}

stored_value account_balance_object::reduce_balance( share_type delta )
{
   if( balance.get_asset() == asset_id_type() ) // CORE asset
      maintenance_flag = true;
   return balance.split( delta );
}

class account_balance_backup : public account_balance_master
{
      asset balance;
      friend class account_balance_object;

   public:
      account_balance_backup( const account_balance_object& original )
         : account_balance_master( original )
      {
         balance = original.balance.get_value();
      }
};

unique_ptr<object> account_balance_object::backup()const
{
   return std::make_unique<account_balance_backup>( *this );
}

void account_balance_object::restore( object& obj )
{
   const auto& backup = static_cast<account_balance_backup&>(obj);
   balance.restore( backup.balance );
   static_cast<account_balance_master&>(*this) = std::move( backup );
}

void account_statistics_object::process_fees(const account_object& a, database& d) const
{
   if( pending_fees.get_amount() > 0 || pending_vested_fees.get_amount() > 0 )
   {
      auto pay_out_fees = [&d]( const account_object& account, stored_value&& core_fee, bool require_vesting )
      {
         // Check the referrer -- if he's no longer a member, pay to the lifetime referrer instead.
         // No need to check the registrar; registrars are required to be lifetime members.
         if( account.referrer(d).is_basic_account(d.head_block_time()) )
            d.modify( account, [](account_object& acc) {
               acc.referrer = acc.lifetime_referrer;
            });

         share_type network_cut = cut_fee(core_fee.get_amount(), account.network_fee_percentage);
         assert( network_cut <= core_fee.get_amount() );

#ifndef NDEBUG
         const auto& props = d.get_global_properties();

         share_type reserveed = cut_fee(network_cut, props.parameters.reserve_percent_of_fee);
         share_type accumulated = network_cut - reserveed;
         assert( accumulated + reserveed == network_cut );
#endif
         share_type lifetime_cut = cut_fee(core_fee.get_amount(), account.lifetime_referrer_fee_percentage);
         share_type referral = core_fee.get_amount() - network_cut - lifetime_cut;

         d.modify( d.get_core_dynamic_data(), [network_cut,&core_fee](asset_dynamic_data_object& addo) {
            addo.accumulated_fees += core_fee.split( network_cut );
         });

         // Potential optimization: Skip some of this math and object lookups by special casing on the account type.
         // For example, if the account is a lifetime member, we can skip all this and just deposit the referral to
         // it directly.
         share_type referrer_cut = cut_fee(referral, account.referrer_rewards_percentage);

         d.deposit_cashback(d.get(account.lifetime_referrer), core_fee.split( lifetime_cut ), require_vesting);
         d.deposit_cashback(d.get(account.referrer), core_fee.split( referrer_cut ), require_vesting);
         d.deposit_cashback(d.get(account.registrar), std::move( core_fee ), require_vesting);
      };

      stored_value transport;
      stored_value transport_vested;
      d.modify(*this, [&transport,&transport_vested](account_statistics_object& s) {
         s.lifetime_fees_paid += s.pending_fees.get_amount() + s.pending_vested_fees.get_amount();
         transport = std::move(s.pending_fees);
         transport_vested = std::move(s.pending_vested_fees);
      });
      pay_out_fees(a, std::move(transport), true);
      pay_out_fees(a, std::move(transport_vested), false);
   }
}

void account_statistics_object::pay_fee( stored_value&& core_fee, share_type cashback_vesting_threshold )
{
   if( core_fee.get_amount() > cashback_vesting_threshold )
      pending_fees += std::move( core_fee );
   else
      pending_vested_fees += std::move( core_fee );
}

class account_statistics_backup : public account_statistics_master
{
      share_type pending_fees;
      share_type pending_vested_fees;
      friend class account_statistics_object;

   public:
      account_statistics_backup( const account_statistics_object& original )
         : account_statistics_master( original )
      {
         pending_fees = original.pending_fees.get_amount();
         pending_vested_fees = original.pending_vested_fees.get_amount();
      }
};

unique_ptr<object> account_statistics_object::backup()const
{
   return std::make_unique<account_statistics_backup>( *this );
}

void account_statistics_object::restore( object& obj )
{
   const auto& backup = static_cast<account_statistics_backup&>(obj);
   pending_fees.restore( asset( backup.pending_fees ) );
   pending_vested_fees.restore( asset( backup.pending_vested_fees ) );
   static_cast<account_statistics_master&>(*this) = std::move( backup );
}

set<account_id_type> account_member_index::get_account_members(const account_object& a)const
{
   set<account_id_type> result;
   for( auto auth : a.owner.account_auths )
      result.insert(auth.first);
   for( auto auth : a.active.account_auths )
      result.insert(auth.first);
   return result;
}
set<public_key_type, pubkey_comparator> account_member_index::get_key_members(const account_object& a)const
{
   set<public_key_type, pubkey_comparator> result;
   for( auto auth : a.owner.key_auths )
      result.insert(auth.first);
   for( auto auth : a.active.key_auths )
      result.insert(auth.first);
   result.insert( a.options.memo_key );
   return result;
}
set<address> account_member_index::get_address_members(const account_object& a)const
{
   set<address> result;
   for( auto auth : a.owner.address_auths )
      result.insert(auth.first);
   for( auto auth : a.active.address_auths )
      result.insert(auth.first);
   result.insert( a.options.memo_key );
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
       set<public_key_type, pubkey_comparator> after_key_members = get_key_members(a);

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

const uint8_t  balances_by_account_index::bits = 20;
const uint64_t balances_by_account_index::mask = (1ULL << balances_by_account_index::bits) - 1;

void balances_by_account_index::object_inserted( const object& obj )
{
   const auto& abo = dynamic_cast< const account_balance_object& >( obj );
   while( balances.size() < (abo.owner.instance.value >> bits) + 1 )
   {
      balances.reserve( (abo.owner.instance.value >> bits) + 1 );
      balances.resize( balances.size() + 1 );
      balances.back().resize( 1ULL << bits );
   }
   balances[abo.owner.instance.value >> bits][abo.owner.instance.value & mask][abo.get_asset()] = &abo;
}

void balances_by_account_index::object_removed( const object& obj )
{
   const auto& abo = dynamic_cast< const account_balance_object& >( obj );
   if( balances.size() < (abo.owner.instance.value >> bits) + 1 ) return;
   balances[abo.owner.instance.value >> bits][abo.owner.instance.value & mask].erase( abo.get_asset() );
}

void balances_by_account_index::about_to_modify( const object& before )
{
   ids_being_modified.emplace( before.id );
}

void balances_by_account_index::object_modified( const object& after  )
{
   FC_ASSERT( ids_being_modified.top() == after.id, "Modification of ID is not supported!");
   ids_being_modified.pop();
}

const map< asset_id_type, const account_balance_object* >& balances_by_account_index::get_account_balances( const account_id_type& acct )const
{
   static const map< asset_id_type, const account_balance_object* > _empty;

   if( balances.size() < (acct.instance.value >> bits) + 1 ) return _empty;
   return balances[acct.instance.value >> bits][acct.instance.value & mask];
}

const account_balance_object* balances_by_account_index::get_account_balance( const account_id_type& acct, const asset_id_type& asset )const
{
   if( balances.size() < (acct.instance.value >> bits) + 1 ) return nullptr;
   const auto& mine = balances[acct.instance.value >> bits][acct.instance.value & mask];
   const auto itr = mine.find( asset );
   if( mine.end() == itr ) return nullptr;
   return itr->second;
}

} } // graphene::chain

FC_REFLECT_DERIVED_NO_TYPENAME( graphene::chain::account_object,
                    (graphene::db::object),
                    (membership_expiration_date)(registrar)(referrer)(lifetime_referrer)
                    (network_fee_percentage)(lifetime_referrer_fee_percentage)(referrer_rewards_percentage)
                    (name)(owner)(active)(options)(statistics)(whitelisting_accounts)(blacklisting_accounts)
                    (whitelisted_accounts)(blacklisted_accounts)
                    (cashback_vb)
                    (owner_special_authority)(active_special_authority)
                    (top_n_control_flags)
                    (allowed_assets)
                    )

FC_REFLECT_DERIVED( graphene::chain::account_balance_master,
                    (graphene::db::object),
                    (owner)(maintenance_flag) )

FC_REFLECT_DERIVED_NO_TYPENAME( graphene::chain::account_balance_object,
                    (graphene::chain::account_balance_master),
                    (balance) )

FC_REFLECT_DERIVED( graphene::chain::account_statistics_master,
                    (graphene::db::object),
                    (owner)(name)
                    (most_recent_op)
                    (total_ops)(removed_ops)
                    (total_core_in_orders)
                    (core_in_balance)
                    (has_cashback_vb)
                    (is_voting)
                    (last_vote_time)
                    (lifetime_fees_paid)
                  )

FC_REFLECT_DERIVED_NO_TYPENAME( graphene::chain::account_statistics_object,
                    (graphene::chain::account_statistics_master),
                    (pending_fees)(pending_vested_fees)
                  )

GRAPHENE_IMPLEMENT_EXTERNAL_SERIALIZATION( graphene::chain::account_object )
GRAPHENE_IMPLEMENT_EXTERNAL_SERIALIZATION( graphene::chain::account_balance_master )
GRAPHENE_IMPLEMENT_EXTERNAL_SERIALIZATION( graphene::chain::account_balance_object )
GRAPHENE_IMPLEMENT_EXTERNAL_SERIALIZATION( graphene::chain::account_statistics_master )
GRAPHENE_IMPLEMENT_EXTERNAL_SERIALIZATION( graphene::chain::account_statistics_object )
