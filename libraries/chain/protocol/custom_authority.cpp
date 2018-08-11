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
#include <graphene/chain/protocol/fee_schedule.hpp>

#include <boost/preprocessor/variadic/to_seq.hpp>
#include <boost/preprocessor/seq/for_each_i.hpp>

#include <type_traits>

namespace graphene { namespace chain {

template <bool B>
using bool_const = std::integral_constant<bool, B>;

struct op_restriction_validate_visitor;
void validate_op_restriction_commons( const operation_restriction& op_restriction );

struct argument_get_units_visitor
{
   typedef uint64_t result_type;

   template<typename T>
   inline result_type operator()( const T& t )
   {
      return 1;
   }

   template<typename T>
   inline result_type operator()( const flat_set<T>& t )
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

// comparable data types can use < <= > >= == !=
template<typename T> struct is_comparable_data_type { static const bool value = false; };

template<> struct is_comparable_data_type<int8_t> { static const bool value = true; };
template<> struct is_comparable_data_type<uint8_t> { static const bool value = true; };
template<> struct is_comparable_data_type<int16_t> { static const bool value = true; };
template<> struct is_comparable_data_type<uint16_t> { static const bool value = true; };
template<> struct is_comparable_data_type<int32_t> { static const bool value = true; };
template<> struct is_comparable_data_type<uint32_t> { static const bool value = true; };
template<> struct is_comparable_data_type<int64_t> { static const bool value = true; };
template<> struct is_comparable_data_type<uint64_t> { static const bool value = true; };
template<> struct is_comparable_data_type<unsigned_int> { static const bool value = true; };
template<> struct is_comparable_data_type<time_point_sec> { static const bool value = true; };

// simple data types can use == !=
template<typename T> struct is_simple_data_type { static const bool value = is_comparable_data_type<T>::value; };

template<> struct is_simple_data_type<bool> { static const bool value = true; };
template<> struct is_simple_data_type<string> { static const bool value = true; };
template<> struct is_simple_data_type<public_key_type> { static const bool value = true; };
template<> struct is_simple_data_type<fc::sha256> { static const bool value = true; };

template<> struct is_simple_data_type<account_id_type> { static const bool value = true; };
template<> struct is_simple_data_type<asset_id_type> { static const bool value = true; };
template<> struct is_simple_data_type<force_settlement_id_type> { static const bool value = true; };
template<> struct is_simple_data_type<committee_member_id_type> { static const bool value = true; };
template<> struct is_simple_data_type<witness_id_type> { static const bool value = true; };
template<> struct is_simple_data_type<limit_order_id_type> { static const bool value = true; };
template<> struct is_simple_data_type<call_order_id_type> { static const bool value = true; };
template<> struct is_simple_data_type<custom_id_type> { static const bool value = true; };
template<> struct is_simple_data_type<proposal_id_type> { static const bool value = true; };
template<> struct is_simple_data_type<withdraw_permission_id_type> { static const bool value = true; };
template<> struct is_simple_data_type<vesting_balance_id_type> { static const bool value = true; };
template<> struct is_simple_data_type<worker_id_type> { static const bool value = true; };
template<> struct is_simple_data_type<balance_id_type> { static const bool value = true; };

template<typename T>
struct compatible_argument_validate_visitor
{
   typedef void result_type;
   const char* name;

   compatible_argument_validate_visitor( const char* _name ) : name(_name) {}

   template<typename ArgType>
   inline result_type operator()( const ArgType& arg );

   // argument type T is compatible with member type T
   inline result_type operator()( const T& arg ) {}
};

// by default incompatible
template<typename T> template<typename ArgType>
inline void compatible_argument_validate_visitor<T>::operator()( const ArgType& arg )
{
   FC_THROW( "Argument '${arg}' is incompatible for ${name}",
             ("arg", arg)("name", name) );
}

// compatible member types X and argument type Y and X != Y
template<> template<> inline void compatible_argument_validate_visitor<char>::operator()( const string& arg ) {}
template<> template<> inline void compatible_argument_validate_visitor<int8_t>::operator()( const int64_t& arg ) {}
template<> template<> inline void compatible_argument_validate_visitor<uint8_t>::operator()( const int64_t& arg ) {}
template<> template<> inline void compatible_argument_validate_visitor<int16_t>::operator()( const int64_t& arg ) {}
template<> template<> inline void compatible_argument_validate_visitor<uint16_t>::operator()( const int64_t& arg ) {}
template<> template<> inline void compatible_argument_validate_visitor<int32_t>::operator()( const int64_t& arg ) {}
template<> template<> inline void compatible_argument_validate_visitor<uint32_t>::operator()( const int64_t& arg ) {}
// no int64_t here because it's already covered by generic template
template<> template<> inline void compatible_argument_validate_visitor<uint64_t>::operator()( const int64_t& arg ) {}
template<> template<> inline void compatible_argument_validate_visitor<unsigned_int>::operator()( const int64_t& arg ) {}

template<typename T>
struct list_argument_validate_visitor
{
   typedef void result_type;
   const char* name;

