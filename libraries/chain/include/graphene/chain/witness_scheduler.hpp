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
#pragma once

#include <algorithm>
#include <deque>
#include <memory>
#include <vector>

#include <boost/container/flat_set.hpp>

namespace graphene { namespace chain {

using boost::container::flat_set;

enum witness_scheduler_relax_flags
{
   emit_turn            = 0x01,
   emit_token           = 0x02
};

template< typename WitnessID, typename RNG, typename CountType, typename OffsetType, bool debug = true >
class generic_witness_scheduler
{
   public:
      void check_invariant() const
      {
#ifndef NDEBUG
         CountType tokens = _ineligible_no_turn.size() + _eligible.size();
         CountType turns = _eligible.size();
         for( const std::pair< WitnessID, bool >& item : _ineligible_waiting_for_token )
            turns += (item.second ? 1 : 0 );

         assert( _tokens == tokens );
         assert( _turns == turns   );
#endif

         flat_set< WitnessID > witness_set;
         // make sure each witness_id occurs only once among the three states
         auto process_id = [&]( WitnessID item )
         {
            assert( witness_set.find( item ) == witness_set.end() );
            witness_set.insert( item );
         } ;

         for( const std::pair< WitnessID, bool >& item : _ineligible_waiting_for_token )
            process_id( item.first );
         for( const WitnessID& item : _ineligible_no_turn )
            process_id( item );
         for( const WitnessID& item : _eligible )
            process_id( item );
         return;
      }

      /**
       * Deterministically evolve over time
       */
      uint32_t relax()
      {
         uint32_t relax_flags = 0;

         if( debug ) check_invariant();
         assert( _min_token_count > 0 );

         // turn distribution
         if( _turns == 0 )
         {
            relax_flags |= emit_turn;
            for( const WitnessID& item : _ineligible_no_turn )
                _eligible.push_back( item );
            _turns += _ineligible_no_turn.size();
            _ineligible_no_turn.clear();
            if( debug ) check_invariant();

            for( std::pair< WitnessID, bool >& item : _ineligible_waiting_for_token )
            {
                assert( item.second == false );
                item.second = true;
            }
            _turns += _ineligible_waiting_for_token.size();
            if( debug ) check_invariant();
         }

         // token distribution
         while( true )
         {
            if( _ineligible_waiting_for_token.empty() )
            {
               // eligible must be non-empty
               assert( !_eligible.empty() );
               return relax_flags;
            }

            if( _tokens >= _min_token_count )
            {
               if( !_eligible.empty() )
                  return relax_flags;
            }

            const std::pair< WitnessID, bool >& item = _ineligible_waiting_for_token.front();
            if( item.second )
               _eligible.push_back( item.first );
            else
               _ineligible_no_turn.push_back( item.first );
            _ineligible_waiting_for_token.pop_front();
            relax_flags |= emit_token;
            _tokens++;
            if( debug ) check_invariant();
         }

         return relax_flags;
      }

      /**
       * Add another element to _schedule
       */
      uint32_t produce_schedule( RNG& rng )
      {
         uint32_t relax_flags = relax();
         if( debug ) check_invariant();
         if( _eligible.empty() )
            return relax_flags;

         decltype( rng( _eligible.size() ) ) pos = rng( _eligible.size() );
         assert( (pos >= 0) && (pos < _eligible.size()) );
         auto it = _eligible.begin() + pos;
         _schedule.push_back( *it );
         _ineligible_waiting_for_token.emplace_back( *it, false );
         _eligible.erase( it );
         _turns--;
         _tokens--;
         if( debug ) check_invariant();
         return relax_flags;
      }

      /**
       * Pull an element from _schedule
       */
      WitnessID consume_schedule()
      {
         assert( _schedule.size() > 0 );

         WitnessID result = _schedule.front();
         _schedule.pop_front();

         auto it = _lame_duck.find( result );
         if( it != _lame_duck.end() )
            _lame_duck.erase( it );
         if( debug ) check_invariant();
         return result;
      }

      /**
       * Remove all witnesses in the removal_set from
       * future scheduling (but not from the current schedule).
       */
      template< typename T >
      void remove_all( const T& removal_set )
      {
         if( debug ) check_invariant();

         _ineligible_waiting_for_token.erase(
            std::remove_if(
               _ineligible_waiting_for_token.begin(),
               _ineligible_waiting_for_token.end(),
               [&]( const std::pair< WitnessID, bool >& item ) -> bool
               {
                  bool found = removal_set.find( item.first ) != removal_set.end();
                  _turns -= (found & item.second) ? 1 : 0;
                  return found;
               } ),
            _ineligible_waiting_for_token.end() );
         if( debug ) check_invariant();

         _ineligible_no_turn.erase(
            std::remove_if(
               _ineligible_no_turn.begin(),
               _ineligible_no_turn.end(),
               [&]( WitnessID item ) -> bool
               {
                  bool found = (removal_set.find( item ) != removal_set.end());
                  _tokens -= (found ? 1 : 0);
                  return found;
               } ),
            _ineligible_no_turn.end() );
         if( debug ) check_invariant();

         _eligible.erase(
            std::remove_if(
               _eligible.begin(),
               _eligible.end(),
               [&]( WitnessID item ) -> bool
               {
                  bool found = (removal_set.find( item ) != removal_set.end());
                  _tokens -= (found ? 1 : 0);
                  _turns  -= (found ? 1 : 0);
                  return found;
               } ),
            _eligible.end() );
         if( debug ) check_invariant();

         return;
      }

