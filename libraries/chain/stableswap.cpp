/*
 * Copyright (c) 2026 BitShares contributors.
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
#include <graphene/chain/stableswap.hpp>

namespace graphene { namespace chain { namespace stableswap {

namespace detail {

/// truncating if the value is out of range (it never should be for protocol-legal inputs).
fc::uint128_t narrow( const wide_uint& v, const char* what )
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
 * where S = x + y, Ann = amp * n (which is A * n^n, with A = amp / 2 for n = 2), and
 * D_P = D^(n+1) / (n^n * prod(x_i)).
 *
 * Returns 0 when the pool is empty. Throws if Newton fails to converge.
 */
fc::uint128_t compute_d( const fc::uint128_t& x, const fc::uint128_t& y, uint64_t amp )
{
   using detail::wide_uint;

   const wide_uint x256 = wide_uint( x );
   const wide_uint y256 = wide_uint( y );
   const wide_uint s = x256 + y256;
   if( s == 0 )
      return fc::uint128_t( 0 ); // genuinely empty pool

   // Exactly one side empty is NOT the same as an empty pool: the D_P terms below divide by
   // each balance, so a zero here would be an integer division by zero. The invariant is
   // undefined for a half-empty pool anyway, so reject rather than invent a value. No
   // operation reaches this today -- every path leaves both balances at one unit or more, and
   // the exchange evaluator's former d_check, which could, is gone -- so it stays as a guard
   // and is tested by calling the function directly.
   FC_ASSERT( x256 > 0 && y256 > 0,
              "StableSwap: pool balances must both be positive to compute D" );

   const wide_uint ann = wide_uint( amp ) * SS_N_COINS; // Ann = amp * n = A * n^n (A = amp/2)

   wide_uint d = s;       // initial guess
   wide_uint d_prev;

   // Integer Newton does not always reach a fixed point here. For heavily imbalanced pools
   // (one balance more than ~10^6 times the other) the iteration settles into a short limit
   // cycle: the members differ from one another by about one part in 10^10 -- mathematically
   // converged -- but never by <= 1, so the stop condition below is never met and the loop
   // would run out of iterations and throw on a pool that is perfectly well defined. Observed
   // cycles run to 19 iterations, so keep a window comfortably longer than that; on seeing a
   // value repeat, stop and take the LARGEST member of the cycle. Largest is the right choice
   // twice over: a bigger D makes compute_new_y hold more of the out-asset back, so rounding
   // stays in the pool's favour, and taking a maximum is deterministic, which consensus
   // requires of every node reaching this line.
   constexpr size_t SS_CYCLE_WINDOW = 32;
   wide_uint recent[SS_CYCLE_WINDOW] = {};
   size_t filled = 0;

   for( int16_t i = 0; i < SS_MAX_ITER; ++i )
   {
      // Every division here truncates, but that does not make D systematically low. The
      // iteration starts at d = x + y, which is at or above D, and approaches it from above;
      // what decides the final value is the stop tolerance below, not the truncation. Measured
      // against a high-precision reference over 4000 random pool states (balances to 1e15,
      // A to 1e6): D came out as much as 515 units ABOVE the exact value -- which is the
      // direction that favours the pool, since a larger D makes compute_new_y hold more back --
      // and never more than exactly 1 unit below it. One unit is what the withdrawal path
      // already gives back to the pool before paying out, so the only direction that could
      // favour the caller is covered. Evidence, not proof: 4000 samples, not an argument.
      //
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

      // Not converging: check whether we have been at this exact value before, which means
      // the iteration is cycling and will keep returning here for as long as we let it.
      // The valid entries are the oldest `filled`, at the head of the window.
      for( size_t k = 0; k < filled; ++k )
      {
         if( recent[k] != d )
            continue;
         wide_uint best = d;
         for( size_t j = k; j < filled; ++j )
            if( recent[j] > best )
               best = recent[j];
         return detail::narrow( best, "D" );
      }

      // Append while there is room, and only shift once the window is full. The window is
      // head-aligned for exactly this reason: shifting all of it on every iteration moved
      // thirty-one entries to make room for one, and did so even while most of the window was
      // still empty. Almost every call converges in a handful of iterations and so never
      // fills it at all, which means it now never shifts.
      if( filled < SS_CYCLE_WINDOW )
      {
         recent[filled] = d;
         ++filled;
      }
      else
      {
         for( size_t k = 1; k < SS_CYCLE_WINDOW; ++k )
            recent[k - 1] = recent[k];
         recent[SS_CYCLE_WINDOW - 1] = d;
      }
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
 * The caller obtains the amount paid out as old_y - returned_y. The result is truncated and
 * can lie up to one unit BELOW the exact solution, which on its own would favour the caller;
 * measured on six pools it was 0.66 to 0.96 units below every time. Both callers correct for
 * it: the swap path keeps one extra unit of the out-asset, and the withdrawal path pays one
 * unit less.
 */
fc::uint128_t compute_new_y( const fc::uint128_t& new_x, const fc::uint128_t& d, uint64_t amp )
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

   for( int16_t i = 0; i < SS_MAX_ITER; ++i )
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

} } } // graphene::chain::stableswap