   list_argument_validate_visitor( const char* _name ) : name(_name) {}

   template<typename ArgType>
   inline result_type operator()( const ArgType& arg );

   // argument type flat_set<T> is compatible with member list_like<T>
   inline result_type operator()( const flat_set<T>& arg ) {}
};

// by default incompatible
template<typename T> template<typename ArgType>
inline void list_argument_validate_visitor<T>::operator()( const ArgType& arg )
{
   FC_THROW( "Argument '${arg}' is incompatible, requires a compatible flat_set for ${name}",
             ("arg", arg)("name", name) );
}

// compatible member types X and argument type flat_set<Y> and X != Y
template<> template<> inline void list_argument_validate_visitor<char>::operator()( const flat_set<string>& arg ) {}
template<> template<> inline void list_argument_validate_visitor<int8_t>::operator()( const flat_set<int64_t>& arg ) {}
template<> template<> inline void list_argument_validate_visitor<uint8_t>::operator()( const flat_set<int64_t>& arg ) {}
template<> template<> inline void list_argument_validate_visitor<int16_t>::operator()( const flat_set<int64_t>& arg ) {}
template<> template<> inline void list_argument_validate_visitor<uint16_t>::operator()( const flat_set<int64_t>& arg ) {}
template<> template<> inline void list_argument_validate_visitor<int32_t>::operator()( const flat_set<int64_t>& arg ) {}
template<> template<> inline void list_argument_validate_visitor<uint32_t>::operator()( const flat_set<int64_t>& arg ) {}
// no int64_t here because it's already covered by generic template
template<> template<> inline void list_argument_validate_visitor<uint64_t>::operator()( const flat_set<int64_t>& arg ) {}
template<> template<> inline void list_argument_validate_visitor<unsigned_int>::operator()( const flat_set<int64_t>& arg ) {}

template<typename T>
struct attr_argument_validate_visitor
{
   typedef void result_type;
   const char* name;

   attr_argument_validate_visitor( const char* _name ) : name(_name) {}

   template<typename ArgType>
   inline result_type operator()( const ArgType& arg )
   {
      FC_THROW( "Argument '${arg}' is incompatible, requires an attr_restriction_type for ${name}",
                ("arg", arg)("name", name) );
   }

   result_type operator()( const attr_restriction_type& arg ) // vector<operation_restriction>
   {
      // Recursively check T.members
      for( const operation_restriction& restriction : arg )
      {
         // validate common data
         validate_op_restriction_commons( restriction );
         // validate member-related
         op_restriction_validate_visitor vtor( restriction );
         vtor( *((const T*)nullptr) );
      }
   }
};


struct number_argument_validate_visitor
{
   typedef void result_type;
   const char* name;

   number_argument_validate_visitor( const char* _name ) : name(_name) {}

   template<typename ArgType>
   inline result_type operator()( const ArgType& arg )
   {
      FC_THROW( "Argument '${arg}' is incompatible, requires a number type for ${name}",
                ("arg", arg)("name", name) );
   }

