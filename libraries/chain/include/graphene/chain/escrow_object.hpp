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
#pragma once

#include <graphene/chain/protocol/types.hpp>
#include <graphene/db/object.hpp>
#include <graphene/db/generic_index.hpp>

namespace graphene { namespace chain {

      /**
       * Temporally save escrow transactions until funds are released or operation expired.
       */
      class escrow_object : public graphene::db::abstract_object<escrow_object> {
         public:
            static const uint8_t space_id = implementation_ids;
            static const uint8_t type_id  = impl_escrow_object_type;

            uint32_t                escrow_id=10;
            account_id_type         from;
            account_id_type         to;
            account_id_type         agent;
            asset                   amount;
            time_point_sec          ratification_deadline;
            time_point_sec          escrow_expiration;
            asset                   pending_fee;
            bool                    to_approved = false;
            bool                    agent_approved = false;
            bool                    disputed = false;

            bool is_approved()const { return to_approved && agent_approved; }
      };

      struct by_from_id;
      struct by_ratification_deadline;
      typedef multi_index_container<
         escrow_object,
         indexed_by<
            ordered_unique< tag< by_id >, member< object, object_id_type, &object::id > >,

            ordered_unique< tag< by_from_id >,
               composite_key< escrow_object,
                  member< escrow_object, account_id_type,  &escrow_object::from >,
                  member< escrow_object, uint32_t, &escrow_object::escrow_id >
               >
            >,
            ordered_unique< tag< by_ratification_deadline >,
               composite_key< escrow_object,
                  const_mem_fun< escrow_object, bool, &escrow_object::is_approved >,
                  member< escrow_object, time_point_sec, &escrow_object::ratification_deadline >,
                  member< escrow_object, uint32_t, &escrow_object::escrow_id >
               >,
               composite_key_compare< std::less< bool >, std::less< time_point_sec >, std::less< uint32_t > >
            >
         >
      > escrow_object_index_type;

      typedef generic_index< escrow_object, escrow_object_index_type > escrow_index;

   } }

FC_REFLECT_DERIVED( graphene::chain::escrow_object, (graphene::db::object),
                    (escrow_id)(from)(to)(agent)(ratification_deadline)(escrow_expiration)(pending_fee)(amount)(disputed)(to_approved)(agent_approved) );