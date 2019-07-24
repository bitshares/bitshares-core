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

#include <graphene/protocol/restriction_predicate.hpp>

#include <fc/exception/exception.hpp>

namespace graphene { namespace protocol {
namespace typelist = fc::typelist;
using std::declval;
using std::size_t;
using restriction_function = restriction::function_type;
using restriction_argument = restriction::argument_type;

// Make our own std::void_t since the real one isn't available in C++14
template<typename...> using make_void = void;

// We use our own is_integral which does not consider bools integral (to disallow comparison between bool and ints)
template<typename T> constexpr static bool is_integral =
        std::conditional_t<std::is_same<T, bool>::value, std::false_type, std::is_integral<T>>::value;

// Metafunction to check if type is some instantiation of fc::safe
template<typename> constexpr static bool is_safe = false;
template<typename I> constexpr static bool is_safe<fc::safe<I>> = true;

// Metafunction to check if two types are comparable, which means not void_t, and either the same or both integral
template<typename T, typename U>
constexpr static bool comparable_types = !std::is_same<T, void_t>::value &&
                                         (std::is_same<T, U>::value || ((is_integral<T> || is_safe<T>) &&
                                                                        (is_integral<U> || is_safe<U>)));

// Metafunction to check if type is a container
template<typename, typename = size_t>
struct is_container_impl { constexpr static bool value = false; };
template<typename T>
struct is_container_impl<T, decltype(declval<T>().size())> { constexpr static bool value = true; };
template<typename T> constexpr static bool is_container = is_container_impl<T>::value;

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

// Final implementations of predicate functors
template<typename Argument> struct predicate_eq {
   Argument a;
   constexpr predicate_eq(const Argument& a) : a(a) {}
   template<typename, typename = void>
   struct can_evaluate_helper : std::false_type {}; template<typename Field>
   struct can_evaluate_helper<Field, make_void<decltype(declval<predicate_eq>()(declval<Field>()))>> {
      static constexpr bool value = typelist::contains<equality_types_list, Argument>();
   };
   template<typename Field> static constexpr bool can_evaluate = can_evaluate_helper<Field>::value;

   // Simple comparison
   template<typename Field, typename = std::enable_if_t<comparable_types<Field, Argument>>>
   constexpr bool operator()(const Field& f) const { return f == a; }
   // Compare container size against int
   template<typename Field, typename Arg = Argument,
            typename = std::enable_if_t<is_container<Field> && is_integral<Arg>>>
   bool operator()(const Field& f) const { return f.size() == a; }
   // Compare optional value against same type
   template<typename Field, typename = std::enable_if_t<comparable_types<Field, Argument>>>
   bool operator()(const fc::optional<Field>& f) const { return f.valid() && *f == a; }
   // Compare optional value against void_t (checks that optional is null)
   template<typename Field, typename Arg = Argument, typename = std::enable_if_t<std::is_same<Arg, void_t>::value>>
   bool operator()(const fc::optional<Field>& f) const { return !f.valid(); }
   // Compare containers of different types
// TODO: Figure out how to actually make this compile... x(
//   template<typename Field, typename Arg = Argument, typename =
//            std::enable_if_t<is_container<Field> && is_container<Arg> && !std::is_same<Field, Arg>::value
//                             && comparable_types<typename Arg::value_type, typename Field::value_type>>>
//   bool operator()(const Field& f) const {
//       flat_set<typename Field::value_type> fs(f.begin(), f.end());
//       return (*this)(fs);
//   }
};
template<typename Argument> struct predicate_ne : predicate_eq<Argument> {
   using base = predicate_eq<Argument>;
   predicate_ne(const Argument& a) : base(a) {}
   template<typename Field> auto operator()(const Field& f) const { return !base::operator()(f); }
};
template<typename Argument> struct predicate_compare {
   Argument a;
   constexpr predicate_compare(const Argument& a) : a(a) {}
   template<typename, typename = void> struct can_evaluate_helper : std::false_type {};
   template<typename Field>
   struct can_evaluate_helper<Field, make_void<decltype(declval<predicate_compare>()(declval<Field>()))>> {
      static constexpr bool value = typelist::contains<comparable_types_list, Argument>();
   };
   template<typename Field> static constexpr bool can_evaluate = can_evaluate_helper<Field>::value;

   // Simple comparison
   template<typename Field, typename = std::enable_if_t<comparable_types<Field, Argument>>>
   constexpr int8_t operator()(const Field& f) const { return f<a? -1 : (f>a? 1 : 0); }
   // Compare optional value against same type
   template<typename Field, typename = std::enable_if_t<comparable_types<Field, Argument>>>
   constexpr int8_t operator()(const fc::optional<Field>& f) const {
       FC_ASSERT(f.valid(), "Cannot compute inequality comparison against a null optional");
       return (*this)(*f);
   }
};
template<typename Argument> struct predicate_lt : predicate_compare<Argument> {
   using base = predicate_compare<Argument>;
   constexpr predicate_lt(const Argument& a) : base(a) {}
   template<typename Field> constexpr bool operator()(const Field& f) const { return base::operator()(f) < 0; }
};
template<typename Argument> struct predicate_le : predicate_compare<Argument> {
   using base = predicate_compare<Argument>;
   constexpr predicate_le(const Argument& a) : base(a) {}
   template<typename Field> constexpr bool operator()(const Field& f) const { return base::operator()(f) <= 0; }
};
template<typename Argument> struct predicate_gt : predicate_compare<Argument> {
   using base = predicate_compare<Argument>;
   constexpr predicate_gt(const Argument& a) : base(a) {}
   template<typename Field> constexpr bool operator()(const Field& f) const { return base::operator()(f) > 0; }
};
template<typename Argument> struct predicate_ge : predicate_compare<Argument> {
   using base = predicate_compare<Argument>;
   constexpr predicate_ge(const Argument& a) : base(a) {}
   template<typename Field> constexpr bool operator()(const Field& f) const { return base::operator()(f) >= 0; }
};
template<typename> struct predicate_in { template<typename> static constexpr bool can_evaluate = false; };
template<typename Element> struct predicate_in<flat_set<Element>> {
   flat_set<Element> a;
   predicate_in(const flat_set<Element>& a) : a(a) {}
   template<typename, typename = void> struct can_evaluate_helper : std::false_type {};
   template<typename Field>
   struct can_evaluate_helper<Field, make_void<decltype(declval<predicate_in>()(declval<Field>()))>> {
      static constexpr bool value = typelist::contains<equality_types_list, Element>();
   };
   template<typename Field> static constexpr bool can_evaluate = can_evaluate_helper<Field>::value;

   // Simple inclusion check
   template<typename Field, typename = std::enable_if_t<comparable_types<Field, Element>>>
   bool operator()(const Field& f) const { return a.count(f) != 0; }
   // Check for safe value
   template<typename Field, typename = std::enable_if_t<comparable_types<Field, Element>>>
   bool operator()(const fc::safe<Field>& f) const { return a.count(f.value) != 0; }
   // Check for optional value
   template<typename Field, typename = std::enable_if_t<comparable_types<Field, Element>>>
   bool operator()(const fc::optional<Field>& f) const {
       FC_ASSERT(f.valid(), "Cannot compute whether null optional is in list");
       return (*this)(*f);
   }
};
template<typename Argument> struct predicate_not_in : predicate_in<Argument>{
   using base = predicate_in<Argument>;
   constexpr predicate_not_in(const Argument& a) : base(a) {}
   template<typename Field> constexpr bool operator()(const Field& f) const { return !base::operator()(f); }
};
template<typename> struct predicate_has_all { template<typename> static constexpr bool can_evaluate = false; };
template<typename Element> struct predicate_has_all<flat_set<Element>> {
   flat_set<Element> a;
   predicate_has_all(const flat_set<Element>& a) : a(a) {}
   template<typename, typename = void> struct can_evaluate_helper : std::false_type {};
   template<typename Field>
   struct can_evaluate_helper<Field, make_void<decltype(declval<predicate_has_all>()(declval<Field>()))>> {
      static constexpr bool value = typelist::contains<equality_types_list, Element>();
   };
   template<typename Field> static constexpr bool can_evaluate = can_evaluate_helper<Field>::value;

   // Field is already flat_set
   template<typename Field, typename = std::enable_if_t<comparable_types<Field, Element>>>
   bool operator()(const flat_set<Field>& f) const {
      if (f.size() < a.size()) return false;
      return std::includes(f.begin(), f.end(), a.begin(), a.end());
   }
   // Field is other container; convert to flat_set
   template<typename Field, typename = std::enable_if_t<is_container<Field> &&
                                                        comparable_types<typename Field::value_type, Element>>>
   bool operator()(const Field& f) const {
      if (f.size() < a.size()) return false;
      flat_set<typename Field::value_type> fs(f.begin(), f.end());
      return (*this)(fs);
   }
   // Field is optional container
   template<typename Field, typename = std::enable_if_t<is_container<Field> &&
                                                        comparable_types<typename Field::value_type, Element>>>
   bool operator()(const fc::optional<Field>& f) const {
      FC_ASSERT(f.valid(), "Cannot compute whether all elements of null optional container are in other container");
      return (*this)(*f);
   }
};
template<typename> struct predicate_has_none { template<typename> static constexpr bool can_evaluate = false; };
template<typename Element> struct predicate_has_none<flat_set<Element>> {
   flat_set<Element> a;
   predicate_has_none(const flat_set<Element>& a) : a(a) {}
   template<typename, typename = void> struct can_evaluate_helper : std::false_type {};
   template<typename Field>
   struct can_evaluate_helper<Field, make_void<decltype(declval<predicate_has_none>()(declval<Field>()))>> {
      static constexpr bool value = typelist::contains<equality_types_list, Element>();
   };
   template<typename Field> static constexpr bool can_evaluate = can_evaluate_helper<Field>::value;

   // Field is already flat_set
   template<typename Field, typename = std::enable_if_t<comparable_types<Field, Element>>>
   bool operator()(const flat_set<Field>& f) const {
      flat_set<Field> intersection;
      std::set_intersection(f.begin(), f.end(), a.begin(), a.end(),
                            std::inserter(intersection, intersection.begin()));
      return intersection.empty();
   }
   // Field is other container; convert to flat_set
   template<typename Field, typename = std::enable_if_t<is_container<Field> &&
                                                        comparable_types<typename Field::value_type, Element>>>
   bool operator()(const Field& f) const {
      flat_set<typename Field::value_type> fs(f.begin(), f.end());
      return (*this)(fs);
   }
   // Field is optional container
   template<typename Field, typename = std::enable_if_t<is_container<Field> &&
                                                        comparable_types<typename Field::value_type, Element>>>
   bool operator()(const fc::optional<Field>& f) const {
      FC_ASSERT(f.valid(), "Cannot compute whether no elements of null optional container are in other container");
      return (*this)(*f);
   }
};

// Type alias for a predicate on a particular field type
template<typename Field>
using object_restriction_predicate = std::function<bool(const Field&)>;

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

// slimmer accelerates build times by reducing the number of elements in the argument static variant to only those
// supported for a given function type. This reduces build time because it eliminates many options the compiler has
// to explore when visiting the argument variant to create a predicate
template<typename List>
struct slimmer {
   restriction_argument arg;
   slimmer(restriction_argument&& arg) : arg(std::move(arg)) {}
   using result_type = typelist::apply<List, static_variant>;

