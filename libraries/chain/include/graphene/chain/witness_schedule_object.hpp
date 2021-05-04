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
#include <graphene/protocol/types.hpp>
#include <graphene/db/generic_index.hpp>

namespace graphene { namespace chain {

class witness_schedule_object : public graphene::db::abstract_object<witness_schedule_object>
{
   public:
      static constexpr uint8_t space_id = implementation_ids;
      static constexpr uint8_t type_id = impl_witness_schedule_object_type;

      vector< witness_id_type > current_shuffled_witnesses;
};

} }

MAP_OBJECT_ID_TO_TYPE(graphene::chain::witness_schedule_object)

FC_REFLECT_TYPENAME( graphene::chain::witness_schedule_object )

GRAPHENE_DECLARE_EXTERNAL_SERIALIZATION( graphene::chain::witness_schedule_object )
