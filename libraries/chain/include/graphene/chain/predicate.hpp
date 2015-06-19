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

#include <vector>

namespace graphene { namespace chain {

class database;

class pred_field_lit_cmp
{
public:
   pred_field_lit_cmp(
      object_id_type obj_id,
      uint16_t field_num,
      const vector<char>& lit,
      uint8_t opc
      ) :
      _obj_id( obj_id ),
      _field_num( field_num ),
      _lit( lit ),
      _opc( opc )
   {}
   pred_field_lit_cmp() {}    // necessary for instantiating static_variant
   ~pred_field_lit_cmp() {}

   bool check_predicate( const database& db )const;

   object_id_type _obj_id;
   uint16_t _field_num;
   vector<char> _lit;
   uint8_t _opc;
};

typedef static_variant<
   pred_field_lit_cmp
  > predicate;

} }

FC_REFLECT( graphene::chain::pred_field_lit_cmp, (_obj_id)(_field_num)(_lit)(_opc) );
