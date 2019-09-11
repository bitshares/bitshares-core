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

vector<object_id_type> custom_generic_evaluator::do_apply(const account_storage_map& op)
{
   auto &index = _db->get_index_type<account_storage_index>().indices().get<by_account_catalog_key>();
   vector<object_id_type> results;

   if (op.extensions.value.remove.valid() && *op.extensions.value.remove)
   {
      for(auto const& row: *op.extensions.value.key_values) {
         auto itr = index.find(make_tuple(_account, *op.extensions.value.catalog, row.first));
         if(itr != index.end()) {
            results.push_back(itr->id);
            _db->remove(*itr);
         }
      }
   }
   else {
      for(auto const& row: *op.extensions.value.key_values) {
         if(row.first.length() > CUSTOM_OPERATIONS_MAX_KEY_SIZE)
         {
            dlog("Key can't be bigger than ${max} characters", ("max", CUSTOM_OPERATIONS_MAX_KEY_SIZE));
            continue;
         }
         auto itr = index.find(make_tuple(_account, *op.extensions.value.catalog, row.first));
         if(itr == index.end())
         {
            try {
               auto created = _db->create<account_storage_object>( [&op, this, &row]( account_storage_object& aso ) {
                  aso.catalog = *op.extensions.value.catalog;
                  aso.account = _account;
                  aso.key = row.first;
                  aso.value = fc::json::from_string(row.second);
               });
               results.push_back(created.id);
            }
            catch(const fc::parse_error_exception& e) { dlog(e.to_detail_string()); }
         }
         else
         {
            try {
               _db->modify(*itr, [&op, this, &row](account_storage_object &aso) {
                  aso.value = fc::json::from_string(row.second);
               });
               results.push_back(itr->id);
            }
            catch(const fc::parse_error_exception& e) { dlog((e.to_detail_string())); }
         }
      }
   }
   return results;
}
vector<object_id_type> custom_generic_evaluator::do_apply(const account_storage_list& op)
{
   auto &index = _db->get_index_type<account_storage_index>().indices().get<by_account_catalog_key>();
   vector<object_id_type> results;

   if (op.extensions.value.remove.valid() && *op.extensions.value.remove)
   {
      for(auto const& list_value: *op.extensions.value.values) {

         auto itr = index.find(make_tuple(_account, *op.extensions.value.catalog, list_value));
         if(itr != index.end()) {
            results.push_back(itr->id);
            _db->remove(*itr);
         }
      }
   }
   else {
      for(auto const& list_value: *op.extensions.value.values) {
         if(list_value.length() > 200)
         {
            dlog("List value can't be bigger than ${max} characters", ("max", CUSTOM_OPERATIONS_MAX_KEY_SIZE));
            continue;
         }
         auto itr = index.find(make_tuple(_account, *op.extensions.value.catalog, list_value));
         if(itr == index.end())
         {
            auto created = _db->create<account_storage_object>([&op, this, &list_value](account_storage_object &aso) {
               aso.catalog = *op.extensions.value.catalog;
               aso.account = _account;
               aso.key = list_value;
            });
            results.push_back(itr->id);
         }
      }
   }
   return results;
}

} }
