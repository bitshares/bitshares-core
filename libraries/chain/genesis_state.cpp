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

#include <graphene/chain/genesis_state.hpp>
#include <graphene/protocol/fee_schedule.hpp>

#include <fc/io/raw.hpp>

namespace graphene { namespace chain {

chain_id_type genesis_state_type::compute_chain_id() const
{
   return initial_chain_id;
}

} } // graphene::chain

FC_REFLECT_DERIVED_NO_TYPENAME(graphene::chain::genesis_state_type::initial_account_type, BOOST_PP_SEQ_NIL,
           (name)(owner_key)(active_key)(is_lifetime_member) )

FC_REFLECT_DERIVED_NO_TYPENAME(graphene::chain::genesis_state_type::initial_asset_type, BOOST_PP_SEQ_NIL,
           (symbol)(issuer_name)(description)(precision)(max_supply)(accumulated_fees)(is_bitasset)
           (collateral_records))

FC_REFLECT_DERIVED_NO_TYPENAME(graphene::chain::genesis_state_type::initial_asset_type::initial_collateral_position,
           BOOST_PP_SEQ_NIL, (owner)(collateral)(debt))

FC_REFLECT_DERIVED_NO_TYPENAME(graphene::chain::genesis_state_type::initial_balance_type, BOOST_PP_SEQ_NIL,
           (owner)(asset_symbol)(amount))

FC_REFLECT_DERIVED_NO_TYPENAME(graphene::chain::genesis_state_type::initial_vesting_balance_type, BOOST_PP_SEQ_NIL,
           (owner)(asset_symbol)(amount)(begin_timestamp)(vesting_duration_seconds)(begin_balance))

FC_REFLECT_DERIVED_NO_TYPENAME(graphene::chain::genesis_state_type::initial_witness_type, BOOST_PP_SEQ_NIL,
           (owner_name)(block_signing_key))

FC_REFLECT_DERIVED_NO_TYPENAME(graphene::chain::genesis_state_type::initial_committee_member_type, BOOST_PP_SEQ_NIL,
           (owner_name))

FC_REFLECT_DERIVED_NO_TYPENAME(graphene::chain::genesis_state_type::initial_worker_type, BOOST_PP_SEQ_NIL,
           (owner_name)(daily_pay))

FC_REFLECT_DERIVED_NO_TYPENAME(graphene::chain::genesis_state_type, BOOST_PP_SEQ_NIL,
           (initial_timestamp)(max_core_supply)(initial_parameters)(initial_accounts)(initial_assets)
           (initial_balances)(initial_vesting_balances)(initial_active_witnesses)(initial_witness_candidates)
           (initial_committee_candidates)(initial_worker_candidates)
           (immutable_parameters))

GRAPHENE_IMPLEMENT_EXTERNAL_SERIALIZATION( graphene::chain::genesis_state_type::initial_account_type )
GRAPHENE_IMPLEMENT_EXTERNAL_SERIALIZATION( graphene::chain::genesis_state_type::initial_asset_type )
GRAPHENE_IMPLEMENT_EXTERNAL_SERIALIZATION( graphene::chain::genesis_state_type::initial_asset_type::initial_collateral_position )
GRAPHENE_IMPLEMENT_EXTERNAL_SERIALIZATION( graphene::chain::genesis_state_type::initial_balance_type )
GRAPHENE_IMPLEMENT_EXTERNAL_SERIALIZATION( graphene::chain::genesis_state_type::initial_vesting_balance_type )
GRAPHENE_IMPLEMENT_EXTERNAL_SERIALIZATION( graphene::chain::genesis_state_type::initial_witness_type )
GRAPHENE_IMPLEMENT_EXTERNAL_SERIALIZATION( graphene::chain::genesis_state_type::initial_committee_member_type )
GRAPHENE_IMPLEMENT_EXTERNAL_SERIALIZATION( graphene::chain::genesis_state_type::initial_worker_type )
GRAPHENE_IMPLEMENT_EXTERNAL_SERIALIZATION( graphene::chain::genesis_state_type )
