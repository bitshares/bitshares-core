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

#include <iostream>

#include <boost/test/unit_test.hpp>

#include <graphene/chain/database.hpp>
#include <graphene/chain/operations.hpp>
#include <graphene/chain/key_object.hpp>
#include <graphene/chain/account_object.hpp>
#include <graphene/chain/proposal_object.hpp>

#include <fc/crypto/digest.hpp>

#include "../common/database_fixture.hpp"

using namespace graphene::chain;

BOOST_AUTO_TEST_SUITE(block_tests)

BOOST_FIXTURE_TEST_CASE( update_account_keys, database_fixture )
{
   try
   {
      const asset_object& core = asset_id_type()(db);
      uint32_t skip_flags =
          database::skip_transaction_dupe_check
        | database::skip_delegate_signature
        | database::skip_transaction_signatures
        | database::skip_authority_check
        ;

      // Sam is the creator of accounts
      private_key_type genesis_key = generate_private_key("genesis");
      private_key_type sam_key = generate_private_key("sam");

      //
      // A = old key set
      // B = new key set
      //
      // we measure how many times we test following four cases:
      //
      //                                     A-B        B-A
      // alice     case_count[0]   A == B    empty      empty
      // bob       case_count[1]   A  < B    empty      nonempty
      // charlie   case_count[2]   B  < A    nonempty   empty
      // dan       case_count[3]   A nc B    nonempty   nonempty
      //
      // and assert that all four cases were tested at least once
      //
      account_object sam_account_object = create_account( "sam", sam_key );

      //Get a sane head block time
      generate_block( skip_flags );

      db.modify(db.get_global_properties(), [](global_property_object& p) {
         p.parameters.genesis_proposal_review_period = fc::hours(1).to_seconds();
      });

      transaction tx;
      processed_transaction ptx;

      account_object genesis_account_object = genesis_account(db);
      // transfer from genesis account to Sam account
      transfer(genesis_account_object, sam_account_object, core.amount(100000));

      const int num_keys = 5;
      vector< private_key_type > numbered_private_keys;
      vector< vector< key_id_type > > numbered_key_id;
      numbered_private_keys.reserve( num_keys );
      numbered_key_id.push_back( vector<key_id_type>() );
      numbered_key_id.push_back( vector<key_id_type>() );

      for( int i=0; i<num_keys; i++ )
      {
         private_key_type privkey = generate_private_key(
            std::string("key_") + std::to_string(i));
         public_key_type pubkey = privkey.get_public_key();
         address addr( pubkey );

         key_id_type public_key_id = register_key( pubkey ).id;
         key_id_type addr_id = register_address( addr ).id;

         numbered_private_keys.push_back( privkey );
         numbered_key_id[0].push_back( public_key_id );
         numbered_key_id[1].push_back( addr_id );
      }

      // each element of possible_key_sched is a list of exactly num_keys
      // indices into numbered_key_id[use_address].  they are defined
      // by repeating selected elements of
      // numbered_private_keys given by a different selector.
      vector< vector< int > > possible_key_sched;
      const int num_key_sched = (1 << num_keys)-1;
      possible_key_sched.reserve( num_key_sched );

      for( int s=1; s<=num_key_sched; s++ )
      {
         vector< int > v;
         int i = 0;
         v.reserve( num_keys );
         while( v.size() < num_keys )
         {
            if( s & (1 << i) )
               v.push_back( i );
            i++;
            if( i >= num_keys )
               i = 0;
         }
         possible_key_sched.push_back( v );
      }

      // we can only undo in blocks
      generate_block( skip_flags );

      std::cout << "update_account_keys:  this test will take a few minutes...\n";
      for( int use_addresses=0; use_addresses<2; use_addresses++ )
      {
         vector< key_id_type > key_ids = numbered_key_id[ use_addresses ];
         for( int num_owner_keys=1; num_owner_keys<=2; num_owner_keys++ )
         {
            for( int num_active_keys=1; num_active_keys<=2; num_active_keys++ )
            {
               std::cout << use_addresses << num_owner_keys << num_active_keys << "\n";
               for( const vector< int >& key_sched_before : possible_key_sched )
               {
                  auto it = key_sched_before.begin();
                  vector< const private_key_type* > owner_privkey;
                  vector< const key_id_type* > owner_keyid;
                  owner_privkey.reserve( num_owner_keys );

                  trx.clear();
                  account_create_operation create_op;
                  create_op.name = "alice";

                  for( int owner_index=0; owner_index<num_owner_keys; owner_index++ )
                  {
                     int i = *(it++);
                     create_op.owner.auths[ key_ids[ i ] ] = 1;
                     owner_privkey.push_back( &numbered_private_keys[i] );
                     owner_keyid.push_back( &key_ids[ i ] );
                  }
                  // size() < num_owner_keys is possible when some keys are duplicates
                  create_op.owner.weight_threshold = create_op.owner.auths.size();

                  for( int active_index=0; active_index<num_active_keys; active_index++ )
                     create_op.active.auths[ key_ids[ *(it++) ] ] = 1;
                  // size() < num_active_keys is possible when some keys are duplicates
                  create_op.active.weight_threshold = create_op.active.auths.size();

                  create_op.memo_key = key_ids[ *(it++) ] ;
                  create_op.registrar = sam_account_object.id;
                  trx.operations.push_back( create_op );
                  // trx.sign( sam_key );
                  wdump( (trx) );

                  processed_transaction ptx_create = db.push_transaction( trx,
                     database::skip_transaction_dupe_check |
                     database::skip_transaction_signatures |
                     database::skip_authority_check
                      );
                  account_id_type alice_account_id =
                     ptx_create.operation_results[0]
                     .get< object_id_type >();

                  generate_block( skip_flags );
                  for( const vector< int >& key_sched_after : possible_key_sched )
                  {
                     auto it = key_sched_after.begin();

                     trx.clear();
                     account_update_operation update_op;
                     update_op.account = alice_account_id;
                     update_op.owner = authority();
                     update_op.active = authority();

                     for( int owner_index=0; owner_index<num_owner_keys; owner_index++ )
                        update_op.owner->auths[ key_ids[ *(it++) ] ] = 1;
                     // size() < num_owner_keys is possible when some keys are duplicates
                     update_op.owner->weight_threshold = update_op.owner->auths.size();
                     for( int active_index=0; active_index<num_active_keys; active_index++ )
                        update_op.active->auths[ key_ids[ *(it++) ] ] = 1;
                     // size() < num_active_keys is possible when some keys are duplicates
                     update_op.active->weight_threshold = update_op.active->auths.size();
                     update_op.memo_key = key_ids[ *(it++) ] ;

                     trx.operations.push_back( update_op );
                     for( int i=0; i<int(create_op.owner.weight_threshold); i++)
                     {
                        trx.sign( *owner_keyid[i], *owner_privkey[i] );
                        if( i < int(create_op.owner.weight_threshold-1) )
                        {
                           BOOST_REQUIRE_THROW(db.push_transaction(trx), fc::exception);
                        }
                        else
                        {
                           db.push_transaction( trx,
                           database::skip_transaction_dupe_check |
                           database::skip_transaction_signatures );
                        }
                     }
                     verify_account_history_plugin_index();
                     generate_block( skip_flags );

                     verify_account_history_plugin_index();
                     db.pop_block();
                     verify_account_history_plugin_index();
                  }
                  db.pop_block();
                  verify_account_history_plugin_index();
               }
            }
         }
      }
   }
   catch( const fc::exception& e )
   {
      edump( (e.to_detail_string()) );
      throw;
   }
}

BOOST_AUTO_TEST_SUITE_END()
