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

#include <graphene/protocol/liquidity_pool.hpp>

#include <fc/uint128.hpp>
#include <fc/exception/exception.hpp>
#include <boost/multiprecision/cpp_int.hpp>
#include <limits>

namespace graphene { namespace chain {

using graphene::protocol::STABLESWAP_AMP_MIN;
using graphene::protocol::STABLESWAP_AMP_MAX;

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
 * The `amp` argument below, and a pool's `amplification`, follow Curve's implementation
 * convention amp = A * n^(n-1): for n = 2 the `A` of the invariant above is amp / 2. That is
 * why the code computes Ann as amp * n, which equals A * n^n.
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
 * therefore uses a 256-bit accumulator; only the final, guaranteed-to-fit result is narrowed
 * back to fc::uint128_t, with an explicit bounds assertion rather than a silent truncation.
 *
 * The accumulator has headroom for products of values below 2^128, which bounds the inputs:
 * x + y must stay below 2^128. compute_d starts from d = x + y and its first step squares it,
 * so a larger sum wraps the unchecked accumulator before any bounds check is reached. Every
 * protocol-legal pool is inside that domain by a factor of 2^64, since balances are int64.
 */

/// Number of coins in the pool. Fixed at 2 for BitShares liquidity pools.
constexpr uint32_t SS_N_COINS = 2;
/// Maximum number of Newton iterations before we give up converging.
constexpr int16_t SS_MAX_ITER = 255;

namespace detail {

using wide_uint = boost::multiprecision::uint256_t;

/// Narrow a 256-bit accumulator back to 128 bits, asserting rather than silently
/// truncating if the value is out of range (it never should be for protocol-legal inputs).
fc::uint128_t narrow( const wide_uint& v, const char* what );

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
fc::uint128_t compute_d( const fc::uint128_t& x, const fc::uint128_t& y, uint64_t amp );

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
fc::uint128_t compute_new_y( const fc::uint128_t& new_x, const fc::uint128_t& d, uint64_t amp );

} // namespace stableswap

} } // graphene::chain
