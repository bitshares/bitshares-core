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

#include <boost/test/unit_test.hpp>

#include <graphene/chain/database.hpp>
#include <graphene/chain/exceptions.hpp>

#include <graphene/chain/account_object.hpp>
#include <graphene/chain/asset_object.hpp>
#include <graphene/chain/committee_member_object.hpp>
#include <graphene/chain/proposal_object.hpp>
#include <graphene/chain/market_object.hpp>
#include <graphene/chain/hardfork.hpp>
#include <graphene/chain/witness_schedule_object.hpp>
#include <graphene/chain/witness_object.hpp>

#include <graphene/utilities/tempdir.hpp>

#include <fc/crypto/digest.hpp>

#include "../common/database_fixture.hpp"

using namespace graphene::chain;
using namespace graphene::chain::test;

genesis_state_type make_genesis() {
   genesis_state_type genesis_state;

   genesis_state.initial_timestamp = time_point_sec( GRAPHENE_TESTING_GENESIS_TIMESTAMP );

   auto init_account_priv_key = fc::ecc::private_key::regenerate(fc::sha256::hash(string("null_key")));
   genesis_state.initial_active_witnesses = 10;
   for( unsigned int i = 0; i < genesis_state.initial_active_witnesses; ++i )
   {
      auto name = "init"+fc::to_string(i);
      genesis_state.initial_accounts.emplace_back(name,
                                                  init_account_priv_key.get_public_key(),
                                                  init_account_priv_key.get_public_key(),
                                                  true);
      genesis_state.initial_committee_candidates.push_back({name});
      genesis_state.initial_witness_candidates.push_back({name, init_account_priv_key.get_public_key()});
   }
   genesis_state.initial_parameters.current_fees->zero_all_fees();
   return genesis_state;
}

BOOST_AUTO_TEST_SUITE(block_tests)

BOOST_AUTO_TEST_CASE( block_database_test )
{
   try {
      fc::temp_directory data_dir( graphene::utilities::temp_directory_path() );

      block_database bdb;
      bdb.open( data_dir.path() );
      FC_ASSERT( bdb.is_open() );
      bdb.close();
      FC_ASSERT( !bdb.is_open() );
      bdb.open( data_dir.path() );

      signed_block b;
      for( uint32_t i = 0; i < 5; ++i )
      {
         if( i > 0 ) b.previous = b.id();
         b.witness = witness_id_type(i+1);
         bdb.store( b.id(), b );

         auto fetch = bdb.fetch_by_number( b.block_num() );
         FC_ASSERT( fetch.valid() );
         FC_ASSERT( fetch->witness ==  b.witness );
         fetch = bdb.fetch_by_number( i+1 );
         FC_ASSERT( fetch.valid() );
         FC_ASSERT( fetch->witness ==  b.witness );
         fetch = bdb.fetch_optional( b.id() );
         FC_ASSERT( fetch.valid() );
         FC_ASSERT( fetch->witness ==  b.witness );
      }

      for( uint32_t i = 1; i < 5; ++i )
      {
         auto blk = bdb.fetch_by_number( i );
         FC_ASSERT( blk.valid() );
         FC_ASSERT( blk->witness == witness_id_type(blk->block_num()) );
      }

      auto last = bdb.last();
      FC_ASSERT( last );
      FC_ASSERT( last->id() == b.id() );

      bdb.close();
      bdb.open( data_dir.path() );
      last = bdb.last();
      FC_ASSERT( last );
      FC_ASSERT( last->id() == b.id() );

      for( uint32_t i = 0; i < 5; ++i )
      {
         auto blk = bdb.fetch_by_number( i+1 );
         FC_ASSERT( blk.valid() );
         FC_ASSERT( blk->witness == witness_id_type(blk->block_num()) );
      }

   } catch (fc::exception& e) {
      edump((e.to_detail_string()));
      throw;
   }
}

BOOST_AUTO_TEST_CASE( generate_empty_blocks )
{
   try {
      fc::time_point_sec now( GRAPHENE_TESTING_GENESIS_TIMESTAMP );
      fc::temp_directory data_dir( graphene::utilities::temp_directory_path() );
      signed_block b;

      // TODO:  Don't generate this here
      auto init_account_priv_key = fc::ecc::private_key::regenerate(fc::sha256::hash(string("null_key")) );
      signed_block cutoff_block;
      uint32_t last_block;
      {
         database db;
         db.open(data_dir.path(), make_genesis, "TEST" );
         b = db.generate_block(db.get_slot_time(1), db.get_scheduled_witness(1), init_account_priv_key, database::skip_nothing);

         // TODO:  Change this test when we correct #406
         // n.b. we generate GRAPHENE_MIN_UNDO_HISTORY+1 extra blocks which will be discarded on save
         for( uint32_t i = 1; ; ++i )
         {
            BOOST_CHECK( db.head_block_id() == b.id() );
            //witness_id_type prev_witness = b.witness;
            witness_id_type cur_witness = db.get_scheduled_witness(1);
            //BOOST_CHECK( cur_witness != prev_witness );
            b = db.generate_block(db.get_slot_time(1), cur_witness, init_account_priv_key, database::skip_nothing);
            BOOST_CHECK( b.witness == cur_witness );
            uint32_t cutoff_height = db.get_dynamic_global_properties().last_irreversible_block_num;
            if( cutoff_height >= 200 )
            {
               cutoff_block = *(db.fetch_block_by_number( cutoff_height ));
               last_block = db.head_block_num();
               break;
            }
         }
         db.close();
      }
      {
         database db;
         db.open(data_dir.path(), []{return genesis_state_type();}, "TEST");
         BOOST_CHECK_EQUAL( db.head_block_num(), last_block );
         while( db.head_block_num() > cutoff_block.block_num() )
            db.pop_block();
         b = cutoff_block;
         for( uint32_t i = 0; i < 200; ++i )
         {
            BOOST_CHECK( db.head_block_id() == b.id() );
            //witness_id_type prev_witness = b.witness;
            witness_id_type cur_witness = db.get_scheduled_witness(1);
            //BOOST_CHECK( cur_witness != prev_witness );
            b = db.generate_block(db.get_slot_time(1), cur_witness, init_account_priv_key, database::skip_nothing);
         }
         BOOST_CHECK_EQUAL( db.head_block_num(), cutoff_block.block_num()+200 );
      }
   } catch (fc::exception& e) {
      edump((e.to_detail_string()));
      throw;
   }
}

BOOST_AUTO_TEST_CASE( undo_block )
{
   try {
      fc::temp_directory data_dir( graphene::utilities::temp_directory_path() );
      {
         database db;
         db.open(data_dir.path(), make_genesis, "TEST");
         fc::time_point_sec now( GRAPHENE_TESTING_GENESIS_TIMESTAMP );
         std::vector< time_point_sec > time_stack;

         auto init_account_priv_key  = fc::ecc::private_key::regenerate(fc::sha256::hash(string("null_key")) );
         for( uint32_t i = 0; i < 5; ++i )
         {
            now = db.get_slot_time(1);
            time_stack.push_back( now );
            auto b = db.generate_block( now, db.get_scheduled_witness( 1 ), init_account_priv_key, database::skip_nothing );
         }
         BOOST_CHECK( db.head_block_num() == 5 );
         BOOST_CHECK( db.head_block_time() == now );
         db.pop_block();
         time_stack.pop_back();
         now = time_stack.back();
         BOOST_CHECK( db.head_block_num() == 4 );
         BOOST_CHECK( db.head_block_time() == now );
         db.pop_block();
         time_stack.pop_back();
         now = time_stack.back();
         BOOST_CHECK( db.head_block_num() == 3 );
         BOOST_CHECK( db.head_block_time() == now );
         db.pop_block();
         time_stack.pop_back();
         now = time_stack.back();
         BOOST_CHECK( db.head_block_num() == 2 );
         BOOST_CHECK( db.head_block_time() == now );
         for( uint32_t i = 0; i < 5; ++i )
         {
            now = db.get_slot_time(1);
            time_stack.push_back( now );
            auto b = db.generate_block( now, db.get_scheduled_witness( 1 ), init_account_priv_key, database::skip_nothing );
         }
         BOOST_CHECK( db.head_block_num() == 7 );
      }
   } catch (fc::exception& e) {
      edump((e.to_detail_string()));
      throw;
   }
}

