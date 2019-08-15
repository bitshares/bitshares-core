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

#include <fc/uint128.hpp>

namespace graphene {
namespace protocol {
   struct price;
}
namespace chain {
   class asset_object;
}

namespace app {
   std::string uint128_amount_to_string( const fc::uint128_t& amount, const uint8_t precision );
   std::string price_to_string( const graphene::protocol::price& _price,
                                const uint8_t base_precision,
                                const uint8_t quote_precision );
   std::string price_to_string( const graphene::protocol::price& _price,
                                const graphene::chain::asset_object& _base,
                                const graphene::chain::asset_object& _quote );
   std::string price_diff_percent_string( const graphene::protocol::price& old_price,
                                          const graphene::protocol::price& new_price );
} }
