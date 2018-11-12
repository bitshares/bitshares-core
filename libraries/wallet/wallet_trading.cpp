/*
 * Copyright (c) 2017 Cryptonomex, Inc., and contributors.
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

#include "implementation/wallet_trading_impl.cpp"

namespace graphene { namespace wallet {

signed_transaction wallet_api::sell_asset(string seller_account,
                                          string amount_to_sell,
                                          string symbol_to_sell,
                                          string min_to_receive,
                                          string symbol_to_receive,
                                          uint32_t expiration,
                                          bool   fill_or_kill,
                                          bool   broadcast)
{
   return my->sell_asset(seller_account, amount_to_sell, symbol_to_sell, min_to_receive,
                         symbol_to_receive, expiration, fill_or_kill, broadcast);
}

signed_transaction wallet_api::borrow_asset(string seller_name, string amount_to_sell,
                                                string asset_symbol, string amount_of_collateral, bool broadcast)
{
   FC_ASSERT(!is_locked());
   return my->borrow_asset(seller_name, amount_to_sell, asset_symbol, amount_of_collateral, broadcast);
}

signed_transaction wallet_api::borrow_asset_ext( string seller_name, string amount_to_sell,
                                                 string asset_symbol, string amount_of_collateral,
                                                 call_order_update_operation::extensions_type extensions,
                                                 bool broadcast)
{
   FC_ASSERT(!is_locked());
   return my->borrow_asset_ext(seller_name, amount_to_sell, asset_symbol, amount_of_collateral, extensions, broadcast);
}

signed_transaction wallet_api::cancel_order(object_id_type order_id, bool broadcast)
{
   FC_ASSERT(!is_locked());
   return my->cancel_order(order_id, broadcast);
}

vector<bucket_object> wallet_api::get_market_history(
      string symbol1,
      string symbol2,
      uint32_t bucket,
      fc::time_point_sec start,
      fc::time_point_sec end )const
{
   return my->_remote_hist->get_market_history( get_asset_id(symbol1), get_asset_id(symbol2), bucket, start, end );
}

vector<limit_order_object> wallet_api::get_limit_orders(string a, string b, uint32_t limit)const
{
   return my->_remote_db->get_limit_orders(get_asset(a).id, get_asset(b).id, limit);
}

vector<call_order_object> wallet_api::get_call_orders(string a, uint32_t limit)const
{
   return my->_remote_db->get_call_orders(get_asset(a).id, limit);
}

vector<force_settlement_object> wallet_api::get_settle_orders(string a, uint32_t limit)const
{
   return my->_remote_db->get_settle_orders(get_asset(a).id, limit);
}

vector<collateral_bid_object> wallet_api::get_collateral_bids(string asset, uint32_t limit, uint32_t start)const
{
   return my->_remote_db->get_collateral_bids(get_asset(asset).id, limit, start);
}

signed_transaction wallet_api::bid_collateral(string bidder_name,
                                              string debt_amount, string debt_symbol,
                                              string additional_collateral,
                                              bool broadcast )
{
   return my->bid_collateral(bidder_name, debt_amount, debt_symbol, additional_collateral, broadcast);
}

order_book wallet_api::get_order_book( const string& base, const string& quote, unsigned limit )
{
   return( my->_remote_db->get_order_book( base, quote, limit ) );
}

}} // graphene::wallet