BOOST_AUTO_TEST_CASE( change_signing_key_test )
{
   try {
      fc::temp_directory data_dir( graphene::utilities::temp_directory_path() );

      auto init_account_priv_key  = fc::ecc::private_key::regenerate(fc::sha256::hash(string("null_key")) );
      auto init_pub_key = init_account_priv_key.get_public_key();
      auto new_key = fc::ecc::private_key::regenerate(fc::sha256::hash(string("new_key")) );
      auto new_pub_key = new_key.get_public_key();

      std::map< public_key_type, fc::ecc::private_key > key_map;
      key_map[init_pub_key] = init_account_priv_key;
      key_map[new_pub_key] = new_key;

      std::set< witness_id_type > witnesses;
      for( uint32_t i = 0; i <= 11; ++i ) // 11 init witnesses and 0 is reserved
         witnesses.insert( witness_id_type(i) );

      auto change_signing_key = [&init_account_priv_key]( database& db, witness_id_type wit, public_key_type new_signing_key ) {
         witness_update_operation wuop;
         wuop.witness_account = wit(db).witness_account;
         wuop.witness = wit;
         wuop.new_signing_key = new_signing_key;
         signed_transaction wu_trx;
         wu_trx.operations.push_back( wuop );
         wu_trx.set_reference_block( db.head_block_id() );
         wu_trx.set_expiration( db.head_block_time()
                               + fc::seconds( 0x1000 * db.get_global_properties().parameters.block_interval ) );
         wu_trx.sign( init_account_priv_key, db.get_chain_id() );
         PUSH_TX( db, wu_trx, 0 );
      };

      {
         database db;

         // open database
         db.open(data_dir.path(), make_genesis, "TEST");

         // generate some empty blocks with init keys
         for( uint32_t i = 0; i < 30; ++i )
         {
            auto now = db.get_slot_time(1);
            auto next_witness = db.get_scheduled_witness( 1 );
            db.generate_block( now, next_witness, init_account_priv_key, database::skip_nothing );
         }

         // generate some blocks and change keys in same block
         for( uint32_t i = 0; i < 9; ++i )
         {
            auto now = db.get_slot_time(1);
            auto next_witness = db.get_scheduled_witness( 1 );
            public_key_type current_key = next_witness(db).signing_key;
            change_signing_key( db, next_witness, new_key.get_public_key() );
            idump( (i)(now)(next_witness) );
            auto b = db.generate_block( now, next_witness, key_map[current_key], database::skip_nothing );
            idump( (b) );
         }

         // pop a few blocks and clear pending, some signing keys should be changed back
         for( uint32_t i = 0; i < 4; ++i )
         {
            db.pop_block();
         }
         db._popped_tx.clear();
         db.clear_pending();

         // generate a few blocks and change keys in same block
         for( uint32_t i = 0; i < 2; ++i )
         {
            auto now = db.get_slot_time(1);
            auto next_witness = db.get_scheduled_witness( 1 );
            public_key_type current_key = next_witness(db).signing_key;
            change_signing_key( db, next_witness, new_key.get_public_key() );
            idump( (i)(now)(next_witness) );
            auto b = db.generate_block( now, next_witness, key_map[current_key], database::skip_nothing );
            idump( (b) );
         }

         // generate some blocks but don't change a key
         for( uint32_t i = 0; i < 25; ++i )
         {
            auto now = db.get_slot_time(1);
            auto next_witness = db.get_scheduled_witness( 1 );
            public_key_type current_key = next_witness(db).signing_key;
            idump( (i)(now)(next_witness) );
            auto b = db.generate_block( now, next_witness, key_map[current_key], database::skip_nothing );
            idump( (b) );
         }

         // close the database, flush all data to disk
         db.close();
      }
      {
         database db;

         // reopen database, all data should be unchanged
         db.open(data_dir.path(), make_genesis, "TEST");

         // generate more blocks and change keys in same block
         for( uint32_t i = 0; i < 25; ++i )
         {
            auto now = db.get_slot_time(1);
            auto next_witness = db.get_scheduled_witness( 1 );
            public_key_type current_key = next_witness(db).signing_key;
            change_signing_key( db, next_witness, new_key.get_public_key() );
            idump( (i)(now)(next_witness) );
            auto b = db.generate_block( now, next_witness, key_map[current_key], database::skip_nothing );
            idump( (b) );
         }

      }
   } catch (fc::exception& e) {
      edump((e.to_detail_string()));
      throw;
   }
}

BOOST_AUTO_TEST_CASE( fork_blocks )
{
   try {
      fc::temp_directory data_dir1( graphene::utilities::temp_directory_path() );
      fc::temp_directory data_dir2( graphene::utilities::temp_directory_path() );

      database db1;
      db1.open(data_dir1.path(), make_genesis, "TEST");
      database db2;
      db2.open(data_dir2.path(), make_genesis, "TEST");
      BOOST_CHECK( db1.get_chain_id() == db2.get_chain_id() );

      auto init_account_priv_key  = fc::ecc::private_key::regenerate(fc::sha256::hash(string("null_key")) );

      BOOST_TEST_MESSAGE( "Adding blocks 1 through 10" );
      for( uint32_t i = 1; i <= 10; ++i )
      {
         auto b = db1.generate_block(db1.get_slot_time(1), db1.get_scheduled_witness(1), init_account_priv_key, database::skip_nothing);
         try {
            PUSH_BLOCK( db2, b );
         } FC_CAPTURE_AND_RETHROW( ("db2") );
      }

      for( uint32_t j = 0; j <= 4; j += 4 )
      {
         // add blocks 11 through 13 to db1 only
         BOOST_TEST_MESSAGE( "Adding 3 blocks to db1 only" );
         for( uint32_t i = 11 + j; i <= 13 + j; ++i )
         {
            BOOST_TEST_MESSAGE( i );
            auto b = db1.generate_block(db1.get_slot_time(1), db1.get_scheduled_witness(1), init_account_priv_key, database::skip_nothing);
         }
         string db1_tip = db1.head_block_id().str();

         // add different blocks 11 through 13 to db2 only
         BOOST_TEST_MESSAGE( "Add 3 different blocks to db2 only" );
         uint32_t next_slot = 3;
         for( uint32_t i = 11 + j; i <= 13 + j; ++i )
         {
            BOOST_TEST_MESSAGE( i );
            auto b =  db2.generate_block(db2.get_slot_time(next_slot), db2.get_scheduled_witness(next_slot), init_account_priv_key, database::skip_nothing);
            next_slot = 1;
            // notify both databases of the new block.
            // only db2 should switch to the new fork, db1 should not
            PUSH_BLOCK( db1, b );
            BOOST_CHECK_EQUAL(db1.head_block_id().str(), db1_tip);
            BOOST_CHECK_EQUAL(db2.head_block_id().str(), b.id().str());
         }

         //The two databases are on distinct forks now, but at the same height.
         BOOST_CHECK_EQUAL(db1.head_block_num(), 13u + j);
         BOOST_CHECK_EQUAL(db2.head_block_num(), 13u + j);
         BOOST_CHECK( db1.head_block_id() != db2.head_block_id() );

         //Make a block on db2, make it invalid, then
         //pass it to db1 and assert that db1 doesn't switch to the new fork.
         signed_block good_block;
         {
            auto b = db2.generate_block(db2.get_slot_time(1), db2.get_scheduled_witness(1), init_account_priv_key, database::skip_nothing);
            good_block = b;
            b.transactions.emplace_back(signed_transaction());
            b.transactions.back().operations.emplace_back(transfer_operation());
            b.sign( init_account_priv_key );
            BOOST_CHECK_EQUAL(b.block_num(), 14u + j);
            GRAPHENE_CHECK_THROW(PUSH_BLOCK( db1, b ), fc::exception);

            // At this point, `fetch_block_by_number` will fetch block from fork_db,
            //    so unable to reproduce the issue which is fixed in PR #938
            //    https://github.com/bitshares/bitshares-core/pull/938
            fc::optional<signed_block> previous_block = db1.fetch_block_by_number(1);
            BOOST_CHECK ( previous_block.valid() );
            uint32_t db1_blocks = db1.head_block_num();
            for( uint32_t curr_block_num = 2; curr_block_num <= db1_blocks; ++curr_block_num )
            {
               fc::optional<signed_block> curr_block = db1.fetch_block_by_number( curr_block_num );
               BOOST_CHECK( curr_block.valid() );
               BOOST_CHECK_EQUAL( curr_block->previous.str(), previous_block->id().str() );
               previous_block = curr_block;
            }
         }
         BOOST_CHECK_EQUAL(db1.head_block_num(), 13u + j);
         BOOST_CHECK_EQUAL(db1.head_block_id().str(), db1_tip);

         if( j == 0 )
         {
            // assert that db1 switches to new fork with good block
            BOOST_CHECK_EQUAL(db2.head_block_num(), 14u + j);
            PUSH_BLOCK( db1, good_block );
            BOOST_CHECK_EQUAL(db1.head_block_id().str(), db2.head_block_id().str());
         }
      }

      // generate more blocks to push the forked blocks out of fork_db
      BOOST_TEST_MESSAGE( "Adding more blocks to db1, push the forked blocks out of fork_db" );
      for( uint32_t i = 1; i <= 50; ++i )
      {
         db1.generate_block(db1.get_slot_time(1), db1.get_scheduled_witness(1), init_account_priv_key, database::skip_nothing);
      }

      {
         // PR #938 make sure db is in a good state https://github.com/bitshares/bitshares-core/pull/938
         BOOST_TEST_MESSAGE( "Checking whether all blocks on disk are good" );
         fc::optional<signed_block> previous_block = db1.fetch_block_by_number(1);
         BOOST_CHECK ( previous_block.valid() );
         uint32_t db1_blocks = db1.head_block_num();
         for( uint32_t curr_block_num = 2; curr_block_num <= db1_blocks; ++curr_block_num )
         {
            fc::optional<signed_block> curr_block = db1.fetch_block_by_number( curr_block_num );
            BOOST_CHECK( curr_block.valid() );
            BOOST_CHECK_EQUAL( curr_block->previous.str(), previous_block->id().str() );
            previous_block = curr_block;
         }
      }
   } catch (fc::exception& e) {
      edump((e.to_detail_string()));
      throw;
   }
}


