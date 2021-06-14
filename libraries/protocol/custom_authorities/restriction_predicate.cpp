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

#include "restriction_predicate.hxx"
#include "sliced_lists.hxx"

namespace graphene { namespace protocol {

restriction_predicate_function get_restriction_predicate(vector<restriction> rs, operation::tag_type op_type) {
   auto f = typelist::runtime::dispatch(operation::list(), op_type, [&rs](auto t) -> restriction_predicate_function {
      using Op = typename decltype(t)::type;
      if (typelist::contains<operation_list_1::list, Op>())
         return get_restriction_pred_list_1(typelist::index_of<operation_list_1::list, Op>(), std::move(rs));
      if (typelist::contains<operation_list_2::list, Op>())
         return get_restriction_pred_list_2(typelist::index_of<operation_list_2::list, Op>(), std::move(rs));
      if (typelist::contains<operation_list_3::list, Op>())
         return get_restriction_pred_list_3(typelist::index_of<operation_list_3::list, Op>(), std::move(rs));
      if (typelist::contains<operation_list_4::list, Op>())
         return get_restriction_pred_list_4(typelist::index_of<operation_list_4::list, Op>(), std::move(rs));
      if (typelist::contains<operation_list_5::list, Op>())
         return get_restriction_pred_list_5(typelist::index_of<operation_list_5::list, Op>(), std::move(rs));
      if (typelist::contains<operation_list_6::list, Op>())
         return get_restriction_pred_list_6(typelist::index_of<operation_list_6::list, Op>(), std::move(rs));
      if (typelist::contains<operation_list_7::list, Op>())
         return get_restriction_pred_list_7(typelist::index_of<operation_list_7::list, Op>(), std::move(rs));
      if (typelist::contains<operation_list_8::list, Op>())
         return get_restriction_pred_list_8(typelist::index_of<operation_list_8::list, Op>(), std::move(rs));
      if (typelist::contains<operation_list_9::list, Op>())
         return get_restriction_pred_list_9(typelist::index_of<operation_list_9::list, Op>(), std::move(rs));
      if (typelist::contains<operation_list_10::list, Op>())
         return get_restriction_pred_list_10(typelist::index_of<operation_list_10::list, Op>(), std::move(rs));
      if (typelist::contains<operation_list_11::list, Op>())
         return get_restriction_pred_list_11(typelist::index_of<operation_list_11::list, Op>(), std::move(rs));
      if (typelist::contains<operation_list_12::list, Op>())
         return get_restriction_pred_list_12(typelist::index_of<operation_list_12::list, Op>(), std::move(rs));
      if (typelist::contains<operation_list_13::list, Op>())
         return get_restriction_pred_list_13(typelist::index_of<operation_list_13::list, Op>(), std::move(rs));
      if (typelist::contains<operation_list_14::list, Op>())
         return get_restriction_pred_list_14(typelist::index_of<operation_list_14::list, Op>(), std::move(rs));
      if (typelist::contains<virtual_operations_list::list, Op>())
         FC_THROW_EXCEPTION( fc::assert_exception, "Virtual operations not allowed!" );

      // Compile time check that we'll never get to the exception below
      static_assert(typelist::contains<typelist::concat<operation_list_1::list, operation_list_2::list,
                                                        operation_list_3::list, operation_list_4::list,
                                                        operation_list_5::list, operation_list_6::list,
                                                        operation_list_7::list, operation_list_8::list,
                                                        operation_list_9::list, operation_list_10::list,
                                                        operation_list_11::list, operation_list_12::list,
                                                        operation_list_13::list, operation_list_14::list,
                                                        virtual_operations_list::list>,
                                       Op>(), "");
      FC_THROW_EXCEPTION(fc::assert_exception,
                         "LOGIC ERROR: Operation type not handled by custom authorities implementation. "
                         "Please report this error.");
   });

   // Wrap function in a layer that, if the function returns an error, reverses the order of the rejection path. This
   // is because the order the path is created in, from the top of the call stack to the bottom, is counterintuitive.
   return [f=std::move(f)](const operation& op) { return f(op).reverse_path(); };
}

predicate_result& predicate_result::reverse_path() {
   if (success == true)
      return *this;
   auto reverse_subpaths = [](rejection_indicator& indicator) {
      if (indicator.is_type<vector<predicate_result>>()) {
         auto& results = indicator.get<vector<predicate_result>>();
         for (predicate_result& result : results) result.reverse_path();
      }
   };
   std::reverse(rejection_path.begin(), rejection_path.end());
   std::for_each(rejection_path.begin(), rejection_path.end(), reverse_subpaths);
   return *this;
}

// These are some compile-time tests of the metafunctions and predicate type analysis. They are turned off to make
// building faster; they only need to be enabled when making changes in restriction_predicate.hxx
#if false
static_assert(!is_container<int>, "");
static_assert(is_container<vector<int>>, "");
static_assert(is_container<flat_set<int>>, "");
static_assert(is_container<string>, "");
static_assert(is_flat_set<flat_set<int>>, "");
static_assert(!is_flat_set<vector<int>>, "");

static_assert(predicate_eq<int, int64_t>()(10, 20) == false, "");
static_assert(predicate_eq<int, int64_t>()(10, 5) == false, "");
static_assert(predicate_eq<int, int64_t>()(10, 10) == true, "");

static_assert(predicate_eq<void_t, void_t>::valid == false, "");
static_assert(predicate_eq<int, void_t>::valid == false, "");
static_assert(predicate_eq<void_t, int64_t>::valid == false, "");
static_assert(predicate_eq<int, int64_t>::valid == true, "");
static_assert(predicate_eq<long, int64_t>::valid == true, "");
static_assert(predicate_eq<vector<bool>, int64_t>::valid == true, "");
static_assert(predicate_eq<flat_set<char>, int64_t>::valid == true, "");
static_assert(predicate_eq<short, int64_t>::valid == true, "");
static_assert(predicate_eq<bool, int64_t>::valid == false, "");
static_assert(predicate_eq<int, bool>::valid == false, "");
static_assert(predicate_eq<fc::optional<int>, int64_t>::valid == true, "");
static_assert(predicate_eq<fc::optional<long>, int64_t>::valid == true, "");
static_assert(predicate_eq<fc::optional<long>, void_t>::valid == true, "");
static_assert(predicate_eq<flat_set<bool>, flat_set<bool>>::valid == true, "");
static_assert(predicate_eq<flat_set<bool>, string>::valid == false, "");
static_assert(predicate_eq<string, string>::valid == true, "");
static_assert(predicate_ne<int, void_t>::valid == false, "");
static_assert(predicate_ne<void_t, int64_t>::valid == false, "");
static_assert(predicate_ne<int, int64_t>::valid == true, "");
static_assert(predicate_ne<long, int64_t>::valid == true, "");
static_assert(predicate_ne<vector<bool>, int64_t>::valid == true, "");
static_assert(predicate_ne<flat_set<char>, int64_t>::valid == true, "");
static_assert(predicate_ne<short, int64_t>::valid == true, "");
static_assert(predicate_ne<bool, int64_t>::valid == false, "");
static_assert(predicate_ne<int, bool>::valid == false, "");
static_assert(predicate_ne<fc::optional<int>, int64_t>::valid == true, "");
static_assert(predicate_ne<fc::optional<long>, int64_t>::valid == true, "");
static_assert(predicate_ne<fc::optional<long>, void_t>::valid == true, "");
static_assert(predicate_ne<string, string>::valid == true, "");

static_assert(predicate_compare<int, int64_t>()(20, 10) == 1, "");
static_assert(predicate_compare<int, int64_t>()(5, 10) == -1, "");
static_assert(predicate_compare<int, int64_t>()(10, 10) == 0, "");
static_assert(predicate_lt<int, int64_t>()(20, 10) == false, "");
static_assert(predicate_lt<int, int64_t>()(5, 10) == true, "");
static_assert(predicate_lt<int, int64_t>()(10, 10) == false, "");
static_assert(predicate_le<int, int64_t>()(20, 10) == false, "");
static_assert(predicate_le<int, int64_t>()(5, 10) == true, "");
static_assert(predicate_le<int, int64_t>()(10, 10) == true, "");
static_assert(predicate_gt<int, int64_t>()(20, 10) == true, "");
static_assert(predicate_gt<int, int64_t>()(5, 10) == false, "");
static_assert(predicate_gt<int, int64_t>()(10, 10) == false, "");
static_assert(predicate_ge<int, int64_t>()(20, 10) == true, "");
static_assert(predicate_ge<int, int64_t>()(5, 10) == false, "");
static_assert(predicate_ge<int, int64_t>()(10, 10) == true, "");

static_assert(predicate_compare<int, int64_t>::valid == true, "");
static_assert(predicate_compare<short, int64_t>::valid == true, "");
static_assert(predicate_compare<string, string>::valid == true, "");
static_assert(predicate_compare<vector<int>, int64_t>::valid == false, "");
static_assert(predicate_compare<fc::optional<int>, int64_t>::valid == true, "");
static_assert(predicate_compare<fc::optional<short>, int64_t>::valid == true, "");
static_assert(predicate_compare<fc::optional<string>, string>::valid == true, "");
static_assert(predicate_lt<int, int64_t>::valid == true, "");
static_assert(predicate_lt<short, int64_t>::valid == true, "");
static_assert(predicate_lt<string, string>::valid == true, "");
static_assert(predicate_lt<vector<int>, int64_t>::valid == false, "");
static_assert(predicate_lt<fc::optional<int>, int64_t>::valid == true, "");
static_assert(predicate_lt<fc::optional<short>, int64_t>::valid == true, "");
static_assert(predicate_lt<fc::optional<string>, string>::valid == true, "");

static_assert(predicate_in<string, string>::valid == false, "");
static_assert(predicate_in<int, flat_set<string>>::valid == false, "");
static_assert(predicate_in<string, flat_set<string>>::valid == true, "");
static_assert(predicate_in<flat_set<string>, flat_set<string>>::valid == false, "");
static_assert(predicate_in<fc::optional<string>, flat_set<string>>::valid == true, "");
static_assert(predicate_not_in<string, string>::valid == false, "");
static_assert(predicate_not_in<int, flat_set<string>>::valid == false, "");
static_assert(predicate_not_in<string, flat_set<string>>::valid == true, "");
static_assert(predicate_not_in<flat_set<string>, flat_set<string>>::valid == false, "");
static_assert(predicate_not_in<fc::optional<string>, flat_set<string>>::valid == true, "");

static_assert(predicate_has_all<string, string>::valid == false, "");
static_assert(predicate_has_all<int, flat_set<string>>::valid == false, "");
static_assert(predicate_has_all<string, flat_set<string>>::valid == false, "");
static_assert(predicate_has_all<flat_set<string>, flat_set<string>>::valid == true, "");
static_assert(predicate_has_all<fc::optional<string>, flat_set<string>>::valid == false, "");
static_assert(predicate_has_all<fc::optional<flat_set<string>>, flat_set<string>>::valid == true, "");
static_assert(predicate_has_none<string, string>::valid == false, "");
static_assert(predicate_has_none<int, flat_set<string>>::valid == false, "");
static_assert(predicate_has_none<string, flat_set<string>>::valid == false, "");
static_assert(predicate_has_none<flat_set<string>, flat_set<string>>::valid == true, "");
static_assert(predicate_has_none<fc::optional<string>, flat_set<string>>::valid == false, "");
static_assert(predicate_has_none<fc::optional<flat_set<string>>, flat_set<string>>::valid == true, "");

#endif

} } // namespace graphene::protocol
