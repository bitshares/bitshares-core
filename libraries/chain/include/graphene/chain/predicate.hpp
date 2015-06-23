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

#include <graphene/chain/types.hpp>

namespace graphene { namespace chain {

class database;

/**
 *  Used to verify that account_id->name is account_name 
 */
struct verify_account_name
{
   account_id_type account_id;
   string          account_name;

   /**
    *  Perform state independent checks, such as verifying that
    *  account_name is a valid name for an account.
    */
   bool validate()const { return is_valid_name( account_name ); }
};

/**
 *  Used to verify that account_id->name is account_name 
 */
struct verify_symbol
{
   asset_id_type   asset_id;
   string          symbol;

   /**
    *  Perform state independent checks, such as verifying that
    *  account_name is a valid name for an account.
    */
   bool validate()const { return is_valid_symbol( symbol ); }
};

/**
 *  When defining predicates do not make the protocol dependent upon 
 *  implementation details.  
 */
typedef static_variant<
   verify_account_name,
   verify_symbol
  > predicate;

} }

FC_REFLECT( graphene::chain::verify_account_name, (account_id)(account_name) )
FC_REFLECT( graphene::chain::verify_symbol,  (asset_id)(symbol) )
 
