/*
 * Copyright (c) 2015 Cryptonomex, Inc., and contributors.
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
#include <cybex/crowdfund_ops.hpp>

namespace graphene { namespace chain {


void initiate_crowdfund_operation::validate()const
{
   FC_ASSERT( 0 );
   FC_ASSERT( t>0,"time lock can not be 0." );
   FC_ASSERT( u>t,"time lock is bigger than crowd sale duration.");
}
void participate_crowdfund_operation::validate()const
{
   FC_ASSERT( 0 );
   FC_ASSERT(valuation>0,"valuation can not be 0." );
   FC_ASSERT(cap>valuation,"cap must be greater than valuation," );
}
void withdraw_crowdfund_operation::validate()const
{
   FC_ASSERT( 0 );
}
#if 0
share_type initiate_crowdfund_operation::calculate_fee(fee_parameters_type const&f)const
{
       return base_operation::calculate_fee(f);
}
share_type participate_crowdfund_operation::calculate_fee(fee_parameters_type const&f)const
{
       return base_operation::calculate_fee(f);
}
share_type withdraw_crowdfund_operation::calculate_fee(fee_parameters_type const&f)const
{
       return base_operation::calculate_fee(f);
}
#endif

} } // namespace graphene::chain
