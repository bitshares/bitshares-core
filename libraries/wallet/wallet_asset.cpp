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

namespace graphene { namespace wallet {

vector<asset_object> wallet_api::list_assets(const string& lowerbound, uint32_t limit)const
{
   return my->_remote_db->list_assets( lowerbound, limit );
}

uint64_t wallet_api::get_asset_count()const
{
   return my->_remote_db->get_asset_count();
}

signed_transaction wallet_api::create_asset(string issuer,
                                            string symbol,
                                            uint8_t precision,
                                            asset_options common,
                                            fc::optional<bitasset_options> bitasset_opts,
                                            bool broadcast)

{
   return my->create_asset(issuer, symbol, precision, common, bitasset_opts, broadcast);
}

signed_transaction wallet_api::update_asset(string symbol,
                                            optional<string> new_issuer,
                                            asset_options new_options,
                                            bool broadcast /* = false */)
{
   return my->update_asset(symbol, new_issuer, new_options, broadcast);
}

signed_transaction wallet_api::update_asset_issuer(string symbol,
                                            string new_issuer,
                                            bool broadcast /* = false */)
{
   return my->update_asset_issuer(symbol, new_issuer, broadcast);
}

signed_transaction wallet_api::update_bitasset(string symbol,
                                               bitasset_options new_options,
                                               bool broadcast /* = false */)
{
   return my->update_bitasset(symbol, new_options, broadcast);
}

signed_transaction wallet_api::update_asset_feed_producers(string symbol,
                                                           flat_set<string> new_feed_producers,
                                                           bool broadcast /* = false */)
{
   return my->update_asset_feed_producers(symbol, new_feed_producers, broadcast);
}

signed_transaction wallet_api::publish_asset_feed(string publishing_account,
                                                  string symbol,
                                                  price_feed feed,
                                                  bool broadcast /* = false */)
{
   return my->publish_asset_feed(publishing_account, symbol, feed, broadcast);
}

signed_transaction wallet_api::fund_asset_fee_pool(string from,
                                                   string symbol,
                                                   string amount,
                                                   bool broadcast /* = false */)
{
   return my->fund_asset_fee_pool(from, symbol, amount, broadcast);
}

signed_transaction wallet_api::claim_asset_fee_pool(string symbol,
                                                    string amount,
                                                    bool broadcast /* = false */)
{
   return my->claim_asset_fee_pool(symbol, amount, broadcast);
}

signed_transaction wallet_api::reserve_asset(string from,
                                          string amount,
                                          string symbol,
                                          bool broadcast /* = false */)
{
   return my->reserve_asset(from, amount, symbol, broadcast);
}

asset_object wallet_api::get_asset(string asset_name_or_id) const
{
   auto a = my->find_asset(asset_name_or_id);
   FC_ASSERT(a);
   return *a;
}

asset_bitasset_data_object wallet_api::get_bitasset_data(string asset_name_or_id) const
{
   auto asset = get_asset(asset_name_or_id);
   FC_ASSERT(asset.is_market_issued() && asset.bitasset_data_id);
   return my->get_object<asset_bitasset_data_object>(*asset.bitasset_data_id);
}

asset_id_type wallet_api::get_asset_id(string asset_symbol_or_id) const
{
   return my->get_asset_id(asset_symbol_or_id);
}

signed_transaction wallet_api::issue_asset(string to_account, string amount, string symbol,
                                           string memo, bool broadcast)
{
   return my->issue_asset(to_account, amount, symbol, memo, broadcast);
}

signed_transaction wallet_api::global_settle_asset(string symbol,
                                                   price settle_price,
                                                   bool broadcast /* = false */)
{
   return my->global_settle_asset(symbol, settle_price, broadcast);
}

signed_transaction wallet_api::settle_asset(string account_to_settle,
                                            string amount_to_settle,
                                            string symbol,
                                            bool broadcast /* = false */)
{
   return my->settle_asset(account_to_settle, amount_to_settle, symbol, broadcast);
}

}} // graphene::wallet
