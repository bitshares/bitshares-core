/*
 * Copyright (c) 2019 Contributors.
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

#include <graphene/protocol/restriction.hpp>
#include <graphene/protocol/operations.hpp>

#include <fc/exception/exception.hpp>

namespace graphene { namespace protocol {
namespace typelist = fc::typelist;
using std::declval;
using std::size_t;
using restriction_function = restriction::function_type;
using restriction_argument = restriction::argument_type;

// Make our own std::void_t since the real one isn't available in C++14
template<typename...> using make_void = void;

// Metafunction to check if type is some instantiation of fc::safe
template<typename> constexpr static bool is_safe = false;
template<typename I> constexpr static bool is_safe<fc::safe<I>> = true;

// Metafunction to check if type is a flat_set of any element type
template<typename> struct is_flat_set_impl : std::false_type {};
template<typename T> struct is_flat_set_impl<flat_set<T>> : std::true_type {};
template<typename T> constexpr static bool is_flat_set = is_flat_set_impl<T>::value;

// We use our own is_integral which does not consider bools integral (to disallow comparison between bool and ints)
template<typename T> constexpr static bool is_integral = !std::is_same<T, bool>::value &&
                                                         !std::is_same<T, safe<bool>>::value &&
                                                         (is_safe<T> || std::is_integral<T>::value);

// Metafunction to check if two types are comparable, which means not void_t, and either the same or both integral
template<typename T, typename U>
constexpr static bool comparable_types = !std::is_same<T, void_t>::value &&
                                         (std::is_same<T, U>::value || (is_integral<T> && is_integral<U>));

// Metafunction to check if type is a container
template<typename, typename = void>
struct is_container_impl : std::false_type {};
template<typename T>
struct is_container_impl<T, make_void<typename T::value_type, decltype(declval<T>().size())>> : std::true_type {};
template<typename T> constexpr static bool is_container = is_container_impl<T>::value;

// Type alias for a predicate on a particular field type
template<typename Field>
using object_restriction_predicate = std::function<bool(const Field&)>;

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// *** Restriction Predicate Logic ***

// This file is where the magic happens for BSIP 40, aka Custom Active Authorities. This gets fairly complicated, so
// let me break it down: we're given a restriction object and an operation type. The restriction contains one or more
// assertions that will eventually be made on operation instances, and this file creates a predicate function that
// takes such an operation instance, evaluates all of the assertions, and returns whether they all pass or not.
//
// So this file works on restriction instances, but only operation *types* -- the actual operation instance won't
// show up until the predicate function this file returns is eventually called. But first, we need to dig into the
// operation/fields based on the specifications in the restrictions, make sure all the assertions are valid, then
// create a predicate that runs quickly, looking through the operation and checking all the assertions.
//
// It kicks off at the bottom of this file, in get_restriction_predicate(), which gets a vector of restrictions and
// an operation type tag. So first, we've got to enter into a template context that knows the actual operation type,
// not just the type tag. And we do a lot of entering into deeper and deeper template contexts, resolving tags to
// actual types so that we know all the type information when we create the predicate, and it can execute quickly
// on an operation instance, knowing exactly what types and fields it needs, how to cast things, how to execute the
// assertions, etc.
//
// To give an overview of the logic, the layers stack up like so, from beginning (bottom of file) to end:
//  - get_restriction_predicate() -- gets vector<restriction> and operation::tag_type, visits operation with the tag
//    type to get an actual operation type as a template parameter
//  - operation_type_resolver -- gets vector<restriction> and operation type tag, resolves tag to operation type
//    template argument. This is the last layer that can assume the type being restricted is an operation; all
//    subsequent layers work on any object or field
//  - restrictions_to_predicate<Object>() -- takes a vector<restriction> and creates a predicate for each of them,
//    but returns a single predicate that returns true only if all sub-predicates return true
//    - create_field_predicate<Object>() -- Resolves which field of Object the restriction is referencing by indexing
//      into the object's reflected fields with the predicates member_index
//    - create_logical_or_predicate<Object>() -- If the predicate is a logical OR function, instead of using
//      create_field_predicate, we recurse into restrictions_to_predicate for each branch of the OR, returning a
//      predicate which returns true if any branch of the OR passes
//  - create_predicate_function<Field>() -- switches on restriction type to determine which predicate template to use
//    going forward
//    - restriction_argument_visitor<Field> -- Determines what type the restriction argument is and creates a
//      predicate functor for that type
//    - attribute_assertion<Field> -- If the restriction is an attribute assertion, instead of using the
//      restriction_argument_visitor, we recurse into restrictions_to_predicate with the current Field as the Object
//  - predicate_xyz<Argument> -- These are functors implementing the various predicate function types
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// These typelists contain the argument types legal for various function types:

// Valid for magnitude comparisons and equality comparisons
using comparable_types_list = typelist::list<int64_t, string, time_point_sec, account_id_type, asset_id_type,
                                             force_settlement_id_type, committee_member_id_type, witness_id_type,
                                             limit_order_id_type, call_order_id_type, custom_id_type,
                                             proposal_id_type, withdraw_permission_id_type,
                                             vesting_balance_id_type, worker_id_type, balance_id_type>;
// Valid for list functions (in, not_in, has_all, has_none)
struct make_flat_set { template<typename T> struct transform { using type = flat_set<T>; }; };
using list_types_list = typelist::transform<typelist::concat<typelist::list<bool, public_key_type, fc::sha256>,
                                                             comparable_types_list>,
                                            make_flat_set>;
// Valid for equality comparisons but not necessarily magnitude comparisons
using equality_types_list = typename typelist::concat<typelist::list<void_t, bool, public_key_type, fc::sha256>,
                                                      comparable_types_list, list_types_list>;
// Valid for attritube assertions
using attr_types_list = typelist::list<vector<restriction>>;
// Valid for logical or assertions
using or_types_list = typelist::list<vector<vector<restriction>>>;

//////////////////////////////////////////////// PREDICATE FUNCTORS ////////////////////////////////////////////////
// An invalid predicate which throws upon construction. Inherited by other predicates when arg types are incompatible
template<typename A, typename B>
struct predicate_invalid {
   constexpr static bool valid = false;
   predicate_invalid() { FC_THROW_EXCEPTION(fc::assert_exception, "Invalid types for predicate"); }
   bool operator()(const A&, const B&) const { return false; }
};
// Equality comparison
template<typename A, typename B, typename = void> struct predicate_eq : predicate_invalid<A, B> {};
template<typename Field, typename Argument>
struct predicate_eq<Field, Argument, std::enable_if_t<comparable_types<Field, Argument>>> {
   // Simple comparison
   constexpr static bool valid = true;
   constexpr bool operator()(const Field& f, const Argument& a) const { return f == a; }
};
template<typename Field, typename Argument>
struct predicate_eq<Field, Argument, std::enable_if_t<is_container<Field> && is_integral<Argument>>> {
   // Compare container size against int
   constexpr static bool valid = true;
   bool operator()(const Field& f, const Argument& a) const { return f.size() == a; }
};
template<typename Field, typename Argument>
struct predicate_eq<fc::optional<Field>, Argument, std::enable_if_t<comparable_types<Field, Argument>>> {
   // Compare optional value against same type
   constexpr static bool valid = true;
   bool operator()(const fc::optional<Field>& f, const Argument& a) const { return f.valid() && *f == a; }
};
template<typename Field>
struct predicate_eq<fc::optional<Field>, void_t, void> {
   // Compare optional value against void_t (checks that optional is null)
   constexpr static bool valid = true;
   bool operator()(const fc::optional<Field>& f, const void_t&) const { return !f.valid(); }
};
// Not-equal is just an equality comparison wrapped in a negator
template<typename Field, typename Argument> struct predicate_ne : predicate_eq<Field, Argument> {
   using equal = predicate_eq<Field, Argument>;
   bool operator()(const Field& f, const Argument& a) const { return !equal::operator()(f, a); }
};

// Shared implementation for all inequality comparisons
template<typename A, typename B, typename = void> struct predicate_compare : predicate_invalid<A, B> {};
template<typename Field, typename Argument>
struct predicate_compare<Field, Argument, std::enable_if_t<comparable_types<Field, Argument>>> {
   // Simple comparison, integral types
   constexpr static bool valid = true;
   constexpr int8_t operator()(const Field& f, const Argument& a) const {
      return f<a? -1 : (f>a? 1 : 0);
   }
};
template<typename Field, typename Argument>
struct predicate_compare<fc::optional<Field>, Argument, void> : predicate_compare<Field, Argument> {
   // Compare optional value against same type
   constexpr static bool valid = true;
   constexpr int8_t operator()(const fc::optional<Field>& f, const Argument& a) const {
       FC_ASSERT(f.valid(), "Cannot compute inequality comparison against a null optional");
       return (*this)(*f, a);
   }
};
// The actual inequality predicates
template<typename Field, typename Argument> struct predicate_lt : predicate_compare<Field, Argument> {
   using base = predicate_compare<Field, Argument>;
   constexpr bool operator()(const Field& f, const Argument& a) const { return base::operator()(f, a) < 0; }
};
template<typename Field, typename Argument> struct predicate_le : predicate_compare<Field, Argument> {
   using base = predicate_compare<Field, Argument>;
   constexpr bool operator()(const Field& f, const Argument& a) const { return base::operator()(f, a) <= 0; }
};
template<typename Field, typename Argument> struct predicate_gt : predicate_compare<Field, Argument> {
   using base = predicate_compare<Field, Argument>;
   constexpr bool operator()(const Field& f, const Argument& a) const { return base::operator()(f, a) > 0; }
};
template<typename Field, typename Argument> struct predicate_ge : predicate_compare<Field, Argument> {
   using base = predicate_compare<Field, Argument>;
   constexpr bool operator()(const Field& f, const Argument& a) const { return base::operator()(f, a) >= 0; }
};

// Field-in-list predicate
template<typename F, typename C, typename = void> struct predicate_in : predicate_invalid<F, C> {};
template<typename Field, typename Element>
struct predicate_in<Field, flat_set<Element>, std::enable_if_t<comparable_types<Field, Element> && !is_safe<Field>>> {
   // Simple inclusion check
   constexpr static bool valid = true;
   bool operator()(const Field& f, const flat_set<Element>& c) const { return c.count(f) != 0; }
};
template<typename Field, typename Element>
struct predicate_in<fc::safe<Field>, flat_set<Element>, std::enable_if_t<comparable_types<Field, Element>>> {
   // Check for safe value
   constexpr static bool valid = true;
   bool operator()(const fc::safe<Field>& f, const flat_set<Element>& c) const { return c.count(f.value) != 0; }
};
template<typename Field, typename Element>
struct predicate_in<fc::optional<Field>, flat_set<Element>, std::enable_if_t<comparable_types<Field, Element>>> {
   // Check for optional value
   constexpr static bool valid = true;
   bool operator()(const fc::optional<Field>& f, const flat_set<Element>& c) const {
       FC_ASSERT(f.valid(), "Cannot compute whether null optional is in list");
       return c.count(*f) != 0;
   }
};
// Field-not-in-list is just field-in-list wrapped in a negator
template<typename Field, typename Container> struct predicate_not_in : predicate_in<Field, Container> {
   using base = predicate_in<Field, Container>;
   bool operator()(const Field& f, const Container& c) const { return !base::operator()(f, c); }
};

// List-contains-list predicate
template<typename C1, typename C2, typename = void> struct predicate_has_all : predicate_invalid<C1, C2> {};
template<typename FieldElement, typename ArgumentElement>
struct predicate_has_all<flat_set<FieldElement>, flat_set<ArgumentElement>,
                         std::enable_if_t<comparable_types<FieldElement, ArgumentElement>>> {
   // Field is already flat_set
   constexpr static bool valid = true;
   bool operator()(const flat_set<FieldElement>& f, const flat_set<ArgumentElement>& a) const {
      if (f.size() < a.size()) return false;
      return std::includes(f.begin(), f.end(), a.begin(), a.end());
   }
};
template<typename FieldContainer, typename ArgumentElement>
struct predicate_has_all<FieldContainer, flat_set<ArgumentElement>,
                         std::enable_if_t<is_container<FieldContainer> && !is_flat_set<FieldContainer> &&
                                          comparable_types<typename FieldContainer::value_type, ArgumentElement>>> {
   predicate_has_all<flat_set<typename FieldContainer::value_type>, flat_set<ArgumentElement>> inner;
   // Field is other container; convert to flat_set
   constexpr static bool valid = true;
   bool operator()(const FieldContainer& f, const flat_set<ArgumentElement>& a) const {
      if (f.size() < a.size()) return false;
      flat_set<typename FieldContainer::value_type> fs(f.begin(), f.end());
      return inner(fs, a);
   }
};
template<typename OptionalType, typename Argument>
struct predicate_has_all<fc::optional<OptionalType>, Argument, void> : predicate_has_all<OptionalType, Argument> {
   // Field is optional container
   bool operator()(const fc::optional<OptionalType>& f, const Argument& a) const {
      FC_ASSERT(f.valid(), "Cannot compute whether all elements of null optional container are in other container");
      return (*this)(*f, a);
   }
};

// List contains none of list predicate
template<typename C1, typename C2, typename = void> struct predicate_has_none : predicate_invalid<C1, C2> {};
template<typename FieldElement, typename ArgumentElement>
struct predicate_has_none<flat_set<FieldElement>, flat_set<ArgumentElement>,
                          std::enable_if_t<comparable_types<FieldElement, ArgumentElement>>> {
   // Field is already flat_set
   constexpr static bool valid = true;
   bool operator()(const flat_set<FieldElement>& f, const flat_set<ArgumentElement>& a) const {
      flat_set<FieldElement> intersection;
      std::set_intersection(f.begin(), f.end(), a.begin(), a.end(),
                            std::inserter(intersection, intersection.begin()));
      return intersection.empty();
   }
};
template<typename FieldContainer, typename ArgumentElement>
struct predicate_has_none<FieldContainer, flat_set<ArgumentElement>,
                          std::enable_if_t<is_container<FieldContainer> && !is_flat_set<FieldContainer> &&
                                           comparable_types<typename FieldContainer::value_type, ArgumentElement>>> {
   predicate_has_none<flat_set<typename FieldContainer::value_type>, flat_set<ArgumentElement>> inner;
   // Field is other container; convert to flat_set
   constexpr static bool valid = true;
   bool operator()(const FieldContainer& f, const flat_set<ArgumentElement>& a) const {
      flat_set<typename FieldContainer::value_type> fs(f.begin(), f.end());
      return inner(fs, a);
   }
};
template<typename OptionalType, typename Argument>
struct predicate_has_none<fc::optional<OptionalType>, Argument, void> : predicate_has_all<OptionalType, Argument> {
   // Field is optional container
   bool operator()(const fc::optional<OptionalType>& f, const Argument& a) const {
      FC_ASSERT(f.valid(), "Cannot evaluate list has-none-of list on null optional list");
      return (*this)(*f, a);
   }
};
////////////////////////////////////////////// END PREDICATE FUNCTORS //////////////////////////////////////////////

// Template to visit the restriction argument, resolving its type, and create the appropriate predicate functor, or
// throw if the types are not compatible for the predicate assertion
template<template<typename> class Predicate, typename Field>
struct restriction_argument_visitor {
   using result_type = object_restriction_predicate<Field>;

   template<typename Argument,
            typename = std::enable_if_t<Predicate<Argument>::template can_evaluate_helper<Field>::value>>
   result_type make_predicate(const Argument& a, short) {
       return Predicate<Argument>(a);
   }
   template<typename Argument>
   result_type make_predicate(const Argument&, long) {
      FC_THROW_EXCEPTION(fc::assert_exception, "Invalid argument types for predicate: ${Field}, ${Argument}",
                         ("Field", fc::get_typename<Field>::name())("Argument", fc::get_typename<Argument>::name()));
   }
   template<typename Argument>
   result_type operator()(const Argument& a) { return make_predicate(a, short()); }
};

// Forward declaration of restrictions_to_predicate, because attribute assertions and logical ORs recurse into it
template<typename Field> object_restriction_predicate<Field> restrictions_to_predicate(vector<restriction>, bool);

template<typename Field>
struct attribute_assertion {
   static object_restriction_predicate<Field> create(vector<restriction>&& rs) {
      return restrictions_to_predicate<Field>(std::move(rs), false);
   }
};
template<typename Field>
struct attribute_assertion<fc::optional<Field>> {
   static object_restriction_predicate<fc::optional<Field>> create(vector<restriction>&& rs) {
      return [p=restrictions_to_predicate<Field>(std::move(rs), false)](const fc::optional<Field>& f) {
         FC_ASSERT(f.valid(), "Cannot evaluate attribute assertion on null optional field");
         return p(*f);
      };
   }
};
template<typename Extension>
struct attribute_assertion<extension<Extension>> {
   static object_restriction_predicate<extension<Extension>> create(vector<restriction>&& rs) {
      return [p=restrictions_to_predicate<Extension>(std::move(rs), false)](const extension<Extension>& x) {
         return p(x.value);
      };
   }
};

template<typename F, typename P, typename A, typename = std::enable_if_t<P::valid>>
object_restriction_predicate<F> mkpred(P p, A a, short) {
   return std::bind(p, std::placeholders::_1, std::move(a));
}
template<typename F, typename P, typename A>
object_restriction_predicate<F> mkpred(P, A, long) {
   FC_THROW_EXCEPTION(fc::assert_exception, "Invalid types for predicated");
}

template<template<typename...> class Predicate, typename Field, typename ArgVariant>
object_restriction_predicate<Field> make_predicate(ArgVariant arg) {
   return typelist::runtime::dispatch(typename ArgVariant::list(), arg.which(),
                                      [&arg](auto t) mutable -> object_restriction_predicate<Field> {
      using Arg = typename decltype(t)::type;
      return mkpred<Field>(Predicate<Field, Arg>(), std::move(arg.template get<Arg>()), short());
   });
}

template<typename Field>
object_restriction_predicate<Field> create_predicate_function(restriction_function func, restriction_argument arg) {
   try {
      switch(func) {
      case restriction::func_eq:
         return make_predicate<predicate_eq, Field>(static_variant<equality_types_list>::import_from(std::move(arg)));
      case restriction::func_ne:
         return make_predicate<predicate_ne, Field>(static_variant<equality_types_list>::import_from(std::move(arg)));
      case restriction::func_lt:
         return make_predicate<predicate_lt, Field>(static_variant<comparable_types_list>
                                                    ::import_from(std::move(arg)));
      case restriction::func_le:
         return make_predicate<predicate_le, Field>(static_variant<comparable_types_list>
                                                    ::import_from(std::move(arg)));
      case restriction::func_gt:
         return make_predicate<predicate_gt, Field>(static_variant<comparable_types_list>
                                                    ::import_from(std::move(arg)));
      case restriction::func_ge:
         return make_predicate<predicate_ge, Field>(static_variant<comparable_types_list>
                                                    ::import_from(std::move(arg)));
      case restriction::func_in:
         return make_predicate<predicate_in, Field>(static_variant<list_types_list>::import_from(std::move(arg)));
      case restriction::func_not_in:
         return make_predicate<predicate_not_in, Field>(static_variant<list_types_list>
                                                        ::import_from(std::move(arg)));
      case restriction::func_has_all:
         return make_predicate<predicate_has_all, Field>(static_variant<list_types_list>
                                                         ::import_from(std::move(arg)));
      case restriction::func_has_none:
         return make_predicate<predicate_has_none, Field>(static_variant<list_types_list>
                                                          ::import_from(std::move(arg)));
      case restriction::func_attr:
         FC_ASSERT(arg.which() == restriction_argument::tag<vector<restriction>>::value,
                   "Argument type for attribute assertion must be restriction list");
         return attribute_assertion<Field>::create(std::move(arg.get<vector<restriction>>()));
      default:
          FC_THROW_EXCEPTION(fc::assert_exception, "Invalid function type on restriction");
      }
   } FC_CAPTURE_AND_RETHROW( (fc::get_typename<Field>::name())(func)(arg) )
}

/**
 * @brief Create a predicate asserting on the field of the object a restriction is referencing
 *
 * @tparam Object The type the restriction restricts
 *
 * A restriction specifies requirements about a field of an object. This struct shifts the focus from the object type
 * the restriction references to the particular field type, creates a predicate on that field, and wraps that
 * predicate to accept the object type and invoke the inner predicate on the specified field.
 */
