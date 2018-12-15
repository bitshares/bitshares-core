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
#include <fc/crypto/sha1.hpp>
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
         //FC_ASSERT( db().head_block_time() > HARDFORK_CORE_1468_TIME,
         //           "Operation not allowed before HARDFORK_CORE_1468_TIME."); // remove after HARDFORK_ESCROW_TIME

         optional<htlc_options> htlc_options = get_committee_htlc_options(db());

         FC_ASSERT(htlc_options, "HTLC Committee options are not set.");

         // make sure the expiration is reasonable
         FC_ASSERT( o.claim_period_seconds <= htlc_options->max_timeout_secs, "HTLC Timeout exceeds allowed length" );
         // make sure the preimage length is reasonable
         FC_ASSERT( o.preimage_size <= htlc_options->max_preimage_size, "HTLC preimage length exceeds allowed length" ); 
         // make sure we have a hash algorithm set
         FC_ASSERT( o.hash_type != graphene::chain::hash_algorithm::unknown, "HTLC Hash Algorithm must be set" );
         // make sure the sender has the funds for the HTLC
         FC_ASSERT( db().get_balance( o.from, o.amount.asset_id) >= (o.amount), "Insufficient funds") ;
         return void_result();
      }

      object_id_type htlc_create_evaluator::do_apply(const htlc_create_operation& o)
      {
         try {
            graphene::chain::database& dbase = db();
            dbase.adjust_balance( o.from, -o.amount );

            const htlc_object& esc = db().create<htlc_object>([&dbase,o]( htlc_object& esc ) {
               esc.from                  = o.from;
               esc.to                    = o.to;
               esc.amount                = o.amount;
               esc.preimage_hash         = o.preimage_hash;
               esc.preimage_size         = o.preimage_size;
               esc.expiration            = dbase.head_block_time() + o.claim_period_seconds;
               esc.preimage_hash_algorithm = o.hash_type;
            });
            return  esc.id;

         } FC_CAPTURE_AND_RETHROW( (o) )
      }

      template<typename T>
      bool htlc_redeem_evaluator::test_hash(const std::vector<uint8_t>& incoming_preimage, const std::vector<uint8_t>& valid_hash)
      {
         T attempted_hash = T::hash( (const char*)incoming_preimage.data(), incoming_preimage.size());
         if (attempted_hash.data_size() != valid_hash.size())
            return false;
         return memcmp(attempted_hash.data(), valid_hash.data(), attempted_hash.data_size()) == 0;
      }

      void_result htlc_redeem_evaluator::do_evaluate(const htlc_redeem_operation& o)
      {
         htlc_obj = &db().get<htlc_object>(o.htlc_id);

         FC_ASSERT(o.preimage.size() == htlc_obj->preimage_size, "Preimage size mismatch.");
         FC_ASSERT(db().head_block_time() < htlc_obj->expiration, "Preimage provided after escrow expiration.");

         // see if the preimages match
         bool match = false;
         switch(htlc_obj->preimage_hash_algorithm)
         {
            case (graphene::chain::hash_algorithm::sha256):
               match = test_hash<fc::sha256>(o.preimage, htlc_obj->preimage_hash);
               break;
            case (graphene::chain::hash_algorithm::ripemd160):
               match = test_hash<fc::ripemd160>(o.preimage, htlc_obj->preimage_hash);
               break;
            case (graphene::chain::hash_algorithm::sha1):
               match = test_hash<fc::sha1>(o.preimage, htlc_obj->preimage_hash);
               break;
            default:
               break;
         }

         FC_ASSERT(match, "Provided preimage does not generate correct hash.");
         return void_result();
      }

      void_result htlc_redeem_evaluator::do_apply(const htlc_redeem_operation& o)
      {
         db().adjust_balance(htlc_obj->to, htlc_obj->amount);
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
