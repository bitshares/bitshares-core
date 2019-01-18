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

namespace graphene { 
   namespace chain {

      optional<htlc_options> get_committee_htlc_options(graphene::chain::database& db)
      {
         return db.get_global_properties().parameters.extensions.value.updatable_htlc_options;
      }

      void_result htlc_create_evaluator::do_evaluate(const htlc_create_operation& o)
      {
         optional<htlc_options> htlc_options = get_committee_htlc_options(db());

         FC_ASSERT(htlc_options, "HTLC Committee options are not set.");

         // make sure the expiration is reasonable
         FC_ASSERT( o.claim_period_seconds <= htlc_options->max_timeout_secs, "HTLC Timeout exceeds allowed length" );
         // make sure the preimage length is reasonable
         FC_ASSERT( o.preimage_size <= htlc_options->max_preimage_size, "HTLC preimage length exceeds allowed length" ); 
         // make sure the sender has the funds for the HTLC
         FC_ASSERT( db().get_balance( o.from, o.amount.asset_id) >= (o.amount), "Insufficient funds") ;
         return void_result();
      }

      object_id_type htlc_create_evaluator::do_apply(const htlc_create_operation& o)
      {
         try {
            graphene::chain::database& dbase = db();
            dbase.adjust_balance( o.from, -o.amount );

            const htlc_object& esc = db().create<htlc_object>([&dbase,&o]( htlc_object& esc ) {
               esc.from                  = o.from;
               esc.to                    = o.to;
               esc.amount                = o.amount;
               esc.preimage_hash         = o.preimage_hash;
               esc.preimage_size         = o.preimage_size;
               esc.expiration            = dbase.head_block_time() + o.claim_period_seconds;
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

         FC_ASSERT(o.preimage.size() == htlc_obj->preimage_size, "Preimage size mismatch.");

         const htlc_redeem_visitor vtor( o.preimage );
         FC_ASSERT( htlc_obj->preimage_hash.visit( vtor ), "Provided preimage does not generate correct hash.");

         return void_result();
      }

      void_result htlc_redeem_evaluator::do_apply(const htlc_redeem_operation& o)
      {
         db().adjust_balance(htlc_obj->to, htlc_obj->amount);
         // notify related parties
         htlc_redeemed_operation virt_op( htlc_obj->id, htlc_obj->from, htlc_obj->to, htlc_obj->amount );
         db().push_applied_operation( virt_op );
         db().remove(*htlc_obj);
         return void_result();
      }

      void_result htlc_extend_evaluator::do_evaluate(const htlc_extend_operation& o)
      {
         htlc_obj = &db().get<htlc_object>(o.htlc_id);
         return void_result();
      }

      void_result htlc_extend_evaluator::do_apply(const htlc_extend_operation& o)
      {
         db().modify(*htlc_obj, [&o](htlc_object& db_obj) {
            db_obj.expiration += o.seconds_to_add;
         });

         return void_result();
      }

   } // namespace chain
} // namespace graphene