/**
 *  These test has been disabled, out of order blocks should result in the node getting disconnected.
 *  
BOOST_AUTO_TEST_CASE( fork_db_tests )
{
   try {
     fork_database fdb;
     signed_block prev;
     signed_block skipped_block;
     for( uint32_t i = 0; i < 2000; ++i )
     {
        signed_block b;
        b.previous = prev.id();
        if( b.block_num() == 1800 )
           skipped_block = b;
        else
           fdb.push_block( b );
        prev = b;
     }
     auto head = fdb.head();
     FC_ASSERT( head && head->data.block_num() == 1799 );

     fdb.push_block(skipped_block);
     head = fdb.head();
     FC_ASSERT( head && head->data.block_num() == 2001, "", ("head",head->data.block_num()) );
  } FC_LOG_AND_RETHROW() 
}
BOOST_AUTO_TEST_CASE( out_of_order_blocks )
{
   try {
      fc::temp_directory data_dir1( graphene::utilities::temp_directory_path() );
      fc::temp_directory data_dir2( graphene::utilities::temp_directory_path() );

      database db1;
      db1.open(data_dir1.path(), make_genesis);
      database db2;
      db2.open(data_dir2.path(), make_genesis);
      BOOST_CHECK( db1.get_chain_id() == db2.get_chain_id() );

      auto init_account_priv_key  = fc::ecc::private_key::regenerate(fc::sha256::hash(string("null_key")) );
      auto b1 = db1.generate_block(db1.get_slot_time(1), db1.get_scheduled_witness(1), init_account_priv_key, database::skip_nothing);
      auto b2 = db1.generate_block(db1.get_slot_time(1), db1.get_scheduled_witness(1), init_account_priv_key, database::skip_nothing);
      auto b3 = db1.generate_block(db1.get_slot_time(1), db1.get_scheduled_witness(1), init_account_priv_key, database::skip_nothing);
      auto b4 = db1.generate_block(db1.get_slot_time(1), db1.get_scheduled_witness(1), init_account_priv_key, database::skip_nothing);
      auto b5 = db1.generate_block(db1.get_slot_time(1), db1.get_scheduled_witness(1), init_account_priv_key, database::skip_nothing);
      auto b6 = db1.generate_block(db1.get_slot_time(1), db1.get_scheduled_witness(1), init_account_priv_key, database::skip_nothing);
      auto b7 = db1.generate_block(db1.get_slot_time(1), db1.get_scheduled_witness(1), init_account_priv_key, database::skip_nothing);
      auto b8 = db1.generate_block(db1.get_slot_time(1), db1.get_scheduled_witness(1), init_account_priv_key, database::skip_nothing);
      auto b9 = db1.generate_block(db1.get_slot_time(1), db1.get_scheduled_witness(1), init_account_priv_key, database::skip_nothing);
      auto b10 = db1.generate_block(db1.get_slot_time(1), db1.get_scheduled_witness(1), init_account_priv_key, database::skip_nothing);
      auto b11 = db1.generate_block(db1.get_slot_time(1), db1.get_scheduled_witness(1), init_account_priv_key, database::skip_nothing);
      auto b12 = db1.generate_block(db1.get_slot_time(1), db1.get_scheduled_witness(1), init_account_priv_key, database::skip_nothing);
      BOOST_CHECK_EQUAL(db1.head_block_num(), 12);
      BOOST_CHECK_EQUAL(db2.head_block_num(), 0);
      PUSH_BLOCK( db2, b1 );
      BOOST_CHECK_EQUAL(db2.head_block_num(), 1);
      PUSH_BLOCK( db2, b3 );
      BOOST_CHECK_EQUAL(db2.head_block_num(), 1);
      PUSH_BLOCK( db2, b2 );
      BOOST_CHECK_EQUAL(db2.head_block_num(), 3);
      PUSH_BLOCK( db2, b5 );
      PUSH_BLOCK( db2, b6 );
      PUSH_BLOCK( db2, b7 );
      BOOST_CHECK_EQUAL(db2.head_block_num(), 3);
      PUSH_BLOCK( db2, b4 );
      BOOST_CHECK_EQUAL(db2.head_block_num(), 7);
      PUSH_BLOCK( db2, b8 );
      BOOST_CHECK_EQUAL(db2.head_block_num(), 8);
      PUSH_BLOCK( db2, b11 );
      PUSH_BLOCK( db2, b10 );
      PUSH_BLOCK( db2, b12 );
      BOOST_CHECK_EQUAL(db2.head_block_num(), 8);
      PUSH_BLOCK( db2, b9 );
      BOOST_CHECK_EQUAL(db2.head_block_num(), 12);
   } catch (fc::exception& e) {
      edump((e.to_detail_string()));
      throw;
   }
}
 */

BOOST_AUTO_TEST_CASE( undo_pending )
{
   try {
      fc::temp_directory data_dir( graphene::utilities::temp_directory_path() );
      {
         database db;
         db.open(data_dir.path(), make_genesis, "TEST");

         auto init_account_priv_key = fc::ecc::private_key::regenerate(fc::sha256::hash(string("null_key")) );
         public_key_type init_account_pub_key  = init_account_priv_key.get_public_key();
         const graphene::db::index& account_idx = db.get_index(protocol_ids, account_object_type);

         transfer_operation t;
         t.to = account_id_type(1);
         t.amount = asset( 10000000 );
         {
            signed_transaction trx;
            set_expiration( db, trx );

            trx.operations.push_back(t);
            PUSH_TX( db, trx, ~0 );

            auto b = db.generate_block(db.get_slot_time(1), db.get_scheduled_witness(1), init_account_priv_key, ~0);
         }

         signed_transaction trx;
         set_expiration( db, trx );
         account_id_type nathan_id = account_idx.get_next_id();
         account_create_operation cop;
         cop.registrar = GRAPHENE_TEMP_ACCOUNT;
         cop.name = "nathan";
         cop.owner = authority(1, init_account_pub_key, 1);
         cop.active = cop.owner;
         trx.operations.push_back(cop);
         //sign( trx,  init_account_priv_key  );
         PUSH_TX( db, trx );

         auto b = db.generate_block(db.get_slot_time(1), db.get_scheduled_witness(1), init_account_priv_key, database::skip_nothing);

         BOOST_CHECK(nathan_id(db).name == "nathan");

         trx.clear();
         set_expiration( db, trx );
         t.fee = asset(1);
         t.from = account_id_type(1);
         t.to = nathan_id;
         t.amount = asset(5000);
         trx.operations.push_back(t);
         db.push_transaction(trx, ~0);
         trx.clear();
         set_expiration( db, trx );
         trx.operations.push_back(t);
         db.push_transaction(trx, ~0);

         BOOST_CHECK(db.get_balance(nathan_id, asset_id_type()).amount == 10000);
         db.clear_pending();
         BOOST_CHECK(db.get_balance(nathan_id, asset_id_type()).amount == 0);
      }
   } catch (fc::exception& e) {
      edump((e.to_detail_string()));
      throw;
   }
}

BOOST_AUTO_TEST_CASE( switch_forks_undo_create )
{
   try {
      fc::temp_directory dir1( graphene::utilities::temp_directory_path() ),
                         dir2( graphene::utilities::temp_directory_path() );
      database db1,
               db2;
      db1.open(dir1.path(), make_genesis, "TEST");
      db2.open(dir2.path(), make_genesis, "TEST");
      BOOST_CHECK( db1.get_chain_id() == db2.get_chain_id() );

      auto init_account_priv_key  = fc::ecc::private_key::regenerate(fc::sha256::hash(string("null_key")) );
      public_key_type init_account_pub_key  = init_account_priv_key.get_public_key();
      const graphene::db::index& account_idx = db1.get_index(protocol_ids, account_object_type);

      signed_transaction trx;
      set_expiration( db1, trx );
      account_id_type nathan_id = account_idx.get_next_id();
      account_create_operation cop;
      cop.registrar = GRAPHENE_TEMP_ACCOUNT;
      cop.name = "nathan";
      cop.owner = authority(1, init_account_pub_key, 1);
      cop.active = cop.owner;
      trx.operations.push_back(cop);
      PUSH_TX( db1, trx );

      // generate blocks
      // db1 : A
      // db2 : B C D

      auto aw = db1.get_global_properties().active_witnesses;
      auto b = db1.generate_block(db1.get_slot_time(1), db1.get_scheduled_witness(1), init_account_priv_key, database::skip_nothing);

      BOOST_CHECK(nathan_id(db1).name == "nathan");

      b = db2.generate_block(db2.get_slot_time(1), db2.get_scheduled_witness(1), init_account_priv_key, database::skip_nothing);
      db1.push_block(b);
      aw = db2.get_global_properties().active_witnesses;
      b = db2.generate_block(db2.get_slot_time(1), db2.get_scheduled_witness(1), init_account_priv_key, database::skip_nothing);
      db1.push_block(b);
      GRAPHENE_REQUIRE_THROW(nathan_id(db2), fc::exception);
      nathan_id(db1); /// it should be included in the pending state
      db1.clear_pending(); // clear it so that we can verify it was properly removed from pending state.
      GRAPHENE_REQUIRE_THROW(nathan_id(db1), fc::exception);

      PUSH_TX( db2, trx );

      aw = db2.get_global_properties().active_witnesses;
      b = db2.generate_block(db2.get_slot_time(1), db2.get_scheduled_witness(1), init_account_priv_key, database::skip_nothing);
      db1.push_block(b);

      BOOST_CHECK(nathan_id(db1).name == "nathan");
      BOOST_CHECK(nathan_id(db2).name == "nathan");
   } catch (fc::exception& e) {
      edump((e.to_detail_string()));
      throw;
   }
}

