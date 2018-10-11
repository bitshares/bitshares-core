/*
 * Copyright (c) 2018 The BitShares Project, and contributors.
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
#include <graphene/chain/protocol/balance.hpp>

namespace graphene { namespace chain {

   void balance_claim_operation::get_required_authorities( vector<const authority*>& a )const
   {
      _auth.weight_threshold = 1;
      if( _auth.key_auths.size() != 1 || _auth.key_auths.begin()->first != balance_owner_key )
      {
         vector<std::pair<public_key_type,weight_type>> temp;
         temp.emplace_back( balance_owner_key, 1 );
         _auth.key_auths = std::move( temp );
      }
      a.push_back( &_auth );
   }

} } // namespace graphene::chain
