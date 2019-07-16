/*
 * Copyright (c) 2015-2018 Cryptonomex, Inc., and contributors.
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
#include <graphene/chain/protocol/asset_ops.hpp>

#include <locale>

namespace graphene { namespace chain {

/**
 *  Valid symbols can contain [A-Z0-9], and '.'
 *  They must start with [A, Z]
 *  They must end with [A, Z] before HF_620 or [A-Z0-9] after it
 *  They can contain a maximum of one '.'
 */
bool is_valid_symbol( const string& symbol )
{
    static const std::locale& loc = std::locale::classic();
    if( symbol.size() < GRAPHENE_MIN_ASSET_SYMBOL_LENGTH )
        return false;

    if( symbol.substr(0,3) == "BIT" ) 
        return false;

    if( symbol.size() > GRAPHENE_MAX_ASSET_SYMBOL_LENGTH )
        return false;

    if( !isalpha( symbol.front(), loc ) )
        return false;

    if( !isalnum( symbol.back(), loc ) )
        return false;

    bool dot_already_present = false;
    for( const auto c : symbol )
    {
        if( (isalpha( c, loc ) && isupper( c, loc )) || isdigit( c, loc ) )
            continue;

        if( c == '.' )
        {
            if( dot_already_present )
                return false;

            dot_already_present = true;
            continue;
        }

        return false;
    }

    return true;
}

void asset_update_operation::validate()const
{
   FC_ASSERT( fee.amount >= 0 );
   if( new_issuer )
      FC_ASSERT(issuer != *new_issuer);
   new_options.validate();

   asset dummy = asset(1, asset_to_update) * new_options.core_exchange_rate;
   FC_ASSERT(dummy.asset_id == asset_id_type());
}

share_type asset_update_operation::calculate_fee(const asset_update_operation::fee_parameters_type& k)const
{
   return k.fee + calculate_data_fee( fc::raw::pack_size(*this), k.price_per_kbyte );
}

void asset_reserve_operation::validate()const
{
   FC_ASSERT( fee.amount >= 0 );
   FC_ASSERT( amount_to_reserve.amount.value <= GRAPHENE_INITIAL_MAX_SHARE_SUPPLY );
   FC_ASSERT( amount_to_reserve.amount.value > 0 );
}

void asset_fund_fee_pool_operation::validate() const
{
   FC_ASSERT( fee.amount >= 0 );
   FC_ASSERT( fee.asset_id == asset_id_type() );
   FC_ASSERT( amount > 0 );
}

void asset_claim_fees_operation::validate()const {
   FC_ASSERT( fee.amount >= 0 );
   FC_ASSERT( amount_to_claim.amount > 0 );
}

void asset_claim_pool_operation::validate()const {
   FC_ASSERT( fee.amount >= 0 );
   FC_ASSERT( fee.asset_id != asset_id);
   FC_ASSERT( amount_to_claim.amount > 0 );
   FC_ASSERT( amount_to_claim.asset_id == asset_id_type());
}

} } // namespace graphene::chain
