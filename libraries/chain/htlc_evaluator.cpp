/*
 * Copyright (c) 2018 jmjatlanta and contributors.
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
#include <graphene/chain/htlc_evaluator.hpp>
#include <graphene/chain/htlc_object.hpp>
#include <graphene/chain/hardfork.hpp>
#include <graphene/chain/is_authorized_asset.hpp>

namespace graphene { 
   namespace chain {

      optional<htlc_options> get_committee_htlc_options(graphene::chain::database& db)
      {
         return db.get_global_properties().parameters.extensions.value.updatable_htlc_options;
      }

      void_result htlc_create_evaluator::do_evaluate(const htlc_create_operation& o)
      {
         graphene::chain::database& d = db();
         optional<htlc_options> htlc_options = get_committee_htlc_options(db());

         FC_ASSERT(htlc_options, "HTLC Committee options are not set.");

         // make sure the expiration is reasonable
         FC_ASSERT( o.claim_period_seconds <= htlc_options->max_timeout_secs, "HTLC Timeout exceeds allowed length" );
         // make sure the preimage length is reasonable
         FC_ASSERT( o.preimage_size <= htlc_options->max_preimage_size, "HTLC preimage length exceeds allowed length" );
         // make sure the sender has the funds for the HTLC
         FC_ASSERT( d.get_balance( o.from, o.amount.asset_id) >= (o.amount), "Insufficient funds") ;
         const auto& asset_to_transfer = o.amount.asset_id( d );
         const auto& from_account = o.from( d );
         const auto& to_account = o.to( d );
         FC_ASSERT( is_authorized_asset( d, from_account, asset_to_transfer ), 
               "Asset ${asset} is not authorized for account ${acct}.", 
               ( "asset", asset_to_transfer.id )( "acct", from_account.id ) );
         FC_ASSERT( is_authorized_asset( d, to_account, asset_to_transfer ), 
               "Asset ${asset} is not authorized for account ${acct}.", 
               ( "asset", asset_to_transfer.id )( "acct", to_account.id ) );
         return void_result();
      }

      object_id_type htlc_create_evaluator::do_apply(const htlc_create_operation& o)
      {
         try {
            graphene::chain::database& dbase = db();
            dbase.adjust_balance( o.from, -o.amount );

            const htlc_object& esc = db().create<htlc_object>([&dbase,&o]( htlc_object& esc ) {
               esc.transfer.from                  = o.from;
               esc.transfer.to                    = o.to;
               esc.transfer.amount                = o.amount.amount;
               esc.transfer.asset_id              = o.amount.asset_id;
               esc.conditions.hash_lock.preimage_hash = o.preimage_hash;
               esc.conditions.hash_lock.preimage_size = o.preimage_size;
               esc.conditions.time_lock.expiration    = dbase.head_block_time() + o.claim_period_seconds;
            });
            return  esc.id;

         } FC_CAPTURE_AND_RETHROW( (o) )
      }

      class htlc_redeem_visitor
      {
      //private:
         const std::vector<char>& data;
      public:
         typedef bool result_type;

         htlc_redeem_visitor( const std::vector<char>& preimage )
            : data( preimage ) {}

         template<typename T>
         bool operator()( const T& preimage_hash )const
         {
            return T::hash( (const char*)data.data(), (uint32_t) data.size() ) == preimage_hash;
         }
      };

      void_result htlc_redeem_evaluator::do_evaluate(const htlc_redeem_operation& o)
      {
         htlc_obj = &db().get<htlc_object>(o.htlc_id);

         FC_ASSERT(o.preimage.size() == htlc_obj->conditions.hash_lock.preimage_size, "Preimage size mismatch.");
         const htlc_redeem_visitor vtor( o.preimage );
         FC_ASSERT( htlc_obj->conditions.hash_lock.preimage_hash.visit( vtor ), 
               "Provided preimage does not generate correct hash.");

         return void_result();
      }

      void_result htlc_redeem_evaluator::do_apply(const htlc_redeem_operation& o)
      {
         db().adjust_balance(htlc_obj->transfer.to, asset(htlc_obj->transfer.amount, htlc_obj->transfer.asset_id) );
         // notify related parties
         htlc_redeemed_operation virt_op( htlc_obj->id, htlc_obj->transfer.from, htlc_obj->transfer.to,
               o.redeemer, asset(htlc_obj->transfer.amount, htlc_obj->transfer.asset_id ) );
         db().push_applied_operation( virt_op );
         db().remove(*htlc_obj);
         return void_result();
      }

      void_result htlc_extend_evaluator::do_evaluate(const htlc_extend_operation& o)
      {
         htlc_obj = &db().get<htlc_object>(o.htlc_id);
         FC_ASSERT(o.update_issuer == htlc_obj->transfer.from, "HTLC may only be extended by its creator.");
         optional<htlc_options> htlc_options = get_committee_htlc_options(db());
         FC_ASSERT( htlc_obj->conditions.time_lock.expiration.sec_since_epoch() 
               + static_cast<uint64_t>(o.seconds_to_add) < fc::time_point_sec::maximum().sec_since_epoch(), 
               "Extension would cause an invalid date");
         FC_ASSERT( htlc_obj->conditions.time_lock.expiration + o.seconds_to_add
                <=  db().head_block_time() + htlc_options->max_timeout_secs, 
                "Extension pushes contract too far into the future" );
         return void_result();
      }

      void_result htlc_extend_evaluator::do_apply(const htlc_extend_operation& o)
      {
         db().modify(*htlc_obj, [&o](htlc_object& db_obj) {
            db_obj.conditions.time_lock.expiration += o.seconds_to_add;
         });

         return void_result();
      }

   } // namespace chain
} // namespace graphene
