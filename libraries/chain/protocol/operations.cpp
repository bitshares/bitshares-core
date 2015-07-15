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
#include <graphene/chain/protocol/operations.hpp>

namespace graphene { namespace chain {


uint64_t base_operation::calculate_data_fee( uint64_t bytes, uint64_t price_per_kbyte )
{
   auto result = (fc::uint128(bytes) * price_per_kbyte) / 1024;
   FC_ASSERT( result <= GRAPHENE_MAX_SHARE_SUPPLY );
   return result.to_uint64();
}




void  balance_claim_operation::validate()const
{
   FC_ASSERT( fee == asset() );
   FC_ASSERT( balance_owner_key != public_key_type() );
}

struct required_auth_visitor
{
   typedef void result_type;

   vector<authority>& result;

   required_auth_visitor( vector<authority>& r ):result(r){}

   /** for most operations this is a no-op */
   template<typename T>
   void operator()(const T& )const {}

   void operator()( const balance_claim_operation& o )const
   {
      result.push_back( authority( 1, o.balance_owner_key, 1 ) );
   }
};

struct required_active_visitor
{
   typedef void result_type;

   flat_set<account_id_type>& result;

   required_active_visitor( flat_set<account_id_type>& r ):result(r){}

   /** for most operations this is just the fee payer */
   template<typename T>
   void operator()(const T& o)const 
   { 
      result.insert( o.fee_payer() );
   }
   void operator()(const account_update_operation& o)const 
   {
      /// if owner authority is required, no active authority is required
      if( !(o.owner || o.active) )
         result.insert( o.fee_payer() );
   }
   void operator()( const proposal_delete_operation& o )const
   {
      if( !o.using_owner_authority )
         result.insert( o.fee_payer() );
   }

   void operator()( const proposal_update_operation& o )const
   {
      result.insert( o.fee_payer() );
      for( auto id : o.active_approvals_to_add )
         result.insert(id);
      for( auto id : o.active_approvals_to_remove )
         result.insert(id);
   }
   void operator()( const custom_operation& o )const
   {
      result.insert( o.required_auths.begin(), o.required_auths.end() );
   }
   void operator()( const assert_operation& o )const
   {
      result.insert( o.fee_payer() );
      result.insert( o.required_auths.begin(), o.required_auths.end() );
   }
};

struct required_owner_visitor
{
   typedef void result_type;

   flat_set<account_id_type>& result;

   required_owner_visitor( flat_set<account_id_type>& r ):result(r){}

   /** for most operations this is a no-op */
   template<typename T>
   void operator()(const T& o)const {}

   void operator()(const account_update_operation& o)const 
   {
      if( o.owner || o.active )
         result.insert( o.account );
   }

   void operator()( const proposal_delete_operation& o )const
   {
      if( o.using_owner_authority )
         result.insert( o.fee_payer() );
   }

   void operator()( const proposal_update_operation& o )const
   {
      for( auto id : o.owner_approvals_to_add )
         result.insert(id);
      for( auto id : o.owner_approvals_to_remove )
         result.insert(id);
   }
};


void operation_get_required_authorities( const operation& op, vector<authority>& result )
{
   op.visit( required_auth_visitor( result ) );
}
void operation_get_required_active_authorities( const operation& op, flat_set<account_id_type>& result )
{
   op.visit( required_active_visitor( result ) );
}
void operation_get_required_owner_authorities( const operation& op, flat_set<account_id_type>& result )
{
   op.visit( required_owner_visitor( result ) );
}

/**
 * @brief Used to validate operations in a polymorphic manner
 */
struct operation_validator
{
   typedef void result_type;
   template<typename T>
   void operator()( const T& v )const { v.validate(); }
};

struct operation_get_required_auth
{
   typedef void result_type;

   flat_set<account_id_type>& active;
   flat_set<account_id_type>& owner;
   vector<authority>&         other;


   operation_get_required_auth( flat_set<account_id_type>& a,
     flat_set<account_id_type>& own,
     vector<authority>&  oth ):active(a),owner(own),other(oth){}

   template<typename T>
   void operator()( const T& v )const 
   { 
      active.insert( v.fee_payer() );
      v.get_required_active_authorities( active ); 
      v.get_required_owner_authorities( owner ); 
      v.get_required_authorities( other );
   }
};

void operation_validate( const operation& op )
{
   op.visit( operation_validator() );
}

void operation_get_required_authorities( const operation& op, 
                                         flat_set<account_id_type>& active,
                                         flat_set<account_id_type>& owner,
                                         vector<authority>&  other )
{
   op.visit( operation_get_required_auth( active, owner, other ) );
}

} } // namespace graphene::chain
