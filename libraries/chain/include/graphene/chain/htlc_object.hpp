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
#pragma once

#include <boost/multi_index/composite_key.hpp>
#include <fc/time.hpp>
#include <graphene/chain/protocol/types.hpp>
#include <graphene/chain/protocol/htlc.hpp>
#include <graphene/chain/protocol/asset.hpp>
#include <graphene/db/object.hpp>
#include <graphene/db/generic_index.hpp>

namespace graphene { namespace chain {

   /**
    * @brief database object to store HTLCs
    * 
    * This object is stored in the database while an HTLC is active. The HTLC will
    * become inactive at expiration or when unlocked via the preimage.
    */
   class htlc_object : public graphene::db::abstract_object<htlc_object> {
      public:
         // uniquely identify this object in the database
         static const uint8_t space_id = protocol_ids;
         static const uint8_t type_id  = htlc_object_type;

         account_id_type from;
         account_id_type to;
         asset amount;
         fc::time_point_sec expiration;
         htlc_hash preimage_hash;
         uint16_t preimage_size;
   };

   struct by_from_id;
   struct by_expiration;
   typedef multi_index_container<
         htlc_object,
         indexed_by<
            ordered_unique< tag< by_id >, member< object, object_id_type, &object::id > >,

            ordered_unique< tag< by_expiration >, 
                  composite_key< htlc_object, 
                  member< htlc_object, fc::time_point_sec, &htlc_object::expiration >,
                  member< object, object_id_type, &object::id > > >,

            ordered_unique< tag< by_from_id >,
                  composite_key< htlc_object, 
                  member< htlc_object, account_id_type,  &htlc_object::from >,
                  member< object, object_id_type, &object::id > > >
         >

   > htlc_object_index_type;

   typedef generic_index< htlc_object, htlc_object_index_type > htlc_index;

} } // namespace graphene::chain

FC_REFLECT_DERIVED( graphene::chain::htlc_object, (graphene::db::object),
               (from)(to)(amount)(expiration)
					(preimage_hash)(preimage_size) );
