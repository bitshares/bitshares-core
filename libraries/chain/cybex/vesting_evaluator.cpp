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
#include <graphene/chain/balance_object.hpp>
#include <graphene/chain/database.hpp>
#include <graphene/chain/exceptions.hpp>
#include <functional>
#include <cybex/vesting_evaluator.hpp>

namespace graphene { namespace chain {


void_result cancel_vesting_evaluator::do_evaluate( const cancel_vesting_operation& o )
{ try {
   const database& d = db();
   const balance_object & obj = o.balance_object(d);
   //ilog("balance_object:${a}",("a",obj));
   FC_ASSERT( &obj != nullptr  ,"balance object not exsists." );
   FC_ASSERT( obj.sender == o.sender,"only balance object sender can cancel vesting" );
   FC_ASSERT( obj.state == 0,"balance object is already cancelled." );
   FC_ASSERT( obj.vesting_policy.valid(),"balance object vesting policy not present." );
   
   return void_result();
} FC_CAPTURE_AND_RETHROW( (o) ) }

void_result  cancel_vesting_evaluator::do_apply( const cancel_vesting_operation& op )
{ try {
   database& d = db();
   int32_t now = d.head_block_time().sec_since_epoch();
   
   const balance_object &  obj = d.get<balance_object>(op.balance_object);
   //ilog("balance_object:${a}",("a",obj));

   int32_t start = obj.vesting_policy->begin_timestamp.sec_since_epoch();
   int32_t duration = obj.vesting_policy->vesting_duration_seconds;


   if ( now<start){
        
        d.adjust_balance(op.sender,obj.balance);
        d.remove(obj);

   } else if ( now > start + duration ) {
        ;
   } else {
        asset remain;
        float percentage = (float)(now-start)/duration; 
        remain.asset_id = obj.balance.asset_id;
        remain.amount.value = obj.balance.amount.value * percentage;
        asset delta = obj.balance  - remain;

        //ilog("delta:${a}",("a",delta));
        //ilog("remain:${a}",("a",remain));
        //ilog("sender:${a}",("a",op.sender));

   
        d.modify( obj,[&]( balance_object& a ) {
             a.state = 1;
             a.balance = remain;
        });

        d.adjust_balance(op.sender,delta);
   }
   return void_result();
} FC_CAPTURE_AND_RETHROW( (op) ) }

}}