BOOST_AUTO_TEST_CASE( duplicate_transactions )
{
   try {
      fc::temp_directory dir1( graphene::utilities::temp_directory_path() ),
                         dir2( graphene::utilities::temp_directory_path() );
      database db1,
               db2;
      db1.open(dir1.path(), make_genesis, "TEST");
      db2.open(dir2.path(), make_genesis, "TEST");
      BOOST_CHECK( db1.get_chain_id() == db2.get_chain_id() );

      auto skip_sigs = database::skip_transaction_signatures | database::skip_authority_check;

      auto init_account_priv_key  = fc::ecc::private_key::regenerate(fc::sha256::hash(string("null_key")) );
      public_key_type init_account_pub_key  = init_account_priv_key.get_public_key();
      const graphene::db::index& account_idx = db1.get_index(protocol_ids, account_object_type);

      signed_transaction trx;
      set_expiration( db1, trx );
      account_id_type nathan_id = account_idx.get_next_id();
      account_create_operation cop;
      cop.name = "nathan";
      cop.owner = authority(1, init_account_pub_key, 1);
      cop.active = cop.owner;
      trx.operations.push_back(cop);
      trx.sign( init_account_priv_key, db1.get_chain_id() );
      PUSH_TX( db1, trx, skip_sigs );

      trx = decltype(trx)();
      set_expiration( db1, trx );
      transfer_operation t;
      t.to = nathan_id;
      t.amount = asset(500);
      trx.operations.push_back(t);
      trx.sign( init_account_priv_key, db1.get_chain_id() );
      PUSH_TX( db1, trx, skip_sigs );

      GRAPHENE_CHECK_THROW(PUSH_TX( db1, trx, skip_sigs ), fc::exception);

      auto b = db1.generate_block( db1.get_slot_time(1), db1.get_scheduled_witness( 1 ), init_account_priv_key, skip_sigs );
      PUSH_BLOCK( db2, b, skip_sigs );

      GRAPHENE_CHECK_THROW(PUSH_TX( db1, trx, skip_sigs ), fc::exception);
      GRAPHENE_CHECK_THROW(PUSH_TX( db2, trx, skip_sigs ), fc::exception);
      BOOST_CHECK_EQUAL(db1.get_balance(nathan_id, asset_id_type()).amount.value, 500);
      BOOST_CHECK_EQUAL(db2.get_balance(nathan_id, asset_id_type()).amount.value, 500);
   } catch (fc::exception& e) {
      edump((e.to_detail_string()));
      throw;
   }
}

BOOST_AUTO_TEST_CASE( tapos )
{
   try {
      fc::temp_directory dir1( graphene::utilities::temp_directory_path() );
      database db1;
      db1.open(dir1.path(), make_genesis, "TEST");

      const account_object& init1 = *db1.get_index_type<account_index>().indices().get<by_name>().find("init1");

      auto init_account_priv_key  = fc::ecc::private_key::regenerate(fc::sha256::hash(string("null_key")) );
      public_key_type init_account_pub_key  = init_account_priv_key.get_public_key();
      const graphene::db::index& account_idx = db1.get_index(protocol_ids, account_object_type);

      auto b = db1.generate_block( db1.get_slot_time(1), db1.get_scheduled_witness( 1 ), init_account_priv_key, database::skip_nothing);

      signed_transaction trx;
      //This transaction must be in the next block after its reference, or it is invalid.
      trx.set_expiration( db1.head_block_time() ); //db1.get_slot_time(1) );
      trx.set_reference_block( db1.head_block_id() );

      account_id_type nathan_id = account_idx.get_next_id();
      account_create_operation cop;
      cop.registrar = init1.id;
      cop.name = "nathan";
      cop.owner = authority(1, init_account_pub_key, 1);
      cop.active = cop.owner;
      trx.operations.push_back(cop);
      trx.sign( init_account_priv_key, db1.get_chain_id() );
      db1.push_transaction(trx);
      b = db1.generate_block(db1.get_slot_time(1), db1.get_scheduled_witness(1), init_account_priv_key, database::skip_nothing);
      trx.clear();

      transfer_operation t;
      t.to = nathan_id;
      t.amount = asset(50);
      trx.operations.push_back(t);
      trx.sign( init_account_priv_key, db1.get_chain_id() );
      //relative_expiration is 1, but ref block is 2 blocks old, so this should fail.
      GRAPHENE_REQUIRE_THROW(PUSH_TX( db1, trx, database::skip_transaction_signatures | database::skip_authority_check ), fc::exception);
      set_expiration( db1, trx );
      trx.clear_signatures();
      trx.sign( init_account_priv_key, db1.get_chain_id() );
      db1.push_transaction(trx, database::skip_transaction_signatures | database::skip_authority_check);
   } catch (fc::exception& e) {
      edump((e.to_detail_string()));
      throw;
   }
}

BOOST_FIXTURE_TEST_CASE( optional_tapos, database_fixture )
{
   try
   {
      ACTORS( (alice)(bob) );

      generate_block();

      BOOST_TEST_MESSAGE( "Create transaction" );

      transfer( account_id_type(), alice_id, asset( 1000000 ) );
      transfer_operation op;
      op.from = alice_id;
      op.to = bob_id;
      op.amount = asset( 1000 );
      signed_transaction tx;
      tx.operations.push_back( op );
      set_expiration( db, tx );

      BOOST_TEST_MESSAGE( "ref_block_num=0, ref_block_prefix=0" );

      tx.ref_block_num = 0;
      tx.ref_block_prefix = 0;
      tx.clear_signatures();
      sign( tx, alice_private_key );
      PUSH_TX( db, tx );

      BOOST_TEST_MESSAGE( "proper ref_block_num, ref_block_prefix" );

      set_expiration( db, tx );
      tx.clear_signatures();
      sign( tx, alice_private_key );
      PUSH_TX( db, tx );

      BOOST_TEST_MESSAGE( "ref_block_num=0, ref_block_prefix=12345678" );

      tx.ref_block_num = 0;
      tx.ref_block_prefix = 0x12345678;
      tx.clear_signatures();
      sign( tx, alice_private_key );
      GRAPHENE_REQUIRE_THROW( PUSH_TX( db, tx ), fc::exception );

      BOOST_TEST_MESSAGE( "ref_block_num=1, ref_block_prefix=12345678" );

      tx.ref_block_num = 1;
      tx.ref_block_prefix = 0x12345678;
      tx.clear_signatures();
      sign( tx, alice_private_key );
      GRAPHENE_REQUIRE_THROW( PUSH_TX( db, tx ), fc::exception );

      BOOST_TEST_MESSAGE( "ref_block_num=9999, ref_block_prefix=12345678" );

      tx.ref_block_num = 9999;
      tx.ref_block_prefix = 0x12345678;
      tx.clear_signatures();
      sign( tx, alice_private_key );
      GRAPHENE_REQUIRE_THROW( PUSH_TX( db, tx ), fc::exception );
   }
   catch (fc::exception& e)
   {
      edump((e.to_detail_string()));
      throw;
   }
}

BOOST_FIXTURE_TEST_CASE( maintenance_interval, database_fixture )
{
   try {
      generate_block();
      BOOST_CHECK_EQUAL(db.head_block_num(), 2);

      fc::time_point_sec maintenence_time = db.get_dynamic_global_properties().next_maintenance_time;
      BOOST_CHECK_GT(maintenence_time.sec_since_epoch(), db.head_block_time().sec_since_epoch());
      auto initial_properties = db.get_global_properties();
      const account_object& nathan = create_account("nathan");
      upgrade_to_lifetime_member(nathan);
      const committee_member_object nathans_committee_member = create_committee_member(nathan);
      {
         account_update_operation op;
         op.account = nathan.id;
         op.new_options = nathan.options;
         op.new_options->votes.insert(nathans_committee_member.vote_id);
         trx.operations.push_back(op);
         PUSH_TX( db, trx, ~0 );
         trx.operations.clear();
      }
      transfer(account_id_type()(db), nathan, asset(5000));

      generate_blocks(maintenence_time - initial_properties.parameters.block_interval);
      BOOST_CHECK_EQUAL(db.get_global_properties().parameters.maximum_transaction_size,
                        initial_properties.parameters.maximum_transaction_size);
      BOOST_CHECK_EQUAL(db.get_dynamic_global_properties().next_maintenance_time.sec_since_epoch(),
                        db.head_block_time().sec_since_epoch() + db.get_global_properties().parameters.block_interval);
      BOOST_CHECK(db.get_global_properties().active_witnesses == initial_properties.active_witnesses);
      BOOST_CHECK(db.get_global_properties().active_committee_members == initial_properties.active_committee_members);

      generate_block();

      auto new_properties = db.get_global_properties();
      BOOST_CHECK(new_properties.active_committee_members != initial_properties.active_committee_members);
      BOOST_CHECK(std::find(new_properties.active_committee_members.begin(),
                            new_properties.active_committee_members.end(), nathans_committee_member.id) !=
                  new_properties.active_committee_members.end());
      BOOST_CHECK_EQUAL(db.get_dynamic_global_properties().next_maintenance_time.sec_since_epoch(),
                        maintenence_time.sec_since_epoch() + new_properties.parameters.maintenance_interval);
      maintenence_time = db.get_dynamic_global_properties().next_maintenance_time;
      BOOST_CHECK_GT(maintenence_time.sec_since_epoch(), db.head_block_time().sec_since_epoch());
      db.close();
   } catch (fc::exception& e) {
      edump((e.to_detail_string()));
      throw;
   }
}


BOOST_FIXTURE_TEST_CASE( limit_order_expiration, database_fixture )
{ try {
   //Get a sane head block time
   generate_block();

   auto* test = &create_bitasset("MIATEST");
   auto* core = &asset_id_type()(db);
   auto* nathan = &create_account("nathan");
   auto* committee = &account_id_type()(db);

   transfer(*committee, *nathan, core->amount(50000));

   BOOST_CHECK_EQUAL( get_balance(*nathan, *core), 50000 );

   limit_order_create_operation op;
   op.seller = nathan->id;
   op.amount_to_sell = core->amount(500);
   op.min_to_receive = test->amount(500);
   op.expiration = db.head_block_time() + fc::seconds(10);
   trx.operations.push_back(op);
   auto ptrx = PUSH_TX( db, trx, ~0 );

   BOOST_CHECK_EQUAL( get_balance(*nathan, *core), 49500 );

   auto ptrx_id = ptrx.operation_results.back().get<object_id_type>();
   auto limit_index = db.get_index_type<limit_order_index>().indices();
   auto limit_itr = limit_index.begin();
   BOOST_REQUIRE( limit_itr != limit_index.end() );
   BOOST_REQUIRE( limit_itr->id == ptrx_id );
   BOOST_REQUIRE( db.find_object(limit_itr->id) );
   BOOST_CHECK_EQUAL( get_balance(*nathan, *core), 49500 );
   auto id = limit_itr->id;

   generate_blocks(op.expiration, false);
   test = &get_asset("MIATEST");
   core = &asset_id_type()(db);
   nathan = &get_account("nathan");
   committee = &account_id_type()(db);

   BOOST_CHECK(db.find_object(id) == nullptr);
   BOOST_CHECK_EQUAL( get_balance(*nathan, *core), 50000 );
} FC_LOG_AND_RETHROW() }

