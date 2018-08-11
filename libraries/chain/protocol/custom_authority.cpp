/*
 * Copyright (c) 2018 Abit More, and contributors.
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
#include <graphene/chain/protocol/custom_authority.hpp>
#include <graphene/chain/protocol/operations.hpp>

namespace graphene { namespace chain {

struct argument_get_units_visitor
{
   typedef uint64_t result_type;

   template<typename T>
   result_type operator()( const T& t )
   {
      return 1;
   }

   template<typename T>
   result_type operator()( const flat_set<T>& t )
   {
      return t.size();
   }

   result_type operator()( const attr_restriction_type& t )
   {
      result_type result = 0;
      for( const auto& restriction : t )
      {
         result += restriction.argument.visit(*this);
      }
      return result;
   }
};

uint64_t operation_restriction::get_units()const
{
   argument_get_units_visitor vtor;
   return argument.visit( vtor );
}

namespace detail {
   template<typename T> bool is_simple_data_type() { return false; }

   template<> bool is_simple_data_type<bool>() { return true; }
   template<> bool is_simple_data_type<char>() { return true; }
   template<> bool is_simple_data_type<int8_t>() { return true; }
   template<> bool is_simple_data_type<uint8_t>() { return true; }
   template<> bool is_simple_data_type<int16_t>() { return true; }
   template<> bool is_simple_data_type<uint16_t>() { return true; }
   template<> bool is_simple_data_type<int32_t>() { return true; }
   template<> bool is_simple_data_type<uint32_t>() { return true; }
   template<> bool is_simple_data_type<int64_t>() { return true; }
   template<> bool is_simple_data_type<uint64_t>() { return true; }
   template<> bool is_simple_data_type<unsigned_int>() { return true; }

   template<> bool is_simple_data_type<string>() { return true; }

   template<> bool is_simple_data_type<account_id_type>() { return true; }
   template<> bool is_simple_data_type<asset_id_type>() { return true; }
   template<> bool is_simple_data_type<force_settlement_id_type>() { return true; }
   template<> bool is_simple_data_type<committee_member_id_type>() { return true; }
   template<> bool is_simple_data_type<witness_id_type>() { return true; }
   template<> bool is_simple_data_type<limit_order_id_type>() { return true; }
   template<> bool is_simple_data_type<call_order_id_type>() { return true; }
   template<> bool is_simple_data_type<custom_id_type>() { return true; }
   template<> bool is_simple_data_type<proposal_id_type>() { return true; }
   template<> bool is_simple_data_type<withdraw_permission_id_type>() { return true; }
   template<> bool is_simple_data_type<vesting_balance_id_type>() { return true; }
   template<> bool is_simple_data_type<worker_id_type>() { return true; }
   template<> bool is_simple_data_type<balance_id_type>() { return true; }
}


template<typename T>
struct list_argument_validate_visitor
{
   typedef void result_type;
   const char* name;

   list_argument_validate_visitor( const char* _name ) : name(_name) {}

   template<typename ArgType>
   result_type operator()( const ArgType& arg );

   // argument type flat_set<T> is compatible with member list_like<T>
   result_type operator()( const flat_set<T>& arg ) {}
};

// by default incompatible
template<typename T> template<typename ArgType>
void list_argument_validate_visitor<T>::operator()( const ArgType& arg )
{
   FC_THROW( "Argument type '${a}' is incompatible with list-like member '${m}' whose contained type is ${t}",
             ("a", fc::get_typename<ArgType>::name())
             ("m", name)
             ("t", fc::get_typename<T>::name()) );
}

// compatible types X : flat_set<Y> and X != Y
template<> template<> void list_argument_validate_visitor<char>::operator()( const flat_set<string>& arg ) {}
template<> template<> void list_argument_validate_visitor<int8_t>::operator()( const flat_set<int64_t>& arg ) {}
template<> template<> void list_argument_validate_visitor<uint8_t>::operator()( const flat_set<int64_t>& arg ) {}
template<> template<> void list_argument_validate_visitor<int16_t>::operator()( const flat_set<int64_t>& arg ) {}
template<> template<> void list_argument_validate_visitor<uint16_t>::operator()( const flat_set<int64_t>& arg ) {}
template<> template<> void list_argument_validate_visitor<int32_t>::operator()( const flat_set<int64_t>& arg ) {}
template<> template<> void list_argument_validate_visitor<uint32_t>::operator()( const flat_set<int64_t>& arg ) {}
// no int64_t here because it's already covered by generic template
template<> template<> void list_argument_validate_visitor<uint64_t>::operator()( const flat_set<int64_t>& arg ) {}
template<> template<> void list_argument_validate_visitor<unsigned_int>::operator()( const flat_set<int64_t>& arg ) {}

struct number_argument_validate_visitor
{
   typedef void result_type;
   const char* name;

   number_argument_validate_visitor( const char* _name ) : name(_name) {}

   template<typename ArgType>
   result_type operator()( const ArgType& arg )
   {
      FC_THROW( "Can only use a number type as argument for ${name}",
                ("name", name) );
   }

   result_type operator()( const int64_t& arg ) {}
};

void require_comparative_function( const operation_restriction& op_restriction, const char* name )
{
   // function should be a comparative function
   FC_ASSERT( op_restriction.function == operation_restriction::func_eq
              || op_restriction.function == operation_restriction::func_ne
              || op_restriction.function == operation_restriction::func_lt
              || op_restriction.function == operation_restriction::func_le
              || op_restriction.function == operation_restriction::func_gt
              || op_restriction.function == operation_restriction::func_ge,
              "Can only use comparative function for ${name}",
              ("name", name) );
}

template<typename T>
void require_list_argument( const operation_restriction& op_restriction, const char* name )
{
   // argument should be flat_set< T-compatible >
   list_argument_validate_visitor<T> vtor( name );
   op_restriction.argument.visit( vtor );
}

void require_number_argument( const operation_restriction& op_restriction, const char* name )
{
   // argument should be a number
   number_argument_validate_visitor vtor( name );
   op_restriction.argument.visit( vtor );
}

struct op_restriction_validation_helper
{
   const operation_restriction& op_restriction; ///< the restriction

   op_restriction_validation_helper( const operation_restriction& opr ) : op_restriction(opr) {}

   // by default don't support undefined types
   // FIXME need it for recursion
   template<typename T>
   void validate_by_member_type( const char* name, const T& t )
   {
      FC_THROW( "Restriction on ${name} is not supported due to its type ${type}",
                ("name", name)("type", fc::get_typename<T>::name()) );
   }

   template<typename T>
   void validate_by_member_type( const char* name, const optional<T>& t )
   {
      if( op_restriction.member_modifier.value == operation_restriction::mmod_none )
      {
         // extract the underlying type
         validate_by_member_type<T>( name, T() );
      }
   }

   template<typename T>
   void validate_by_member_type( const char* name, const safe<T>& t )
   {
      if( op_restriction.member_modifier.value == operation_restriction::mmod_none )
      {
         // extract the underlying type
         validate_by_member_type<T>( name, t.value );
      }
   }

   template<typename T>
   void validate_by_member_type( const char* name, const smart_ref<T>& t )
   {
      if( op_restriction.member_modifier.value == operation_restriction::mmod_none )
      {
         // extract the underlying type
         validate_by_member_type<T>( name, t.value );
      }
   }

   template<typename T>
   void validate_list_like_member( const char* name )
   {
      if( op_restriction.member_modifier.value == operation_restriction::mmod_none )
      {
         FC_ASSERT( op_restriction.function == operation_restriction::func_has_all
                    || op_restriction.function == operation_restriction::func_has_none,
                    "List-like member '${name}' can only use func_has_all or func_has_none",
                    ("name", name) );
         FC_ASSERT( detail::is_simple_data_type<T>(),
                    "Simple data type in list-like member '${name}' is required",
                    ("name", name) );
         // validate argument, need to be a list, and type should be compatible to T
         require_list_argument<T>( op_restriction, name );
      }
   }

   template<typename T>
   void validate_by_member_type( const char* name, const vector<T>& t )
   {
      validate_list_like_member<T>( name );
   }

   template<typename T>
   void validate_by_member_type( const char* name, const set<T>& t )
   {
      validate_list_like_member<T>( name );
   }

   template<typename T>
   void validate_by_member_type( const char* name, const flat_set<T>& t )
   {
      validate_list_like_member<T>( name );
   }
};

/*
   result_type operator()( const attr_restriction_type& t )
   {
      for( const auto& restriction : t )
      {
         restriction.argument.visit(*this);
      }
   }
};
*/

