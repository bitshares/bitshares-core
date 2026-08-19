/*
 * Copyright (c) 2026 Claude / BitShares contributors.
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
#pragma once

#include <fc/uint128.hpp>
#include <fc/exception/exception.hpp>
#include <boost/multiprecision/cpp_int.hpp>
#include <limits>

namespace graphene { namespace chain {

/// Minimum amplification coefficient A for a stable pool (A=1 is barely curved).
constexpr uint64_t STABLESWAP_AMP_MIN = 1;
/// Maximum amplification coefficient A. Bounded so that Ann*S cannot overflow 128 bits for
/// any int64 balances (A * n * (x+y) with x+y < 2^64 stays well under 2^128).
constexpr uint64_t STABLESWAP_AMP_MAX = 1000000;

namespace stableswap {

/**
 * Integer implementation of the Curve / StableSwap invariant for a two-asset pool.
 *
 * The invariant for an n-coin pool is
 *
 *     A * n^n * sum(x_i) + D = A * D * n^n + D^(n+1) / ( n^n * prod(x_i) )
 *
 * For n = 2 this reduces to the equation solved by @ref compute_d below. `A` is the
 * amplification coefficient: A -> 0 degenerates to the constant-product curve (x*y=k),
 * while large A approaches the constant-sum curve (x+y=const), i.e. a near-flat 1:1 peg.
 *
 * IMPORTANT: both balances must be expressed in the *same unit scale* before being passed
 * in. This v1 enforces equal asset precision at pool-creation time (see the create
 * evaluator), so the raw on-chain `share_type` balances are already directly comparable
 * and no per-asset rescaling is required here.
 *
 * The two balances of a BitShares pool are int64 `share_type` values, so the final D and y
 * results always fit comfortably within 128 bits for any protocol-legal balances. However,
 * the *intermediate* products taken during each Newton step (e.g. D_P * D, before it is
 * divided back down) can transiently exceed 128 bits well before that -- for example with a
 * heavily imbalanced pool (one balance near GRAPHENE_MAX_SHARE_SUPPLY, the other tiny), the
 * `d_p * d` term alone can exceed 2^128 by six orders of magnitude even though D itself does
 * not. A pure fc::uint128_t (== unsigned __int128) accumulator would silently wrap on that
 * intermediate multiply, corrupting the on-chain invariant. All internal arithmetic here
 * therefore uses a 256-bit accumulator (ample headroom for any product of two ~128-bit
 * intermediates); only the final, guaranteed-to-fit result is narrowed back to
 * fc::uint128_t, with an explicit bounds assertion rather than a silent truncation.
 */

/// Number of coins in the pool. Fixed at 2 for BitShares liquidity pools.
constexpr uint32_t SS_N_COINS = 2;
/// Maximum number of Newton iterations before we give up converging.
constexpr int SS_MAX_ITER = 255;

namespace detail {

using wide_uint = boost::multiprecision::uint256_t;

/// Narrow a 256-bit accumulator back to 128 bits, asserting rather than silently
/// truncating if the value is out of range (it never should be for protocol-legal inputs).
inline fc::uint128_t narrow( const wide_uint& v, const char* what )
{
   FC_ASSERT( v <= wide_uint( std::numeric_limits<fc::uint128_t>::max() ),
              "StableSwap: ${w} exceeds 128 bits", ("w", what) );
   return static_cast<fc::uint128_t>( v );
}

} // namespace detail

/**
 * Compute the StableSwap invariant D for balances (x, y) and amplification A.
 *
 * Solves, by Newton's method on D:
 *     Ann*S + n*D_P  =  (Ann - 1)*D + (n+1)*D_P        ... rearranged fixed-point form
 * where S = x + y, Ann = A * n^n, and D_P = D^(n+1) / (n^n * prod(x_i)).
 *
 * Returns 0 when the pool is empty. Throws if Newton fails to converge.
 */
