/*
 * Copyright (c) 2019 Contributors.
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
#include <graphene/protocol/authority.hpp>
#include <graphene/protocol/custom_authority.hpp>
#include <graphene/protocol/restriction_predicate.hpp>
#include <graphene/db/generic_index.hpp>
#include <graphene/chain/types.hpp>
#include <boost/multi_index/composite_key.hpp>

namespace graphene { namespace chain {

   /**
    * @brief Tracks account custom authorities
    * @ingroup object
    *
    */
   class custom_authority_object : public abstract_object<custom_authority_object> {
      /// Unreflected field to store a cache of the predicate function
      /// Note that this cache can be modified when the object is const!
      mutable optional<restriction_predicate_function> predicate_cache;

   public:
      static const uint8_t space_id = protocol_ids;
      static const uint8_t type_id = custom_authority_object_type;

      account_id_type account;
      bool enabled;
      time_point_sec valid_from;
      time_point_sec valid_to;
      unsigned_int operation_type;
      authority auth;
      flat_map<uint16_t, restriction> restrictions;
      uint16_t restriction_counter = 0;

      /// Check whether the custom authority is valid
      bool is_valid(time_point_sec now) const { return enabled && now >= valid_from && now < valid_to; }

      /// Get the restrictions as a vector rather than a map
      vector<restriction> get_restrictions() const {
         vector<restriction> rs;
         std::transform(restrictions.begin(), restrictions.end(),
                        std::back_inserter(rs), [](auto i) { return i.second; });
         return rs;
      }
      /// Get predicate, from cache if possible, and update cache if not (modifies const object!)
      restriction_predicate_function get_predicate() const {
         if (!predicate_cache.valid())
            update_predicate_cache();

         return *predicate_cache;
      }
      /// Regenerate predicate function and update predicate cache
      void update_predicate_cache() const {
         predicate_cache = get_restriction_predicate(get_restrictions(), operation_type);
      }
      /// Clear the cache of the predicate function
      void clear_predicate_cache() { predicate_cache.reset(); }
   };

   struct by_account_custom;
   struct by_expiration;

   /**
    * @ingroup object_index
    */
   typedef multi_index_container<
      custom_authority_object,
      indexed_by<
         ordered_unique<tag<by_id>, member<object, object_id_type, &object::id>>,
         ordered_unique<tag<by_account_custom>,
            composite_key<custom_authority_object,
               member<custom_authority_object, account_id_type, &custom_authority_object::account>,
               member<custom_authority_object, unsigned_int, &custom_authority_object::operation_type>,
               member<custom_authority_object, bool, &custom_authority_object::enabled>,
               member<object, object_id_type, &object::id>
            >>,
         ordered_unique<tag<by_expiration>,
            composite_key<custom_authority_object,
               member<custom_authority_object, time_point_sec, &custom_authority_object::valid_to>,
               member<object, object_id_type, &object::id>
            >
         >
      >
   > custom_authority_multi_index_type;

   /**
    * @ingroup object_index
    */
   using custom_authority_index = generic_index<custom_authority_object, custom_authority_multi_index_type>;

} } // graphene::chain

MAP_OBJECT_ID_TO_TYPE(graphene::chain::custom_authority_object)

FC_REFLECT_TYPENAME(graphene::chain::custom_authority_object)

GRAPHENE_DECLARE_EXTERNAL_SERIALIZATION(graphene::chain::custom_authority_object)
