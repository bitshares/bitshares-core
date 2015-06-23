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

struct predicate_visitor 
{
   typedef bool result_type;
   const database& db;

   predicate_visitor( const database& d ):db(d){}
   
   bool operator()( const verify_account_name& p )const
   {
      return p.account_id(db).name == p.account_name;
   }
   bool operator()( const verify_symbol& p )const
   {
      return p.asset_id(db).symbol == p.symbol;
   }
};

void_result assert_evaluator::do_evaluate( const assert_operation& o )
{
   const database& _db = db();
   uint32_t skip = _db.get_node_properties().skip_flags;

   if( skip & database::skip_assert_evaluation )
      return void_result();

   for( const vector<char>& pdata : o.predicates )
   {
      fc::datastream<const char*> ds( pdata.data(), pdata.size() );
      predicate p;
      try {
         fc::raw::unpack( ds, p );
      } catch ( const fc::exception& e )
      {
         if( skip & database::skip_unknown_predicate )
            continue;
         throw;
      }
      FC_ASSERT( p.visit( predicate_visitor( _db ) ) );
   }
   return void_result();
}

void_result assert_evaluator::do_apply( const assert_operation& o )
{
   // assert_operation is always a no-op
   return void_result();
}

} } // graphene::chain