inline fc::uint128_t compute_d( const fc::uint128_t& x, const fc::uint128_t& y, uint64_t amp )
{
   using detail::wide_uint;

   const wide_uint x256 = wide_uint( x );
   const wide_uint y256 = wide_uint( y );
   const wide_uint s = x256 + y256;
   if( s == 0 )
      return fc::uint128_t( 0 ); // genuinely empty pool

   // Exactly one side empty is NOT the same as an empty pool: the D_P terms below divide by
   // each balance, so a zero here would be an integer division by zero. The invariant is
   // undefined for a half-empty pool anyway, so reject rather than invent a value. This is
   // reachable from the exchange evaluator's d_check, where compute_new_y() can return 0 when
   // a swap would drain the out-asset side completely; failing closed there rejects the
   // draining trade instead of crashing the node.
   FC_ASSERT( x256 > 0 && y256 > 0,
              "StableSwap: pool balances must both be positive to compute D" );

   const wide_uint ann = wide_uint( amp ) * SS_N_COINS; // A * n  (n^n = n^2 folded below)

   wide_uint d = s;       // initial guess
   wide_uint d_prev;

   for( int i = 0; i < SS_MAX_ITER; ++i )
   {
      // D_P = D^(n+1) / (n^n * prod(x_i)) ; for n=2: D_P = D^3 / (4 * x * y)
      // Computed in a 256-bit accumulator: the D_P*D intermediate below can transiently
      // exceed 128 bits for imbalanced pools even though D itself never does.
      wide_uint d_p = d;
      d_p = d_p * d / ( x256 * SS_N_COINS ); // D^2 / (n*x)
      d_p = d_p * d / ( y256 * SS_N_COINS ); // D^3 / (n^2 * x * y)

      d_prev = d;

      // D = (Ann*S + n*D_P) * D / ((Ann-1)*D + (n+1)*D_P)
      const wide_uint numerator   = ( ann * s + d_p * SS_N_COINS ) * d;
      const wide_uint denominator = ( ann - 1 ) * d + ( SS_N_COINS + 1 ) * d_p;
      d = numerator / denominator;

      // Converged when successive iterates differ by at most one unit.
      if( ( d > d_prev ? ( d - d_prev ) : ( d_prev - d ) ) <= 1 )
         return detail::narrow( d, "D" );
   }

   FC_THROW_EXCEPTION( fc::exception, "StableSwap D did not converge" );
}

/**
 * Given the new balance `new_x` of the in-asset and the (unchanged) invariant `d`,
 * compute the new balance `y` of the out-asset by solving the quadratic
 *
 *     y^2 + (b - D) y - c = 0
 *
 * via Newton's method, where (for n = 2):
 *     c = D^(n+1) / (n^n * new_x * Ann)   and   b = new_x + D / Ann
 *
 * The caller obtains the amount paid out as old_y - returned_y. Rounds in the pool's
 * favour (Newton converges from above and we stop at <=1 unit).
 */
inline fc::uint128_t compute_new_y( const fc::uint128_t& new_x, const fc::uint128_t& d, uint64_t amp )
{
   using detail::wide_uint;

   FC_ASSERT( new_x > 0, "in-asset balance must be positive" );

   const wide_uint new_x256 = wide_uint( new_x );
   const wide_uint d256 = wide_uint( d );
   const wide_uint ann = wide_uint( amp ) * SS_N_COINS;

   // c = D^3 / (n^n * new_x * Ann) = D^3 / (4 * new_x * Ann), built up to avoid overflow.
   // As in compute_d, the D*D intermediate can transiently exceed 128 bits, so this is
   // computed in a 256-bit accumulator.
   wide_uint c = d256;
   c = c * d256 / ( new_x256 * SS_N_COINS );  // D^2 / (n * new_x)
   c = c * d256 / ( ann * SS_N_COINS );       // D^3 / (n^2 * new_x * Ann)

   // b = new_x + D / Ann
   const wide_uint b = new_x256 + d256 / ann;

   wide_uint y = d256;       // initial guess
   wide_uint y_prev;

   for( int i = 0; i < SS_MAX_ITER; ++i )
   {
      y_prev = y;
      // y = (y^2 + c) / (2y + b - D)
      // wide_uint is unsigned, so `2y + b - D` would silently wrap to an enormous value
      // instead of going negative, and is an outright division by zero when equal. Check
      // before subtracting rather than after.
      const wide_uint numerator   = y * y + c;
      const wide_uint denom_lhs   = SS_N_COINS * y + b;
      FC_ASSERT( denom_lhs > d256, "StableSwap: y iteration denominator underflow" );
      const wide_uint denominator = denom_lhs - d256;
      y = numerator / denominator;

      if( ( y > y_prev ? ( y - y_prev ) : ( y_prev - y ) ) <= 1 )
         return detail::narrow( y, "y" );
   }

   FC_THROW_EXCEPTION( fc::exception, "StableSwap y did not converge" );
}

} // namespace stableswap

} } // graphene::chain
