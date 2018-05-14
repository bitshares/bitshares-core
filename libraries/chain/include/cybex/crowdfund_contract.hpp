/*
 * Copyright (c) 2015 Cryptonomex, Inc., and contributors.
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

#include <graphene/chain/protocol/base.hpp>

namespace graphene { namespace chain {
   enum crowdfund_contract_state{ 
	   CROWDFUND_STATE_INACTIVE,
	   CROWDFUND_STATE_ACTIVE,
	   CROWDFUND_STATE_PERM,
	   CROWDFUND_STATE_USED,
	   CROWDFUND_STATE_ENDED
   };

   class crowdfund_contract_object : public abstract_object<crowdfund_contract_object>
   {
      public:
         static const uint8_t space_id = protocol_ids;
         static const uint8_t type_id  = crowdfund_contract_object_type;
          

         account_id_type owner;
         crowdfund_id_type   crowdfund;
         share_type valuation;
         share_type cap;
         fc::time_point_sec when; 
         //address    A;
         int        state;// used?
         crowdfund_id_type crowdfund_type()const { return crowdfund; }
   };

   struct by_owner;
   struct by_crowdfund;

   /**
    * @ingroup object_index
    */
   using crowdfund_contract_multi_index_type = multi_index_container<
      crowdfund_contract_object,
      indexed_by<
         ordered_unique< tag<by_id>, member< object, object_id_type, &object::id > >,
         ordered_non_unique< tag<by_owner>, composite_key<
            crowdfund_contract_object,
            member<crowdfund_contract_object, account_id_type, &crowdfund_contract_object::owner>,
            const_mem_fun<crowdfund_contract_object, crowdfund_id_type, &crowdfund_contract_object::crowdfund_type>
         > >,
         ordered_non_unique< tag<by_crowdfund>, composite_key< 
            crowdfund_contract_object,
            const_mem_fun<crowdfund_contract_object, crowdfund_id_type, &crowdfund_contract_object::crowdfund_type>,
            member<crowdfund_contract_object, share_type, &crowdfund_contract_object::cap>
         > >

      >
   >;

   /**
    * @ingroup object_index
    */
   using crowdfund_contract_index = generic_index<crowdfund_contract_object, crowdfund_contract_multi_index_type>;
} }

FC_REFLECT_DERIVED( graphene::chain::crowdfund_contract_object, (graphene::db::object),
                    (owner)(crowdfund)(valuation)(cap)(when)(state) )

FC_REFLECT_ENUM(graphene::chain::crowdfund_contract_state,
                    (CROWDFUND_STATE_INACTIVE)(CROWDFUND_STATE_ACTIVE)
                    (CROWDFUND_STATE_PERM)(CROWDFUND_STATE_USED)(CROWDFUND_STATE_ENDED))
                         
