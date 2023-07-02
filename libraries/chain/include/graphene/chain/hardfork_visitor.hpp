/*
 * Copyright (c) 2019 Contributors
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

#include <graphene/protocol/operations.hpp>

#include <graphene/chain/hardfork.hpp>

#include <fc/reflect/typelist.hpp>

#include <type_traits>
#include <functional>

namespace graphene { namespace chain {

/**
 * @brief The hardfork_visitor struct checks whether a given operation type has been hardforked in or not
 *
 * This visitor can be invoked in several different ways, including operation::visit, typelist::runtime::dispatch, or
 * direct invocation by calling the visit() method passing an operation variant, narrow operation type, operation tag,
 * or templating on the narrow operation type
 */
struct hardfork_visitor {
   using result_type = bool;
   using first_unforked_op = protocol::custom_authority_create_operation;
   using BSIP_40_ops = fc::typelist::list< protocol::custom_authority_create_operation,
                                           protocol::custom_authority_update_operation,
                                           protocol::custom_authority_delete_operation>;
   using hf1604_ops = fc::typelist::list< protocol::limit_order_update_operation>;
   using hf2103_ops = fc::typelist::list< protocol::ticket_create_operation,
                                          protocol::ticket_update_operation>;
   using liquidity_pool_ops = fc::typelist::list< protocol::liquidity_pool_create_operation,
                                                  protocol::liquidity_pool_delete_operation,
                                                  protocol::liquidity_pool_deposit_operation,
                                                  protocol::liquidity_pool_withdraw_operation,
                                                  protocol::liquidity_pool_exchange_operation >;
   using liquidity_pool_update_op = fc::typelist::list< protocol::liquidity_pool_update_operation >;
   using samet_fund_ops = fc::typelist::list< protocol::samet_fund_create_operation,
                                              protocol::samet_fund_delete_operation,
                                              protocol::samet_fund_update_operation,
                                              protocol::samet_fund_borrow_operation,
                                              protocol::samet_fund_repay_operation >;
   using credit_offer_ops = fc::typelist::list< protocol::credit_offer_create_operation,
                                                protocol::credit_offer_delete_operation,
                                                protocol::credit_offer_update_operation,
                                                protocol::credit_offer_accept_operation,
                                                protocol::credit_deal_repay_operation,
                                                protocol::credit_deal_expired_operation >;
   using credit_deal_update_op = fc::typelist::list< protocol::credit_deal_update_operation >;

   fc::time_point_sec now;

   /// @note using head block time for all operations
   explicit hardfork_visitor(const fc::time_point_sec& head_block_time) : now(head_block_time) {}

   /// The real visitor implementations. Future operation types get added in here.
   /// @{
   template<typename Op>
   std::enable_if_t<operation::tag<Op>::value < protocol::operation::tag<first_unforked_op>::value, bool>
   visit() { return true; }
   template<typename Op>
   std::enable_if_t<fc::typelist::contains<BSIP_40_ops, Op>(), bool>
   visit() { return HARDFORK_BSIP_40_PASSED(now); }
   template<typename Op>
   std::enable_if_t<fc::typelist::contains<hf1604_ops, Op>(), bool>
   visit() { return HARDFORK_CORE_1604_PASSED(now); }
   template<typename Op>
   std::enable_if_t<fc::typelist::contains<hf2103_ops, Op>(), bool>
   visit() { return HARDFORK_CORE_2103_PASSED(now); }
   template<typename Op>
   std::enable_if_t<fc::typelist::contains<liquidity_pool_ops, Op>(), bool>
   visit() { return HARDFORK_LIQUIDITY_POOL_PASSED(now); }
   template<typename Op>
   std::enable_if_t<fc::typelist::contains<samet_fund_ops, Op>(), bool>
   visit() { return HARDFORK_CORE_2351_PASSED(now); }
   template<typename Op>
   std::enable_if_t<fc::typelist::contains<credit_offer_ops, Op>(), bool>
   visit() { return HARDFORK_CORE_2362_PASSED(now); }
   template<typename Op>
   std::enable_if_t<fc::typelist::contains<credit_deal_update_op, Op>(), bool>
   visit() { return HARDFORK_CORE_2595_PASSED(now); }
   template<typename Op>
   std::enable_if_t<fc::typelist::contains<liquidity_pool_update_op, Op>(), bool>
   visit() { return HARDFORK_CORE_2604_PASSED(now); }
   /// @}

   /// typelist::runtime::dispatch adaptor
   template<class W, class Op=typename W::type>
   std::enable_if_t<fc::typelist::contains<protocol::operation::list, Op>(), bool>
   operator()(W) { return visit<Op>(); }
   /// static_variant::visit adaptor
   template<class Op>
   std::enable_if_t<fc::typelist::contains<protocol::operation::list, Op>(), bool>
   operator()(const Op&) { return visit<Op>(); }
   /// Tag adaptor
   bool visit(protocol::operation::tag_type tag) const {
      return fc::typelist::runtime::dispatch(protocol::operation::list(), (size_t)tag, *this);
   }
   /// operation adaptor
   bool visit(const protocol::operation& op) const {
      return visit(op.which());
   }
};

} } // namespace graphene::chain
