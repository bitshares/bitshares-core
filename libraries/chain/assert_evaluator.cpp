/*
 * Copyright (c) 2015, Cryptonomex, Inc.
 * All rights reserved.
 *
 * This source code is provided for evaluation in private test networks only, until September 8, 2015. After this date, this license expires and
 * the code may not be used, modified or distributed for any purpose. Redistribution and use in source and binary forms, with or without modification,
 * are permitted until September 8, 2015, provided that the following conditions are met:
 *
 * 1. The code and/or derivative works are used only for private test networks consisting of no more than 10 P2P nodes.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <graphene/chain/assert_evaluator.hpp>
#include <graphene/chain/database.hpp>
#include <graphene/chain/predicate.hpp>

#include <sstream>

namespace graphene { namespace chain {

namespace detail {

struct predicate_check_visitor
{
   predicate_check_visitor( const database& d ): _db(d){}

   typedef bool result_type;
   template<typename Predicate> bool operator()( const Predicate& pred )const
   {
      return pred.check_predicate( _db );
   }

   const database& _db;
};

} // graphene::chain::detail

void_result assert_evaluator::do_evaluate( const assert_operation& o )
{
   const database& _db = db();
   uint32_t skip = _db.get_node_properties().skip_flags;
   // TODO:  Skip flags
   if( skip & database::skip_assert_evaluation )
      return void_result();
   for( const vector<char>& s_pred : o.predicates )
   {
      std::istringstream is( string( s_pred.begin(), s_pred.end() ) );
      // de-serialize just the static_variant tag
      unsigned_int t;
      fc::raw::unpack( is, t );
      // everyone checks: delegates must have allocated an opcode for it
      FC_ASSERT( t < _db.get_global_properties().parameters.max_predicate_opcode );
      if( t >= predicate::count() )
      {
         //
         // delegates allocated an opcode, but our client doesn't know
         //  the semantics (i.e. we are running an old client)
         //
         // skip_unknown_predicate indicates we're cool with assuming
         //  unknown predicates pass
         //
         if( skip & database::skip_unknown_predicate )
            continue;
         //
         // ok, unknown predicate must die
         //
         FC_ASSERT( false, "unknown predicate" );
      }
      // rewind to beginning, unpack it, and check it
      is.clear();
      is.seekg(0);
      predicate pred;
      fc::raw::unpack( is, pred );
      bool pred_passed = pred.visit( detail::predicate_check_visitor( _db ) );
      FC_ASSERT( pred_passed );
   }
   return void_result();
}

void_result assert_evaluator::do_apply( const assert_operation& o )
{
   // assert_operation is always a no-op
   return void_result();
}

} } // graphene::chain