BOOST_FIXTURE_TEST_CASE( double_sign_check, database_fixture )
{ try {
   generate_block();
   const auto& alice = account_id_type()(db);
   ACTOR(bob);
   asset amount(1000);

   set_expiration( db, trx );
   transfer_operation t;
   t.from = alice.id;
   t.to = bob.id;
   t.amount = amount;
   trx.operations.push_back(t);
   for( auto& op : trx.operations ) db.current_fee_schedule().set_fee(op);
   trx.validate();

   db.push_transaction(trx, ~0);

   trx.operations.clear();
   t.from = bob.id;
   t.to = alice.id;
   t.amount = amount;
   trx.operations.push_back(t);
   for( auto& op : trx.operations ) db.current_fee_schedule().set_fee(op);
   trx.validate();

   BOOST_TEST_MESSAGE( "Verify that not-signing causes an exception" );
   GRAPHENE_REQUIRE_THROW( db.push_transaction(trx, 0), fc::exception );

   BOOST_TEST_MESSAGE( "Verify that double-signing causes an exception" );
   sign( trx, bob_private_key );
   sign( trx, bob_private_key );
   GRAPHENE_REQUIRE_THROW( db.push_transaction(trx, 0), tx_duplicate_sig );

   BOOST_TEST_MESSAGE( "Verify that signing with an extra, unused key fails" );
   trx.signatures.pop_back();
   sign( trx, generate_private_key("bogus" ));
   GRAPHENE_REQUIRE_THROW( db.push_transaction(trx, 0), tx_irrelevant_sig );

   BOOST_TEST_MESSAGE( "Verify that signing once with the proper key passes" );
   trx.signatures.pop_back();
   trx.signees.clear(); // signees should be invalidated
   db.push_transaction(trx, 0);

} FC_LOG_AND_RETHROW() }

BOOST_FIXTURE_TEST_CASE( change_block_interval, database_fixture )
{ try {
   generate_block();

   db.modify(db.get_global_properties(), [](global_property_object& p) {
      p.parameters.committee_proposal_review_period = fc::hours(1).to_seconds();
   });

   BOOST_TEST_MESSAGE( "Creating a proposal to change the block_interval to 1 second" );
   {
      proposal_create_operation cop = proposal_create_operation::committee_proposal(db.get_global_properties().parameters, db.head_block_time());
      cop.fee_paying_account = GRAPHENE_TEMP_ACCOUNT;
      cop.expiration_time = db.head_block_time() + *cop.review_period_seconds + 10;
      committee_member_update_global_parameters_operation uop;
      uop.new_parameters.block_interval = 1;
      cop.proposed_ops.emplace_back(uop);
      trx.operations.push_back(cop);
      db.push_transaction(trx);
   }
   BOOST_TEST_MESSAGE( "Updating proposal by signing with the committee_member private key" );
   {
      proposal_update_operation uop;
      uop.fee_paying_account = GRAPHENE_TEMP_ACCOUNT;
      uop.active_approvals_to_add = {get_account("init0").get_id(), get_account("init1").get_id(),
                                     get_account("init2").get_id(), get_account("init3").get_id(),
                                     get_account("init4").get_id(), get_account("init5").get_id(),
                                     get_account("init6").get_id(), get_account("init7").get_id()};
      trx.operations.push_back(uop);
      sign( trx, init_account_priv_key );
      /*
      sign( trx, get_account("init1" ).active.get_keys().front(),init_account_priv_key);
      sign( trx, get_account("init2" ).active.get_keys().front(),init_account_priv_key);
      sign( trx, get_account("init3" ).active.get_keys().front(),init_account_priv_key);
      sign( trx, get_account("init4" ).active.get_keys().front(),init_account_priv_key);
      sign( trx, get_account("init5" ).active.get_keys().front(),init_account_priv_key);
      sign( trx, get_account("init6" ).active.get_keys().front(),init_account_priv_key);
      sign( trx, get_account("init7" ).active.get_keys().front(),init_account_priv_key);
      */
      db.push_transaction(trx);
      BOOST_CHECK(proposal_id_type()(db).is_authorized_to_execute(db));
   }
   BOOST_TEST_MESSAGE( "Verifying that the interval didn't change immediately" );

   BOOST_CHECK_EQUAL(db.get_global_properties().parameters.block_interval, 5);
   auto past_time = db.head_block_time().sec_since_epoch();
   generate_block();
   BOOST_CHECK_EQUAL(db.head_block_time().sec_since_epoch() - past_time, 5);
   generate_block();
   BOOST_CHECK_EQUAL(db.head_block_time().sec_since_epoch() - past_time, 10);

   BOOST_TEST_MESSAGE( "Generating blocks until proposal expires" );
   generate_blocks(proposal_id_type()(db).expiration_time + 5);
   BOOST_TEST_MESSAGE( "Verify that the block interval is still 5 seconds" );
   BOOST_CHECK_EQUAL(db.get_global_properties().parameters.block_interval, 5);

   BOOST_TEST_MESSAGE( "Generating blocks until next maintenance interval" );
   generate_blocks(db.get_dynamic_global_properties().next_maintenance_time);
   generate_block();   // get the maintenance skip slots out of the way

   BOOST_TEST_MESSAGE( "Verify that the new block interval is 1 second" );
   BOOST_CHECK_EQUAL(db.get_global_properties().parameters.block_interval, 1);
   past_time = db.head_block_time().sec_since_epoch();
   generate_block();
   BOOST_CHECK_EQUAL(db.head_block_time().sec_since_epoch() - past_time, 1);
   generate_block();
   BOOST_CHECK_EQUAL(db.head_block_time().sec_since_epoch() - past_time, 2);
} FC_LOG_AND_RETHROW() }

BOOST_FIXTURE_TEST_CASE( pop_block_twice, database_fixture )
{
   try
   {
      uint32_t skip_flags = (
           database::skip_witness_signature
         | database::skip_transaction_signatures
         | database::skip_authority_check
         );

      const asset_object& core = asset_id_type()(db);

      // Sam is the creator of accounts
      private_key_type committee_key = init_account_priv_key;
      private_key_type sam_key = generate_private_key("sam");
      account_object sam_account_object = create_account("sam", sam_key);

      //Get a sane head block time
      generate_block( skip_flags );

      db.modify(db.get_global_properties(), [](global_property_object& p) {
         p.parameters.committee_proposal_review_period = fc::hours(1).to_seconds();
      });

      transaction tx;
      processed_transaction ptx;

      account_object committee_account_object = committee_account(db);
      // transfer from committee account to Sam account
      transfer(committee_account_object, sam_account_object, core.amount(100000));

      generate_block(skip_flags);

      create_account("alice");
      generate_block(skip_flags);
      create_account("bob");
      generate_block(skip_flags);

      db.pop_block();
      db.pop_block();
   } catch(const fc::exception& e) {
      edump( (e.to_detail_string()) );
      throw;
   }
}

