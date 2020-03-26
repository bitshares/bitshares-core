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

#include <graphene/protocol/htlc.hpp>
#include <graphene/db/generic_index.hpp>
#include <graphene/db/undo_database.hpp>

#include <boost/multi_index/composite_key.hpp>

namespace graphene { namespace chain {
   using namespace protocol;

   /**
    * @brief database object to store HTLCs
    * 
    * This object is stored in the database while an HTLC is active. The HTLC will
    * become inactive at expiration or when unlocked via the preimage.
    */
   class htlc_object;
   class htlc_master : public graphene::db::abstract_object< htlc_master, htlc_object > {
      public:
         // uniquely identify this object in the database
         static constexpr uint8_t space_id = protocol_ids;
         static constexpr uint8_t type_id  = htlc_object_type;

         struct transfer_info_master {
            account_id_type from;
            account_id_type to;
         };
         struct condition_info {
            struct hash_lock_info {  
               htlc_hash preimage_hash;
               uint16_t preimage_size;
            } hash_lock;
            struct time_lock_info {
               fc::time_point_sec expiration;
            } time_lock;
         } conditions;

      /****
       * Index helper for timelock
       */
      struct timelock_extractor {
         typedef fc::time_point_sec result_type;
         const result_type& operator()(const htlc_master& o)const { return o.conditions.time_lock.expiration; }
      };

      virtual const transfer_info_master& get_transfer_info()const { FC_ASSERT( !"Override in subclass!" ); }
   };

   class htlc_object : public htlc_master
   {
      public:
         struct transfer_info : transfer_info_master {
            stored_value amount;
         } transfer;

      /*****
       * Index helper for from
       */
      struct from_extractor {
         typedef account_id_type result_type;
         const result_type& operator()(const htlc_object& o)const { return o.transfer.from; }
      };

      /*****
       * Index helper for to
       */
      struct to_extractor {
         typedef account_id_type result_type;
         const result_type& operator()(const htlc_object& o)const { return o.transfer.to; }
      };

      virtual const transfer_info_master& get_transfer_info()const { return transfer; }

   protected:
      virtual unique_ptr<graphene::db::object> backup()const;
      virtual void restore( graphene::db::object& obj );
      virtual void clear();
   };

   struct by_from_id;
   struct by_expiration;
   struct by_to_id;
   typedef multi_index_container<
         htlc_object,
         indexed_by<
            ordered_unique< tag< by_id >, member< object, object_id_type, &object::id > >,

            ordered_unique< tag< by_expiration >, 
                  composite_key< htlc_object,
                  htlc_master::timelock_extractor,
                  member< object, object_id_type, &object::id > > >,

            ordered_unique< tag< by_from_id >,
                  composite_key< htlc_object, 
                  htlc_object::from_extractor,
                  member< object, object_id_type, &object::id > > >,

            ordered_unique< tag< by_to_id >,
                  composite_key< htlc_object,
                  htlc_object::to_extractor,
                  member< object, object_id_type, &object::id > > >
      >

   > htlc_object_index_type;

   typedef generic_index< htlc_object, htlc_object_index_type > htlc_index;

} } // namespace graphene::chain

MAP_OBJECT_ID_TO_TYPE(graphene::chain::htlc_object)

FC_REFLECT( graphene::chain::htlc_master::transfer_info_master, (from)(to) )
FC_REFLECT( graphene::chain::htlc_master::condition_info::hash_lock_info,
   (preimage_hash) (preimage_size) )
FC_REFLECT( graphene::chain::htlc_master::condition_info::time_lock_info,
   (expiration) )
FC_REFLECT( graphene::chain::htlc_master::condition_info,
   (hash_lock)(time_lock) )
FC_REFLECT_DERIVED( graphene::chain::htlc_master, (graphene::db::object), (conditions) )

FC_REFLECT_TYPENAME( graphene::chain::htlc_object )

GRAPHENE_DECLARE_EXTERNAL_SERIALIZATION( graphene::chain::htlc_master )
GRAPHENE_DECLARE_EXTERNAL_SERIALIZATION( graphene::chain::htlc_object )
