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
#include <graphene/chain/futures_object.hpp>

#include <fc/io/raw.hpp>

namespace graphene { namespace chain {

} } // graphene::chain

FC_REFLECT_DERIVED_NO_TYPENAME( graphene::chain::futures_market_object, (graphene::db::object),
                    (owner)
                    (symbol)
                    (description)
                    (oracle_id)
                    (collateral_asset)
                    (contract_size)
                    (expiry)
                    (options)
                    (mark_price)
                    (mark_price_time)
                    (open_interest)
                    (cumulative_funding)
                    (last_funding_time)
                    (premium_avg)
                    (premium_last)
                    (last_premium_time)
                    (insurance_fund)
                    (settlement_price)
                    (is_settled)
                  )

FC_REFLECT_DERIVED_NO_TYPENAME( graphene::chain::futures_position_object, (graphene::db::object),
                    (owner)
                    (market_id)
                    (size)
                    (entry_value)
                    (margin)
                    (last_cumulative_funding)
                  )

FC_REFLECT_DERIVED_NO_TYPENAME( graphene::chain::futures_order_object, (graphene::db::object),
                    (owner)
                    (market_id)
                    (is_long)
                    (price_per_contract)
                    (size)
                    (deferred_margin)
                  )

GRAPHENE_IMPLEMENT_EXTERNAL_SERIALIZATION( graphene::chain::futures_market_object )
GRAPHENE_IMPLEMENT_EXTERNAL_SERIALIZATION( graphene::chain::futures_position_object )
GRAPHENE_IMPLEMENT_EXTERNAL_SERIALIZATION( graphene::chain::futures_order_object )
