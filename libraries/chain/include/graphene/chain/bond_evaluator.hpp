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
#pragma once

#include <graphene/chain/evaluator.hpp>

namespace graphene { namespace chain {

class bond_create_offer_evaluator : public evaluator<bond_create_offer_evaluator>
{
    public:
        typedef bond_create_offer_operation operation_type;

        object_id_type do_evaluate( const bond_create_offer_operation& op );
        object_id_type do_apply( const bond_create_offer_operation& op );
};

class bond_cancel_offer_evaluator : public evaluator<bond_cancel_offer_evaluator>
{
    public:
        typedef bond_cancel_offer_operation operation_type;

        object_id_type do_evaluate( const bond_cancel_offer_operation& op );
        object_id_type do_apply( const bond_cancel_offer_operation& op );

        const bond_offer_object* _offer = nullptr;
};

class bond_accept_offer_evaluator : public evaluator<bond_accept_offer_evaluator>
{
    public:
        typedef bond_accept_offer_operation operation_type;

        object_id_type do_evaluate( const bond_accept_offer_operation& op );
        object_id_type do_apply( const bond_accept_offer_operation& op );

        const bond_offer_object* _offer = nullptr;
        asset                    _fill_amount;
};

class bond_claim_collateral_evaluator : public evaluator<bond_claim_collateral_evaluator>
{
    public:
        typedef bond_claim_collateral_operation operation_type;

        object_id_type do_evaluate( const bond_claim_collateral_operation& op );
        object_id_type do_apply( const bond_claim_collateral_operation& op );

        const bond_object* _bond = nullptr;
        asset              _interest_due;
};

} } // graphene::chain
