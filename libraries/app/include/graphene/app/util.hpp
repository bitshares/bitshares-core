/*
 * Copyright (c) 2018 Abit More, and contributors.
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

#include <boost/multiprecision/cpp_int.hpp>

#include <fc/uint128.hpp>

#include <graphene/chain/protocol/asset.hpp>

namespace graphene { namespace app {
   using namespace graphene::chain;

   typedef boost::multiprecision::uint256_t u256;

   u256 to256( const fc::uint128& t );
   fc::uint128 to_capped128( const u256& t );
   string uint128_amount_to_string( const fc::uint128& amount, const uint8_t precision );
   string price_to_string( const price& _price, const uint8_t base_precision, const uint8_t quote_precision);
   string price_diff_percent_string( const price& old_price, const price& new_price );

} }
