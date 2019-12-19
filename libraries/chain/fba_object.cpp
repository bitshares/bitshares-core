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
#include <graphene/chain/fba_object.hpp>

namespace graphene { namespace chain {

bool fba_accumulator_master::is_configured( const database& db )const
{
   if( !designated_asset.valid() )
   {
      ilog( "FBA fee in block ${b} not paid because designated asset was not configured", ("b", db.head_block_num()) );
      return false;
   }
   const asset_object* dasset = db.find(*designated_asset);
   if( dasset == nullptr )
   {
      ilog( "FBA fee in block ${b} not paid because of FBA misconfiguration:  designated asset does not exist", ("b", db.head_block_num()) );
      return false;
   }
   if( dasset->is_market_issued() )
   {
      ilog( "FBA fee in block ${b} not paid because of FBA misconfiguration:  FBA is a BitAsset", ("b", db.head_block_num()) );
      return false;
   }

   const uint16_t allowed_flags = charge_market_fee;

   // check enabled issuer_permissions bits is subset of allowed_flags bits
   if( (dasset->options.issuer_permissions & allowed_flags) != dasset->options.issuer_permissions )
   {
      ilog( "FBA fee in block ${b} not paid because of FBA misconfiguration:  Disallowed permissions enabled", ("b", db.head_block_num()) );
      return false;
   }

   // check enabled issuer_permissions bits is subset of allowed_flags bits
   if( (dasset->options.flags & allowed_flags) != dasset->options.flags )
   {
      ilog( "FBA fee in block ${b} not paid because of FBA misconfiguration:  Disallowed flags enabled", ("b", db.head_block_num()) );
      return false;
   }

   if( !dasset->buyback_account.valid() )
   {
      ilog( "FBA fee in block ${b} not paid because of FBA misconfiguration:  designated asset does not have a buyback account", ("b", db.head_block_num()) );
      return false;
   }
   const account_object& issuer_acct = dasset->issuer(db);

   if( !issuer_acct.owner_special_authority.is_type< top_holders_special_authority >() )
   {
      ilog( "FBA fee in block ${b} not paid because of FBA misconfiguration:  designated asset issuer has not set owner top_n control", ("b", db.head_block_num()) );
      return false;
   }
   if( !issuer_acct.active_special_authority.is_type< top_holders_special_authority >() )
   {
      ilog( "FBA fee in block ${b} not paid because of FBA misconfiguration:  designated asset issuer has not set active top_n control", ("b", db.head_block_num()) );
      return false;
   }
   if( issuer_acct.owner_special_authority.get< top_holders_special_authority >().asset != *designated_asset )
   {
      ilog( "FBA fee in block ${b} not paid because of FBA misconfiguration:  designated asset issuer's top_n_control is not set to designated asset", ("b", db.head_block_num()) );
      return false;
   }
   if( issuer_acct.active_special_authority.get< top_holders_special_authority >().asset != *designated_asset )
   {
      ilog( "FBA fee in block ${b} not paid because of FBA misconfiguration:  designated asset issuer's top_n_control is not set to designated asset", ("b", db.head_block_num()) );
      return false;
   }

   if( issuer_acct.top_n_control_flags != (account_object::top_n_control_owner | account_object::top_n_control_active) )
   {
      ilog( "FBA fee in block ${b} not paid because designated asset's top_n control has not yet activated (wait until next maintenance interval)", ("b", db.head_block_num()) );
      return false;
   }

   return true;
}

class fba_accumulator_backup : public fba_accumulator_master, public backup_object<fba_accumulator_object>
{
      share_type accumulated_fba_fees;
      friend class fba_accumulator_object;

   public:
      fba_accumulator_backup( const fba_accumulator_object& original )
         : fba_accumulator_master( original )
      {
         accumulated_fba_fees = original.accumulated_fba_fees.get_amount();
      }

      virtual object* recreate() { return graphene::db::backup_object<fba_accumulator_object>::recreate(); }
};

unique_ptr<object> fba_accumulator_object::backup()const
{
   return std::make_unique<fba_accumulator_backup>( *this );
}

void fba_accumulator_object::restore( object& obj )
{
   const auto& backup = static_cast<fba_accumulator_backup&>(obj);
   accumulated_fba_fees.restore( asset( backup.accumulated_fba_fees ) );
   static_cast<fba_accumulator_master&>(*this) = std::move( backup );
}

void fba_accumulator_object::clear()
{
   accumulated_fba_fees.clear();
}

} }

FC_REFLECT_DERIVED_NO_TYPENAME( graphene::chain::fba_accumulator_object, (graphene::chain::fba_accumulator_master),
                                (accumulated_fba_fees) )

GRAPHENE_IMPLEMENT_EXTERNAL_SERIALIZATION( graphene::chain::fba_accumulator_master )
GRAPHENE_IMPLEMENT_EXTERNAL_SERIALIZATION( graphene::chain::fba_accumulator_object )
