/*
 * Copyright (c) 2019 oxarbitrage and contributors.
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
#include <graphene/custom_operations/custom_operations.hpp>

namespace graphene { namespace custom_operations {

void account_contact_operation::validate()const
{
}

void create_htlc_order_operation::validate()const
{
   FC_ASSERT(extensions.value.bitshares_amount.valid());
   FC_ASSERT(extensions.value.bitshares_amount->amount.value > 0);
   FC_ASSERT(extensions.value.blockchain.valid());
   FC_ASSERT(extensions.value.blockchain_account.valid());
   FC_ASSERT(extensions.value.blockchain_asset.valid());
   FC_ASSERT(extensions.value.blockchain_amount.valid());
   FC_ASSERT(extensions.value.expiration.valid());
}

void take_htlc_order_operation::validate()const
{
   FC_ASSERT(extensions.value.blockchain_account.valid());
   FC_ASSERT(extensions.value.htlc_order_id.valid());
}

} } //graphene::custom_operations

GRAPHENE_IMPLEMENT_EXTERNAL_SERIALIZATION( graphene::custom_operations::account_contact_operation )
GRAPHENE_IMPLEMENT_EXTERNAL_SERIALIZATION( graphene::custom_operations::create_htlc_order_operation )
GRAPHENE_IMPLEMENT_EXTERNAL_SERIALIZATION( graphene::custom_operations::take_htlc_order_operation )
