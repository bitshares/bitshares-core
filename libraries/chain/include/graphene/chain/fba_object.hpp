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

#include <graphene/chain/types.hpp>
#include <graphene/chain/stored_value.hpp>
#include <graphene/db/generic_index.hpp>

namespace graphene { namespace chain {

class database;

/**
 * fba_accumulator_object accumulates fees to be paid out via buyback or other FBA mechanism.
 */
class fba_accumulator_object;
class fba_accumulator_master
   : public graphene::db::abstract_object< fba_accumulator_master, fba_accumulator_object >
{
   public:
      static constexpr uint8_t space_id = implementation_ids;
      static constexpr uint8_t type_id = impl_fba_accumulator_object_type;

      optional< asset_id_type > designated_asset;

      bool is_configured( const database& db )const;
};

class fba_accumulator_object : public fba_accumulator_master
{
   public:
      static const uint8_t space_id = implementation_ids;
      static const uint8_t type_id = impl_fba_accumulator_object_type;

      stored_value accumulated_fba_fees;

      protected:
         virtual unique_ptr<graphene::db::object> backup()const;
         virtual void restore( graphene::db::object& obj );
         virtual void clear();
};

} } // graphene::chain

MAP_OBJECT_ID_TO_TYPE(graphene::chain::fba_accumulator_object)

FC_REFLECT_DERIVED( graphene::chain::fba_accumulator_master, (graphene::db::object), (designated_asset) )
FC_REFLECT_TYPENAME( graphene::chain::fba_accumulator_object )

GRAPHENE_DECLARE_EXTERNAL_SERIALIZATION( graphene::chain::fba_accumulator_master )
GRAPHENE_DECLARE_EXTERNAL_SERIALIZATION( graphene::chain::fba_accumulator_object )