      /**
       * Convenience function to call insert_all() and remove_all()
       * as needed to update to the given revised_set.
       */
      template< typename T >
      void insert_all( const T& insertion_set )
      {
         if( debug ) check_invariant();
         for( const WitnessID wid : insertion_set )
         {
            _eligible.push_back( wid );
         }
         _turns += insertion_set.size();
         _tokens += insertion_set.size();
         if( debug ) check_invariant();
         return;
      }

      /**
       * Convenience function to call insert_all() and remove_all()
       * as needed to update to the given revised_set.
       *
       * This function calls find() on revised_set for all current
       * witnesses.  Running time is O(n*log(n)) if the revised_set
       * implementation of find() is O(log(n)).
       *
       * TODO:  Rewriting to use std::set_difference may marginally
       * increase efficiency, but a benchmark is needed to justify this.
       */
      template< typename T >
      void update( const T& revised_set )
      {
         flat_set< WitnessID > current_set;
         flat_set< WitnessID > schedule_set;

         current_set.reserve(
              _ineligible_waiting_for_token.size()
            + _ineligible_no_turn.size()
            + _eligible.size()
            + _schedule.size() );
         for( const auto& item : _ineligible_waiting_for_token )
            current_set.insert( item.first );
         for( const WitnessID& item : _ineligible_no_turn )
            current_set.insert( item );
         for( const WitnessID& item : _eligible )
            current_set.insert( item );
         for( const WitnessID& item : _schedule )
         {
            current_set.insert( item );
            schedule_set.insert( item );
         }

         flat_set< WitnessID > insertion_set;
         insertion_set.reserve( revised_set.size() );
         for( const WitnessID& item : revised_set )
         {
            if( current_set.find( item ) == current_set.end() )
               insertion_set.insert( item );
         }

         flat_set< WitnessID > removal_set;
         removal_set.reserve( current_set.size() );
         for( const WitnessID& item : current_set )
         {
            if( revised_set.find( item ) == revised_set.end() )
            {
               if( schedule_set.find( item ) == schedule_set.end() )
                  removal_set.insert( item );
               else
                  _lame_duck.insert( item );
            }
         }

         insert_all( insertion_set );
         remove_all( removal_set );

         return;
      }

      /**
       * Get the number of scheduled witnesses
       */

      size_t size( )const
      {
         return _schedule.size();
      }

      bool get_slot( OffsetType offset, WitnessID& wit )const
      {
         if( offset >= _schedule.size() )
            return false;
         wit = _schedule[ offset ];
         return true;
      }

      // keep track of total turns / tokens in existence
      CountType _turns = 0;
      CountType _tokens = 0;

      // new tokens handed out when _tokens < _min_token_count
      CountType _min_token_count;

      // WitnessID appears in exactly one of the following:
      // has no token; second indicates whether we have a turn or not:
      std::deque < std::pair< WitnessID, bool > > _ineligible_waiting_for_token;   // ".." | "T."
      // has token, but no turn
      std::vector< WitnessID > _ineligible_no_turn;  // ".t"
      // has token and turn
      std::vector< WitnessID > _eligible;  // "Tt"

      // scheduled
      std::deque < WitnessID > _schedule;

      // in _schedule, but not to be replaced
      flat_set< WitnessID > _lame_duck;
};

template< typename WitnessID, typename RNG, typename CountType, typename OffsetType, bool debug = true >
class generic_far_future_witness_scheduler
{
   public:
      generic_far_future_witness_scheduler(
         const generic_witness_scheduler< WitnessID, RNG, CountType, OffsetType, debug >& base_scheduler,
         RNG rng
         )
      {
         generic_witness_scheduler< WitnessID, RNG, CountType, OffsetType, debug > extended_scheduler = base_scheduler;
         _begin_offset = base_scheduler.size()+1;
         while( (extended_scheduler.produce_schedule( rng ) & emit_turn) == 0 )
            _begin_offset++;
         assert( _begin_offset == extended_scheduler.size() );

         _end_offset = _begin_offset;
         while( (extended_scheduler.produce_schedule( rng ) & emit_turn) == 0 )
            _end_offset++;
         assert( _end_offset == extended_scheduler.size()-1 );
         _schedule.resize( extended_scheduler._schedule.size() );
         std::copy( extended_scheduler._schedule.begin(),
                    extended_scheduler._schedule.end(),
                    _schedule.begin() );
         return;
      }

      bool get_slot( OffsetType offset, WitnessID& wit )const
      {
         if( offset <= _end_offset )
            wit = _schedule[ offset ];
         else
            wit = _schedule[ _begin_offset +
               (
                  (offset - _begin_offset) %
                  (_end_offset + 1 - _begin_offset)
               ) ];
         return true;
      }

      std::vector< WitnessID > _schedule;
      OffsetType _begin_offset;
      OffsetType _end_offset;
};

} }
