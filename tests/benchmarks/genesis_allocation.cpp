/*
 * Copyright (c) 2015 Cryptonomex, Inc., and contributors.  All rights reserved.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */
#include <graphene/chain/database.hpp>
#include <graphene/chain/account_object.hpp>
#include <graphene/utilities/tempdir.hpp>

#include <graphene/time/time.hpp>

#include <fc/crypto/digest.hpp>
#include <fc/smart_ref_impl.hpp>

#include <boost/test/auto_unit_test.hpp>

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
      genesis_state_type genesis_state;

#ifdef NDEBUG
      ilog("Running in release mode.");
      const int account_count = 2000000;
      const int blocks_to_produce = 1000000;
#else
      ilog("Running in debug mode.");
      const int account_count = 30000;
      const int blocks_to_produce = 1000;
#endif

      for( int i = 0; i < account_count; ++i )
         genesis_state.initial_accounts.emplace_back("target"+fc::to_string(i),
                                                     public_key_type(fc::ecc::private_key::regenerate(fc::digest(i)).get_public_key()));

      fc::temp_directory data_dir( graphene::utilities::temp_directory_path() );

      {
         database db;
         db.open(data_dir.path(), [&]{return genesis_state;});

         for( int i = 11; i < account_count + 11; ++i)
            BOOST_CHECK(db.get_balance(account_id_type(i), asset_id_type()).amount == GRAPHENE_MAX_SHARE_SUPPLY / account_count);

         fc::time_point start_time = fc::time_point::now();
         db.close();
         ilog("Closed database in ${t} milliseconds.", ("t", (fc::time_point::now() - start_time).count() / 1000));
      }
      {
         database db;

         fc::time_point start_time = fc::time_point::now();
         db.open(data_dir.path(), [&]{return genesis_state;});
         ilog("Opened database in ${t} milliseconds.", ("t", (fc::time_point::now() - start_time).count() / 1000));

         for( int i = 11; i < account_count + 11; ++i)
            BOOST_CHECK(db.get_balance(account_id_type(i), asset_id_type()).amount == GRAPHENE_MAX_SHARE_SUPPLY / account_count);

         int blocks_out = 0;
         auto witness_priv_key = fc::ecc::private_key::regenerate(fc::sha256::hash(string("null_key")) );
         auto aw = db.get_global_properties().active_witnesses;
         auto b =  db.generate_block( db.get_slot_time( 1 ), db.get_scheduled_witness( 1 ), witness_priv_key, ~0 );

         start_time = fc::time_point::now();
         /* TODO: get this buliding again
         for( int i = 0; i < blocks_to_produce; ++i )
         {
            signed_transaction trx;
            trx.operations.emplace_back(transfer_operation(asset(1), account_id_type(i + 11), account_id_type(), asset(1), memo_data()));
            db.push_transaction(trx, ~0);

            aw = db.get_global_properties().active_witnesses;
            b =  db.generate_block( db.get_slot_time( 1 ), db.get_scheduled_witness( 1 ), witness_priv_key, ~0 );
         }
         */
         ilog("Pushed ${c} blocks (1 op each, no validation) in ${t} milliseconds.",
              ("c", blocks_out)("t", (fc::time_point::now() - start_time).count() / 1000));

         start_time = fc::time_point::now();
         db.close();
         ilog("Closed database in ${t} milliseconds.", ("t", (fc::time_point::now() - start_time).count() / 1000));
      }
      {
         database db;

         auto start_time = fc::time_point::now();
         wlog( "about to start reindex..." );
         db.reindex(data_dir.path(), genesis_state);
         ilog("Replayed database in ${t} milliseconds.", ("t", (fc::time_point::now() - start_time).count() / 1000));

         for( int i = 0; i < blocks_to_produce; ++i )
            BOOST_CHECK(db.get_balance(account_id_type(i + 11), asset_id_type()).amount == GRAPHENE_MAX_SHARE_SUPPLY / account_count - 2);
      }

   } catch(fc::exception& e) {
      edump((e.to_detail_string()));
      throw;
   }
}
