/*
 * Copyright (c) 2019 Abit More, and contributors.
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

#include <graphene/app/plugin.hpp>

#include <graphene/protocol/types.hpp>

#include <boost/multi_index/composite_key.hpp>

namespace graphene { namespace plugins { namespace txid {

using namespace boost::multi_index;

using namespace graphene::db;
using namespace graphene::protocol;

//
// Plugins should #define their SPACE_ID's so plugins with
// conflicting SPACE_ID assignments can be compiled into the
// same binary (by simply re-assigning some of the conflicting #defined
// SPACE_ID's in a build script).
//
// Assignment of SPACE_ID's cannot be done at run-time because
// various template automagic depends on them being known at compile
// time.
//
#ifndef TXID_PLUGIN_SPACE_ID
#define TXID_PLUGIN_SPACE_ID 8
#endif

enum txid_plugin_object_type
{
   transaction_position_object_type = 0
};

/// @brief This data structure indicates where a transaction is included in the blockchain
struct transaction_position_object : public abstract_object<transaction_position_object>
{
   static const uint8_t space_id = TXID_PLUGIN_SPACE_ID;
   static const uint8_t type_id  = transaction_position_object_type;

   /// The hash of the transaction
   transaction_id_type trx_id;
   /// The number (height) of the block that includes the transaction
   uint32_t            block_num = 0;
   /// The index (sequence number) of the transaction in the block, starts from 0
   uint16_t            trx_in_block = 0;
};


struct by_txid;
struct by_block;

typedef multi_index_container<
   transaction_position_object,
   indexed_by<
      ordered_unique< tag<by_id>, member< object, object_id_type, &object::id > >,
      ordered_unique< tag<by_txid>,
         member< transaction_position_object, transaction_id_type, &transaction_position_object::trx_id > >,
      ordered_unique< tag<by_block>,
         composite_key< transaction_position_object,
            member< transaction_position_object, uint32_t, &transaction_position_object::block_num >,
            member< transaction_position_object, uint16_t, &transaction_position_object::trx_in_block >
         >
      >
   >
> transaction_position_multi_index_type;

typedef generic_index<transaction_position_object, transaction_position_multi_index_type> transaction_position_index;


namespace detail
{
    class txid_plugin_impl;
}

class txid_plugin : public graphene::app::plugin
{
   public:
      txid_plugin();
      virtual ~txid_plugin();

      std::string plugin_name()const override;
      std::string plugin_description()const override;
      virtual void plugin_set_program_options(
         boost::program_options::options_description& cli,
         boost::program_options::options_description& cfg) override;
      virtual void plugin_initialize(const boost::program_options::variables_map& options) override;
      virtual void plugin_startup() override;

      friend class detail::txid_plugin_impl;
      std::unique_ptr<detail::txid_plugin_impl> my;
};

} } } //graphene::plugins::txid

FC_REFLECT_DERIVED( graphene::plugins::txid::transaction_position_object,
                    (graphene::db::object),
                    (trx_id)(block_num)(trx_in_block) )
