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
//#include <fc/io/raw.hpp>

#include <graphene/chain/protocol/types.hpp>
#include <graphene/db/object.hpp>
//#include <graphene/db/index.hpp>
#include <graphene/db/generic_index.hpp>
//#include <fc/uint128.hpp>

//#include <boost/multi_index_container.hpp>
//#include <boost/multi_index/member.hpp>
//#include <boost/multi_index/ordered_index.hpp>
//#include <boost/multi_index/hashed_index.hpp>
//#include <boost/multi_index/mem_fun.hpp>

namespace graphene { namespace chain {
      //using namespace graphene::db;
      //using boost::multi_index_container;
      //using namespace boost::multi_index;
      /**
       * Explain WTF the object is for.
       */
      class escrow_object : public graphene::db::abstract_object<escrow_object> {
         public:
            static const uint8_t space_id = implementation_ids;
            static const uint8_t type_id  = impl_escrow_object_type;

            uint32_t       escrow_id;
            account_id_type         from;
            account_id_type         to;
            account_id_type         agent;
            time_point_sec expiration;
            asset          balance;
            bool           disputed = false;
      };

      struct by_from_id;
      struct by_to;
      struct by_agent;
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
      ordered_unique< tag< by_to >,
      composite_key< escrow_object,
            member< escrow_object, account_id_type,  &escrow_object::to >,
      member< object, object_id_type, &object::id >
      >
      >,
      ordered_unique< tag< by_agent >,
      composite_key< escrow_object,
            member< escrow_object, account_id_type,  &escrow_object::agent >,
      member< object, object_id_type, &object::id >
      >
      >
      >
      > escrow_object_index_type;


      typedef generic_index< escrow_object, escrow_object_index_type > escrow_index;

   } }

FC_REFLECT_DERIVED( graphene::chain::escrow_object, (graphene::db::object),
(escrow_id)(from)(to)(agent)(expiration)(balance)(disputed) );