BOOST_FIXTURE_TEST_CASE( rsf_missed_blocks, database_fixture )
{
   try
   {
      generate_block();

      auto rsf = [&]() -> string
      {
         fc::uint128 rsf = db.get_dynamic_global_properties().recent_slots_filled;
         string result = "";
         result.reserve(128);
         for( int i=0; i<128; i++ )
         {
            result += ((rsf.lo & 1) == 0) ? '0' : '1';
            rsf >>= 1;
         }
         return result;
      };

      auto pct = []( uint32_t x ) -> uint32_t
      {
         return uint64_t( GRAPHENE_100_PERCENT ) * x / 128;
      };

      BOOST_CHECK_EQUAL( rsf(),
         "1111111111111111111111111111111111111111111111111111111111111111"
         "1111111111111111111111111111111111111111111111111111111111111111"
      );
      BOOST_CHECK_EQUAL( db.witness_participation_rate(), GRAPHENE_100_PERCENT );

      generate_block( ~0, init_account_priv_key, 1 );
      BOOST_CHECK_EQUAL( rsf(),
         "0111111111111111111111111111111111111111111111111111111111111111"
         "1111111111111111111111111111111111111111111111111111111111111111"
      );
      BOOST_CHECK_EQUAL( db.witness_participation_rate(), pct(127) );

      generate_block( ~0, init_account_priv_key, 1 );
      BOOST_CHECK_EQUAL( rsf(),
         "0101111111111111111111111111111111111111111111111111111111111111"
         "1111111111111111111111111111111111111111111111111111111111111111"
      );
      BOOST_CHECK_EQUAL( db.witness_participation_rate(), pct(126) );

      generate_block( ~0, init_account_priv_key, 2 );
      BOOST_CHECK_EQUAL( rsf(),
         "0010101111111111111111111111111111111111111111111111111111111111"
         "1111111111111111111111111111111111111111111111111111111111111111"
      );
      BOOST_CHECK_EQUAL( db.witness_participation_rate(), pct(124) );

      generate_block( ~0, init_account_priv_key, 3 );
      BOOST_CHECK_EQUAL( rsf(),
         "0001001010111111111111111111111111111111111111111111111111111111"
         "1111111111111111111111111111111111111111111111111111111111111111"
      );
      BOOST_CHECK_EQUAL( db.witness_participation_rate(), pct(121) );

      generate_block( ~0, init_account_priv_key, 5 );
      BOOST_CHECK_EQUAL( rsf(),
         "0000010001001010111111111111111111111111111111111111111111111111"
         "1111111111111111111111111111111111111111111111111111111111111111"
      );
      BOOST_CHECK_EQUAL( db.witness_participation_rate(), pct(116) );

      generate_block( ~0, init_account_priv_key, 8 );
      BOOST_CHECK_EQUAL( rsf(),
         "0000000010000010001001010111111111111111111111111111111111111111"
         "1111111111111111111111111111111111111111111111111111111111111111"
      );
      BOOST_CHECK_EQUAL( db.witness_participation_rate(), pct(108) );

      generate_block( ~0, init_account_priv_key, 13 );
      BOOST_CHECK_EQUAL( rsf(),
         "0000000000000100000000100000100010010101111111111111111111111111"
         "1111111111111111111111111111111111111111111111111111111111111111"
      );
      BOOST_CHECK_EQUAL( db.witness_participation_rate(), pct(95) );

      generate_block();
      BOOST_CHECK_EQUAL( rsf(),
         "1000000000000010000000010000010001001010111111111111111111111111"
         "1111111111111111111111111111111111111111111111111111111111111111"
      );
      BOOST_CHECK_EQUAL( db.witness_participation_rate(), pct(95) );

      generate_block();
      BOOST_CHECK_EQUAL( rsf(),
         "1100000000000001000000001000001000100101011111111111111111111111"
         "1111111111111111111111111111111111111111111111111111111111111111"
      );
      BOOST_CHECK_EQUAL( db.witness_participation_rate(), pct(95) );

      generate_block();
      BOOST_CHECK_EQUAL( rsf(),
         "1110000000000000100000000100000100010010101111111111111111111111"
         "1111111111111111111111111111111111111111111111111111111111111111"
      );
      BOOST_CHECK_EQUAL( db.witness_participation_rate(), pct(95) );

      generate_block();
      BOOST_CHECK_EQUAL( rsf(),
         "1111000000000000010000000010000010001001010111111111111111111111"
         "1111111111111111111111111111111111111111111111111111111111111111"
      );
      BOOST_CHECK_EQUAL( db.witness_participation_rate(), pct(95) );

      generate_block( ~0, init_account_priv_key, 64 );
      BOOST_CHECK_EQUAL( rsf(),
         "0000000000000000000000000000000000000000000000000000000000000000"
         "1111100000000000001000000001000001000100101011111111111111111111"
      );
      BOOST_CHECK_EQUAL( db.witness_participation_rate(), pct(31) );

      generate_block( ~0, init_account_priv_key, 32 );
      BOOST_CHECK_EQUAL( rsf(),
         "0000000000000000000000000000000010000000000000000000000000000000"
         "0000000000000000000000000000000001111100000000000001000000001000"
      );
      BOOST_CHECK_EQUAL( db.witness_participation_rate(), pct(8) );
   }
   FC_LOG_AND_RETHROW()
}

BOOST_FIXTURE_TEST_CASE( transaction_invalidated_in_cache, database_fixture )
{
   try
   {
      ACTORS( (alice)(bob) );

      auto generate_block = [&]( database& d, uint32_t skip ) -> signed_block
      {
         return d.generate_block(d.get_slot_time(1), d.get_scheduled_witness(1), init_account_priv_key, skip);
      };

      // tx's created by ACTORS() have bogus authority, so we need to
      // skip_authority_check in the block where they're included
      signed_block b1 = generate_block(db, database::skip_authority_check);

      fc::temp_directory data_dir2( graphene::utilities::temp_directory_path() );

      database db2;
      db2.open(data_dir2.path(), make_genesis, "TEST");
      BOOST_CHECK( db.get_chain_id() == db2.get_chain_id() );

      while( db2.head_block_num() < db.head_block_num() )
      {
         optional< signed_block > b = db.fetch_block_by_number( db2.head_block_num()+1 );
         db2.push_block(*b, database::skip_witness_signature
                           |database::skip_authority_check );
      }
      BOOST_CHECK( db2.get( alice_id ).name == "alice" );
      BOOST_CHECK( db2.get( bob_id ).name == "bob" );

      db2.push_block(generate_block(db, database::skip_nothing));
      transfer( account_id_type(), alice_id, asset( 1000 ) );
      transfer( account_id_type(),   bob_id, asset( 1000 ) );
      // need to skip authority check here as well for same reason as above
      db2.push_block(generate_block(db, database::skip_authority_check), database::skip_authority_check);

      BOOST_CHECK_EQUAL(db.get_balance(alice_id, asset_id_type()).amount.value, 1000);
      BOOST_CHECK_EQUAL(db.get_balance(  bob_id, asset_id_type()).amount.value, 1000);
      BOOST_CHECK_EQUAL(db2.get_balance(alice_id, asset_id_type()).amount.value, 1000);
      BOOST_CHECK_EQUAL(db2.get_balance(  bob_id, asset_id_type()).amount.value, 1000);

      auto generate_and_send = [&]( int n )
      {
         for( int i=0; i<n; i++ )
         {
            signed_block b = generate_block(db2, database::skip_nothing);
            PUSH_BLOCK( db, b );
         }
      };

      auto generate_xfer_tx = [&]( account_id_type from, account_id_type to, share_type amount, int blocks_to_expire ) -> signed_transaction
      {
         signed_transaction tx;
         transfer_operation xfer_op;
         xfer_op.from = from;
         xfer_op.to = to;
         xfer_op.amount = asset( amount, asset_id_type() );
         xfer_op.fee = asset( 0, asset_id_type() );
         tx.operations.push_back( xfer_op );
         tx.set_expiration( db.head_block_time() + blocks_to_expire * db.get_global_properties().parameters.block_interval );
         if( from == alice_id )
            sign( tx, alice_private_key );
         else
            sign( tx, bob_private_key );
         return tx;
      };

      signed_transaction tx = generate_xfer_tx( alice_id, bob_id, 1000, 2 );
      tx.set_expiration( db.head_block_time() + 2 * db.get_global_properties().parameters.block_interval );
      tx.clear_signatures();
      sign( tx, alice_private_key );
      // put the tx in db tx cache
      PUSH_TX( db, tx );

      BOOST_CHECK_EQUAL(db.get_balance(alice_id, asset_id_type()).amount.value,    0);
      BOOST_CHECK_EQUAL(db.get_balance(  bob_id, asset_id_type()).amount.value, 2000);

      // generate some blocks with db2, make tx expire in db's cache
      generate_and_send(3);

      BOOST_CHECK_EQUAL(db.get_balance(alice_id, asset_id_type()).amount.value, 1000);
      BOOST_CHECK_EQUAL(db.get_balance(  bob_id, asset_id_type()).amount.value, 1000);

      // generate a block with db and ensure we don't somehow apply it
      PUSH_BLOCK(db2, generate_block(db, database::skip_nothing));
      BOOST_CHECK_EQUAL(db.get_balance(alice_id, asset_id_type()).amount.value, 1000);
      BOOST_CHECK_EQUAL(db.get_balance(  bob_id, asset_id_type()).amount.value, 1000);

      // now the tricky part...
      // (A) Bob sends 1000 to Alice
      // (B) Alice sends 2000 to Bob
      // (C) Alice sends 500 to Bob
      //
      // We push AB, then receive a block containing C.
      // we need to apply the block, then invalidate B in the cache.
      // AB results in Alice having 0, Bob having 2000.
      // C results in Alice having 500, Bob having 1500.
      //
      // This needs to occur while switching to a fork.
      //

      signed_transaction tx_a = generate_xfer_tx( bob_id, alice_id, 1000, 2 );
      signed_transaction tx_b = generate_xfer_tx( alice_id, bob_id, 2000, 10 );
      signed_transaction tx_c = generate_xfer_tx( alice_id, bob_id,  500, 10 );

      generate_block( db, database::skip_nothing );

      PUSH_TX( db, tx_a );
      BOOST_CHECK_EQUAL(db.get_balance(alice_id, asset_id_type()).amount.value, 2000);
      BOOST_CHECK_EQUAL(db.get_balance(  bob_id, asset_id_type()).amount.value,    0);

      PUSH_TX( db, tx_b );
      PUSH_TX( db2, tx_c );

      BOOST_CHECK_EQUAL(db.get_balance(alice_id, asset_id_type()).amount.value, 0);
      BOOST_CHECK_EQUAL(db.get_balance(  bob_id, asset_id_type()).amount.value, 2000);

      BOOST_CHECK_EQUAL(db2.get_balance(alice_id, asset_id_type()).amount.value, 500);
      BOOST_CHECK_EQUAL(db2.get_balance(  bob_id, asset_id_type()).amount.value, 1500);

      // generate enough blocks on db2 to cause db to switch forks
      generate_and_send(2);

      // db should invalidate B, but still be applying A, so the states don't agree

      BOOST_CHECK_EQUAL(db.get_balance(alice_id, asset_id_type()).amount.value, 1500);
      BOOST_CHECK_EQUAL(db.get_balance(  bob_id, asset_id_type()).amount.value, 500);

      BOOST_CHECK_EQUAL(db2.get_balance(alice_id, asset_id_type()).amount.value, 500);
      BOOST_CHECK_EQUAL(db2.get_balance(  bob_id, asset_id_type()).amount.value, 1500);

      // This will cause A to expire in db
      generate_and_send(1);

      BOOST_CHECK_EQUAL(db.get_balance(alice_id, asset_id_type()).amount.value, 500);
      BOOST_CHECK_EQUAL(db.get_balance(  bob_id, asset_id_type()).amount.value, 1500);

      BOOST_CHECK_EQUAL(db2.get_balance(alice_id, asset_id_type()).amount.value, 500);
      BOOST_CHECK_EQUAL(db2.get_balance(  bob_id, asset_id_type()).amount.value, 1500);

      // Make sure we can generate and accept a plain old empty block on top of all this!
      generate_and_send(1);
   }
   catch (fc::exception& e)
   {
      edump((e.to_detail_string()));
      throw;
   }
}