   inline result_type operator()( const int64_t& arg ) {}
};

inline bool is_subset_function( const operation_restriction::function_type function )
{
   return ( function == operation_restriction::func_in
            || function == operation_restriction::func_not_in );
}

inline bool is_superset_function( const operation_restriction::function_type function )
{
   return ( function == operation_restriction::func_has_all
            || function == operation_restriction::func_has_none );
}

inline bool is_equal_function( const operation_restriction::function_type function )
{
   return ( function == operation_restriction::func_eq
            || function == operation_restriction::func_ne );
}

inline bool is_compare_function( const operation_restriction::function_type function )
{
   return ( function == operation_restriction::func_eq
            || function == operation_restriction::func_ne
            || function == operation_restriction::func_lt
            || function == operation_restriction::func_le
            || function == operation_restriction::func_gt
            || function == operation_restriction::func_ge );
}

void require_compare_function( const operation_restriction& op_restriction, const char* name )
{
   // function should be a compare function
   FC_ASSERT( is_compare_function( op_restriction.function ),
              "Function '${func}' is incompatible, requires a compare function for ${name}",
              ("func", op_restriction.function)("name", name) );
}

template<typename T>
void require_compatible_argument( const operation_restriction& op_restriction, const char* name )
{
   // argument should be T-compatible
   compatible_argument_validate_visitor<T> vtor( name );
   op_restriction.argument.visit( vtor );
}

template<typename T>
void require_list_argument( const operation_restriction& op_restriction, const char* name )
{
   // argument should be flat_set< T-compatible >
   list_argument_validate_visitor<T> vtor( name );
   op_restriction.argument.visit( vtor );
}

template<typename T>
void require_attr_argument( const operation_restriction& op_restriction, const char* name )
{
   // argument should be flat_set< T-compatible >
   attr_argument_validate_visitor<T> vtor( name );
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

   op_restriction_validation_helper( const operation_restriction& opr ) : op_restriction(opr)
   {
      FC_ASSERT( op_restriction.member_modifier.value == operation_restriction::mmod_none,
                 "Internal error: should only use this helper for mmod_none" );
   }

   template<typename T> // comparable, simple data type
   void validate_member( const char* name, std::true_type, std::true_type )
   {
      if( is_subset_function( op_restriction.function ) )
      {
         // argument need to be a list, and type should be compatible to T
         require_list_argument<T>( op_restriction, name );
      }
      else if( is_compare_function( op_restriction.function ) )
      {
         // argument need to be compatible
         require_compatible_argument<T>( op_restriction, name );
      }
      else
      {
         FC_THROW( "Function '${func}' is incompatible, requires a compare function or subset function for ${name}",
                   ("func", op_restriction.function)("name", name) );
      }
   }

   template<typename T> // non-comparable, simple data type
   void validate_member( const char* name, std::false_type, std::true_type )
   {
      if( is_subset_function( op_restriction.function ) )
      {
         // argument need to be a list, and type should be compatible to T
         require_list_argument<T>( op_restriction, name );
      }
      else if( is_equal_function( op_restriction.function ) )
      {
         // argument need to be compatible
         require_compatible_argument<T>( op_restriction, name );
      }
      else
      {
         FC_THROW( "Function '${func}' is incompatible, requires an equal function or subset function for ${name}",
                   ("func", op_restriction.function)("name", name) );
      }
   }

   template<typename T> // non-compatible, not-simple, aka object-like type
   void validate_member( const char* name, std::false_type, std::false_type )
   {
      FC_ASSERT( op_restriction.function == operation_restriction::func_attr,
                 "Object-like member '${name}' can only use func_attr",
                 ("name", name) );
      // argument need to be a attribute_restriction
      require_attr_argument<T>( op_restriction, name );
   }

   template<typename T>
   void validate_list_like_member( const char* name )
   {
      FC_ASSERT( is_superset_function( op_restriction.function ),
                 "List-like member '${name}' can only use func_has_all or func_has_none",
                 ("name", name) );
      FC_ASSERT( is_simple_data_type<T>::value,
                 "Simple data type in list-like member '${name}' is required",
                 ("name", name) );
      // argument need to be a list, and type should be compatible to T
      require_list_argument<T>( op_restriction, name );
   }

   template<typename T>
   void validate_by_member_type( const char* name, const T* t )
   {
      const bool is_comparable = is_comparable_data_type<T>::value;
      const bool is_simple     = is_simple_data_type<T>::value;
      validate_member<T>( name, bool_const<is_comparable>(), bool_const<is_simple>() );
   }

   template<typename T>
   void validate_by_member_type( const char* name, const optional<T>* t )
   {
      // extract the underlying type
      validate_by_member_type( name, (const T*)nullptr );
   }

   template<typename T>
   void validate_by_member_type( const char* name, const safe<T>* t )
   {
      // extract the underlying type
      validate_by_member_type( name, (const T*)nullptr );
   }

   template<typename T>
   void validate_by_member_type( const char* name, const smart_ref<T>* t )
   {
      // extract the underlying type
      validate_by_member_type( name, (const T*)nullptr );
   }

   template<typename T>
   void validate_by_member_type( const char* name, const extension<T>* t )
   {
      // extract the underlying type
      validate_by_member_type( name, (const T*)nullptr );
   }

   template<typename T>
   void validate_by_member_type( const char* name, const vector<T>* t )
   {
      validate_list_like_member<T>( name );
   }

   template<typename T>
   void validate_by_member_type( const char* name, const set<T>* t )
   {
      validate_list_like_member<T>( name );
   }

   template<typename T>
   void validate_by_member_type( const char* name, const flat_set<T>* t )
   {
      validate_list_like_member<T>( name );
   }

   template<typename... T>
   void validate_by_member_type( const char* name, const flat_map<T...>* t )
   {
      FC_THROW( "Restriction on '${name}' is not supported due to its type",
                ("name", name) );
   }

   template<typename... T>
   void validate_by_member_type( const char* name, const static_variant<T...>* t )
   {
      FC_THROW( "Restriction on '${name}' is not supported due to its type",
                ("name", name) );
   }

   void validate_by_member_type( const char* name, const fba_accumulator_id_type* t )
   {
      FC_THROW( "Restriction on '${name}' is not supported due to its type",
                ("name", name) );
   }

};

template< typename OpType >
struct by_index_member_validate_visitor
{
   typedef void result_type;

