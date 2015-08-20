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
#include <graphene/chain/fork_database.hpp>
#include <graphene/chain/exceptions.hpp>
#include <graphene/chain/protocol/fee_schedule.hpp>
#include <fc/smart_ref_impl.hpp>

namespace graphene { namespace chain {
fork_database::fork_database()
{
}
void fork_database::reset()
{
   _head.reset();
   _index.clear();
}

void fork_database::pop_block()
{
   if( _head ) _head = _head->prev.lock();
}

void     fork_database::start_block(signed_block b)
{
   auto item = std::make_shared<fork_item>(std::move(b));
   _index.insert(item);
   _head = item;
}

/**
 * Pushes the block into the fork database and caches it if it doesn't link
 *
 */
shared_ptr<fork_item>  fork_database::push_block(const signed_block& b)
{
   auto item = std::make_shared<fork_item>(b);
   try {
      _push_block(item);
   } 
   catch ( const unlinkable_block_exception& e )
   {
      wlog( "Pushing block to fork database that failed to link." );
      _unlinked_index.insert( item );
   }
   return _head;
}

void  fork_database::_push_block(const item_ptr& item)
{
   if( _head ) // make sure the block is within the range that we are caching
   {
      FC_ASSERT( item->num > std::max<int64_t>( 0, int64_t(_head->num) - (_max_size) ),
                 "attempting to push a block that is too old", 
                 ("item->num",item->num)("head",_head->num)("max_size",_max_size));
      FC_ASSERT( item->num < _head->num + 32 );
   }

   if( _head && item->previous_id() != block_id_type() )
   {
      auto itr = _index.get<block_id>().find(item->previous_id());
      GRAPHENE_ASSERT(itr != _index.get<block_id>().end(), unlinkable_block_exception, "block does not link to known chain");
      FC_ASSERT(!(*itr)->invalid);
      item->prev = *itr;
   }

   _index.insert(item);
   if( !_head ) _head = item;
   else if( item->num > _head->num )
   {
      _head = item;
      _index.get<block_num>().erase(_head->num - _max_size);
      _unlinked_index.get<block_num>().erase(_head->num - _max_size);
   }

   _push_next( item );
}

/**
 *  Iterate through the unlinked cache and insert anything that
 *  links to the newly inserted item.  This will start a recursive
 *  set of calls performing a depth-first insertion of pending blocks as
 *  _push_next(..) calls _push_block(...) which will in turn call _push_next
 */
void fork_database::_push_next( const item_ptr& new_item )
{
    auto& prev_idx = _unlinked_index.get<by_previous>();
    vector<item_ptr> newly_inserted;

    auto itr = prev_idx.find( new_item->id );
    while( itr != prev_idx.end() )
    {
       auto tmp = *itr;
       prev_idx.erase( itr );
       _push_block( tmp ); 

       itr = prev_idx.find( new_item->id );
    }
}

void fork_database::set_max_size( uint32_t s )
{
   _max_size = s;
   if( !_head ) return;

   { /// index
      auto& by_num_idx = _index.get<block_num>();
      auto itr = by_num_idx.begin();
      while( itr != by_num_idx.end() )
      {
         if( (*itr)->num < std::max(int64_t(0),int64_t(_head->num) - _max_size) )
            by_num_idx.erase(itr);
         else
            break;
         itr = by_num_idx.begin();
      }
   }
   { /// unlinked_index
      auto& by_num_idx = _unlinked_index.get<block_num>();
      auto itr = by_num_idx.begin();
      while( itr != by_num_idx.end() )
      {
         if( (*itr)->num < std::max(int64_t(0),int64_t(_head->num) - _max_size) )
            by_num_idx.erase(itr);
         else
            break;
         itr = by_num_idx.begin();
      }
   }
}




bool fork_database::is_known_block(const block_id_type& id)const
{
   auto& index = _index.get<block_id>();
   auto itr = index.find(id);
   return itr != index.end();
}
item_ptr fork_database::fetch_block(const block_id_type& id)const
{
   auto itr = _index.get<block_id>().find(id);
   if( itr != _index.get<block_id>().end() )
      return *itr;
   return item_ptr();
}
vector<item_ptr> fork_database::fetch_block_by_number(uint32_t num)const
{
   vector<item_ptr> result;
   auto itr = _index.get<block_num>().find(num);
   while( itr != _index.get<block_num>().end() )
   {
      if( (*itr)->num == num )
         result.push_back( *itr );
      else
         break;
      ++itr;
   }
   return result;
}

pair<fork_database::branch_type,fork_database::branch_type>
  fork_database::fetch_branch_from(block_id_type first, block_id_type second)const
{ try {
   pair<branch_type,branch_type> result;
   auto first_branch_itr = _index.get<block_id>().find(first);
   FC_ASSERT(first_branch_itr != _index.get<block_id>().end());
   auto first_branch = *first_branch_itr;

   auto second_branch_itr = _index.get<block_id>().find(second);
   FC_ASSERT(second_branch_itr != _index.get<block_id>().end());
   auto second_branch = *second_branch_itr;


   while( first_branch->data.block_num() > second_branch->data.block_num() )
   {
      result.first.push_back(first_branch);
      first_branch = first_branch->prev.lock();
      FC_ASSERT(first_branch);
   }
   while( second_branch->data.block_num() > first_branch->data.block_num() )
   {
      result.second.push_back( second_branch );
      second_branch = second_branch->prev.lock();
      FC_ASSERT(second_branch);
   }
   while( first_branch->data.previous != second_branch->data.previous )
   {
      result.first.push_back(first_branch);
      result.second.push_back(second_branch);
      first_branch = first_branch->prev.lock();
      FC_ASSERT(first_branch);
      second_branch = second_branch->prev.lock();
      FC_ASSERT(second_branch);
   }
   if( first_branch && second_branch )
   {
      result.first.push_back(first_branch);
      result.second.push_back(second_branch);
   }
   return result;
} FC_CAPTURE_AND_RETHROW( (first)(second) ) }

void fork_database::set_head(shared_ptr<fork_item> h)
{
   _head = h;
}

void fork_database::remove(block_id_type id)
{
   _index.get<block_id>().erase(id);
}

} } // graphene::chain