template<typename Object,
         typename = std::enable_if_t<typelist::length<typename fc::reflector<Object>::native_members>() != 0>>
object_restriction_predicate<Object> create_field_predicate(restriction&& r, short) {
   using member_list = typename fc::reflector<Object>::native_members;
   FC_ASSERT(r.member_index < typelist::length<member_list>(), "Invalid member index ${I} for object ${O}",
             ("I", r.member_index)("O", fc::get_typename<Object>::name()));
   auto predicator = [f=r.restriction_type, a=std::move(r.argument)](auto t) -> object_restriction_predicate<Object> {
      using FieldReflection = typename decltype(t)::type;
      using Field = typename FieldReflection::type;
      auto p = create_predicate_function<Field>(static_cast<restriction_function>(f), std::move(a));
      return [p=std::move(p)](const Object& o) { return p(FieldReflection::get(o)); };
   };
   return typelist::runtime::dispatch(member_list(), r.member_index, predicator);
}
template<typename Object>
object_restriction_predicate<Object> create_field_predicate(restriction&&, long) {
   FC_THROW_EXCEPTION(fc::assert_exception, "Invalid restriction references member of non-object type: ${O}",
                      ("O", fc::get_typename<Object>::name()));
}

template<typename Object>
object_restriction_predicate<Object> create_logical_or_predicate(vector<vector<restriction>> rs) {
   FC_ASSERT(rs.size() > 1, "Logical OR must have at least two branches");
   auto to_predicate = std::bind(restrictions_to_predicate<Object>, std::placeholders::_1, false);

   vector<object_restriction_predicate<Object>> predicates;
   std::transform(std::make_move_iterator(rs.begin()), std::make_move_iterator(rs.end()),
                  std::back_inserter(predicates), to_predicate);

   return [predicates=std::move(predicates)](const Object& obj) {
      return std::any_of(predicates.begin(), predicates.end(), [o=std::cref(obj)](const auto& p) { return p(o); });
   };
}

