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
#include <graphene/chain/account_object.hpp>

namespace graphene { namespace chain {

class account_create_evaluator : public evaluator<account_create_evaluator>
{
public:
   typedef account_create_operation operation_type;

   void_result do_evaluate( const account_create_operation& o );
   object_id_type do_apply( const account_create_operation& o ) ;
};

class account_update_evaluator : public evaluator<account_update_evaluator>
{
public:
   typedef account_update_operation operation_type;

   void_result do_evaluate( const account_update_operation& o );
   void_result do_apply( const account_update_operation& o );

   const account_object* acnt;
};

class account_upgrade_evaluator : public evaluator<account_upgrade_evaluator>
{
public:
   typedef account_upgrade_operation operation_type;

   void_result do_evaluate(const operation_type& o);
   void_result do_apply(const operation_type& o);

   const account_object* account;
};

class account_whitelist_evaluator : public evaluator<account_whitelist_evaluator>
{
public:
   typedef account_whitelist_operation operation_type;

   void_result do_evaluate( const account_whitelist_operation& o);
   void_result do_apply( const account_whitelist_operation& o);

   const account_object* listed_account;
};

} } // graphene::chain
