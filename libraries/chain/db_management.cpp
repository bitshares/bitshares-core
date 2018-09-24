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

#include <graphene/chain/chain_property_object.hpp>
#include <graphene/chain/witness_schedule_object.hpp>
#include <graphene/chain/special_authority_object.hpp>
#include <graphene/chain/operation_history_object.hpp>
#include <graphene/chain/protocol/fee_schedule.hpp>

#include <fc/io/fstream.hpp>

#include <fstream>
#include <functional>
#include <iostream>

namespace graphene { namespace chain {

database::database()
{
   initialize_indexes();
   initialize_evaluators();
}

database::~database()
{
   clear_pending();
}

void database::reindex( fc::path data_dir )
{ try {
   auto last_block = _block_id_to_block.last();
   if( !last_block ) {
      elog( "!no last block" );
      edump((last_block));
      return;
   }
   if( last_block->block_num() <= head_block_num()) return;

   ilog( "reindexing blockchain" );
   auto start = fc::time_point::now();
   const auto last_block_num = last_block->block_num();
   uint32_t flush_point = last_block_num < 10000 ? 0 : last_block_num - 10000;
   uint32_t undo_point = last_block_num < 50 ? 0 : last_block_num - 50;

   ilog( "Replaying blocks, starting at ${next}...", ("next",head_block_num() + 1) );
   if( head_block_num() >= undo_point )
   {
      if( head_block_num() > 0 )
         _fork_db.start_block( *fetch_block_by_number( head_block_num() ) );
   }
   else
      _undo_db.disable();

   uint32_t skip = skip_witness_signature |
                   skip_block_size_check |
                   skip_transaction_signatures |
                   skip_transaction_dupe_check |
                   skip_tapos_check |
                   skip_witness_schedule_check |
                   skip_authority_check;
   for( uint32_t i = head_block_num() + 1; i <= last_block_num; ++i )
   {
      if( i % 10000 == 0 ) std::cerr << "   " << double(i*100)/last_block_num << "%   "<<i << " of " <<last_block_num<<"   \n";
      if( i == flush_point )
      {
         ilog( "Writing database to disk at block ${i}", ("i",i) );
         flush();
         ilog( "Done" );
      }
      fc::optional< signed_block > block = _block_id_to_block.fetch_by_number(i);
      if( !block.valid() )
      {
         wlog( "Reindexing terminated due to gap:  Block ${i} does not exist!", ("i", i) );
         uint32_t dropped_count = 0;
         while( true )
         {
            fc::optional< block_id_type > last_id = _block_id_to_block.last_id();
            // this can trigger if we attempt to e.g. read a file that has block #2 but no block #1
            if( !last_id.valid() )
               break;
            // we've caught up to the gap
            if( block_header::num_from_id( *last_id ) <= i )
               break;
            _block_id_to_block.remove( *last_id );
            dropped_count++;
         }
         wlog( "Dropped ${n} blocks from after the gap", ("n", dropped_count) );
         break;
      }
      if( i < undo_point )
         apply_block( *block, skip );
      else
      {
         _undo_db.enable();
         push_block( *block, skip );
      }
   }
   _undo_db.enable();
   auto end = fc::time_point::now();
   ilog( "Done reindexing, elapsed time: ${t} sec", ("t",double((end-start).count())/1000000.0 ) );
} FC_CAPTURE_AND_RETHROW( (data_dir) ) }

void database::wipe(const fc::path& data_dir, bool include_blocks)
{
   ilog("Wiping database", ("include_blocks", include_blocks));
   if (_opened) {
     close();
   }
   object_database::wipe(data_dir);
   if( include_blocks )
      fc::remove_all( data_dir / "database" );
}

void database::open(
   const fc::path& data_dir,
   std::function<genesis_state_type()> genesis_loader,
   const std::string& db_version)
{
   try
   {
      bool wipe_object_db = false;
      if( !fc::exists( data_dir / "db_version" ) )
         wipe_object_db = true;
      else
      {
         std::string version_string;
         fc::read_file_contents( data_dir / "db_version", version_string );
         wipe_object_db = ( version_string != db_version );
      }
      if( wipe_object_db ) {
          ilog("Wiping object_database due to missing or wrong version");
          object_database::wipe( data_dir );
          std::ofstream version_file( (data_dir / "db_version").generic_string().c_str(),
                                      std::ios::out | std::ios::binary | std::ios::trunc );
          version_file.write( db_version.c_str(), db_version.size() );
          version_file.close();
      }

      object_database::open(data_dir);

      _block_id_to_block.open(data_dir / "database" / "block_num_to_block");

      if( !find(global_property_id_type()) )
         init_genesis(genesis_loader());
      else
      {
         _p_core_asset_obj = &get( asset_id_type() );
         _p_core_dynamic_data_obj = &get( asset_dynamic_data_id_type() );
         _p_global_prop_obj = &get( global_property_id_type() );
         _p_chain_property_obj = &get( chain_property_id_type() );
         _p_dyn_global_prop_obj = &get( dynamic_global_property_id_type() );
         _p_witness_schedule_obj = &get( witness_schedule_id_type() );
      }

      fc::optional<block_id_type> last_block = _block_id_to_block.last_id();
      if( last_block.valid() )
      {
         FC_ASSERT( *last_block >= head_block_id(),
                    "last block ID does not match current chain state",
                    ("last_block->id", last_block)("head_block_id",head_block_num()) );
         reindex( data_dir );
      }
      _opened = true;
   }
   FC_CAPTURE_LOG_AND_RETHROW( (data_dir) )
}

void database::close(bool rewind)
{
   // TODO:  Save pending tx's on close()
   clear_pending();

   // pop all of the blocks that we can given our undo history, this should
   // throw when there is no more undo history to pop
   if( rewind )
   {
      try
      {
         uint32_t cutoff = get_dynamic_global_properties().last_irreversible_block_num;

         ilog( "Rewinding from ${head} to ${cutoff}", ("head",head_block_num())("cutoff",cutoff) );
         while( head_block_num() > cutoff )
         {
            block_id_type popped_block_id = head_block_id();
            pop_block();
            _fork_db.remove(popped_block_id); // doesn't throw on missing
         }
      }
      catch ( const fc::exception& e )
      {
         wlog( "Database close unexpected exception: ${e}", ("e", e) );
      }
   }

   // Since pop_block() will move tx's in the popped blocks into pending,
   // we have to clear_pending() after we're done popping to get a clean
   // DB state (issue #336).
   clear_pending();

   // ************************************************************************************************************** //
   // Issue Number:           #946                                                                                   //
   // Issue Title:            Possible to save unclean object database to file during replay                         //
   // Issue URL:              https://github.com/bitshares/bitshares-core/issues/946                                 //
   // ************************************************************************************************************** //
   // Issue Description:      During replay, undo_db is disabled, that said, when there is an exception thrown,      //
   // changes made to object database won't rewound, which means the in-memory object database would be unclean.     //
   // When caught the exception, the node will dump object database from memory to disk.                             //
   // That means the on-disk object database would be unclean.                                                       //
   // On next (and future) startup, if does not specify --replay-blockchain,                                         //
   // the on-disk object database would be considered clean and be loaded into memory directly.                      //
   // ************************************************************************************************************** //
   // Issue Solution:         Check if was caught exception during replay with disabled undo_db,                     //
   // then we shouldn't flush object database, otherwise if undo_db is enabled we have to flush                      //
   // object database even if there was caught exception, also if there wasn't caught exception we have to flush     //
   // object database even if undo_db is disabled.                                                                   //
   // ************************************************************************************************************** //
   // Issue Conclusion:       We should NOT flush object database when:                                              //
   //                                  1. undo_db is disabled                                                        //
   //                                           AND                                                                  //
   //                                  2. was caught exception                                                       //
   //                                                                                                                //
   //                         We should flush object database when:                                                  //
   //                                  1. undo_db is enabled                                                         //
   //                                           OR                                                                   //
   //                                  2. wasn't caught exception                                                    //
   // ************************************************************************************************************** //
   // Issue summary:          There is a method std::current_exception() for checking caught exception, at the same  //
   // time it depends on error handling implementation. Currently is implemented throw/catch approach in which       //
   // are disadvantages like: performance overhead, exceptions aren't supported by all platforms,                    //
   // the biggest drawback is that handling exceptions is not enforced by the type-system.                           //
   // Unlike, Java, for example, where exceptions must be caught by the caller,                                      //
   // catching a C++ exception is optional. his means spotting all the unhandled exceptions during a code review     //
   // will be challenging, and requires deep knowledge of all of the functions called.                               //
   // Consider using an Either type to handle errors. They lift the error into the type-system,                      //
   // making them safer than exceptions whilst yielding the same performance characteristics as error-codes.         //
   //                                                                                                                //
   // https://hackernoon.com/error-handling-in-c-or-why-you-should-use-eithers-in-favor-of-exceptions-and-error-codes-f0640912eb45
   // https://github.com/LoopPerfect/neither (C++ Either implementation)                                             //
   //                                                                                                                //
   // See #1338 issue for Error Handling implementation by Eithers.                                                  //
   // https://github.com/bitshares/bitshares-core/issues/1338                                                        //
   // ************************************************************************************************************** //
   if (_undo_db.enabled() || !std::current_exception())
      object_database::flush();
   object_database::close();

   if( _block_id_to_block.is_open() )
      _block_id_to_block.close();

   _fork_db.reset();

   _opened = false;
}

} }
