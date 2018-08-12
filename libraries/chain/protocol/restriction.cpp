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
#include <graphene/chain/protocol/restriction.hpp>
#include <graphene/chain/protocol/operations.hpp>
#include <graphene/chain/protocol/fee_schedule.hpp>

#include <boost/preprocessor/variadic/to_seq.hpp>
#include <boost/preprocessor/seq/for_each_i.hpp>

#include <type_traits>

namespace graphene { namespace chain {

template <bool B>
using bool_const = std::integral_constant<bool, B>;

struct restriction_validation_visitor;
void validate_restriction_commons( const restriction_type& op_restriction );

struct argument_get_units_visitor
{
   typedef uint64_t result_type;

   template<typename T>
   inline result_type operator()( const T& t )
   {
      return 1;
   }

   inline result_type operator()( const fc::sha256& t )
   {
      return 4;
   }

   inline result_type operator()( const public_key_type& t )
   {
      return 4;
   }

   inline result_type operator()( const string& t )
   {
      return ( t.size() + 7 ) / 8;
   }

   template<typename T>
   inline result_type operator()( const flat_set<T>& t )
   {
      return t.size() * (*this)( *((const T*)nullptr) );
   }

   template<typename T>
   inline result_type operator()( const flat_set<string>& t )
   {
      result_type result = 0;
      for( const auto& s : t )
      {
         result += ( s.size() + 7 ) / 8;
      }
      return result;
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

uint64_t restriction_type::get_units()const
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
struct compatible_argument_validation_visitor
{
   typedef void result_type;
   const char* name;

   compatible_argument_validation_visitor( const char* _name ) : name(_name) {}

   template<typename ArgType>
   inline result_type operator()( const ArgType& arg ) // by default incompatible
   {
      FC_THROW( "Argument '${arg}' is incompatible for ${name}",
                ("arg", arg)("name", name) );
   }

   // argument type T is compatible with member type T
   inline result_type operator()( const T& arg ) {}
};

// compatible member types X and argument type Y and X != Y
template<> template<> inline void compatible_argument_validation_visitor<char>::operator()( const string& arg ) {}
template<> template<> inline void compatible_argument_validation_visitor<int8_t>::operator()( const int64_t& arg ) {}
template<> template<> inline void compatible_argument_validation_visitor<uint8_t>::operator()( const int64_t& arg ) {}
template<> template<> inline void compatible_argument_validation_visitor<int16_t>::operator()( const int64_t& arg ) {}
template<> template<> inline void compatible_argument_validation_visitor<uint16_t>::operator()( const int64_t& arg ) {}
template<> template<> inline void compatible_argument_validation_visitor<int32_t>::operator()( const int64_t& arg ) {}
template<> template<> inline void compatible_argument_validation_visitor<uint32_t>::operator()( const int64_t& arg ) {}
// no int64_t here because it's already covered by generic template
template<> template<> inline void compatible_argument_validation_visitor<uint64_t>::operator()( const int64_t& arg ) {}
template<> template<> inline void compatible_argument_validation_visitor<unsigned_int>::operator()( const int64_t& arg ) {}

template<typename T>
struct list_argument_validation_visitor
{
   typedef void result_type;
   const char* name;

   list_argument_validation_visitor( const char* _name ) : name(_name) {}

   template<typename ArgType>
   inline result_type operator()( const ArgType& arg ) // by default incompatible
   {
      FC_THROW( "Argument '${arg}' is incompatible, requires a compatible flat_set for ${name}",
                ("arg", arg)("name", name) );
   }

   // argument type flat_set<T> is compatible with member list_like<T>
   inline result_type operator()( const flat_set<T>& arg ) {}
};

// compatible member types X and argument type flat_set<Y> and X != Y
template<> template<> inline void list_argument_validation_visitor<char>::operator()( const flat_set<string>& arg ) {}
template<> template<> inline void list_argument_validation_visitor<int8_t>::operator()( const flat_set<int64_t>& arg ) {}
template<> template<> inline void list_argument_validation_visitor<uint8_t>::operator()( const flat_set<int64_t>& arg ) {}
template<> template<> inline void list_argument_validation_visitor<int16_t>::operator()( const flat_set<int64_t>& arg ) {}
template<> template<> inline void list_argument_validation_visitor<uint16_t>::operator()( const flat_set<int64_t>& arg ) {}
template<> template<> inline void list_argument_validation_visitor<int32_t>::operator()( const flat_set<int64_t>& arg ) {}
template<> template<> inline void list_argument_validation_visitor<uint32_t>::operator()( const flat_set<int64_t>& arg ) {}
// no int64_t here because it's already covered by generic template
template<> template<> inline void list_argument_validation_visitor<uint64_t>::operator()( const flat_set<int64_t>& arg ) {}
template<> template<> inline void list_argument_validation_visitor<unsigned_int>::operator()( const flat_set<int64_t>& arg ) {}

template<typename T>
struct attr_argument_validation_visitor
{
   typedef void result_type;
   const char* name;

   attr_argument_validation_visitor( const char* _name ) : name(_name) {}

   template<typename ArgType>
   inline result_type operator()( const ArgType& arg )
   {
      FC_THROW( "Argument '${arg}' is incompatible, requires an attr_restriction_type for ${name}",
                ("arg", arg)("name", name) );
   }

   result_type operator()( const attr_restriction_type& arg ) // vector<restriction_type>
   {
      // Recursively check T.members
      for( const restriction_type& restriction : arg )
      {
         // validate common data
         validate_restriction_commons( restriction );
         // validate member-related data
         restriction_validation_visitor vtor( restriction );
         vtor( *((const T*)nullptr) );
      }
   }
};

struct number_argument_validation_visitor
{
   typedef void result_type;
   const char* name;

   number_argument_validation_visitor( const char* _name ) : name(_name) {}

   template<typename ArgType>
   inline result_type operator()( const ArgType& arg )
   {
      FC_THROW( "Argument '${arg}' is incompatible, requires a number type for ${name}",
                ("arg", arg)("name", name) );
   }

   inline result_type operator()( const int64_t& arg ) {}
};

inline bool is_subset_function( const restriction_type::function_type function )
{
   return ( function == restriction_type::func_in
            || function == restriction_type::func_not_in );
}

inline bool is_superset_function( const restriction_type::function_type function )
{
   return ( function == restriction_type::func_has_all
            || function == restriction_type::func_has_none );
}

inline bool is_equal_function( const restriction_type::function_type function )
{
   return ( function == restriction_type::func_eq
            || function == restriction_type::func_ne );
}

inline bool is_compare_function( const restriction_type::function_type function )
{
   return ( function == restriction_type::func_eq
            || function == restriction_type::func_ne
            || function == restriction_type::func_lt
            || function == restriction_type::func_le
            || function == restriction_type::func_gt
            || function == restriction_type::func_ge );
}

void require_compare_function( const restriction_type& op_restriction, const char* name )
{
   // function should be a compare function
   FC_ASSERT( is_compare_function( op_restriction.function ),
              "Function '${func}' is incompatible, requires a compare function for ${name}",
              ("func", op_restriction.function)("name", name) );
}

template<typename T>
void require_compatible_argument( const restriction_type& op_restriction, const char* name )
{
   // argument should be T-compatible
   compatible_argument_validation_visitor<T> vtor( name );
   op_restriction.argument.visit( vtor );
}

template<typename T>
void require_compatible_list_argument( const restriction_type& op_restriction, const char* name )
{
   // argument should be flat_set< T-compatible >
   list_argument_validation_visitor<T> vtor( name );
   op_restriction.argument.visit( vtor );
}

template<typename T>
void require_attr_argument( const restriction_type& op_restriction, const char* name )
{
   // argument should be attr and compatible to T
   attr_argument_validation_visitor<T> vtor( name );
   op_restriction.argument.visit( vtor );
}

void require_number_argument( const restriction_type& op_restriction, const char* name )
{
   // argument should be a number
   number_argument_validation_visitor vtor( name );
   op_restriction.argument.visit( vtor );
}

struct restriction_validation_helper
{
   const restriction_type& op_restriction; ///< the restriction

   restriction_validation_helper( const restriction_type& opr ) : op_restriction(opr)
   {
      FC_ASSERT( op_restriction.member_modifier.value == restriction_type::mmod_none,
                 "Internal error: should only use this helper for mmod_none" );
   }

   template<typename T> // comparable, simple data type
   void validate_member( const char* name, std::true_type, std::true_type )
   {
      if( is_subset_function( op_restriction.function ) )
      {
         // argument need to be a list, and type should be compatible to T
         require_compatible_list_argument<T>( op_restriction, name );
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
         require_compatible_list_argument<T>( op_restriction, name );
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

   template<typename T> // non-comparable, not-simple, aka object-like type
   void validate_member( const char* name, std::false_type, std::false_type )
   {
      FC_ASSERT( op_restriction.function == restriction_type::func_attr,
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
      require_compatible_list_argument<T>( op_restriction, name );
   }

   template<typename T>
   void validate_by_member_type( const char* name, const T* t ) // generic template
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

struct member_validation_visitor
{
   typedef void result_type;

   const restriction_type& op_restriction;

   member_validation_visitor( const restriction_type& opr ) : op_restriction(opr) {}

   template<typename Member, class Class, Member (Class::*member)>
   void operator()( const char* name )const
   {
      restriction_validation_helper helper( op_restriction );
      helper.validate_by_member_type( name, (const Member*)nullptr );
   }
};

struct restriction_validation_visitor
{
   typedef void result_type;
   const restriction_type& op_restriction;

   restriction_validation_visitor( const restriction_type& opr ) : op_restriction(opr) {}

   template<typename OpType>
   result_type operator()( const OpType& ) // Note: the parameter is a reference of *nullptr, we should not use it
   {
      FC_ASSERT( op_restriction.member < fc::reflector<OpType>::total_member_count,
                 "member number ${m} is too large",
                 ("m",op_restriction.member) );

      // other member modifiers should have been checked outside, so only check mmod_none here
      if( op_restriction.member_modifier.value == restriction_type::mmod_none )
      {
         member_validation_visitor vtor( op_restriction );
         fc::reflector<OpType>::visit_local_member( vtor, op_restriction.member.value );
      }
   }

};

#define GRAPHENE_OP_IDX_CASE_VISIT(r, data, I, elem) \
   case I : \
      operation::visit< I >( vtor ); \
      break;

void validate_restriction_details( const restriction_type& op_restriction, unsigned_int op_type )
{
   restriction_validation_visitor vtor( op_restriction );
   switch( op_type.value )
   {
      // will have code like below for all operations:
      //    case op_type : operation::visit<op_type>( visitor )
      BOOST_PP_SEQ_FOR_EACH_I( GRAPHENE_OP_IDX_CASE_VISIT, , BOOST_PP_VARIADIC_TO_SEQ( GRAPHENE_OPERATIONS_VARIADIC ) )

      default:
         break;
   }

}

void validate_restriction_commons( const restriction_type& op_restriction )
{
   // validate member modifier
   FC_ASSERT( op_restriction.member_modifier < restriction_type::MEMBER_MODIFIER_TYPE_COUNT,
              "member modifier number ${mm} is too large",
              ("mm", op_restriction.member_modifier) );

   if( op_restriction.member_modifier.value == restriction_type::mmod_size )
   {
      require_compare_function( op_restriction, "size modifier" );
      require_number_argument( op_restriction, "size modifier" );
   }
   else if( op_restriction.member_modifier.value == restriction_type::mmod_pack_size )
   {
      require_compare_function( op_restriction, "pack_size modifier" );
      require_number_argument( op_restriction, "pack_size modifier" );
   }

   // validate function
   FC_ASSERT( op_restriction.function < restriction_type::FUNCTION_TYPE_COUNT,
              "function number ${f} is too large",
              ("f", op_restriction.function) );
}

void restriction_type::validate( unsigned_int op_type )const
{
   // validate common data
   validate_restriction_commons( *this );
   // validate details by operation_type
   validate_restriction_details( *this, op_type );
}

} } // graphene::chain
