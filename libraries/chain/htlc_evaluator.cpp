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

      void_result htlc_create_evaluator::do_evaluate(const htlc_create_operation& o)
      {
          //FC_ASSERT( db().head_block_time() > HARDFORK_ESCROW_TIME,
          //           "Operation not allowed before HARDFORK_ESCROW_TIME."); // remove after HARDFORK_ESCROW_TIME

          FC_ASSERT( fc::time_point_sec(o.epoch) > db().head_block_time() );
          // make sure we have the funds for the HTLC
          FC_ASSERT( db().get_balance( o.source, o.amount.asset_id ) >= (o.amount ) );
          // make sure we have the funds for the fee
          FC_ASSERT( db().get_balance( o.source, asset().asset_id) > o.fee );
          return void_result();
      }

      object_id_type htlc_create_evaluator::do_apply(const htlc_create_operation& o)
      {
          try {
             //FC_ASSERT( db().head_block_time() > HARDFORK_ESCROW_TIME,
             //           "Operation not allowed before HARDFORK_ESCROW_TIME."); // remove after HARDFORK_ESCROW_TIME

             db().adjust_balance( o.source, -o.amount );

             const htlc_object& esc = db().create<htlc_object>([&]( htlc_object& esc ) {
                esc.from                  = o.source;
                esc.to                    = o.destination;
                esc.amount                = o.amount;
                esc.preimage_hash		   = o.key_hash;
                esc.preimage_size		   = o.key_size;
                esc.expiration		      = o.epoch;
                esc.preimage_hash_algorithm = o.hash_type;
             });
             return  esc.id;

          } FC_CAPTURE_AND_RETHROW( (o) )
      }

      template<typename T>
      bool test_hash(const std::vector<unsigned char>& incoming_preimage, const std::vector<unsigned char>& valid_hash)
      {
         std::string incoming_string(incoming_preimage.begin(), incoming_preimage.end());
         T attempted_hash = T::hash(incoming_string);
         if (attempted_hash.data_size() != valid_hash.size())
            return false;
         char* data = attempted_hash.data();
         for(size_t i = 0; i < attempted_hash.data_size(); ++i)
         {
            if ( ((unsigned char)data[i]) != valid_hash[i])
               return false;
         }
         return true;
      }

      void_result htlc_update_evaluator::do_evaluate(const htlc_update_operation& o)
      {
    	  htlc_obj = &db().get<htlc_object>(o.htlc_id);

    	  // TODO: Use signatures to determine what to do, not whether preimage was provided
    	  if (o.preimage.size() > 0)
    	  {
    		   FC_ASSERT(o.preimage.size() == htlc_obj->preimage_size, "Preimage size mismatch.");
    		   FC_ASSERT(fc::time_point::now().sec_since_epoch() < htlc_obj->expiration.sec_since_epoch(), "Preimage provided after escrow expiration.");

    		   // see if the preimages match
            bool match = false;
            if (htlc_obj->preimage_hash_algorithm == graphene::chain::hash_algorithm::sha256)
               match = test_hash<fc::sha256>(o.preimage, htlc_obj->preimage_hash);
            if (htlc_obj->preimage_hash_algorithm == graphene::chain::hash_algorithm::ripemd160)
               match = test_hash<fc::ripemd160>(o.preimage, htlc_obj->preimage_hash);

    		  FC_ASSERT(match, "Provided preimage does not generate correct hash.");
    	  }
    	  else
    	  {
    		  FC_ASSERT(fc::time_point::now().sec_since_epoch() > htlc_obj->expiration.sec_since_epoch(), "Unable to reclaim until escrow expiration.");
    	  }
    	  return void_result();
      }

      void_result htlc_update_evaluator::do_apply(const htlc_update_operation& o)
      {
 		  db().adjust_balance(htlc_obj->to, htlc_obj->amount);
  		  db().remove(*htlc_obj);
    	  return void_result();
      }
   } // namespace chain
} // namespace graphene