   template<typename T, typename = std::enable_if_t<typelist::contains<List, T>()>>
   result_type do_cast(T&& content, short) { return result_type(content); }
   template<typename T>
   result_type do_cast(T&&, long) {
      FC_THROW_EXCEPTION(fc::assert_exception, "Invalid argument type for restriction function type");
   }

   template<typename T> result_type operator()(T) {
      return do_cast(std::move(arg.get<typename T::type>()), short());
   }
};

template<typename Field>
object_restriction_predicate<Field> create_predicate_function(restriction_function func, restriction_argument arg) {
   try {
      using typelist::runtime::dispatch;
      using std::move;
      using Arg = decltype(arg);
      switch(func) {
      case restriction::func_eq: {
         restriction_argument_visitor<predicate_eq, Field> visitor;
         return dispatch(Arg::list(), arg.which(), slimmer<equality_types_list>(move(arg))).visit(visitor);
      }
      case restriction::func_ne: {
         restriction_argument_visitor<predicate_ne, Field> visitor;
         return dispatch(Arg::list(), arg.which(), slimmer<equality_types_list>(move(arg))).visit(visitor);
      }
      case restriction::func_lt: {
         restriction_argument_visitor<predicate_lt, Field> visitor;
         return dispatch(Arg::list(), arg.which(), slimmer<comparable_types_list>(move(arg))).visit(visitor);
      }
      case restriction::func_le: {
         restriction_argument_visitor<predicate_le, Field> visitor;
         return dispatch(Arg::list(), arg.which(), slimmer<comparable_types_list>(move(arg))).visit(visitor);
      }
      case restriction::func_gt: {
         restriction_argument_visitor<predicate_gt, Field> visitor;
         return dispatch(Arg::list(), arg.which(), slimmer<comparable_types_list>(move(arg))).visit(visitor);
      }
      case restriction::func_ge: {
         restriction_argument_visitor<predicate_ge, Field> visitor;
         return dispatch(Arg::list(), arg.which(), slimmer<comparable_types_list>(move(arg))).visit(visitor);
      }
      case restriction::func_in: {
         restriction_argument_visitor<predicate_in, Field> visitor;
         return dispatch(Arg::list(), arg.which(), slimmer<list_types_list>(move(arg))).visit(visitor);
      }
      case restriction::func_not_in: {
         restriction_argument_visitor<predicate_not_in, Field> visitor;
         return dispatch(Arg::list(), arg.which(), slimmer<list_types_list>(move(arg))).visit(visitor);
      }
      case restriction::func_has_all: {
         restriction_argument_visitor<predicate_has_all, Field> visitor;
         return dispatch(Arg::list(), arg.which(), slimmer<list_types_list>(move(arg))).visit(visitor);
      }
      case restriction::func_has_none: {
         restriction_argument_visitor<predicate_has_none, Field> visitor;
         return dispatch(Arg::list(), arg.which(), slimmer<list_types_list>(move(arg))).visit(visitor);
      }
      case restriction::func_attr:
         FC_ASSERT(arg.which() == restriction_argument::tag<vector<restriction>>::value,
                   "Argument type for attribute assertion must be restriction list");
         return attribute_assertion<Field>::create(std::move(arg.get<vector<restriction>>()));
      default:
          FC_THROW_EXCEPTION(fc::assert_exception, "Invalid function type on restriction");
      }
   } FC_CAPTURE_AND_RETHROW( (func) )
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

/**
 * @brief Visitor on the generic operation to determine the actual operation type and make a predicate for it
 *
 * This struct is used as a visitor to the operation static_variant. It has a visit method which is called with the
 * particular operation type as a template argument, and this method constructs a restriction predicate which accepts
 * an operation wrapping the visited type and returns whether the operation complies with all restrictions or not.
 */
struct operation_type_resolver {
   using result_type = restriction_predicate_function;

   const vector<restriction>& restrictions;

   operation_type_resolver(const vector<restriction>& restrictions) : restrictions(restrictions) {}

   template<typename Op>
   result_type operator()(const Op&) {
      auto predicate = restrictions_to_predicate<Op>(restrictions, true);
      return [predicate=std::move(predicate)](const operation& op) {
         FC_ASSERT(op.which() == operation::tag<Op>::value,
                   "Supplied operation is incorrect type for restriction predicate");
         return predicate(op.get<Op>());
      };
   }
};

restriction_predicate_function get_restriction_predicate(const vector<restriction> &r, operation::tag_type op_type) {
   operation_type_resolver visitor(r);
   return operation::visit(op_type, visitor, static_cast<void*>(nullptr));
}

// Now for some compile-time tests of the metafunctions we use in here...
#ifndef NDEBUG
static_assert(!is_container<int>, "");
static_assert(is_container<vector<int>>, "");

static_assert(predicate_eq<int64_t>(10)(20) == false, "");
static_assert(predicate_eq<int64_t>(10)(5) == false, "");
static_assert(predicate_eq<int64_t>(10)(10) == true, "");

static_assert(predicate_eq<void_t>::can_evaluate<void_t> == false, "");
static_assert(predicate_eq<void_t>::can_evaluate<int> == false, "");
static_assert(predicate_eq<int64_t>::can_evaluate<void_t> == false, "");
static_assert(predicate_eq<int64_t>::can_evaluate<int> == true, "");
static_assert(predicate_eq<int64_t>::can_evaluate<long> == true, "");
static_assert(predicate_eq<int64_t>::can_evaluate<vector<bool>> == true, "");
static_assert(predicate_eq<int64_t>::can_evaluate<flat_set<char>> == true, "");
static_assert(predicate_eq<int64_t>::can_evaluate<short> == true, "");
static_assert(predicate_eq<int64_t>::can_evaluate<bool> == false, "");
static_assert(predicate_eq<bool>::can_evaluate<int> == false, "");
static_assert(predicate_eq<int64_t>::can_evaluate<fc::optional<int>> == true, "");
static_assert(predicate_eq<int64_t>::can_evaluate<fc::optional<long>> == true, "");
static_assert(predicate_eq<void_t>::can_evaluate<fc::optional<long>> == true, "");
static_assert(predicate_eq<flat_set<bool>>::can_evaluate<flat_set<bool>> == true, "");
//static_assert(predicate_eq<flat_set<bool>>::can_evaluate<vector<bool>> == true, "");
static_assert(predicate_eq<string>::can_evaluate<flat_set<bool>> == false, "");
static_assert(predicate_eq<string>::can_evaluate<string> == true, "");
static_assert(predicate_ne<void_t>::can_evaluate<int> == false, "");
static_assert(predicate_ne<int64_t>::can_evaluate<void_t> == false, "");
static_assert(predicate_ne<int64_t>::can_evaluate<int> == true, "");
static_assert(predicate_ne<int64_t>::can_evaluate<long> == true, "");
static_assert(predicate_ne<int64_t>::can_evaluate<vector<bool>> == true, "");
static_assert(predicate_ne<int64_t>::can_evaluate<flat_set<char>> == true, "");
static_assert(predicate_ne<int64_t>::can_evaluate<short> == true, "");
static_assert(predicate_ne<int64_t>::can_evaluate<bool> == false, "");
static_assert(predicate_ne<bool>::can_evaluate<int> == false, "");
static_assert(predicate_ne<int64_t>::can_evaluate<fc::optional<int>> == true, "");
static_assert(predicate_ne<int64_t>::can_evaluate<fc::optional<long>> == true, "");
static_assert(predicate_ne<void_t>::can_evaluate<fc::optional<long>> == true, "");
static_assert(predicate_ne<string>::can_evaluate<string> == true, "");

static_assert(predicate_compare<int64_t>(10)(20) == 1, "");
static_assert(predicate_compare<int64_t>(10)(5) == -1, "");
static_assert(predicate_compare<int64_t>(10)(10) == 0, "");
static_assert(predicate_lt<int64_t>(10)(20) == false, "");
static_assert(predicate_lt<int64_t>(10)(5) == true, "");
static_assert(predicate_lt<int64_t>(10)(10) == false, "");
static_assert(predicate_le<int64_t>(10)(20) == false, "");
static_assert(predicate_le<int64_t>(10)(5) == true, "");
static_assert(predicate_le<int64_t>(10)(10) == true, "");
static_assert(predicate_gt<int64_t>(10)(20) == true, "");
static_assert(predicate_gt<int64_t>(10)(5) == false, "");
static_assert(predicate_gt<int64_t>(10)(10) == false, "");
static_assert(predicate_ge<int64_t>(10)(20) == true, "");
static_assert(predicate_ge<int64_t>(10)(5) == false, "");
static_assert(predicate_ge<int64_t>(10)(10) == true, "");

static_assert(predicate_compare<int64_t>::can_evaluate<int> == true, "");
static_assert(predicate_compare<int64_t>::can_evaluate<short> == true, "");
static_assert(predicate_compare<string>::can_evaluate<string> == true, "");
static_assert(predicate_compare<int64_t>::can_evaluate<vector<int>> == false, "");
static_assert(predicate_compare<int64_t>::can_evaluate<fc::optional<int>> == true, "");
static_assert(predicate_compare<int64_t>::can_evaluate<fc::optional<short>> == true, "");
static_assert(predicate_compare<string>::can_evaluate<fc::optional<string>> == true, "");
static_assert(predicate_lt<int64_t>::can_evaluate<int> == true, "");
static_assert(predicate_lt<int64_t>::can_evaluate<short> == true, "");
static_assert(predicate_lt<string>::can_evaluate<string> == true, "");
static_assert(predicate_lt<int64_t>::can_evaluate<vector<int>> == false, "");
static_assert(predicate_lt<int64_t>::can_evaluate<fc::optional<int>> == true, "");
static_assert(predicate_lt<int64_t>::can_evaluate<fc::optional<short>> == true, "");
static_assert(predicate_lt<string>::can_evaluate<fc::optional<string>> == true, "");

static_assert(predicate_in<string>::can_evaluate<string> == false, "");
static_assert(predicate_in<flat_set<string>>::can_evaluate<int> == false, "");
static_assert(predicate_in<flat_set<string>>::can_evaluate<string> == true, "");
static_assert(predicate_in<flat_set<string>>::can_evaluate<flat_set<string>> == false, "");
static_assert(predicate_in<flat_set<string>>::can_evaluate<fc::optional<string>> == true, "");
static_assert(predicate_not_in<string>::can_evaluate<string> == false, "");
static_assert(predicate_not_in<flat_set<string>>::can_evaluate<int> == false, "");
static_assert(predicate_not_in<flat_set<string>>::can_evaluate<string> == true, "");
static_assert(predicate_not_in<flat_set<string>>::can_evaluate<flat_set<string>> == false, "");
static_assert(predicate_not_in<flat_set<string>>::can_evaluate<fc::optional<string>> == true, "");

static_assert(predicate_has_all<string>::can_evaluate<string> == false, "");
static_assert(predicate_has_all<flat_set<string>>::can_evaluate<int> == false, "");
static_assert(predicate_has_all<flat_set<string>>::can_evaluate<string> == false, "");
static_assert(predicate_has_all<flat_set<string>>::can_evaluate<flat_set<string>> == true, "");
static_assert(predicate_has_all<flat_set<string>>::can_evaluate<fc::optional<string>> == false, "");
static_assert(predicate_has_all<flat_set<string>>::can_evaluate<fc::optional<flat_set<string>>> == true, "");
static_assert(predicate_has_none<string>::can_evaluate<string> == false, "");
static_assert(predicate_has_none<flat_set<string>>::can_evaluate<int> == false, "");
static_assert(predicate_has_none<flat_set<string>>::can_evaluate<string> == false, "");
static_assert(predicate_has_none<flat_set<string>>::can_evaluate<flat_set<string>> == true, "");
static_assert(predicate_has_none<flat_set<string>>::can_evaluate<fc::optional<string>> == false, "");
static_assert(predicate_has_none<flat_set<string>>::can_evaluate<fc::optional<flat_set<string>>> == true, "");

#endif

} } // namespace graphene::protocol
