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

#include "restriction_predicate.hxx"
#include "sliced_lists.hxx"

namespace graphene { namespace protocol {

using result_type = object_restriction_predicate<operation>;

result_type get_restriction_predicate_list_3(size_t idx, vector<restriction> rs) {
   return typelist::runtime::dispatch(operation_list_3::list(), idx, [&rs] (auto t) -> result_type {
      using Op = typename decltype(t)::type;
      return [p=restrictions_to_predicate<Op>(std::move(rs), true)] (const operation& op) {
         FC_ASSERT(op.which() == operation::tag<Op>::value,
                   "Supplied operation is incorrect type for restriction predicate");
         return p(op.get<Op>());
      };
   });
}
} }