BOOST_AUTO_TEST_CASE( genesis_reserve_ids )
{
   try
   {
      fc::time_point_sec now( GRAPHENE_TESTING_GENESIS_TIMESTAMP );
      fc::temp_directory data_dir( graphene::utilities::temp_directory_path() );

      uint32_t num_special_accounts = 100;
      uint32_t num_special_assets = 30;

      database db;
      db.open( data_dir.path(), [&]() -> genesis_state_type
      {
         genesis_state_type genesis_state = make_genesis();
         genesis_state_type::initial_asset_type usd;

         usd.symbol = "USD";
         usd.issuer_name = "init0";
         usd.description = "federally floated";
         usd.precision = 4;
         usd.max_supply = GRAPHENE_MAX_SHARE_SUPPLY;
         usd.accumulated_fees = 0;
         usd.is_bitasset = true;
         
         genesis_state.immutable_parameters.num_special_accounts = num_special_accounts;
         genesis_state.immutable_parameters.num_special_assets = num_special_assets;
         genesis_state.initial_assets.push_back( usd );

         return genesis_state;
      }, "TEST" );

      const auto& acct_idx = db.get_index_type<account_index>().indices().get<by_name>();
      auto acct_itr = acct_idx.find("init0");
      BOOST_REQUIRE( acct_itr != acct_idx.end() );
      BOOST_CHECK( acct_itr->id == account_id_type( num_special_accounts ) );
      
      const auto& asset_idx = db.get_index_type<asset_index>().indices().get<by_symbol>();
      auto asset_itr = asset_idx.find("USD");
      BOOST_REQUIRE( asset_itr != asset_idx.end() );
      BOOST_CHECK( asset_itr->id == asset_id_type( num_special_assets ) );
   }
   catch (fc::exception& e)
   {
      edump((e.to_detail_string()));
      throw;
   }
}

BOOST_FIXTURE_TEST_CASE( miss_some_blocks, database_fixture )
{ try {
   std::vector<witness_id_type> witnesses = witness_schedule_id_type()(db).current_shuffled_witnesses;
   BOOST_CHECK_EQUAL( 10, witnesses.size() );
   // database_fixture constructor calls generate_block once, signed by witnesses[0]
   generate_block(); // witnesses[1]
   generate_block(); // witnesses[2]
   for( const auto& id : witnesses )
      BOOST_CHECK_EQUAL( 0, id(db).total_missed );
   // generate_blocks generates another block *now* (witnesses[3])
   // and one at now+10 blocks (witnesses[12%10])
   generate_blocks( db.head_block_time() + db.get_global_properties().parameters.block_interval * 10, true );
   // i. e. 8 blocks are missed in between by witness[4..11%10]
   for( uint32_t i = 0; i < witnesses.size(); i++ )
      BOOST_CHECK_EQUAL( (i+7) % 10 < 2 ? 0 : 1, witnesses[i](db).total_missed );
} FC_LOG_AND_RETHROW() }

BOOST_FIXTURE_TEST_CASE( miss_many_blocks, database_fixture )
{
   try
   {
      auto get_misses = []( database& db ) {
         std::map< witness_id_type, uint32_t > misses;
         for( const auto& witness_id : witness_schedule_id_type()(db).current_shuffled_witnesses )
            misses[witness_id] = witness_id(db).total_missed;
         return misses;
      };
      generate_block();
      generate_block();
      generate_block();
      auto missed_before = get_misses( db );
      // miss 10 maintenance intervals
      generate_blocks( db.get_dynamic_global_properties().next_maintenance_time + db.get_global_properties().parameters.maintenance_interval * 10, true );
      generate_block();
      generate_block();
      generate_block();
      auto missed_after = get_misses( db );
      BOOST_CHECK_EQUAL( missed_before.size(), missed_after.size() );
      for( const auto& miss : missed_before )
      {
          const auto& after = missed_after.find( miss.first );
          BOOST_REQUIRE( after != missed_after.end() );
          BOOST_CHECK_EQUAL( miss.second, after->second );
      }
   }
   catch (fc::exception& e)
   {
      edump((e.to_detail_string()));
      throw;
   }
}