template< typename OpType >
struct member_validate_visitor
{
   member_validate_visitor( const OpType& o, const operation_restriction& opr ) : op(o), op_restriction(opr) {}

   template<typename Member, class Class, Member (Class::*member)>
   void operator()( const char* name )const
   {
      if( which == op_restriction.member ) // `op.*member` is the specified member
      {

         // Firstly we check if the member type is supported

         //validate_member( op_restriction );

         op_restriction_validation_helper helper( op_restriction );
         helper.validate_by_member_type( name, op.*member );

         // Giving the member type, we know what function is available
         // Giving function, we know what argument it should be

         //function_validate_visitor vtor( op_restriction );

         // validate argument
         //argument_validate_visitor vtor;
         //argument.visit( vtor );
      }
      ++which;
   }

   mutable uint32_t which = 0;
   const OpType& op;                            ///< the operation
   const operation_restriction& op_restriction; ///< the restriction
};

struct op_restriction_validate_visitor
{
   typedef void result_type;
   const operation_restriction& op_restriction;

   op_restriction_validate_visitor( const operation_restriction& opr ) : op_restriction(opr) {}

   template<typename OpType>
   result_type operator()( const OpType& op )
   {
      FC_ASSERT( op_restriction.member < fc::reflector<OpType>::total_member_count,
                 "member number ${m} is too large",
                 ("m",op_restriction.member) );

      // TODO: this implementation iterates through all reflected members to find specified member,
      //       possible to improve performance by visiting specified member by index/number directly
      member_validate_visitor<OpType> vtor( op, op_restriction );
      fc::reflector<OpType>::visit( vtor );
   }

};

