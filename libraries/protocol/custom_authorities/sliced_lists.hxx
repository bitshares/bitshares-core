/*
 * Copyright (c) 2019 Contributors.
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

#include <graphene/protocol/operations.hpp>

#include <fc/reflect/typelist.hpp>

namespace graphene { namespace protocol {
namespace typelist = fc::typelist;

// To make the build gentler on RAM, break the operation list into several pieces to build over several files
using operation_list_1 = static_variant<typelist::slice<operation::list, 0, 4>>;
using operation_list_2 = static_variant<typelist::slice<operation::list, 5, 9>>;
static_assert(typelist::index_of<operation::list, account_transfer_operation>() == 9,
              "Custom Active Authorities do not support account_transfer_operation");
using operation_list_3 = static_variant<typelist::slice<operation::list, 10, 11>>;
using operation_list_4 = static_variant<typelist::slice<operation::list, 11, 12>>;
using operation_list_5 = static_variant<typelist::slice<operation::list, 12, 15>>;
using operation_list_6 = static_variant<typelist::slice<operation::list, 15, 22>>;
using operation_list_7 = static_variant<typelist::slice<operation::list, 22, 29>>;
using operation_list_8 = static_variant<typelist::slice<operation::list, 29, 31>>;
static_assert(typelist::index_of<operation::list, committee_member_update_global_parameters_operation>() == 31,
              "Custom Active Authorities do not support committee_member_update_global_parameters_operation");
using operation_list_9 = static_variant<typelist::slice<operation::list, 32, 35>>;
using operation_list_10 = static_variant<typelist::slice<operation::list, 35, 40>>;
static_assert(typelist::index_of<operation::list, blind_transfer_operation>() == 40,
              "Custom Active Authorities do not support blind_transfer_operation");
static_assert(typelist::index_of<operation::list, transfer_from_blind_operation>() == 41,
              "Custom Active Authorities do not support transfer_from_blind_operation");
static_assert(typelist::index_of<operation::list, asset_settle_cancel_operation>() == 42,
              "Custom Active Authorities do not support asset_settle_cancel_operation");
using operation_list_11 = static_variant<typelist::builder<>
                                                ::add<asset_claim_fees_operation> // 43
                                                ::add<bid_collateral_operation>   // 45
                                                ::add<asset_claim_pool_operation> // 47
                                                ::add<htlc_create_operation>      // 49
                                                ::add<htlc_redeem_operation>      // 50
                                                ::add<htlc_extend_operation>      // 52
                                                ::finalize>;
static_assert(typelist::index_of<operation::list, fba_distribute_operation>() == 44,
              "Custom Active Authorities do not support fba_distribute_operation");
static_assert(typelist::index_of<operation::list, execute_bid_operation>() == 46,
              "Custom Active Authorities do not support execute_bid_operation");
static_assert(typelist::index_of<operation::list, asset_update_issuer_operation>() == 48,
              "Custom Active Authorities do not support asset_update_issuer_operation");
static_assert(typelist::index_of<operation::list, htlc_redeemed_operation>() == 51,
              "Custom Active Authorities do not support htlc_redeemed_operation");
static_assert(typelist::index_of<operation::list, htlc_refund_operation>() == 53,
              "Custom Active Authorities do not support htlc_refund_operation");
static_assert(typelist::index_of<operation::list, custom_authority_create_operation>() == 54,
              "Custom Active Authorities do not support custom_authority_create_operation");
static_assert(typelist::index_of<operation::list, custom_authority_update_operation>() == 55,
              "Custom Active Authorities do not support custom_authority_update_operation");
static_assert(typelist::index_of<operation::list, custom_authority_delete_operation>() == 56,
              "Custom Active Authorities do not support custom_authority_delete_operation");
using operation_list_12 = static_variant<typelist::slice<operation::list, 57>>;
using unsupported_operations_list = static_variant<account_transfer_operation, // 9
                                                   committee_member_update_global_parameters_operation, // 31
                                                   blind_transfer_operation, // 40
                                                   transfer_from_blind_operation, // 41
                                                   asset_update_issuer_operation,  // 49
                                                   custom_authority_create_operation, // 54
                                                   custom_authority_update_operation, // 55
                                                   custom_authority_delete_operation  // 56
                                                  >;
using virtual_operations_list = static_variant<fill_order_operation,          // 4
                                               asset_settle_cancel_operation, // 42
                                               fba_distribute_operation,      // 44
                                               execute_bid_operation,         // 46
                                               htlc_redeemed_operation,       // 51
                                               htlc_refund_operation          // 53
                                              >;

object_restriction_predicate<operation> get_restriction_predicate_list_1(size_t idx, vector<restriction> rs);
object_restriction_predicate<operation> get_restriction_predicate_list_2(size_t idx, vector<restriction> rs);
object_restriction_predicate<operation> get_restriction_predicate_list_3(size_t idx, vector<restriction> rs);
object_restriction_predicate<operation> get_restriction_predicate_list_4(size_t idx, vector<restriction> rs);
object_restriction_predicate<operation> get_restriction_predicate_list_5(size_t idx, vector<restriction> rs);
object_restriction_predicate<operation> get_restriction_predicate_list_6(size_t idx, vector<restriction> rs);
object_restriction_predicate<operation> get_restriction_predicate_list_7(size_t idx, vector<restriction> rs);
object_restriction_predicate<operation> get_restriction_predicate_list_8(size_t idx, vector<restriction> rs);
object_restriction_predicate<operation> get_restriction_predicate_list_9(size_t idx, vector<restriction> rs);
object_restriction_predicate<operation> get_restriction_predicate_list_10(size_t idx, vector<restriction> rs);
object_restriction_predicate<operation> get_restriction_predicate_list_11(size_t idx, vector<restriction> rs);
object_restriction_predicate<operation> get_restriction_predicate_list_12(size_t idx, vector<restriction> rs);

} } // namespace graphene::protocol
