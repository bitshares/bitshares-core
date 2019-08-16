/*
 * Copyright (c) 2019 oxarbitrage and contributors.
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
#include <graphene/chain/database.hpp>

#include <graphene/custom_operations/custom_operations_plugin.hpp>
#include <graphene/custom_operations/custom_objects.hpp>
#include <graphene/custom_operations/custom_evaluators.hpp>

namespace graphene { namespace custom_operations {

custom_generic_evaluator::custom_generic_evaluator(database& db, const account_id_type account)
{
   _db = &db;
   _account = account;
}

object_id_type custom_generic_evaluator::do_apply(const account_contact_operation& op)
{
   auto &index = _db->get_index_type<account_contact_index>().indices().get<by_custom_account>();

   auto itr = index.find(_account);
   if( itr != index.end() )
   {
      _db->modify( *itr, [&op, this]( account_contact_object& aco ){
         aco.account = _account;
         if(op.extensions.value.name.valid()) aco.name = *op.extensions.value.name;
         if(op.extensions.value.email.valid()) aco.email = *op.extensions.value.email;
         if(op.extensions.value.phone.valid()) aco.phone = *op.extensions.value.phone;
         if(op.extensions.value.address.valid()) aco.address = *op.extensions.value.address;
         if(op.extensions.value.company.valid()) aco.company = *op.extensions.value.company;
         if(op.extensions.value.url.valid()) aco.url = *op.extensions.value.url;
      });
      return itr->id;
   }
   else
   {
      auto created = _db->create<account_contact_object>( [&op, this]( account_contact_object& aco ) {
         aco.account = _account;
         if(op.extensions.value.name.valid()) aco.name = *op.extensions.value.name;
         if(op.extensions.value.email.valid()) aco.email = *op.extensions.value.email;
         if(op.extensions.value.phone.valid()) aco.phone = *op.extensions.value.phone;
         if(op.extensions.value.address.valid()) aco.address = *op.extensions.value.address;
         if(op.extensions.value.company.valid()) aco.company = *op.extensions.value.company;
         if(op.extensions.value.url.valid()) aco.url = *op.extensions.value.url;
      });
      return created.id;
   }
}

object_id_type custom_generic_evaluator::do_apply(const create_htlc_order_operation& op)
{
   FC_ASSERT(*op.extensions.value.expiration > _db->head_block_time() + fc::seconds(3600));

   auto order_time = _db->head_block_time();
   auto created = _db->create<htlc_order_object>( [&op, &order_time, this]( htlc_order_object& hoo ) {
      hoo.bitshares_account = _account;
      if(op.extensions.value.bitshares_amount.valid()) hoo.bitshares_amount = *op.extensions.value.bitshares_amount;
      if(op.extensions.value.blockchain.valid()) hoo.blockchain = *op.extensions.value.blockchain;
      if(op.extensions.value.blockchain_account.valid()) hoo.blockchain_account = *op.extensions.value.blockchain_account;
      if(op.extensions.value.blockchain_asset.valid()) hoo.blockchain_asset = *op.extensions.value.blockchain_asset;
      if(op.extensions.value.blockchain_asset_precision.valid()) hoo.blockchain_asset_precision =
            *op.extensions.value.blockchain_asset_precision;
      if(op.extensions.value.blockchain_amount.valid()) hoo.blockchain_amount = *op.extensions.value.blockchain_amount;
      if(op.extensions.value.expiration.valid()) hoo.expiration = *op.extensions.value.expiration;
      if(op.extensions.value.token_contract.valid()) hoo.token_contract = *op.extensions.value.token_contract;
      if(op.extensions.value.tag.valid()) hoo.tag = *op.extensions.value.tag;

      hoo.order_time = order_time;
      hoo.active = true;
   });
   return created.id;
}

object_id_type custom_generic_evaluator::do_apply(const take_htlc_order_operation& op)
{
   auto &index = _db->get_index_type<htlc_orderbook_index>().indices().get<by_custom_id>();
   htlc_order_id_type htlc_order_id;

   if(op.extensions.value.htlc_order_id.valid()) {
      htlc_order_id = *op.extensions.value.htlc_order_id;
      auto itr = index.find(htlc_order_id);
      if (itr != index.end()) {
         auto close_time = _db->head_block_time();
         _db->modify(*itr, [&op, &close_time, this](htlc_order_object &hoo) {
            hoo.active = false;
            hoo.taker_bitshares_account = _account;
            if (op.extensions.value.blockchain_account.valid())
               hoo.taker_blockchain_account = op.extensions.value.blockchain_account;
            hoo.close_time = close_time;
         });
      }
   }
   return htlc_order_id;
}

} }
