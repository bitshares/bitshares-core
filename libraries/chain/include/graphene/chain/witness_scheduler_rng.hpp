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

#include <boost/multiprecision/integer.hpp>

namespace graphene { namespace chain {

/**
 * Always returns 0.  Useful for testing.
 */
class nullary_rng
{
   public:
      nullary_rng() {}
      virtual ~nullary_rng() {}

      template< typename T > T operator()( T max )
      {   return T(0);   }
} ;

/**
 * The sha256_ctr_rng generates bits using SHA256 in counter (CTR)
 * mode.
 */
template< class HashClass, int SeedLength=32 >
class hash_ctr_rng
{
   public:
      hash_ctr_rng( const char* seed, uint64_t counter = 0 )
         : _counter( counter ), _current_offset( 0 )
      {
         memcpy( _seed, seed, SeedLength );
         _reset_current_value();
         return;
      }

      virtual ~hash_ctr_rng() {}

      uint64_t get_bits( uint8_t count )
      {
         uint64_t result = 0;
         uint64_t mask = 1;
         // grab the requested number of bits
         while( count > 0 )
         {
            result |=
               (
                  (
                     (
                        _current_value.data()[ (_current_offset >> 3) & 0x1F ]
                        & ( 1 << (_current_offset & 0x07) )
                     )
                     != 0
                  ) ? mask : 0
               );
            mask += mask;
            --count;
            ++_current_offset;
            if( _current_offset == (_current_value.data_size() << 3) )
            {
               _counter++;
               _current_offset = 0;
               _reset_current_value();
            }
         }
         return result;
      }

      uint64_t operator()( uint64_t bound )
      {
         if( bound <= 1 )
            return 0;
         uint8_t bitcount = boost::multiprecision::detail::find_msb( bound ) + 1;

         // probability of loop exiting is >= 1/2, so probability of
         // running N times is bounded above by (1/2)^N
         while( true )
         {
            uint64_t result = get_bits( bitcount );
            if( result < bound )
               return result;
         }
      }

      // convenience method which does casting for types other than uint64_t
      template< typename T > T operator()( T bound )
      {  return (T) ( (*this)(uint64_t( bound )) );   }

      void _reset_current_value()
      {
         // internal implementation detail, called to update
         //   _current_value when _counter changes
         typename HashClass::encoder enc;
         enc.write(           _seed   , SeedLength );
         enc.write( (char *) &_counter,          8 );
         _current_value = enc.result();
         return;
      }

      uint64_t _counter;
      char _seed[ SeedLength ];
      HashClass _current_value;
      uint16_t _current_offset;

      static const int seed_length = SeedLength;
} ;

} }
