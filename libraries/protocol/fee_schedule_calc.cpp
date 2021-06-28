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

#include <graphene/protocol/fee_schedule.hpp>

#include <fc/uint128.hpp>

namespace graphene { namespace protocol {

   struct calc_fee_visitor
   {
      using result_type = uint64_t;

      const fee_schedule& param;
      const int current_op;
      calc_fee_visitor( const fee_schedule& p, const operation& op ):param(p),current_op(op.which())
      { /* Nothing else to do */ }

      template<typename OpType>
      result_type operator()( const OpType& op )const
      {
         try {
            return op.calculate_fee( param.get<OpType>() ).value;
         } catch (fc::assert_exception& e) {
             fee_parameters params;
             params.set_which(current_op);
             auto itr = param.parameters.find(params);
             if( itr != param.parameters.end() )
                params = *itr;
             return op.calculate_fee( params.get<typename OpType::fee_parameters_type>() ).value;
         }
      }
   };

   template<>
   uint64_t calc_fee_visitor::operator()(const htlc_create_operation& op)const
   {
      //TODO: refactor for performance (see https://github.com/bitshares/bitshares-core/issues/2150)
      transfer_operation::fee_parameters_type t;
      if (param.exists<transfer_operation>())
         t = param.get<transfer_operation>();
      return op.calculate_fee( param.get<htlc_create_operation>(), t.price_per_kbyte).value;
   }

   template<>
   uint64_t calc_fee_visitor::operator()(const asset_create_operation& op)const
   {
      //TODO: refactor for performance (see https://github.com/bitshares/bitshares-core/issues/2150)
      optional<uint64_t> sub_asset_creation_fee;
      if( param.exists<account_transfer_operation>() && param.exists<ticket_create_operation>() )
         sub_asset_creation_fee = param.get<account_transfer_operation>().fee;
      asset_create_operation::fee_parameters_type old_asset_creation_fee_params;
      if( param.exists<asset_create_operation>() )
         old_asset_creation_fee_params = param.get<asset_create_operation>();
      return op.calculate_fee( old_asset_creation_fee_params, sub_asset_creation_fee ).value;
   }

   asset fee_schedule::calculate_fee( const operation& op )const
   {
      uint64_t required_fee = op.visit( calc_fee_visitor( *this, op ) );
      if( scale != GRAPHENE_100_PERCENT )
      {
         auto scaled = fc::uint128_t(required_fee) * scale;
         scaled /= GRAPHENE_100_PERCENT;
         FC_ASSERT( scaled <= GRAPHENE_MAX_SHARE_SUPPLY,
                    "Required fee after scaling would exceed maximum possible supply" );
         required_fee = static_cast<uint64_t>(scaled);
      }
      return asset( required_fee );
   }

   asset fee_schedule::calculate_fee( const operation& op, const price& core_exchange_rate )const
   {
      return calculate_fee( op ).multiply_and_round_up( core_exchange_rate );
   }

} } // graphene::protocol
