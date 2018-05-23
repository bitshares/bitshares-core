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
#include <graphene/chain/asset_object.hpp>
#include <graphene/chain/account_object.hpp>
#include <graphene/chain/database.hpp>
#include <graphene/chain/exceptions.hpp>
#include <functional>
#include <cybex/crowdfund_evaluator.hpp>
#include <cybex/crowdfund_contract.hpp>
#include <cybex/crowdfund.hpp>

namespace graphene { namespace chain {


void_result initiate_crowdfund_evaluator::do_evaluate( const initiate_crowdfund_operation& o )
{ try {
   const database& d = db();
   const asset_object &a = d.get(o.asset_id);
   FC_ASSERT( o.owner == a.issuer,"only asset issuer can initiate crownfund" );
   FC_ASSERT( !a.is_market_issued(), "Cannot manually issue a market-issued asset." );

   auto const & asset_dyn_data = a.dynamic_asset_data_id(d);
   FC_ASSERT( (0==asset_dyn_data.current_supply),"asset is already issued" );

   auto &crowdfund_idx = d.get_index_type<crowdfund_index>().indices().get<by_owner>();
   auto itr = crowdfund_idx.find(boost::make_tuple(o.owner, o.asset_id));
   FC_ASSERT( itr == crowdfund_idx.end(),"asset is already on crowd sale." );
   
   return void_result();
} FC_CAPTURE_AND_RETHROW( (o) ) }

object_id_type  initiate_crowdfund_evaluator::do_apply( const initiate_crowdfund_operation& op )
{ try {
   fc::time_point_sec now = db().head_block_time();
   
   auto next_crowdfund_id = db().get_index_type<crowdfund_index>().get_next_id();

   const crowdfund_object& new_crowdfund =
     db().create<crowdfund_object>( [&]( crowdfund_object& a ) {
         a.owner = op.owner;
         a.t = op.t;
         a.u = op.u;
         a.asset_id = op.asset_id;
         a.begin = now; 
         a.V=0;
         a.state=0; 
      });
   assert( new_crowdfund.id == next_crowdfund_id );

   return (object_id_type)new_crowdfund.id;
} FC_CAPTURE_AND_RETHROW( (op) ) }


void_result participate_crowdfund_evaluator::do_evaluate(const participate_crowdfund_operation& op)
{ try {
   fc::time_point_sec now = db().head_block_time();
   
   const database& d = db();
   from_account      = &op.buyer(d);
   crowdfund         = &d.get(op.crowdfund);
   crowdfund_asset   = &d.get(crowdfund->asset_id);
 
   const asset_object & cyb_asset = d.get(asset_id_type(0)); 
   
   FC_ASSERT( op.buyer != crowdfund_asset->issuer,"asset issuer can not participate crownfund" );

   bool insufficient_balance = d.get_balance( *from_account, cyb_asset ).amount >= op.valuation;
   FC_ASSERT( insufficient_balance,
                 "Insufficient Balance: ${balance}, '${a}' is unable to buy '${total_buy}'  '${t}'",
                 ("a",from_account->name)
                 ("t",crowdfund->asset_id)
                 ("total_buy",op.cap)
                 ("balance",d.to_pretty_string(d.get_balance(*from_account, *crowdfund_asset))) );

   s = (now-crowdfund->begin).to_seconds();

   FC_ASSERT(s<crowdfund->u,"crowd sale has ended.");

   // any inactive address can take part in.i.e. has the buyer already partcipated?
   const auto& crowdfund_contract_idx = d.get_index_type<crowdfund_contract_index>();
   const auto& by_owner_idx = crowdfund_contract_idx.indices().get<by_owner>();
   const auto& itr = by_owner_idx.find( boost::make_tuple(op.buyer,op.crowdfund));
   FC_ASSERT(itr==by_owner_idx.end(),"must from inactive address.");

   // when s>=t, stricter inequality:c(A)>V
   if ( s> crowdfund->t)
   {
       FC_ASSERT(op.cap>crowdfund->V,"c(A)>V");  
   }              

   return void_result();
} FC_CAPTURE_AND_RETHROW( (op) ) }

object_id_type  participate_crowdfund_evaluator::do_apply(const participate_crowdfund_operation& op)
{ try {
   database& d = db();
   fc::time_point_sec now = d.head_block_time();
   auto next_crowdfund_contract_id = d.get_index_type<crowdfund_contract_index>().get_next_id();

   const crowdfund_contract_object& new_crowdfund_contract =
     d.create<crowdfund_contract_object>( [&]( crowdfund_contract_object& a ) {
         a.owner     = op.buyer;
         a.valuation = op.valuation;
         a.cap       = op.cap;
         a.crowdfund = op.crowdfund;
         a.when      = now;  
         a.state     = CROWDFUND_STATE_ACTIVE;  
      });
   assert( new_crowdfund_contract.id == next_crowdfund_contract_id );

   asset cyb_amount,b_A_amount;
   cyb_amount.asset_id     = asset_id_type(0);
   cyb_amount.amount       = op.valuation; 
   b_A_amount.asset_id     = crowdfund->asset_id;
   b_A_amount.amount       = op.valuation*crowdfund->p(s);  //v(A)*(1+20%)

   d.adjust_balance( from_account->get_id(), -cyb_amount );
   d.adjust_balance( from_account->get_id(), b_A_amount );

   d.modify( *crowdfund,[&](crowdfund_object &c) {
         c.V+= op.valuation;
   });

   //update asset dynamic data current_supply
   auto const & asset_dyn_data = crowdfund_asset->dynamic_asset_data_id(d);
   d.modify( asset_dyn_data,[&](asset_dynamic_data_object &dyn_data) {
          dyn_data.current_supply += op.valuation;
   });

   return (object_id_type)new_crowdfund_contract.id;


} FC_CAPTURE_AND_RETHROW( (op) ) }

void_result withdraw_crowdfund_evaluator::do_evaluate(const withdraw_crowdfund_operation& o)
{ try {

   const database & d=db();
   contract  = &d.get(o.crowdfund_contract);
   FC_ASSERT(contract!=NULL,"crowdfund contract ${id}not exists.",("id",o.crowdfund_contract));
   FC_ASSERT( contract->owner == o.buyer,"only owner can withdraw."  );
   FC_ASSERT( contract->state == CROWDFUND_STATE_ACTIVE );
   crowdfund = &d.get(contract->crowdfund);
   FC_ASSERT(crowdfund!=NULL,"crowdfund ${id} not exists.",("id",contract->crowdfund));
   owner = &d.get(contract->owner);
   FC_ASSERT(owner!=NULL,"contract owner ${id} not exists.",("id",contract->owner));
 
   fc::time_point_sec now = d.head_block_time();
   FC_ASSERT( now>crowdfund->begin);

   uint64_t s = (now-crowdfund->begin).to_seconds();
   t = crowdfund->t;
   u = crowdfund->u;
   FC_ASSERT( s<t, "time is over.");
   FC_ASSERT( s<u, "has ended.");
   FC_ASSERT( contract->state !=CROWDFUND_STATE_PERM,"can not be withdrawn again.");

   return void_result();
} FC_CAPTURE_AND_RETHROW( (o) ) }

void_result withdraw_crowdfund_evaluator::do_apply(const withdraw_crowdfund_operation& o)
{ try {
   database& d = db();


    
   asset cyb_amount,b_A_amount;

   //refunds v(A)·(t−s)/t native tokens back to A
   share_type refund_amount =  contract->valuation.value*(t-s)/t;
   cyb_amount.asset_id = asset_id_type(0);
   cyb_amount.amount = refund_amount;

   //b(A)=v(A)·s/t·p(s + (u−s)/3), 
   share_type b_A = contract->valuation.value*s/t*(2*crowdfund->p(s)-1)/3;  
   b_A_amount.asset_id= crowdfund->asset_id;
   b_A_amount.amount.value= b_A.value;

   // delta of total asset issued.
   // share_type delta = b_A.value - d.get_balance( creator, crowdfund.asset_id ).amount;  

    
   db().adjust_balance( owner->get_id(), cyb_amount );
   db().set_balance( owner->get_id(), b_A_amount );
   
   db().modify(*contract, [&](crowdfund_contract_object& c) {
         c.state = CROWDFUND_STATE_PERM;
   });
   db().modify( *crowdfund,[&](crowdfund_object &c) {
         c.V -=  refund_amount.value;
   });
   //update asset dynamic data current_supply
   const auto & crowdfund_asset = d.get(crowdfund->asset_id); 
   auto const & asset_dyn_data = crowdfund_asset.dynamic_asset_data_id(d);
   d.modify( asset_dyn_data,[&](asset_dynamic_data_object &dyn_data) {
          dyn_data.current_supply -= refund_amount.value;
   });
   

   return void_result();
} FC_CAPTURE_AND_RETHROW( (o) ) }



} } // graphene::chain
