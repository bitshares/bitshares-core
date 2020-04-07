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
   std::string operator()(const graphene::protocol::object_id_type& oid);
   std::string operator()(const graphene::protocol::asset& a);
};

// BLOCK  TRX  OP  VOP
struct operation_printer
{
private:
   std::ostream& out;
   const wallet_api_impl& wallet;
   graphene::protocol::operation_result result;
   graphene::chain::operation_history_object hist;

   std::string fee(const graphene::protocol::asset& a) const;

public:
   operation_printer( std::ostream& out, const wallet_api_impl& wallet, 
         const graphene::chain::operation_history_object& obj )
      : out(out),
        wallet(wallet),
        result(obj.result),
        hist(obj)
   {}
   typedef std::string result_type;

   template<typename T>
   std::string operator()(const T& op)const
   {
      auto a = wallet.get_asset( op.fee.asset_id );
      auto payer = wallet.get_account( op.fee_payer() );

      std::string op_name = fc::get_typename<T>::name();
      if( op_name.find_last_of(':') != std::string::npos )
         op_name.erase(0, op_name.find_last_of(':')+1);
      out << op_name <<" ";
      out << payer.name << " fee: " << a.amount_to_pretty_string( op.fee );
      operation_result_printer rprinter(wallet);
      std::string str_result = result.visit(rprinter);
      if( str_result != "" )
      {
         out << "   result: " << str_result;
      }
      return "";
   }

   std::string operator()(const graphene::protocol::transfer_operation& op)const;
   std::string operator()(const graphene::protocol::transfer_from_blind_operation& op)const;
   std::string operator()(const graphene::protocol::transfer_to_blind_operation& op)const;
   std::string operator()(const graphene::protocol::account_create_operation& op)const;
   std::string operator()(const graphene::protocol::account_update_operation& op)const;
   std::string operator()(const graphene::protocol::asset_create_operation& op)const;
   std::string operator()(const graphene::protocol::htlc_create_operation& op)const;
   std::string operator()(const graphene::protocol::htlc_redeem_operation& op)const;
};

}}} // namespace graphene::wallet::detail
