/*
 * Copyright (c) 2019 Blockchain Projects BV.
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
#include <boost/range/numeric.hpp>
#include <boost/range/adaptor/map.hpp>
#include <boost/container/flat_map.hpp>
#include <boost/container/flat_set.hpp>
#include <boost/multi_index/composite_key.hpp>

#include <fc/optional.hpp>

#include <graphene/chain/database.hpp>
#include <graphene/voting_stat/voting_stat_plugin.hpp>

#include <graphene/protocol/types.hpp>
#include <graphene/protocol/vote.hpp>

#include <graphene/db/generic_index.hpp>

using graphene::chain::object_id_type;
using graphene::chain::account_id_type;
using graphene::chain::vote_id_type;

using graphene::db::object;
using graphene::db::abstract_object;
using graphene::db::generic_index;
using graphene::db::by_id;

using namespace boost::multi_index;
using boost::container::flat_map;
using boost::container::flat_set;

namespace graphene { namespace voting_stat {
   /**
    * @brief tracks the number maintenance interval occurences
    * @ingroup object
    * @ingroup voting_stat_plugin
    *
    * The number of maintenance intervals to be tracked is set in this object. Since a fork can occur during a
    * maintenance interval, it is not sufficient to track the number of intervals through a plugin internal
    * variable. In the case of a fork this object will be reverted together with the internal maintenance counter.
    * Through the lifetime of the plugin there will be only one of this objects.
    *
    * @note By default this object are not tracked, the voting_stat_plugin must be loaded for this object to
    * be maintained.
    */
   class maintenance_counter_object : public abstract_object<maintenance_counter_object>
   {
   public:
      static const uint8_t space_id = VOTING_STAT_SPACE_ID;
      static const uint8_t type_id  = voting_stat_object_type_ids::maintenance_counter_object_type_id;

      maintenance_counter_object(){}

      bool counter_reached( chain::database& db ) const
      {
         if( counter == max_counter )
         {
            db.modify<maintenance_counter_object>( *this, [](maintenance_counter_object& o) {
               o.counter = 0;
            });
            return true;
         }
         db.modify<maintenance_counter_object>( *this, [](maintenance_counter_object& o) {
            o.counter += 1;
         });
         return false;
      }

      uint16_t max_counter = 12; // every 12th maintenance interval vote*_objects will be created
      uint16_t counter     = 12;
   };

   typedef multi_index_container< maintenance_counter_object,
      indexed_by<
         ordered_unique< tag<by_id>,
            member< object, object_id_type, &object::id >
         >
      >
   > maintenance_counter_multi_index_type;

   typedef generic_index<
      maintenance_counter_object, maintenance_counter_multi_index_type > maintenance_counter_index;

}} // graphene::chain

FC_REFLECT_DERIVED( graphene::voting_stat::maintenance_counter_object, (graphene::chain::object),
   (max_counter)(counter) )
