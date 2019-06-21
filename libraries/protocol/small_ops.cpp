/*
 * Copyright (c) 2019 BitShares Blockchain Foundation, and contributors.
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

#include <graphene/protocol/balance.hpp>
#include <graphene/protocol/buyback.hpp>
#include <graphene/protocol/exceptions.hpp>
#include <graphene/protocol/fba.hpp>
#include <graphene/protocol/vesting.hpp>

#include <fc/io/raw.hpp>

namespace graphene { namespace protocol {

FC_IMPLEMENT_EXCEPTION( protocol_exception, 4000000, "protocol exception" )

FC_IMPLEMENT_DERIVED_EXCEPTION( transaction_exception,      protocol_exception, 4010000,
                                "transaction validation exception" )

FC_IMPLEMENT_DERIVED_EXCEPTION( tx_missing_active_auth,     transaction_exception, 4010001,
                                "missing required active authority" )
FC_IMPLEMENT_DERIVED_EXCEPTION( tx_missing_owner_auth,      transaction_exception, 4010002,
                                "missing required owner authority" )
FC_IMPLEMENT_DERIVED_EXCEPTION( tx_missing_other_auth,      transaction_exception, 4010003,
                                "missing required other authority" )
FC_IMPLEMENT_DERIVED_EXCEPTION( tx_irrelevant_sig,          transaction_exception, 4010004,
                                "irrelevant signature included" )
FC_IMPLEMENT_DERIVED_EXCEPTION( tx_duplicate_sig,           transaction_exception, 4010005,
                                "duplicate signature included" )
FC_IMPLEMENT_DERIVED_EXCEPTION( invalid_committee_approval, transaction_exception, 4010006,
                                "committee account cannot directly approve transaction" )
FC_IMPLEMENT_DERIVED_EXCEPTION( insufficient_fee,           transaction_exception, 4010007, "insufficient fee" )

} } // graphene::protocol


GRAPHENE_IMPLEMENT_EXTERNAL_SERIALIZATION( graphene::protocol::balance_claim_operation )
GRAPHENE_IMPLEMENT_EXTERNAL_SERIALIZATION( graphene::protocol::buyback_account_options )
GRAPHENE_IMPLEMENT_EXTERNAL_SERIALIZATION( graphene::protocol::fba_distribute_operation )

GRAPHENE_IMPLEMENT_EXTERNAL_SERIALIZATION( graphene::protocol::vesting_balance_create_operation::fee_parameters_type )
GRAPHENE_IMPLEMENT_EXTERNAL_SERIALIZATION( graphene::protocol::vesting_balance_withdraw_operation::fee_parameters_type )
GRAPHENE_IMPLEMENT_EXTERNAL_SERIALIZATION( graphene::protocol::vesting_balance_create_operation )
GRAPHENE_IMPLEMENT_EXTERNAL_SERIALIZATION( graphene::protocol::vesting_balance_withdraw_operation )