void operation_restriction::validate( const op_wrapper& opw )const
{
   // validate member modifier
   FC_ASSERT( member_modifier < MEMBER_MODIFIER_TYPE_COUNT,
              "member modifier number ${mm} is too large",
              ("mm",member_modifier) );

   if( member_modifier.value == mmod_size )
   {
      require_comparative_function( *this, "size modifier" );
      require_number_argument( *this, "size modifier" );
   }
   else if( member_modifier.value == mmod_pack_size )
   {
      require_comparative_function( *this, "pack_size modifier" );
      require_number_argument( *this, "pack_size modifier" );
   }

   // validate function
   FC_ASSERT( function < FUNCTION_TYPE_COUNT,
              "function number ${f} is too large",
              ("f",function) );

   // validate details
   op_restriction_validate_visitor vtor( *this );
   opw.op.visit( vtor );
}

share_type custom_authority_create_operation::calculate_fee( const fee_parameters_type& k )const
{
   share_type core_fee_required = k.basic_fee;

   if( enabled )
   {
      share_type unit_fee = k.price_per_k_unit;
      unit_fee *= (valid_to - valid_from).to_seconds();
      unit_fee *= auth.num_auths();
      uint64_t restriction_units = 0;
      for( const auto& restriction : restrictions )
      {
         restriction_units += restriction.get_units();
      }
      unit_fee *= restriction_units;
      unit_fee /= 1000;
      core_fee_required += unit_fee;
   }

   return core_fee_required;
}

void custom_authority_create_operation::validate()const
{
   FC_ASSERT( fee.amount >= 0, "Fee amount can not be negative" );

   FC_ASSERT( account != GRAPHENE_TEMP_ACCOUNT
              && account != GRAPHENE_COMMITTEE_ACCOUNT
              && account != GRAPHENE_WITNESS_ACCOUNT
              && account != GRAPHENE_RELAXED_COMMITTEE_ACCOUNT,
              "Can not create custom authority for special accounts" );

   FC_ASSERT( valid_from < valid_to, "valid_from must be earlier than valid_to" );

   // Note: when adding new operation with hard fork, need to check more strictly in evaluator
   // TODO add code in evaluator
   FC_ASSERT( operation_type < operation::count(), "operation type too large" );
   operation op;
   op.set_which( operation_type );
   op_wrapper opw( op );

   // Note: allow auths to be empty
   //FC_ASSERT( auth.num_auths() > 0, "Can not set empty auth" );
   FC_ASSERT( auth.address_auths.size() == 0, "Address auth is not supported" );
   // Note: allow auths to be impossible
   //FC_ASSERT( !auth.is_impossible(), "cannot use an imposible authority threshold" );

   // Note: allow restrictions to be empty
   for( const auto& restriction : restrictions )
   {
      // recursively validate member index and argument type
      restriction.validate( opw );
   }
}

share_type custom_authority_update_operation::calculate_fee( const fee_parameters_type& k )const
{
   share_type core_fee_required = k.basic_fee;

   share_type unit_fee = k.price_per_k_unit;
   unit_fee *= delta_units;
   unit_fee /= 1000;

   return core_fee_required + unit_fee;
}

void custom_authority_update_operation::validate()const
{
   FC_ASSERT( fee.amount >= 0, "Fee amount can not be negative" );

   FC_ASSERT( account != GRAPHENE_TEMP_ACCOUNT
              && account != GRAPHENE_COMMITTEE_ACCOUNT
              && account != GRAPHENE_WITNESS_ACCOUNT
              && account != GRAPHENE_RELAXED_COMMITTEE_ACCOUNT,
              "Can not create custom authority for special accounts" );
/*
   FC_ASSERT( valid_from < valid_to, "valid_from must be earlier than valid_to" );

   // Note: when adding new operation with hard fork, need to check more strictly in evaluator
   // TODO add code in evaluator
   FC_ASSERT( operation_type < operation::count(), "operation type too large" );

   FC_ASSERT( auth.num_auths() > 0, "Can not set empty auth" );
   FC_ASSERT( auth.address_auths.size() == 0, "Address auth is not supported" );
   //FC_ASSERT( !auth.is_impossible(), "cannot use an imposible authority threshold" );

   // Note: allow restrictions to be empty
   for( const auto& restriction : restrictions )
   {
      // TODO recursively validate member index and argument type
      restriction.validate( operation_type );
   }
*/
}

void custom_authority_delete_operation::validate()const
{
}

} } // graphene::chain
