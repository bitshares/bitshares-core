/*
 * Copyright (c) 2015 Cryptonomex, Inc., and contributors.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:
 *
 * 1. Any modified source or binaries are used only with the BitShares network.
 *
 * 2. Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
 *
 * 3. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */
#pragma once
#include <graphene/chain/protocol/block.hpp>

#include <boost/multi_index_container.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/mem_fun.hpp>


namespace graphene { namespace chain {
   using boost::multi_index_container;
   using namespace boost::multi_index;

   struct fork_item
   {
      fork_item( signed_block d )
      :num(d.block_num()),id(d.id()),data( std::move(d) ){}

      block_id_type previous_id()const { return data.previous; }

      weak_ptr< fork_item > prev;
      uint32_t              num;    // initialized in ctor
      /**
       * Used to flag a block as invalid and prevent other blocks from
       * building on top of it.
       */
      bool                  invalid = false;
      block_id_type         id;
      signed_block          data;
   };
   typedef shared_ptr<fork_item> item_ptr;


   /**
    *  As long as blocks are pushed in order the fork
    *  database will maintain a linked tree of all blocks
    *  that branch from the start_block.  The tree will
    *  have a maximum depth of 1024 blocks after which
    *  the database will start lopping off forks.
    *
    *  Every time a block is pushed into the fork DB the
    *  block with the highest block_num will be returned.
    */
   class fork_database
   {
      public:
         typedef vector<item_ptr> branch_type;
         /// The maximum number of blocks that may be skipped in an out-of-order push
         const static int MAX_BLOCK_REORDERING = 1024;

         fork_database();
         void reset();

         void                             start_block(signed_block b);
         void                             remove(block_id_type b);
         void                             set_head(shared_ptr<fork_item> h);
         bool                             is_known_block(const block_id_type& id)const;
         shared_ptr<fork_item>            fetch_block(const block_id_type& id)const;
         vector<item_ptr>                 fetch_block_by_number(uint32_t n)const;

         /**
          *  @return the new head block ( the longest fork )
          */
         shared_ptr<fork_item>            push_block(const signed_block& b);
         shared_ptr<fork_item>            head()const { return _head; }
         void                             pop_block();

         /**
          *  Given two head blocks, return two branches of the fork graph that
          *  end with a common ancestor (same prior block)
          */
         pair< branch_type, branch_type >  fetch_branch_from(block_id_type first,
                                                             block_id_type second)const;

         struct block_id;
         struct block_num;
         struct by_previous;
         typedef multi_index_container<
            item_ptr,
            indexed_by<
               hashed_unique<tag<block_id>, member<fork_item, block_id_type, &fork_item::id>, std::hash<fc::ripemd160>>,
               hashed_non_unique<tag<by_previous>, const_mem_fun<fork_item, block_id_type, &fork_item::previous_id>, std::hash<fc::ripemd160>>,
               ordered_non_unique<tag<block_num>, member<fork_item,uint32_t,&fork_item::num>>
            >
         > fork_multi_index_type;

         void set_max_size( uint32_t s );

      private:
         /** @return a pointer to the newly pushed item */
         void _push_block(const item_ptr& b );
         void _push_next(const item_ptr& newly_inserted);

         uint32_t                 _max_size = 1024;

         fork_multi_index_type    _unlinked_index;
         fork_multi_index_type    _index;
         shared_ptr<fork_item>    _head;
   };
} } // graphene::chain