   const operation_restriction& op_restriction;

   by_index_member_validate_visitor( const operation_restriction& opr ) : op_restriction(opr) {}

   template<typename Member, class Class, Member (Class::*member)>
   void operator()( const char* name )const
   {
      op_restriction_validation_helper helper( op_restriction );
      helper.validate_by_member_type( name, (const Member*)nullptr );
   }
};

struct op_restriction_validate_visitor
{
   typedef void result_type;
   const operation_restriction& op_restriction;

   op_restriction_validate_visitor( const operation_restriction& opr ) : op_restriction(opr) {}

   template<typename OpType>
   result_type operator()( const OpType& ) // Note: the parameter is a reference of *nullptr, we should not use it
   {
      FC_ASSERT( op_restriction.member < fc::reflector<OpType>::total_member_count,
                 "member number ${m} is too large",
                 ("m",op_restriction.member) );

      // other member modifiers have been checked outside, so only check mmod_none here
      if( op_restriction.member_modifier.value == operation_restriction::mmod_none )
      {
         // member_validate_visitor<OpType> vtor( op_restriction );
         // fc::reflector<OpType>::visit( vtor );

         // NOTE: above implementation iterates through all reflected members to find specified member,
         //       possible to improve performance by visiting specified member by index/number directly
         // ------------------------
         // So we have the code below
         // TODO cleanup above comments

         by_index_member_validate_visitor<OpType> vtor( op_restriction );
         fc::reflector<OpType>::visit_local_member( vtor, op_restriction.member.value );
      }
   }

};

#define GRAPHENE_OP_IDX_CASE_VISIT(r, data, I, elem) \
   case I : \
      operation::visit< I >( vtor ); \
      break;

void validate_op_restriction_by_op_type( const operation_restriction& op_restriction, unsigned_int op_type )
{
   op_restriction_validate_visitor vtor( op_restriction );
   switch( op_type.value )
   {
      // will have code like below for all operations:
      //    case op_type : operation::visit<op_type>( visitor )
      BOOST_PP_SEQ_FOR_EACH_I( GRAPHENE_OP_IDX_CASE_VISIT, , BOOST_PP_VARIADIC_TO_SEQ( GRAPHENE_OPERATIONS_VARIADIC ) )

      default:
         break;
   }

}

void validate_op_restriction_commons( const operation_restriction& op_restriction )
{
   // validate member modifier
   FC_ASSERT( op_restriction.member_modifier < operation_restriction::MEMBER_MODIFIER_TYPE_COUNT,
              "member modifier number ${mm} is too large",
              ("mm", op_restriction.member_modifier) );

   if( op_restriction.member_modifier.value == operation_restriction::mmod_size )
   {
      require_compare_function( op_restriction, "size modifier" );
      require_number_argument( op_restriction, "size modifier" );
   }
   else if( op_restriction.member_modifier.value == operation_restriction::mmod_pack_size )
   {
      require_compare_function( op_restriction, "pack_size modifier" );
      require_number_argument( op_restriction, "pack_size modifier" );
   }

   // validate function
   FC_ASSERT( op_restriction.function < operation_restriction::FUNCTION_TYPE_COUNT,
              "function number ${f} is too large",
              ("f", op_restriction.function) );
}

void operation_restriction::validate( unsigned_int op_type )const
{
   // validate common data
   validate_op_restriction_commons( *this );
   // validate details by operation_type
   validate_op_restriction_by_op_type( *this, op_type );
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
   FC_ASSERT( operation_type < operation::count(), "operation_type is too large" );

   // Note: allow auths to be empty
   //FC_ASSERT( auth.num_auths() > 0, "Can not set empty auth" );
   FC_ASSERT( auth.address_auths.size() == 0, "Address auth is not supported" );
   // Note: allow auths to be impossible
   //FC_ASSERT( !auth.is_impossible(), "cannot use an imposible authority threshold" );

   // Note: allow restrictions to be empty
   for( const auto& restriction : restrictions )
   {
      // recursively validate member index and argument type
      restriction.validate( operation_type );
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
