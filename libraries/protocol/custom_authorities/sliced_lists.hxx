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
using operation_list_2 = static_variant<typelist::slice<operation::list, 5, 7>>;
using operation_list_3 = static_variant<typelist::slice<operation::list, 10, 11>>;
using operation_list_5 = static_variant<typelist::slice<operation::list, 13, 16>>;
using operation_list_6 = static_variant<typelist::slice<operation::list, 19, 22>>;
using operation_list_9 = static_variant<typelist::slice<operation::list, 32, 34>>;
using operation_list_10 = static_variant<typelist::slice<operation::list, 38, 39>>;
using operation_list_11 = static_variant<typelist::builder<>
                                                ::add_list<typelist::slice<operation::list, 49, 51>>
                                                ::add<htlc_extend_operation>      // 52
                                                ::finalize>;
// Note: Since BSIP-40 is not to be enabled on the BitShares Mainnet any time soon,
//       by now, the list of supported operations ends here.
//       These operations are used in unit tests.
//       As of writing, transfer_operation and limit_order_create_operation appeared on the public testnet
//       between block #31808000 and #31811000.
//       Other operations are added to the unsupported_operations_list with a comment "Unsupported".
//       The operations in the unsupported_operations_list without the comment are virtual operations or
//       unimplemented, so should be kept there anyway.
//       This is to reduce the compilation time and the size of binaries.
// TODO support more operations when we decide to continue BSIP-40 development.
using unsupported_operations_list = static_variant<typelist::builder<>
                                                ::add<fill_order_operation>          // 4
                                                ::add_list<typelist::slice<operation::list, 7, 9>> // Unsupported
                                                ::add<account_transfer_operation>    // 9
                                                ::add_list<typelist::slice<operation::list, 11, 13>> // Unsupported
                                                ::add_list<typelist::slice<operation::list, 16, 19>> // Unsupported
                                                ::add_list<typelist::slice<operation::list, 22, 32>> // Unsupported
                                                ::add_list<typelist::slice<operation::list, 34, 38>> // Unsupported
                                                ::add_list<typelist::slice<operation::list, 39, 42>> // Unsupported
                                                ::add<asset_settle_cancel_operation> // 42
                                                ::add_list<typelist::slice<operation::list, 43, 44>> // Unsupported
                                                ::add<fba_distribute_operation>      // 44
                                                ::add_list<typelist::slice<operation::list, 45, 46>> // Unsupported
                                                ::add<execute_bid_operation>         // 46
                                                ::add_list<typelist::slice<operation::list, 47, 49>> // Unsupported
                                                ::add<htlc_redeemed_operation>       // 51
                                                ::add<htlc_refund_operation>         // 53
                                                // New operations are added here
                                                ::add_list<typelist::slice<operation::list, 54>> // Unsupported
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
