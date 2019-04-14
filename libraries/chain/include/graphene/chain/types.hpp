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

namespace graphene { namespace chain {

using namespace graphene::protocol;

enum impl_object_type {
    impl_global_property_object_type,
    impl_dynamic_global_property_object_type,
    impl_reserved0_object_type,      // formerly index_meta_object_type, TODO: delete me
    impl_asset_dynamic_data_type,
    impl_asset_bitasset_data_type,
    impl_account_balance_object_type,
    impl_account_statistics_object_type,
    impl_transaction_object_type,
    impl_block_summary_object_type,
    impl_account_transaction_history_object_type,
    impl_blinded_balance_object_type,
    impl_chain_property_object_type,
    impl_witness_schedule_object_type,
    impl_budget_record_object_type,
    impl_special_authority_object_type,
    impl_buyback_object_type,
    impl_fba_accumulator_object_type,
    impl_collateral_bid_object_type
};

using global_property_id_type = object_id<implementation_ids, impl_global_property_object_type>;
using dynamic_global_property_id_type = object_id<implementation_ids, impl_dynamic_global_property_object_type>;
using asset_dynamic_data_id_type = object_id<implementation_ids, impl_asset_dynamic_data_type>;
using asset_bitasset_data_id_type = object_id<implementation_ids, impl_asset_bitasset_data_type>;
using account_balance_id_type = object_id<implementation_ids, impl_account_balance_object_type>;
using account_statistics_id_type = object_id<implementation_ids, impl_account_statistics_object_type>;
using transaction_obj_id_type = object_id<implementation_ids, impl_transaction_object_type>;
using block_summary_id_type = object_id<implementation_ids, impl_block_summary_object_type>;
using account_transaction_history_id_type = object_id<implementation_ids, impl_account_transaction_history_object_type>;
using chain_property_id_type = object_id<implementation_ids, impl_chain_property_object_type>;
using witness_schedule_id_type = object_id<implementation_ids, impl_witness_schedule_object_type>;
using budget_record_id_type = object_id<implementation_ids, impl_budget_record_object_type>;
using blinded_balance_id_type = object_id<implementation_ids, impl_blinded_balance_object_type>;
using special_authority_id_type = object_id<implementation_ids, impl_special_authority_object_type>;
using buyback_id_type = object_id<implementation_ids, impl_buyback_object_type>;
using fba_accumulator_id_type = object_id<implementation_ids, impl_fba_accumulator_object_type>;
using collateral_bid_id_type = object_id<implementation_ids, impl_collateral_bid_object_type>;

} }

FC_REFLECT_ENUM(graphene::chain::impl_object_type,
                (impl_global_property_object_type)
                (impl_dynamic_global_property_object_type)
                (impl_reserved0_object_type)
                (impl_asset_dynamic_data_type)
                (impl_asset_bitasset_data_type)
                (impl_account_balance_object_type)
                (impl_account_statistics_object_type)
                (impl_transaction_object_type)
                (impl_block_summary_object_type)
                (impl_account_transaction_history_object_type)
                (impl_blinded_balance_object_type)
                (impl_chain_property_object_type)
                (impl_witness_schedule_object_type)
                (impl_budget_record_object_type)
                (impl_special_authority_object_type)
                (impl_buyback_object_type)
                (impl_fba_accumulator_object_type)
                (impl_collateral_bid_object_type))

FC_REFLECT_TYPENAME(graphene::chain::global_property_id_type)
FC_REFLECT_TYPENAME(graphene::chain::dynamic_global_property_id_type)
FC_REFLECT_TYPENAME(graphene::chain::asset_dynamic_data_id_type)
FC_REFLECT_TYPENAME(graphene::chain::asset_bitasset_data_id_type)
FC_REFLECT_TYPENAME(graphene::chain::account_balance_id_type)
FC_REFLECT_TYPENAME(graphene::chain::account_statistics_id_type)
FC_REFLECT_TYPENAME(graphene::chain::transaction_obj_id_type)
FC_REFLECT_TYPENAME(graphene::chain::block_summary_id_type)
FC_REFLECT_TYPENAME(graphene::chain::account_transaction_history_id_type)
FC_REFLECT_TYPENAME(graphene::chain::budget_record_id_type)
FC_REFLECT_TYPENAME(graphene::chain::special_authority_id_type)
FC_REFLECT_TYPENAME(graphene::chain::buyback_id_type)
FC_REFLECT_TYPENAME(graphene::chain::fba_accumulator_id_type)
FC_REFLECT_TYPENAME(graphene::chain::collateral_bid_id_type)
