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

void wallet_api::dbg_make_uia(string creator, string symbol)
{
   FC_ASSERT(!is_locked());
   my->dbg_make_uia(creator, symbol);
}

void wallet_api::dbg_make_mia(string creator, string symbol)
{
   FC_ASSERT(!is_locked());
   my->dbg_make_mia(creator, symbol);
}

void wallet_api::dbg_push_blocks( std::string src_filename, uint32_t count )
{
   my->dbg_push_blocks( src_filename, count );
}

void wallet_api::dbg_generate_blocks( std::string debug_wif_key, uint32_t count )
{
   my->dbg_generate_blocks( debug_wif_key, count );
}

void wallet_api::dbg_stream_json_objects( const std::string& filename )
{
   my->dbg_stream_json_objects( filename );
}

void wallet_api::dbg_update_object( fc::variant_object update )
{
   my->dbg_update_object( update );
}

void wallet_api::flood_network(string prefix, uint32_t number_of_transactions)
{
   FC_ASSERT(!is_locked());
   my->flood_network(prefix, number_of_transactions);
}

}} // graphene::wallet
