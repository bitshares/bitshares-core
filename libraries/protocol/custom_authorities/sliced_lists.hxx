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
using operation_list_1 = static_variant<typelist::builder<>
                                                ::add<transfer_operation>            // 0
                                                ::add<limit_order_create_operation>  // 1
                                                ::add<limit_order_cancel_operation>  // 2
                                                ::add<call_order_update_operation>   // 3
                                                ::finalize>;
using operation_list_2 = static_variant<typelist::builder<>
                                                ::add<account_create_operation>      // 5
                                                ::add<account_update_operation>      // 6
                                                ::finalize>;
using operation_list_3 = static_variant<typelist::builder<>
                                                ::add<asset_create_operation>        // 10
                                                ::finalize>;
using operation_list_5 = static_variant<typelist::builder<>
                                                ::add<asset_update_feed_producers_operation> // 13
                                                ::add<asset_issue_operation>                 // 14
                                                ::add<asset_reserve_operation>               // 15
                                                ::finalize>;
using operation_list_6 = static_variant<typelist::builder<>
                                                ::add<asset_publish_feed_operation>          // 19
                                                ::add<witness_update_operation>              // 21
                                                ::finalize>;
using operation_list_9 = static_variant<typelist::builder<>
                                                ::add<vesting_balance_create_operation>      // 32
                                                ::add<vesting_balance_withdraw_operation>    // 33
                                                ::finalize>;
using operation_list_10 = static_variant<typelist::builder<>
                                                ::add<override_transfer_operation>   // 38
                                                ::finalize>;
using operation_list_11 = static_variant<typelist::builder<>
                                                ::add<htlc_create_operation>         // 49
                                                ::add<htlc_redeem_operation>         // 50
                                                ::add<htlc_extend_operation>         // 52
                                                ::finalize>;
// Note: Since BSIP-40 is not to be enabled on the BitShares Mainnet any time soon,
//       by now, the list of supported operations ends here.
//       These operations are used in unit tests.
//       As of writing, transfer_operation and limit_order_create_operation appeared on the public testnet
//       between block #31808000 and #31811000.
//       Other operations are added to the unsupported_operations_list with a comment "Unsupported".
//       The operations in the unsupported_operations_list with other comments are virtual operations or
//       unimplemented, so should be kept there anyway.
//       This is to reduce the compilation time and the size of binaries.
// TODO support more operations when we decide to continue BSIP-40 development.
using unsupported_operations_list = static_variant<typelist::builder<>
                                                ::add<fill_order_operation>            // 4  // VIRTUAL
                                                ::add<account_whitelist_operation>     // 7  // Unsupported
                                                ::add<account_upgrade_operation>       // 8  // Unsupported
                                                ::add<account_transfer_operation>      // 9  // Unimplemented
                                                ::add<asset_update_operation>          // 11 // Unsupported
                                                ::add<asset_update_bitasset_operation> // 12 // Unsupported
                                                ::add<asset_fund_fee_pool_operation>   // 16 // Unsupported
                                                ::add<asset_settle_operation>          // 17 // Unsupported
                                                ::add<asset_global_settle_operation>   // 18 // Unsupported
                                                ::add<witness_create_operation>        // 20 // Unsupported
                                                // [22, 32) // Unsupported
                                                ::add_list<typelist::slice<operation::list,
                                                      typelist::index_of< operation::list,
                                                                          proposal_create_operation >(),
                                                      typelist::index_of< operation::list,
                                                                          vesting_balance_create_operation >() >>
                                                ::add<worker_create_operation>       // 34 // Unsupported
                                                ::add<custom_operation>              // 35 // Unsupported
                                                ::add<assert_operation>              // 36 // Unsupported
                                                ::add<balance_claim_operation>       // 37 // Unsupported
                                                ::add<transfer_to_blind_operation>   // 39 // Unsupported
                                                ::add<blind_transfer_operation>      // 40 // Unsupported
                                                ::add<transfer_from_blind_operation> // 41 // Unsupported
                                                ::add<asset_settle_cancel_operation> // 42 // VIRTUAL
                                                ::add<asset_claim_fees_operation>    // 43 // Unsupported
                                                ::add<fba_distribute_operation>      // 44 // VIRTUAL
                                                ::add<bid_collateral_operation>      // 45 // Unsupported
                                                ::add<execute_bid_operation>         // 46 // VIRTUAL
                                                ::add<asset_claim_pool_operation>    // 47 // Unsupported
                                                ::add<asset_update_issuer_operation> // 48 // Unsupported
                                                ::add<htlc_redeemed_operation>       // 51 // VIRTUAL
                                                ::add<htlc_refund_operation>         // 53 // VIRTUAL
                                                // New operations are added here // Unsupported
                                                ::add_list<typelist::slice<operation::list,
                                                      typelist::index_of< operation::list,
                                                                          custom_authority_create_operation >() >>
                                                ::finalize>;

object_restriction_predicate<operation> get_restriction_pred_list_1(size_t idx, vector<restriction> rs);
object_restriction_predicate<operation> get_restriction_pred_list_2(size_t idx, vector<restriction> rs);
object_restriction_predicate<operation> get_restriction_pred_list_3(size_t idx, vector<restriction> rs);
object_restriction_predicate<operation> get_restriction_pred_list_5(size_t idx, vector<restriction> rs);
object_restriction_predicate<operation> get_restriction_pred_list_6(size_t idx, vector<restriction> rs);
object_restriction_predicate<operation> get_restriction_pred_list_9(size_t idx, vector<restriction> rs);
object_restriction_predicate<operation> get_restriction_pred_list_10(size_t idx, vector<restriction> rs);
object_restriction_predicate<operation> get_restriction_pred_list_11(size_t idx, vector<restriction> rs);

} } // namespace graphene::protocol
