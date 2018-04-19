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
#include <graphene/chain/protocol/escrow.hpp>

namespace graphene { namespace chain {

      void escrow_transfer_operation::validate()const {
         FC_ASSERT( agent_fee.amount >= 0 );
         FC_ASSERT( amount.amount > 0 );
         FC_ASSERT( from != to );
         FC_ASSERT( from != agent && to != agent );
         FC_ASSERT( agent_fee.asset_id == amount.asset_id ); // agent fee only in bts
         FC_ASSERT( amount.asset_id == asset_id_type()); // only bts is allowed by now
      }
      void escrow_approve_operation::validate()const {
         FC_ASSERT( who == to || who == agent );
      }
      void escrow_dispute_operation::validate()const {
         FC_ASSERT( who == from || who == to );
      }
      void escrow_release_operation::validate()const {
         FC_ASSERT( who != to );
         FC_ASSERT( amount.amount > 0 );
         FC_ASSERT( amount.asset_id == asset_id_type()); // only bts is allowed by now
      }
   } }
