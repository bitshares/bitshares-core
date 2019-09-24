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
#include <graphene/wallet/wallet_api_impl.hpp>

namespace graphene { namespace wallet { namespace detail {
   
   signed_transaction wallet_api_impl::update_worker_votes(
      string account,
      worker_vote_delta delta,
      bool broadcast
      )
   {
      account_object acct = get_account( account );

      // you could probably use a faster algorithm for this, but flat_set is fast enough :)
      flat_set< worker_id_type > merged;
      merged.reserve( delta.vote_for.size() + delta.vote_against.size() + delta.vote_abstain.size() );
      for( const worker_id_type& wid : delta.vote_for )
      {
         bool inserted = merged.insert( wid ).second;
         FC_ASSERT( inserted, "worker ${wid} specified multiple times", ("wid", wid) );
      }
      for( const worker_id_type& wid : delta.vote_against )
      {
         bool inserted = merged.insert( wid ).second;
         FC_ASSERT( inserted, "worker ${wid} specified multiple times", ("wid", wid) );
      }
      for( const worker_id_type& wid : delta.vote_abstain )
      {
         bool inserted = merged.insert( wid ).second;
         FC_ASSERT( inserted, "worker ${wid} specified multiple times", ("wid", wid) );
      }

      // should be enforced by FC_ASSERT's above
      assert( merged.size() == delta.vote_for.size() + delta.vote_against.size() + delta.vote_abstain.size() );

      vector< object_id_type > query_ids;
      for( const worker_id_type& wid : merged )
         query_ids.push_back( wid );

      flat_set<vote_id_type> new_votes( acct.options.votes );

      fc::variants objects = _remote_db->get_objects( query_ids, {} );
      for( const variant& obj : objects )
      {
         worker_object wo;
         from_variant( obj, wo, GRAPHENE_MAX_NESTED_OBJECTS );
         new_votes.erase( wo.vote_for );
         new_votes.erase( wo.vote_against );
         if( delta.vote_for.find( wo.id ) != delta.vote_for.end() )
            new_votes.insert( wo.vote_for );
         else if( delta.vote_against.find( wo.id ) != delta.vote_against.end() )
            new_votes.insert( wo.vote_against );
         else
            assert( delta.vote_abstain.find( wo.id ) != delta.vote_abstain.end() );
      }

      account_update_operation update_op;
      update_op.account = acct.id;
      update_op.new_options = acct.options;
      update_op.new_options->votes = new_votes;

      signed_transaction tx;
      tx.operations.push_back( update_op );
      set_operation_fees( tx, _remote_db->get_global_properties().parameters.get_current_fees() );
      tx.validate();

      return sign_transaction( tx, broadcast );
   }

}}} // namespace graphene::wallet::detail