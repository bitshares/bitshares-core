/*
 * Copyright (c) 2017 Cryptonomex, Inc., and contributors.
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

#include <graphene/app/api_objects.hpp>
#include <graphene/protocol/base.hpp>
#include <graphene/protocol/asset.hpp>
#include <graphene/protocol/operations.hpp>
#include <graphene/chain/operation_history_object.hpp>

#include <string>
#include <ostream>

#include "wallet_api_impl.hpp"

namespace graphene { namespace wallet { namespace detail {

struct operation_result_printer
{
public:
   explicit operation_result_printer( const wallet_api_impl& w )
      : _wallet(w) {}
   const wallet_api_impl& _wallet;
   typedef std::string result_type;

   std::string operator()(const graphene::protocol::void_result& x) const;
   std::string operator()(const graphene::protocol::object_id_type& oid) const;
   std::string operator()(const graphene::protocol::asset& a) const;
   std::string operator()(const graphene::protocol::generic_operation_result& r) const;
   std::string operator()(const graphene::protocol::generic_exchange_operation_result& r) const;
   std::string operator()(const graphene::protocol::extendable_operation_result& r) const;
};

// BLOCK  TRX  OP  VOP
struct operation_printer
{
private:
   std::ostream& out;
   const wallet_api_impl& wallet;
   graphene::protocol::operation_result result;
   graphene::chain::operation_history_object hist;

   std::string format_asset(const graphene::protocol::asset& a) const;
   void print_fee(const graphene::protocol::asset& a) const;

public:
   operation_printer( std::ostream& out, const wallet_api_impl& wallet,
         const graphene::chain::operation_history_object& obj )
      : out(out),
        wallet(wallet),
        result(obj.result),
        hist(obj)
   {}

   /// Return the decrypted memo if a memo exists, otherwise return an empty string
   typedef std::string result_type;

   template<typename T>
   std::string operator()(const T& op)const
   {
      auto payer = wallet.get_account( op.fee_payer() );

      std::string op_name = fc::get_typename<T>::name();
      if( op_name.find_last_of(':') != std::string::npos )
         op_name.erase(0, op_name.find_last_of(':')+1);
      out << op_name << " ";
      out << payer.name;
      print_fee( op.fee );
      print_result();
      return "";
   }

   std::string operator()(const graphene::protocol::transfer_operation& op)const;
   std::string operator()(const graphene::protocol::override_transfer_operation& op)const;
   std::string operator()(const graphene::protocol::transfer_from_blind_operation& op)const;
   std::string operator()(const graphene::protocol::transfer_to_blind_operation& op)const;
   std::string operator()(const graphene::protocol::account_create_operation& op)const;
   std::string operator()(const graphene::protocol::account_update_operation& op)const;
   std::string operator()(const graphene::protocol::asset_create_operation& op)const;
   std::string operator()(const graphene::protocol::asset_update_operation& op)const;
   std::string operator()(const graphene::protocol::asset_update_bitasset_operation& op)const;
   std::string operator()(const graphene::protocol::asset_update_feed_producers_operation& op)const;
   std::string operator()(const graphene::protocol::asset_publish_feed_operation& op)const;
   std::string operator()(const graphene::protocol::asset_fund_fee_pool_operation& op)const;
   std::string operator()(const graphene::protocol::asset_claim_pool_operation& op)const;
   std::string operator()(const graphene::protocol::asset_issue_operation& op)const;
   std::string operator()(const graphene::protocol::asset_reserve_operation& op)const;
   std::string operator()(const graphene::protocol::asset_settle_operation& op)const;
   std::string operator()(const graphene::protocol::call_order_update_operation& op)const;
   std::string operator()(const graphene::protocol::limit_order_create_operation& op)const;
   std::string operator()(const graphene::protocol::limit_order_cancel_operation& op)const;
   std::string operator()(const graphene::protocol::fill_order_operation& op)const;
   std::string operator()(const graphene::protocol::proposal_update_operation& op)const;
   std::string operator()(const graphene::protocol::htlc_create_operation& op)const;
   std::string operator()(const graphene::protocol::htlc_redeem_operation& op)const;
   std::string operator()(const graphene::protocol::htlc_redeemed_operation& op)const;

protected:
   std::string print_memo( const fc::optional<graphene::protocol::memo_data>& memo)const;
   void print_preimage( const std::vector<char>& preimage)const;
   void print_redeem(const graphene::protocol::htlc_id_type& id,
         const std::string& redeemer, const std::vector<char>& preimage,
         const graphene::protocol::asset& op_fee)const;
   void print_result()const;
};

}}} // namespace graphene::wallet::detail
