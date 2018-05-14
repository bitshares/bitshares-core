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
#include <graphene/chain/transfer_evaluator.hpp>
#include <graphene/chain/account_object.hpp>
#include <graphene/chain/exceptions.hpp>
#include <graphene/chain/hardfork.hpp>
#include <graphene/chain/is_authorized_asset.hpp>
#include <graphene/chain/vesting_balance_object.hpp>

namespace graphene { namespace chain {
void_result transfer_evaluator::do_evaluate( const transfer_operation& op )
{ try {
   
   const database& d = db();

   const account_object& from_account    = op.from(d);
   const account_object& to_account      = op.to(d);
   const asset_object&   asset_type      = op.amount.asset_id(d);

   for ( auto ext: op.extensions)
   {
      if(ext.which()==4)
      {
                cybex_ext_xfer_to_name & ext1= ext.get<cybex_ext_xfer_to_name>();

                FC_ASSERT(to_account.name==ext1.name,"${id} is ${to}.it does not match ${name}",
                     ("id",op.to)
                     ("to",to_account.name)
                     ("name",ext1.name) );
                FC_ASSERT(asset_type.symbol==ext1.asset_sym,"asset ${id} is ${asset}.it does not match ${sym}",
                     ("id",op.amount.asset_id)
                     ("asset",asset_type.symbol)
                     ("sym",ext1.asset_sym) );
                const asset_object&   fee_asset_type      = op.fee.asset_id(d);
                FC_ASSERT(fee_asset_type.symbol==ext1.fee_asset_sym,"fee asset ${id} is ${asset}.it does not match ${sym}",
                     ("id",op.fee.asset_id)
                     ("asset",fee_asset_type.symbol)
                     ("sym",ext1.fee_asset_sym) );
      }
      else if(ext.which()==1)
      {
               cybex_ext_vesting & ext1= ext.get<cybex_ext_vesting>();
               cybex_ext_vesting_check(to_account,ext1);
      }
   }
   try {

      GRAPHENE_ASSERT(
         is_authorized_asset( d, from_account, asset_type ),
         transfer_from_account_not_whitelisted,
         "'from' account ${from} is not whitelisted for asset ${asset}",
         ("from",op.from)
         ("asset",op.amount.asset_id)
         );
      GRAPHENE_ASSERT(
         is_authorized_asset( d, to_account, asset_type ),
         transfer_to_account_not_whitelisted,
         "'to' account ${to} is not whitelisted for asset ${asset}",
         ("to",op.to)
         ("asset",op.amount.asset_id)
         );

      if( asset_type.is_transfer_restricted() )
      {
         GRAPHENE_ASSERT(
            from_account.id == asset_type.issuer || to_account.id == asset_type.issuer,
            transfer_restricted_transfer_asset,
            "Asset {asset} has transfer_restricted flag enabled",
            ("asset", op.amount.asset_id)
          );
      }

      bool insufficient_balance = d.get_balance( from_account, asset_type ).amount >= op.amount.amount;
      FC_ASSERT( insufficient_balance,
                 "Insufficient Balance: ${balance}, unable to transfer '${total_transfer}' from account '${a}' to '${t}'", 
                 ("a",from_account.name)("t",to_account.name)("total_transfer",d.to_pretty_string(op.amount))("balance",d.to_pretty_string(d.get_balance(from_account, asset_type))) );

      return void_result();
   } FC_RETHROW_EXCEPTIONS( error, "Unable to transfer ${a} from ${f} to ${t}", ("a",d.to_pretty_string(op.amount))("f",op.from(d).name)("t",op.to(d).name) );

}  FC_CAPTURE_AND_RETHROW( (op) ) }

void_result transfer_evaluator::do_apply( const transfer_operation& o )
{ try {
//
// common_options.extensins
//
    fc::time_point_sec now = db().head_block_time();
    struct linear_vesting_policy vp;

    bool vesting=false;

    public_key_type public_key;
    for( future_extensions sv: o.extensions)
    {
           if(sv.which()==1) {
                cybex_ext_vesting & ext1= sv.get<cybex_ext_vesting>();
                //printf("vesting period:%lu\n",ext1.vesting_period);
                /// Duration of the vesting period, in seconds. Must be greater than 0 and greater than vesting_cliff_seconds.  uint32_t 
                vp.vesting_duration_seconds =  ext1.vesting_period;
               /// No amount may be withdrawn before this many seconds of the vesting period have elapsed.  uint32_t 
                vp.vesting_cliff_seconds = ext1.vesting_period;
                vesting=true;
                public_key= ext1.public_key;
            }
   }

   if(vesting)
   {
      /// This is the time at which funds begin vesting.  fc::time_point_sec
        vp.begin_timestamp=now;
      /// The total amount of asset to vest.  share_type 
        vp.begin_balance=o.amount.amount.value;
        db().adjust_vesting_balance( o.from,o.to,public_key, o.amount,vp );
        db().adjust_balance( o.from, -o.amount );
   }
   else
   {
      db().adjust_balance( o.from, -o.amount );
      db().adjust_balance( o.to, o.amount );
   }
   return void_result();
} FC_CAPTURE_AND_RETHROW( (o) ) }



void_result override_transfer_evaluator::do_evaluate( const override_transfer_operation& op )
{ try {
   const database& d = db();

   const asset_object&   asset_type      = op.amount.asset_id(d);
   GRAPHENE_ASSERT(
      asset_type.can_override(),
      override_transfer_not_permitted,
      "override_transfer not permitted for asset ${asset}",
      ("asset", op.amount.asset_id)
      );
   FC_ASSERT( asset_type.issuer == op.issuer );

   const account_object& from_account    = op.from(d);
   const account_object& to_account      = op.to(d);

   FC_ASSERT( is_authorized_asset( d, to_account, asset_type ) );
   FC_ASSERT( is_authorized_asset( d, from_account, asset_type ) );

   if( d.head_block_time() <= HARDFORK_419_TIME )
   {
      FC_ASSERT( is_authorized_asset( d, from_account, asset_type ) );
   }
   // the above becomes no-op after hardfork because this check will then be performed in evaluator

   FC_ASSERT( d.get_balance( from_account, asset_type ).amount >= op.amount.amount,
              "", ("total_transfer",op.amount)("balance",d.get_balance(from_account, asset_type).amount) );

   return void_result();
} FC_CAPTURE_AND_RETHROW( (op) ) }

void_result override_transfer_evaluator::do_apply( const override_transfer_operation& o )
{ try {
   db().adjust_balance( o.from, -o.amount );
   db().adjust_balance( o.to, o.amount );
   return void_result();
} FC_CAPTURE_AND_RETHROW( (o) ) }

} } // graphene::chain
