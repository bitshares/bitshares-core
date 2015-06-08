/*
 * Copyright (c) 2015, Cryptonomex, Inc.
 * All rights reserved.
 *
 * This source code is provided for evaluation in private test networks only, until September 8, 2015. After this date, this license expires and
 * the code may not be used, modified or distributed for any purpose. Redistribution and use in source and binary forms, with or without modification,
 * are permitted until September 8, 2015, provided that the following conditions are met:
 *
 * 1. The code and/or derivative works are used only for private test networks consisting of no more than 10 P2P nodes.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#include <graphene/chain/account_object.hpp>
#include <graphene/chain/bond_evaluator.hpp>
#include <graphene/chain/bond_object.hpp>
#include <graphene/chain/database.hpp>

namespace graphene { namespace chain {

object_id_type bond_create_offer_evaluator::do_evaluate( const bond_create_offer_operation& op )
{
    const auto& d = db();

    const auto& creator_account = op.creator( d );
    const auto& base_asset = op.collateral_rate.base.asset_id( d );
    const auto& quote_asset = op.collateral_rate.quote.asset_id( d );

    // TODO: Check asset authorizations and withdrawals

    const auto& amount_asset = (op.amount.asset_id == op.collateral_rate.base.asset_id) ? base_asset : quote_asset;

    FC_ASSERT( !base_asset.is_transfer_restricted() && !quote_asset.is_transfer_restricted() );

    if( base_asset.options.whitelist_markets.size() ) 
       FC_ASSERT( base_asset.options.whitelist_markets.find( quote_asset.id ) != base_asset.options.whitelist_markets.end() );
    if( base_asset.options.blacklist_markets.size() ) 
       FC_ASSERT( base_asset.options.blacklist_markets.find( quote_asset.id ) == base_asset.options.blacklist_markets.end() );

    FC_ASSERT( d.get_balance( creator_account, amount_asset ) >= op.amount );

    return object_id_type();
}

object_id_type bond_create_offer_evaluator::do_apply( const bond_create_offer_operation& op )
{
    db().adjust_balance( op.creator, -op.amount );
    db().adjust_core_in_orders( op.creator(db()), op.amount );

    const auto& offer = db().create<bond_offer_object>( [&]( bond_offer_object& obj )
    {
        obj.offered_by_account = op.creator;
        obj.offer_to_borrow = op.offer_to_borrow;
        obj.amount = op.amount;
        obj.min_match = op.min_match;
        obj.collateral_rate = op.collateral_rate;
        obj.min_loan_period_sec = op.min_loan_period_sec;
        obj.loan_period_sec = op.loan_period_sec;
        obj.interest_apr = op.interest_apr;
    } );

    return offer.id;
}


object_id_type bond_cancel_offer_evaluator::do_evaluate( const bond_cancel_offer_operation& op )
{
    _offer = &op.offer_id(db());
    FC_ASSERT( op.creator == _offer->offered_by_account );
    FC_ASSERT( _offer->amount == op.refund );
    return object_id_type();
}

object_id_type bond_cancel_offer_evaluator::do_apply( const bond_cancel_offer_operation& op )
{
    assert( _offer != nullptr );
    db().adjust_balance( op.creator, op.refund );
    db().adjust_core_in_orders( op.creator(db()), -op.refund );
    db().remove( *_offer );
    return  object_id_type();
}

object_id_type bond_accept_offer_evaluator::do_evaluate( const bond_accept_offer_operation& op )
{ try {
    _offer = &op.offer_id(db());

    if( _offer->offer_to_borrow )
       FC_ASSERT( op.amount_borrowed.amount >= _offer->min_match );
    else
       FC_ASSERT( op.amount_collateral.amount >= _offer->min_match );

    FC_ASSERT( (op.amount_borrowed / op.amount_collateral  == _offer->collateral_rate) ||
               (op.amount_collateral / op.amount_borrowed  == _offer->collateral_rate)  );

    return object_id_type();
} FC_CAPTURE_AND_RETHROW((op)) }

object_id_type bond_accept_offer_evaluator::do_apply( const bond_accept_offer_operation& op )
{ try {

    if( op.claimer == op.lender )
    {
       db().adjust_balance( op.lender, -op.amount_borrowed );
    }
    else // claimer == borrower
    {
       db().adjust_balance( op.borrower, -op.amount_collateral );
       db().adjust_core_in_orders( op.borrower(db()), op.amount_collateral );
    }
    db().adjust_balance( op.borrower, op.amount_borrowed );

    const auto& bond = db().create<bond_object>( [&]( bond_object& obj )
    {
        obj.borrowed = op.amount_borrowed;
        obj.collateral = op.amount_collateral;
        obj.borrower   = op.borrower;
        obj.lender     = op.lender;

        auto head_time = db().get_dynamic_global_properties().time;
        obj.interest_apr         = _offer->interest_apr;
        obj.start_date           = head_time;
        obj.due_date             = head_time + fc::seconds( _offer->loan_period_sec ); 
        obj.earliest_payoff_date = head_time + fc::seconds( _offer->min_loan_period_sec );
    } );

    if( _offer->offer_to_borrow && op.amount_borrowed < _offer->amount )
    {
       db().modify( *_offer, [&]( bond_offer_object& offer ){
           offer.amount -= op.amount_borrowed;
       });
    }
    else if( !_offer->offer_to_borrow && op.amount_collateral < _offer->amount )
    {
       db().modify( *_offer, [&]( bond_offer_object& offer ){
           offer.amount -= op.amount_collateral;
       });
    }
    else
    {
       db().remove( *_offer );
    }
    return  bond.id;
} FC_CAPTURE_AND_RETHROW((op)) }



object_id_type bond_claim_collateral_evaluator::do_evaluate( const bond_claim_collateral_operation& op )
{
    _bond = &op.bond_id(db());
    auto head_time = db().get_dynamic_global_properties().time;
    FC_ASSERT( head_time > _bond->earliest_payoff_date );


    FC_ASSERT( op.collateral_claimed <= _bond->collateral );
    if( _bond->borrower == op.claimer )
    {
       auto elapsed_time = head_time - _bond->start_date;
       auto elapsed_days = 1 + elapsed_time.to_seconds() / (60*60*24);

       fc::uint128 tmp = _bond->borrowed.amount.value;
       tmp *= elapsed_days;
       tmp *= _bond->interest_apr;
       tmp /= (365 * GRAPHENE_100_PERCENT);
       FC_ASSERT( tmp < GRAPHENE_MAX_SHARE_SUPPLY );
       _interest_due = asset(tmp.to_uint64(), _bond->borrowed.asset_id);

       FC_ASSERT( _interest_due + _bond->borrowed <= op.payoff_amount );

       auto total_debt = _interest_due + _bond->borrowed;
       
       fc::uint128 max_claim = _bond->collateral.amount.value;
       max_claim *= op.payoff_amount.amount.value;
       max_claim /= total_debt.amount.value;

       FC_ASSERT( op.collateral_claimed.amount.value == max_claim.to_uint64() );
    }
    else
    {
       FC_ASSERT( _bond->lender == op.claimer );
       FC_ASSERT( head_time > _bond->due_date );
       FC_ASSERT( _bond->collateral == op.collateral_claimed );
       FC_ASSERT( op.payoff_amount == asset(0,_bond->borrowed.asset_id ) );
    }
    return object_id_type();
}

object_id_type bond_claim_collateral_evaluator::do_apply( const bond_claim_collateral_operation& op )
{
    assert( _bond != nullptr );

    const account_object& claimer = op.claimer(db());

    db().adjust_core_in_orders( _bond->borrower(db()), -op.collateral_claimed );

    if( op.payoff_amount.amount > 0 )
    {
       db().adjust_balance( claimer, -op.payoff_amount );
       db().adjust_balance( op.lender, op.payoff_amount );
    }
    db().adjust_balance( claimer, op.collateral_claimed );

    if( op.collateral_claimed == _bond->collateral )
       db().remove(*_bond);
    else
       db().modify( *_bond, [&]( bond_object& bond ){
               bond.borrowed   -= op.payoff_amount + _interest_due;
               bond.collateral -= op.collateral_claimed;
               bond.start_date = db().get_dynamic_global_properties().time;
          });

    return  object_id_type();
}

} } // graphene::chain
