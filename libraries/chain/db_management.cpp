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

#include <graphene/chain/database.hpp>

#include <graphene/chain/operation_history_object.hpp>
#include <graphene/chain/protocol/fee_schedule.hpp>

#include <functional>

namespace graphene { namespace chain {

database::database()
{
   initialize_indexes();
   initialize_evaluators();
}

database::~database(){
   if( _pending_block_session )
      _pending_block_session->commit();
}

void database::reindex(fc::path data_dir, const genesis_state_type& initial_allocation)
{ try {
   wipe(data_dir, false);
   open(data_dir, [&initial_allocation]{return initial_allocation;});

   auto start = fc::time_point::now();
   auto last_block = _block_id_to_block.last();
   if( !last_block ) return;

   const auto last_block_num = last_block->block_num();

   // TODO: disable undo tracking during reindex, this currently causes crashes in the benchmark test
   //_undo_db.disable();
   for( uint32_t i = 1; i <= last_block_num; ++i )
   {
      apply_block(*_block_id_to_block.fetch_by_number(i), skip_witness_signature |
                                skip_transaction_signatures |
                                skip_transaction_dupe_check |
                                skip_tapos_check |
                                skip_authority_check);
   }
   //_undo_db.enable();
   auto end = fc::time_point::now();
   wdump( ((end-start).count()/1000000.0) );
} FC_CAPTURE_AND_RETHROW( (data_dir) ) }

void database::wipe(const fc::path& data_dir, bool include_blocks)
{
   ilog("Wiping database", ("include_blocks", include_blocks));
   close();
   object_database::wipe(data_dir);
   if( include_blocks )
      fc::remove_all( data_dir / "database" );
}

void database::close(uint32_t blocks_to_rewind)
{
   _pending_block_session.reset();

   for(uint32_t i = 0; i < blocks_to_rewind && head_block_num() > 0; ++i)
      pop_block();

   object_database::flush();
   object_database::close();

   if( _block_id_to_block.is_open() )
      _block_id_to_block.close();

   _fork_db.reset();
}

} }
