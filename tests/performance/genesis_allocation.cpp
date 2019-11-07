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
#include <graphene/chain/database.hpp>
#include <graphene/chain/db_with.hpp>
#include <graphene/chain/account_object.hpp>
#include <graphene/utilities/tempdir.hpp>

#include <fc/crypto/digest.hpp>

#include <boost/test/auto_unit_test.hpp>

#include <database_fixture.hpp>

extern uint32_t GRAPHENE_TESTING_GENESIS_TIMESTAMP;

using namespace graphene::chain;

BOOST_AUTO_TEST_CASE( operation_sanity_check )
{
   try {
      operation op = account_create_operation();
      op.get<account_create_operation>().active.add_authority(account_id_type(), 123);
      operation tmp = std::move(op);
      wdump((tmp.which()));
   } catch (fc::exception& e) {
      edump((e.to_detail_string()));
      throw;
   }
}

BOOST_AUTO_TEST_CASE( genesis_and_persistence_bench )
{
   try {
      const auto witness_priv_key = fc::ecc::private_key::regenerate(fc::sha256::hash(string("null_key")) );
      const auto witness_pub_key = witness_priv_key.get_public_key();

      genesis_state_type genesis_state;
      genesis_state.initial_timestamp = fc::time_point_sec( GRAPHENE_TESTING_GENESIS_TIMESTAMP );
      genesis_state.initial_parameters.get_mutable_fees().zero_all_fees();
      genesis_state.initial_active_witnesses = 10;
      genesis_state.initial_chain_id = fc::sha256::hash(string("dummy_id"));
      for( unsigned int i = 0; i < genesis_state.initial_active_witnesses; ++i )
      {
         auto name = "init"+fc::to_string(i);
         genesis_state.initial_accounts.emplace_back(name, witness_pub_key, witness_pub_key, true);
         genesis_state.initial_committee_candidates.push_back({name});
         genesis_state.initial_witness_candidates.push_back({name, witness_pub_key});
      }

#ifdef NDEBUG
      ilog("Running in release mode.");
      const int account_count = 2000000;
      const int blocks_to_produce = 1000000;
#else
      ilog("Running in debug mode.");
      const int account_count = 30000;
      const int blocks_to_produce = 1000;
#endif

      const auto account_pub_key = fc::ecc::private_key::regenerate(fc::digest(account_count)).get_public_key();
      for( int i = 0; i < account_count; ++i )
         genesis_state.initial_accounts.emplace_back("target"+fc::to_string(i),
                                                     public_key_type(account_pub_key));

      fc::temp_directory data_dir( graphene::utilities::temp_directory_path() );

      {
         database db;
         db.open(data_dir.path(), [&genesis_state]{return genesis_state;}, "test");

         for( int i = 11; i < account_count + 11; ++i)
            BOOST_CHECK(db.get_balance(account_id_type(i), asset_id_type()).amount == 0);

         fc::time_point start_time = fc::time_point::now();
         db.close();
         ilog("Closed database in ${t} milliseconds.", ("t", (fc::time_point::now() - start_time).count() / 1000));
      }
      {
         database db;

         fc::time_point start_time = fc::time_point::now();
         db.open(data_dir.path(), [&genesis_state]{return genesis_state;}, "test");
         ilog("Opened database in ${t} milliseconds.", ("t", (fc::time_point::now() - start_time).count() / 1000));

         for( int i = 11; i < account_count + 11; ++i)
            BOOST_CHECK(db.get_balance(account_id_type(i), asset_id_type()).amount == 0);

         int blocks_out = 0;
         db.generate_block( db.get_slot_time( 1 ), db.get_scheduled_witness( 1 ), witness_priv_key, ~0 );

         start_time = fc::time_point::now();
         transfer_operation top;
         top.amount = asset(1);
         top.from = account_id_type();
         for( int i = 0; i < blocks_to_produce; ++i )
         {
            signed_transaction trx;
            test::set_expiration( db, trx );
            top.to = account_id_type(i + 11);
            trx.operations.push_back( top );
            db.push_transaction(trx, ~graphene::chain::database::skip_transaction_dupe_check);
            db.generate_block( db.get_slot_time( 1 ), db.get_scheduled_witness( 1 ), witness_priv_key,
                               ~graphene::chain::database::skip_transaction_dupe_check );
         }
         ilog("Pushed ${c} blocks (1 op each, no validation) in ${t} milliseconds.",
              ("c", blocks_out)("t", (fc::time_point::now() - start_time).count() / 1000));

         for( int i = 0; i < blocks_to_produce; ++i )
            BOOST_CHECK_EQUAL( 1, db.get_balance(account_id_type(i + 11), asset_id_type()).amount.value );

         start_time = fc::time_point::now();
         db.close();
         ilog("Closed database in ${t} milliseconds.", ("t", (fc::time_point::now() - start_time).count() / 1000));
      }
      {
         database db;
         const auto skip = graphene::chain::database::skip_witness_signature |
                graphene::chain::database::skip_block_size_check |
                graphene::chain::database::skip_merkle_check |
                graphene::chain::database::skip_transaction_signatures |
                graphene::chain::database::skip_transaction_dupe_check |
                graphene::chain::database::skip_tapos_check |
                graphene::chain::database::skip_witness_schedule_check;

         auto start_time = fc::time_point::now();
         wlog( "about to start reindex..." );
         graphene::chain::detail::with_skip_flags( db, skip, [&data_dir,&db,&genesis_state] () {
            db.open(data_dir.path(), [&genesis_state]{return genesis_state;}, "force_wipe");
         });
        
         ilog("Replayed database in ${t} milliseconds.", ("t", (fc::time_point::now() - start_time).count() / 1000));

         for( int i = 0; i < blocks_to_produce; ++i )
            BOOST_CHECK( db.get_balance(account_id_type(i + 11), asset_id_type()).amount == 1 );
      }

   } catch(fc::exception& e) {
      edump((e.to_detail_string()));
      throw;
   }
}