BOOST_FIXTURE_TEST_CASE( update_account_keys, database_fixture )
{
   try
   {
      const asset_object& core = asset_id_type()(db);
      uint32_t skip_flags =
          database::skip_transaction_dupe_check
        | database::skip_witness_signature
        | database::skip_transaction_signatures
        | database::skip_authority_check
        ;

      // Sam is the creator of accounts
      private_key_type committee_key = init_account_priv_key;
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

      // upgrade sam to LTM
      upgrade_to_lifetime_member(sam_account_object.id);

      //Get a sane head block time
      generate_block( skip_flags );

      db.modify(db.get_global_properties(), [](global_property_object& p) {
         p.parameters.committee_proposal_review_period = fc::hours(1).to_seconds();
      });

      transaction tx;
      processed_transaction ptx;

      account_object committee_account_object = committee_account(db);
      // transfer from committee account to Sam account
      transfer(committee_account_object, sam_account_object, core.amount(100000));

      const int num_keys = 5;
      vector< private_key_type > numbered_private_keys;
      vector< vector< public_key_type > > numbered_key_id;
      numbered_private_keys.reserve( num_keys );
      numbered_key_id.push_back( vector<public_key_type>() );
      numbered_key_id.push_back( vector<public_key_type>() );

      for( int i=0; i<num_keys; i++ )
      {
         private_key_type privkey = generate_private_key(std::string("key_") + std::to_string(i));
         public_key_type pubkey = privkey.get_public_key();
         address addr( pubkey );

         numbered_private_keys.push_back( privkey );
         numbered_key_id[0].push_back( pubkey );
         //numbered_key_id[1].push_back( addr );
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

      // Originally we had a loop here to go from use_address=0 to 1
      // Live chain do not allow this so it had to be removed: https://github.com/bitshares/bitshares-core/issues/565
      vector< public_key_type > key_ids = numbered_key_id[ 0 ];
      for( int num_owner_keys=1; num_owner_keys<=2; num_owner_keys++ )
      {
         for( int num_active_keys=1; num_active_keys<=2; num_active_keys++ )
         {
            std::cout << 0 << num_owner_keys << num_active_keys << "\n";
            for( const vector< int >& key_sched_before : possible_key_sched )
            {
               auto it = key_sched_before.begin();
               vector< const private_key_type* > owner_privkey;
               vector< const public_key_type* > owner_keyid;
               owner_privkey.reserve( num_owner_keys );

               trx.clear();
               account_create_operation create_op;
               create_op.name = "alice";

               for( int owner_index=0; owner_index<num_owner_keys; owner_index++ )
               {
                  int i = *(it++);
                  create_op.owner.key_auths[ key_ids[ i ] ] = 1;
                  owner_privkey.push_back( &numbered_private_keys[i] );
                  owner_keyid.push_back( &key_ids[ i ] );
               }
               // size() < num_owner_keys is possible when some keys are duplicates
               create_op.owner.weight_threshold = create_op.owner.key_auths.size();

               for( int active_index=0; active_index<num_active_keys; active_index++ )
                  create_op.active.key_auths[ key_ids[ *(it++) ] ] = 1;
               // size() < num_active_keys is possible when some keys are duplicates
               create_op.active.weight_threshold = create_op.active.key_auths.size();

               create_op.options.memo_key = key_ids[ *(it++) ] ;
               create_op.registrar = sam_account_object.id;
               trx.operations.push_back( create_op );
               // trx.sign( sam_key );

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
                  update_op.new_options = create_op.options;

                  for( int owner_index=0; owner_index<num_owner_keys; owner_index++ )
                     update_op.owner->key_auths[ key_ids[ *(it++) ] ] = 1;
                  // size() < num_owner_keys is possible when some keys are duplicates
                  update_op.owner->weight_threshold = update_op.owner->key_auths.size();
                  for( int active_index=0; active_index<num_active_keys; active_index++ )
                     update_op.active->key_auths[ key_ids[ *(it++) ] ] = 1;
                  // size() < num_active_keys is possible when some keys are duplicates
                  update_op.active->weight_threshold = update_op.active->key_auths.size();
                  FC_ASSERT( update_op.new_options.valid() );
                  update_op.new_options->memo_key = key_ids[ *(it++) ] ;

                  trx.operations.push_back( update_op );
                  for( int i=0; i<int(create_op.owner.weight_threshold); i++)
                  {
                     sign( trx, *owner_privkey[i] );
                     if( i < int(create_op.owner.weight_threshold-1) )
                     {
                        GRAPHENE_REQUIRE_THROW(db.push_transaction(trx), fc::exception);
                     }
                     else
                     {
                        db.push_transaction( trx,
                           database::skip_transaction_dupe_check |
                           database::skip_transaction_signatures );
                     }
                  }
                  generate_block( skip_flags );

                  db.pop_block();
               }
               db.pop_block();
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

// The next test is commented out as it will fail in current bitshares implementaton
// where "witnesses should never sign 2 consecutive blocks" is not enforced.
// https://github.com/bitshares/bitshares-core/issues/565
// Leaving it here to use it if we implement.later

/**
 *  To have a secure random number we need to ensure that the same
 *  witness does not get to produce two blocks in a row.  There is
 *  always a chance that the last witness of one round will be the
 *  first witness of the next round.
 *
 *  This means that when we shuffle witness we need to make sure
 *  that there is at least N/2 witness between consecutive turns
 *  of the same witness.    This means that durring the random
 *  shuffle we need to restrict the placement of witness to maintain
 *  this invariant.
 *
 *  This test checks the requirement using Monte Carlo approach
 *  (produce lots of blocks and check the invariant holds).
 */
/*
BOOST_FIXTURE_TEST_CASE( witness_order_mc_test, database_fixture )
{
   try {
      size_t num_witnesses = db.get_global_properties().active_witnesses.size();
      size_t dmin = num_witnesses >> 1;

      vector< witness_id_type > cur_round;
      vector< witness_id_type > full_schedule;
      // if we make the maximum witness count testable,
      // we'll need to enlarge this.
      std::bitset< 0x40 > witness_seen;
      size_t total_blocks = 1000000;

      cur_round.reserve( num_witnesses );
      full_schedule.reserve( total_blocks );
      cur_round.push_back( db.get_dynamic_global_properties().current_witness );

      // we assert so the test doesn't continue, which would
      // corrupt memory
      assert( num_witnesses <= witness_seen.size() );

      while( full_schedule.size() < total_blocks )
      {
         if( (db.head_block_num() & 0x3FFF) == 0 )
         {
             wdump( (db.head_block_num()) );
         }
         witness_id_type wid = db.get_scheduled_witness( 1 );
         full_schedule.push_back( wid );
         cur_round.push_back( wid );
         if( cur_round.size() == num_witnesses )
         {
            // check that the current round contains exactly 1 copy
            // of each witness
            witness_seen.reset();
            for( const witness_id_type& w : cur_round )
            {
               uint64_t inst = w.instance.value;
               BOOST_CHECK( !witness_seen.test( inst ) );
               assert( !witness_seen.test( inst ) );
               witness_seen.set( inst );
            }
            cur_round.clear();
         }
         generate_block();
      }

      for( size_t i=0,m=full_schedule.size(); i<m; i++ )
      {
         for( size_t j=i+1,n=std::min( m, i+dmin ); j<n; j++ )
         {
            BOOST_CHECK( full_schedule[i] != full_schedule[j] );
            assert( full_schedule[i] != full_schedule[j] );
         }
      }

   } catch (fc::exception& e) {
      edump((e.to_detail_string()));
      throw;
   }
}
*/

BOOST_FIXTURE_TEST_CASE( tapos_rollover, database_fixture )
{
   try
   {
      ACTORS((alice)(bob));

      BOOST_TEST_MESSAGE( "Give Alice some money" );
      transfer(committee_account, alice_id, asset(10000));
      generate_block();

      BOOST_TEST_MESSAGE( "Generate up to block 0xFF00" );
      generate_blocks( 0xFF00 );
      signed_transaction xfer_tx;

      BOOST_TEST_MESSAGE( "Transfer money at/about 0xFF00" );
      transfer_operation xfer_op;
      xfer_op.from = alice_id;
      xfer_op.to = bob_id;
      xfer_op.amount = asset(1000);

      xfer_tx.operations.push_back( xfer_op );
      xfer_tx.set_expiration( db.head_block_time() + fc::seconds( 0x1000 * db.get_global_properties().parameters.block_interval ) );
      xfer_tx.set_reference_block( db.head_block_id() );

      sign( xfer_tx, alice_private_key );
      PUSH_TX( db, xfer_tx, 0 );
      generate_block();

      BOOST_TEST_MESSAGE( "Sign new tx's" );
      xfer_tx.set_expiration( db.head_block_time() + fc::seconds( 0x1000 * db.get_global_properties().parameters.block_interval ) );
      xfer_tx.set_reference_block( db.head_block_id() );
      xfer_tx.clear_signatures();
      sign( xfer_tx, alice_private_key );

      BOOST_TEST_MESSAGE( "Generate up to block 0x10010" );
      generate_blocks( 0x110 );

      BOOST_TEST_MESSAGE( "Transfer at/about block 0x10010 using reference block at/about 0xFF00" );
      PUSH_TX( db, xfer_tx, 0 );
      generate_block();
   }
   catch (fc::exception& e)
   {
      edump((e.to_detail_string()));
      throw;
   }
}

BOOST_FIXTURE_TEST_CASE( temp_account_balance, database_fixture )
{ try {
   ACTORS( (alice) );
   fund( alice );
   create_user_issued_asset( "UIA" );

   generate_block();
   set_expiration( db, trx );

   transfer_operation top;
   top.amount = asset( 1000 );
   top.from = alice_id;
   top.to   = GRAPHENE_TEMP_ACCOUNT;
   trx.operations.push_back( top );

   limit_order_create_operation loc;
   loc.amount_to_sell = top.amount;
   loc.expiration = db.head_block_time() + 1;
   loc.seller = GRAPHENE_TEMP_ACCOUNT;
   loc.min_to_receive = asset( 1000, asset_id_type(1) );
   trx.operations.push_back( loc );
   sign( trx, alice_private_key );
   PUSH_TX( db, trx );
   trx.clear();

   generate_block();
   generate_block();
   generate_block();

   top.to = GRAPHENE_COMMITTEE_ACCOUNT;
   trx.operations.push_back( top );
   sign( trx, alice_private_key );
   BOOST_CHECK_THROW( PUSH_TX( db, trx ), fc::assert_exception );

   generate_blocks( HARDFORK_CORE_1040_TIME );

   set_expiration( db, trx );
   trx.clear_signatures();
   sign( trx, alice_private_key );
   PUSH_TX( db, trx );

   BOOST_CHECK( get_balance( GRAPHENE_TEMP_ACCOUNT, asset_id_type() ) > 0 );
} FC_LOG_AND_RETHROW() }

///
/// This test case tries to
/// * generate blocks when there are too many pending transactions,
/// * push blocks that are too large.
/// If we add some logging in signed_transaction::get_signature_keys(), we can see if the code will extract public key(s)
/// from signature(s) of same transactions multiple times.
/// See https://github.com/bitshares/bitshares-core/pull/1251
///
BOOST_FIXTURE_TEST_CASE( block_size_test, database_fixture )
{
   try
   {
      ACTORS((alice)(bob));

      const fc::ecc::private_key& key = generate_private_key("null_key");
      BOOST_TEST_MESSAGE( "Give Alice some money" );
      transfer(committee_account, alice_id, asset(10000000));
      generate_block();

      const size_t default_block_header_size = fc::raw::pack_size( signed_block_header() );
      const auto& gpo = db.get_global_properties();
      const auto block_interval = gpo.parameters.block_interval;
      idump( (db.head_block_num())(default_block_header_size)(gpo.parameters.maximum_block_size) );

      BOOST_TEST_MESSAGE( "Start" );
      // Note: a signed transaction with a transfer operation inside is at least 102 bytes;
      //       after processed, it become 103 bytes;
      //       an empty block is 112 bytes;
      //       a block with a transfer is 215 bytes;
      //       a block with 2 transfers is 318 bytes.
      uint32_t large_block_count = 0;
      for( uint64_t i = 90; i <= 230; ++i )
      {
         if( i > 120 && i < 200 ) // skip some
            i = 200;

         // Temporarily disable undo db and change max block size
         db._undo_db.disable();
         db.modify( gpo, [i,&default_block_header_size](global_property_object& p) {
            p.parameters.maximum_block_size = default_block_header_size + i;
         });
         db._undo_db.enable();
         idump( (i)(gpo.parameters.maximum_block_size) );

         // push a transaction
         signed_transaction xfer_tx;
         transfer_operation xfer_op;
         xfer_op.from = alice_id;
         xfer_op.to = bob_id;
         xfer_op.amount = asset(i);
         xfer_tx.operations.push_back( xfer_op );
         xfer_tx.set_expiration( db.head_block_time() + fc::seconds( 0x1000 * block_interval ) );
         xfer_tx.set_reference_block( db.head_block_id() );
         sign( xfer_tx, alice_private_key );
         auto processed_tx = PUSH_TX( db, xfer_tx, database::skip_nothing );

         // sign a temporary block
         signed_block maybe_large_block;
         maybe_large_block.transactions.push_back(processed_tx);
         maybe_large_block.previous = db.head_block_id();
         maybe_large_block.timestamp = db.get_slot_time(1);
         maybe_large_block.transaction_merkle_root = maybe_large_block.calculate_merkle_root();
         maybe_large_block.witness = db.get_scheduled_witness(1);
         maybe_large_block.sign(key);
         auto maybe_large_block_size = fc::raw::pack_size(maybe_large_block);
         idump( (maybe_large_block_size) );

         // should fail to push if it's too large
         if( maybe_large_block_size > gpo.parameters.maximum_block_size )
         {
            ++large_block_count;
            BOOST_CHECK_THROW( db.push_block(maybe_large_block), fc::exception );
         }

         // generate a block normally
         auto good_block = db.generate_block( db.get_slot_time(1), db.get_scheduled_witness(1), key, database::skip_nothing );
         idump( (fc::raw::pack_size(good_block)) );
      }
      // make sure we have tested at least once pushing a large block
      BOOST_CHECK_GT( large_block_count, 0 );
   }
   catch( fc::exception& e )
   {
      edump((e.to_detail_string()));
      throw;
   }
}

BOOST_AUTO_TEST_SUITE_END()
