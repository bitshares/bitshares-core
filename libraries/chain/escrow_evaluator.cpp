/*
 * Copyright (c) 2018 oxarbitrage and contributors.
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
#include <graphene/chain/escrow_evaluator.hpp>
#include <graphene/chain/escrow_object.hpp>
//#include <graphene/chain/db_balance.hpp>


namespace graphene { namespace chain {

      void_result escrow_transfer_evaluator::do_evaluate(const escrow_transfer_operation& o)
      {

      }


      object_id_type escrow_transfer_evaluator::do_apply(const escrow_transfer_operation& o)
      {

         try {
            //FC_ASSERT( db().has_hardfork( STEEMIT_HARDFORK_0_9 ) ); /// TODO: remove this after HF9

            //const auto& from_account = o.from(db());
            //const auto& to_account = o.to(db());
            //const auto& agent_account = o.agent(db());

            FC_ASSERT( db().get_balance( o.from, o.amount.asset_id ) >= (o.amount + o.fee + o.agent_fee) );

            if( o.fee.amount > 0 ) {
               db().adjust_balance( o.from, -o.agent_fee );
               db().adjust_balance( o.agent, o.agent_fee );
            }

            db().adjust_balance( o.from, -o.amount );

            db().create<escrow_object>([&]( escrow_object& esc ) {
               esc.escrow_id              = o.escrow_id;
               esc.from                   = o.from;
               esc.to                     = o.to;
               esc.agent                  = o.agent;
               esc.amount                 = o.amount;
               esc.pending_fee            = o.agent_fee;
               esc.ratification_deadline  = o.ratification_deadline;
               esc.escrow_expiration      = o.escrow_expiration;
            });

         } FC_CAPTURE_AND_RETHROW( (o) )
      }

      void_result escrow_approve_evaluator::do_evaluate(const escrow_approve_operation& o)
      {

      }


      object_id_type escrow_approve_evaluator::do_apply(const escrow_approve_operation& o)
      {

         try
         {
            //FC_ASSERT( db().has_hardfork( STEEMIT_HARDFORK_0_14__143 ) ); /// TODO: remove this after HF14

            const auto& escrow = db().get_escrow( o.from, o.escrow_id );

            FC_ASSERT( escrow.to == o.to, "op 'to' does not match escrow 'to'" );
            FC_ASSERT( escrow.agent == o.agent, "op 'agent' does not match escrow 'agent'" );

            bool reject_escrow = !o.approve;

            if( o.who == o.to )
            {
               FC_ASSERT( !escrow.to_approved, "'to' has already approved the escrow" );

               if( !reject_escrow )
               {
                  db().modify( escrow, [&]( escrow_object& esc )
                  {
                     esc.to_approved = true;
                  });
               }
            }
            else if( o.who == o.agent )
            {
               FC_ASSERT( !escrow.agent_approved, "'agent' has already approved the escrow" );

               if( !reject_escrow )
               {
                  db().modify( escrow, [&]( escrow_object& esc )
                  {
                     esc.agent_approved = true;
                  });
               }
            }
            else
            {
               FC_ASSERT( false, "op 'who' is not 'to' or 'agent'. This should have failed validation. Please create a github issue with this error dump." );
            }

            if( reject_escrow )
            {
               //const auto& from_account = db().get_account( o.from );
               //db().adjust_balance( from_account, escrow.steem_balance );
               //db().adjust_balance( from_account, escrow.sbd_balance );
               db().adjust_balance( o.from, escrow.amount );
               db().adjust_balance( o.from, escrow.pending_fee );

               db().remove( escrow );
            }
            else if( escrow.to_approved && escrow.agent_approved )
            {
               //const auto& agent_account = db().get_account( o.agent );
               db().adjust_balance( o.agent, escrow.pending_fee );

               db().modify( escrow, [&]( escrow_object& esc )
               {
                  esc.pending_fee.amount = 0;
               });
            }
         }
         FC_CAPTURE_AND_RETHROW( (o) )


      }



      void_result escrow_dispute_evaluator::do_evaluate(const escrow_dispute_operation& o)
      {

      }


      object_id_type escrow_dispute_evaluator::do_apply(const escrow_dispute_operation& o)
      {

         try {
            //FC_ASSERT( db().has_hardfork( STEEMIT_HARDFORK_0_9 ) ); /// TODO: remove this after HF9
            //const auto& from_account = db().get_account(o.from);

            const auto& e = db().get_escrow( o.from, o.escrow_id );
            FC_ASSERT( !e.disputed );
            FC_ASSERT( e.to == o.to );

            db().modify( e, [&]( escrow_object& esc ){
               esc.disputed = true;
            });
         } FC_CAPTURE_AND_RETHROW( (o) )
      }


      void_result escrow_release_evaluator::do_evaluate(const escrow_release_operation& o)
      {

      }


      object_id_type escrow_release_evaluator::do_apply(const escrow_release_operation& o)
      {

         try {
            //FC_ASSERT( db().has_hardfork( STEEMIT_HARDFORK_0_9 ) ); /// TODO: remove this after HF9

            //const auto& from_account = db().get_account(o.from);
            //const auto& to_account = db().get_account(o.to);
            //const auto& who_account = db().get_account(o.who);

            const auto& e = db().get_escrow( o.from, o.escrow_id );

            //FC_ASSERT( e.balance >= o.amount && e.balance.asset_id == o.amount.asset_id );
            FC_ASSERT( e.amount >= o.amount && e.amount.asset_id == o.amount.asset_id );

            /// TODO assert o.amount > 0

            if( e.expiration > db().head_block_time() ) {
               if( o.who == e.from )    FC_ASSERT( o.to == e.to );
               else if( o.who == e.to ) FC_ASSERT( o.to == e.from );
               else {
                  FC_ASSERT( e.disputed && o.who == e.agent );
               }
               } else {
                  FC_ASSERT( o.who == e.to || o.who == e.from );
            }

            db().adjust_balance( o.to, o.amount );
            if( e.amount == o.amount )
               db().remove( e );
            else {
               db().modify( e, [&]( escrow_object& esc ) {
                  esc.amount -= o.amount;
               });
            }
         } FC_CAPTURE_AND_RETHROW( (o) )
      }


   } } // graphene::chain

