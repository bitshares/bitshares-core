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

#include <fc/io/raw.hpp>
#include <fc/uint128.hpp>

#define MAX_FEE_STABILIZATION_ITERATION 4

namespace graphene { namespace protocol {

   struct calc_fee_visitor
   {
      typedef uint64_t result_type;

      const fee_schedule& param;
      const int current_op;
      calc_fee_visitor( const fee_schedule& p, const operation& op ):param(p),current_op(op.which()){}

      template<typename OpType>
      result_type operator()( const OpType& op )const
      {
         try {
            return op.calculate_fee( param.get<OpType>() ).value;
         } catch (fc::assert_exception& e) {
             fee_parameters params; params.set_which(current_op);
             auto itr = param.parameters.find(params);
             if( itr != param.parameters.end() ) params = *itr;
             return op.calculate_fee( params.get<typename OpType::fee_parameters_type>() ).value;
         }
      }
   };

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

GRAPHENE_IMPLEMENT_EXTERNAL_SERIALIZATION( graphene::protocol::fee_schedule )