template<typename Object>
object_restriction_predicate<Object> restrictions_to_predicate(vector<restriction> rs, bool allow_empty) {
   if (!allow_empty)
      FC_ASSERT(!rs.empty(), "Empty attribute assertions and logical OR branches are not permitted");

   vector<object_restriction_predicate<Object>> predicates;
   std::transform(std::make_move_iterator(rs.begin()), std::make_move_iterator(rs.end()),
                  std::back_inserter(predicates), [](restriction&& r) {
      if (r.restriction_type.value == restriction::func_logical_or) {
          FC_ASSERT(r.argument.which() == restriction_argument::tag<vector<vector<restriction>>>::value,
                    "Restriction argument for logical OR function type must be list of restriction lists.");
          return create_logical_or_predicate<Object>(std::move(r.argument.get<vector<vector<restriction>>>()));
      }
      return create_field_predicate<Object>(std::move(r), short());
   });

   return [predicates=std::move(predicates)](const Object& obj) {
      return std::all_of(predicates.begin(), predicates.end(), [o=std::cref(obj)](const auto& p) { return p(o); });
   };
}

// To make the build gentler on RAM, break the operation list into several pieces to build over several files
using operation_list_1 = static_variant<typelist::slice<operation::list, 0, 5>>;
using operation_list_2 = static_variant<typelist::slice<operation::list, 5, 10>>;
using operation_list_3 = static_variant<typelist::slice<operation::list, 10, 20>>;
using operation_list_4 = static_variant<typelist::slice<operation::list, 20, 30>>;
using operation_list_5 = static_variant<typelist::slice<operation::list, 30, 40>>;
using operation_list_6 = static_variant<typelist::slice<operation::list, 40, 50>>;
using operation_list_7 = static_variant<typelist::slice<operation::list, 50>>;

object_restriction_predicate<operation> get_restriction_predicate_list_1(size_t idx, vector<restriction> rs);
object_restriction_predicate<operation> get_restriction_predicate_list_2(size_t idx, vector<restriction> rs);
object_restriction_predicate<operation> get_restriction_predicate_list_3(size_t idx, vector<restriction> rs);
object_restriction_predicate<operation> get_restriction_predicate_list_4(size_t idx, vector<restriction> rs);
object_restriction_predicate<operation> get_restriction_predicate_list_5(size_t idx, vector<restriction> rs);
object_restriction_predicate<operation> get_restriction_predicate_list_6(size_t idx, vector<restriction> rs);
object_restriction_predicate<operation> get_restriction_predicate_list_7(size_t idx, vector<restriction> rs);

} } // namespace graphene::protocol
